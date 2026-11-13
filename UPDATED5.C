*
 * 5.c - Rectangle Inventory and Geometry Analysis
 *
 * Interactive program that stores rectangles in a dynamically allocated
 * array and supports add, display, search, update, delete, area
 * statistics, sorted display and an area-threshold filter.
 *
 * DOCUMENTED DESIGN DECISIONS
 * ---------------------------
 * Initial count : 1 .. 1000 (MAX_INITIAL_COUNT). Anything else is rejected.
 * Precision     : Dimensions, area and perimeter are stored as double
 *                 (the original demo used float; double avoids needless
 *                 rounding error in comparisons and sums).
 * Dimensions    : Must be finite and within [0.01, 1,000,000]. NaN, inf,
 *                 zero, negatives and out-of-range values are rejected.
 *                 The bounds guarantee area and perimeter can never
 *                 overflow, underflow to zero, or become inf/NaN.
 * Consistency   : Area and perimeter are ONLY ever written by
 *                 rect_set_dimensions(), which is the single place that
 *                 sets width/height, so derived fields cannot go stale.
 * Square test   : |w - h| <= SQUARE_TOLERANCE * max(w, h), with a RELATIVE
 *                 tolerance of 1e-9. Exact equality is never used.
 * Capacity      : Doubles (overflow-checked) when full. Halves when the
 *                 count falls to <= 1/4 of capacity, but never below the
 *                 initial capacity. realloc results go to a temporary
 *                 pointer, so a failed resize keeps the old block valid
 *                 (a failed grow rejects the add; a failed shrink is
 *                 ignored because it is only an optimisation).
 * Ties          : Largest/smallest area ties go to the smaller ID.
 * Sorting       : "Sorted" works on a COPY (ascending area, ties by
 *                 ascending ID); the stored order is not changed.
 * Input         : An invalid field cancels the whole operation, leaving
 *                 data untouched. Updates are staged and committed
 *                 atomically. End-of-input exits cleanly and frees memory.
 *
 * Build:                gcc -std=c11 -Wall -Wextra -o 5 5.c -lm
 * Test realloc failure: gcc -DSIMULATE_REALLOC_FAILURE ... (see rect_realloc)
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
 
/* ----------------------------- constants ------------------------------ */
 
#define MAX_INITIAL_COUNT   1000
#define MIN_DIMENSION       0.01
#define MAX_DIMENSION       1000000.0
#define SQUARE_TOLERANCE    1e-9
#define LINE_BUF            128
 
/* ------------------------------- types -------------------------------- */
 
typedef struct {
    int    id;                  /* unique, positive */
    double width;
    double height;
    double area;                /* derived */
    double perimeter;           /* derived */
} Rectangle;
 
typedef struct {
    Rectangle *data;
    size_t     count;
    size_t     capacity;
    size_t     min_capacity;    /* never shrink below this */
} RectList;
 
typedef enum { IN_OK, IN_BLANK, IN_INVALID, IN_EOF } InputStatus;
 
static bool g_eof = false;
 
/* Test hook: lets us prove failed-resize handling. */
#ifdef SIMULATE_REALLOC_FAILURE
static void *rect_realloc(void *p, size_t n) { (void)p; (void)n; return NULL; }
#else
#define rect_realloc realloc
#endif
 
/* ------------------------- input / validation ------------------------- */
 
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}
 
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
 
/* Parses a finite double. Rejects NaN, inf, trailing junk, overflow. */
static bool parse_finite_double(const char *s, double *out)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    if (!isfinite(v)) return false;          /* NaN, inf, ERANGE overflow */
    *out = v;
    return true;
}
 
static InputStatus read_long_field(const char *prompt, long lo, long hi,
                                   long *out, const char *errmsg)
{
    char buf[LINE_BUF];
    InputStatus st = get_input(prompt, buf, sizeof buf, false);
    if (st != IN_OK) return st;
    if (!parse_long(buf, lo, hi, out)) {
        printf("Error: %s\n", errmsg);
        return IN_INVALID;
    }
    return IN_OK;
}
 
static InputStatus read_id(const char *prompt, int *out)
{
    long v;
    InputStatus st = read_long_field(prompt, 1, INT_MAX, &v,
                                     "ID must be a positive integer.");
    if (st == IN_OK) *out = (int)v;
    return st;
}
 
