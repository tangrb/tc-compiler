/*
 * tc_aot_codegen.c — TC → C99 转译（AOT Codegen）
 *
 * 消费 Analyzer 产出的 TcTypedProgram，逐语句生成等价的 C99 代码。
 * 生成的代码由统一全局 slots[]、tc_func_<id> 函数、tc_init_static_vars 与 main() 组成。
 *
 * 设计原则：
 *   - 算术、cast、比较、逻辑、位运算、I/O、ptr/memblock/struct
 *     均通过 tc_aot_rt.h shim 委托共享语义核，与 TC-VM 一致。
 *   - let 常量编译器已求值，Codegen 直接将 const_value.bits 写为字面量。
 *   - CONST_REF / CONST_CAST 在 Analyzer 阶段应已被折叠，Codegen 发现则报错。
 *   - if → 原生 C if-else；label/goto → tc_label_<stmt_index>（无 shim）。
 *
 * 子模块：tc_aot_emit_rhs.c（RHS 发射）、tc_aot_emit_stmt.c（语句发射）、
 * tc_aot_emit_func.c（函数 / static var / 函数表）。共享声明见
 * tc_aot_codegen_internal.h。
 */
#include "tc_aot_codegen.h"
#include "tc_aot_codegen_internal.h"

#include "tc_stmt_index.h"
#include "tc_struct_check.h"
#include "tc_symbol.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int tc_aot_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth) {
    int i = 0;

    for (i = 0; i < depth; i++) {
        if (a[i].owner_stmt_index != b[i].owner_stmt_index || a[i].kind != b[i].kind) {
            return 0;
        }
    }
    return 1;
}

int tc_aot_block_path_push(TcAotBlockPath *bp, TcBlockId block_id) {
    if (bp->depth >= TC_AOT_BLOCK_DEPTH_MAX) {
        return -1;
    }
    bp->path[bp->depth++] = block_id;
    return 0;
}

void tc_aot_block_path_pop(TcAotBlockPath *bp) {
    if (bp->depth > 0) {
        bp->depth--;
    }
}

int tc_aot_loop_stack_push(TcAotLoopStack *loops, int loop_id) {
    if (loop_id < 0 || loops->depth >= TC_AOT_LOOP_DEPTH_MAX) {
        return -1;
    }
    loops->loop_ids[loops->depth++] = loop_id;
    return 0;
}

void tc_aot_loop_stack_pop(TcAotLoopStack *loops) {
    if (loops->depth > 0) {
        loops->depth--;
    }
}

/*
 * §7.3：每个函数独立标签表。仅解析当前函数（func_id）内的标签；
 * 跨函数同名标签互不相关（与 Analyzer pass2 tc_resolve_goto_label 对称）。
 */
