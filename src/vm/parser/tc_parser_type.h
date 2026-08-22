/*
 * tc_parser_type.h — 类型语法解析（tc_parser_type.c）
 *
 * 非公共 API；供 parser 子模块（语句/RHS/字段/形参）调用。
 */
#ifndef TC_PARSER_TYPE_H
#define TC_PARSER_TYPE_H

#include "tc_parser.h"

/**
 * 解析完整类型语法（标量 / void / ptr / memblock / struct 名）。
 * @param allow_void  1 允许 void（函数返回类型）
 * @param out_type    输出 TcType（含堆分配嵌套 ptr/memblock）
 * @param out_struct_name 输出顶层结构体名（堆，调用方释放）；非 struct 为 NULL
 */
int tc_parse_type_syntax(const TcTokenList *tokens, size_t *index, int line_no,
                         int allow_void, TcType *out_type, char **out_struct_name,
                         TcDiagnostic *diag);

#endif /* TC_PARSER_TYPE_H */
