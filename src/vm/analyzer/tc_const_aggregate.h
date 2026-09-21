/*
 * tc_const_aggregate.h — 常量 struct/memblock 构造与堆所有权
 *
 * 子模块共享：聚合构造在 tc_const_aggregate.c；operand/RHS 求值在
 * tc_const_eval.c。调用方仍 include tc_const_eval.h。
 */
#ifndef TC_CONST_AGGREGATE_H
#define TC_CONST_AGGREGATE_H

#include "tc_const_eval.h"
#include "tc_struct_check.h"

int tc_const_heap_named(uint64_t bits, const TcSymbolTable *table);
void tc_const_drop_temp_heap(TcValue *value, const TcSymbolTable *visible,
                             const TcSymbolTable *global);

int tc_eval_const_operand(const TcOperand *operand, TcTypeTag expected,
                          const TcSymbolTable *visible, const TcSymbolTable *global,
                          const TcMemberIndex *members, const char *const_name, TcValue *out,
                          int line, TcDiagnostic *diag);

int tc_eval_const_rhs(const TcRhs *rhs, TcTypeTag expected_type, const TcSymbolTable *visible,
                      const TcSymbolTable *global, const TcStructTable *struct_table,
                      const char *const_name, const TcMemberIndex *members, TcValue *out,
                      int line, TcDiagnostic *diag);

int tc_eval_const_struct_ctor(const TcRhs *rhs, const TcStructTable *table,
                              const TcSymbolTable *visible, const TcSymbolTable *global,
                              const char *const_name, const TcMemberIndex *members, TcValue *out,
                              int line, TcDiagnostic *diag);

int tc_eval_const_memblock_ctor(const TcRhs *rhs, const TcStructTable *struct_table,
                                const TcSymbolTable *visible, const TcSymbolTable *global,
                                const char *const_name, const TcMemberIndex *members, TcValue *out,
                                int line, TcDiagnostic *diag);

#endif /* TC_CONST_AGGREGATE_H */
