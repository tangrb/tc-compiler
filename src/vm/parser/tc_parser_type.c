/*
 * tc_parser_type.c — 类型语法解析（tc_parse_type_syntax）
 *
 * 从 tc_parser.c 拆出：完整类型语法（标量 / void / ptr / memblock / struct 名）。
 */
#include "tc_parser_type.h"

#include "tc_parser_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tc_parse_type_depth(const TcTokenList *tokens, size_t *index, int line_no,
                               int allow_void, TcType *out_type, char **out_struct_name,
                               int depth, TcDiagnostic *diag);

int tc_parse_type_syntax(const TcTokenList *tokens, size_t *index, int line_no,
                         int allow_void, TcType *out_type, char **out_struct_name,
                         TcDiagnostic *diag) {
    return tc_parse_type_depth(tokens, index, line_no, allow_void, out_type, out_struct_name, 0,
                               diag);
}

static int tc_parse_type_depth(const TcTokenList *tokens, size_t *index, int line_no,
                               int allow_void, TcType *out_type, char **out_struct_name,
                               int depth, TcDiagnostic *diag) {
    const TcToken *tok = NULL;
    char *nested_struct = NULL;

    if (out_struct_name) {
        *out_struct_name = NULL;
    }
    memset(out_type, 0, sizeof(*out_type));

    tok = tc_peek(tokens, *index);
    if (tok->kind == TC_TOK_VOID) {
        if (!allow_void) {
            return tc_syntax_error(diag, line_no, tok->column, "void type not allowed here");
        }
        *out_type = tc_type_scalar(TC_VOID);
        (*index)++;
        return 0;
    }

    if (tc_token_is_type(tok)) {
        *out_type = tc_type_scalar(tok->u.int_type);
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_PTR) {
        TcType *pointee = NULL;

        /*
         * B-28：类型表达式的递归下降必须有深度上限——无上限时
         * `ptr<ptr<…int32…>>` 这类输入会耗尽栈并 SIGSEGV（附录 A 决定语法
         * 接受集，此类 Token 序列应给出 TC_CE_SYNTAX 而非崩溃）。
         */
        if (depth >= TC_PARSER_MAX_DEPTH) {
            return tc_syntax_error(diag, line_no, tok->column, "type nesting too deep");
        }
        pointee = (TcType *)malloc(sizeof(TcType));
        if (!pointee) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_LT, line_no, diag) != 0) {
            free(pointee);
            return -1;
        }
        if (tc_parse_type_depth(tokens, index, line_no, 0, pointee, &nested_struct, depth + 1,
                                diag) != 0) {
            free(nested_struct);
            tc_type_free(pointee);
            free(pointee);
            return -1;
        }
        free(nested_struct);
        if (tc_expect_token(tokens, index, TC_TOK_GT, line_no, diag) != 0) {
            tc_type_free(pointee);
            free(pointee);
            return -1;
        }
        *out_type = tc_type_make_ptr(pointee);
        return 0;
    }

    if (tok->kind == TC_TOK_MEMBLOCK) {
        TcType *element = NULL;
        uint64_t count = 0;
        char *count_name = NULL;

        if (depth >= TC_PARSER_MAX_DEPTH) {
            return tc_syntax_error(diag, line_no, tok->column, "type nesting too deep");
        }
        element = (TcType *)malloc(sizeof(TcType));
        if (!element) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_LT, line_no, diag) != 0) {
            free(element);
            return -1;
        }
        if (tc_parse_type_depth(tokens, index, line_no, 0, element, &nested_struct, depth + 1,
                                diag) != 0) {
            free(nested_struct);
            tc_type_free(element);
            free(element);
            return -1;
        }
        free(nested_struct);
        if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
            tc_type_free(element);
            free(element);
            return -1;
        }
        tok = tc_peek(tokens, *index);
        if (tok->kind == TC_TOK_INTEGER) {
            /*
             * §3.8.1：N 必须是编译期 usize 常量、取值为正整数（≥1）。词法器将 -5
             * 产为 negative=1 的单个 INTEGER token，不得静默取 magnitude；字面量 0
             * 同样拒绝（避免 intern 出 count=0 的占位类型泄漏到 AOT 分配）。
             *
             * B-40（§11「阶段优先」）：拒绝属 SEM 类，不能在语法阶段直接失败，否则
             * 更晚的语法错误会被更早的 SEM 诊断掩盖。此处挂起 SEM 诊断并继续解析，
             * 占位 count 取 1（合法且不会放大分配），由 SEM 阶段按源序位置发布。
             */
            if (tok->u.literal.negative || tok->u.literal.magnitude < 1) {
                if (tc_diagnostic_defer_sem(diag, TC_CE_CONSTANT_EXPRESSION, line_no,
                                            tok->column,
                                            "memblock count must be at least 1") != 0) {
                    tc_type_free(element);
                    free(element);
                    return -1;
                }
                count = 1;
            } else {
                count = tok->u.literal.magnitude;
            }
            (*index)++;
        } else if (tok->kind == TC_TOK_IDENTIFIER || tok->kind == TC_TOK_SELF) {
            if (tc_parse_binding_name(tokens, index, line_no, &count_name, diag) != 0) {
                tc_type_free(element);
                free(element);
                return -1;
            }
            count = 0;
        } else {
            tc_type_free(element);
            free(element);
            return tc_syntax_error(diag, line_no, tok->column, "expected memblock size");
        }
        if (tc_expect_token(tokens, index, TC_TOK_GT, line_no, diag) != 0) {
            free(count_name);
            tc_type_free(element);
            free(element);
            return -1;
        }
        *out_type = tc_type_make_memblock(element, count);
        out_type->params.memblock_type.pending_count_name = count_name;
        return 0;
    }

    if (tok->kind == TC_TOK_IDENTIFIER) {
        const TcToken *member_tok = NULL;

        if (!out_struct_name) {
            return tc_syntax_error(diag, line_no, tok->column, "expected type");
        }
        /*
         * struct_type = identifier | imported_member_name（语言标准附录 A）。
         * 导入的公开结构体以 <模块名>.<结构体名> 作为类型名。
         */
        if (*index + 2 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
            size_t total = 0;

            member_tok = tc_peek(tokens, *index + 2);
            if (member_tok->kind != TC_TOK_IDENTIFIER) {
                return tc_syntax_error(diag, line_no, member_tok->column, "expected struct name");
            }
            total = tok->length + 1 + member_tok->length + 1;
            *out_struct_name = (char *)malloc(total);
            if (!*out_struct_name) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                return -1;
            }
            snprintf(*out_struct_name, total, "%.*s.%.*s", (int)tok->length, tok->start,
                     (int)member_tok->length, member_tok->start);
            (*index) += 3;
        } else {
            *out_struct_name = tc_token_strdup(tok, line_no, diag);
            if (!*out_struct_name) {
                return -1;
            }
            (*index)++;
        }
        /*
         * §3.9.1：结构体名（含嵌套在 ptr<…>/memblock<…> 内的）以未决名
         * 暂存于 TcType.pending_name，由 Analyzer 在注册结构体表后按位置
         * 规则解析；此处不得丢弃（此前实现将嵌套名 free，导致指针所指
         * 位置与 memblock 元素位置的结构体身份永久丢失）。
         */
        *out_type = tc_type_make_struct(-1);
        out_type->pending_name = strdup(*out_struct_name);
        if (!out_type->pending_name) {
            free(*out_struct_name);
            *out_struct_name = NULL;
            return tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                     "memory allocation failed");
        }
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected type");
}
