/*
 * tc_parser.c — TC 语法分析器实现
 *
 * 消费 tc_tokenize_line 产出的 TcTokenList，按 TC 语言语法规则
 * 将 Token 流解析为 TcStatement / TcProgram（AST）。
 *
 * Phase 2 起强制模块头：首行须为 #program 或 #lib；并支持
 * import / struct / func / static / 可见性 / Self 等模块语法。
 * 顶层声明按五层顺序校验（见 TcParseLayer）。
 */
#include "tc_parser.h"
#include "tc_parser_free.h"
#include "tc_parser_rhs.h"
#include "tc_parser_internal.h"

#include "tc_diagnostic.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* forward declarations — multi行 struct/func 解析依赖缩进块工具 */
typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcStmtBlock;

static int tc_first_token_kind(const TcSourceLine *line);
static int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message);
static int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                                 TcDiagnostic *diag, int line_no);
static void tc_stmt_block_init(TcStmtBlock *block);
static void tc_stmt_block_free(TcStmtBlock *block);
static int tc_parse_statement_mode(TcParserCtx *ctx, const TcTokenList *tokens, int line_no,
                                   TcModuleMode mode, TcStatement *out, TcDiagnostic *diag);
static int tc_parse_block_body_mode(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                                    size_t *index, int base_indent,
                                    const TcFileIndent *file_indent, TcModuleMode mode,
                                    TcStmtBlock *block, TcDiagnostic *diag);
static int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, int base_indent,
                               const TcFileIndent *file_indent, TcStmtBlock *block,
                               TcDiagnostic *diag);


/* ------------------------------------------------------------------ */
/*  便捷错误报告辅助函数                                                 */
/* ------------------------------------------------------------------ */

int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_SYNTAX, line, column, message);
    return -1;
}


static int tc_operand_count_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_OPERAND_COUNT, line, column, message);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  底层解析工具函数                                                     */
/* ------------------------------------------------------------------ */

/** 读取 Token 列表中的第 index 个 Token（不做越界检查） */
const TcToken *tc_peek(const TcTokenList *tokens, size_t index) {
    assert(tokens->count > 0);
    assert(index < tokens->count);
    return &tokens->items[index];
}

/*
 * @brief 解析一个操作数：变量引用或字面量
 * @param tokens  Token 列表
 * @param index   当前读取位置（解析成功后被推进）
 * @param line_no 当前行号
 * @param out     输出：解析结果 TcOperand
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1
 */
int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                            TcOperand *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_IDENTIFIER) {
        out->kind = TC_OPERAND_VAR;
        out->u.name = strndup(tok->start, tok->length);
        if (!out->u.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_BOOL_LIT) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_FLOAT_LIT) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_NULLPTR) {
        out->kind = TC_OPERAND_LIT;
        memset(&out->u.lit, 0, sizeof(out->u.lit));
        out->u.lit.is_nullptr = 1;
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected operand");
}

/** 断言当前位置的 Token 种类与期望的一致，然后推进 index */
int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                           int line_no, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    if (tok->kind != kind) {
        return tc_syntax_error(diag, line_no, tok->column, "unexpected token");
    }
    (*index)++;
    return 0;
}

/** 检查语句结尾：允许可选的分号后紧跟 EOF */
static int tc_expect_stmt_end(const TcTokenList *tokens, size_t *index, int line_no,
                              TcDiagnostic *diag) {
    const TcToken *tail = tc_peek(tokens, *index);
    if (tail->kind == TC_TOK_SEMICOLON) {
        (*index)++;
        tail = tc_peek(tokens, *index);
    }
    if (tail->kind != TC_TOK_EOF) {
        return tc_syntax_error(diag, line_no, tail->column, "unexpected trailing tokens");
    }
    return 0;
}

int tc_token_is_type(const TcToken *tok) {
    return tok->kind == TC_TOK_INT_TYPE || tok->kind == TC_TOK_FLOAT_TYPE;
}

char *tc_token_strdup(const TcToken *tok, int line_no, TcDiagnostic *diag) {
    char *copy = NULL;

    if (!tok) {
        return NULL;
    }
    copy = (char *)strndup(tok->start, tok->length);
    if (!copy) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
    }
    return copy;
}

int tc_token_is_ident_named(const TcToken *tok, const char *name) {
    size_t name_len = 0;

    if (!tok || !name || tok->kind != TC_TOK_IDENTIFIER) {
        return 0;
    }
    name_len = strlen(name);
    return tok->length == name_len && strncmp(tok->start, name, name_len) == 0;
}

static int tc_module_diag(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                          const char *message) {
    /* 模块语义错误：写入指定 TcErrorKind（非一律 SYNTAX） */
    tc_diagnostic_set(diag, kind, line, column, message);
    return -1;
}

int tc_parse_type_syntax(const TcTokenList *tokens, size_t *index, int line_no,
                         int allow_void, TcType *out_type, char **out_struct_name,
                         TcDiagnostic *diag) {
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
        TcType *pointee = (TcType *)malloc(sizeof(TcType));

        if (!pointee) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_LT, line_no, diag) != 0) {
            free(pointee);
            return -1;
        }
        if (tc_parse_type_syntax(tokens, index, line_no, 0, pointee, &nested_struct, diag) != 0) {
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
        TcType *element = (TcType *)malloc(sizeof(TcType));
        uint64_t count = 0;

        if (!element) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_LT, line_no, diag) != 0) {
            free(element);
            return -1;
        }
        if (tc_parse_type_syntax(tokens, index, line_no, 0, element, &nested_struct, diag) != 0) {
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
            count = tok->u.literal.magnitude;
            (*index)++;
        } else if (tok->kind == TC_TOK_IDENTIFIER) {
            count = 0;
            (*index)++;
        } else {
            tc_type_free(element);
            free(element);
            return tc_syntax_error(diag, line_no, tok->column, "expected memblock size");
        }
        if (tc_expect_token(tokens, index, TC_TOK_GT, line_no, diag) != 0) {
            tc_type_free(element);
            free(element);
            return -1;
        }
        *out_type = tc_type_make_memblock(element, count);
        return 0;
    }

    if (tok->kind == TC_TOK_IDENTIFIER) {
        if (!out_struct_name) {
            return tc_syntax_error(diag, line_no, tok->column, "expected type");
        }
        *out_struct_name = tc_token_strdup(tok, line_no, diag);
        if (!*out_struct_name) {
            return -1;
        }
        *out_type = tc_type_make_struct(-1);
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected type");
}

/*
 * 模块顶层声明分层（Parser 侧，与 tc_module 五层语义对齐）。
 * IMPORT → STRUCT → VALUE → FUNC → EXEC；数值越大越靠后。
 * #program 比较时将 EXEC 与 VALUE 归一（见 tc_check_layer）。
 */
typedef enum {
    TC_PARSE_LAYER_IMPORT = 1,
    TC_PARSE_LAYER_STRUCT = 2,
    TC_PARSE_LAYER_VALUE = 3,
    TC_PARSE_LAYER_FUNC = 4,
    TC_PARSE_LAYER_EXEC = 5
} TcParseLayer;

