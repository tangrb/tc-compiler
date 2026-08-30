/*
 * tc_aot_codegen_internal.h — AOT Codegen 子模块共享声明
 *
 * tc_aot_codegen.c / tc_aot_emit_rhs.c / tc_aot_emit_stmt.c / tc_aot_emit_func.c
 * 四个源文件共享的结构体与跨文件函数声明，不对外暴露。
 */
#ifndef TC_AOT_CODEGEN_INTERNAL_H
#define TC_AOT_CODEGEN_INTERNAL_H

#include "tc_aot_codegen.h"

#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* A1 端序契约（§3.5）：codegen 期从 const_bits 折叠标量字段时按固定 LE
 * （低字节在前）读取，与 VM/const_eval/AOT 运行时一致，不依赖宿主字节序。 */
static inline uint64_t tc_aot_cg_load_bits(const uint8_t *src, size_t nbytes) {
    uint64_t bits = 0;
    size_t i = 0;

    for (i = 0; i < nbytes && i < sizeof(bits); i++) {
        bits |= ((uint64_t)src[i]) << (8U * i);
    }
    return bits;
}

/* 块路径深度上限（与 Analyzer / Executor 同构：then=if*2，else=if*2+1） */
#define TC_AOT_BLOCK_DEPTH_MAX 64
#define TC_AOT_LOOP_DEPTH_MAX 64

typedef struct {
    TcBlockId path[TC_AOT_BLOCK_DEPTH_MAX];
    int depth;
} TcAotBlockPath;

typedef struct {
    int loop_ids[TC_AOT_LOOP_DEPTH_MAX];
    int depth;
} TcAotLoopStack;

/** Codegen 阶段 DFS 语句序号 + 块路径（与 Analyzer / Executor 一致） */
typedef struct {
    TcStmtIndexCursor index;
    TcSymbolNameIndex sym_index;
    TcAotBlockPath block_path;
    TcAotLoopStack loops;
    const TcTypedProgram *program;
    int current_func_id; /* -1 = 顶层 */
    TcTypeTag current_return_type;
    int tmp_seq; /* 嵌套 RHS 临时变量唯一序号 */
    int embed_mode; /* 1 = 嵌入库模式 */
} TcAotEmitCtx;

typedef struct {
    char *param_name;
    TcRhs *value;
} TcAotFuncallExprArg;

/* 路径 / 循环栈辅助 */
int tc_aot_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth);
int tc_aot_block_path_push(TcAotBlockPath *bp, TcBlockId block_id);
void tc_aot_block_path_pop(TcAotBlockPath *bp);
int tc_aot_loop_stack_push(TcAotLoopStack *loops, int loop_id);
void tc_aot_loop_stack_pop(TcAotLoopStack *loops);

/* 符号解析辅助 */
const TcLabelEntry *tc_aot_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                              int func_id, const TcAotBlockPath *goto_path);
const TcSymbol *tc_aot_find_def_symbol(const TcSymbolTable *symbols, const char *name,
                                       int def_line);
const TcFuncDef *tc_aot_find_func_def(const TcTypedProgram *prog, int func_id,
                                      const TcProgram **out_module);
int tc_aot_func_body_index_range(const TcProgram *module, int func_id, int *out_body_start,
                                 int *out_body_end);
int tc_aot_param_slot(const TcSymbolTable *symbols, const TcFuncDef *func, const char *param_name,
                      int *out_slot);
const TcRhs *tc_aot_find_named_arg_rhs(const char *param_name, const TcNamedArg *args,
                                       size_t arg_count);
const TcRhs *tc_aot_find_expr_arg_rhs(const char *param_name, const TcAotFuncallExprArg *args,
                                      size_t arg_count);
const TcSymbol *tc_aot_find_symbol_by_name(const TcSymbolTable *symbols, const char *name);
int tc_aot_resolve_var_slot(const TcSymbolTable *symbols, const TcSymbolNameIndex *sym_index,
                            const char *name, int stmt_index, int *out_slot);

/* 枚举映射 / 字符串辅助 */
const char *tc_aot_type_enum(TcTypeTag type);
const char *tc_aot_format_enum(TcFormatSpec fmt);
const char *tc_aot_ptr_compare_op(TcCompareOp op);
void tc_aot_sub_indent(char *out, size_t out_size, const char *base, int levels);
void tc_aot_emit_c_string(FILE *out, const char *value);

/* 表达式发射 */
void tc_aot_emit_literal_expr(FILE *out, TcTypeTag type, const TcLiteral *lit);
void tc_aot_emit_const_memblock_expr(FILE *out, uint64_t host_bits, size_t nbytes, int line);
void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcTypeTag type,
                              const TcAotEmitCtx *ctx, int stmt_index);
int tc_aot_emit_operand_assign(FILE *out, const TcOperand *operand, TcTypeTag type,
                               const char *dst_expr, const char *indent, const TcAotEmitCtx *ctx,
                               int stmt_index);
int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcTypeTag expected_type, const char *dst_expr,
                    const char *indent, TcAotEmitCtx *ctx, int stmt_index, int line);
int tc_aot_emit_rhs_slot(FILE *out, const TcRhs *rhs, TcTypeTag expected_type, int dst_slot,
                         const char *indent, TcAotEmitCtx *ctx, int stmt_index, int line);
int tc_aot_emit_funcall(FILE *out, int func_id, const TcNamedArg *stmt_args,
                        size_t stmt_arg_count, const TcAotFuncallExprArg *expr_args,
                        size_t expr_arg_count, int use_expr_args, const char *indent,
                        TcAotEmitCtx *ctx, int stmt_index, int line, int want_result,
                        const char *dst_expr);

/* 语句 / 函数发射 */
int tc_aot_emit_statement_impl(FILE *out, const TcStatement *stmt, TcAotEmitCtx *ctx,
                               const char *indent);
int tc_aot_emit_static_vars_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx);
int tc_aot_emit_function(FILE *out, const TcFuncDef *func, const TcProgram *module,
                         TcAotEmitCtx *ctx);
int tc_aot_emit_functions_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx);
void tc_aot_emit_func_decls(FILE *out, const TcTypedProgram *program, TcAotEmitCtx *ctx);
int tc_aot_emit_func_table(FILE *out, const TcTypedProgram *program);

#endif /* TC_AOT_CODEGEN_INTERNAL_H */
