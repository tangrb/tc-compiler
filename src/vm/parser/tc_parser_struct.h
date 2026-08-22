/*
 * tc_parser_struct.h — struct 定义语法解析（tc_parser_struct.c）
 *
 * 非公共 API；供 tc_parser.c 的语句分发调用。
 */
#ifndef TC_PARSER_STRUCT_H
#define TC_PARSER_STRUCT_H

#include "tc_parser.h"

/** 解析 `struct Name then ... end` 定义（附录 A struct_definition） */
int tc_parse_struct_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                        size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                        TcStatement *out, TcDiagnostic *diag);

#endif /* TC_PARSER_STRUCT_H */