static InputStatus read_dimension(const char *prompt, bool allow_blank,
                                  double *out)
{
    char buf[LINE_BUF];
    double v;
    InputStatus st = get_input(prompt, buf, sizeof buf, allow_blank);
    if (st != IN_OK) return st;
    if (!parse_finite_double(buf, &v)) {
        printf("Error: value must be a finite number (NaN and infinity "
               "are not allowed).\n");
        return IN_INVALID;
    }
    if (v <= 0.0) {
        printf("Error: dimension must be positive.\n");
        return IN_INVALID;
    }
    if (v < MIN_DIMENSION || v > MAX_DIMENSION) {
        printf("Error: dimension must be between %.2f and %.0f.\n",
               MIN_DIMENSION, MAX_DIMENSION);
        return IN_INVALID;
    }
    *out = v;
    return IN_OK;
}
 
static InputStatus read_threshold(const char *prompt, double *out)
{
    char buf[LINE_BUF];
    double v;
    InputStatus st = get_input(prompt, buf, sizeof buf, false);
    if (st != IN_OK) return st;
    if (!parse_finite_double(buf, &v) || v < 0.0) {
        printf("Error: threshold must be a finite number >= 0.\n");
        return IN_INVALID;
    }
    *out = v;
    return IN_OK;
}
 
static bool field_failed(InputStatus st)
{
    if (st == IN_EOF) { g_eof = true; return true; }
    if (st == IN_INVALID) { printf("Operation cancelled.\n"); return true; }
    return false;
}
 
/* ---------------------------- geometry logic -------------------------- */
 
/* The ONLY writer of width/height/area/perimeter. */
static void rect_set_dimensions(Rectangle *r, double w, double h)
{
    r->width = w;
    r->height = h;
    r->area = w * h;
    r->perimeter = 2.0 * (w + h);
}
 
static bool rect_is_square(const Rectangle *r)
{
    double larger = fmax(r->width, r->height);
    return fabs(r->width - r->height) <= SQUARE_TOLERANCE * larger;
}
 
/* --------------------------- memory management ------------------------ */
 
static size_t max_capacity(void)
{
    return SIZE_MAX / sizeof(Rectangle);
}
 
static bool list_init(RectList *l, long requested)
{
    l->data = NULL;
    l->count = l->capacity = l->min_capacity = 0;
 
    if (requested < 1 || requested > MAX_INITIAL_COUNT) {
        printf("Error: number of rectangles must be between 1 and %d.\n",
               MAX_INITIAL_COUNT);
        return false;
    }
    l->data = malloc((size_t)requested * sizeof(Rectangle));
    if (!l->data) {
        printf("Error: not enough memory for %ld rectangles.\n", requested);
        return false;
    }
    l->capacity = l->min_capacity = (size_t)requested;
    return true;
}
 
static bool list_grow(RectList *l)
{
    size_t limit = max_capacity();
    if (l->capacity >= limit) {
        printf("Error: maximum capacity reached.\n");
        return false;
    }
    size_t new_cap = (l->capacity > limit / 2) ? limit : l->capacity * 2;
 
    Rectangle *tmp = rect_realloc(l->data, new_cap * sizeof(Rectangle));
    if (!tmp) {                              /* old block untouched */
        printf("Error: out of memory while expanding to %zu rectangles.\n",
               new_cap);
        return false;
    }
    l->data = tmp;
    l->capacity = new_cap;
    printf("(Capacity expanded to %zu.)\n", new_cap);
    return true;
}
 
/* Optional optimisation: failure is harmless, so it is silent. */
static void list_maybe_shrink(RectList *l)
{
    if (l->capacity / 2 < l->min_capacity) return;
    if (l->count > l->capacity / 4) return;
 
    size_t new_cap = l->capacity / 2;
    Rectangle *tmp = rect_realloc(l->data, new_cap * sizeof(Rectangle));
    if (!tmp) return;
    l->data = tmp;
    l->capacity = new_cap;
    printf("(Capacity reduced to %zu.)\n", new_cap);
}
 
static void list_free(RectList *l)
{
    free(l->data);
    l->data = NULL;
    l->count = l->capacity = l->min_capacity = 0;
}
 
/* ----------------------------- list operations ------------------------ */
 
