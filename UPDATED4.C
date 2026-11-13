/*
 * 4.c - Student Records and Academic Analytics System
 *
 * Menu-driven program that stores student records in a dynamically
 * allocated array (malloc / realloc / free) and supports add, display,
 * search, update, delete, GPA summary and sorted display.
 *
 * DESIGN DECISIONS (documented as required)
 * -----------------------------------------
 * Sorting   : "Display sorted" sorts a COPY of the array (qsort on a
 *             temporary buffer). The master array keeps insertion order,
 *             so sorting never disturbs the stored records. Order is
 *             GPA descending, ties broken by roll number ascending.
 * Delete    : Removes the record and shifts later records left with
 *             memmove, so insertion order of the survivors is preserved.
 * Growth    : Capacity doubles when full. The doubling is checked for
 *             size_t overflow. realloc's result goes to a temporary
 *             pointer, so the original pointer stays valid on failure.
 * Names     : Read with fgets so spaces are allowed. Names longer than
 *             49 characters are rejected (never silently truncated).
 * Ties      : Highest / lowest GPA ties are resolved by smaller roll no.
 * Input     : Any invalid field cancels the current operation and the
 *             record is not modified (updates are applied atomically).
 *             End-of-input (Ctrl+D / Ctrl+Z) exits cleanly, freeing memory.
 *
 * Build: gcc -std=c11 -Wall -Wextra -o 4 4.c
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
 
/* ----------------------------- constants ------------------------------ */
 
#define NAME_SIZE            50             /* 49 chars + '\0'            */
#define NAME_MAX_LEN         (NAME_SIZE - 1)
#define MIN_AGE              16
#define MAX_AGE              100
#define MIN_GPA              0.0
#define MAX_GPA              4.0
#define LINE_BUF             128            /* input line buffer          */
#define MAX_INITIAL_CAPACITY 1000000        /* sanity cap for first alloc */
 
/* ------------------------------- types -------------------------------- */
 
typedef struct {
    int   roll;                 /* unique, positive */
    char  name[NAME_SIZE];
    int   age;
    float gpa;
} Student;
 
typedef struct {
    Student *data;
    size_t   count;             /* records stored   */
    size_t   capacity;          /* slots allocated  */
} StudentList;
 
typedef enum { IN_OK, IN_BLANK, IN_INVALID, IN_EOF } InputStatus;
 
static bool g_eof = false;      /* set when stdin is exhausted */
 
/* ------------------------- input / validation ------------------------- */
 
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}
 
/* Reads one trimmed line. Over-long lines are discarded and reported. */
static InputStatus get_input(const char *prompt, char *buf, size_t size,
                             bool allow_blank)
{
    printf("%s", prompt);
    fflush(stdout);
 
    if (!fgets(buf, (int)size, stdin)) return IN_EOF;
 
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else if (len == size - 1) {           /* line did not fit */
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
        printf("Error: input too long.\n");
        return IN_INVALID;
    }
 
    trim(buf);
    if (buf[0] == '\0') {
        if (allow_blank) return IN_BLANK;
        printf("Error: input cannot be empty.\n");
        return IN_INVALID;
    }
    return IN_OK;
}
 
static bool parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi)
        return false;
    *out = v;
    return true;
}
 
static bool parse_gpa(const char *s, float *out)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    if (!(v >= MIN_GPA && v <= MAX_GPA)) return false;   /* also rejects NaN */
    *out = (float)v;
    return true;
}
 
static InputStatus read_int_field(const char *prompt, long lo, long hi,
                                  bool allow_blank, int *out,
                                  const char *errmsg)
{
    char buf[LINE_BUF];
    long v;
    InputStatus st = get_input(prompt, buf, sizeof buf, allow_blank);
    if (st != IN_OK) return st;
    if (!parse_long(buf, lo, hi, &v)) {
        printf("Error: %s\n", errmsg);
        return IN_INVALID;
    }
    *out = (int)v;
    return IN_OK;
}
 
static InputStatus read_roll(const char *prompt, int *out)
{
    return read_int_field(prompt, 1, INT_MAX, false, out,
                          "roll number must be a positive integer.");
}
 
static InputStatus read_age(const char *prompt, bool allow_blank, int *out)
{
    return read_int_field(prompt, MIN_AGE, MAX_AGE, allow_blank, out,
                          "age must be an integer between 16 and 100.");
}
 
static InputStatus read_gpa(const char *prompt, bool allow_blank, float *out)
{
    char buf[LINE_BUF];
    InputStatus st = get_input(prompt, buf, sizeof buf, allow_blank);
    if (st != IN_OK) return st;
    if (!parse_gpa(buf, out)) {
        printf("Error: GPA must be a number between 0.00 and 4.00.\n");
        return IN_INVALID;
    }
    return IN_OK;
}
 
static InputStatus read_name(const char *prompt, bool allow_blank, char *out)
{
    char buf[LINE_BUF];
    InputStatus st = get_input(prompt, buf, sizeof buf, allow_blank);
    if (st != IN_OK) return st;
    if (strlen(buf) > NAME_MAX_LEN) {
        printf("Error: name exceeds %d characters.\n", NAME_MAX_LEN);
        return IN_INVALID;
    }
    strcpy(out, buf);
    return IN_OK;
}
 
