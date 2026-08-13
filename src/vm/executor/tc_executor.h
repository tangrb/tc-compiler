/*
 * tc_executor.h — 执行引擎接口
 *
 * Executor 消费 Analyzer 产出的 TcTypedProgram，按语句顺序 dispatch：
 * 求值 RHS 并将结果写入对应变量的运行时槽位。
 * TC 源码即「高级字节码」，无需 lowering 为第二套 opcode。
 */
#ifndef TC_EXECUTOR_H
#define TC_EXECUTOR_H

#include "tc_types.h"

/* 前向声明 — TcExecuteCtx typedef 与完整定义在 tc_executor_internal.h（C99 禁止重复 typedef） */
struct TcExecuteCtx;

typedef enum {
    TC_EXEC_NORMAL,
    TC_EXEC_BREAK,
    TC_EXEC_CONTINUE,
    TC_EXEC_GOTO,
    TC_EXEC_RETURN,
    TC_EXEC_ERROR
} TcExecControlKind;

typedef struct {
    TcExecControlKind kind;
    int loop_id;
    int target_stmt_index; /* TC_EXEC_GOTO 时为 Analyzer 解析的 label stmt_index */
    TcValue return_value;
    int has_return_value;
} TcExecControl;

/**
 * 执行已类型化的程序。
 * @param program 已类型化的程序（含语句列表和符号表）
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误（溢出、除零、浮点异常等）返回 -1 并设置 diag
 */
int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag);

/**
 * 执行单条语句（单元测试 / 白盒驱动；使用已有变量槽数组）。
 * @param stmt    待执行的语句
 * @param slots   运行时变量槽位数组（长度至少 symbols->count）
 * @param symbols 全局符号表
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误返回 -1 并设置 diag
 */
int tc_execute_statement(const TcStatement *stmt, TcValue *slots, const TcSymbolTable *symbols,
                         TcDiagnostic *diag);

/**
 * 按 func_id 执行函数（公共包装）。
 * 调用前实参必须已写入形参 slot。
 * @param func_id    函数 ID
 * @param ctx        执行器上下文
 * @param ret_out    非 NULL 时写入返回值（仅 want_return 为真时写入）
 * @param want_return 函数是否有返回值
 * @param diag       诊断对象
 * @param line       错误报告行号
 * @return 成功返回 0；运行时错误返回 -1 并设置 diag
 */
int tc_exec_call_function_public(int func_id, struct TcExecuteCtx *ctx,
                                  TcValue *ret_out, int want_return,
                                  TcDiagnostic *diag, int line);

/**
 * 初始化所有 static var（入口 + 依赖模块）。
 * 供 embed 模块在 tc_embed_create 中调用。
 * @param program 已类型化的程序
 * @param ctx     执行器上下文（slots 须已分配）
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag
 */
int tc_exec_init_all_static_vars(const TcTypedProgram *program, struct TcExecuteCtx *ctx,
                                 TcDiagnostic *diag);

#endif
