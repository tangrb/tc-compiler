/*
 * tc_parser_func.h — 函数定义语法解析（tc_parser_func.c）
 *
 * 非公共 API；供 tc_parser.c 的语句分发调用。
 */
#ifndef TC_PARSER_FUNC_H
#define TC_PARSER_FUNC_H

#include "tc_parser.h"

/** 解析 `(public|private) func Name(params) return_type then ... end` */
int tc_parse_func_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                      size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                      TcStatement *out, TcDiagnostic *diag);

#endif /* TC_PARSER_FUNC_H */
