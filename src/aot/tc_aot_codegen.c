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
 *   - if → 原生 C if-else；label/goto → tc_label_<stmt_index>（无 shim）。
 */
#include "tc_aot_codegen.h"

#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/** 块路径深度上限（与 Analyzer / Executor 同构：then=if*2，else=if*2+1） */
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
} TcAotEmitCtx;

static int tc_aot_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth) {
    int i = 0;

    for (i = 0; i < depth; i++) {
        if (a[i].owner_stmt_index != b[i].owner_stmt_index || a[i].kind != b[i].kind) {
            return 0;
        }
    }
    return 1;
}

static int tc_aot_block_path_push(TcAotBlockPath *bp, TcBlockId block_id) {
    if (bp->depth >= TC_AOT_BLOCK_DEPTH_MAX) {
        return -1;
    }
    bp->path[bp->depth++] = block_id;
    return 0;
}

static void tc_aot_block_path_pop(TcAotBlockPath *bp) {
    if (bp->depth > 0) {
        bp->depth--;
    }
}

static int tc_aot_loop_stack_push(TcAotLoopStack *loops, int loop_id) {
    if (loop_id < 0 || loops->depth >= TC_AOT_LOOP_DEPTH_MAX) {
        return -1;
    }
    loops->loop_ids[loops->depth++] = loop_id;
    return 0;
}

static void tc_aot_loop_stack_pop(TcAotLoopStack *loops) {
    if (loops->depth > 0) {
        loops->depth--;
    }
}

/**
 * 解析 goto 目标：优先同路径，其次最近祖先（与 Analyzer / Executor 一致）。
 */
static const TcLabelEntry *tc_aot_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                                     const TcAotBlockPath *goto_path) {
    const TcLabelEntry *best_same = NULL;
    const TcLabelEntry *best_ancestor = NULL;
    const TcLabelEntry *any = NULL;
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0) {
            continue;
        }
        any = entry;
        if (entry->block_depth == goto_path->depth &&
            tc_aot_paths_equal_prefix(entry->block_path, goto_path->path, entry->block_depth)) {
            best_same = entry;
        } else if (entry->block_depth < goto_path->depth &&
                   tc_aot_paths_equal_prefix(entry->block_path, goto_path->path,
                                              entry->block_depth)) {
            if (!best_ancestor || entry->block_depth > best_ancestor->block_depth) {
                best_ancestor = entry;
            }
        }
    }
    if (best_same) {
        return best_same;
    }
    if (best_ancestor) {
        return best_ancestor;
    }
    return any;
}

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

/** 将 TcTypeKind 枚举映射为 C 源码中的枚举名 */
static const char *tc_aot_type_enum(TcTypeKind type) {
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

/** 将任意源文件名发射为不依赖宿主扩展的 C99 字符串字面量。 */
static void tc_aot_emit_c_string(FILE *out, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "<source>");

    fputc('"', out);
    while (*p != '\0') {
        switch (*p) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20U || *p >= 0x7fU) {
                fprintf(out, "\\%03o", (unsigned int)*p);
            } else {
                fputc((int)*p, out);
            }
            break;
        }
        p++;
    }
    fputc('"', out);
}

/* ------------------------------------------------------------------ */
/*  表达式发射                                                          */
/* ------------------------------------------------------------------ */

