/*
 * tc_ptr_exec.c — ptr 抽象地址编码与运行时运算
 */
#include "tc_ptr_exec.h"

#include "tc_semantics.h"
#include "tc_struct_check.h"

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
    out->type = tc_type_tag_singleton(TC_PTR);
    out->bits = tc_ptr_encode_slot(sym->slot);
    return 0;
}

int tc_exec_ptr_load(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    int slot = 0;

    if (tc_ptr_eval_operand(ptr_op, ctx, &ptr_value, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_is_null(ptr_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0 || !ctx->slots || slot < 0 ||
        (size_t)slot >= ctx->slot_capacity) {
        /*
         * 抽象槽编码（§3.10.9、§1.3 实现定义清单第 4 项）之外的位模式一律按空指针
         * 处理：AOT 侧 tc_aot_ptr_load 对无法解码的编码同样报
         * TC_RE_NULL_POINTER_DEREFERENCE，两后端须给出一致结果，且不得把用户可
         * 触发的路径表现为实现内部错误（`bitcast(ptr<T>, <usize>)` 伪造编码）。
         */
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    *out = ctx->slots[slot];
    /*
     * 语言标准 §3.4 / §6.8.2：`ptr_load` 的结果类型为 `bool` 时
     * 按「`0x00` → `false`，其它字节 → `true`」规范化。经指针别名写入的非规范
     * 字节（如先 `ptr_store(int8, …, 2)`）读回后仍须落在 bool 的抽象值域 {0,1}，
     * 与 ptr_store 对 bool 的写入规范化对称。
     */
    if (pointee && pointee->tag == TC_BOOL) {
        out->bits = out->bits != 0 ? 1ULL : 0ULL;
        out->type = tc_type_tag_singleton(TC_BOOL);
    }
    return 0;
}

int tc_exec_ptr_store(const TcType *pointee, const TcOperand *ptr_op, const TcOperand *value_op,
                      TcExecuteCtx *ctx, TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    TcValue value;
    int slot = 0;
    TcTypeTag store_type = pointee ? pointee->tag : TC_VOID;

    if (tc_ptr_eval_operand(ptr_op, ctx, &ptr_value, diag, line) != 0) {
        return -1;
    }
    if (tc_ptr_is_null(ptr_value.bits)) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0 || !ctx->slots || slot < 0 ||
        (size_t)slot >= ctx->slot_capacity) {
        /* 同上：与 AOT tc_aot_ptr_store 一致的非法编码处理。 */
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_eval_operand(value_op, store_type, ctx, &value, diag, line) != 0) {
        return -1;
    }
    if (store_type == TC_BOOL) {
        value.bits = value.bits ? 1ULL : 0ULL;
        value.type = tc_type_tag_singleton(TC_BOOL);
    }
    ctx->slots[slot] = value;
    return 0;
}

static int tc_ptr_read_offset(const TcOperand *offset_op, TcExecuteCtx *ctx, uint64_t *out,
                              TcDiagnostic *diag, int line) {
    TcValue offset_value;

    /*
     * 偏移严格为 usize（§3.10.8）；分析器已静态拒绝 isize 偏移。
     * B-49：以**无符号**位模式返回，避免 ≥ 2^63 的偏移转 int64_t 触发有符号溢出
     *（UB）；指针算术按 §6.8.5 的无符号语义完成。
     */
    if (tc_eval_operand(offset_op, TC_USIZE, ctx, &offset_value, diag, line) != 0) {
        return -1;
    }
    *out = offset_value.bits;
    return 0;
}

int tc_exec_ptr_arith(int is_add, const TcType *pointee, const TcOperand *ptr_op,
                      const TcOperand *offset_op, TcExecuteCtx *ctx, TcValue *out,
                      TcDiagnostic *diag, int line) {
    TcValue ptr_value;
    uint64_t offset = 0;
    int slot = 0;
    uint64_t new_slot = 0;

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
    if (tc_ptr_decode_slot(ptr_value.bits, &slot) != 0 || (size_t)slot >= ctx->slot_capacity) {
        /* 与 AOT tc_aot_ptr_arith 一致：非法编码按空指针算术处理。 */
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_ARITHMETIC, line, TC_COLUMN_UNKNOWN,
                          "null pointer arithmetic");
        return -1;
    }
    /*
     * B-49：按 §6.8.5 的 usize 语义用**无符号**运算完成，避免 ≥ 2^63 的偏移转
     * int64_t 触发有符号溢出 UB；结果越过槽位容量时按「非法指针值」报
     * TC_RE_NULL_POINTER_ARITHMETIC（与 B-10/B-42 的非法编码口径一致），
     * 而不是回绕/截断成可能指向合法槽位的编码。
     */
    new_slot = is_add ? (uint64_t)slot + offset : (uint64_t)slot - offset;
    if (new_slot >= ctx->slot_capacity) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_ARITHMETIC, line, TC_COLUMN_UNKNOWN,
                          "null pointer arithmetic");
        return -1;
    }
    out->type = tc_type_tag_singleton(TC_PTR);
    out->bits = ((new_slot << 1) | TC_PTR_TAG);
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
        out->type = tc_type_tag_singleton(TC_BOOL);
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
    out->type = tc_type_tag_singleton(TC_BOOL);
    out->bits = result ? 1ULL : 0ULL;
    return 0;
}

int tc_exec_ptr_size(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line) {
    size_t bits = 0;

    (void)ptr_op;
    if (!pointee || !out) {
        return -1;
    }
    bits = tc_sizeof_bits_ex(pointee, tc_struct_table_width_bits, ctx->program->struct_table);
    out->type = tc_type_tag_singleton(TC_USIZE);
    out->bits = bits;
    (void)line;
    (void)diag;
    return 0;
}
