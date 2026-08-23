/*
 * tc_parser_internal.h — parser 子模块共享辅助
 *
 * 非公共 API。供 tc_parser.c 与 type/struct/func/stmt/rhs/free 共用。
 */
#ifndef TC_PARSER_INTERNAL_H
#define TC_PARSER_INTERNAL_H

#include "tc_types.h"
#include "tc_lexer.h"
#include "tc_diagnostic.h"

/** 缩进块语句容器（parser 子模块共享） */
typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcStmtBlock;

int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message);
const TcToken *tc_peek(const TcTokenList *tokens, size_t index);
int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                     TcOperand *out, TcDiagnostic *diag);
int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                    int line_no, TcDiagnostic *diag);
int tc_expect_stmt_end(const TcTokenList *tokens, size_t *index, int line_no,
                       TcDiagnostic *diag);
int tc_token_is_type(const TcToken *tok);
int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message);
int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                          TcDiagnostic *diag, int line_no);
int tc_first_token_kind(const TcSourceLine *line);
int tc_end_line_check(const TcSourceLine *line, TcDiagnostic *diag);
int tc_module_diag(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                   const char *message);
void tc_stmt_block_init(TcStmtBlock *block);
void tc_stmt_block_free(TcStmtBlock *block);
int tc_parse_block_body_mode(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                             size_t *index, int base_indent,
                             const TcFileIndent *file_indent, TcModuleMode mode,
                             TcStmtBlock *block, TcDiagnostic *diag);
int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                        size_t *index, int base_indent,
                        const TcFileIndent *file_indent, TcStmtBlock *block,
                        TcDiagnostic *diag);
int tc_parse_visibility_prefix(const TcTokenList *tokens, size_t *index,
                               TcModuleMode mode, TcVisibility *out_vis,
                               int require_vis, TcDiagnostic *diag, int line_no);
void tc_string_list_free_local(char **items, size_t count);
int tc_operand_count_error(TcDiagnostic *diag, int line, int column,
                         const char *message);
int tc_parse_field_chain(const TcTokenList *tokens, size_t *index, int line_no,
                         char **out_base, char ***out_fields, size_t *out_field_count,
                         TcDiagnostic *diag);

/** 堆分配复制 Token 文本；失败设置 OOM 并返回 NULL */
char *tc_token_strdup(const TcToken *tok, int line_no, TcDiagnostic *diag);

/** 标识符 Token 是否与 name 完全匹配 */
int tc_token_is_ident_named(const TcToken *tok, const char *name);

/* tc_parse_type_syntax 权威声明见 tc_parser_type.h */
#include "tc_parser_type.h"

#endif /* TC_PARSER_INTERNAL_H */