/* Common handling after a field read. Returns true if the caller must
 * abort the current operation. */
static bool field_failed(InputStatus st)
{
    if (st == IN_EOF) { g_eof = true; return true; }
    if (st == IN_INVALID) { printf("Operation cancelled.\n"); return true; }
    return false;
}
 
/* --------------------------- memory management ------------------------ */
 
static size_t max_capacity(void)
{
    return SIZE_MAX / sizeof(Student);
}
 
/* Allocates the initial array. Returns false on bad size or no memory. */
static bool list_init(StudentList *l, long requested)
{
    l->data = NULL;
    l->count = 0;
    l->capacity = 0;
 
    if (requested <= 0) {
        printf("Error: capacity must be at least 1.\n");
        return false;
    }
    if ((unsigned long)requested > MAX_INITIAL_CAPACITY ||
        (size_t)requested > max_capacity()) {
        printf("Error: capacity too large (maximum %d).\n",
               MAX_INITIAL_CAPACITY);
        return false;
    }
    l->data = malloc((size_t)requested * sizeof(Student));
    if (!l->data) {
        printf("Error: not enough memory for %ld records.\n", requested);
        return false;
    }
    l->capacity = (size_t)requested;
    return true;
}
 
/* Doubles capacity (overflow-safe). On failure the list is unchanged. */
static bool list_grow(StudentList *l)
{
    size_t limit = max_capacity();
    size_t new_cap;
 
    if (l->capacity >= limit) {
        printf("Error: maximum capacity reached.\n");
        return false;
    }
    new_cap = (l->capacity > limit / 2) ? limit : l->capacity * 2;
 
    Student *tmp = realloc(l->data, new_cap * sizeof(Student));
    if (!tmp) {                         /* l->data still valid */
        printf("Error: out of memory while expanding to %zu records.\n",
               new_cap);
        return false;
    }
    l->data = tmp;
    l->capacity = new_cap;
    printf("(Capacity expanded to %zu.)\n", new_cap);
    return true;
}
 
static void list_free(StudentList *l)
{
    free(l->data);
    l->data = NULL;
    l->count = l->capacity = 0;
}
 
/* ----------------------------- record logic --------------------------- */
 
static bool find_index(const StudentList *l, int roll, size_t *idx)
{
    for (size_t i = 0; i < l->count; i++) {
        if (l->data[i].roll == roll) {
            if (idx) *idx = i;
            return true;
        }
    }
    return false;
}
 
static bool list_add(StudentList *l, const Student *s)
{
    if (find_index(l, s->roll, NULL)) {
        printf("Error: roll number %d already exists.\n", s->roll);
        return false;
    }
    if (l->count == l->capacity && !list_grow(l)) return false;
    l->data[l->count++] = *s;
    return true;
}
 
static void list_delete_at(StudentList *l, size_t idx)
{
    memmove(&l->data[idx], &l->data[idx + 1],
            (l->count - idx - 1) * sizeof(Student));
    l->count--;
}
 
/* ------------------------------ display ------------------------------- */
 
static void print_header(void)
{
    printf("%-8s %-*s %-5s %-5s\n", "Roll", NAME_MAX_LEN, "Name", "Age", "GPA");
}
 
static void print_student(const Student *s)
{
    printf("%-8d %-*s %-5d %.2f\n", s->roll, NAME_MAX_LEN, s->name,
           s->age, s->gpa);
}
 
static void display_all(const Student *arr, size_t n)
{
    print_header();
    for (size_t i = 0; i < n; i++) print_student(&arr[i]);
}
 
/* ------------------------------ sorting ------------------------------- */
 
static int cmp_gpa_desc_roll_asc(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    if (x->gpa > y->gpa) return -1;
    if (x->gpa < y->gpa) return 1;
    return (x->roll > y->roll) - (x->roll < y->roll);
}
 
/* Returns a freshly allocated sorted copy (caller frees), or NULL. */
static Student *sorted_copy(const StudentList *l)
{
    Student *copy = malloc(l->count * sizeof(Student));
    if (!copy) return NULL;
    memcpy(copy, l->data, l->count * sizeof(Student));
    qsort(copy, l->count, sizeof(Student), cmp_gpa_desc_roll_asc);
    return copy;
}
 
/* ------------------------------ summary ------------------------------- */
 
static void report_summary(const StudentList *l)
{
    if (l->count == 0) {
        printf("No students stored; GPA summary is not available.\n");
        return;
    }
    size_t hi = 0, lo = 0;
    double sum = 0.0;
 
    for (size_t i = 0; i < l->count; i++) {
        const Student *s = &l->data[i];
        sum += s->gpa;
        if (s->gpa > l->data[hi].gpa ||
            (s->gpa == l->data[hi].gpa && s->roll < l->data[hi].roll)) hi = i;
        if (s->gpa < l->data[lo].gpa ||
            (s->gpa == l->data[lo].gpa && s->roll < l->data[lo].roll)) lo = i;
    }
    printf("Highest GPA:\n");
    print_header();
    print_student(&l->data[hi]);
    printf("Lowest GPA:\n");
    print_header();
    print_student(&l->data[lo]);
    printf("Average GPA: %.2f (over %zu student%s)\n", sum / (double)l->count,
           l->count, l->count == 1 ? "" : "s");
}
 
