/*
 * tc_aot_codegen.c — TC → C99 转译（AOT Codegen）
 *
 * 消费 Analyzer 产出的 TcTypedProgram，逐语句生成等价的 C99 代码。
 * 生成的代码由一个 main() 函数 + 一个 uint64_t slot 数组组成，
 * 格式为：`#include "tc_aot_rt.h"` → main → 初始化 slots → 逐语句 emit。
 *
 * 设计原则：
 *   - 算术、cast、比较、逻辑、位运算、I/O 均通过 tc_aot_rt.h 中的 shim 函数
 *     委托 tc_semantics.c / tc_io.c，保证与 TC-VM 行为完全一致。
 *   - let 常量编译器已求值，Codegen 直接将 const_value.bits 写为字面量。
 *   - CONST_REF / CONST_CAST 在 Analyzer 阶段应已被折叠，Codegen 发现则报错。
 */
#include "tc_aot_codegen.h"

#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/** Codegen 阶段 DFS 语句序号（与 Analyzer / Executor 一致） */
typedef struct {
    TcStmtIndexCursor index;
    TcSymbolNameIndex sym_index;
} TcAotEmitCtx;

static const TcSymbol *tc_aot_find_def_symbol(const TcSymbolTable *symbols, const char *name,
                                              int def_line) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->def_line == def_line && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  辅助函数                                                           */
/* ------------------------------------------------------------------ */

/** 将 TcType 枚举映射为 C 源码中的枚举名 */
static const char *tc_aot_type_enum(TcType type) {
    switch (type) {
    case TC_INT8:
        return "TC_INT8";
    case TC_UINT8:
        return "TC_UINT8";
    case TC_INT16:
        return "TC_INT16";
    case TC_UINT16:
        return "TC_UINT16";
    case TC_INT32:
        return "TC_INT32";
    case TC_UINT32:
        return "TC_UINT32";
    case TC_INT64:
        return "TC_INT64";
    case TC_UINT64:
        return "TC_UINT64";
    case TC_BOOL:
        return "TC_BOOL";
    case TC_FLOAT32:
        return "TC_FLOAT32";
    case TC_FLOAT64:
        return "TC_FLOAT64";
    }
    return "TC_INT32";
}

/** 将 TcFormatSpec 枚举映射为 C 源码中的枚举名 */
static const char *tc_aot_format_enum(TcFormatSpec fmt) {
    switch (fmt) {
    case TC_FMT_NONE:
        return "TC_FMT_NONE";
    case TC_FMT_D:
        return "TC_FMT_D";
    case TC_FMT_I:
        return "TC_FMT_I";
    case TC_FMT_U:
        return "TC_FMT_U";
    case TC_FMT_X:
        return "TC_FMT_X";
    case TC_FMT_XU:
        return "TC_FMT_XU";
    case TC_FMT_O:
        return "TC_FMT_O";
    case TC_FMT_B:
        return "TC_FMT_B";
    case TC_FMT_T:
        return "TC_FMT_T";
    case TC_FMT_F:
        return "TC_FMT_F";
    case TC_FMT_E:
        return "TC_FMT_E";
    case TC_FMT_EU:
        return "TC_FMT_EU";
    case TC_FMT_G:
        return "TC_FMT_G";
    case TC_FMT_GU:
        return "TC_FMT_GU";
    }
    return "TC_FMT_NONE";
}

/** 在 base 缩进后追加 levels×4 个空格，写入 out（至少容纳 base + levels*4 + 1） */
static void tc_aot_sub_indent(char *out, size_t out_size, const char *base, int levels) {
    size_t len = strlen(base);
    size_t extra = (size_t)levels * 4U;

    if (len + extra + 1U > out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, base, len);
    memset(out + len, ' ', extra);
    out[len + extra] = '\0';
}

/* ------------------------------------------------------------------ */
/*  表达式发射                                                          */
/* ------------------------------------------------------------------ */

/** 发射字面量构造表达式 */
static void tc_aot_emit_literal_expr(FILE *out, TcType type, const TcLiteral *lit) {
    if (lit->is_bool) {
        fprintf(out, "tc_aot_lit(%s, %lluULL, 0, 0)", tc_aot_type_enum(TC_BOOL),
                lit->magnitude ? 1ULL : 0ULL);
        return;
    }
    if (lit->is_float) {
        double d = lit->float_value;
        if (type == TC_FLOAT32) {
            float f = (float)d;
            uint32_t b32 = 0;
            memcpy(&b32, &f, sizeof(b32));
            fprintf(out, "tc_aot_lit(%s, 0x%xULL, 0, 0)", tc_aot_type_enum(type), b32);
        } else {
            uint64_t b64 = 0;
            memcpy(&b64, &d, sizeof(b64));
            fprintf(out, "tc_aot_lit(%s, 0x%" PRIx64 "ULL, 0, 0)", tc_aot_type_enum(type), b64);
        }
        return;
    }
    fprintf(out, "tc_aot_lit(%s, %" PRIu64 "ULL, %d, %d)", tc_aot_type_enum(type), lit->magnitude,
            lit->negative, lit->unsigned_suffix);
}