static bool find_index(const RectList *l, int id, size_t *idx)
{
    for (size_t i = 0; i < l->count; i++) {
        if (l->data[i].id == id) {
            if (idx) *idx = i;
            return true;
        }
    }
    return false;
}
 
static bool list_add(RectList *l, int id, double w, double h)
{
    if (find_index(l, id, NULL)) {
        printf("Error: ID %d already exists.\n", id);
        return false;
    }
    if (l->count == l->capacity && !list_grow(l)) return false;
 
    Rectangle *r = &l->data[l->count];
    r->id = id;
    rect_set_dimensions(r, w, h);
    l->count++;
    return true;
}
 
static bool list_update_at(RectList *l, size_t idx, double w, double h)
{
    if (idx >= l->count) {                   /* invalid index */
        printf("Error: invalid index %zu.\n", idx);
        return false;
    }
    rect_set_dimensions(&l->data[idx], w, h);
    return true;
}
 
static bool list_delete_at(RectList *l, size_t idx)
{
    if (idx >= l->count) {                   /* invalid index */
        printf("Error: invalid index %zu.\n", idx);
        return false;
    }
    memmove(&l->data[idx], &l->data[idx + 1],
            (l->count - idx - 1) * sizeof(Rectangle));
    l->count--;
    list_maybe_shrink(l);
    return true;
}
 
/* ------------------------------- display ------------------------------ */
 
static void print_header(void)
{
    printf("%-8s %-14s %-14s %-16s %-16s %s\n",
           "ID", "Width", "Height", "Area", "Perimeter", "Shape");
}
 
static void print_rect(const Rectangle *r)
{
    printf("%-8d %-14.4f %-14.4f %-16.4f %-16.4f %s\n", r->id, r->width,
           r->height, r->area, r->perimeter,
           rect_is_square(r) ? "Square" : "Non-square");
}
 
static void display_all(const Rectangle *arr, size_t n)
{
    print_header();
    for (size_t i = 0; i < n; i++) print_rect(&arr[i]);
}
 
/* ------------------------------- sorting ------------------------------ */
 
static int cmp_area_asc_id_asc(const void *a, const void *b)
{
    const Rectangle *x = a, *y = b;
    if (x->area < y->area) return -1;
    if (x->area > y->area) return 1;
    return (x->id > y->id) - (x->id < y->id);
}
 
static Rectangle *sorted_copy(const RectList *l)
{
    Rectangle *copy = malloc(l->count * sizeof(Rectangle));
    if (!copy) return NULL;
    memcpy(copy, l->data, l->count * sizeof(Rectangle));
    qsort(copy, l->count, sizeof(Rectangle), cmp_area_asc_id_asc);
    return copy;
}
 
/* ------------------------------ statistics ---------------------------- */
 
static void report_statistics(const RectList *l)
{
    if (l->count == 0) {
        printf("No rectangles stored; statistics are not available.\n");
        return;
    }
    size_t big = 0, small = 0;
    double total = 0.0;
 
    for (size_t i = 0; i < l->count; i++) {
        const Rectangle *r = &l->data[i];
        total += r->area;
        if (r->area > l->data[big].area ||
            (r->area == l->data[big].area && r->id < l->data[big].id))
            big = i;
        if (r->area < l->data[small].area ||
            (r->area == l->data[small].area && r->id < l->data[small].id))
            small = i;
    }
    printf("Largest area:\n");
    print_header();
    print_rect(&l->data[big]);
    printf("Smallest area:\n");
    print_header();
    print_rect(&l->data[small]);
    printf("Total area:   %.4f\n", total);
    printf("Average area: %.4f (over %zu rectangle%s)\n",
           total / (double)l->count, l->count, l->count == 1 ? "" : "s");
}
 
/* ----------------------------- menu actions --------------------------- */
 
static void op_add(RectList *l)
{
    int id;
    double w, h;
 
    if (field_failed(read_id("Rectangle ID: ", &id))) return;
    if (find_index(l, id, NULL)) {
        printf("Error: ID %d already exists.\n", id);
        return;
    }
    if (field_failed(read_dimension("Width: ", false, &w))) return;
    if (field_failed(read_dimension("Height: ", false, &h))) return;
 
    if (list_add(l, id, w, h))
        printf("Success: rectangle %d added.\n", id);
}
 