const TcLabelEntry *tc_aot_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                              int func_id, const TcAotBlockPath *goto_path) {
    const TcLabelEntry *best_same = NULL;
    const TcLabelEntry *best_ancestor = NULL;
    const TcLabelEntry *any = NULL;
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0 || entry->func_id != func_id) {
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

const TcSymbol *tc_aot_find_def_symbol(const TcSymbolTable *symbols, const char *name,
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

const TcFuncDef *tc_aot_find_func_def(const TcTypedProgram *prog, int func_id,
                                      const TcProgram **out_module) {
    size_t i = 0;
    size_t j = 0;

    if (!prog || func_id < 0) {
        return NULL;
    }
    for (i = 0; i < prog->program.count; i++) {
        if (prog->program.items[i].kind == TC_STMT_FUNC_DEF &&
            prog->program.items[i].u.func_def.func_id == func_id) {
            if (out_module) {
                *out_module = &prog->program;
            }
            return &prog->program.items[i].u.func_def;
        }
    }
    for (i = 0; i < prog->dep_count; i++) {
        for (j = 0; j < prog->deps[i].count; j++) {
            if (prog->deps[i].items[j].kind == TC_STMT_FUNC_DEF &&
                prog->deps[i].items[j].u.func_def.func_id == func_id) {
                if (out_module) {
                    *out_module = &prog->deps[i];
                }
                return &prog->deps[i].items[j].u.func_def;
            }
        }
    }
    return NULL;
}

int tc_aot_func_body_index_range(const TcProgram *module, int func_id, int *out_body_start,
                                 int *out_body_end) {
    size_t i = 0;
    int cursor = 0;

    if (!module || !out_body_start || !out_body_end) {
        return -1;
    }
    for (i = 0; i < module->count; i++) {
        const TcStatement *stmt = &module->items[i];
        int span = tc_stmt_subtree_index_count(stmt);

        if (stmt->kind == TC_STMT_FUNC_DEF && stmt->u.func_def.func_id == func_id) {
            *out_body_start = cursor + 1;
            *out_body_end = cursor + span;
            return 0;
        }
        cursor += span;
    }
    return -1;
}

int tc_aot_param_slot(const TcSymbolTable *symbols, const TcFuncDef *func,
                      const char *param_name, int *out_slot) {
    size_t i = 0;
    size_t pi = 0;

    if (!symbols || !func || !param_name || !out_slot) {
        return -1;
    }
    for (pi = 0; pi < func->param_count; pi++) {
        if (func->params[pi].name && strcmp(func->params[pi].name, param_name) == 0) {
            size_t seen = 0;
            for (i = 0; i < symbols->count; i++) {
                const TcSymbol *sym = &symbols->symbols[i];

                if (sym->slot_domain != TC_SLOT_PARAM || sym->def_line != func->line) {
                    continue;
                }
                if (seen == pi) {
                    *out_slot = sym->slot;
                    return 0;
                }
                seen++;
            }
            break;
        }
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->slot_domain == TC_SLOT_PARAM && sym->def_line == func->line && sym->name &&
            strcmp(sym->name, param_name) == 0) {
            *out_slot = sym->slot;
            return 0;
        }
    }
    return -1;
}

const TcRhs *tc_aot_find_named_arg_rhs(const char *param_name, const TcNamedArg *args,
                                       size_t arg_count) {
    size_t i = 0;

    for (i = 0; i < arg_count; i++) {
        if (args[i].param_name && param_name && strcmp(args[i].param_name, param_name) == 0) {
            return &args[i].value;
        }
    }
    return NULL;
}

const TcRhs *tc_aot_find_expr_arg_rhs(const char *param_name,
                                      const TcAotFuncallExprArg *args, size_t arg_count) {
    size_t i = 0;

    for (i = 0; i < arg_count; i++) {
        if (args[i].param_name && param_name && strcmp(args[i].param_name, param_name) == 0) {
            return args[i].value;
        }
    }
    return NULL;
}

const TcSymbol *tc_aot_find_symbol_by_name(const TcSymbolTable *symbols,
                                           const char *name) {
    size_t i = 0;
    const TcSymbol *best = NULL;

    if (!symbols || !name) {
        return NULL;
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (!sym->name || strcmp(sym->name, name) != 0) {
            continue;
        }
        if (!best || sym->def_stmt_index > best->def_stmt_index) {
            best = sym;
        }
    }
    return best;
}

int tc_aot_resolve_var_slot(const TcSymbolTable *symbols, const TcSymbolNameIndex *sym_index,
                            const char *name, int stmt_index, int *out_slot) {
    const TcSymbol *sym = tc_symbol_table_find_visible(symbols, name, stmt_index, sym_index);

    /* 禁止回退到全局按名扫表：共享符号表跨函数/模块存在同名符号时，
     * 按 def_stmt_index 取最大会绑错槽（memblock store/copy 已改由
     * Pass2 持久化的 binding 取槽，此函数仅作防御路径）。 */
    if (!sym || sym->slot < 0) {
        return -1;
    }
    *out_slot = sym->slot;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  辅助函数                                                           */
/* ------------------------------------------------------------------ */

const char *tc_aot_type_enum(TcTypeTag type) {
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
    case TC_ISIZE:
        return "TC_ISIZE";
    case TC_USIZE:
        return "TC_USIZE";
    case TC_PTR:
        return "TC_PTR";
    case TC_MEMBLOCK:
        return "TC_MEMBLOCK";
    case TC_VOID:
        return "TC_VOID";
    case TC_STRUCT:
        return "TC_STRUCT";
    }
    return "TC_INT32";
}

const char *tc_aot_format_enum(TcFormatSpec fmt) {
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

void tc_aot_sub_indent(char *out, size_t out_size, const char *base, int levels) {
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

void tc_aot_emit_c_string(FILE *out, const char *value) {
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

const char *tc_aot_ptr_compare_op(TcCompareOp op) {
    switch (op) {
    case TC_CMP_EQ:
        return "TC_CMP_EQ";
    case TC_CMP_NE:
        return "TC_CMP_NE";
    case TC_CMP_LT:
        return "TC_CMP_LT";
    case TC_CMP_LE:
        return "TC_CMP_LE";
    case TC_CMP_GT:
        return "TC_CMP_GT";
    case TC_CMP_GE:
        return "TC_CMP_GE";
    }
    return "TC_CMP_EQ";
}

/* ------------------------------------------------------------------ */
/*  表达式发射                                                          */
/* ------------------------------------------------------------------ */

void tc_aot_emit_literal_expr(FILE *out, TcTypeTag type, const TcLiteral *lit) {
    if (lit->is_nullptr) {
        fprintf(out, "0ULL");
        return;
    }
    if (lit->is_bool) {
        fprintf(out, "tc_aot_lit(%s, %lluULL, 0, 0)", tc_aot_type_enum(TC_BOOL),
                lit->magnitude ? 1ULL : 0ULL);
        return;
    }
    if (lit->is_float) {
        uint64_t bits = 0;

        if (lit->is_float_special) {
            if (lit->float_special == 0) {
                bits = (type == TC_FLOAT32) ? TC_FLOAT32_CANONICAL_NAN_BITS
                                            : TC_FLOAT64_CANONICAL_NAN_BITS;
            } else if (type == TC_FLOAT32) {
                bits = (lit->float_special < 0) ? TC_FLOAT32_NEG_INF_BITS
                                                : TC_FLOAT32_POS_INF_BITS;
            } else {
                bits = (lit->float_special < 0) ? TC_FLOAT64_NEG_INF_BITS
                                                : TC_FLOAT64_POS_INF_BITS;
            }
            fprintf(out, "tc_aot_lit(%s, 0x%" PRIx64 "ULL, 0, 0)", tc_aot_type_enum(type), bits);
            return;
        }
        if (type == TC_FLOAT32) {
            float f = (float)lit->float_value;
            uint32_t b32 = 0;
            memcpy(&b32, &f, sizeof(b32));
            bits = (uint64_t)b32;
        } else {
            double d = lit->float_value;
            memcpy(&bits, &d, sizeof(d));
        }
        fprintf(out, "tc_aot_lit(%s, 0x%" PRIx64 "ULL, 0, 0)", tc_aot_type_enum(type), bits);
        return;
    }
    fprintf(out, "tc_aot_lit(%s, %" PRIu64 "ULL, %d, %d)", tc_aot_type_enum(type), lit->magnitude,
            lit->negative, lit->unsigned_suffix);
}

void tc_aot_emit_const_memblock_expr(FILE *out, uint64_t host_bits, size_t nbytes, int line) {
    const uint8_t *data = (const uint8_t *)(uintptr_t)host_bits;

    if (!data || nbytes < sizeof(uint64_t)) {
        fprintf(out, "0");
        return;
    }
    fprintf(out, "tc_aot_memblock_from_bytes(");
    tc_aot_emit_byte_array_expr(out, data, nbytes);
    fprintf(out, ", %zu, tc_aot_cur_diag, %d)", nbytes, line);
}

/**
 * 发射 C99 复合字面量字节数组（如 (const uint8_t[]){1, 2, 3}）。
 * 供 const 基址的 struct/memblock 字段折叠使用：生成代码不得嵌入
 * 分析期堆指针（const_bits），而是把字节内联进生成二进制再深拷贝。
 */
void tc_aot_emit_byte_array_expr(FILE *out, const uint8_t *data, size_t nbytes) {
    size_t i = 0;

    fprintf(out, "(const uint8_t[]){");
    for (i = 0; i < nbytes; i++) {
        if (i > 0) {
            fprintf(out, ", ");
        }
        fprintf(out, "%u", (unsigned)data[i]);
    }
    fprintf(out, "}");
}

void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcTypeTag type,
                              const TcAotEmitCtx *ctx, int stmt_index) {
    const TcSymbolTable *symbols = &ctx->program->symbols;

    if (operand->kind == TC_OPERAND_LIT) {
        tc_aot_emit_literal_expr(out, type, &operand->u.lit);
        return;
    }
    if (operand->kind == TC_OPERAND_FIELD_READ &&
        operand->u.field_read.resolved.resolved) {
        const TcResolvedFieldAccess *access = &operand->u.field_read.resolved;
        const TcStructTable *table = ctx->program->struct_table;

        if (access->is_memblock_count) {
            fprintf(out, "%" PRIu64 "ULL", (uint64_t)access->const_bits);
            return;
        }
        if (access->field_count > 0 && access->offsets && access->field_type) {
            const TcType *field_type = access->field_type;
            size_t offset = access->offsets[access->field_count - 1];
            size_t nbytes = 0;

            /* let/static let 基址：codegen 期从 const_bits 折叠，禁止嵌入分析期堆指针 */
            if (access->base_slot < 0 && field_type->tag != TC_STRUCT &&
                field_type->tag != TC_MEMBLOCK) {
                /* 标量 / 指针字段：把位模式折叠成整型字面量 */
                uint64_t bits = 0;
                const uint8_t *data = (const uint8_t *)(uintptr_t)access->const_bits;

                nbytes = (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits, table) + 7U) /
                         8U;
                if (data && nbytes > 0) {
                    memcpy(&bits, data + offset,
                           nbytes <= sizeof(bits) ? nbytes : sizeof(bits));
                }
                if (field_type->tag == TC_BOOL) {
                    bits = bits ? 1ULL : 0ULL;
                }
                fprintf(out, "0x%016" PRIx64 "ULL", bits);
                return;
            }

            if (field_type->tag == TC_STRUCT || field_type->tag == TC_MEMBLOCK) {
                if (field_type->tag == TC_STRUCT) {
                    const TcStructEntry *nested =
                        tc_struct_table_get(table, field_type->params.struct_type.struct_id);
                    nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
                } else {
                    nbytes =
                        (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits, table) + 7U) /
                        8U;
                }
                if (access->base_slot >= 0) {
                    fprintf(out, "tc_aot_struct_extract(slots[%d], %zu, %zu, tc_aot_cur_diag, 0)",
                            access->base_slot, offset, nbytes);
                } else {
                    /* const 基址：把字段字节内联为复合字面量，运行期再深拷贝 */
                    const uint8_t *data = (const uint8_t *)(uintptr_t)access->const_bits;
                    const uint8_t zero = 0;

                    fprintf(out, "tc_aot_struct_extract((uint64_t)(uintptr_t)&");
                    tc_aot_emit_byte_array_expr(out, (data && nbytes > 0) ? data + offset : &zero,
                                                nbytes > 0 ? nbytes : 1);
                    fprintf(out, ", 0, %zu, tc_aot_cur_diag, 0)", nbytes);
                }
            } else {
                /* 标量 / 指针字段（槽位基址）：运行期读位 */
                nbytes =
                    (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits, table) + 7U) / 8U;
                fprintf(out, "tc_aot_struct_load_bits_value(slots[%d], %zu, %zu)",
                        access->base_slot, offset, nbytes);
            }
            return;
        }
    }
    if (operand->binding.resolved) {
        if (operand->binding.is_const) {
            if (type == TC_MEMBLOCK && operand->binding.type &&
                operand->binding.type->tag == TC_MEMBLOCK &&
                operand->binding.type->params.memblock_type.element) {
                size_t elem_bits = tc_sizeof_bits_ex(
                    operand->binding.type->params.memblock_type.element,
                    tc_struct_table_width_bits, ctx->program->struct_table);
                size_t elem_bytes = (elem_bits + 7U) / 8U;
                uint64_t count = tc_type_memblock_count(operand->binding.type);
                size_t nbytes = sizeof(uint64_t) + (size_t)count * elem_bytes;

                tc_aot_emit_const_memblock_expr(out, operand->binding.const_bits, nbytes, 0);
            } else {
                fprintf(out, "0x%016" PRIx64 "ULL", operand->binding.const_bits);
            }
        } else if (operand->binding.slot >= 0) {
            fprintf(out, "slots[%d]", operand->binding.slot);
        }
        return;
    }
    {
        const TcSymbol *symbol =
            tc_symbol_table_find_visible(symbols, operand->u.name, stmt_index, &ctx->sym_index);

        if (!symbol) {
            symbol = tc_aot_find_symbol_by_name(symbols, operand->u.name);
        }
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

int tc_aot_emit_operand_assign(FILE *out, const TcOperand *operand, TcTypeTag type,
                               const char *dst_expr, const char *indent,
                               const TcAotEmitCtx *ctx, int stmt_index) {
    /* 值语义：memblock/struct 操作数赋值（含 return、结构体构造器复合字段）
     * 必须深拷贝，不能复制堆指针（§3.8.4 / §3.9.4）。 */
    if ((type == TC_MEMBLOCK || type == TC_STRUCT) && operand->kind != TC_OPERAND_LIT &&
        operand->binding.resolved && !operand->binding.is_const &&
        operand->binding.slot >= 0) {
        const TcType *btype = operand->binding.type;

        if (btype && btype->tag == type) {
            if (type == TC_MEMBLOCK && btype->params.memblock_type.element) {
                size_t elem_bits = tc_sizeof_bits_ex(
                    btype->params.memblock_type.element, tc_struct_table_width_bits,
                    ctx->program->struct_table);
                size_t elem_bytes = (elem_bits + 7U) / 8U;
                uint64_t count = tc_type_memblock_count(btype);
                char abort_indent[64];

                tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
                fprintf(out, "%s%s = tc_aot_memblock_clone(slots[%d], %zu, %" PRIu64
                             "ULL, tc_aot_cur_diag, %d);\n",
                        indent, dst_expr, operand->binding.slot, elem_bytes, count, stmt_index);
                fprintf(out,
                        "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                        "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                        abort_indent, stmt_index);
                return 0;
            }
            if (type == TC_STRUCT && tc_type_struct_id(btype) >= 0 &&
                ctx->program->struct_table) {
                const TcStructEntry *e =
                    tc_struct_table_get(ctx->program->struct_table, tc_type_struct_id(btype));
                size_t bytes = 0;
                char abort_indent[64];

                if (e) {
                    bytes = (e->width_bits + 7U) / 8U;
                }
                if (bytes == 0) {
                    return -1;
                }
                tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
                fprintf(out, "%s%s = tc_aot_struct_clone(slots[%d], %zu, tc_aot_cur_diag, %d);\n",
                        indent, dst_expr, operand->binding.slot, bytes, stmt_index);
                fprintf(out,
                        "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                        "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                        abort_indent, stmt_index);
                return 0;
            }
        }
    }
    fprintf(out, "%s%s = ", indent, dst_expr);
    tc_aot_emit_operand_expr(out, operand, type, ctx, stmt_index);
    fprintf(out, ";\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  C 文件生成入口                                                       */
/* ------------------------------------------------------------------ */

/* ── 嵌入模式：生成头文件 ── */
int tc_aot_emit_embed_header(FILE *out, const TcTypedProgram *program,
                              const char *source_name) {
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);

    (void)source_name;
    fprintf(out, "/* Auto-generated header by tc-aot --embed. Do not edit. */\n");
    fprintf(out, "#ifndef TC_AOT_EMBED_H\n");
    fprintf(out, "#define TC_AOT_EMBED_H\n\n");
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include \"tc_diagnostic.h\"\n");
    fprintf(out, "#include \"tc_aot_embed_rt.h\"\n\n");
    if (slot_count > 0) {
        fprintf(out, "#define TC_AOT_SLOT_COUNT %zu\n\n", slot_count);
        fprintf(out, "extern uint64_t slots[%zu];\n\n", slot_count);
    } else {
        fprintf(out, "#define TC_AOT_SLOT_COUNT 1\n\n");
        fprintf(out, "extern uint64_t slots[1];\n\n");
    }
    fprintf(out, "int tc_aot_init(TcDiagnostic *diag);\n");
    fprintf(out, "void tc_aot_cleanup(void);\n\n");

    /* 函数表类型 */
    fprintf(out, "typedef int (*tc_aot_func_entry_t)(TcDiagnostic *diag);\n\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int func_id;\n");
    fprintf(out, "    tc_aot_func_entry_t entry;\n");
    fprintf(out, "    uint64_t *ret_ptr;\n");
    fprintf(out, "} tc_aot_func_entry;\n\n");
    fprintf(out, "extern const tc_aot_func_entry tc_aot_func_table[];\n\n");

    fprintf(out, "#endif /* TC_AOT_EMBED_H */\n");
    return 0;
}

int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name,
                  int embed_mode) {
    size_t i = 0;
    size_t di = 0;
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);
    TcAotEmitCtx ctx;
    TcDiagnostic diag;
    int rc = 0;

    tc_stmt_index_reset(&ctx.index);
    tc_symbol_name_index_init(&ctx.sym_index);
    ctx.block_path.depth = 0;
    ctx.loops.depth = 0;
    ctx.program = program;
    ctx.current_func_id = -1;
    ctx.current_return_type = TC_VOID;
    ctx.tmp_seq = 0;
    ctx.embed_mode = embed_mode;
    tc_diagnostic_init(&diag);
    if (tc_symbol_name_index_build(&program->symbols, &ctx.sym_index, &diag) != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        return -1;
    }

    fprintf(out, "/* Auto-generated by tc-aot. Do not edit. */\n");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include \"tc_aot_rt.h\"\n");
    if (embed_mode) {
        fprintf(out, "#include \"tc_aot_embed_rt.h\"\n");
        /* 宏替换：非致命 abort 后带值返回。embed 生成的函数均为 int
         * （tc_aot_init / tc_aot_func_*），裸 return; 违反 C99 约束。 */
        fprintf(out, "#define tc_aot_abort(diag, line) do { \\\n");
        fprintf(out, "    tc_aot_embed_abort(diag, line); \\\n");
        fprintf(out, "    return 1; \\\n");
        fprintf(out, "} while (0)\n");
    }
    fputc('\n', out);

    if (slot_count > 0 || embed_mode) {
        size_t count = slot_count > 0 ? slot_count : 1;
        const char *qual = embed_mode ? "" : "static ";
        fprintf(out, "%suint64_t slots[%zu];\n\n", qual, count);
    }

    if (embed_mode) {
        fprintf(out, "TcDiagnostic *tc_aot_cur_diag;\n");
        fprintf(out, "int tc_aot_embed_error_flag;\n\n");
    } else {
        fprintf(out, "static TcDiagnostic *tc_aot_cur_diag;\n\n");
    }

    tc_aot_emit_func_decls(out, program, &ctx);

    /* ── 静态初始化 ── */
    if (embed_mode) {
        fprintf(out, "int tc_aot_init(TcDiagnostic *diag) {\n");
    } else {
        fprintf(out, "static void tc_init_static_vars(TcDiagnostic *diag) {\n");
    }
    fprintf(out, "    tc_aot_cur_diag = diag;\n");
    for (di = 0; di < program->dep_count; di++) {
        if (tc_aot_emit_static_vars_program(out, &program->deps[di], &ctx) != 0) {
            rc = -1;
            break;
        }
    }
    if (rc == 0) {
        if (tc_aot_emit_static_vars_program(out, &program->program, &ctx) != 0) {
            rc = -1;
        }
    }
    if (embed_mode) {
        fprintf(out, "    return 0;\n");
    }
    fprintf(out, "}\n\n");

    /* ── 函数定义 ── */
    if (rc == 0) {
        for (di = 0; di < program->dep_count; di++) {
            if (tc_aot_emit_functions_program(out, &program->deps[di], &ctx) != 0) {
                rc = -1;
                break;
            }
        }
    }
    if (rc == 0) {
        if (tc_aot_emit_functions_program(out, &program->program, &ctx) != 0) {
            rc = -1;
        }
    }

    if (rc != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        tc_diagnostic_clear(&diag);
        return rc;
    }

    /* ── 嵌入模式：函数表 + 清理函数 ── */
    if (embed_mode) {
        if (tc_aot_emit_func_table(out, program) != 0) {
            tc_symbol_name_index_free(&ctx.sym_index);
            tc_diagnostic_clear(&diag);
            return -1;
        }
        fprintf(out, "void tc_aot_cleanup(void) {\n");
        fprintf(out, "    tc_aot_memblock_heap_free_all();\n");
        fprintf(out, "    tc_aot_struct_heap_free_all();\n");
        fprintf(out, "}\n");
        tc_symbol_name_index_free(&ctx.sym_index);
        tc_diagnostic_clear(&diag);
        return 0;
    }

    /* ── 独立程序模式：生成 main() ── */
    fprintf(out, "int main(void) {\n");
    fprintf(out, "    TcDiagnostic diag;\n");
    fprintf(out, "    tc_aot_diag_init(&diag);\n");
    fprintf(out, "    tc_aot_cur_diag = &diag;\n");
    fprintf(out, "    if (tc_diagnostic_set_source(&diag, ");
    tc_aot_emit_c_string(out, source_name);
    fprintf(out, ", NULL) != 0) {\n");
    fprintf(out, "        tc_aot_abort(&diag, 0);\n");
    fprintf(out, "    }\n");
    if (slot_count > 0) {
        fprintf(out, "    tc_aot_init_slots(slots, %zu);\n", slot_count);
    }
    fprintf(out, "    tc_init_static_vars(&diag);\n");
    fprintf(out, "    if (diag.domain != TC_DIAG_NONE) tc_aot_abort(&diag, 0);\n");
    fprintf(out, "\n");

    ctx.current_func_id = -1;
    ctx.block_path.depth = 0;
    ctx.loops.depth = 0;
    tc_stmt_index_reset(&ctx.index);

    for (i = 0; i < program->program.count; i++) {
        if (tc_aot_emit_statement_impl(out, &program->program.items[i], &ctx, "    ") != 0) {
            rc = -1;
            break;
        }
    }

    fprintf(out, "\n    tc_aot_memblock_heap_free_all();\n");
    fprintf(out, "    tc_aot_struct_heap_free_all();\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    tc_symbol_name_index_free(&ctx.sym_index);
    tc_diagnostic_clear(&diag);
    return rc;
}
