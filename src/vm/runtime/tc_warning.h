/*
 * tc_warning.h — 编译警告接口
 *
 * TC-VM 的警告系统不阻止执行。Analyzer 通过后将警告批量打印到 stderr。
 * 目前仅 TC_WARN_UNINITIALIZED_VARIABLE 一种警告。
 */
#ifndef TC_WARNING_H
#define TC_WARNING_H

#include <stdio.h>

#include "tc_types.h"

/** 初始化空警告列表 */
void tc_warning_list_init(TcWarningList *list);

/** 释放警告列表全部动态内存（包括每个 warning->message） */
void tc_warning_list_free(TcWarningList *list);

/**
 * 追加一条警告。
 * @param list    警告列表
 * @param kind    警告种类
 * @param line    行号（0 表示无行号）
 * @param message 警告描述（内部 strdup 复制）
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_warning_list_add(TcWarningList *list, TcWarningKind kind, int line, const char *message);

/** 将警告列表格式化输出到指定流 */
void tc_warning_list_print(const TcWarningList *list, FILE *out);

#endif