static int tc_parse_optional_padding(const TcTokenList *tokens, size_t *index, int line_no,
                                     uint64_t *out_padding, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_padding = 0;
    if (tok->kind != TC_TOK_AT) {
        return 0;
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_PADDING, line_no, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    tok = tc_peek(tokens, *index);
    if (tok->kind == TC_TOK_INTEGER) {
        *out_padding = tok->u.literal.magnitude;
        (*index)++;
    } else {
        return tc_syntax_error(diag, line_no, tok->column, "expected padding size");
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        return -1;
    }
    return 0;
}

static void tc_string_list_free_local(char **items, size_t count) {
    size_t i = 0;
    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static int tc_parse_field_chain(const TcTokenList *tokens, size_t *index, int line_no,
                                char **out_base, char ***out_fields, size_t *out_field_count,
                                TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    char **fields = NULL;
    size_t field_count = 0;
    size_t field_cap = 0;

    *out_base = NULL;
    *out_fields = NULL;
    *out_field_count = 0;

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }
    *out_base = tc_token_strdup(tok, line_no, diag);
    if (!*out_base) {
        return -1;
    }
    (*index)++;

    while (tc_peek(tokens, *index)->kind == TC_TOK_DOT) {
        char *field_name = NULL;

        (*index)++;
        tok = tc_peek(tokens, *index);
        if (tok->kind != TC_TOK_IDENTIFIER) {
            free(*out_base);
            *out_base = NULL;
            tc_string_list_free_local(fields, field_count);
            return tc_syntax_error(diag, line_no, tok->column, "expected field name");
        }
        field_name = tc_token_strdup(tok, line_no, diag);
        if (!field_name) {
            free(*out_base);
            *out_base = NULL;
            tc_string_list_free_local(fields, field_count);
            return -1;
        }
        if (field_count == field_cap) {
            size_t new_cap = field_cap == 0 ? 4 : field_cap * 2;
            char **new_fields = (char **)realloc(fields, new_cap * sizeof(char *));

            if (!new_fields) {
                free(field_name);
                free(*out_base);
                *out_base = NULL;
                tc_string_list_free_local(fields, field_count);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
                return -1;
            }
            fields = new_fields;
            field_cap = new_cap;
        }
        fields[field_count++] = field_name;
        (*index)++;
    }

    *out_fields = fields;
    *out_field_count = field_count;
    return 0;
}

/**
 * 解析可选的 public/private 前缀。
 * #program 禁止可见性；#lib 在 require_vis=1 时缺失则报 MISSING_VISIBILITY。
 */
static int tc_parse_visibility_prefix(const TcTokenList *tokens, size_t *index,
                                      TcModuleMode mode, TcVisibility *out_vis,
                                      int require_vis, TcDiagnostic *diag, int line_no) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_vis = TC_VIS_NONE;
    if (tok->kind == TC_TOK_PUBLIC) {
        if (mode == TC_MODULE_PROGRAM) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "public is not allowed in #program mode");
        }
        *out_vis = TC_VIS_PUBLIC;
        (*index)++;
        return 0;
    }
    if (tok->kind == TC_TOK_PRIVATE) {
        if (mode == TC_MODULE_PROGRAM) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "private is not allowed in #program mode");
        }
        *out_vis = TC_VIS_PRIVATE;
        (*index)++;
        return 0;
    }
    if (require_vis && mode == TC_MODULE_LIB) {
        return tc_module_diag(diag, TC_CE_MISSING_VISIBILITY, line_no, tok->column,
                              "missing public or private visibility");
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/*  语句解析：write / writeln / read / var / let / 赋值                  */
/* ------------------------------------------------------------------ */

/*
 * @brief 解析 write/writeln 语句的括号内参数
 *
 * 语法：write/writeln(type [, fmt,] operand)
 *   - type 必选
 *   - fmt 可选（%d/%i/%u/%x/%X/%o/%b/%t/%f/%e/%E/%g/%G）
 *   - operand 必选（变量或字面量）
 *   额外操作数报 TC_CE_OPERAND_COUNT
 */
static int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcIoWrite *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;
    memset(&out->fmt, 0, sizeof(out->fmt));
    out->fmt.spec = TC_FMT_NONE;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (!tc_token_is_type(type_tok)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    /* 可选的格式说明符（位于第二参数位置，后跟逗号和操作数） */
    {
        const TcToken *next = tc_peek(tokens, *index);
        if (next->kind == TC_TOK_FORMAT_SPEC) {
            out->fmt = next->u.format_spec;
            (*index)++;
            next = tc_peek(tokens, *index);
            if (next->kind == TC_TOK_RPAREN) {
                return tc_operand_count_error(diag, line_no, next->column, "operand count error");
            }
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
            next = tc_peek(tokens, *index);
            if (next->kind == TC_TOK_RPAREN) {
                return tc_operand_count_error(diag, line_no, next->column, "operand count error");
            }
        }
    }

    memset(&out->operand, 0, sizeof(out->operand));
    if (tc_parse_operand(tokens, index, line_no, &out->operand, diag) != 0) {
        return -1;
    }

    /* 检查是否有多余的操作数 */
    {
        const TcToken *tail = tc_peek(tokens, *index);
        if (tail->kind == TC_TOK_COMMA) {
            return tc_operand_count_error(diag, line_no, tail->column, "operand count error");
        }
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_operand_free(&out->operand);
        return -1;
    }
    return 0;
}

/* 解析 read(type, id) 语句 */
static int tc_parse_read_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRead *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;
    out->name = NULL;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (!tc_token_is_type(type_tok)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        out->name = strndup(name_tok->start, name_tok->length);
        if (!out->name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        free(out->name);
        out->name = NULL;
        return -1;
    }
    return 0;
}

static int tc_parse_funcall_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                int line_no, TcRhs *out, TcDiagnostic *diag);

/*
 * @brief 解析 var 或 let 定义
 * @param is_const 1 表示 let 常量，0 表示 var 变量
 *
 * 语法：
 *   var id: type = rhs
 *   let id: type = rhs（必须初始化）
 */
static int tc_parse_var_or_const_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                     int line_no, int is_const, TcStatement *out,
                                     TcDiagnostic *diag) {
    char *name = NULL;
    char *struct_name = NULL;
    TcType full_type;
    TcRhs rhs;

    (*index)++;

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        name = tc_token_strdup(name_tok, line_no, diag);
        if (!name) {
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        free(name);
        return -1;
    }

    if (tc_parse_type_syntax(tokens, index, line_no, 0, &full_type, &struct_name, diag) != 0) {
        free(name);
        return -1;
    }

    {
        const TcToken *maybe_eq = tc_peek(tokens, *index);
        if (maybe_eq->kind == TC_TOK_EQUAL) {
            (*index)++;
            if (tc_peek(tokens, *index)->kind == TC_TOK_EOF ||
                tc_peek(tokens, *index)->kind == TC_TOK_SEMICOLON) {
                free(name);
                free(struct_name);
                tc_type_free(&full_type);
                if (!is_const) {
                    tc_diagnostic_set(diag, TC_CE_VAR_MISSING_INIT, line_no, maybe_eq->column,
                                      "variable definition requires initializer");
                    return -1;
                }
                return tc_syntax_error(diag, line_no, maybe_eq->column,
                                       "constant definition requires initializer");
            }
            memset(&rhs, 0, sizeof(rhs));
            if (is_const) {
                if (tc_parse_const_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                    free(name);
                    free(struct_name);
                    tc_type_free(&full_type);
                    tc_rhs_free(&rhs);
                    return -1;
                }
            } else if (tc_peek(tokens, *index)->kind == TC_TOK_FUNCALL) {
                if (tc_parse_funcall_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                    free(name);
                    free(struct_name);
                    tc_type_free(&full_type);
                    tc_rhs_free(&rhs);
                    return -1;
                }
            } else if (tc_parse_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                free(name);
                free(struct_name);
                tc_type_free(&full_type);
                tc_rhs_free(&rhs);
                return -1;
            }
        } else {
            free(name);
            free(struct_name);
            tc_type_free(&full_type);
            if (is_const) {
                return tc_syntax_error(diag, line_no, maybe_eq->column,
                                       "constant definition requires initializer");
            }
            tc_diagnostic_set(diag, TC_CE_VAR_MISSING_INIT, line_no, maybe_eq->column,
                              "variable definition requires initializer");
            return -1;
        }
    }

    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        tc_rhs_free(&rhs);
        return -1;
    }

    if (is_const) {
        out->kind = TC_STMT_CONST_DEF;
        out->u.const_def.line = line_no;
        out->u.const_def.name = name;
        out->u.const_def.type = full_type.kind;
        out->u.const_def.full_type = full_type;
        out->u.const_def.struct_type_name = struct_name;
        out->u.const_def.rhs = rhs;
    } else {
        out->kind = TC_STMT_VAR_DEF;
        out->u.var_def.line = line_no;
        out->u.var_def.name = name;
        out->u.var_def.type = full_type.kind;
        out->u.var_def.full_type = full_type;
        out->u.var_def.struct_type_name = struct_name;
        out->u.var_def.rhs = rhs;
    }
    return 0;
}

/**
 * 解析 #lib 内 static var / static let。
 * #program 中出现 static → PROGRAM_MODE_MISUSE；缺可见性 → MISSING_VISIBILITY。
 */
static int tc_parse_static_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                               int line_no, TcModuleMode mode, TcVisibility vis,
                               TcStatement *out, TcDiagnostic *diag) {
    int is_const = 0;
    char *name = NULL;
    char *struct_name = NULL;
    TcType full_type;
    TcRhs rhs;

    if (mode == TC_MODULE_PROGRAM) {
        const TcToken *tok = tc_peek(tokens, *index);
        return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                              "static is not allowed in #program mode");
    }
    if (mode == TC_MODULE_LIB && vis == TC_VIS_NONE) {
        const TcToken *tok = tc_peek(tokens, *index);
        return tc_module_diag(diag, TC_CE_MISSING_VISIBILITY, line_no, tok->column,
                              "missing public or private visibility");
    }

    (*index)++; /* static */
    {
        const TcToken *kind_tok = tc_peek(tokens, *index);
        if (kind_tok->kind == TC_TOK_VAR) {
            is_const = 0;
        } else if (kind_tok->kind == TC_TOK_LET) {
            is_const = 1;
        } else {
            return tc_syntax_error(diag, line_no, kind_tok->column, "expected var or let after static");
        }
        (*index)++;
    }

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        name = tc_token_strdup(name_tok, line_no, diag);
        if (!name) {
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        free(name);
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &full_type, &struct_name, diag) != 0) {
        free(name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_EQUAL, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        return -1;
    }
    memset(&rhs, 0, sizeof(rhs));
    if (is_const) {
        if (tc_parse_const_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
            free(name);
            free(struct_name);
            tc_type_free(&full_type);
            return -1;
        }
    } else if (tc_parse_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        tc_rhs_free(&rhs);
        return -1;
    }

    if (is_const) {
        out->kind = TC_STMT_STATIC_LET_DEF;
        out->u.static_let_def.line = line_no;
        out->u.static_let_def.visibility = vis;
        out->u.static_let_def.name = name;
        out->u.static_let_def.type = full_type;
        out->u.static_let_def.struct_type_name = struct_name;
        out->u.static_let_def.rhs = rhs;
    } else {
        out->kind = TC_STMT_STATIC_VAR_DEF;
        out->u.static_var_def.line = line_no;
        out->u.static_var_def.visibility = vis;
        out->u.static_var_def.name = name;
        out->u.static_var_def.type = full_type;
        out->u.static_var_def.struct_type_name = struct_name;
        out->u.static_var_def.rhs = rhs;
        out->u.static_var_def.static_slot = -1;
    }
    return 0;
}