/** 发射字面量构造表达式 */
static void tc_aot_emit_literal_expr(FILE *out, TcTypeKind type, const TcLiteral *lit) {
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

/** 发射操作数表达式：字面量、内联 let 位模式或 var 槽。 */
static void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcTypeKind type,
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
        if (symbol->sym_kind == TC_SYM_CONSTANT && symbol->has_const_value) {
            fprintf(out, "0x%016" PRIx64 "ULL", symbol->const_value.bits);
        } else {
            fprintf(out, "slots[%d]", symbol->slot);
        }
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
 *   - CONST_REF：let 发射规范化十六进制位模式，var 发射 slot 读取
 *   - CONST_CAST：理论上已在 Analyzer 折叠，出现即报内部错误
 */
/* ------------------------------------------------------------------ */
/*  RHS 发射                                                            */
/* ------------------------------------------------------------------ */

/** 发射 RHS 求值代码；dst_expr 为目标左值（如 slots[3] 或 _cond） */
static int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcTypeKind expected_type,
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

    if (rhs->kind == TC_RHS_CONST_REF) {
        const TcSymbol *symbol = tc_symbol_table_find_visible(
            symbols, rhs->u.const_ref.name, stmt_index, sym_index);

        if (!symbol) {
            return -1;
        }
        if (symbol->sym_kind == TC_SYM_CONSTANT && symbol->has_const_value) {
            fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                    symbol->const_value.bits);
        } else {
            fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr, symbol->slot);
        }
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

    if (rhs->kind == TC_RHS_BITCAST) {
        fprintf(out, "%sif (tc_aot_bitcast(%s, %s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitcast.target),
                tc_aot_type_enum(rhs->u.bitcast.source_type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitcast.source,
                                 rhs->u.bitcast.source_type, symbols, sym_index, stmt_index);
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
    if (rhs->kind == TC_RHS_CONST_CAST) {
        return -1;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        const char *mode = rhs->u.float_arith.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
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
        const char *mode = rhs->u.float_unary.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
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

    if (rhs->kind != TC_RHS_CAST) {
        return -1;
    }

    {
        const char *mode =
            rhs->u.cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        fprintf(out, "%sif (tc_aot_cast(%s, %s, ", indent,
                tc_aot_type_enum(rhs->u.cast.target), mode);
        tc_aot_emit_operand_expr(out, &rhs->u.cast.source, rhs->u.cast.source_type,
                                 symbols, sym_index, stmt_index);
        fprintf(out, ", %s, &%s, &diag, %d) != 0)\n",
                tc_aot_type_enum(rhs->u.cast.source_type), dst_expr, line);
        fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, line);
    }
    return 0;
}

/** 将 RHS 结果写入 slots[dst_slot] */
static int tc_aot_emit_rhs_slot(FILE *out, const TcRhs *rhs, TcTypeKind expected_type, int dst_slot,
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
    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        char loop_indent[64];
        char body_indent[64];
        char control_indent[64];
        char cond_name[32];
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(loop_indent, sizeof(loop_indent), indent, 1);
        tc_aot_sub_indent(body_indent, sizeof(body_indent), loop_indent, 1);
        tc_aot_sub_indent(control_indent, sizeof(control_indent), body_indent, 1);
        if (loop_indent[0] == '\0' || body_indent[0] == '\0' ||
            control_indent[0] == '\0') {
            return -1;
        }
        if (snprintf(cond_name, sizeof(cond_name), "tc_cond_%d", while_stmt_index) < 0) {
            return -1;
        }

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%sfor (;;) {\n", loop_indent);
        fprintf(out, "%suint64_t %s;\n", body_indent, cond_name);
        if (tc_aot_emit_rhs(out, &while_stmt->condition, TC_BOOL, cond_name, body_indent,
                            symbols, &ctx->sym_index, while_stmt_index,
                            while_stmt->line) != 0) {
            return -1;
        }
        fprintf(out, "%sif (%s == 0) {\n", body_indent, cond_name);
        fprintf(out, "%sbreak;\n", control_indent);
        fprintf(out, "%s}\n", body_indent);

        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){while_stmt_index, TC_BLOCK_WHILE}) != 0 ||
            tc_aot_loop_stack_push(&ctx->loops, while_stmt->loop_id) != 0) {
            return -1;
        }
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_aot_emit_statement_impl(out, &while_stmt->body[i], symbols, ctx,
                                           body_indent) != 0) {
                return -1;
            }
        }
        tc_aot_loop_stack_pop(&ctx->loops);
        tc_aot_block_path_pop(&ctx->block_path);

        fprintf(out, "%s}\n", loop_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

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
        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){if_stmt_index, TC_BLOCK_IF_THEN}) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_aot_emit_statement_impl(out, &if_stmt->then_body[i], symbols, ctx,
                                           branch_indent) != 0) {
                return -1;
            }
        }
        tc_aot_block_path_pop(&ctx->block_path);

        if (if_stmt->else_count > 0) {
            fprintf(out, "%s} else {\n", block_indent);
            if (tc_aot_block_path_push(
                    &ctx->block_path,
                    (TcBlockId){if_stmt_index, TC_BLOCK_IF_ELSE}) != 0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_aot_emit_statement_impl(out, &if_stmt->else_body[i], symbols, ctx,
                                               branch_indent) != 0) {
                    return -1;
                }
            }
            tc_aot_block_path_pop(&ctx->block_path);
        }

        fprintf(out, "%s}\n", block_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    /* 标签：生成原生 C 标签；空语句保证可位于复合语句末尾 */
    if (stmt->kind == TC_STMT_LABEL_DEF) {
        int stmt_index_val = tc_stmt_index_take(&ctx->index);

        /* 未被 TC goto 引用的合法标签也必须通过 host cc 的 -Wunused-label。 */
        fprintf(out, "%sif (0) goto tc_label_%d;\n", indent, stmt_index_val);
        fprintf(out, "%stc_label_%d: ;\n", indent, stmt_index_val);
        return 0;
    }

    /* goto：按块路径解析目标，生成 goto tc_label_<stmt_index> */
    if (stmt->kind == TC_STMT_GOTO) {
        const TcLabelEntry *entry = NULL;

        tc_stmt_index_take(&ctx->index);
        entry = tc_aot_resolve_goto_label(symbols, stmt->u.goto_stmt.target, &ctx->block_path);
        if (!entry) {
            return -1;
        }
        fprintf(out, "%sgoto tc_label_%d;\n", indent, entry->stmt_index);
        return 0;
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        const TcLoopControlStmt *loop_control =
            stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (ctx->loops.depth == 0 ||
            ctx->loops.loop_ids[ctx->loops.depth - 1] != loop_control->loop_id) {
            return -1;
        }
        fprintf(out, "%s%s;\n", indent,
                stmt->kind == TC_STMT_BREAK ? "break" : "continue");
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
            return tc_aot_emit_rhs_slot(out, &var_def->rhs, var_def->type, symbol->slot, indent,
                                        symbols, &ctx->sym_index, stmt_index, var_def->line);
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            /* let 是纯编译期声明，不发射运行时代码。 */
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
            char abort_indent[64];

            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
            fprintf(out, "%sif (tc_aot_write(%s, %s, ", indent, tc_aot_type_enum(io->type),
                    tc_aot_format_enum(io->fmt));
            tc_aot_emit_operand_expr(out, &io->operand, io->type, symbols, &ctx->sym_index,
                                     stmt_index);
            fprintf(out, ", %d, &diag, %d) != 0)\n", newline, io->line);
            fprintf(out, "%stc_aot_abort(&diag, %d);\n", abort_indent, io->line);
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

    return -1;
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
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);
    TcAotEmitCtx ctx;
    TcDiagnostic diag;
    int rc = 0;

    tc_stmt_index_reset(&ctx.index);
    tc_symbol_name_index_init(&ctx.sym_index);
    ctx.block_path.depth = 0;
    ctx.loops.depth = 0;
    tc_diagnostic_init(&diag);
    if (tc_symbol_name_index_build(&program->symbols, &ctx.sym_index, &diag) != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        return -1;
    }

    fprintf(out, "/* Auto-generated by tc-aot. Do not edit. */\n");
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
    fprintf(out, "    if (tc_diagnostic_set_source(&diag, ");
    tc_aot_emit_c_string(out, source_name);
    fprintf(out, ", NULL) != 0) {\n");
    fprintf(out, "        tc_aot_abort(&diag, 0);\n");
    fprintf(out, "    }\n");
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
