/*
 * tc_const_eval.h — let 常量编译期求值接口
 *
 * 源序求值 let；禁止自引用/前向引用。运行时错误映射为编译期常量错误。
 * 静态布尔三态（if/while 条件、短路读边）也在本模块。
 */
#ifndef TC_CONST_EVAL_H
#define TC_CONST_EVAL_H

#include "tc_symbol.h"
#include "tc_types.h"

struct TcStructTable;

typedef enum {
    TC_STATIC_BOOL_UNKNOWN = -1,
    TC_STATIC_BOOL_FALSE = 0,
    TC_STATIC_BOOL_TRUE = 1
} TcStaticBoolResult;

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
                           const TcSymbolTable *global, const struct TcStructTable *struct_table,
                           int line, TcDiagnostic *diag);

/** 使用 Pass2 已解析绑定判断一个 bool 操作数是否为静态常量。 */
void tc_try_eval_static_bool_operand(const TcOperand *operand, TcStaticBoolResult *result);

/**
 * 按 TC 0.0.42 §5.2.4 求值合法的单层静态布尔 RHS。
 * unknown 不设置诊断；常量语义错误返回 -1 并按 §5.2.3 映射诊断。
 */
int tc_try_eval_static_bool(const TcRhs *rhs, int line, TcStaticBoolResult *result,
                            TcDiagnostic *diag);

#endif /* TC_CONST_EVAL_H */
