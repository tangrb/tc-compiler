/*
 * tc_aot_rt.c — AOT 运行时辅助实现
 *
 * 将 AOT 生成的 C 代码中的 uint64_t 槽位操作委托给 tc_semantics.c，
 * 包括：字面量求值（tc_aot_lit）、算术运算（tc_aot_arith）、
 * 单目运算（tc_aot_unary）、按位运算（tc_aot_bitwise_*）、移位（tc_aot_shift）、
 * 类型转换（tc_aot_cast）、
 * 格式化输出（tc_aot_write）、输入（tc_aot_read）及错误中止（tc_aot_abort）。
 */
#include "tc_aot_rt.h"

#include "tc_io.h"
#include "tc_semantics.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tc_aot_diag_init(TcDiagnostic *diag) {
    tc_diagnostic_init(diag);
}

/*
 * AOT 槽位初始化：用未初始化哨兵值填充所有槽位，
 * 与 VM 的 tc_slots_init_uninitialized 使用一致的哨兵值
 * （TC_UNINITIALIZED_SLOT_BITS），保障"使用未初始化变量"检测
 * 在 VM 和 AOT 下的行为一致。
 */
void tc_aot_init_slots(uint64_t *slots, size_t count) {
    tc_slot_bits_init_uninitialized(slots, count);
}

/* ------------------------------------------------------------------ */
/*  字面量 & 算术 & 单目 & cast 委托                                      */
/* ------------------------------------------------------------------ */

uint64_t tc_aot_lit(TcTypeTag type, uint64_t magnitude, int negative, int unsigned_suffix) {
    TcLiteral lit;
    TcValue value;

    if (tc_type_is_float(type)) {
        return magnitude;
    }

    memset(&lit, 0, sizeof(lit));
    lit.magnitude = magnitude;
    lit.negative = negative;
    lit.unsigned_suffix = unsigned_suffix;
    lit.is_bool = tc_type_is_bool(type) ? 1 : 0;
    value = tc_literal_to_value(&lit, type);
    return value.bits;
}