/** 解析 `import Name;` —— 目标须为标识符（模块文件名不含 .tc）。 */
static int tc_parse_import_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                TcStatement *out, TcDiagnostic *diag) {
    const TcToken *name_tok = NULL;

    (*index)++; /* import */
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, name_tok->column, "expected module name");
    }
    out->kind = TC_STMT_IMPORT;
    out->u.import_stmt.line = line_no;
    out->u.import_stmt.module_name = tc_token_strdup(name_tok, line_no, diag);
    if (!out->u.import_stmt.module_name) {
        return -1;
    }
    (*index)++;
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(out->u.import_stmt.module_name);
        out->u.import_stmt.module_name = NULL;
        return -1;
    }
    return 0;
}

static int tc_parse_return_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                TcStatement *out, TcDiagnostic *diag) {
    TcReturnStmt ret;

    (*index)++; /* return */
    memset(&ret, 0, sizeof(ret));
    ret.line = line_no;
    {
        const TcToken *tail = tc_peek(tokens, *index);
        if (tail->kind != TC_TOK_EOF && tail->kind != TC_TOK_SEMICOLON) {
            ret.has_value = 1;
            if (tc_parse_operand(tokens, index, line_no, &ret.value, diag) != 0) {
                return -1;
            }
        }
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        if (ret.has_value) {
            tc_operand_free(&ret.value);
        }
        return -1;
    }
    out->kind = TC_STMT_RETURN;
    out->u.return_stmt = ret;
    return 0;
}

/**
 * 解析 funcall 调用目标：Self.member / Qual.member / 裸名。
 * 写入 is_self、qualifier、member_name、target（规范化文本）。
 */
static int tc_parse_funcall_target(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcFuncallStmt *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    memset(out, 0, sizeof(*out));
    out->line = line_no;
    out->resolved_func_id = -1;

    if (tok->kind == TC_TOK_SELF) {
        const TcToken *member_tok = NULL;

        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        (*index) += 2;
        member_tok = tc_peek(tokens, *index);
        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        out->is_self = 1;
        out->qualifier = strdup("Self");
        out->member_name = tc_token_strdup(member_tok, line_no, diag);
        if (!out->qualifier || !out->member_name) {
            free(out->qualifier);
            free(out->member_name);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                              "memory allocation failed");
            return -1;
        }
        {
            size_t target_len = 4 + member_tok->length + 1;
            out->target = (char *)malloc(target_len);
            if (!out->target) {
                free(out->qualifier);
                free(out->member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                                  "memory allocation failed");
                return -1;
            }
            snprintf(out->target, target_len, "Self.%.*s", (int)member_tok->length, member_tok->start);
        }
        (*index)++;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected funcall target");
    }

    if (tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
        const TcToken *qual_tok = tok;
        const TcToken *member_tok = tc_peek(tokens, *index + 2);

        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        out->qualifier = tc_token_strdup(qual_tok, line_no, diag);
        out->member_name = tc_token_strdup(member_tok, line_no, diag);
        if (!out->qualifier || !out->member_name) {
            free(out->qualifier);
            free(out->member_name);
            return -1;
        }
        {
            size_t target_len = qual_tok->length + 1 + member_tok->length + 1;
            out->target = (char *)malloc(target_len);
            if (!out->target) {
                free(out->qualifier);
                free(out->member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                return -1;
            }
            snprintf(out->target, target_len, "%.*s.%.*s", (int)qual_tok->length, qual_tok->start,
                     (int)member_tok->length, member_tok->start);
        }
        (*index) += 3;
        return 0;
    }

    out->target = tc_token_strdup(tok, line_no, diag);
    if (!out->target) {
        return -1;
    }
    (*index)++;
    return 0;
}

static void tc_funcall_stmt_free_partial(TcFuncallStmt *stmt) {
    free(stmt->target);
    free(stmt->qualifier);
    free(stmt->member_name);
    stmt->target = NULL;
    stmt->qualifier = NULL;
    stmt->member_name = NULL;
}

static int tc_parse_funcall_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                 int line_no, TcModuleMode mode, TcStatement *out,
                                 TcDiagnostic *diag) {
    TcFuncallStmt call;
    TcNamedArg *args = NULL;
    size_t arg_count = 0;
    size_t arg_cap = 0;

    (void)mode;
    (*index)++; /* funcall */
    memset(&call, 0, sizeof(call));
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_funcall_target(tokens, index, line_no, &call, diag) != 0) {
        return -1;
    }
    /* 允许零实参：funcall(Self.f)；有实参时 target 后须有逗号 */
    if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
        (*index)++;
        while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
            const TcToken *param_tok = tc_peek(tokens, *index);
            TcNamedArg arg;

            memset(&arg, 0, sizeof(arg));
            if (param_tok->kind != TC_TOK_IDENTIFIER) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, param_tok->column, "expected parameter name");
            }
            arg.param_name = tc_token_strdup(param_tok, line_no, diag);
            if (!arg.param_name) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            if (tc_parse_rhs(ctx, tokens, index, line_no, &arg.value, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            if (arg_count == arg_cap) {
                size_t new_cap = arg_cap == 0 ? 4 : arg_cap * 2;
                TcNamedArg *new_args = (TcNamedArg *)realloc(args, new_cap * sizeof(TcNamedArg));
                if (!new_args) {
                    free(arg.param_name);
                    tc_rhs_free(&arg.value);
                    tc_funcall_stmt_free_partial(&call);
                    for (size_t j = 0; j < arg_count; j++) {
                        free(args[j].param_name);
                        tc_rhs_free(&args[j].value);
                    }
                    free(args);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, param_tok->column,
                                      "memory allocation failed");
                    return -1;
                }
                args = new_args;
                arg_cap = new_cap;
            }
            args[arg_count++] = arg;

            if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
                (*index)++;
            } else if (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, tc_peek(tokens, *index)->column,
                                       "expected , or )");
            }
        }
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        size_t i = 0;
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        size_t i = 0;
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }

    call.args = args;
    call.arg_count = arg_count;
    out->kind = TC_STMT_FUNCALL;
    out->u.funcall_stmt = call;
    return 0;
}

/**
 * 解析 `funcall(...)` 为 RHS（用于 var/赋值右侧；不含语句结尾检查）。
 */
