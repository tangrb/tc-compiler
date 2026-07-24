/*
 * tc_ptr_exec.c — ptr 抽象地址编码与运行时运算
 */
#include "tc_ptr_exec.h"

#include "tc_semantics.h"

#include <stdint.h>

#define TC_PTR_TAG 1ULL

static int tc_ptr_is_null(uint64_t bits) {
    return bits == 0;
}

static int tc_ptr_decode_slot(uint64_t bits, int *slot) {
    if (tc_ptr_is_null(bits) || (bits & TC_PTR_TAG) == 0) {
        return -1;
    }
    *slot = (int)(bits >> 1);
    return 0;
}

static uint64_t tc_ptr_encode_slot(int slot) {
    return ((uint64_t)slot << 1) | TC_PTR_TAG;
}

static int tc_ptr_eval_operand(const TcOperand *operand, TcExecuteCtx *ctx, TcValue *out,
                                 TcDiagnostic *diag, int line) {
    return tc_eval_operand(operand, TC_PTR, ctx, out, diag, line);
}

int tc_exec_ptr_address(const TcType *pointee, const char *name, TcExecuteCtx *ctx,
                        TcValue *out, TcDiagnostic *diag, int line) {
    const TcSymbol *sym = NULL;

    (void)pointee;
    if (!name || !ctx || !out || !diag) {
        return -1;
    }
    sym = tc_exec_find_symbol(ctx->symbols, name);
    if (!sym || sym->slot < 0) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved ptr_address target");
        return -1;
    }
    out->type = TC_PTR;
    out->bits = tc_ptr_encode_slot(sym->slot);
    return 0;
}

int tc_exec_ptr_load(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    int slot = 0;

    (void)pointee;
    if (tc_ptr_eval_operand(ptr_op, ctx, &ptr_value, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_is_null(ptr_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0 || !ctx->slots || slot < 0) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid pointer value");
        return -1;
    }
    *out = ctx->slots[slot];
    return 0;
}

int tc_exec_ptr_store(const TcType *pointee, const TcOperand *ptr_op, const TcOperand *value_op,
                      TcExecuteCtx *ctx, TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    TcValue value;
    int slot = 0;
    TcTypeKind store_type = pointee ? pointee->kind : TC_VOID;

    if (tc_ptr_eval_operand(ptr_op, ctx, &ptr_value, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_is_null(ptr_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0 || !ctx->slots || slot < 0) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid pointer value");
        return -1;
    }
    if (tc_eval_operand(value_op, store_type, ctx, &value, diag, line) != 0) {
        return -1;
    }
    if (store_type == TC_BOOL) {
        value.bits = value.bits ? 1ULL : 0ULL;
        value.type = TC_BOOL;
    }
    ctx->slots[slot] = value;
    return 0;
}

static int tc_ptr_read_offset(const TcOperand *offset_op, TcExecuteCtx *ctx, int64_t *out,
                              TcDiagnostic *diag, int line) {
    TcValue offset_value;

    if (tc_eval_operand(offset_op, TC_USIZE, ctx, &offset_value, diag, line) != 0 &&
        tc_eval_operand(offset_op, TC_ISIZE, ctx, &offset_value, diag, line) != 0) {
        return -1;
    }
    if (offset_value.type == TC_ISIZE) {
        *out = tc_bits_to_signed(TC_ISIZE, offset_value.bits);
    } else {
        *out = (int64_t)offset_value.bits;
    }
    return 0;
}

int tc_exec_ptr_arith(int is_add, const TcType *pointee, const TcOperand *ptr_op,
                      const TcOperand *offset_op, TcExecuteCtx *ctx, TcValue *out,
                      TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    int64_t offset = 0;
    int slot = 0;
    int64_t new_slot = 0;

    (void)pointee;
    if (tc_ptr_eval_operand(ptr_op, ctx, &ptr_value, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_is_null(ptr_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_ARITHMETIC, line, TC_COLUMN_UNKNOWN,
                          "null pointer arithmetic");
        return -1;
    }
    if (tc_ptr_read_offset(offset_op, ctx, &offset, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid pointer value");
        return -1;
    }
    new_slot = is_add ? (int64_t)slot + offset : (int64_t)slot - offset;
    out->type = TC_PTR;
    out->bits = tc_ptr_encode_slot((int)new_slot);
    return 0;
}

int tc_exec_ptr_compare(TcCompareOp op, const TcType *pointee, const TcOperand *lhs_op,
                        const TcOperand *rhs_op, TcExecuteCtx *ctx, TcValue *out,
                        TcDiagnostic *diag, int line) {
    TcValue lhs;
    TcValue rhs_value;
    int result = 0;

    (void)pointee;
    if (tc_ptr_eval_operand(lhs_op, ctx, &lhs, diag, line) != 0 ||
        tc_ptr_eval_operand(rhs_op, ctx, &rhs_value, diag, line) != 0) {
        return -1;
    }
    if (op == TC_CMP_EQ || op == TC_CMP_NE) {
        result = (lhs.bits == rhs_value.bits) ? 1 : 0;
        if (op == TC_CMP_NE) {
            result = result ? 0 : 1;
        }
        out->type = TC_BOOL;
        out->bits = result ? 1ULL : 0ULL;
        return 0;
    }
    if (tc_ptr_is_null(lhs.bits) || tc_ptr_is_null(rhs_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    switch (op) {
    case TC_CMP_LT:
        result = lhs.bits < rhs_value.bits;
        break;
    case TC_CMP_LE:
        result = lhs.bits <= rhs_value.bits;
        break;
    case TC_CMP_GT:
        result = lhs.bits > rhs_value.bits;
        break;
    case TC_CMP_GE:
        result = lhs.bits >= rhs_value.bits;
        break;
    default:
        tc_exec_set_internal_error(diag, line, "internal error: invalid pointer compare");
        return -1;
    }
    out->type = TC_BOOL;
    out->bits = result ? 1ULL : 0ULL;
    return 0;
}

int tc_exec_ptr_size(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line) {
    size_t bits = 0;

    (void)ptr_op;
    (void)ctx;
    if (!pointee || !out) {
        return -1;
    }
    bits = tc_sizeof_bits(pointee);
    out->type = TC_USIZE;
    out->bits = bits;
    (void)line;
    (void)diag;
    return 0;
}
