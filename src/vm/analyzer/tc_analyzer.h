/*
 * tc_analyzer.h — 静态分析器公共入口
 *
 * Parser 产出 TcProgram 后，tc_analyze_ex 编排模块/函数/Pass1/Pass2/CFG/调用图。
 * 成功则写入 TcTypedProgram（AST、符号表、多域 CFG、类型池），供 VM / AOT / Embed 消费。
 *
 * tc_analyze 无路径：只做结构与本文件语义，不解析 import。
 */
#ifndef TC_ANALYZER_H
#define TC_ANALYZER_H

#include "tc_symbol.h"
#include "tc_types.h"
#include "tc_module.h"

/**
 * 初始化已类型化程序为空状态。
 * @param program 待初始化的 TcTypedProgram 指针
 */
void tc_typed_program_init(TcTypedProgram *program);

/**
 * 释放已类型化程序（释放语句列表、符号表和警告列表）。
 * @param program 待释放的 TcTypedProgram 指针
 */
void tc_typed_program_free(TcTypedProgram *program);

/**
 * 对 program 做静态分析，结果写入 out。
 * @param entry_path 入口文件路径；非 NULL 时在结构检查后解析 import（4b/4c）
 * @param search     模块搜索路径；可为 NULL
 */
int tc_analyze_ex(TcProgram *program, TcTypedProgram *out, const char *entry_path,
                  const TcModuleSearchPaths *search, TcDiagnostic *diag);

/**
 * 内存源入口：入口目录与模块名不由文件路径推导。
 * @param display_name      入口显示名（诊断与源码片段），用作 source_path
 * @param entry_module_name 入口模块名（"" = 无模块名，内存源默认形态）
 * @param search            模块搜索路径；可为 NULL
 * @note 与文件入口覆盖同一阶段范围：display_name 所在目录为导入搜索第一候选，
 *       `import` 照常解析（语言标准 §4.5、§1.3）。
 */
int tc_analyze_memory(TcProgram *program, TcTypedProgram *out, const char *display_name,
                      const char *entry_module_name, const TcModuleSearchPaths *search,
                      TcDiagnostic *diag);

/**
 * 等价于 tc_analyze_ex(..., NULL, NULL, diag)（不解析 import）。
 */
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag);

#endif
