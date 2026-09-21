/*
 * tc_const_eval.c — let 常量编译期求值实现
 *
 * 源序求值 let；映射运行时错误为编译期常量错误；
 * 与 Executor 共用 tc_sem_*。复合构造见 tc_const_aggregate.c。
 */
#include "tc_const_eval.h"
#include "tc_const_aggregate.h"

#include "tc_analyzer_internal.h"
#include "tc_diagnostic.h"
#include "tc_semantics.h"
#include "tc_struct_check.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


static uint64_t tc_ce_load_bits(const uint8_t *src, size_t nbytes) {
    uint64_t bits = 0;
    size_t i = 0;

    for (i = 0; i < nbytes && i < sizeof(bits); i++) {
        bits |= ((uint64_t)src[i]) << (8U * i);
    }
    return bits;
}
#include <stdlib.h>
#include <string.h>


/* ------------------------------------------------------------------ */
/*  常量求值辅助                                                         */
/* ------------------------------------------------------------------ */

/*
 * 将运行时语义错误（tc_exec_* 产生的 TC_RE_*）映射为对应的
 * 编译期常量错误（TC_CE_CONSTANT_*）。
 *
 * 运行时错误类型与常量错误类型一一对应，这种区分使 TC 语言的
 * 错误报告能精确区分"运行时溢出"和"编译期常量溢出"，
 * 便于测试断言和用户定位。
 *
 * 阶段归属：本函数产出的全部码均属语言标准 §11 的 **CT 类**阶段
 * （编译器标准 §1.3「诊断类阶段与处理阶段的对应」），因此一律
 * **挂起**而非立即写入 diag——§11「阶段优先」要求 CT 类诊断晚于全部
 * SEM 类诊断（可达性、确定初始化、调用图）报告。
 * 挂起由 tc_analyzer 在各 SEM 检查无触发后统一发布。
 * 形态与来源类子条件（同码位但不经本函数）仍属 SEM，照常立即报告。
 */
static int tc_const_map_runtime_error(TcErrorKind kind, TcDiagnostic *diag, int line) {
    switch (kind) {
    case TC_RE_INTEGER_OVERFLOW:
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                   "constant overflow");
    case TC_RE_DIVISION_BY_ZERO:
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_DIV_ZERO, line, TC_COLUMN_UNKNOWN,
                                   "constant division by zero");
    case TC_RE_NEGATIVE_SHIFT_COUNT:
        /* 常量移位负计数报常量表达式错误，不映射为常量溢出（规范 §6.4.3） */
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                   "negative shift count");
    case TC_RE_CAST_OVERFLOW:
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                   "constant cast overflow");
    case TC_RE_FLOAT_OVERFLOW:
    case TC_RE_FLOAT_UNDERFLOW:
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                   "constant overflow");
    case TC_RE_FLOAT_INVALID:
        return tc_diagnostic_defer(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                   "invalid floating-point constant expression");
    default:
        return -1;
    }
}

/* 字段读取的常量访问辅助（定义在本文件后部，静态布尔判定需前置引用） */
static int tc_const_read_resolved_field(const TcResolvedFieldAccess *access,
                                        const TcSymbol *base_sym, TcTypeTag expected,
                                        TcValue *out, int line, TcDiagnostic *diag);
static const TcSymbol *tc_const_lookup_field_base(const char *base, const TcSymbolTable *visible,
                                                  const TcSymbolTable *global,
                                                  const TcMemberIndex *members);

/*
 * 静态布尔判定的原子操作数（语言标准 §5.2.2）：
 * 字面量；在当前词法作用域可见、定义语句更早且已成功求值的 `let`（含经
 * `Self.` / 导入限定解析到的 `static let`）；只读结构体字段读取；`mb.count`。
 * 含 `var` / 形参等不能静态解析的操作数一律返回 0（判定为 unknown），
 * 不得降级为诊断。
 */