static void op_display(const RectList *l)
{
    if (l->count == 0) { printf("No rectangles stored.\n"); return; }
    display_all(l->data, l->count);
}
 
static void op_search(const RectList *l)
{
    int id;
    size_t idx;
    if (field_failed(read_id("ID to search: ", &id))) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: ID %d not found.\n", id);
        return;
    }
    print_header();
    print_rect(&l->data[idx]);
}
 
static void op_update(RectList *l)
{
    int id;
    size_t idx;
    double w, h;
    InputStatus st;
 
    if (field_failed(read_id("ID to update: ", &id))) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: ID %d not found.\n", id);
        return;
    }
    w = l->data[idx].width;
    h = l->data[idx].height;
    printf("Current record:\n");
    print_header();
    print_rect(&l->data[idx]);
    printf("(Press Enter to keep the current value.)\n");
 
    st = read_dimension("New width: ", true, &w);
    if (field_failed(st)) return;
    st = read_dimension("New height: ", true, &h);
    if (field_failed(st)) return;
 
    if (list_update_at(l, idx, w, h)) {
        printf("Success: rectangle %d updated.\n", id);
        print_header();
        print_rect(&l->data[idx]);
    }
}
 
static void op_delete(RectList *l)
{
    int id;
    size_t idx;
    if (field_failed(read_id("ID to delete: ", &id))) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: ID %d not found.\n", id);
        return;
    }
    if (list_delete_at(l, idx))
        printf("Success: rectangle %d deleted.\n", id);
}
 
static void op_sorted(const RectList *l)
{
    if (l->count == 0) { printf("No rectangles stored.\n"); return; }
    Rectangle *copy = sorted_copy(l);
    if (!copy) {
        printf("Error: not enough memory to sort.\n");
        return;
    }
    printf("Sorted by area (ascending), ties by ID (ascending):\n");
    display_all(copy, l->count);
    free(copy);
}
 
static void op_filter(const RectList *l)
{
    double threshold;
    size_t shown = 0;
 
    if (field_failed(read_threshold("Show rectangles with area greater than: ",
                                    &threshold))) return;
    for (size_t i = 0; i < l->count; i++) {
        if (l->data[i].area > threshold) {
            if (shown == 0) print_header();
            print_rect(&l->data[i]);
            shown++;
        }
    }
    if (shown == 0)
        printf("No rectangles have an area greater than %.4f.\n", threshold);
}
 
/* -------------------------------- main -------------------------------- */
 
static void print_menu(void)
{
    printf("\n===== Rectangle Inventory =====\n"
           "1. Add rectangle\n"
           "2. Display all rectangles\n"
           "3. Search by ID\n"
           "4. Update dimensions\n"
           "5. Delete rectangle\n"
           "6. Area statistics (largest / smallest / total / average)\n"
           "7. Display sorted by area\n"
           "8. Filter by minimum area\n"
           "0. Exit\n");
}
 
int main(void)
{
    RectList list = { NULL, 0, 0, 0 };
 
    for (;;) {
        long n;
        char msg[64];
        snprintf(msg, sizeof msg, "number must be an integer from 1 to %d.",
                 MAX_INITIAL_COUNT);
        InputStatus st = read_long_field("Number of rectangles (initial "
                                         "capacity): ", LONG_MIN, LONG_MAX,
                                         &n, msg);
        if (st == IN_EOF) return 1;
        if (st != IN_OK) continue;
        if (list_init(&list, n)) break;
    }
    printf("Allocated space for %zu rectangles.\n", list.capacity);
 
    while (!g_eof) {
        long choice;
        print_menu();
        InputStatus st = read_long_field("Choice: ", 0, 8, &choice,
                                         "choose a number from 0 to 8.");
        if (st == IN_EOF) break;
        if (st != IN_OK) continue;
        if (choice == 0) break;
 
        switch (choice) {
        case 1: op_add(&list);              break;
        case 2: op_display(&list);          break;
        case 3: op_search(&list);           break;
        case 4: op_update(&list);           break;
        case 5: op_delete(&list);           break;
        case 6: report_statistics(&list);   break;
        case 7: op_sorted(&list);           break;
        case 8: op_filter(&list);           break;
        }
    }
 
    list_free(&list);
    printf("\nMemory released. Goodbye.\n");
    return 0;
}
 
