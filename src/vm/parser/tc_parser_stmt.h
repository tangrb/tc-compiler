/*
 * tc_parser_stmt.h — 语句级语法解析（tc_parser_stmt.c）
 *
 * 非公共 API；供 tc_parser.c 的语句分发调用。
 */
#ifndef TC_PARSER_STMT_H
#define TC_PARSER_STMT_H

#include "tc_parser.h"

/** funcall RHS（var/let 初始化调用；也在赋值等 RHS 位置使用） */
int tc_parse_funcall_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                         int line_no, TcRhs *out, TcDiagnostic *diag);

int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                           TcIoWrite *out, TcDiagnostic *diag);
int tc_parse_var_or_const_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                              int line_no, int is_const, TcStatement *out,
                              TcDiagnostic *diag);
int tc_parse_static_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                        int line_no, TcModuleMode mode, TcVisibility vis,
                        TcStatement *out, TcDiagnostic *diag);
int tc_parse_import_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                         TcStatement *out, TcDiagnostic *diag);
int tc_parse_return_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                         TcStatement *out, TcDiagnostic *diag);
int tc_parse_funcall_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                          int line_no, TcModuleMode mode, TcStatement *out,
                          TcDiagnostic *diag);
int tc_parse_field_assign_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                               int line_no, TcStatement *out, TcDiagnostic *diag);
int tc_parse_ptr_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                            TcStatement *out, TcDiagnostic *diag);
int tc_parse_memblock_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                 TcStatement *out, TcDiagnostic *diag);
int tc_parse_memblock_copy_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                TcStatement *out, TcDiagnostic *diag);
int tc_parse_memcopy_unsafe_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                 TcStatement *out, TcDiagnostic *diag);

#endif /* TC_PARSER_STMT_H */
