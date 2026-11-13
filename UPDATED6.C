/*
 * 6.c - Memory-Efficient Typed Records and Safe Union Access
 *
 * Part 1: memory-layout analysis of a struct and a union with matching
 *         members (sizes, alignments, offsetof, member addresses).
 * Part 2: a tagged record (enum tag + union) with validated setters and a
 *         tag-driven printer.
 *
 * SAFETY RULES FOLLOWED
 * ---------------------
 *  - A union member is read ONLY immediately after that same member was
 *    written, or through the tag that says it is the active member.
 *    Inactive members are never read as if they still held old values.
 *  - To show that union members overlap, the raw object bytes are viewed
 *    through `unsigned char *` (always permitted) - never by reading an
 *    overwritten member as a different type.
 *  - Text is never printed as a C string unless a '\0' is verified to
 *    exist inside the 20-byte array.
 *  - All sizes/alignments/offsets are printed with %zu, pointers with %p
 *    after a cast to (void *). Nothing assumes a specific platform layout.
 *
 * Build: gcc -std=c11 -Wall -Wextra -o 6 6.c
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>     /* offsetof, size_t */
#include <math.h>       /* isfinite */
#include <stdbool.h>
 
#define TEXT_CAPACITY 20            /* bytes, including the terminating '\0' */
#define MAX_TEXT_LEN  (TEXT_CAPACITY - 1)
#define BULK_COUNT    100
 
/* ------------------------------- types -------------------------------- */
 
typedef union {
    int   i;
    float f;
    char  str[TEXT_CAPACITY];
} UnionData;
 
typedef struct {
    int   i;
    float f;
    char  str[TEXT_CAPACITY];
} StructData;
 
/* Extra struct whose members DO force padding, to illustrate it. */
typedef struct {
    char   a;
    double b;
    char   c;
} PaddingDemo;
 
typedef enum ValueType {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_TEXT,
    VALUE_EMPTY                     /* nothing stored yet */
} ValueType;
 
typedef struct {
    ValueType type;                 /* says which member of value is valid */
    UnionData value;
} TaggedValue;
 
typedef enum {
    SET_OK,
    SET_ERR_NULL_ARG,
    SET_ERR_TOO_LONG,
    SET_ERR_NOT_FINITE
} SetResult;
 
_Static_assert(sizeof(UnionData) >= TEXT_CAPACITY,
               "union must be able to hold the text buffer");
_Static_assert(sizeof(UnionData) <= sizeof(StructData),
               "a union is never larger than the matching struct");
 
/* ------------------------------ helpers ------------------------------- */
 
static void section(const char *title)
{
    printf("\n=== %s ===\n", title);
}
 
static void print_bytes(const char *label, const void *obj, size_t n)
{
    const unsigned char *p = (const unsigned char *)obj;  /* always legal */
    printf("  %-28s", label);
    for (size_t k = 0; k < n; k++) printf("%02x ", p[k]);
    printf("\n");
}
 
/* ----------------------- Part 1: layout analysis ---------------------- */
 
static void report_sizes_and_alignment(void)
{
    section("1. Size and alignment (this platform)");
    printf("  %-14s size = %2zu  alignment = %zu\n", "UnionData",
           sizeof(UnionData), _Alignof(UnionData));
    printf("  %-14s size = %2zu  alignment = %zu\n", "StructData",
           sizeof(StructData), _Alignof(StructData));
    printf("  %-14s size = %2zu  alignment = %zu\n", "TaggedValue",
           sizeof(TaggedValue), _Alignof(TaggedValue));
    printf("  %-14s size = %2zu  alignment = %zu\n", "PaddingDemo",
           sizeof(PaddingDemo), _Alignof(PaddingDemo));
    printf("  (member alignments: int = %zu, float = %zu, char[] = %zu)\n",
           _Alignof(int), _Alignof(float), _Alignof(char[TEXT_CAPACITY]));
}
 