static int tc_try_eval_bound_operand(const TcOperand *operand, TcTypeTag expected,
                                     const TcSymbolTable *visible, const TcSymbolTable *global,
                                     TcValue *out) {
    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            return 0;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 1;
    }
    if (operand->kind == TC_OPERAND_FIELD_READ) {
        const TcResolvedFieldAccess *access = &operand->u.field_read.resolved;
        const TcSymbol *base_sym = NULL;
        TcDiagnostic scratch;

        if (!access->resolved) {
            return 0;
        }
        if (access->is_memblock_count) {
            if (expected != TC_USIZE) {
                return 0;
            }
            base_sym = tc_const_lookup_field_base(operand->u.field_read.base, visible, global, NULL);
            *out = tc_value_make(TC_USIZE, base_sym && base_sym->type
                                               ? tc_type_memblock_count(base_sym->type)
                                               : access->const_bits);
            return 1;
        }
        /*
         * 只读字段读取：基址须为已求值的常量（let / static let）。static let
         * 的提前求值会固化并释放基址与字段名（§4.3 拓扑求值），故此时只能依靠
         * resolved 元数据——resolved.const_bits 已携带基址常量位，
         * base_slot >= 0 表示运行时 var 基址（不可静态求值）。
         */
        if (!access->field_type || tc_type_tag_of(access->field_type) != expected) {
            return 0;
        }
        base_sym = tc_const_lookup_field_base(operand->u.field_read.base, visible, global, NULL);
        tc_diagnostic_init(&scratch);
        if (tc_const_read_resolved_field(access, base_sym, expected, out, 0, &scratch) != 0) {
            tc_diagnostic_clear(&scratch);
            return 0;
        }
        tc_diagnostic_clear(&scratch);
        return 1;
    }
    if (!operand->binding.resolved || !operand->binding.is_const ||
        operand->binding.type->tag != expected) {
        return 0;
    }
    *out = tc_value_make(expected, operand->binding.const_bits);
    return 1;
}

static int tc_try_eval_bound_const_ref(const TcRhs *rhs, TcValue *out) {
    const TcResolvedBinding *binding = &rhs->u.const_ref.binding;

    if (!binding->resolved || !binding->is_const || binding->type->tag != TC_BOOL) {
        return 0;
    }
    *out = tc_value_make(TC_BOOL, binding->const_bits);
    return 1;
}

/*
 * `Self.<名>` / 导入限定名（`<模块名>.<名>`）解析为 bool 型 static let 时取常量值。
 * 非 bool、未成功求值或非 static let 一律返回 0（unknown）。
 */
static int tc_try_eval_self_member_bool(const TcRhs *rhs, const TcSymbolTable *symbols,
                                        TcValue *out) {
    const char *member = rhs->u.self_member.member_name;
    const TcSymbol *sym = NULL;

    if (!member || !symbols) {
        return 0;
    }
    sym = tc_symbol_table_find(symbols, member);
    if (!sym || sym->sym_kind != TC_SYM_CONSTANT || !sym->has_const_value ||
        tc_type_tag_of(sym->type) != TC_BOOL) {
        return 0;
    }
    *out = tc_value_make(TC_BOOL, sym->const_value.bits);
    return 1;
}

void tc_try_eval_static_bool_operand(const TcOperand *operand, TcStaticBoolResult *result) {
    TcValue value = {0};

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (tc_try_eval_bound_operand(operand, TC_BOOL, NULL, NULL, &value)) {
        *result = value.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    }
}

int tc_const_value_withheld(const TcSymbol *sym, const TcDiagnostic *diag) {
    return sym && sym->ct_eval_failed && tc_diagnostic_has_deferred(diag);
}