static int tc_parse_funcall_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                int line_no, TcRhs *out, TcDiagnostic *diag) {
    TcFuncallStmt call;
    TcNamedArg *args = NULL;
    size_t arg_count = 0;
    size_t arg_cap = 0;
    size_t i = 0;

    (*index)++; /* funcall */
    memset(&call, 0, sizeof(call));
    memset(out, 0, sizeof(*out));
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_funcall_target(tokens, index, line_no, &call, diag) != 0) {
        return -1;
    }
    if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
        (*index)++;
        while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
            const TcToken *param_tok = tc_peek(tokens, *index);
            TcNamedArg arg;

            memset(&arg, 0, sizeof(arg));
            if (param_tok->kind != TC_TOK_IDENTIFIER) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, param_tok->column, "expected parameter name");
            }
            arg.param_name = tc_token_strdup(param_tok, line_no, diag);
            if (!arg.param_name) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            if (tc_parse_rhs(ctx, tokens, index, line_no, &arg.value, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            if (arg_count == arg_cap) {
                size_t new_cap = arg_cap == 0 ? 4 : arg_cap * 2;
                TcNamedArg *new_args = (TcNamedArg *)realloc(args, new_cap * sizeof(TcNamedArg));
                if (!new_args) {
                    free(arg.param_name);
                    tc_rhs_free(&arg.value);
                    tc_funcall_stmt_free_partial(&call);
                    for (i = 0; i < arg_count; i++) {
                        free(args[i].param_name);
                        tc_rhs_free(&args[i].value);
                    }
                    free(args);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, param_tok->column,
                                      "memory allocation failed");
                    return -1;
                }
                args = new_args;
                arg_cap = new_cap;
            }
            args[arg_count++] = arg;
            if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
                (*index)++;
            } else if (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, tc_peek(tokens, *index)->column,
                                       "expected , or )");
            }
        }
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }

    out->kind = TC_RHS_FUNCALL_EXPR;
    out->u.funcall_expr.target = call.target;
    out->u.funcall_expr.is_self = call.is_self;
    out->u.funcall_expr.qualifier = call.qualifier;
    out->u.funcall_expr.member_name = call.member_name;
    out->u.funcall_expr.resolved_func_id = -1;
    out->u.funcall_expr.arg_count = arg_count;
    if (arg_count > 0) {
        out->u.funcall_expr.args =
            calloc(arg_count, sizeof(*out->u.funcall_expr.args));
        if (!out->u.funcall_expr.args) {
            for (i = 0; i < arg_count; i++) {
                free(args[i].param_name);
                tc_rhs_free(&args[i].value);
            }
            free(args);
            tc_funcall_stmt_free_partial(&call);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < arg_count; i++) {
            TcRhs *value_copy = (TcRhs *)malloc(sizeof(TcRhs));

            out->u.funcall_expr.args[i].param_name = args[i].param_name;
            if (!value_copy) {
                size_t k = 0;
                for (k = 0; k < i; k++) {
                    free(out->u.funcall_expr.args[k].param_name);
                    tc_rhs_free((TcRhs *)out->u.funcall_expr.args[k].value);
                    free(out->u.funcall_expr.args[k].value);
                }
                free(out->u.funcall_expr.args);
                for (k = i; k < arg_count; k++) {
                    free(args[k].param_name);
                    tc_rhs_free(&args[k].value);
                }
                free(args);
                free(call.target);
                free(call.qualifier);
                free(call.member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            *value_copy = args[i].value;
            out->u.funcall_expr.args[i].value = (struct TcRhs *)value_copy;
        }
        free(args);
    } else {
        out->u.funcall_expr.args = NULL;
    }
    return 0;
}

static int tc_parse_field_assign_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                      int line_no, TcStatement *out, TcDiagnostic *diag) {
    TcFieldAssign fa;
    char *base = NULL;
    char **fields = NULL;
    size_t field_count = 0;

    memset(&fa, 0, sizeof(fa));
    fa.line = line_no;
    if (tc_parse_field_chain(tokens, index, line_no, &base, &fields, &field_count, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_EQUAL, line_no, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return -1;
    }
    if (tc_parse_rhs(ctx, tokens, index, line_no, &fa.rhs, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        tc_rhs_free(&fa.rhs);
        return -1;
    }
    fa.base = base;
    fa.fields = fields;
    fa.field_count = field_count;
    out->kind = TC_STMT_FIELD_ASSIGN;
    out->u.field_assign = fa;
    return 0;
}

static int tc_parse_ptr_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcStatement *out, TcDiagnostic *diag) {
    TcPtrStoreStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* ptr_store identifier token consumed by caller */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.pointee_type, &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.ptr, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.value, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        tc_operand_free(&stmt.value);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        tc_operand_free(&stmt.value);
        return -1;
    }
    out->kind = TC_STMT_PTR_STORE;
    out->u.ptr_store = stmt;
    return 0;
}

static int tc_parse_memblock_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                        TcStatement *out, TcDiagnostic *diag) {
    TcMemblockStoreStmt stmt;
    const TcToken *name_tok = NULL;
    char *struct_name = NULL;

    (*index)++; /* memblock_store */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        tc_type_free(&stmt.element_type);
        return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
    }
    stmt.memblock_name = tc_token_strdup(name_tok, line_no, diag);
    if (!stmt.memblock_name) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.value, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        tc_operand_free(&stmt.value);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        tc_operand_free(&stmt.value);
        return -1;
    }
    out->kind = TC_STMT_MEMBLOCK_STORE;
    out->u.memblock_store = stmt;
    return 0;
}

static int tc_parse_memblock_copy_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                       TcStatement *out, TcDiagnostic *diag) {
    TcMemblockCopyStmt stmt;
    const TcToken *name_tok = NULL;
    char *struct_name = NULL;

    (*index)++; /* memblock_copy */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        tc_type_free(&stmt.element_type);
        return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
    }
    stmt.dst_name = tc_token_strdup(name_tok, line_no, diag);
    if (!stmt.dst_name) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        tc_operand_free(&stmt.dst_index);
        return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
    }
    stmt.src_name = tc_token_strdup(name_tok, line_no, diag);
    if (!stmt.src_name) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.length, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    out->kind = TC_STMT_MEMBLOCK_COPY;
    out->u.memblock_copy = stmt;
    return 0;
}

static int tc_parse_memcopy_unsafe_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                        TcStatement *out, TcDiagnostic *diag) {
    TcMemcopyUnsafeStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* memcopy_unsafe */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_ptr, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_ptr, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.length, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    out->kind = TC_STMT_MEMCOPY_UNSAFE;
    out->u.memcopy_unsafe = stmt;
    return 0;
}

static int tc_struct_field_push(TcStructField **fields, size_t *count, size_t *cap,
                                const TcStructField *field, TcDiagnostic *diag, int line_no) {
    if (*count == *cap) {
        size_t new_cap = *cap == 0 ? 4 : *cap * 2;
        TcStructField *new_fields = (TcStructField *)realloc(*fields, new_cap * sizeof(TcStructField));
        if (!new_fields) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        *fields = new_fields;
        *cap = new_cap;
    }
    (*fields)[*count] = *field;
    (*count)++;
    return 0;
}

static void tc_struct_def_fail(TcStructDef *def) {
    size_t i = 0;
    free(def->name);
    def->name = NULL;
    for (i = 0; i < def->field_count; i++) {
        free(def->fields[i].name);
        free(def->fields[i].struct_type_name);
        tc_type_free(&def->fields[i].type);
    }
    free(def->fields);
    def->fields = NULL;
    def->field_count = 0;
}

static int tc_parse_struct_field_line(TcParserCtx *ctx, const TcSourceLine *line,
                                      TcStructField *out, TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *tok = tc_peek(&line->tokens, index);
    char *struct_name = NULL;
    TcStructField field;

    memset(&field, 0, sizeof(field));
    if (tok->kind == TC_TOK_VAR) {
        field.is_var = 1;
    } else if (tok->kind == TC_TOK_LET) {
        field.is_var = 0;
    } else {
        return tc_syntax_error(diag, line->line_no, tok->column, "expected var or let field");
    }
    index++;
    tok = tc_peek(&line->tokens, index);
    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line->line_no, tok->column, "expected field name");
    }
    field.name = tc_token_strdup(tok, line->line_no, diag);
    if (!field.name) {
        return -1;
    }
    index++;
    if (tc_expect_token(&line->tokens, &index, TC_TOK_COLON, line->line_no, diag) != 0) {
        free(field.name);
        return -1;
    }
    if (tc_parse_type_syntax(&line->tokens, &index, line->line_no, 0, &field.type, &struct_name,
                             diag) != 0) {
        free(field.name);
        return -1;
    }
    field.struct_type_name = struct_name;
    if (tc_parse_optional_padding(&line->tokens, &index, line->line_no, &field.padding, diag) != 0) {
        free(field.name);
        free(field.struct_type_name);
        tc_type_free(&field.type);
        return -1;
    }
    if (tc_expect_stmt_end(&line->tokens, &index, line->line_no, diag) != 0) {
        free(field.name);
        free(field.struct_type_name);
        tc_type_free(&field.type);
        return -1;
    }
    (void)ctx;
    *out = field;
    return 0;
}

static int tc_parse_struct_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                               TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *struct_line = &lines[*index];
    size_t tok_index = 0;
    int base_indent = struct_line->indent;
    TcStructDef def;
    TcVisibility vis = TC_VIS_NONE;
    size_t field_cap = 0;

    memset(&def, 0, sizeof(def));
    def.line = struct_line->line_no;
    def.struct_id = -1;

    if (tc_parse_visibility_prefix(&struct_line->tokens, &tok_index, mode, &vis,
                                   mode == TC_MODULE_LIB, diag, struct_line->line_no) != 0) {
        return -1;
    }
    def.visibility = vis;
    if (tc_expect_token(&struct_line->tokens, &tok_index, TC_TOK_STRUCT, struct_line->line_no,
                        diag) != 0) {
        return -1;
    }
    {
        const TcToken *name_tok = tc_peek(&struct_line->tokens, tok_index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, struct_line->line_no, name_tok->column,
                                   "expected struct name");
        }
        def.name = tc_token_strdup(name_tok, struct_line->line_no, diag);
        if (!def.name) {
            return -1;
        }
        tok_index++;
    }
    if (tc_expect_token(&struct_line->tokens, &tok_index, TC_TOK_THEN, struct_line->line_no,
                        diag) != 0) {
        tc_struct_def_fail(&def);
        return -1;
    }
    if (tc_expect_stmt_end(&struct_line->tokens, &tok_index, struct_line->line_no, diag) != 0) {
        tc_struct_def_fail(&def);
        return -1;
    }

    (*index)++;
    while (*index < line_count && lines[*index].indent > base_indent) {
        TcStructField field;
        TcSourceLine *field_line = &lines[*index];

        if (tc_block_indent_valid(file_indent, base_indent, field_line->indent, diag,
                                  field_line->line_no) != 0) {
            tc_struct_def_fail(&def);
            return -1;
        }
        memset(&field, 0, sizeof(field));
        if (tc_parse_struct_field_line(ctx, field_line, &field, diag) != 0) {
            tc_struct_def_fail(&def);
            return -1;
        }
        if (tc_struct_field_push(&def.fields, &def.field_count, &field_cap,
                                 &field, diag, field_line->line_no) != 0) {
            free(field.name);
            free(field.struct_type_name);
            tc_type_free(&field.type);
            tc_struct_def_fail(&def);
            return -1;
        }
        (*index)++;
    }

    if (*index >= line_count || tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_struct_def_fail(&def);
        return tc_indent_diag(diag, TC_CE_MISSING_END, struct_line->line_no,
                              "missing end for struct definition");
    }
    if (lines[*index].indent != base_indent) {
        tc_struct_def_fail(&def);
        return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                              "end indentation does not match struct");
    }
    (*index)++;

    out->kind = TC_STMT_STRUCT_DEF;
    out->u.struct_def = def;
    return 0;
}