/** 发射操作数表达式：字面量或 slots[slot] */
static void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcType type,
                                     const TcSymbolTable *symbols,
                                     const TcSymbolNameIndex *sym_index, int stmt_index) {
    if (operand->kind == TC_OPERAND_LIT) {
        tc_aot_emit_literal_expr(out, type, &operand->u.lit);
    } else {
        const TcSymbol *symbol =
            tc_symbol_table_find_visible(symbols, operand->u.name, stmt_index, sym_index);

        if (!symbol) {
            return;
        }
        fprintf(out, "slots[%d]", symbol->slot);
    }
}

/*
 * RHS 发射：按 TcRhsKind 分派到不同的代码生成逻辑。
 *
 * 每种 RHS 生成对应的 tc_aot_*() 调用（委托 tc_semantics.c），
 * 调用结果以"if (tc_aot_*(...) != 0) tc_aot_abort(...)"模式包裹，
 * 确保运行时错误能通过 tc_aot_abort 传播诊断信息。
 *
 * LIT 和 CONST_REF/CAST 特殊处理：
 *   - LIT：直接 inline 字面量值到 slots[dst_slot]
 *   - CONST_REF/CAST：理论上已在 Analyzer 折叠，出现即报内部错误
 */
/* ------------------------------------------------------------------ */
/*  RHS 发射                                                            */
/* ------------------------------------------------------------------ */