int tc_try_eval_static_bool(const TcRhs *rhs, const TcSymbolTable *symbols,
                            const struct TcStructTable *struct_table, int line,
                            TcStaticBoolResult *result, TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_value = {0};
    TcValue out = {0};
    int status = 0;

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (rhs->kind == TC_RHS_LIT) {
        if (rhs->u.lit.is_bool) {
            *result = rhs->u.lit.magnitude == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }
    if (rhs->kind == TC_RHS_CONST_REF) {
        if (tc_try_eval_bound_const_ref(rhs, &out)) {
            *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }
    /* `Self.<名>` / 导入限定名解析到的 bool 型 static let（§5.2.2）。 */
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        if (tc_try_eval_self_member_bool(rhs, symbols, &out)) {
            *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }
    /* 只读结构体字段读取 / `mb.count` 作整条条件（§5.2.2 原子表达式）。 */
    if (rhs->kind == TC_RHS_FIELD_READ) {
        const TcResolvedFieldAccess *access = &rhs->u.field_read.resolved;
        TcValue field_value = {0};
        TcDiagnostic scratch;

        /*
         * static let 的提前求值会固化并释放基址与字段名（见
         * tc_struct_finalize_field_access），故只能用 resolved 元数据判定。
         */
        if (access->resolved && access->field_type &&
            tc_type_tag_of(access->field_type) == TC_BOOL) {
            tc_diagnostic_init(&scratch);
            if (tc_const_read_resolved_field(access, NULL, TC_BOOL, &field_value, line,
                                             &scratch) == 0) {
                *result = field_value.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
            }
            tc_diagnostic_clear(&scratch);
        }
        (void)struct_table;
        return 0;
    }

    switch (rhs->kind) {
    case TC_RHS_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, symbols,
                                      symbols, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, symbols,
                                      symbols, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type->tag, &lhs, &rhs_value, &out,
                                 &tmp_diag, line);
        break;
    case TC_RHS_FLOAT_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.float_compare.lhs,
                                       rhs->u.float_compare.type->tag, symbols, symbols, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.float_compare.rhs,
                                       rhs->u.float_compare.type->tag, symbols, symbols,
                                       &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type->tag,
                                    rhs->u.float_compare.mode, &lhs, &rhs_value, &out,
                                    &tmp_diag, line);
        break;
    case TC_RHS_LOGIC_BIN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_bin.lhs, TC_BOOL, symbols, symbols, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.logic_bin.rhs, TC_BOOL, symbols, symbols,
                                       &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_value, &out, &tmp_diag,
                                      line);
        break;
    case TC_RHS_LOGIC_UN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_un.operand, TC_BOOL, symbols, symbols,
                                       &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, &out, &tmp_diag, line);
        break;
    case TC_RHS_CAST:
        if (rhs->u.cast.target.tag != TC_BOOL || rhs->u.cast.mode != TC_TRUNC_STRICT ||
            !rhs->u.cast.source_type_resolved ||
            !tc_try_eval_bound_operand(&rhs->u.cast.source, rhs->u.cast.source_type->tag, symbols,
                                       symbols, &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_cast(TC_BOOL, &lhs, &out, &tmp_diag, line);
        break;
    default:
        return 0;
    }

    if (status != 0) {
        (void)tc_const_map_runtime_error(tmp_diag.kind, diag, line);
        tc_diagnostic_clear(&tmp_diag);
        return -1;
    }
    tc_diagnostic_clear(&tmp_diag);
    *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    return 0;
}