static int tc_func_param_push(TcFuncParam **params, size_t *count, size_t *cap,
                              const TcFuncParam *param, TcDiagnostic *diag, int line_no) {
    if (*count == *cap) {
        size_t new_cap = *cap == 0 ? 4 : *cap * 2;
        TcFuncParam *new_params = (TcFuncParam *)realloc(*params, new_cap * sizeof(TcFuncParam));
        if (!new_params) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        *params = new_params;
        *cap = new_cap;
    }
    (*params)[*count] = *param;
    (*count)++;
    return 0;
}

static void tc_func_def_fail(TcFuncDef *def) {
    size_t i = 0;
    free(def->name);
    free(def->return_struct_name);
    def->name = NULL;
    def->return_struct_name = NULL;
    tc_type_free(&def->return_type);
    for (i = 0; i < def->param_count; i++) {
        free(def->params[i].name);
        free(def->params[i].struct_type_name);
        tc_type_free(&def->params[i].type);
    }
    free(def->params);
    def->params = NULL;
    for (i = 0; i < def->body_count; i++) {
        tc_statement_free(&def->body[i]);
    }
    free(def->body);
    def->body = NULL;
}

static int tc_parse_func_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                             size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                             TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *func_line = &lines[*index];
    size_t tok_index = 0;
    int base_indent = func_line->indent;
    TcFuncDef def;
    TcVisibility vis = TC_VIS_NONE;
    size_t param_cap = 0;
    TcStmtBlock body;

    if (mode == TC_MODULE_PROGRAM) {
        const TcToken *tok = tc_peek(&func_line->tokens, tok_index);
        return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, func_line->line_no, tok->column,
                              "func is not allowed in #program mode");
    }

    memset(&def, 0, sizeof(def));
    def.line = func_line->line_no;
    def.func_id = -1;
    tc_stmt_block_init(&body);

    if (tc_parse_visibility_prefix(&func_line->tokens, &tok_index, mode, &vis, 1, diag,
                                   func_line->line_no) != 0) {
        return -1;
    }
    def.visibility = vis;
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_FUNC, func_line->line_no, diag) != 0) {
        return -1;
    }
    {
        const TcToken *name_tok = tc_peek(&func_line->tokens, tok_index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, func_line->line_no, name_tok->column, "expected function name");
        }
        def.name = tc_token_strdup(name_tok, func_line->line_no, diag);
        if (!def.name) {
            return -1;
        }
        tok_index++;
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_LPAREN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    while (tc_peek(&func_line->tokens, tok_index)->kind != TC_TOK_RPAREN) {
        TcFuncParam param;
        const TcToken *param_tok = tc_peek(&func_line->tokens, tok_index);

        memset(&param, 0, sizeof(param));
        if (param_tok->kind != TC_TOK_IDENTIFIER) {
            tc_func_def_fail(&def);
            return tc_syntax_error(diag, func_line->line_no, param_tok->column, "expected parameter name");
        }
        param.name = tc_token_strdup(param_tok, func_line->line_no, diag);
        if (!param.name) {
            tc_func_def_fail(&def);
            return -1;
        }
        tok_index++;
        if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_COLON, func_line->line_no, diag) != 0) {
            free(param.name);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_parse_type_syntax(&func_line->tokens, &tok_index, func_line->line_no, 0, &param.type,
                                 &param.struct_type_name, diag) != 0) {
            free(param.name);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_func_param_push(&def.params, &def.param_count, &param_cap, &param, diag,
                               func_line->line_no) != 0) {
            free(param.name);
            free(param.struct_type_name);
            tc_type_free(&param.type);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_peek(&func_line->tokens, tok_index)->kind == TC_TOK_COMMA) {
            tok_index++;
        }
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_RPAREN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_parse_type_syntax(&func_line->tokens, &tok_index, func_line->line_no, 1,
                             &def.return_type, &def.return_struct_name, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_THEN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_expect_stmt_end(&func_line->tokens, &tok_index, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }

    (*index)++;
    if (tc_parse_block_body_mode(ctx, lines, line_count, index, base_indent, file_indent,
                                 TC_MODULE_LIB, &body, diag) != 0) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return -1;
    }
    if (*index >= line_count || tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return tc_indent_diag(diag, TC_CE_MISSING_END, func_line->line_no,
                              "missing end for function definition");
    }
    if (lines[*index].indent != base_indent) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                              "end indentation does not match function");
    }
    (*index)++;

    def.body = body.items;
    def.body_count = body.count;
    body.items = NULL;
    out->kind = TC_STMT_FUNC_DEF;
    out->u.func_def = def;
    return 0;
}

/** 根据行首 Token 判定顶层所属分层；Self 单独出现于顶层则报错。 */
static int tc_classify_top_layer(const TcSourceLine *line, TcModuleMode mode, TcParseLayer *layer,
                                 TcDiagnostic *diag) {
    size_t idx = 0;
    const TcToken *tok = tc_peek(&line->tokens, idx);

    (void)mode;
    if (tok->kind == TC_TOK_PUBLIC || tok->kind == TC_TOK_PRIVATE) {
        idx++;
        tok = tc_peek(&line->tokens, idx);
    }
    if (tok->kind == TC_TOK_IMPORT) {
        *layer = TC_PARSE_LAYER_IMPORT;
        return 0;
    }
    if (tok->kind == TC_TOK_STRUCT) {
        *layer = TC_PARSE_LAYER_STRUCT;
        return 0;
    }
    if (tok->kind == TC_TOK_STATIC || tok->kind == TC_TOK_VAR || tok->kind == TC_TOK_LET) {
        *layer = TC_PARSE_LAYER_VALUE;
        return 0;
    }
    if (tok->kind == TC_TOK_FUNC) {
        *layer = TC_PARSE_LAYER_FUNC;
        return 0;
    }
    if (tok->kind == TC_TOK_SELF) {
        return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line->line_no, tok->column,
                              "Self is not allowed at module top level");
    }
    *layer = TC_PARSE_LAYER_EXEC;
    return 0;
}

static int tc_check_layer(TcParseLayer stmt_layer, TcParseLayer *cur, int line_no,
                          TcDiagnostic *diag) {
    TcParseLayer norm_stmt = stmt_layer;
    TcParseLayer norm_cur = *cur;

    /* #program 值声明与可执行语句允许交错（CFG/uninit 依赖 goto 跳过 var）；
     * import/struct/func 仍须保持相对顺序。 */
    if (norm_stmt == TC_PARSE_LAYER_EXEC) {
        norm_stmt = TC_PARSE_LAYER_VALUE;
    }
    if (norm_cur == TC_PARSE_LAYER_EXEC) {
        norm_cur = TC_PARSE_LAYER_VALUE;
    }

    if (stmt_layer == TC_PARSE_LAYER_IMPORT) {
        if (*cur > TC_PARSE_LAYER_IMPORT) {
            return tc_module_diag(diag, TC_CE_MODULE_LAYER, line_no, TC_COLUMN_UNKNOWN,
                                  "import must appear before other declarations");
        }
        return 0;
    }
    if (norm_stmt < norm_cur) {
        return tc_module_diag(diag, TC_CE_MODULE_LAYER, line_no, TC_COLUMN_UNKNOWN,
                              "declaration out of module layer order");
    }
    if (stmt_layer > *cur) {
        *cur = stmt_layer;
    }
    return 0;
}

/**
 * 强制模块头：源文件第一非空逻辑行必须是单独的 #program 或 #lib（可选分号）。
 * 成功后 *start_index = 1，后续从第二行起解析主体。
 */
