/*
 * tc_aot_codegen.c — TC → C99 转译
 *
 * 消费 Analyzer 产出的 TcTypedProgram，逐语句生成等价的 C99 代码。
 * 算术、cast、I/O 操作通过 tc_aot_rt.h 中的运行时辅助函数委托 tc_semantics.c，
 * 保证与 TC-VM 行为完全一致。
 */
#include "tc_aot_codegen.h"

#include "tc_symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  辅助函数                                                           */
/* ------------------------------------------------------------------ */

static const TcSymbol *tc_aot_find_symbol(const TcSymbolTable *symbols, const char *name) {
    return tc_symbol_table_find(symbols, name);
}

/** 将 TcIntType 枚举映射为 C 源码中的枚举名 */
static const char *tc_aot_type_enum(TcIntType type) {
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
    }
    return "TC_INT32";
}

/* ------------------------------------------------------------------ */
/*  表达式发射                                                          */
/* ------------------------------------------------------------------ */

/** 发射字面量构造表达式 */
static void tc_aot_emit_literal_expr(FILE *out, TcIntType type, const TcLiteral *lit) {
    if (lit->is_bool) {
        fprintf(out, "tc_aot_lit(%s, %lluULL, 0, 0)", tc_aot_type_enum(TC_BOOL),
                lit->magnitude ? 1ULL : 0ULL);
        return;
    }
    fprintf(out, "tc_aot_lit(%s, %" PRIu64 "ULL, %d, %d)", tc_aot_type_enum(type), lit->magnitude,
            lit->negative, lit->unsigned_suffix);
}

/** 发射操作数表达式：字面量或 slots[slot] */
static void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcIntType type,
                                     const TcSymbolTable *symbols) {
    if (operand->kind == TC_OPERAND_LIT) {
        tc_aot_emit_literal_expr(out, type, &operand->u.lit);
    } else {
        const TcSymbol *symbol = tc_aot_find_symbol(symbols, operand->u.name);
        fprintf(out, "slots[%d]", symbol->slot);
    }
}

/* ------------------------------------------------------------------ */
/*  RHS 发射                                                            */
/* ------------------------------------------------------------------ */

