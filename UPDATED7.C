#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
 
#define COMMAND_BUF_SIZE 32
#define LINE_BUF_SIZE 256
#define DESC_SIZE 100
#define DESC_MAX_LEN (DESC_SIZE - 1)
#define INITIAL_CAPACITY 2
 
typedef enum {
    CMD_ADD,
    CMD_DELETE,
    CMD_VIEW,
    CMD_UPDATE,
    CMD_COMPLETE,
    CMD_FIND,
    CMD_HELP,
    CMD_EXIT,
    CMD_UNKNOWN
} CommandType;
 
typedef enum {
    STATUS_PENDING,
    STATUS_DONE
} TaskStatus;
 
typedef struct {
    int id;
    char description[DESC_SIZE];
    TaskStatus status;
} Task;
 
typedef struct {
    Task *data;
    size_t count;
    size_t capacity;
} TaskList;
 
typedef enum {
    IN_OK,
    IN_EMPTY,
    IN_TOO_LONG,
    IN_EOF
} InputStatus;
 
typedef struct {
    const char *name;
    CommandType type;
    const char *help;
} CommandEntry;
 
static const CommandEntry COMMAND_TABLE[] = {
    { "ADD",      CMD_ADD,      "add a new task (asks for ID and description)" },
    { "DELETE",   CMD_DELETE,   "delete a task by ID" },
    { "VIEW",     CMD_VIEW,     "list all tasks" },
    { "UPDATE",   CMD_UPDATE,   "change the description of a task" },
    { "COMPLETE", CMD_COMPLETE, "mark a task as DONE" },
    { "FIND",     CMD_FIND,     "show one task by ID" },
    { "HELP",     CMD_HELP,     "show this list of commands" },
    { "EXIT",     CMD_EXIT,     "quit the program" }
};
 
#define COMMAND_COUNT (sizeof COMMAND_TABLE / sizeof COMMAND_TABLE[0])
 
static bool g_eof = false;
 
#ifdef SIMULATE_REALLOC_FAILURE
static void *task_realloc(void *p, size_t n)
{
    (void)p;
    (void)n;
    return NULL;
}
#else
#define task_realloc realloc
#endif
 
static bool equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}
 
static CommandType get_command_type(const char *commandStr)
{
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (equals_ignore_case(commandStr, COMMAND_TABLE[i].name))
            return COMMAND_TABLE[i].type;
    }
    return CMD_UNKNOWN;
}
 
static const char *status_name(TaskStatus s)
{
    return s == STATUS_DONE ? "DONE" : "PENDING";
}
 
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}
 
static InputStatus read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
 
    if (!fgets(buf, (int)size, stdin)) return IN_EOF;
 
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else if (len == size - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
        return IN_TOO_LONG;
    }
 
    trim(buf);
    return buf[0] == '\0' ? IN_EMPTY : IN_OK;
}
 
static bool read_id(const char *prompt, int *out)
{
    char buf[LINE_BUF_SIZE];
    InputStatus st = read_line(prompt, buf, sizeof buf);
 
    if (st == IN_EOF) {
        g_eof = true;
        return false;
    }
    if (st == IN_TOO_LONG) {
        printf("Error: input too long. Operation cancelled.\n");
        return false;
    }
    if (st == IN_EMPTY) {
        printf("Error: a task ID is required. Operation cancelled.\n");
        return false;
    }
 
    char *end;
    errno = 0;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || errno == ERANGE || v < 1 || v > INT_MAX) {
        printf("Error: task ID must be a positive integer. "
               "Operation cancelled.\n");
        return false;
    }
    *out = (int)v;
    return true;
}
 
static bool read_description(const char *prompt, char *out)
{
    char buf[LINE_BUF_SIZE];
    InputStatus st = read_line(prompt, buf, sizeof buf);
 
    if (st == IN_EOF) {
        g_eof = true;
        return false;
    }
    if (st == IN_TOO_LONG || (st == IN_OK && strlen(buf) > DESC_MAX_LEN)) {
        printf("Error: description exceeds %d characters. "
               "Operation cancelled.\n", DESC_MAX_LEN);
        return false;
    }
    if (st == IN_EMPTY) {
        printf("Error: description cannot be empty. Operation cancelled.\n");
        return false;
    }
    strcpy(out, buf);
    return true;
}
 
static bool list_init(TaskList *l)
{
    l->count = 0;
    l->capacity = 0;
    l->data = malloc(INITIAL_CAPACITY * sizeof(Task));
    if (!l->data) {
        printf("Error: could not allocate memory for the task list.\n");
        return false;
    }
    l->capacity = INITIAL_CAPACITY;
    return true;
}
 
static bool list_grow(TaskList *l)
{
    size_t limit = SIZE_MAX / sizeof(Task);
    if (l->capacity >= limit) {
        printf("Error: maximum capacity reached.\n");
        return false;
    }
    size_t new_cap = (l->capacity > limit / 2) ? limit : l->capacity * 2;
 
    Task *tmp = task_realloc(l->data, new_cap * sizeof(Task));
    if (!tmp) {
        printf("Error: out of memory while expanding to %zu tasks.\n", new_cap);
        return false;
    }
    l->data = tmp;
    l->capacity = new_cap;
    printf("(Capacity expanded to %zu.)\n", new_cap);
    return true;
}
 
