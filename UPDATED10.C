#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
 
typedef enum {
    TYPE_EMPTY,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_CHAR,
    TYPE_STRING
} DataType;
 
typedef union {
    int int_value;
    float float_value;
    char char_value;
    char *string_value;
} Data;
 
typedef struct {
    Data data;
    DataType type;
} Container;
 
typedef enum {
    CONTAINER_OK,
    CONTAINER_ERR_NULL_ARG,
    CONTAINER_ERR_WRONG_TYPE,
    CONTAINER_ERR_ALLOC
} ContainerStatus;
 
static bool g_fail_next_alloc = false;
 
static void container_test_fail_next_alloc(void)
{
    g_fail_next_alloc = true;
}
 
static void *container_alloc(size_t n)
{
    if (g_fail_next_alloc) {
        g_fail_next_alloc = false;
        return NULL;
    }
    return malloc(n);
}
 
static const char *container_status_name(ContainerStatus s)
{
    switch (s) {
    case CONTAINER_OK:             return "OK";
    case CONTAINER_ERR_NULL_ARG:   return "ERROR: null argument";
    case CONTAINER_ERR_WRONG_TYPE: return "ERROR: wrong type";
    case CONTAINER_ERR_ALLOC:      return "ERROR: allocation failed";
    }
    return "ERROR: unknown status";
}
 
static const char *container_type_name(DataType t)
{
    switch (t) {
    case TYPE_EMPTY:  return "EMPTY";
    case TYPE_INT:    return "INT";
    case TYPE_FLOAT:  return "FLOAT";
    case TYPE_CHAR:   return "CHAR";
    case TYPE_STRING: return "STRING";
    }
    return "INVALID";
}
 
static ContainerStatus container_init(Container *c)
{
    if (!c) return CONTAINER_ERR_NULL_ARG;
    memset(&c->data, 0, sizeof c->data);
    c->type = TYPE_EMPTY;
    return CONTAINER_OK;
}
 
static ContainerStatus container_clear(Container *c)
{
    if (!c) return CONTAINER_ERR_NULL_ARG;
    if (c->type == TYPE_STRING) free(c->data.string_value);
    memset(&c->data, 0, sizeof c->data);
    c->type = TYPE_EMPTY;
    return CONTAINER_OK;
}
 
static ContainerStatus container_set_int(Container *c, int value)
{
    if (!c) return CONTAINER_ERR_NULL_ARG;
    container_clear(c);
    c->data.int_value = value;
    c->type = TYPE_INT;
    return CONTAINER_OK;
}
 
static ContainerStatus container_set_float(Container *c, float value)
{
    if (!c) return CONTAINER_ERR_NULL_ARG;
    container_clear(c);
    c->data.float_value = value;
    c->type = TYPE_FLOAT;
    return CONTAINER_OK;
}
 
static ContainerStatus container_set_char(Container *c, char value)
{
    if (!c) return CONTAINER_ERR_NULL_ARG;
    container_clear(c);
    c->data.char_value = value;
    c->type = TYPE_CHAR;
    return CONTAINER_OK;
}
 
static char *duplicate_text(const char *text)
{
    size_t len = strlen(text);
    if (len == SIZE_MAX) return NULL;
    char *copy = container_alloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}
 
static ContainerStatus container_set_string(Container *c, const char *value)
{
    if (!c || !value) return CONTAINER_ERR_NULL_ARG;
 
    char *copy = duplicate_text(value);
    if (!copy) return CONTAINER_ERR_ALLOC;
 
    container_clear(c);
    c->data.string_value = copy;
    c->type = TYPE_STRING;
    return CONTAINER_OK;
}
 
static ContainerStatus container_get_int(const Container *c, int *out_value)
{
    if (!c || !out_value) return CONTAINER_ERR_NULL_ARG;
    if (c->type != TYPE_INT) return CONTAINER_ERR_WRONG_TYPE;
    *out_value = c->data.int_value;
    return CONTAINER_OK;
}
 
static ContainerStatus container_get_float(const Container *c, float *out_value)
{
    if (!c || !out_value) return CONTAINER_ERR_NULL_ARG;
    if (c->type != TYPE_FLOAT) return CONTAINER_ERR_WRONG_TYPE;
    *out_value = c->data.float_value;
    return CONTAINER_OK;
}
 