static int tc_const_read_resolved_field(const TcResolvedFieldAccess *access,
                                        const TcSymbol *base_sym, TcTypeTag expected,
                                        TcValue *out, int line, TcDiagnostic *diag) {
    const uint8_t *data = NULL;
    size_t offset = 0;
    size_t nbytes = 0;
    const TcType *field_type = NULL;
    uint64_t bits = 0;

    if (!access || !access->resolved) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    if (access->base_slot >= 0) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant expression cannot reference var variable");
        return -1;
    }
    if (access->is_memblock_count) {
        if (base_sym && base_sym->type) {
            *out = tc_value_make(TC_USIZE, tc_type_memblock_count(base_sym->type));
        } else {
            *out = tc_value_make(TC_USIZE, 0);
        }
        return 0;
    }
    if (!access->field_type) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    field_type = access->field_type;
    if (access->field_count == 0) {
        /*
         * 单点限定名（`<模块>.<成员>`）整体读取——常量值就是该绑定自身，
         * 无字段偏移（`offsets` 为 NULL）。类型不符仍按常量类型不匹配报错。
         */
        if (tc_type_tag_of(field_type) != expected) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant type does not match expected type");
            return -1;
        }
        out->type = field_type;
        out->bits = access->const_bits;
        if (field_type->tag == TC_BOOL) {
            out->bits = out->bits ? 1ULL : 0ULL;
        }
        return 0;
    }
    if (!access->offsets) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    if (tc_type_tag_of(field_type) != expected) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "constant type does not match expected type");
        return -1;
    }
    if (field_type->tag == TC_STRUCT || field_type->tag == TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    /* 优先用基址符号当前 const_value（static let 拓扑求值后才就绪） */
    if (base_sym && base_sym->has_const_value) {
        bits = base_sym->const_value.bits;
    } else {
        bits = access->const_bits;
    }
    data = (const uint8_t *)(uintptr_t)bits;
    if (!data) {
        /* 基址常量因挂起的 CT 类诊断而无值：派生失败，不另报 SEM 诊断。 */
        if (tc_const_value_withheld(base_sym, diag)) {
            return -1;
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant value is not available by source order");
        return -1;
    }
    offset = access->offsets[access->field_count - 1];
    nbytes = (tc_sizeof_bits(field_type) + 7U) / 8U;
    out->type = field_type;
    out->bits = tc_ce_load_bits(data + offset, nbytes);
    if (field_type->tag == TC_BOOL) {
        out->bits = out->bits ? 1ULL : 0ULL;
    }
    return 0;
}

/*
 * 字段读基址：与名称解析同一入口（Self. / 限定名 / 裸名）。
 * `members` 为空时 `Self.` 退回按名查找（静态布尔折叠发生在 Pass2 接受之后）。
 */
static const TcSymbol *tc_const_lookup_field_base(const char *base, const TcSymbolTable *visible,
                                                  const TcSymbolTable *global,
                                                  const TcMemberIndex *members) {
    if (!base) {
        return NULL;
    }
    return tc_find_named_binding(visible, global, base, members);
}

int tc_eval_const_operand(const TcOperand *operand, TcTypeTag expected,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 const TcMemberIndex *members, const char *const_name,
                                 TcValue *out, int line, TcDiagnostic *diag) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 0;
    }

    if (operand->kind == TC_OPERAND_FIELD_READ) {
        const TcSymbol *base_sym = NULL;

        if (!operand->u.field_read.resolved.resolved) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant expression");
            return -1;
        }
        base_sym = tc_const_lookup_field_base(operand->u.field_read.base, visible, global, members);
        return tc_const_read_resolved_field(&operand->u.field_read.resolved, base_sym, expected,
                                            out, line, diag);
    }

    {
        const TcSymbol *symbol = NULL;

        if (const_name && operand->u.name && strcmp(operand->u.name, const_name) == 0) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_find_named_binding(visible, global, operand->u.name, members);
        if (!symbol) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (!symbol->has_const_value) {
            /* 该常量因挂起的 CT 类诊断而无值：派生失败，不另报 SEM 诊断。 */
            if (tc_const_value_withheld(symbol, diag)) {
                return -1;
            }
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                              "constant value is not available by source order");
            return -1;
        }
        if (tc_type_tag_of(symbol->type) != expected) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match operation type");
            return -1;
        }
        *out = symbol->const_value;
        return 0;
    }
}