static void report_offsets(void)
{
    section("2. Field offsets (offsetof) and padding");
 
    size_t off_i = offsetof(StructData, i);
    size_t off_f = offsetof(StructData, f);
    size_t off_s = offsetof(StructData, str);
    size_t end_s = off_s + sizeof(((StructData *)0)->str);
 
    printf("  StructData: offset(i) = %zu, offset(f) = %zu, offset(str) = %zu\n",
           off_i, off_f, off_s);
    printf("    padding before f   : %zu byte(s)\n",
           off_f - (off_i + sizeof(int)));
    printf("    padding before str : %zu byte(s)\n",
           off_s - (off_f + sizeof(float)));
    printf("    trailing padding   : %zu byte(s)\n",
           sizeof(StructData) - end_s);
 
    printf("  PaddingDemo {char a; double b; char c;}:\n");
    printf("    offset(a) = %zu, offset(b) = %zu, offset(c) = %zu\n",
           offsetof(PaddingDemo, a), offsetof(PaddingDemo, b),
           offsetof(PaddingDemo, c));
    printf("    members total %zu byte(s) but sizeof = %zu -> %zu byte(s) "
           "of padding\n",
           sizeof(char) + sizeof(double) + sizeof(char), sizeof(PaddingDemo),
           sizeof(PaddingDemo) - (sizeof(char) + sizeof(double) + sizeof(char)));
 
    printf("  Each offset is a multiple of that member's alignment, and the\n"
           "  struct size is a multiple of the struct's alignment.\n");
}
 
static void report_addresses(void)
{
    section("3. Member addresses: union overlaps, struct does not");
 
    UnionData  u;
    StructData s;
    memset(&u, 0, sizeof u);
    memset(&s, 0, sizeof s);
 
    printf("  UnionData object at %p\n", (void *)&u);
    printf("    &u.i   = %p\n", (void *)&u.i);
    printf("    &u.f   = %p\n", (void *)&u.f);
    printf("    &u.str = %p\n", (void *)&u.str);
    bool same = ((void *)&u.i == (void *)&u.f) &&
                ((void *)&u.f == (void *)&u.str);
    printf("    all union members start at the same address: %s\n",
           same ? "yes" : "no");
 
    printf("  StructData object at %p\n", (void *)&s);
    printf("    &s.i   = %p  (offset %zu)\n", (void *)&s.i,
           offsetof(StructData, i));
    printf("    &s.f   = %p  (offset %zu)\n", (void *)&s.f,
           offsetof(StructData, f));
    printf("    &s.str = %p  (offset %zu)\n", (void *)&s.str,
           offsetof(StructData, str));
    bool distinct = ((void *)&s.i != (void *)&s.f) &&
                    ((void *)&s.f != (void *)&s.str) &&
                    ((void *)&s.i != (void *)&s.str);
    printf("    all struct members have distinct addresses: %s\n",
           distinct ? "yes" : "no");
}
 
static void demo_assignments(void)
{
    section("4. Assigning values one at a time");
 
    StructData s;
    memset(&s, 0, sizeof s);
    s.i = 10;
    s.f = 220.5f;
    snprintf(s.str, sizeof s.str, "%s", "Struct Programming");
    printf("  struct (all three coexist): i = %d, f = %.2f, str = \"%s\"\n",
           s.i, s.f, s.str);
 
    UnionData u;
    memset(&u, 0, sizeof u);
 
    u.i = 10;
    printf("  union after u.i = 10        : u.i = %d\n", u.i);
    print_bytes("raw bytes (first sizeof int):", &u, sizeof(int));
 
    snprintf(u.str, sizeof u.str, "%s", "Union Programming");
    printf("  union after u.str = text    : u.str = \"%s\"\n", u.str);
 
    u.f = 220.5f;
    printf("  union after u.f = 220.5     : u.f = %.2f\n", u.f);
    print_bytes("raw bytes (first sizeof int):", &u, sizeof(int));
 
    printf("  The first bytes changed when u.f was written: the integer and\n"
           "  float share storage. u.i and u.str are NOT read again because\n"
           "  they are no longer the active member.\n");
}
 
