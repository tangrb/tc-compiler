/*
 * tc_diagnostic.h — 错误诊断接口
 *
 * TC-VM 各阶段（Lexer / Parser / Analyzer / Executor）通过 TcDiagnostic
 * 统一报告错误：设置错误种类、源文件、行号、列号及描述消息，最终由 driver 打印到 stderr。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_DIAGNOSTIC_H
#define TC_DIAGNOSTIC_H

#include <stdio.h>

#include "tc_types.h"

/**
 * @brief 初始化诊断结构为默认空状态
 * @param diag 待初始化的诊断对象指针
 */
void tc_diagnostic_init(TcDiagnostic *diag);

/**
 * @brief 释放诊断对象的堆字段并清空位置信息
 * @param diag 待清除的诊断对象指针
 * @note 可重复调用
 */
void tc_diagnostic_clear(TcDiagnostic *diag);

/**
 * @brief 绑定诊断所对应的源文件路径与完整源文本
 * @param diag     诊断对象指针
 * @param filename 源文件路径（会被自动复制）
 * @param source   完整源文本指针（调用方须保证其在诊断打印前有效）
 */
void tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source);

/**
 * @brief 设置一条新的诊断信息
 * @param diag    诊断对象指针
 * @param kind    错误种类枚举值
 * @param line    出错行号（1-based），0 表示无行号
 * @param column  出错列号（1-based），可传入 TC_COLUMN_UNKNOWN
 * @param message 错误描述消息（会被自动复制）
 */
void tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                       const char *message);

/**
 * @brief 将诊断信息格式化输出到指定流（类 GCC/clang 格式）
 * @param diag 诊断对象指针
 * @param out  输出文件流（通常为 stderr）
 */
void tc_diagnostic_print(const TcDiagnostic *diag, FILE *out);

#endif