static ContainerStatus container_get_char(const Container *c, char *out_value)
{
    if (!c || !out_value) return CONTAINER_ERR_NULL_ARG;
    if (c->type != TYPE_CHAR) return CONTAINER_ERR_WRONG_TYPE;
    *out_value = c->data.char_value;
    return CONTAINER_OK;
}
 
static ContainerStatus container_get_string(const Container *c,
                                            const char **out_value)
{
    if (!c || !out_value) return CONTAINER_ERR_NULL_ARG;
    if (c->type != TYPE_STRING || !c->data.string_value)
        return CONTAINER_ERR_WRONG_TYPE;
    *out_value = c->data.string_value;
    return CONTAINER_OK;
}
 
static ContainerStatus container_copy(Container *destination,
                                      const Container *source)
{
    if (!destination || !source) return CONTAINER_ERR_NULL_ARG;
    if (destination == source) return CONTAINER_OK;
 
    if (source->type == TYPE_STRING) {
        if (!source->data.string_value) return CONTAINER_ERR_WRONG_TYPE;
        char *copy = duplicate_text(source->data.string_value);
        if (!copy) return CONTAINER_ERR_ALLOC;
        container_clear(destination);
        destination->data.string_value = copy;
        destination->type = TYPE_STRING;
        return CONTAINER_OK;
    }
 
    DataType t = source->type;
    Data d = source->data;
    container_clear(destination);
    switch (t) {
    case TYPE_INT:
    case TYPE_FLOAT:
    case TYPE_CHAR:
        destination->data = d;
        destination->type = t;
        return CONTAINER_OK;
    case TYPE_EMPTY:
        return CONTAINER_OK;
    default:
        return CONTAINER_ERR_WRONG_TYPE;
    }
}
 
static void container_print(const Container *c)
{
    if (!c) {
        printf("<null container>");
        return;
    }
    switch (c->type) {
    case TYPE_EMPTY:
        printf("<empty>");
        break;
    case TYPE_INT:
        printf("%d", c->data.int_value);
        break;
    case TYPE_FLOAT:
        printf("%.2f", (double)c->data.float_value);
        break;
    case TYPE_CHAR:
        if (isprint((unsigned char)c->data.char_value))
            printf("'%c'", c->data.char_value);
        else
            printf("'\\x%02x'", (unsigned char)c->data.char_value);
        break;
    case TYPE_STRING:
        if (c->data.string_value)
            printf("\"%s\"", c->data.string_value);
        else
            printf("<null string>");
        break;
    default:
        printf("<invalid type tag>");
        break;
    }
}
 
static void print_status(const char *label, ContainerStatus s)
{
    printf("  %-44s -> %s\n", label, container_status_name(s));
}
 
static void show(const char *label, const Container *c)
{
    printf("  %-44s -> [%s] ", label, container_type_name(c->type));
    container_print(c);
    printf("\n");
}
 
static void section(const char *title)
{
    printf("\n=== %s ===\n", title);
}
 
static void demo_sequential(void)
{
    section("1. One container, values set in sequence");
 
    Container c;
    container_init(&c);
 
    int i;
    float f;
    char ch;
    const char *s;
 
    container_set_int(&c, 10);
    if (container_get_int(&c, &i) == CONTAINER_OK) printf("  Int: %d\n", i);
 
    container_set_float(&c, 3.14f);
    if (container_get_float(&c, &f) == CONTAINER_OK)
        printf("  Float: %.2f\n", (double)f);
 
    container_set_char(&c, 'A');
    if (container_get_char(&c, &ch) == CONTAINER_OK)
        printf("  Char: %c\n", ch);
 
    container_set_string(&c, "Hello, World!");
    if (container_get_string(&c, &s) == CONTAINER_OK)
        printf("  String: %s\n", s);
 
    container_clear(&c);
}
 
static void demo_array(Container *arr, size_t n)
{
    section("2. Array of mixed-type containers");
 
    container_set_int(&arr[0], 10);
    container_set_float(&arr[1], 3.14f);
    container_set_char(&arr[2], 'A');
    container_set_string(&arr[3], "Hello");
    container_set_int(&arr[4], 0);
    container_set_string(&arr[5], "Data");
 
    for (size_t k = 0; k < n; k++) {
        printf("  [%zu] %-7s ", k, container_type_name(arr[k].type));
        container_print(&arr[k]);
        printf("\n");
    }
}
 
