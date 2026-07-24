/*
 * tc_struct_check.h — 结构体定义表与构造/字段访问验证（Phase 3）
 *
 * 职责：
 *   1. 从程序 AST 注册 struct 定义 → TcStructTable（分配 struct_id、计算位宽）
 *   2. 将声明中的未解析 struct 名解析为 struct_id
 *   3. 校验结构体构造器、字段读、字段写（含 let 字段不可变）
 *
 * 嵌套 struct 字段必须引用「更早定义」的结构体（禁止自引用与前向引用）。
 */
#ifndef TC_STRUCT_CHECK_H
#define TC_STRUCT_CHECK_H

#include "tc_types.h"
#include "tc_diagnostic.h"
#include "tc_warning.h"
#include "tc_symbol.h"
#include "tc_analyzer_internal.h"

/** 一张已注册结构体条目（模块内唯一名 → struct_id） */
typedef struct {
    char *name;
    TcVisibility visibility;
    TcStructField *fields; /* 深拷贝自 AST；嵌套类型解析后写入 struct_id */
    size_t field_count;
    int struct_id;         /* 等于在 table.items[] 中的下标 */
    size_t width_bits;     /* 含各字段 @padding 的布局位宽 */
} TcStructEntry;

struct TcStructTable {
    TcStructEntry *items;
    size_t count;
    size_t capacity;
};

typedef struct TcStructTable TcStructTable;

/** 按 struct_id 取条目；越界返回 NULL */
const TcStructEntry *tc_struct_table_get(const TcStructTable *table, int struct_id);

/**
 * 计算字段链相对基结构体起始处的字节偏移与末字段类型。
 * @return 0 成功；-1 失败（已写 diag）
 */
int tc_struct_path_offset_bytes(const TcStructTable *table, int struct_id, char *const *fields,
                                size_t field_count, size_t *out_offset_bytes,
                                const TcType **out_field_type, TcDiagnostic *diag, int line);

void tc_struct_table_init(TcStructTable *table);
void tc_struct_table_free(TcStructTable *table);

/**
 * 扫描 program 中全部 STRUCT_DEF：查重、校验字段、入表，并解析其它声明上的 struct 名。
 * 成功时回写 AST 的 struct_id。
 */
int tc_struct_table_register_program(TcProgram *program, TcStructTable *table,
                                     TcDiagnostic *diag);

const TcStructEntry *tc_struct_table_find(const TcStructTable *table, const char *name);

/**
 * tc_sizeof_bits 回调：按 struct_id 查表返回 width_bits。
 * @param userdata 指向 TcStructTable
 */
size_t tc_struct_table_width_bits(int struct_id, void *userdata);

/** 结构体构造器：字段齐全、无未知/重复、声明顺序、各字段类型匹配 */
int tc_struct_check_constructor(const TcRhs *rhs, const TcType *expected,
                                const TcStructTable *table, const TcSymbolTable *visible,
                                const TcSymbolTable *global, TcInitHistory *hist,
                                size_t stmt_index, int line, TcDiagnostic *diag,
                                TcWarningList *warnings, const char *self_name);

/** 字段链读取 a.b.c：沿 struct 嵌套解析，最终类型须匹配 expected（若非 NULL） */
int tc_struct_check_field_read(const TcRhs *rhs, const TcType *expected,
                               const TcStructTable *table, const TcSymbolTable *visible,
                               const TcSymbolTable *global, TcInitHistory *hist,
                               size_t stmt_index, int line, TcDiagnostic *diag,
                               TcWarningList *warnings, const char *self_name);

/** 字段赋值：基对象非常量；路径上每个字段须为 var；RHS 类型匹配最末字段 */
int tc_struct_check_field_assign(const TcFieldAssign *assign, const TcStructTable *table,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                                 TcWarningList *warnings);

#endif /* TC_STRUCT_CHECK_H */
