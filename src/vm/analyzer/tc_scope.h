/*
 * tc_scope.h — 本库成员索引与 Self 使用检查
 *
 * 从 #lib AST 收集顶层成员名（func / static / struct），并拒绝
 * #program 中的 Self（PROGRAM_MODE_MISUSE）。
 * 限定名与 private 访问由 Pass2 / tc_func_check 在名称解析时执行。
 */
#ifndef TC_SCOPE_H
#define TC_SCOPE_H

#include "tc_types.h"
#include "tc_diagnostic.h"

/** 本库顶层成员种类（索引表条目） */
typedef enum {
    TC_MEMBER_FUNC,
    TC_MEMBER_STATIC_VAR,
    TC_MEMBER_STATIC_LET,
    TC_MEMBER_STRUCT
} TcMemberKind;

typedef struct {
    char *name;
    TcMemberKind kind;
    TcVisibility visibility;
    int stmt_index; /* 在 TcProgram.items[] 中的下标 */
} TcMemberEntry;

typedef struct {
    TcMemberEntry *items;
    size_t count;
    size_t capacity;
} TcMemberIndex;

void tc_member_index_init(TcMemberIndex *index);
void tc_member_index_free(TcMemberIndex *index);

/**
 * 收集 #lib（或任意程序）顶层 func / static let / static var / struct 声明名。
 * 线性扫描，不做重名冲突检测（由 Analyzer 其它阶段负责）。
 */
int tc_member_index_build(const TcProgram *program, TcMemberIndex *out, TcDiagnostic *diag);

/** 按名查找；未找到返回 NULL。线性扫描，适合小规模成员表。 */
const TcMemberEntry *tc_member_index_find(const TcMemberIndex *index, const char *name);

/**
 * 校验 Self 仅出现在 #lib。
 * 扫描 AST 中的 TC_RHS_SELF_MEMBER、funcall 的 is_self，以及嵌套 if/while/func 体。
 * #program 中出现 Self → TC_CE_PROGRAM_MODE_MISUSE。
 */
int tc_scope_check_self_usage(const TcProgram *program, TcDiagnostic *diag);

#endif /* TC_SCOPE_H */