static void demo_deep_copy(Container *arr)
{
    section("3. Deep copy of a string container");
 
    Container copy;
    container_init(&copy);
 
    print_status("copy arr[5] into copy", container_copy(&copy, &arr[5]));
    printf("  separate buffers: %s\n",
           arr[5].data.string_value != copy.data.string_value ? "yes" : "no");
 
    container_set_string(&arr[5], "Changed");
    show("original after set_string(\"Changed\")", &arr[5]);
    show("copy (must still hold the first text)", &copy);
 
    container_clear(&arr[5]);
    show("original after clear", &arr[5]);
    show("copy after original cleared", &copy);
 
    container_clear(&copy);
}
 
static void demo_edge_cases(void)
{
    section("4. Edge cases");
 
    Container c;
    container_init(&c);
    int iv = -1;
    float fv = -1.0f;
    const char *sv = NULL;
 
    container_set_int(&c, 0);
    ContainerStatus st = container_get_int(&c, &iv);
    printf("  valid integer zero: status = %s, value = %d\n",
           container_status_name(st), iv);
 
    iv = -1;
    st = container_get_float(&c, &fv);
    printf("  wrong-type getter (float from INT): status = %s, "
           "out value untouched = %s\n", container_status_name(st),
           fv == -1.0f ? "yes" : "no");
 
    Container empty;
    container_init(&empty);
    show("empty container", &empty);
    print_status("get_int on empty container",
                 container_get_int(&empty, &iv));
    print_status("get_string on empty container",
                 container_get_string(&empty, &sv));
 
    container_set_string(&c, "");
    st = container_get_string(&c, &sv);
    printf("  empty string: status = %s, length = %zu\n",
           container_status_name(st), st == CONTAINER_OK ? strlen(sv) : 0);
 
    container_set_string(&c, "keep me");
    container_test_fail_next_alloc();
    print_status("set_string with simulated alloc failure",
                 container_set_string(&c, "replacement"));
    show("previous value preserved", &c);
 
    Container dst;
    container_init(&dst);
    container_set_int(&dst, 77);
    container_test_fail_next_alloc();
    print_status("copy with simulated alloc failure",
                 container_copy(&dst, &c));
    show("destination unchanged", &dst);
 
    print_status("switch away from string (set_int)",
                 container_set_int(&c, 5));
    show("after switching", &c);
 
    container_set_string(&c, "same source");
    const char *own = NULL;
    container_get_string(&c, &own);
    print_status("set_string using the container's own buffer",
                 container_set_string(&c, own));
    show("after same-source replacement", &c);
 
    print_status("self-copy", container_copy(&c, &c));
    show("after self-copy", &c);
 
    print_status("first clear", container_clear(&c));
    print_status("second clear", container_clear(&c));
    print_status("third clear", container_clear(&c));
    show("after repeated clear", &c);
 
    print_status("set_int(NULL, 1)", container_set_int(NULL, 1));
    print_status("set_string(&c, NULL)", container_set_string(&c, NULL));
    print_status("get_int(&c, NULL)", container_get_int(&c, NULL));
    print_status("copy(NULL, &c)", container_copy(NULL, &c));
    printf("  print(NULL): ");
    container_print(NULL);
    printf("\n");
 
    container_clear(&dst);
    container_clear(&empty);
    container_clear(&c);
}
 
static void demo_transitions(void)
{
    section("5. Type transitions: STRING -> INT -> STRING -> FLOAT");
 
    Container c;
    container_init(&c);
 
    container_set_string(&c, "first text");
    show("set string", &c);
    container_set_int(&c, 42);
    show("set int (string freed)", &c);
    container_set_string(&c, "second text");
    show("set string again", &c);
    container_set_float(&c, 2.5f);
    show("set float (string freed)", &c);
 
    container_clear(&c);
    show("cleared", &c);
}
 
int main(void)
{
    enum { COUNT = 6 };
 
    demo_sequential();
 
    Container *arr = malloc(COUNT * sizeof *arr);
    if (!arr) {
        printf("Error: could not allocate the container array.\n");
        return 1;
    }
    for (size_t k = 0; k < COUNT; k++) container_init(&arr[k]);
 
    demo_array(arr, COUNT);
    demo_deep_copy(arr);
    demo_edge_cases();
    demo_transitions();
 
    section("6. Cleanup");
    for (size_t k = 0; k < COUNT; k++) container_clear(&arr[k]);
    free(arr);
    printf("  all containers cleared and array freed\n");
 
    return 0;
}
 
