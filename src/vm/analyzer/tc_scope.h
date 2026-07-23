/*
 * tc_scope.h — 本库成员索引与 Self / 限定名查找骨架（阶段 6b 预留 / Phase 2 D-6/D-8）
 */
#ifndef TC_SCOPE_H
#define TC_SCOPE_H

#include "tc_types.h"
#include "tc_diagnostic.h"

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
    int stmt_index;
} TcMemberEntry;

typedef struct {
    TcMemberEntry *items;
    size_t count;
    size_t capacity;
} TcMemberIndex;

void tc_member_index_init(TcMemberIndex *index);
void tc_member_index_free(TcMemberIndex *index);

/** 收集 #lib 顶层 func / static let / static var / struct 声明名 */
int tc_member_index_build(const TcProgram *program, TcMemberIndex *out, TcDiagnostic *diag);

const TcMemberEntry *tc_member_index_find(const TcMemberIndex *index, const char *name);

/**
 * 校验 Self 仅出现在 #lib；#program 中 Self → PROGRAM_MODE_MISUSE。
 * 扫描程序 AST 中的 SELF_MEMBER / 限定目标。
 */
int tc_scope_check_self_usage(const TcProgram *program, TcDiagnostic *diag);

#endif /* TC_SCOPE_H */
