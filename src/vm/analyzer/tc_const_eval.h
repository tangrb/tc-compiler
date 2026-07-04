/*
 * tc_const_eval.h — let 常量编译期求值接口
 *
 * 从 analyzer.c 中拆分的独立模块，负责 let 定义的编译期常量计算。
 * 包含循环依赖检测、运行时错误到常量错误映射、类型检查等逻辑。
 */
#ifndef TC_CONST_EVAL_H
#define TC_CONST_EVAL_H

#include "tc_symbol.h"
#include "tc_types.h"

/**
 * 编译期求值 let RHS，写入符号表的 const_value 字段。
 * @param sym     目标符号（输出：has_const_value=1, const_value=计算结果）
 * @param rhs     let 初始化表达式
 * @param visible 当前可见符号表（包含之前定义的符号）
 * @param global  全局符号表（用于前向引用解析）
 * @param line    当前行号
 * @param diag    诊断对象
 * @return 成功 0；失败 -1 并设置 diag
 */
int tc_resolve_const_value(TcSymbol *sym, const TcRhs *rhs, const TcSymbolTable *visible,
                           const TcSymbolTable *global, int line, TcDiagnostic *diag);

#endif /* TC_CONST_EVAL_H */