static int tc_parse_module_header(TcSourceLine *lines, size_t line_count, TcProgram *program,
                                  size_t *start_index, TcDiagnostic *diag) {
    const TcSourceLine *hdr = NULL;
    size_t idx = 0;
    const TcToken *tok = NULL;

    if (line_count == 0) {
        return tc_syntax_error(diag, 0, TC_COLUMN_UNKNOWN, "expected #program or #lib");
    }
    hdr = &lines[0];
    tok = tc_peek(&hdr->tokens, idx);
    if (tok->kind != TC_TOK_PROGRAM && tok->kind != TC_TOK_LIB) {
        return tc_syntax_error(diag, hdr->line_no, tok->column, "expected #program or #lib");
    }
    program->mode = (tok->kind == TC_TOK_PROGRAM) ? TC_MODULE_PROGRAM : TC_MODULE_LIB;
    idx++;
    tok = tc_peek(&hdr->tokens, idx);
    if (tok->kind == TC_TOK_SEMICOLON) {
        idx++;
        tok = tc_peek(&hdr->tokens, idx);
    }
    if (tok->kind != TC_TOK_EOF) {
        return tc_syntax_error(diag, hdr->line_no, tok->column,
                               "unexpected token after module directive");
    }
    *start_index = 1;
    return 0;
}


/*
 * 语句语法分析入口。
 * 根据首个 Token 的种类 dispatch 到对应解析逻辑：
 *   TC_TOK_VAR / TC_TOK_LET           → tc_parse_var_or_const_def
 *   TC_TOK_STATIC                     → tc_parse_static_def（#lib）
 *   TC_TOK_IMPORT                     → tc_parse_import_stmt
 *   TC_TOK_PUBLIC / TC_TOK_PRIVATE    → 可见性前缀后再分派
 *   TC_TOK_WRITE / TC_TOK_WRITELN      → tc_parse_io_write_stmt
 *   TC_TOK_READ                        → tc_parse_read_stmt
 *   TC_TOK_GOTO / TC_TOK_LABEL         → goto / label
 *   TC_TOK_IDENTIFIER                  → 赋值
 *   其它                               → SyntaxError
 *
 * tc_parse_statement 默认按 #program 模式；整文件解析走 tc_parse_statement_mode。
 */
/* ------------------------------------------------------------------ */
/*  tc_parse_statement — 语法分析入口                                   */
/* ------------------------------------------------------------------ */

int tc_parse_statement(TcParserCtx *ctx, const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag) {
    return tc_parse_statement_mode(ctx, tokens, line_no, TC_MODULE_PROGRAM, out, diag);
}

static int tc_parse_statement_mode(TcParserCtx *ctx, const TcTokenList *tokens, int line_no,
                                   TcModuleMode mode, TcStatement *out, TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *first = NULL;
    TcVisibility vis = TC_VIS_NONE;

    memset(out, 0, sizeof(*out));
    first = tc_peek(tokens, index);

    if (first->kind == TC_TOK_PUBLIC || first->kind == TC_TOK_PRIVATE) {
        if (tc_parse_visibility_prefix(tokens, &index, mode, &vis, 0, diag, line_no) != 0) {
            return -1;
        }
        first = tc_peek(tokens, index);
    }

    if (first->kind == TC_TOK_STATIC) {
        return tc_parse_static_def(ctx, tokens, &index, line_no, mode, vis, out, diag);
    }

    if (first->kind == TC_TOK_VAR) {
        (void)vis;
        return tc_parse_var_or_const_def(ctx, tokens, &index, line_no, 0, out, diag);
    }

    if (first->kind == TC_TOK_LET) {
        return tc_parse_var_or_const_def(ctx, tokens, &index, line_no, 1, out, diag);
    }

    if (first->kind == TC_TOK_IMPORT) {
        return tc_parse_import_stmt(tokens, &index, line_no, out, diag);
    }

    if (first->kind == TC_TOK_RETURN) {
        return tc_parse_return_stmt(tokens, &index, line_no, out, diag);
    }

    if (first->kind == TC_TOK_FUNCALL) {
        return tc_parse_funcall_stmt(ctx, tokens, &index, line_no, mode, out, diag);
    }

    if (first->kind == TC_TOK_WRITE || first->kind == TC_TOK_WRITELN) {
        TcIoWrite io_write;
        TcStmtKind kind = first->kind == TC_TOK_WRITE ? TC_STMT_WRITE : TC_STMT_WRITELN;

        index++;
        memset(&io_write, 0, sizeof(io_write));
        if (tc_parse_io_write_stmt(tokens, &index, line_no, &io_write, diag) != 0) {
            tc_operand_free(&io_write.operand);
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            tc_operand_free(&io_write.operand);
            return -1;
        }
        out->kind = kind;
        out->u.io_write = io_write;
        return 0;
    }

    if (first->kind == TC_TOK_READ) {
        TcRead io_read;

        index++;
        if (tc_parse_read_stmt(tokens, &index, line_no, &io_read, diag) != 0) {
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(io_read.name);
            return -1;
        }
        out->kind = TC_STMT_READ;
        out->u.io_read = io_read;
        return 0;
    }

    if (first->kind == TC_TOK_BREAK || first->kind == TC_TOK_CONTINUE) {
        TcLoopControlStmt control;

        memset(&control, 0, sizeof(control));
        control.line = line_no;
        control.loop_id = -1;
        index++;
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            return -1;
        }
        if (first->kind == TC_TOK_BREAK) {
            out->kind = TC_STMT_BREAK;
            out->u.break_stmt = control;
        } else {
            out->kind = TC_STMT_CONTINUE;
            out->u.continue_stmt = control;
        }
        return 0;
    }

    if (first->kind == TC_TOK_GOTO) {
        TcGoto goto_stmt;
        const TcToken *name_tok = NULL;

        index++;
        memset(&goto_stmt, 0, sizeof(goto_stmt));
        goto_stmt.line = line_no;
        name_tok = tc_peek(tokens, index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column,
                                   "expected identifier after 'goto'");
        }
        goto_stmt.target = strndup(name_tok->start, name_tok->length);
        if (!goto_stmt.target) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column,
                              "memory allocation failed");
            return -1;
        }
        index++;
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(goto_stmt.target);
            return -1;
        }
        out->kind = TC_STMT_GOTO;
        out->u.goto_stmt = goto_stmt;
        return 0;
    }

    if (first->kind == TC_TOK_LABEL) {
        TcLabelDef label_def;
        const TcToken *name_tok = NULL;
        const TcToken *colon_tok = NULL;

        index++;
        memset(&label_def, 0, sizeof(label_def));
        label_def.line = line_no;
        name_tok = tc_peek(tokens, index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column,
                                   "expected identifier after 'label'");
        }
        label_def.name = strndup(name_tok->start, name_tok->length);
        if (!label_def.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column,
                              "memory allocation failed");
            return -1;
        }
        index++;
        colon_tok = tc_peek(tokens, index);
        if (colon_tok->kind != TC_TOK_COLON) {
            free(label_def.name);
            return tc_syntax_error(diag, line_no, colon_tok->column,
                                   "expected ':' after label name");
        }
        index++;
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(label_def.name);
            return -1;
        }
        out->kind = TC_STMT_LABEL_DEF;
        out->u.label_def = label_def;
        return 0;
    }

    if (first->kind == TC_TOK_IDENTIFIER) {
        if (tc_token_is_ident_named(first, "ptr_store")) {
            return tc_parse_ptr_store_stmt(tokens, &index, line_no, out, diag);
        }
        if (tc_token_is_ident_named(first, "memblock_store")) {
            return tc_parse_memblock_store_stmt(tokens, &index, line_no, out, diag);
        }
        if (tc_token_is_ident_named(first, "memblock_copy")) {
            return tc_parse_memblock_copy_stmt(tokens, &index, line_no, out, diag);
        }
        if (tc_token_is_ident_named(first, "memcopy_unsafe")) {
            return tc_parse_memcopy_unsafe_stmt(tokens, &index, line_no, out, diag);
        }
        if (index + 1 < tokens->count && tc_peek(tokens, index + 1)->kind == TC_TOK_DOT) {
            return tc_parse_field_assign_stmt(ctx, tokens, &index, line_no, out, diag);
        }
        TcAssign assign;
        assign.line = line_no;
        assign.name = strndup(first->start, first->length);
        if (!assign.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, first->column, "memory allocation failed");
            return -1;
        }
        index++;
        if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
            free(assign.name);
            return -1;
        }
        memset(&assign.rhs, 0, sizeof(assign.rhs));
        if (tc_peek(tokens, index)->kind == TC_TOK_FUNCALL) {
            if (tc_parse_funcall_rhs(ctx, tokens, &index, line_no, &assign.rhs, diag) != 0) {
                free(assign.name);
                tc_rhs_free(&assign.rhs);
                return -1;
            }
        } else if (tc_parse_rhs(ctx, tokens, &index, line_no, &assign.rhs, diag) != 0) {
            free(assign.name);
            tc_rhs_free(&assign.rhs);
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(assign.name);
            tc_rhs_free(&assign.rhs);
            return -1;
        }
        out->kind = TC_STMT_ASSIGN;
        out->u.assign = assign;
        return 0;
    }

    return tc_syntax_error(diag, line_no, first->column, "expected statement");
}