static void list_free(TaskList *l)
{
    free(l->data);
    l->data = NULL;
    l->count = 0;
    l->capacity = 0;
}
 
static bool find_index(const TaskList *l, int id, size_t *idx)
{
    for (size_t i = 0; i < l->count; i++) {
        if (l->data[i].id == id) {
            if (idx) *idx = i;
            return true;
        }
    }
    return false;
}
 
static void print_task_header(void)
{
    printf("%-8s %-40s %s\n", "ID", "Description", "Status");
}
 
static void print_task(const Task *t)
{
    printf("%-8d %-40s %s\n", t->id, t->description, status_name(t->status));
}
 
static void cmd_add(TaskList *l)
{
    int id;
    Task t;
 
    if (!read_id("Task ID: ", &id)) return;
    if (find_index(l, id, NULL)) {
        printf("Error: task ID %d already exists. Operation cancelled.\n", id);
        return;
    }
    if (!read_description("Description: ", t.description)) return;
 
    if (l->count == l->capacity && !list_grow(l)) {
        printf("Task %d was not added.\n", id);
        return;
    }
    t.id = id;
    t.status = STATUS_PENDING;
    l->data[l->count++] = t;
    printf("Success: task %d added.\n", id);
}
 
static void cmd_delete(TaskList *l)
{
    int id;
    size_t idx;
 
    if (!read_id("Task ID to delete: ", &id)) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: task ID %d not found.\n", id);
        return;
    }
    memmove(&l->data[idx], &l->data[idx + 1],
            (l->count - idx - 1) * sizeof(Task));
    l->count--;
    printf("Success: task %d deleted.\n", id);
}
 
static void cmd_view(const TaskList *l)
{
    if (l->count == 0) {
        printf("The task list is empty.\n");
        return;
    }
    print_task_header();
    for (size_t i = 0; i < l->count; i++) print_task(&l->data[i]);
    printf("(%zu task%s)\n", l->count, l->count == 1 ? "" : "s");
}
 
static void cmd_update(TaskList *l)
{
    int id;
    size_t idx;
    char desc[DESC_SIZE];
 
    if (!read_id("Task ID to update: ", &id)) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: task ID %d not found.\n", id);
        return;
    }
    printf("Current description: %s\n", l->data[idx].description);
    if (!read_description("New description: ", desc)) return;
 
    strcpy(l->data[idx].description, desc);
    printf("Success: task %d updated.\n", id);
}
 
static void cmd_complete(TaskList *l)
{
    int id;
    size_t idx;
 
    if (!read_id("Task ID to complete: ", &id)) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: task ID %d not found.\n", id);
        return;
    }
    if (l->data[idx].status == STATUS_DONE) {
        printf("Task %d is already DONE.\n", id);
        return;
    }
    l->data[idx].status = STATUS_DONE;
    printf("Success: task %d marked DONE.\n", id);
}
 
static void cmd_find(const TaskList *l)
{
    int id;
    size_t idx;
 
    if (!read_id("Task ID to find: ", &id)) return;
    if (!find_index(l, id, &idx)) {
        printf("Error: task ID %d not found.\n", id);
        return;
    }
    print_task_header();
    print_task(&l->data[idx]);
}
 
static void cmd_help(void)
{
    printf("Commands (case-insensitive):\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++)
        printf("  %-9s %s\n", COMMAND_TABLE[i].name, COMMAND_TABLE[i].help);
}
 
int main(void)
{
    TaskList list;
    char input[COMMAND_BUF_SIZE];
    bool running = true;
 
    if (!list_init(&list)) return 1;
 
    printf("Task Manager. Type HELP for the list of commands.\n");
 
    while (running && !g_eof) {
        InputStatus st = read_line("\nEnter command: ", input, sizeof input);
 
        if (st == IN_EOF) {
            printf("\nEnd of input reached.\n");
            break;
        }
        if (st == IN_TOO_LONG) {
            printf("Error: command too long (maximum %d characters).\n",
                   COMMAND_BUF_SIZE - 1);
            continue;
        }
        if (st == IN_EMPTY) continue;
 
        switch (get_command_type(input)) {
            case CMD_ADD:      cmd_add(&list);      break;
            case CMD_DELETE:   cmd_delete(&list);   break;
            case CMD_VIEW:     cmd_view(&list);     break;
            case CMD_UPDATE:   cmd_update(&list);   break;
            case CMD_COMPLETE: cmd_complete(&list); break;
            case CMD_FIND:     cmd_find(&list);     break;
            case CMD_HELP:     cmd_help();          break;
            case CMD_EXIT:
                printf("Exiting program.\n");
                running = false;
                break;
            case CMD_UNKNOWN:
            default:
                printf("Error: unknown command \"%s\". Type HELP for the "
                       "list of commands.\n", input);
                break;
        }
    }
 
    if (g_eof) printf("\nEnd of input reached.\n");
 
    list_free(&list);
    printf("Memory released.\n");
    return 0;
}
 