/** 发射 RHS 求值代码；dst_expr 为目标左值（如 slots[3] 或 _cond） */
static int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcType expected_type,
                           const char *dst_expr, const char *indent,
                           const TcSymbolTable *symbols, const TcSymbolNameIndex *sym_index,
                           int stmt_index, int line) {
    char abort_indent[64];

    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    if (rhs->kind == TC_RHS_LIT) {
        fprintf(out, "%s%s = ", indent, dst_expr);
        tc_aot_emit_literal_expr(out, expected_type, &rhs->u.lit);
        fprintf(out, ";\n");
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        const char *mode =
            rhs->u.arith.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = NULL;

        switch (rhs->u.arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }

        fprintf(out, "%sif (tc_aot_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.arith.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.arith.lhs, rhs->u.arith.type, symbols, sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.arith.rhs, rhs->u.arith.type, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        const char *mode =
            rhs->u.unary.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = rhs->u.unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.unary.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.unary.operand, rhs->u.unary.type, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        const char *op_name = "TC_CMP_EQ";
        switch (rhs->u.compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_compare(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.compare.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.compare.lhs, rhs->u.compare.type, symbols, sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.compare.rhs, rhs->u.compare.type, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        fprintf(out, "%sif (tc_aot_logic_unary(TC_LOGIC_NOT, &%s, ", indent, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_un.operand, TC_BOOL, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        const char *op_name =
            rhs->u.logic_bin.op == TC_LOGIC_AND ? "TC_LOGIC_AND" : "TC_LOGIC_OR";
        fprintf(out, "%sif (tc_aot_logic(%s, &%s, ", indent, op_name, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.lhs, TC_BOOL, symbols, sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.rhs, TC_BOOL, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        const char *op_name = "TC_BIT_AND";

        switch (rhs->u.bitwise_bin.op) {
        case TC_BIT_AND:
            op_name = "TC_BIT_AND";
            break;
        case TC_BIT_OR:
            op_name = "TC_BIT_OR";
            break;
        case TC_BIT_XOR:
            op_name = "TC_BIT_XOR";
            break;
        }

        fprintf(out, "%sif (tc_aot_bitwise_binary(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.bitwise_bin.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type, symbols,
                                 sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type, symbols,
                                 sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        fprintf(out, "%sif (tc_aot_bitwise_unary(%s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitwise_un.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type, symbols,
                                 sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        const char *op_name =
            rhs->u.shift.op == TC_SHIFT_SHL ? "TC_SHIFT_SHL" : "TC_SHIFT_SHR";
        const char *mode =
            rhs->u.shift.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";

        fprintf(out, "%sif (tc_aot_shift(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.shift.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.shift.value, rhs->u.shift.type, symbols, sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.shift.count, rhs->u.shift.type, symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    /*
     * CONST_REF / CONST_CAST 应在 Analyzer 阶段已被编译期折叠
     * （tc_resolve_const_value + const_value.bits 写入），
     * 或在 tc_aot_emit_statement 中以字面量直接 emit。
     * 若在此处遇到，说明 Analyzer 未正确处理，严格报错以防生成错误代码。
     */
    if (rhs->kind == TC_RHS_CONST_REF || rhs->kind == TC_RHS_CONST_CAST) {
        return -1;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        const char *mode = rhs->u.float_arith.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE" :
                           rhs->u.float_arith.mode == TC_FLOAT_WRAP ? "TC_FLOAT_WRAP" :
                           "TC_FLOAT_STRICT";
        const char *op_name = "TC_ADD";

        switch (rhs->u.float_arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_arith.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.lhs, rhs->u.float_arith.type, symbols,
                                 sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.rhs, rhs->u.float_arith.type, symbols,
                                 sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        const char *mode = rhs->u.float_unary.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE" :
                           rhs->u.float_unary.mode == TC_FLOAT_WRAP ? "TC_FLOAT_WRAP" :
                           "TC_FLOAT_STRICT";
        const char *op_name =
            rhs->u.float_unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_fp_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_unary.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_unary.operand, rhs->u.float_unary.type,
                                 symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        const char *mode = rhs->u.float_compare.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE" :
                           "TC_FLOAT_STRICT";
        const char *op_name = "TC_CMP_EQ";

        switch (rhs->u.float_compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_compare(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_compare.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.lhs, rhs->u.float_compare.type,
                                 symbols, sym_index, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.rhs, rhs->u.float_compare.type,
                                 symbols, sym_index, stmt_index);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_CAST) {
        const TcSymbol *source =
            tc_symbol_table_find_visible(symbols, rhs->u.float_cast.source, stmt_index, sym_index);
        const char *mode =
            rhs->u.float_cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        if (!source) {
            return -1;
        }

        fprintf(out, "%sif (tc_aot_fp_cast(%s, %s, slots[%d], %s, &%s, &diag, %d) != 0)\n",
                indent, tc_aot_type_enum(rhs->u.float_cast.target), mode, source->slot,
                tc_aot_type_enum(source->type), dst_expr, line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind != TC_RHS_CAST) {
        return -1;
    }

    {
        const TcSymbol *source =
            tc_symbol_table_find_visible(symbols, rhs->u.cast.source, stmt_index, sym_index);
        const char *mode =
            rhs->u.cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        if (!source) {
            return -1;
        }

        if (tc_type_is_float(source->type) || tc_type_is_float(rhs->u.cast.target)) {
            fprintf(out, "%sif (tc_aot_fp_cast(%s, %s, slots[%d], %s, &%s, &diag, %d) != 0)\n",
                    indent, tc_aot_type_enum(rhs->u.cast.target), mode, source->slot,
                    tc_aot_type_enum(source->type), dst_expr, line);
        } else {
            fprintf(out, "%sif (tc_aot_cast(%s, %s, slots[%d], %s, &%s, &diag, %d) != 0)\n", indent,
                    tc_aot_type_enum(rhs->u.cast.target), mode, source->slot,
                    tc_aot_type_enum(source->type), dst_expr, line);
        }
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
    }
    return 0;
}

/** 将 RHS 结果写入 slots[dst_slot] */
static int tc_aot_emit_rhs_slot(FILE *out, const TcRhs *rhs, TcType expected_type, int dst_slot,
                                const char *indent, const TcSymbolTable *symbols,
                                const TcSymbolNameIndex *sym_index, int stmt_index, int line) {
    char dst_expr[32];

    snprintf(dst_expr, sizeof(dst_expr), "slots[%d]", dst_slot);
    return tc_aot_emit_rhs(out, rhs, expected_type, dst_expr, indent, symbols, sym_index,
                           stmt_index, line);
}

/* ------------------------------------------------------------------ */
/*  语句发射                                                            */
/* ------------------------------------------------------------------ */

static int tc_aot_emit_statement_impl(FILE *out, const TcStatement *stmt,
                                      const TcSymbolTable *symbols, TcAotEmitCtx *ctx,
                                      const char *indent) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        char block_indent[64];
        char branch_indent[64];
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(block_indent, sizeof(block_indent), indent, 1);
        tc_aot_sub_indent(branch_indent, sizeof(branch_indent), block_indent, 1);

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%suint64_t _cond;\n", block_indent);

        if (tc_aot_emit_rhs(out, &if_stmt->condition, TC_BOOL, "_cond", block_indent, symbols,
                            &ctx->sym_index, if_stmt_index, if_stmt->line) != 0) {
            return -1;
        }

        fprintf(out, "%sif (_cond != 0) {\n", block_indent);
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_aot_emit_statement_impl(out, &if_stmt->then_body[i], symbols, ctx,
                                           branch_indent) != 0) {
                return -1;
            }
        }

        if (if_stmt->else_count > 0) {
            fprintf(out, "%s} else {\n", block_indent);
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_aot_emit_statement_impl(out, &if_stmt->else_body[i], symbols, ctx,
                                               branch_indent) != 0) {
                    return -1;
                }
            }
        }

        fprintf(out, "%s}\n", block_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    {
        int stmt_index = tc_stmt_index_take(&ctx->index);
        const TcSymbol *symbol = NULL;

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;

            symbol = tc_aot_find_def_symbol(symbols, var_def->name, var_def->line);
            if (!symbol) {
                return -1;
            }
            if (!var_def->has_rhs) {
                return 0;
            }
            return tc_aot_emit_rhs_slot(out, &var_def->rhs, var_def->type, symbol->slot, indent,
                                        symbols, &ctx->sym_index, stmt_index, var_def->line);
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            const TcConstDef *const_def = &stmt->u.const_def;

            symbol = tc_aot_find_def_symbol(symbols, const_def->name, const_def->line);
            if (!symbol) {
                return -1;
            }
            if (symbol->has_const_value) {
                fprintf(out, "%sslots[%d] = 0x%016" PRIx64 "ULL;\n", indent, symbol->slot,
                        symbol->const_value.bits);
            } else {
                return tc_aot_emit_rhs_slot(out, &const_def->rhs, const_def->type, symbol->slot,
                                            indent, symbols, &ctx->sym_index, stmt_index,
                                            const_def->line);
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_ASSIGN) {
            const TcAssign *assign = &stmt->u.assign;

            symbol = tc_symbol_table_find_visible(symbols, assign->name, stmt_index, &ctx->sym_index);
            if (!symbol) {
                return -1;
            }
            return tc_aot_emit_rhs_slot(out, &assign->rhs, symbol->type, symbol->slot, indent,
                                        symbols, &ctx->sym_index, stmt_index, assign->line);
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io = &stmt->u.io_write;
            int newline = stmt->kind == TC_STMT_WRITELN ? 1 : 0;

            fprintf(out, "%stc_aot_write(%s, %s, ", indent, tc_aot_type_enum(io->type),
                    tc_aot_format_enum(io->fmt));
            tc_aot_emit_operand_expr(out, &io->operand, io->type, symbols, &ctx->sym_index,
                                     stmt_index);
            fprintf(out, ", %d);\n", newline);
            return 0;
        }

        if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            char abort_indent[64];

            symbol = tc_symbol_table_find_visible(symbols, io_read->name, stmt_index,
                                                  &ctx->sym_index);
            if (!symbol) {
                return -1;
            }
            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

            fprintf(out, "%sif (tc_aot_read(%s, &slots[%d], &diag, %d) != 0)\n", indent,
                    tc_aot_type_enum(io_read->type), symbol->slot, io_read->line);
            fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, io_read->line);
            return 0;
        }
    }

    return 0;
}

static int tc_aot_emit_statement(FILE *out, const TcStatement *stmt, const TcSymbolTable *symbols,
                                 TcAotEmitCtx *ctx, const char *indent) {
    return tc_aot_emit_statement_impl(out, stmt, symbols, ctx, indent);
}

/* ------------------------------------------------------------------ */
/*  C 文件生成入口                                                       */
/* ------------------------------------------------------------------ */

int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name) {
    size_t i = 0;
    size_t slot_count = program->symbols.count;
    TcAotEmitCtx ctx;
    TcDiagnostic diag;
    int rc = 0;

    tc_stmt_index_reset(&ctx.index);
    tc_symbol_name_index_init(&ctx.sym_index);
    tc_diagnostic_init(&diag);
    if (tc_symbol_name_index_build(&program->symbols, &ctx.sym_index, &diag) != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        return -1;
    }

    fprintf(out, "/* Auto-generated by tc-aot from %s. Do not edit. */\n",
            source_name ? source_name : "<source>");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include \"tc_aot_rt.h\"\n\n");

    if (slot_count > 0) {
        fprintf(out, "static uint64_t slots[%zu];\n\n", slot_count);
    }

    fprintf(out, "int main(void) {\n");
    fprintf(out, "    TcDiagnostic diag;\n");
    fprintf(out, "    tc_aot_diag_init(&diag);\n");
    if (slot_count > 0) {
        fprintf(out, "    tc_aot_init_slots(slots, %zu);\n", slot_count);
    }
    fprintf(out, "\n");

    for (i = 0; i < program->program.count; i++) {
        if (tc_aot_emit_statement(out, &program->program.items[i], &program->symbols, &ctx,
                                  "    ") != 0) {
            rc = -1;
            break;
        }
    }

    fprintf(out, "\n    return 0;\n");
    fprintf(out, "}\n");
    tc_symbol_name_index_free(&ctx.sym_index);
    tc_diagnostic_clear(&diag);
    return rc;
}