/* ------------------------------------------------------------------ */
/*  缩进引擎与 if 语句（多行 parse）                                      */
/* ------------------------------------------------------------------ */

static int tc_is_only_whitespace(const char *line) {
    while (*line != '\0' && *line != '\r' && *line != '\n') {
        if (*line != ' ' && *line != '\t') {
            return 0;
        }
        line++;
    }
    return 1;
}

static int tc_is_comment_only_line(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return *line == ';' || *line == '\0' || *line == '\r' || *line == '\n';
}

static int tc_is_skippable_line(const char *line) {
    if (line == NULL) {
        return 1;
    }
    if (tc_is_only_whitespace(line)) {
        return 1;
    }
    return tc_is_comment_only_line(line);
}

static int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message) {
    tc_diagnostic_set(diag, kind, line_no, TC_COLUMN_UNKNOWN, message);
    return -1;
}

static int tc_measure_line_indent(const char *line, TcFileIndent *file_indent, int line_no,
                                  TcDiagnostic *diag, int *out_indent) {
    int spaces = 0;
    int tabs = 0;
    const char *cursor = line;

    while (*cursor == ' ' || *cursor == '\t') {
        if (*cursor == ' ') {
            spaces++;
        } else {
            tabs++;
        }
        cursor++;
    }

    if (spaces > 0 && tabs > 0) {
        return tc_indent_diag(diag, TC_CE_INDENT_MIXED, line_no,
                              "mixed spaces and tabs in indentation");
    }

    if (spaces > 0) {
        if (file_indent->indent_char == '\0') {
            file_indent->indent_char = ' ';
        } else if (file_indent->indent_char != ' ') {
            return tc_indent_diag(diag, TC_CE_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        *out_indent = spaces;
        return 0;
    }

    if (tabs > 0) {
        if (file_indent->indent_char == '\0') {
            file_indent->indent_char = '\t';
            file_indent->indent_width = 1;
        } else if (file_indent->indent_char != '\t') {
            return tc_indent_diag(diag, TC_CE_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        *out_indent = tabs;
        return 0;
    }

    *out_indent = 0;
    return 0;
}

static void tc_source_lines_free(TcSourceLine *lines, size_t count) {
    size_t i = 0;

    if (!lines) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(lines[i].text);
        lines[i].text = NULL;
        tc_token_list_free(&lines[i].tokens);
    }
    free(lines);
}

static int tc_detect_indent_width(const TcSourceLine *lines, size_t count, char indent_char) {
    size_t i = 0;

    if (indent_char == '\t') {
        return 1;
    }

    for (i = 0; i < count; i++) {
        size_t j = 0;

        if (lines[i].tokens.count == 0 ||
            (lines[i].tokens.items[0].kind != TC_TOK_IF &&
             lines[i].tokens.items[0].kind != TC_TOK_WHILE)) {
            continue;
        }
        for (j = i + 1; j < count; j++) {
            if (lines[j].indent <= lines[i].indent) {
                break;
            }
            return lines[j].indent - lines[i].indent;
        }
    }
    return 4;
}

static int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                                 TcDiagnostic *diag, int line_no) {
    int delta = 0;

    if (indent <= base_indent) {
        return 0;
    }
    delta = indent - base_indent;
    if (file_indent->indent_char == '\t') {
        if (delta < file_indent->indent_width) {
            return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                                  "insufficient indentation in block");
        }
        return 0;
    }

    if (file_indent->indent_char == '\0' || file_indent->indent_char == ' ') {
        if (delta < file_indent->indent_width ||
            (delta % file_indent->indent_width) != 0) {
            return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                                  "insufficient indentation in block");
        }
        return 0;
    }

    return 0;
}

static void tc_stmt_block_init(TcStmtBlock *block) {
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

static void tc_stmt_block_free(TcStmtBlock *block) {
    size_t i = 0;

    for (i = 0; i < block->count; i++) {
        tc_statement_free(&block->items[i]);
    }
    free(block->items);
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

static int tc_stmt_block_push(TcStmtBlock *block, const TcStatement *stmt, TcDiagnostic *diag,
                              int line_no) {
    if (block->count == block->capacity) {
        size_t new_cap = block->capacity == 0 ? 4 : block->capacity * 2;
        TcStatement *items =
            (TcStatement *)realloc(block->items, new_cap * sizeof(TcStatement));

        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        block->items = items;
        block->capacity = new_cap;
    }
    block->items[block->count++] = *stmt;
    return 0;
}

static int tc_first_token_kind(const TcSourceLine *line) {
    if (line->tokens.count == 0) {
        return TC_TOK_EOF;
    }
    return (int)line->tokens.items[0].kind;
}

static int tc_parse_block_body_mode(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                                    size_t *index, int base_indent,
                                    const TcFileIndent *file_indent, TcModuleMode mode,
                                    TcStmtBlock *block, TcDiagnostic *diag) {
    while (*index < line_count) {
        TcSourceLine *line = &lines[*index];
        TcStatement stmt;
        int first_kind = 0;

        if (line->indent <= base_indent) {
            break;
        }
        if (tc_block_indent_valid(file_indent, base_indent, line->indent, diag,
                                  line->line_no) != 0) {
            return -1;
        }

        first_kind = tc_first_token_kind(line);
        if (first_kind == TC_TOK_ELSE) {
            return tc_indent_diag(diag, TC_CE_ELSE_POSITION, line->line_no,
                                  "else must appear at same indentation as if");
        }
        if (first_kind == TC_TOK_END) {
            return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, line->line_no,
                                  "end indentation does not match if");
        }

        memset(&stmt, 0, sizeof(stmt));
        if (first_kind == TC_TOK_IF) {
            if (tc_parse_if_stmt(ctx, lines, line_count, index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else if (first_kind == TC_TOK_WHILE) {
            if (tc_parse_while_stmt(ctx, lines, line_count, index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else {
            if (tc_parse_statement_mode(ctx, &line->tokens, line->line_no, mode, &stmt, diag) != 0) {
                return -1;
            }
            (*index)++;
        }

        if (tc_stmt_block_push(block, &stmt, diag, line->line_no) != 0) {
            tc_statement_free(&stmt);
            return -1;
        }
    }
    return 0;
}

static int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, int base_indent,
                               const TcFileIndent *file_indent, TcStmtBlock *block,
                               TcDiagnostic *diag) {
    return tc_parse_block_body_mode(ctx, lines, line_count, index, base_indent, file_indent,
                                    TC_MODULE_PROGRAM, block, diag);
}

int tc_parse_if_stmt(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count, size_t *index,
                     const TcFileIndent *file_indent, TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *if_line = NULL;
    size_t tok_index = 0;
    int base_indent = 0;
    TcIfStmt if_stmt;
    TcStmtBlock then_block;
    TcStmtBlock else_block;
    const TcToken *tok = NULL;

    if (*index >= line_count) {
        return tc_syntax_error(diag, 0, TC_COLUMN_UNKNOWN, "unexpected end of file");
    }

    if_line = &lines[*index];
    base_indent = if_line->indent;
    memset(&if_stmt, 0, sizeof(if_stmt));
    if_stmt.line = if_line->line_no;
    tc_stmt_block_init(&then_block);
    tc_stmt_block_init(&else_block);

    tok = tc_peek(&if_line->tokens, tok_index);
    if (tok->kind != TC_TOK_IF) {
        return tc_syntax_error(diag, if_line->line_no, tok->column, "expected if");
    }
    tok_index++;

    if (tc_parse_rhs(ctx, &if_line->tokens, &tok_index, if_line->line_no, &if_stmt.condition,
                     diag) != 0) {
        goto fail;
    }

    if (tc_expect_token(&if_line->tokens, &tok_index, TC_TOK_THEN, if_line->line_no, diag) != 0) {
        goto fail;
    }
    if (tc_expect_stmt_end(&if_line->tokens, &tok_index, if_line->line_no, diag) != 0) {
        goto fail;
    }

    (*index)++;
    if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, &then_block,
                            diag) != 0) {
        goto fail;
    }

    if (*index >= line_count) {
        tc_indent_diag(diag, TC_CE_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }

    if (tc_first_token_kind(&lines[*index]) == TC_TOK_ELSE) {
        if (lines[*index].indent != base_indent) {
            tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                           "else indentation does not match if");
            goto fail;
        }
        (*index)++;
        if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent,
                                &else_block, diag) != 0) {
            goto fail;
        }
    }

    if (*index >= line_count) {
        tc_indent_diag(diag, TC_CE_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }

    if (tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_indent_diag(diag, TC_CE_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }
    if (lines[*index].indent != base_indent) {
        tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                        "end indentation does not match if");
        goto fail;
    }
    (*index)++;

    if_stmt.then_body = then_block.items;
    if_stmt.then_count = then_block.count;
    if_stmt.else_body = else_block.items;
    if_stmt.else_count = else_block.count;
    then_block.items = NULL;
    else_block.items = NULL;

    out->kind = TC_STMT_IF;
    out->u.if_stmt = if_stmt;
    return 0;

fail:
    tc_rhs_free(&if_stmt.condition);
    tc_stmt_block_free(&then_block);
    tc_stmt_block_free(&else_block);
    return -1;
}

int tc_parse_while_stmt(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count, size_t *index,
                        const TcFileIndent *file_indent, TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *while_line = NULL;
    size_t tok_index = 0;
    int base_indent = 0;
    TcWhileStmt while_stmt;
    TcStmtBlock body;
    const TcToken *tok = NULL;

    if (*index >= line_count) {
        return tc_syntax_error(diag, 0, TC_COLUMN_UNKNOWN, "unexpected end of file");
    }

    while_line = &lines[*index];
    base_indent = while_line->indent;
    memset(&while_stmt, 0, sizeof(while_stmt));
    while_stmt.line = while_line->line_no;
    while_stmt.loop_id = -1;
    tc_stmt_block_init(&body);

    tok = tc_peek(&while_line->tokens, tok_index);
    if (tok->kind != TC_TOK_WHILE) {
        return tc_syntax_error(diag, while_line->line_no, tok->column, "expected while");
    }
    tok_index++;

    if (tc_parse_rhs(ctx, &while_line->tokens, &tok_index, while_line->line_no,
                     &while_stmt.condition, diag) != 0) {
        goto fail;
    }
    if (tc_expect_token(&while_line->tokens, &tok_index, TC_TOK_THEN, while_line->line_no,
                        diag) != 0 ||
        tc_expect_stmt_end(&while_line->tokens, &tok_index, while_line->line_no, diag) != 0) {
        goto fail;
    }

    (*index)++;
    if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, &body,
                            diag) != 0) {
        goto fail;
    }
    if (*index >= line_count || tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_indent_diag(diag, TC_CE_MISSING_END, while_line->line_no,
                       "missing end for while statement");
        goto fail;
    }
    if (lines[*index].indent != base_indent) {
        tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                       "end indentation does not match while");
        goto fail;
    }
    (*index)++;

    while_stmt.body = body.items;
    while_stmt.body_count = body.count;
    body.items = NULL;
    out->kind = TC_STMT_WHILE;
    out->u.while_stmt = while_stmt;
    return 0;

fail:
    tc_rhs_free(&while_stmt.condition);
    tc_stmt_block_free(&body);
    return -1;
}

