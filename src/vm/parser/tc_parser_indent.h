/*
 * tc_parser_indent.h — 缩进测量、skippable 行、块体解析
 *
 * 块体/缩进诊断声明收在 tc_parser_internal.h；本头与 tc_parser_indent.c 成对。
 */
#ifndef TC_PARSER_INDENT_H
#define TC_PARSER_INDENT_H

#include "tc_parser.h"
#include "tc_parser_internal.h"

int tc_measure_line_indent(const char *line, int line_no, TcDiagnostic *diag,
                           int *out_indent);
int tc_is_skippable_line(const char *line);
void tc_source_lines_free(TcSourceLine *lines, size_t count);

#endif /* TC_PARSER_INDENT_H */