/* ----------------------------- menu actions --------------------------- */
 
static void op_add(StudentList *l)
{
    Student s;
    memset(&s, 0, sizeof s);
 
    if (field_failed(read_roll("Roll number: ", &s.roll))) return;
    if (find_index(l, s.roll, NULL)) {            /* fail early */
        printf("Error: roll number %d already exists.\n", s.roll);
        return;
    }
    if (field_failed(read_name("Name (max 49 chars): ", false, s.name))) return;
    if (field_failed(read_age("Age (16-100): ", false, &s.age))) return;
    if (field_failed(read_gpa("GPA (0.00-4.00): ", false, &s.gpa))) return;
 
    if (list_add(l, &s))
        printf("Success: student %d added.\n", s.roll);
}
 
static void op_display(const StudentList *l)
{
    if (l->count == 0) { printf("No students stored.\n"); return; }
    display_all(l->data, l->count);
}
 
static void op_search(const StudentList *l)
{
    int roll;
    size_t idx;
    if (field_failed(read_roll("Roll number to search: ", &roll))) return;
    if (!find_index(l, roll, &idx)) {
        printf("Error: roll number %d not found.\n", roll);
        return;
    }
    print_header();
    print_student(&l->data[idx]);
}
 
static void op_update(StudentList *l)
{
    int roll;
    size_t idx;
    Student upd;
    InputStatus st;
 
    if (field_failed(read_roll("Roll number to update: ", &roll))) return;
    if (!find_index(l, roll, &idx)) {
        printf("Error: roll number %d not found.\n", roll);
        return;
    }
    upd = l->data[idx];                 /* stage changes on a copy */
    printf("Current record:\n");
    print_header();
    print_student(&upd);
    printf("(Press Enter to keep the current value.)\n");
 
    st = read_name("New name: ", true, upd.name);
    if (field_failed(st)) return;
    st = read_age("New age (16-100): ", true, &upd.age);
    if (field_failed(st)) return;
    st = read_gpa("New GPA (0.00-4.00): ", true, &upd.gpa);
    if (field_failed(st)) return;
 
    l->data[idx] = upd;                 /* commit atomically */
    printf("Success: student %d updated.\n", roll);
}
 
static void op_delete(StudentList *l)
{
    int roll;
    size_t idx;
    if (field_failed(read_roll("Roll number to delete: ", &roll))) return;
    if (!find_index(l, roll, &idx)) {
        printf("Error: roll number %d not found.\n", roll);
        return;
    }
    list_delete_at(l, idx);
    printf("Success: student %d deleted.\n", roll);
}
 
static void op_sorted(const StudentList *l)
{
    if (l->count == 0) { printf("No students stored.\n"); return; }
    Student *copy = sorted_copy(l);
    if (!copy) {
        printf("Error: not enough memory to sort.\n");
        return;
    }
    printf("Sorted by GPA (descending), ties by roll number (ascending):\n");
    display_all(copy, l->count);
    free(copy);
}
 
/* -------------------------------- main -------------------------------- */
 
static void print_menu(void)
{
    printf("\n===== Student Records System =====\n"
           "1. Add student\n"
           "2. Display all students\n"
           "3. Search by roll number\n"
           "4. Update student\n"
           "5. Delete student\n"
           "6. GPA summary (highest / lowest / average)\n"
           "7. Display sorted by GPA\n"
           "0. Exit\n");
}
 
int main(void)
{
    StudentList list = { NULL, 0, 0 };
 
    /* Initial capacity: re-prompt until valid and allocatable. */
    for (;;) {
        char buf[LINE_BUF];
        long cap;
        InputStatus st = get_input("Enter initial capacity: ", buf,
                                   sizeof buf, false);
        if (st == IN_EOF) return 1;
        if (st != IN_OK) continue;
        if (!parse_long(buf, LONG_MIN, LONG_MAX, &cap)) {
            printf("Error: capacity must be an integer.\n");
            continue;
        }
        if (list_init(&list, cap)) break;
    }
    printf("Allocated space for %zu students.\n", list.capacity);
 
    while (!g_eof) {
        int choice;
        print_menu();
        InputStatus st = read_int_field("Choice: ", 0, 7, false, &choice,
                                        "choose a number from 0 to 7.");
        if (st == IN_EOF) break;
        if (st != IN_OK) continue;
        if (choice == 0) break;
 
        switch (choice) {
        case 1: op_add(&list);          break;
        case 2: op_display(&list);      break;
        case 3: op_search(&list);       break;
        case 4: op_update(&list);       break;
        case 5: op_delete(&list);       break;
        case 6: report_summary(&list);  break;
        case 7: op_sorted(&list);       break;
        }
    }
 
    list_free(&list);
    printf("\nMemory released. Goodbye.\n");
    return 0;
}
 