/** 发射 RHS 求值代码（字面量赋值 / 算术/cast/单目调用） */
static int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcIntType expected_type, int dst_slot,
                           const TcSymbolTable *symbols, int line) {
    if (rhs->kind == TC_RHS_LIT) {
        fprintf(out, "    slots[%d] = ", dst_slot);
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

        fprintf(out, "    if (tc_aot_arith(%s, %s, %s, &slots[%d], ", op_name,
                tc_aot_type_enum(rhs->u.arith.type), mode, dst_slot);
        tc_aot_emit_operand_expr(out, &rhs->u.arith.lhs, rhs->u.arith.type, symbols);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.arith.rhs, rhs->u.arith.type, symbols);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        const char *mode =
            rhs->u.unary.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = rhs->u.unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "    if (tc_aot_unary(%s, %s, %s, &slots[%d], ", op_name,
                tc_aot_type_enum(rhs->u.unary.type), mode, dst_slot);
        tc_aot_emit_operand_expr(out, &rhs->u.unary.operand, rhs->u.unary.type, symbols);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
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
        fprintf(out, "    if (tc_aot_compare(%s, %s, &slots[%d], ", op_name,
                tc_aot_type_enum(rhs->u.compare.type), dst_slot);
        tc_aot_emit_operand_expr(out, &rhs->u.compare.lhs, rhs->u.compare.type, symbols);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.compare.rhs, rhs->u.compare.type, symbols);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        fprintf(out, "    if (tc_aot_logic_unary(TC_LOGIC_NOT, &slots[%d], ", dst_slot);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_un.operand, TC_BOOL, symbols);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        const char *op_name =
            rhs->u.logic_bin.op == TC_LOGIC_AND ? "TC_LOGIC_AND" : "TC_LOGIC_OR";
        fprintf(out, "    if (tc_aot_logic(%s, &slots[%d], ", op_name, dst_slot);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.lhs, TC_BOOL, symbols);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.rhs, TC_BOOL, symbols);
        fprintf(out, ", &diag, %d) != 0)\n", line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF || rhs->kind == TC_RHS_CONST_CAST) {
        return -1;
    }

    {
        const TcSymbol *source = tc_aot_find_symbol(symbols, rhs->u.cast.source);
        const char *mode =
            rhs->u.cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        fprintf(out, "    if (tc_aot_cast(%s, %s, slots[%d], %s, &slots[%d], &diag, %d) != 0)\n",
                tc_aot_type_enum(rhs->u.cast.target), mode, source->slot,
                tc_aot_type_enum(source->type), dst_slot, line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", line);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  语句发射                                                            */
/* ------------------------------------------------------------------ */

static int tc_aot_emit_statement(FILE *out, const TcStatement *stmt, const TcSymbolTable *symbols) {
    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        const TcSymbol *symbol = tc_aot_find_symbol(symbols, var_def->name);

        if (!var_def->has_rhs) {
            return 0;  /* 无初始化表达式的 var，槽位保持未初始化哨兵 */
        }
        return tc_aot_emit_rhs(out, &var_def->rhs, var_def->type, symbol->slot, symbols,
                               var_def->line);
    }

    if (stmt->kind == TC_STMT_CONST_DEF) {
        const TcConstDef *const_def = &stmt->u.const_def;
        const TcSymbol *symbol = tc_aot_find_symbol(symbols, const_def->name);

        if (symbol->has_const_value) {
            /* let 常量使用编译期求值的位模式直接初始化 */
            fprintf(out, "    slots[%d] = 0x%016" PRIx64 "ULL;\n", symbol->slot,
                    symbol->const_value.bits);
        } else {
            return tc_aot_emit_rhs(out, &const_def->rhs, const_def->type, symbol->slot, symbols,
                                   const_def->line);
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_ASSIGN) {
        const TcAssign *assign = &stmt->u.assign;
        const TcSymbol *symbol = tc_aot_find_symbol(symbols, assign->name);
        return tc_aot_emit_rhs(out, &assign->rhs, symbol->type, symbol->slot, symbols,
                               assign->line);
    }

    if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        const TcIoWrite *io = &stmt->u.io_write;
        int newline = stmt->kind == TC_STMT_WRITELN ? 1 : 0;

        fprintf(out, "    tc_aot_write(%s, %s, ", tc_aot_type_enum(io->type),
                io->fmt == TC_FMT_NONE ? "TC_FMT_NONE"
                : io->fmt == TC_FMT_D        ? "TC_FMT_D"
                : io->fmt == TC_FMT_I        ? "TC_FMT_I"
                : io->fmt == TC_FMT_U        ? "TC_FMT_U"
                : io->fmt == TC_FMT_X        ? "TC_FMT_X"
                : io->fmt == TC_FMT_XU       ? "TC_FMT_XU"
                : io->fmt == TC_FMT_O        ? "TC_FMT_O"
                : io->fmt == TC_FMT_B        ? "TC_FMT_B"
                                               : "TC_FMT_T");
        tc_aot_emit_operand_expr(out, &io->operand, io->type, symbols);
        fprintf(out, ", %d);\n", newline);
        return 0;
    }

    if (stmt->kind == TC_STMT_READ) {
        const TcRead *io_read = &stmt->u.io_read;
        const TcSymbol *symbol = tc_aot_find_symbol(symbols, io_read->name);

        fprintf(out, "    if (tc_aot_read(%s, &slots[%d], &diag, %d) != 0)\n",
                tc_aot_type_enum(io_read->type), symbol->slot, io_read->line);
        fprintf(out, "        tc_aot_abort(&diag, %d);\n", io_read->line);
        return 0;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  C 文件生成入口                                                       */
/* ------------------------------------------------------------------ */

int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name) {
    size_t i = 0;
    size_t slot_count = program->symbols.count;

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
        if (tc_aot_emit_statement(out, &program->program.items[i], &program->symbols) != 0) {
            return -1;
        }
    }

    fprintf(out, "\n    return 0;\n");
    fprintf(out, "}\n");
    return 0;
}
