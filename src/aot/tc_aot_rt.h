/*
 * tc_aot_rt.h — AOT 生成 C 代码的运行时辅助接口
 *
 * 提供全部 tc_aot_* shim 函数，覆盖：字面量构造、算术、单目、比较、
 * 逻辑（含短路）、按位运算、移位、类型转换、I/O（write/read）、
 * 诊断初始化和错误中止。
 *
 * 所有 shim 函数内部委托 tc_semantics.c / tc_io.c 完成实际语义运算，
 * 保证 AOT 生成代码与 TC-VM 行为完全一致。
 * 这些函数被 tc-aot 生成的 main.c 调用。
 */
#ifndef TC_AOT_RT_H
#define TC_AOT_RT_H

#include <stdint.h>

#include "tc_diagnostic.h"
#include "tc_types.h"

void tc_aot_diag_init(TcDiagnostic *diag);
void tc_aot_init_slots(uint64_t *slots, size_t count);
uint64_t tc_aot_lit(TcTypeTag type, uint64_t magnitude, int negative, int unsigned_suffix);
int tc_aot_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t lhs,
                 uint64_t rhs, TcDiagnostic *diag, int line);
int tc_aot_unary(TcUnaryOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t operand,
                 TcDiagnostic *diag, int line);
int tc_aot_compare(TcCompareOp op, TcTypeTag type, uint64_t *out, uint64_t lhs, uint64_t rhs,
                   TcDiagnostic *diag, int line);
int tc_aot_logic(TcLogicOp op, uint64_t *out, uint64_t lhs, uint64_t rhs, TcDiagnostic *diag,
                 int line);
int tc_aot_logic_unary(TcLogicOp op, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                       int line);
int tc_aot_bitwise_binary(TcBitwiseOp op, TcTypeTag type, uint64_t *out, uint64_t lhs,
                          uint64_t rhs, TcDiagnostic *diag, int line);
int tc_aot_bitwise_unary(TcTypeTag type, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                         int line);
int tc_aot_shift(TcShiftOp op, TcTypeTag type, TcWrapMode mode, uint64_t *out, uint64_t value,
                 uint64_t count, TcDiagnostic *diag, int line);
int tc_aot_cast(TcTypeTag target, TcTruncateMode mode, uint64_t src_bits, TcTypeTag src_type,
                uint64_t *out, TcDiagnostic *diag, int line);
int tc_aot_bitcast(TcTypeTag target, TcTypeTag source_type, uint64_t *out, uint64_t source_bits,
                   TcDiagnostic *diag, int line);
int tc_aot_fp_arith(TcArithOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out, uint64_t lhs,
                    uint64_t rhs, TcDiagnostic *diag, int line);
int tc_aot_fp_unary(TcUnaryOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out,
                    uint64_t operand, TcDiagnostic *diag, int line);
int tc_aot_fp_compare(TcCompareOp op, TcTypeTag type, TcFloatMode mode, uint64_t *out,
                        uint64_t lhs, uint64_t rhs, TcDiagnostic *diag, int line);
int tc_aot_fp_cast(TcTypeTag target, TcTruncateMode mode, uint64_t src_bits, TcTypeTag src_type,
                   uint64_t *out, TcDiagnostic *diag, int line);
int tc_aot_write(TcTypeTag type, TcFormatSpec fmt, uint64_t bits, int newline,
                 TcDiagnostic *diag, int line);
int tc_aot_read(TcTypeTag type, uint64_t *out, TcDiagnostic *diag, int line);
void tc_aot_abort(const TcDiagnostic *diag, int line);

/* ---- Phase 5: ptr / memblock（槽抽象地址编码与 VM 一致） ---- */

uint64_t tc_aot_ptr_address(int slot);
int tc_aot_ptr_load(uint64_t *slots, uint64_t ptr_bits, uint64_t *out, TcDiagnostic *diag,
                    int line);
int tc_aot_ptr_store(uint64_t *slots, uint64_t ptr_bits, uint64_t value_bits,
                     TcTypeTag store_type, TcDiagnostic *diag, int line);
int tc_aot_ptr_arith(int is_add, uint64_t ptr_bits, int64_t offset, uint64_t *out,
                     TcDiagnostic *diag, int line);
int tc_aot_ptr_compare(TcCompareOp op, uint64_t lhs, uint64_t rhs, uint64_t *out,
                       TcDiagnostic *diag, int line);
uint64_t tc_aot_ptr_size(size_t sizeof_bits);

uint64_t tc_aot_memblock_alloc(uint64_t count, size_t element_bytes, TcDiagnostic *diag,
                               int line);
uint64_t tc_aot_memblock_clone(uint64_t src, size_t element_bytes, uint64_t count,
                               TcDiagnostic *diag, int line);
void tc_aot_memblock_set_elem(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                              uint64_t value_bits);
uint64_t tc_aot_memblock_get_count(uint64_t mb_bits);
int tc_aot_memblock_load(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                         TcTypeTag elem_type, uint64_t *out, TcDiagnostic *diag, int line);
int tc_aot_memblock_store(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                          uint64_t value_bits, TcTypeTag elem_type, TcDiagnostic *diag,
                          int line);
int tc_aot_memcopy_unsafe(uint64_t *slots, uint64_t dst_ptr, uint64_t dst_index,
                           uint64_t src_ptr, uint64_t src_index, int64_t length,
                           size_t element_bytes, TcDiagnostic *diag, int line);
int tc_aot_memblock_copy(uint64_t dst_bits, uint64_t dst_index, uint64_t src_bits,
                         uint64_t src_index, uint64_t length, size_t element_bytes,
                         TcDiagnostic *diag, int line);
void tc_aot_memblock_heap_free_all(void);

/* ---- struct 运行时（堆块指针存于 slots[]，与 VM 一致） ---- */
uint64_t tc_aot_struct_alloc(size_t bytes, TcDiagnostic *diag, int line);
uint64_t tc_aot_struct_clone(uint64_t src_bits, size_t bytes, TcDiagnostic *diag, int line);
void tc_aot_struct_store_bits(uint64_t dst_bits, size_t offset, size_t nbytes, uint64_t value_bits);
void tc_aot_struct_load_bits(uint64_t src_bits, size_t offset, size_t nbytes, uint64_t *out);
void tc_aot_struct_memcpy_field(uint64_t dst_bits, size_t offset, size_t nbytes,
                                uint64_t src_bits);
uint64_t tc_aot_struct_extract(uint64_t src_bits, size_t offset, size_t nbytes,
                               TcDiagnostic *diag, int line);
void tc_aot_struct_heap_free_all(void);

#endif
