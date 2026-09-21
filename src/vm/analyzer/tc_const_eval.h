/*
 * tc_const_eval.h — let 常量编译期求值接口
 *
 * 源序求值 let；禁止自引用/前向引用。运行时错误映射为编译期常量错误。
 * 静态布尔三态（if/while 条件、短路读边）也在本模块。
 */
#ifndef TC_CONST_EVAL_H
#define TC_CONST_EVAL_H

#include "tc_scope.h"
#include "tc_symbol.h"
#include "tc_types.h"

#include "tc_diagnostic.h"

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
 * @param members 当前模块成员索引（`Self.<名>` 归属判定；可为 NULL）
 * @param line    当前行号
 * @param diag    诊断对象
 * @return 成功 0；失败 -1 并设置 diag
 */
int tc_resolve_const_value(TcSymbol *sym, const TcRhs *rhs, const TcSymbolTable *visible,
                           const TcSymbolTable *global, const struct TcStructTable *struct_table,
                           const TcMemberIndex *members, int line, TcDiagnostic *diag);

/** 使用 Pass2 已解析绑定判断一个 bool 操作数是否为静态常量。 */
void tc_try_eval_static_bool_operand(const TcOperand *operand, TcStaticBoolResult *result);

/**
 * 按语言标准 §5.2.2 求值合法的单层静态布尔 RHS（`if` / `while` 条件的
 * 三态判定）。原子操作数限 §5.2.1 原子表达式集合：字面量、`nullptr`、
 * 更早且已求值的 `let`、经 `Self.` / 导入限定解析到的 `static let`、
 * 只读结构体字段读取，以及 `mb.count`。
 * unknown 不设置诊断；常量语义错误返回 -1 并按 §5.2.3 映射诊断。
 *
 * @param symbols 程序符号表（解析 `Self.<名>` / 限定名与字段读基址）
 */
int tc_try_eval_static_bool(const TcRhs *rhs, const TcSymbolTable *symbols,
                            const struct TcStructTable *struct_table, int line,
                            TcStaticBoolResult *result, TcDiagnostic *diag);

/**
 * 该常量符号的值是否因**挂起的 CT 类诊断**而不可用。
 *
 * 语言标准 §11「阶段优先」：CT 类诊断挂起后，依赖该常量的
 * 后续求值失败属于该诊断的**派生**结果，而非独立的 SEM 类错误。调用方在
 * 本函数返回 1 时应直接返回 -1 且**不另设诊断**——挂起的 CT 诊断仍是首个
 * 规范诊断，会在全部 SEM 检查无触发后由分析器统一发布。
 *
 * @return 属于派生失败返回 1；需要调用方自行报告 SEM 诊断返回 0
 */
int tc_const_value_withheld(const TcSymbol *sym, const TcDiagnostic *diag);

#endif /* TC_CONST_EVAL_H */
