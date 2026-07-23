/*
 * tc_lib.h — libtc 嵌入 API
 *
 * 允许外部程序嵌入 TC 的编译（解析 + 静态分析）与执行能力。
 * 单次编译可以独立于调用方存在——编译时复制或转交所有权，执行后释放资源。
 *
 * 典型使用：
 *   TcTypedProgram prog;
 *   if (tc_compile_file("input.tc", &prog, &diag) == 0) {
 *       // 0.0.31 无语言警告；成功对象可直接重复执行或交给 AOT。
 *       tc_run_typed(&prog, &diag);
 *       tc_typed_program_free(&prog);
 *   }
 *
 * 内存所有权约定见 docs/libtc-api.md。
 */
#ifndef TC_LIB_H
#define TC_LIB_H

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_types.h"

/**
 * 将源字符串编译为已类型化程序（Parse + Analyze）。
 * @param source 源字符串（仅调用期间须有效；返回后可立即释放）
 * @param out    输出参数，成功时写入 TcTypedProgram（调用方须 tc_typed_program_free）
 * @param diag   诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag
 * @note 仅成功时写入 out 并转移所有权。任一失败阶段都不修改 out，
 *       调用方无需也不得释放本次调用的输出。
 */
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * 设置模块搜索路径（-I 等价；复制路径字符串）。
 * 传入 count=0 可清空。线程不安全（进程级全局）。
 */
int tc_set_module_search_paths(char *const *paths, size_t count, TcDiagnostic *diag);

/**
 * 从文件路径读取源码并编译；自动解析 import 的 #lib。
 * @param path 源文件路径
 * @param out  输出参数
 * @param diag 诊断对象
 * @return 成功返回 0；I/O 或编译失败返回 -1
 */
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * 执行已类型化程序。
 * @param program 已通过静态分析的程序
 * @param diag    诊断对象
 * @return 成功返回 0；运行时失败返回 -1
 * @note program->warnings 是兼容空壳；0.0.31 成功编译时始终为空。
 */
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);

#endif
