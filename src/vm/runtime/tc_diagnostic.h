/*
 * tc_diagnostic.h — 错误诊断接口
 *
 * TC 全流水线（Lexer / Parser / Analyzer / Executor / AOT）通过 TcDiagnostic
 * 统一报告错误。TcDiagnostic 设计为单槽（fail-fast），仅保留第一条错误。
 * 诊断最终由 driver 层格式化输出到 stderr（类 GCC/clang 格式）。
 */
#ifndef TC_DIAGNOSTIC_H
#define TC_DIAGNOSTIC_H

#include <stdio.h>

#include "tc_types.h"

/**
 * 可移植 strndup：复制字符串至多 n 字节并保证 NUL 结尾。
 * glibc / MSYS2 提供 strndup，但上游 MinGW-w64 无此函数（隐式声明在
 * GCC 14+ 下为硬错误），故统一走本实现，避免平台差异。
 * @param s 源串（NULL 返回 NULL）
 * @param n 最大复制字节数（遇 NUL 提前停止）
 * @return 新分配副本；分配失败返回 NULL
 */
char *tc_strndup(const char *s, size_t n);

/** 初始化诊断结构为默认空状态 */
void tc_diagnostic_init(TcDiagnostic *diag);

/**
 * 释放诊断模块管理的文本字段（message / filename / snippet / source）并清空位置信息。
 * @note 可重复调用；每次 tc_diagnostic_set 前无需手动清除。
 */
void tc_diagnostic_clear(TcDiagnostic *diag);

/**
 * 绑定诊断所对应的源文件路径与完整源文本。
 * @param diag     诊断对象
 * @param filename 源文件路径（内部 strdup 复制）
 * @param source   完整源文本（内部 strdup 复制；NULL 表示无源文本）
 * @return 成功返回 0；文本分配失败返回 -1 并设置 Implementation/OutOfMemory
 */
int tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source);

/**
 * 读取当前绑定的源文件路径与源文本。
 * @param diag      诊断对象
 * @param filename  输出：内部存储的路径指针（NULL 表示未绑定；不得释放）
 * @param source    输出：内部存储的完整源文本指针（可为 NULL；不得释放）
 */
void tc_diagnostic_get_source(const TcDiagnostic *diag, const char **filename,
                              const char **source);

/**
 * 设置一条新的诊断信息。
 * @param diag    诊断对象
 * @param kind    错误种类
 * @param line    出错行号（1-based），0 表示无行号
 * @param column  出错列号（1-based），可传入 TC_COLUMN_UNKNOWN
 * @param message 错误描述（内部 strdup 复制）
 * @return 成功返回 0；文本分配失败返回 -1 并设置 Implementation/OutOfMemory
 * @note 若 diag->source 可用且 line > 0，同时捕获出错行源码到 snippet 字段
 */
int tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                      const char *message);

/** 设置 API/环境域诊断；成功返回 0，文本分配失败返回 -1 并改设 OOM。 */
int tc_diagnostic_set_api(TcDiagnostic *diag, TcApiErrorCode code, const char *message);

/**
 * 将诊断信息格式化输出到指定流。
 * @param diag 诊断对象
 * @param out  输出流（通常为 stderr）
 *
 * 输出格式：
 *   <file>:<line>:<column>: error: <message>
 *   <file>:<line>: error: <message>          （无列号）
 *   <file>: error: <message>                  （无行号）
 *   随后附加出错行源码与 ^ 指示符（若有 snippet）
 */
void tc_diagnostic_print(const TcDiagnostic *diag, FILE *out);

#ifdef TC_DIAGNOSTIC_TESTING
/** 单元测试专用：在指定成功分配次数后令诊断文本复制失败；负数关闭。 */
void tc_diagnostic_test_fail_alloc_after(int successful_allocations);
#endif

#endif