static int tc_collect_source_lines(const char *source, TcSourceLine **out_lines, size_t *out_count,
                                   TcFileIndent *file_indent, TcDiagnostic *diag) {
    const char *cursor = source;
    int line_no = 1;
    TcSourceLine *lines = NULL;
    size_t count = 0;
    size_t capacity = 0;

    *out_lines = NULL;
    *out_count = 0;
    file_indent->indent_char = '\0';
    file_indent->indent_width = 4;

    while (*cursor != '\0') {
        const char *line_start = cursor;
        const char *line_end = cursor;
        char *line_copy = NULL;
        int indent = 0;

        while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        line_copy = (char *)malloc((size_t)(line_end - line_start) + 1);
        if (!line_copy) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            tc_source_lines_free(lines, count);
            return -1;
        }
        memcpy(line_copy, line_start, (size_t)(line_end - line_start));
        line_copy[line_end - line_start] = '\0';

        if (!tc_is_skippable_line(line_copy)) {
            TcSourceLine entry;

            if (tc_measure_line_indent(line_copy, file_indent, line_no, diag, &indent) != 0) {
                free(line_copy);
                tc_source_lines_free(lines, count);
                return -1;
            }

            memset(&entry, 0, sizeof(entry));
            entry.line_no = line_no;
            entry.indent = indent;
            entry.text = strdup(line_copy);
            if (!entry.text) {
                free(line_copy);
                tc_source_lines_free(lines, count);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
                return -1;
            }
            tc_token_list_init(&entry.tokens);
            if (tc_tokenize_line(entry.text, line_no, &entry.tokens, diag) != 0) {
                free(line_copy);
                free(entry.text);
                tc_token_list_free(&entry.tokens);
                tc_source_lines_free(lines, count);
                return -1;
            }

            if (count == capacity) {
                size_t new_cap = capacity == 0 ? 8 : capacity * 2;
                TcSourceLine *new_lines =
                    (TcSourceLine *)realloc(lines, new_cap * sizeof(TcSourceLine));

                if (!new_lines) {
                    free(line_copy);
                    free(entry.text);
                    tc_token_list_free(&entry.tokens);
                    tc_source_lines_free(lines, count);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                      "memory allocation failed");
                    return -1;
                }
                lines = new_lines;
                capacity = new_cap;
            }
            lines[count++] = entry;
        }

        free(line_copy);

        if (*line_end == '\r') {
            line_end++;
        }
        if (*line_end == '\n') {
            line_end++;
        }
        cursor = line_end;
        line_no++;
    }

    if (file_indent->indent_char == ' ') {
        file_indent->indent_width = tc_detect_indent_width(lines, count, file_indent->indent_char);
    }

    *out_lines = lines;
    *out_count = count;
    return 0;
}

static int tc_parse_module_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                                size_t start_index, const TcFileIndent *file_indent,
                                TcProgram *program, TcDiagnostic *diag) {
    size_t index = start_index;
    TcParseLayer cur_layer = TC_PARSE_LAYER_IMPORT;

    while (index < line_count) {
        TcStatement stmt;
        TcSourceLine *line = &lines[index];
        TcParseLayer stmt_layer = TC_PARSE_LAYER_EXEC;
        size_t tok_index = 0;
        TcVisibility vis = TC_VIS_NONE;
        const TcToken *first = tc_peek(&line->tokens, 0);

        memset(&stmt, 0, sizeof(stmt));

        if (tc_classify_top_layer(line, program->mode, &stmt_layer, diag) != 0) {
            return -1;
        }
        if (tc_check_layer(stmt_layer, &cur_layer, line->line_no, diag) != 0) {
            return -1;
        }

        if (stmt_layer == TC_PARSE_LAYER_STRUCT) {
            if (tc_parse_struct_def(ctx, lines, line_count, &index, program->mode, file_indent,
                                    &stmt, diag) != 0) {
                return -1;
            }
        } else if (stmt_layer == TC_PARSE_LAYER_FUNC) {
            if (tc_parse_func_def(ctx, lines, line_count, &index, program->mode, file_indent,
                                  &stmt, diag) != 0) {
                return -1;
            }
        } else if (first->kind == TC_TOK_PUBLIC || first->kind == TC_TOK_PRIVATE) {
            if (tc_parse_visibility_prefix(&line->tokens, &tok_index, program->mode, &vis, 0, diag,
                                           line->line_no) != 0) {
                return -1;
            }
            first = tc_peek(&line->tokens, tok_index);
            if (first->kind == TC_TOK_STATIC) {
                if (tc_parse_static_def(ctx, &line->tokens, &tok_index, line->line_no,
                                        program->mode, vis, &stmt, diag) != 0) {
                    return -1;
                }
            } else {
                return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line->line_no,
                                      first->column, "invalid use of visibility modifier");
            }
            index++;
        } else if (tc_first_token_kind(line) == TC_TOK_IF) {
            if (tc_parse_if_stmt(ctx, lines, line_count, &index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else if (tc_first_token_kind(line) == TC_TOK_WHILE) {
            if (tc_parse_while_stmt(ctx, lines, line_count, &index, file_indent, &stmt, diag) !=
                0) {
                return -1;
            }
        } else {
            if (tc_parse_statement_mode(ctx, &line->tokens, line->line_no, program->mode, &stmt,
                                        diag) != 0) {
                return -1;
            }
            index++;
        }

        if (tc_program_push(program, &stmt, diag) != 0) {
            tc_statement_free(&stmt);
            return -1;
        }
    }
    return 0;
}

int tc_parse_source_to_program(const char *source, TcProgram *program, TcDiagnostic *diag) {
    TcSourceLine *lines = NULL;
    size_t line_count = 0;
    size_t start_index = 0;
    TcFileIndent file_indent;
    TcParserCtx ctx;
    int rc = 0;

    tc_program_init(program);
    memset(&file_indent, 0, sizeof(file_indent));
    file_indent.indent_width = 4;

    rc = tc_collect_source_lines(source, &lines, &line_count, &file_indent, diag);
    if (rc != 0) {
        tc_program_free(program);
        return -1;
    }

    rc = tc_parse_module_header(lines, line_count, program, &start_index, diag);
    if (rc != 0) {
        tc_source_lines_free(lines, line_count);
        tc_program_free(program);
        return -1;
    }

    ctx.depth = 0;
    rc = tc_parse_module_body(&ctx, lines, line_count, start_index, &file_indent, program, diag);
    tc_source_lines_free(lines, line_count);
    if (rc != 0) {
        tc_program_free(program);
        return -1;
    }
    return 0;
}
