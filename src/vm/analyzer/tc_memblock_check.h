/*
 * tc_memblock_check.h — memblock RHS/语句静态验证
 *
 * 覆盖：memblock_load / 构造器 / .count / store / copy / memcopy_unsafe。
 * 类型等价只比较元素类型 T；声明长度 N 用于构造器与静态下标越界检查。
 * tc_type_equals 对 memblock 忽略 N（语言标准约定）。
 */
#ifndef TC_MEMBLOCK_CHECK_H
#define TC_MEMBLOCK_CHECK_H

#include "tc_types.h"
#include "tc_diagnostic.h"
#include "tc_warning.h"
#include "tc_symbol.h"
#include "tc_analyzer_internal.h"

struct TcStructTable;

int tc_memblock_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                          const TcSymbolTable *global, const struct TcStructTable *struct_table,
                          TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                          TcWarningList *warnings, const char *self_name);

int tc_memblock_check_store(const TcMemblockStoreStmt *stmt, const TcSymbolTable *visible,
                            const TcSymbolTable *global, const struct TcStructTable *struct_table,
                            TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                            TcWarningList *warnings);

/** 整块拷贝：两端须同为 memblock 且声明 N 相同；元素类型匹配语句注解 */
int tc_memblock_check_copy(const TcMemblockCopyStmt *stmt, const TcSymbolTable *visible,
                           const TcSymbolTable *global, TcInitHistory *hist,
                           size_t stmt_index, TcDiagnostic *diag, TcWarningList *warnings);

/**
 * memcopy_unsafe：拒绝 void 元素类型。
 * 负 length 与越界不在此检查（越界为实现定义；负 length 由执行器报
 * TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE）。
 */
int tc_memblock_check_memcopy_unsafe(const TcMemcopyUnsafeStmt *stmt,
                                     const TcSymbolTable *visible,
                                     const TcSymbolTable *global, TcInitHistory *hist,
                                     size_t stmt_index, TcDiagnostic *diag,
                                     TcWarningList *warnings);

/** 解析类型树中 memblock 的 usize_operand 名（Self.N / Qual.N / 标识符）为声明 N */
int tc_memblock_resolve_type_counts(TcType *type, const TcSymbolTable *visible,
                                    const TcSymbolTable *global, size_t stmt_index, int line,
                                    TcDiagnostic *diag);

/**
 * 解析 memblock 值构造器 `count:` 的 `usize_operand` 名称为元素个数。
 *
 * `#lib` 的 `static let` 在 Pass2 之前就完成编译期求值，此时构造器的
 * `count_name` 尚未解析；静态初始化器解析路径须用它提前固化，否则
 * `memblock(int32, count: Self.N, …)` 会被当成 `count == 0` 而误报。
 * 合法来源与诊断同 `N`（语言标准 §3.8.3、§4.2；附录 A `usize_operand`）。
 *
 * @return 成功 0 并写入 *out_count；失败 -1 并设置 diag
 */
int tc_memblock_resolve_count_name(const char *name, const TcSymbolTable *visible,
                                   const TcSymbolTable *global, size_t stmt_index, int line,
                                   TcDiagnostic *diag, uint64_t *out_count);

#endif /* TC_MEMBLOCK_CHECK_H */
