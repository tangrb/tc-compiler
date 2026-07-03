/*
 * warning.c — 编译警告列表管理
 */
#include "tc_warning.h"

#include <stdlib.h>
#include <string.h>

void tc_warning_list_init(TcWarningList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void tc_warning_list_free(TcWarningList *list) {
    size_t i = 0;
    for (i = 0; i < list->count; i++) {
        free(list->items[i].message);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

int tc_warning_list_add(TcWarningList *list, TcWarningKind kind, int line, const char *message) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        TcWarning *items = (TcWarning *)realloc(list->items, new_cap * sizeof(TcWarning));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->capacity = new_cap;
    }
    list->items[list->count].kind = kind;
    list->items[list->count].line = line;
    list->items[list->count].message = message ? strdup(message) : NULL;
    if (message && !list->items[list->count].message) {
        return -1;
    }
    list->count++;
    return 0;
}

void tc_warning_list_print(const TcWarningList *list, FILE *out) {
    size_t i = 0;
    for (i = 0; i < list->count; i++) {
        const TcWarning *warn = &list->items[i];
        const char *msg = warn->message ? warn->message : "";
        if (warn->line > 0) {
            fprintf(out, "warning: %s (line %d)\n", msg, warn->line);
        } else {
            fprintf(out, "warning: %s\n", msg);
        }
    }
}