int tc_eval_const_rhs(const TcRhs *rhs, TcTypeTag expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const TcStructTable *struct_table, const char *const_name,
                             const TcMemberIndex *members, TcValue *out, int line,
                             TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_val = {0};

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_context(&rhs->u.lit, expected_type, NULL)) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&rhs->u.lit, expected_type);
        return 0;
    }

    if (rhs->kind == TC_RHS_FIELD_READ && rhs->u.field_read.resolved.resolved) {
        const TcSymbol *base_sym =
            tc_const_lookup_field_base(rhs->u.field_read.base, visible, global, members);

        return tc_const_read_resolved_field(&rhs->u.field_read.resolved, base_sym, expected_type,
                                            out, line, diag);
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        if (const_name && strcmp(rhs->u.const_ref.name, const_name) == 0) {
            char msg[128];

            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        {
            const TcSymbol *symbol = tc_find_named_binding(visible, global, rhs->u.const_ref.name,
                                                           members);
            char msg[128];
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                /* 派生失败：挂起的 CT 类诊断仍是首个规范诊断。 */
                if (tc_const_value_withheld(symbol, diag)) {
                    return -1;
                }
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
            }
            if (tc_type_tag_of(symbol->type) != expected_type) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "constant type does not match expected type");
                return -1;
            }
            *out = symbol->const_value;
            return 0;
        }
    }

    /*
     * `Self.<名>`（§5.2.1 原子表达式：经 `Self.` 解析到的 `static let`）。
     * 与 `TC_RHS_CONST_REF` 同口径：取模块 static let 的编译期常量值。
     * 函数内 `let x = Self.K` 与 `static let M = Self.K` 同一常量上下文。
     */
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const char *member = rhs->u.self_member.member_name;
        const TcSymbol *symbol = NULL;
        char msg[128];

        if (!member) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                              "missing Self member name");
            return -1;
        }
        /* §4.3、§4.4：`Self.<名>` 只解析**本模块**顶层成员 */
        symbol = tc_resolve_self_member(member, global, members);
        if (!symbol) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", member);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (!symbol->has_const_value) {
            /* 派生失败：挂起的 CT 类诊断仍是首个规范诊断。 */
            if (tc_const_value_withheld(symbol, diag)) {
                return -1;
            }
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                              "constant value is not available by source order");
            return -1;
        }
        if (tc_type_tag_of(symbol->type) != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant type does not match expected type");
            return -1;
        }
        /* struct / memblock 常量：按值共享（调用方 tc_resolve_const_value 依别名判定
         * 是否取得堆块所有权），与 `TC_RHS_CONST_REF` 分支完全同口径。 */
        *out = symbol->const_value;
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        if (tc_validate_arith_mode(rhs->u.arith.op, rhs->u.arith.type->tag,
                                   rhs->u.arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.arith.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.lhs, rhs->u.arith.type->tag, visible, global, members,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.rhs, rhs->u.arith.type->tag, visible, global, members,
                                  const_name, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type->tag, rhs->u.arith.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        if (tc_validate_unary_mode(rhs->u.unary.op, rhs->u.unary.type->tag,
                                   rhs->u.unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.unary.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.unary.operand, rhs->u.unary.type->tag, visible, global, members,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type->tag, rhs->u.unary.mode, &lhs, out,
                          &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, visible, global, members,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, visible, global, members,
                                  const_name, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type->tag, &lhs, &rhs_val, out, &tmp_diag,
                            line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, members, const_name,
                                  &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, members, const_name,
                                  &rhs_val, line, diag) != 0) {
            return -1;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_AND && lhs.bits == 0) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs.bits != 0) {
            *out = tc_value_make(TC_BOOL, 1);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_XOR) {
            /* xor 不短路：两侧均需求值 */
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_un.operand, TC_BOOL, visible, global, members, const_name,
                                  &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (rhs->u.bitwise_bin.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type->tag, visible,
                                  global, members, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type->tag, visible,
                                  global, members, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_binary(rhs->u.bitwise_bin.op, rhs->u.bitwise_bin.type->tag, &lhs,
                                   &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        if (rhs->u.bitwise_un.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type->tag, visible,
                                  global, members, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_unary(rhs->u.bitwise_un.type->tag, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        if (tc_validate_shift_mode(rhs->u.shift.op, rhs->u.shift.type->tag,
                                   rhs->u.shift.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.shift.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.value, rhs->u.shift.type->tag, visible, global, members,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.count, rhs->u.shift.type->tag, visible, global, members,
                                  const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_shift(rhs->u.shift.op, rhs->u.shift.type->tag, rhs->u.shift.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (tc_validate_fp_arith_mode(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                                      rhs->u.float_arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_arith.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type->tag, visible,
                                  global, members, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type->tag, visible,
                                  global, members, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_arith(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                             rhs->u.float_arith.mode,
                             &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (tc_validate_fp_unary_mode(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                                      rhs->u.float_unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_unary.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type->tag, visible,
                                  global, members, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_unary(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                             rhs->u.float_unary.mode,
                             &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (tc_validate_fp_compare_mode(rhs->u.float_compare.type->tag,
                                        rhs->u.float_compare.mode, diag, line) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type->tag, visible,
                                  global, members, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type->tag, visible,
                                  global, members, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type->tag,
                               rhs->u.float_compare.mode, &lhs, &rhs_val, out, &tmp_diag,
                               line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        TcBitcastRhs *bitcast = (TcBitcastRhs *)&rhs->u.bitcast;
        TcValue source = {0};
        TcTypeTag source_type = TC_INT32;
        int width = tc_type_bit_width(bitcast->target.tag);

        if (bitcast->target.tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant bitcast type mismatch");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol = tc_find_named_binding(visible, global, bitcast->source.u.name,
                                                           members);
            char msg[128];

            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               bitcast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            source_type = tc_type_tag_of(symbol->type);
        } else if (bitcast->source.kind == TC_OPERAND_FIELD_READ) {
            if (!bitcast->source.u.field_read.resolved.resolved ||
                !bitcast->source.u.field_read.resolved.field_type) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid constant bitcast source");
                return -1;
            }
            source_type = tc_type_tag_of(bitcast->source.u.field_read.resolved.field_type);
        } else if (bitcast->source.u.lit.is_bool) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        } else if (bitcast->source.u.lit.is_nullptr) {
            /*
             * `nullptr` 不参与 `bitcast`（含常量路径）。
             * 语言标准 §6.6.1.1 的源类型表未列入该组合，§3.10.2 的定型位置也不含
             * `bitcast`；按 §1.3 静态拒绝。`cast(ptr<T>, nullptr)` 仍合法（§6.6.6）。
             */
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "nullptr cannot participate in bitcast");
            return -1;
            *out = tc_value_make(bitcast->target.tag, 0);
            return 0;
        } else if (bitcast->source.u.lit.is_float) {
            if (!isfinite(bitcast->source.u.lit.float_value) &&
                !bitcast->source.u.lit.float32_suffix) {
                source_type = width == 32 ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = bitcast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            }
        } else if (width == 64) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        } else if (width == 32) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT32 : TC_INT32;
        } else if (width == 16) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT16 : TC_INT16;
        } else {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT8 : TC_INT8;
        }
        if (tc_type_is_bool(bitcast->target.tag) || tc_type_is_bool(source_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        }
        /* §6.6.6 / §3.10.9：指针不得与浮点互转；类型类别先于位宽。 */
        if ((source_type == TC_PTR && tc_type_is_float(bitcast->target.tag)) ||
            (bitcast->target.tag == TC_PTR && tc_type_is_float(source_type))) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer and float types cannot participate in bitcast");
            return -1;
        }
        if (tc_type_bit_width(source_type) != width) {
            tc_diagnostic_set(diag, TC_CE_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source and target widths must match");
            return -1;
        }
        bitcast->source_type = tc_type_tag_singleton(source_type);
        bitcast->source_type_resolved = 1;
        if (tc_eval_const_operand(&bitcast->source, source_type, visible,
                                  global, members, const_name, &source, line,
                                  diag) != 0) {
            return -1;
        }
        return tc_exec_bitcast(bitcast->target.tag, &source, out, diag, line);
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        TcCastRhs *cast = (TcCastRhs *)&rhs->u.const_cast;
        TcValue src_val = {0};
        TcTypeTag source_type = TC_INT64;

        if (cast->target.tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant cast target type mismatch");
            return -1;
        }
        if (cast->target.tag == TC_STRUCT || cast->target.tag == TC_MEMBLOCK ||
            cast->target.tag == TC_VOID) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target must be scalar or ptr type");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT && cast->source.u.lit.is_nullptr) {
            if (cast->target.tag != TC_PTR) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "pointer cast requires a pointer source");
                return -1;
            }
            *out = tc_value_make(TC_PTR, 0);
            return 0;
        }
        if (cast->target.tag == TC_PTR) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer cast requires a pointer source");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT) {
            if (cast->source.u.lit.is_bool) {
                source_type = TC_BOOL;
            } else if (cast->source.u.lit.is_float) {
                source_type = cast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = cast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
            }
            /*
             * §6.6.1.1 规定无后缀整数字面量的源类型是 int64，数值须先落在
             * 该类型范围内；不满足时属**字面量**诊断（TC_CE_LITERAL_OUT_OF_RANGE /
             * TC_CE_LITERAL_TYPE，附录 B.6），不得降级为 TC_CE_CONSTANT_EXPRESSION
             *（var 形式的同一表达式即报字面量码，§5.2.1 第 2 步）。
             */
            if (tc_check_literal(&cast->source.u.lit, source_type, line, diag,
                                 TC_CE_LITERAL_TYPE) != 0) {
                return -1;
            }
            src_val = tc_literal_to_value(&cast->source.u.lit, source_type);
        } else if (cast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol =
                tc_find_named_binding(visible, global, cast->source.u.name, members);
            char msg[128];

            if (const_name && strcmp(cast->source.u.name, const_name) == 0) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                /* 派生失败：挂起的 CT 类诊断仍是首个规范诊断。 */
                if (tc_const_value_withheld(symbol, diag)) {
                    return -1;
                }
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
            }
            src_val = symbol->const_value;
            source_type = tc_type_tag_of(symbol->type);
        } else if (cast->source.kind == TC_OPERAND_FIELD_READ) {
            if (!cast->source.u.field_read.resolved.resolved ||
                !cast->source.u.field_read.resolved.field_type) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid constant cast source");
                return -1;
            }
            source_type = tc_type_tag_of(cast->source.u.field_read.resolved.field_type);
            if (tc_eval_const_operand(&cast->source, source_type, visible, global, members, const_name,
                                      &src_val, line, diag) != 0) {
                return -1;
            }
        } else {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant cast source");
            return -1;
        }
        cast->source_type = tc_type_tag_singleton(source_type);
        cast->source_type_resolved = 1;
        tc_diagnostic_init(&tmp_diag);
        if ((cast->mode == TC_TRUNC_TRUNCATE
                 ? tc_exec_truncate(cast->target.tag, &src_val, out, &tmp_diag, line)
                 : tc_exec_cast(cast->target.tag, &src_val, out, &tmp_diag, line)) != 0) {
            if (tmp_diag.kind == TC_CE_MODE_MISMATCH) {
                tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "truncate requires an integer target narrower than the source");
            } else {
                tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            }
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        const TcResolvedBinding *binding = &rhs->u.memblock_count.binding;

        if (!binding->resolved) {
            /*
             * `static let` 的求值早于 Pass2：此时 `.count` 的基址绑定尚未固化，
             * 需按名解析（含 `Self.` / 导入限定名）。基址须是编译期常量
             * （`let` / `static let`）；引用可变绑定（`var` / `static var`）
             * 不构成常量来源（语言标准 §5.2.1、§6.7.2.5）。
             */
            const TcSymbol *base_sym =
                tc_find_named_binding(visible, global, rhs->u.memblock_count.memblock_name, members);

            if (base_sym && base_sym->sym_kind == TC_SYM_CONSTANT && base_sym->type &&
                base_sym->type->tag == TC_MEMBLOCK) {
                if (expected_type != TC_USIZE) {
                    tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                      "constant type does not match expected type");
                    return -1;
                }
                *out = tc_value_make(TC_USIZE, tc_type_memblock_count(base_sym->type));
                return 0;
            }
        }
        if (!binding->resolved || !binding->is_const) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (expected_type != TC_USIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant type does not match expected type");
            return -1;
        }
        *out = tc_value_make(TC_USIZE, tc_type_memblock_count(binding->type));
        return 0;
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        if (expected_type != TC_STRUCT) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "struct constructor type mismatch in constant expression");
            return -1;
        }
        return tc_eval_const_struct_ctor(rhs, struct_table, visible, global, const_name, members,
                                         out, line, diag);
    }

    if (rhs->kind == TC_RHS_PTR_SIZE) {
        size_t bits = 0;

        if (expected_type != TC_USIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        bits = tc_sizeof_bits_ex(&rhs->u.ptr_size.pointee_type, tc_struct_table_width_bits,
                                 struct_table);
        *out = tc_value_make(TC_USIZE, (uint64_t)bits);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        if (expected_type != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor type mismatch in constant expression");
            return -1;
        }
        return tc_eval_const_memblock_ctor(rhs, struct_table, visible, global, const_name, members,
                                           out, line, diag);
    }

    tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                      "invalid constant expression");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  公开接口 — 编译期求值 let RHS                                        */
/* ------------------------------------------------------------------ */

int tc_resolve_const_value(TcSymbol *sym, const TcRhs *rhs, const TcSymbolTable *visible,
                           const TcSymbolTable *global, const TcStructTable *struct_table,
                           const TcMemberIndex *members, int line, TcDiagnostic *diag) {
    TcValue value = {0};

    if (tc_eval_const_rhs(rhs, tc_type_tag_of(sym->type), visible, global, struct_table, sym->name,
                          members, &value, line, diag) != 0) {
        return -1;
    }
    {
        int aliased = tc_const_heap_named(value.bits, visible) ||
                      tc_const_heap_named(value.bits, global);

        /* 先判断是否别名，再写入本符号，避免 named 扫到自己 */
        sym->const_value = value;
        sym->has_const_value = 1;
        sym->owns_const_heap = 0;
        if (value.type && (value.type->tag == TC_STRUCT || value.type->tag == TC_MEMBLOCK) &&
            value.bits != 0 && !aliased) {
            sym->owns_const_heap = 1;
        }
    }
    return 0;
}
