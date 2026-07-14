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

/** 初始化诊断结构为默认空状态 */
void tc_diagnostic_init(TcDiagnostic *diag);

/**
 * 释放诊断对象的堆字段（message / filename / snippet / source）并清空位置信息。
 * @note 可重复调用；每次 tc_diagnostic_set 前无需手动清除。
 */
void tc_diagnostic_clear(TcDiagnostic *diag);

/**
 * 绑定诊断所对应的源文件路径与完整源文本。
 * @param diag     诊断对象
 * @param filename 源文件路径（内部 strdup 复制）
 * @param source   完整源文本（内部 strdup 复制；NULL 表示无源文本）
 */
void tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source);

/**
 * 设置一条新的诊断信息。
 * @param diag    诊断对象
 * @param kind    错误种类
 * @param line    出错行号（1-based），0 表示无行号
 * @param column  出错列号（1-based），可传入 TC_COLUMN_UNKNOWN
 * @param message 错误描述（内部 strdup 复制）
 * @note 若 diag->source 可用且 line > 0，同时捕获出错行源码到 snippet 字段
 */
void tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                       const char *message);

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

#endif