static void report_bulk_memory(void)
{
    section("5. Memory for 100 records");
 
    size_t s_bytes = sizeof(StructData[BULK_COUNT]);
    size_t u_bytes = sizeof(UnionData[BULK_COUNT]);
    size_t t_bytes = sizeof(TaggedValue[BULK_COUNT]);
 
    printf("  %d x StructData  = %zu bytes\n", BULK_COUNT, s_bytes);
    printf("  %d x UnionData   = %zu bytes\n", BULK_COUNT, u_bytes);
    printf("  %d x TaggedValue = %zu bytes\n", BULK_COUNT, t_bytes);
    printf("  union saves %zu bytes vs struct (%.1f%%)\n",
           s_bytes - u_bytes,
           100.0 * (double)(s_bytes - u_bytes) / (double)s_bytes);
    printf("  tagged record saves %zu bytes vs struct (%.1f%%), and costs "
           "%zu extra bytes vs the bare union\n",
           s_bytes - t_bytes,
           100.0 * (double)(s_bytes - t_bytes) / (double)s_bytes,
           t_bytes - u_bytes);
}
 
/* ---------------------- Part 2: tagged record API --------------------- */
 
static void tagged_init(TaggedValue *t)
{
    if (!t) return;
    memset(&t->value, 0, sizeof t->value);
    t->type = VALUE_EMPTY;
}
 
static SetResult tagged_set_int(TaggedValue *t, int v)
{
    if (!t) return SET_ERR_NULL_ARG;
    memset(&t->value, 0, sizeof t->value);      /* drop old contents */
    t->value.i = v;
    t->type = VALUE_INT;
    return SET_OK;
}
 
static SetResult tagged_set_float(TaggedValue *t, float v)
{
    if (!t) return SET_ERR_NULL_ARG;
    if (!isfinite(v)) return SET_ERR_NOT_FINITE;
    memset(&t->value, 0, sizeof t->value);
    t->value.f = v;
    t->type = VALUE_FLOAT;
    return SET_OK;
}
 
/* On failure the record is left completely unchanged. */
static SetResult tagged_set_text(TaggedValue *t, const char *s)
{
    if (!t || !s) return SET_ERR_NULL_ARG;
    size_t len = strlen(s);
    if (len > MAX_TEXT_LEN) return SET_ERR_TOO_LONG;   /* needs len+1 bytes */
    memset(&t->value, 0, sizeof t->value);
    memcpy(t->value.str, s, len + 1);                   /* incl. '\0' */
    t->type = VALUE_TEXT;
    return SET_OK;
}
 
static const char *set_result_name(SetResult r)
{
    switch (r) {
    case SET_OK:             return "ok";
    case SET_ERR_NULL_ARG:   return "rejected (null argument)";
    case SET_ERR_TOO_LONG:   return "rejected (does not fit in 20 bytes "
                                    "with terminator)";
    case SET_ERR_NOT_FINITE: return "rejected (NaN or infinity)";
    }
    return "unknown result";
}
 
static const char *type_name(ValueType t)
{
    switch (t) {
    case VALUE_INT:   return "INT";
    case VALUE_FLOAT: return "FLOAT";
    case VALUE_TEXT:  return "TEXT";
    case VALUE_EMPTY: return "EMPTY";
    }
    return "INVALID";
}
 
/* Prints ONLY the active member, chosen by the tag. No newline. */
static void tagged_print(const TaggedValue *t)
{
    if (!t) { printf("[null record]"); return; }
 
    switch (t->type) {
    case VALUE_INT:
        printf("[INT]   %d", t->value.i);
        break;
    case VALUE_FLOAT:
        printf("[FLOAT] %.2f", t->value.f);
        break;
    case VALUE_TEXT:
        /* Only treat as a C string if a terminator really exists. */
        if (memchr(t->value.str, '\0', sizeof t->value.str))
            printf("[TEXT]  \"%s\"", t->value.str);
        else
            printf("[TEXT]  <corrupt: no terminator, not printed>");
        break;
    case VALUE_EMPTY:
        printf("[EMPTY] (no value stored)");
        break;
    default:
        printf("[INVALID TAG %d] value not read", (int)t->type);
        break;
    }
}
 
/* ----------------------------- Part 2 demos --------------------------- */
 