int tc_aot_compare(TcCompareOp op, TcTypeTag type, uint64_t *out, uint64_t lhs, uint64_t rhs,
                   TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_compare(op, type, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_logic(TcLogicOp op, uint64_t *out, uint64_t lhs, uint64_t rhs, TcDiagnostic *diag,
                 int line) {
    TcValue lhs_value = tc_value_make(TC_BOOL, lhs);
    TcValue rhs_value = tc_value_make(TC_BOOL, rhs);
    TcValue result;

    if (op == TC_LOGIC_NOT) {
        if (tc_exec_logic_unary(op, &lhs_value, &result, diag, line) != 0) {
            return -1;
        }
    } else if (tc_exec_logic_binary(op, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_logic_unary(TcLogicOp op, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                       int line) {
    TcValue operand_value = tc_value_make(TC_BOOL, operand);
    TcValue result;

    if (tc_exec_logic_unary(op, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t lhs,
                 uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_arith(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_unary(TcUnaryOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t operand,
                 TcDiagnostic *diag, int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_unary(op, type, mode, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitwise_binary(TcBitwiseOp op, TcTypeTag type, uint64_t *out, uint64_t lhs,
                          uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_bitwise_binary(op, type, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitwise_unary(TcTypeTag type, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                         int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_bitwise_unary(type, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_shift(TcShiftOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t value,
                 uint64_t count, TcDiagnostic *diag, int line) {
    TcValue value_v = tc_value_make(type, value);
    TcValue count_v = tc_value_make(type, count);
    TcValue result;

    if (tc_exec_shift(op, type, mode, &value_v, &count_v, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_cast(TcTypeTag target, TcTruncateMode mode, uint64_t src_bits, TcTypeTag src_type,
                uint64_t *out, TcDiagnostic *diag, int line) {
    TcValue src = tc_value_make(src_type, src_bits);
    TcValue result;

    if ((mode == TC_TRUNC_TRUNCATE
             ? tc_exec_truncate(target, &src, &result, diag, line)
             : tc_exec_cast(target, &src, &result, diag, line)) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitcast(TcTypeTag target, TcTypeTag source_type, uint64_t *out, uint64_t source_bits,
                   TcDiagnostic *diag, int line) {
    TcValue source = tc_value_make(source_type, source_bits);
    TcValue result;

    if (tc_exec_bitcast(target, &source, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_arith(TcArithOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out, uint64_t lhs,
                    uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_fp_arith(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_unary(TcUnaryOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out,
                    uint64_t operand, TcDiagnostic *diag, int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_fp_unary(op, type, mode, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_compare(TcCompareOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out,
                      uint64_t lhs, uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_fp_compare(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_cast(TcTypeTag target, TcTruncateMode mode, uint64_t src_bits, TcTypeTag src_type,
                   uint64_t *out, TcDiagnostic *diag, int line) {
    TcValue src = tc_value_make(src_type, src_bits);
    TcValue result;

    if ((mode == TC_TRUNC_TRUNCATE
             ? tc_exec_truncate(target, &src, &result, diag, line)
             : tc_exec_cast(target, &src, &result, diag, line)) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

int tc_aot_write(TcTypeTag type, TcFormatSpec fmt, uint64_t bits, int newline,
                 TcDiagnostic *diag, int line) {
    TcValue value = tc_value_make(type, bits);

    if (tc_io_write_value(&value, fmt, newline, stdout) != 0) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  输入                                                               */
/* ------------------------------------------------------------------ */

int tc_aot_read(TcTypeTag type, uint64_t *out, TcDiagnostic *diag, int line) {
    return tc_io_read_value(type, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  错误中止                                                           */
/* ------------------------------------------------------------------ */

/*
 * AOT 生成代码的错误回调：打印诊断信息后以 exit(1) 终止。
 * 与 VM 的 fail-fast 一致：遇到首个运行时错误即中止执行。
 * line 参数保留供扩展使用（如精确指示生成代码中的出错位置）。
 */
void tc_aot_abort(const TcDiagnostic *diag, int line) {
    (void)line;
    tc_diagnostic_print(diag, stderr);
    exit(1);
}

/* ------------------------------------------------------------------ */
/*  Phase 5: ptr / memblock                                             */
/* ------------------------------------------------------------------ */

#define TC_AOT_PTR_TAG 1ULL

static void **tc_aot_mb_heap = NULL;
static size_t tc_aot_mb_heap_count = 0;
static size_t tc_aot_mb_heap_cap = 0;

static int tc_aot_mb_track(void *block, TcDiagnostic *diag, int line) {
    void **items = NULL;

    if (tc_aot_mb_heap_count == tc_aot_mb_heap_cap) {
        size_t new_cap = tc_aot_mb_heap_cap == 0 ? 8 : tc_aot_mb_heap_cap * 2;
        items = (void **)realloc(tc_aot_mb_heap, new_cap * sizeof(void *));
        if (!items) {
            free(block);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        tc_aot_mb_heap = items;
        tc_aot_mb_heap_cap = new_cap;
    }
    tc_aot_mb_heap[tc_aot_mb_heap_count++] = block;
    return 0;
}

static int tc_aot_ptr_decode(uint64_t bits, int *slot) {
    if (bits == 0 || (bits & TC_AOT_PTR_TAG) == 0) {
        return -1;
    }
    *slot = (int)(bits >> 1);
    return 0;
}

uint64_t tc_aot_ptr_address(int slot) {
    return ((uint64_t)slot << 1) | TC_AOT_PTR_TAG;
}

int tc_aot_ptr_load(uint64_t *slots, uint64_t ptr_bits, uint64_t *out, TcDiagnostic *diag,
                    int line) {
    int slot = 0;

    if (ptr_bits == 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_aot_ptr_decode(ptr_bits, &slot) != 0 || !slots || slot < 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    *out = slots[slot];
    return 0;
}

int tc_aot_ptr_store(uint64_t *slots, uint64_t ptr_bits, uint64_t value_bits,
                     TcTypeTag store_type, TcDiagnostic *diag, int line) {
    int slot = 0;

    if (ptr_bits == 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (tc_aot_ptr_decode(ptr_bits, &slot) != 0 || !slots || slot < 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if (store_type == TC_BOOL) {
        value_bits = value_bits ? 1ULL : 0ULL;
    }
    slots[slot] = value_bits;
    return 0;
}

int tc_aot_ptr_arith(int is_add, uint64_t ptr_bits, int64_t offset, uint64_t *out,
                     TcDiagnostic *diag, int line) {
    int slot = 0;
    int64_t new_slot = 0;

    if (ptr_bits == 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_ARITHMETIC, line, TC_COLUMN_UNKNOWN,
                          "null pointer arithmetic");
        return -1;
    }
    if (tc_aot_ptr_decode(ptr_bits, &slot) != 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_ARITHMETIC, line, TC_COLUMN_UNKNOWN,
                          "null pointer arithmetic");
        return -1;
    }
    new_slot = is_add ? (int64_t)slot + offset : (int64_t)slot - offset;
    *out = tc_aot_ptr_address((int)new_slot);
    return 0;
}

int tc_aot_ptr_compare(TcCompareOp op, uint64_t lhs, uint64_t rhs, uint64_t *out,
                       TcDiagnostic *diag, int line) {
    int result = 0;

    if (op == TC_CMP_EQ || op == TC_CMP_NE) {
        result = (lhs == rhs) ? 1 : 0;
        if (op == TC_CMP_NE) {
            result = result ? 0 : 1;
        }
        *out = result ? 1ULL : 0ULL;
        return 0;
    }
    if (lhs == 0 || rhs == 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    switch (op) {
    case TC_CMP_LT:
        result = lhs < rhs;
        break;
    case TC_CMP_LE:
        result = lhs <= rhs;
        break;
    case TC_CMP_GT:
        result = lhs > rhs;
        break;
    case TC_CMP_GE:
        result = lhs >= rhs;
        break;
    default:
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "internal error: invalid pointer compare");
        return -1;
    }
    *out = result ? 1ULL : 0ULL;
    return 0;
}

uint64_t tc_aot_ptr_size(size_t sizeof_bits) {
    return (uint64_t)sizeof_bits;
}

static uint8_t *tc_aot_mb_elem_ptr(void *block, size_t element_bytes, uint64_t index) {
    return (uint8_t *)block + sizeof(uint64_t) + index * element_bytes;
}

uint64_t tc_aot_memblock_alloc(uint64_t count, size_t element_bytes, TcDiagnostic *diag,
                               int line) {
    size_t payload = (size_t)count * element_bytes;
    void *block = malloc(sizeof(uint64_t) + payload);

    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return 0;
    }
    memcpy(block, &count, sizeof(uint64_t));
    memset((uint8_t *)block + sizeof(uint64_t), 0, payload);
    if (tc_aot_mb_track(block, diag, line) != 0) {
        return 0;
    }
    return (uint64_t)(uintptr_t)block;
}

uint64_t tc_aot_memblock_clone(uint64_t src, size_t element_bytes, uint64_t count,
                               TcDiagnostic *diag, int line) {
    size_t payload = (size_t)count * element_bytes;
    void *src_block = (void *)(uintptr_t)src;
    void *block = NULL;

    if (!src_block) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return 0;
    }
    block = malloc(sizeof(uint64_t) + payload);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return 0;
    }
    /* 深拷贝：整块复制（含 usize 长度头部与全部元素位串），头部强制写回 count，
     * 保证克隆结果始终处于规范状态（§3.8.2 头部值 == 类型参数 count）。 */
    memcpy(block, src_block, sizeof(uint64_t) + payload);
    memcpy(block, &count, sizeof(uint64_t));
    if (tc_aot_mb_track(block, diag, line) != 0) {
        return 0;
    }
    return (uint64_t)(uintptr_t)block;
}

void tc_aot_memblock_set_elem(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                              uint64_t value_bits) {
    void *block = (void *)(uintptr_t)mb_bits;

    if (!block) {
        return;
    }
    memcpy(tc_aot_mb_elem_ptr(block, element_bytes, index), &value_bits, element_bytes);
}

uint64_t tc_aot_memblock_get_count(uint64_t mb_bits) {
    void *block = (void *)(uintptr_t)mb_bits;
    uint64_t count = 0;

    if (!block) {
        return 0;
    }
    memcpy(&count, block, sizeof(uint64_t));
    return count;
}

int tc_aot_memblock_load(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                         TcTypeTag elem_type, uint64_t *out, TcDiagnostic *diag, int line) {
    void *block = (void *)(uintptr_t)mb_bits;
    uint64_t count = 0;
    uint64_t bits = 0;

    (void)elem_type;
    if (!block) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    memcpy(&count, block, sizeof(uint64_t));
    if (index >= count) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    memcpy(&bits, tc_aot_mb_elem_ptr(block, element_bytes, index), element_bytes);
    *out = bits;
    return 0;
}

int tc_aot_memblock_store(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                          uint64_t value_bits, TcTypeTag elem_type, TcDiagnostic *diag,
                          int line) {
    void *block = (void *)(uintptr_t)mb_bits;
    uint64_t count = 0;

    if (!block) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    memcpy(&count, block, sizeof(uint64_t));
    if (index >= count) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    if (elem_type == TC_BOOL) {
        value_bits = value_bits ? 1ULL : 0ULL;
    }
    memcpy(tc_aot_mb_elem_ptr(block, element_bytes, index), &value_bits, element_bytes);
    return 0;
}

int tc_aot_memblock_copy(uint64_t dst_bits, uint64_t dst_index, uint64_t src_bits,
                         uint64_t src_index, uint64_t length, size_t element_bytes,
                         TcDiagnostic *diag, int line) {
    void *dst = (void *)(uintptr_t)dst_bits;
    void *src = (void *)(uintptr_t)src_bits;
    uint64_t dst_count = 0;
    uint64_t src_count = 0;
    size_t nbytes = 0;
    void *temp = NULL;

    if (!dst || !src) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    memcpy(&dst_count, dst, sizeof(uint64_t));
    memcpy(&src_count, src, sizeof(uint64_t));
    if (length > 0 && (dst_index + length > dst_count || src_index + length > src_count)) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    nbytes = (size_t)length * element_bytes;
    temp = malloc(nbytes);
    if (!temp) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    memcpy(temp, tc_aot_mb_elem_ptr(src, element_bytes, src_index), nbytes);
    memcpy(tc_aot_mb_elem_ptr(dst, element_bytes, dst_index), temp, nbytes);
    free(temp);
    return 0;
}

void tc_aot_memblock_heap_free_all(void) {
    size_t i = 0;

    for (i = 0; i < tc_aot_mb_heap_count; i++) {
        free(tc_aot_mb_heap[i]);
    }
    free(tc_aot_mb_heap);
    tc_aot_mb_heap = NULL;
    tc_aot_mb_heap_count = 0;
    tc_aot_mb_heap_cap = 0;
}

/* ------------------------------------------------------------------ */
/*  struct                                                              */
/* ------------------------------------------------------------------ */

static void **tc_aot_st_heap = NULL;
static size_t tc_aot_st_heap_count = 0;
static size_t tc_aot_st_heap_cap = 0;

static int tc_aot_st_track(void *block, TcDiagnostic *diag, int line) {
    void **items = NULL;

    if (tc_aot_st_heap_count == tc_aot_st_heap_cap) {
        size_t new_cap = tc_aot_st_heap_cap == 0 ? 8 : tc_aot_st_heap_cap * 2;
        items = (void **)realloc(tc_aot_st_heap, new_cap * sizeof(void *));
        if (!items) {
            free(block);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        tc_aot_st_heap = items;
        tc_aot_st_heap_cap = new_cap;
    }
    tc_aot_st_heap[tc_aot_st_heap_count++] = block;
    return 0;
}

uint64_t tc_aot_struct_alloc(size_t bytes, TcDiagnostic *diag, int line) {
    void *block = NULL;

    if (bytes == 0) {
        return 0;
    }
    block = calloc(1, bytes);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return 0;
    }
    if (tc_aot_st_track(block, diag, line) != 0) {
        return 0;
    }
    return (uint64_t)(uintptr_t)block;
}

uint64_t tc_aot_struct_clone(uint64_t src_bits, size_t bytes, TcDiagnostic *diag, int line) {
    void *src = (void *)(uintptr_t)src_bits;
    uint64_t dst_bits = 0;
    void *dst = NULL;

    if (!src || bytes == 0) {
        return 0;
    }
    dst_bits = tc_aot_struct_alloc(bytes, diag, line);
    if (dst_bits == 0) {
        return 0;
    }
    dst = (void *)(uintptr_t)dst_bits;
    memcpy(dst, src, bytes);
    return dst_bits;
}

void tc_aot_struct_store_bits(uint64_t dst_bits, size_t offset, size_t nbytes, uint64_t value_bits) {
    uint8_t *dst = (uint8_t *)(uintptr_t)dst_bits;

    if (!dst || nbytes == 0) {
        return;
    }
    memset(dst + offset, 0, nbytes);
    memcpy(dst + offset, &value_bits, nbytes <= sizeof(value_bits) ? nbytes : sizeof(value_bits));
}

void tc_aot_struct_load_bits(uint64_t src_bits, size_t offset, size_t nbytes, uint64_t *out) {
    const uint8_t *src = (const uint8_t *)(uintptr_t)src_bits;

    if (!src || !out || nbytes == 0) {
        return;
    }
    *out = 0;
    memcpy(out, src + offset, nbytes <= sizeof(*out) ? nbytes : sizeof(*out));
}

void tc_aot_struct_memcpy_field(uint64_t dst_bits, size_t offset, size_t nbytes,
                                uint64_t src_bits) {
    uint8_t *dst = (uint8_t *)(uintptr_t)dst_bits;
    const void *src = (const void *)(uintptr_t)src_bits;

    if (!dst || !src || nbytes == 0) {
        return;
    }
    memcpy(dst + offset, src, nbytes);
}

uint64_t tc_aot_struct_extract(uint64_t src_bits, size_t offset, size_t nbytes,
                               TcDiagnostic *diag, int line) {
    const uint8_t *src = (const uint8_t *)(uintptr_t)src_bits;
    uint64_t dst_bits = 0;
    void *dst = NULL;

    if (!src || nbytes == 0) {
        return 0;
    }
    dst_bits = tc_aot_struct_alloc(nbytes, diag, line);
    if (dst_bits == 0) {
        return 0;
    }
    dst = (void *)(uintptr_t)dst_bits;
    memcpy(dst, src + offset, nbytes);
    return dst_bits;
}

void tc_aot_struct_heap_free_all(void) {
    size_t i = 0;

    for (i = 0; i < tc_aot_st_heap_count; i++) {
        free(tc_aot_st_heap[i]);
    }
    free(tc_aot_st_heap);
    tc_aot_st_heap = NULL;
    tc_aot_st_heap_count = 0;
    tc_aot_st_heap_cap = 0;
}
