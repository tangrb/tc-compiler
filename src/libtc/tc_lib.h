/*
 * tc_lib.h — libtc 嵌入 API
 *
 * 允许外部程序嵌入 TC 的编译（解析 + 静态分析）与执行能力。
 * 内存所有权约定见 docs/libtc-api.md。
 */
#ifndef TC_LIB_H
#define TC_LIB_H

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_types.h"

/**
 * @brief 将源字符串编译为已类型化程序（Parse + Analyze）
 * @param source 源字符串（调用方须保证在 diag 使用期间有效）
 * @param out    输出参数，成功时写入 TcTypedProgram（调用方须 tc_typed_program_free）
 * @param diag   诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag（out 未初始化）
 */
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * @brief 从文件路径读取源码并编译
 * @param path 源文件路径
 * @param out  输出参数
 * @param diag 诊断对象
 * @return 成功返回 0；I/O 或编译失败返回 -1
 */
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * @brief 执行已类型化程序
 * @param program 已通过静态分析的程序
 * @param diag    诊断对象
 * @return 成功返回 0；运行时失败返回 -1
 * @note 警告已在 compile 阶段收集于 program->warnings，执行前由调用方决定是否打印
 */
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);

#endif