static void demo_records(void)
{
    section("6. Five tagged records of mixed types");
 
    TaggedValue recs[5];
    for (size_t k = 0; k < 5; k++) tagged_init(&recs[k]);
 
    tagged_set_int(&recs[0], 10);
    tagged_set_float(&recs[1], 220.5f);
    tagged_set_text(&recs[2], "Union Programming");
    tagged_set_int(&recs[3], 42);
    tagged_set_text(&recs[4], "Hello");
 
    for (size_t k = 0; k < 5; k++) {
        printf("  record %zu: ", k + 1);
        tagged_print(&recs[k]);
        printf("\n");
    }
 
    printf("  sizeof(TaggedValue) = %zu, sizeof(UnionData) = %zu, "
           "sizeof(StructData) = %zu\n",
           sizeof(TaggedValue), sizeof(UnionData), sizeof(StructData));
}
 
static void try_text(TaggedValue *t, const char *label, const char *s)
{
    SetResult r = tagged_set_text(t, s);
    printf("  set text (%s, %zu chars): %s -> ", label,
           s ? strlen(s) : (size_t)0, set_result_name(r));
    tagged_print(t);
    printf("\n");
}
 
static void run_edge_tests(void)
{
    section("7. Edge-case tests");
 
    TaggedValue t;
    tagged_init(&t);
    printf("  fresh record: ");
    tagged_print(&t);
    printf("\n");
 
    /* Maximum valid string: 19 characters + '\0' = 20 bytes. */
    try_text(&t, "maximum valid", "ABCDEFGHIJKLMNOPQRS");
    /* Overlength: the previous valid value must survive. */
    try_text(&t, "overlength",    "ABCDEFGHIJKLMNOPQRST");
    try_text(&t, "very long",     "This string is much longer than twenty");
    try_text(&t, "empty",         "");
    try_text(&t, "NULL pointer",  NULL);
 
    printf("  -- switching ONE record: int -> text -> float --\n");
    tagged_set_int(&t, 7);
    printf("  after set int  : "); tagged_print(&t); printf("\n");
    tagged_set_text(&t, "seven");
    printf("  after set text : "); tagged_print(&t); printf("\n");
    tagged_set_float(&t, 7.25f);
    printf("  after set float: "); tagged_print(&t); printf("\n");
 
    printf("  -- float validation --\n");
    SetResult r = tagged_set_float(&t, NAN);
    printf("  set float NaN  : %s -> ", set_result_name(r));
    tagged_print(&t); printf("\n");
    r = tagged_set_float(&t, INFINITY);
    printf("  set float inf  : %s -> ", set_result_name(r));
    tagged_print(&t); printf("\n");
    r = tagged_set_int(NULL, 1);
    printf("  set int on NULL record: %s\n", set_result_name(r));
 
    printf("  -- invalid tag handling --\n");
    tagged_set_int(&t, 99);
    t.type = (ValueType)99;                     /* simulate corruption */
    printf("  tag forced to 99 (%s): ", type_name(t.type));
    tagged_print(&t); printf("\n");
 
    printf("  -- unterminated text under a TEXT tag --\n");
    t.type = VALUE_TEXT;
    memset(t.value.str, 'X', sizeof t.value.str);   /* no '\0' anywhere */
    printf("  corrupted text record: ");
    tagged_print(&t); printf("\n");
 
    tagged_print(NULL);
    printf("  <- printing a NULL record\n");
}
 
static void print_observations(void)
{
    section("8. Observations");
    printf(
"  * Union members share one block of memory, sized for the largest member\n"
"    (rounded up to the union's alignment). A struct gives every member its\n"
"    own storage, so its size is at least the sum of the member sizes.\n"
"  * Writing one union member overwrites the others. Only the most\n"
"    recently written member holds a meaningful value, so a union cannot\n"
"    keep an int, a float and a string at the same time.\n"
"  * That limit is what makes a union cheaper: when a record holds exactly\n"
"    ONE of several types at a time, one shared block is enough.\n"
"  * The union alone does not remember which member is valid. The enum tag\n"
"    records that, and every read goes through it. The tag costs a few\n"
"    bytes (plus any alignment padding) but makes access safe.\n"
"  * Padding and alignment are platform-dependent: always use sizeof,\n"
"    _Alignof and offsetof instead of assuming a layout.\n");
}
 
/* -------------------------------- main -------------------------------- */
 
int main(void)
{
    report_sizes_and_alignment();
    report_offsets();
    report_addresses();
    demo_assignments();
    report_bulk_memory();
    demo_records();
    run_edge_tests();
    print_observations();
    return 0;
}
