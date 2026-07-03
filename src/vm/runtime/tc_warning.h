/*
 * tc_warning.h — 编译警告接口
 */
#ifndef TC_WARNING_H
#define TC_WARNING_H

#include <stdio.h>

#include "tc_types.h"

void tc_warning_list_init(TcWarningList *list);
void tc_warning_list_free(TcWarningList *list);
int tc_warning_list_add(TcWarningList *list, TcWarningKind kind, int line, const char *message);
void tc_warning_list_print(const TcWarningList *list, FILE *out);

#endif
