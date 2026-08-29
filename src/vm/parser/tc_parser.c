/*
 * tc_parser.c — TC 语法分析器实现
 *
 * 消费 tc_tokenize_line 产出的 TcTokenList，按 TC 语言语法规则
 * 将 Token 流解析为 TcStatement / TcProgram（AST）。
 *
 * 强制模块头：首行须为 #program 或 #lib。
 * 支持 import / struct / func / static / 可见性 / Self；顶层按五层顺序校验（TcParseLayer）。
 * 语句/类型/函数/struct/RHS 解析见 tc_parser_{stmt,type,func,struct,rhs}.c。
 */
#include "tc_parser.h"
#include "tc_parser_struct.h"
#include "tc_parser_type.h"
#include "tc_parser_func.h"
#include "tc_parser_stmt.h"
#include "tc_parser_free.h"
#include "tc_parser_rhs.h"
#include "tc_parser_internal.h"

#include "tc_diagnostic.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 缩进块工具声明见 tc_parser_internal.h；此处仅需本文件 static 前向声明 */
static int tc_parse_statement_mode(TcParserCtx *ctx, const TcTokenList *tokens, int line_no,
                                   TcModuleMode mode, TcStatement *out, TcDiagnostic *diag);


/* ------------------------------------------------------------------ */
/*  便捷错误报告辅助函数                                                 */
/* ------------------------------------------------------------------ */

int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_SYNTAX, line, column, message);
    return -1;
}


int tc_operand_count_error(TcDiagnostic *diag, int line, int column, const char *message) {
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
        if (*index + 1 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
            return tc_parse_field_access_operand(tokens, index, line_no, out, diag);
        }
        out->kind = TC_OPERAND_VAR;
        out->u.name = tc_strndup(tok->start, tok->length);
        if (!out->u.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_SELF &&
        *index + 1 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
        return tc_parse_field_access_operand(tokens, index, line_no, out, diag);
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
int tc_expect_stmt_end(const TcTokenList *tokens, size_t *index, int line_no,
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
    copy = (char *)tc_strndup(tok->start, tok->length);
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

int tc_parse_binding_name(const TcTokenList *tokens, size_t *index, int line_no,
                          char **out_name, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    const TcToken *member = NULL;
    size_t total = 0;
    char *name = NULL;

    if (!out_name) {
        return -1;
    }
    *out_name = NULL;

    if (tok->kind == TC_TOK_SELF) {
        if (*index + 2 >= tokens->count) {
            return tc_syntax_error(diag, line_no, tok->column, "expected Self.member");
        }
        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        member = tc_peek(tokens, *index + 2);
        if (member->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member->column, "expected member name");
        }
        total = 5 + 1 + member->length + 1;
        name = (char *)malloc(total);
        if (!name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(name, total, "Self.%.*s", (int)member->length, member->start);
        *out_name = name;
        *index += 3;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }
    if (*index + 2 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
        tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER) {
        member = tc_peek(tokens, *index + 2);
        total = tok->length + 1 + member->length + 1;
        name = (char *)malloc(total);
        if (!name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(name, total, "%.*s.%.*s", (int)tok->length, tok->start, (int)member->length,
                 member->start);
        *out_name = name;
        *index += 3;
        return 0;
    }
    name = tc_token_strdup(tok, line_no, diag);
    if (!name) {
        return -1;
    }
    *out_name = name;
    (*index)++;
    return 0;
}

int tc_module_diag(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                          const char *message) {
    /* 模块语义错误：写入指定 TcErrorKind（非一律 SYNTAX） */
    tc_diagnostic_set(diag, kind, line, column, message);
    return -1;
}


/*
 * 模块顶层声明分层（Parser 侧，与 tc_module 五层语义对齐）。
 * IMPORT → STRUCT → VALUE → FUNC → EXEC；数值越大越靠后，禁止回退。
 */
typedef enum {
    TC_PARSE_LAYER_IMPORT = 1,
    TC_PARSE_LAYER_STRUCT = 2,
    TC_PARSE_LAYER_VALUE = 3,
    TC_PARSE_LAYER_FUNC = 4,
    TC_PARSE_LAYER_EXEC = 5
} TcParseLayer;


void tc_string_list_free_local(char **items, size_t count) {
    size_t i = 0;
    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

int tc_parse_field_chain(const TcTokenList *tokens, size_t *index, int line_no,
                                char **out_base, char ***out_fields, size_t *out_field_count,
                                TcDiagnostic *diag) {
    TcOperand operand;
    size_t saved = *index;

    memset(&operand, 0, sizeof(operand));
    *out_base = NULL;
    *out_fields = NULL;
    *out_field_count = 0;

    if (tc_parse_field_access_operand(tokens, index, line_no, &operand, diag) != 0) {
        return -1;
    }
    if (operand.kind != TC_OPERAND_FIELD_READ) {
        *index = saved;
        tc_operand_free(&operand);
        return tc_syntax_error(diag, line_no, TC_COLUMN_UNKNOWN, "expected field access");
    }
    *out_base = operand.u.field_read.base;
    *out_fields = operand.u.field_read.fields;
    *out_field_count = operand.u.field_read.field_count;
    operand.u.field_read.base = NULL;
    operand.u.field_read.fields = NULL;
    operand.u.field_read.field_count = 0;
    return 0;
}

static int tc_parse_field_access_base(const TcTokenList *tokens, size_t *index, int line_no,
                                    char **out_base, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_base = NULL;
    if (tok->kind == TC_TOK_SELF) {
        const TcToken *member_tok = NULL;
        size_t base_len = 0;

        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        (*index) += 2;
        member_tok = tc_peek(tokens, *index);
        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        base_len = 5 + member_tok->length + 1;
        *out_base = (char *)malloc(base_len);
        if (!*out_base) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(*out_base, base_len, "Self.%.*s", (int)member_tok->length, member_tok->start);
        (*index)++;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }

    if (*index + 3 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
        tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER &&
        tc_peek(tokens, *index + 3)->kind == TC_TOK_DOT &&
        tok->length > 0 && tok->start[0] >= 'A' && tok->start[0] <= 'Z') {
        const TcToken *qual_tok = tok;
        const TcToken *member_tok = tc_peek(tokens, *index + 2);
        size_t base_len = qual_tok->length + 1 + member_tok->length + 1;

        *out_base = (char *)malloc(base_len);
        if (!*out_base) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(*out_base, base_len, "%.*s.%.*s", (int)qual_tok->length, qual_tok->start,
                 (int)member_tok->length, member_tok->start);
        *index += 3;
        return 0;
    }

    *out_base = tc_token_strdup(tok, line_no, diag);
    if (!*out_base) {
        return -1;
    }
    (*index)++;
    return 0;
}

int tc_parse_field_access_operand(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcOperand *out, TcDiagnostic *diag) {
    char *base = NULL;
    char **fields = NULL;
    size_t field_count = 0;
    size_t field_cap = 0;

    if (tc_parse_field_access_base(tokens, index, line_no, &base, diag) != 0) {
        return -1;
    }

    while (tc_peek(tokens, *index)->kind == TC_TOK_DOT) {
        char *field_name = NULL;
        const TcToken *tok = NULL;

        (*index)++;
        tok = tc_peek(tokens, *index);
        if (tok->kind != TC_TOK_IDENTIFIER) {
            free(base);
            tc_string_list_free_local(fields, field_count);
            return tc_syntax_error(diag, line_no, tok->column, "expected field name");
        }
        field_name = tc_token_strdup(tok, line_no, diag);
        if (!field_name) {
            free(base);
            tc_string_list_free_local(fields, field_count);
            return -1;
        }
        if (field_count == field_cap) {
            size_t new_cap = field_cap == 0 ? 4 : field_cap * 2;
            char **new_fields = (char **)realloc(fields, new_cap * sizeof(char *));

            if (!new_fields) {
                free(field_name);
                free(base);
                tc_string_list_free_local(fields, field_count);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                return -1;
            }
            fields = new_fields;
            field_cap = new_cap;
        }
        fields[field_count++] = field_name;
        (*index)++;
    }

    if (field_count == 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return tc_syntax_error(diag, line_no, TC_COLUMN_UNKNOWN, "expected field name");
    }

    if (out) {
        memset(&out->u.field_read.resolved, 0, sizeof(out->u.field_read.resolved));
        out->kind = TC_OPERAND_FIELD_READ;
        out->u.field_read.base = base;
        out->u.field_read.fields = fields;
        out->u.field_read.field_count = field_count;
    } else {
        free(base);
        tc_string_list_free_local(fields, field_count);
    }
    return 0;
}

/**
 * 解析可选的 public/private 前缀。
 * #program 禁止可见性；#lib 在 require_vis=1 时缺失则报 MISSING_VISIBILITY。
 */
int tc_parse_visibility_prefix(const TcTokenList *tokens, size_t *index,
                                      TcModuleMode mode, TcVisibility *out_vis,
                                      int require_vis, TcDiagnostic *diag, int line_no) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_vis = TC_VIS_NONE;
    if (tok->kind == TC_TOK_PUBLIC) {
        if (mode == TC_MODULE_PROGRAM) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "public is not allowed in #program mode");
        }
        /* 仅 #lib 模块顶层允许可见性；函数体等其它上下文一律拒绝（附录 A：suite
         * 的 statement 不含可见性前缀）。 */
        if (mode != TC_MODULE_LIB) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "visibility modifier is not allowed inside a function body");
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
        if (mode != TC_MODULE_LIB) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "visibility modifier is not allowed inside a function body");
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

/* 解析 read(type, id) 语句 */
static int tc_parse_read_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRead *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = tc_type_tag_singleton(TC_INT32);
    out->name = NULL;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (!tc_token_is_type(type_tok)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = tc_type_tag_singleton(type_tok->u.int_type);
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_parse_binding_name(tokens, index, line_no, &out->name, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        free(out->name);
        out->name = NULL;
        return -1;
    }
    return 0;
}

/*
 * @brief 解析 var 或 let 定义
 * @param is_const 1 表示 let 常量，0 表示 var 变量
 *
 * 语法：
 *   var id: type = rhs
 *   let id: type = rhs（必须初始化）
 */

/**
 * 解析 #lib 内 static var / static let。
 * #program 中出现 static → PROGRAM_MODE_MISUSE；缺可见性 → MISSING_VISIBILITY。
 */

/** 解析 `import Name;` —— 目标须为标识符（模块文件名不含 .tc）。 */


/**
 * 解析 funcall 调用目标：Self.member / Qual.member / 裸名。
 * 写入 is_self、qualifier、member_name、target（规范化文本）。
 */



/**
 * 解析 `funcall(...)` 为 RHS（用于 var/赋值右侧；不含语句结尾检查）。
 */













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
    if (stmt_layer == TC_PARSE_LAYER_IMPORT) {
        if (*cur > TC_PARSE_LAYER_IMPORT) {
            return tc_module_diag(diag, TC_CE_MODULE_LAYER, line_no, TC_COLUMN_UNKNOWN,
                                  "import must appear before other declarations");
        }
        return 0;
    }
    if (stmt_layer < *cur) {
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
        goto_stmt.target = tc_strndup(name_tok->start, name_tok->length);
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
        label_def.name = tc_strndup(name_tok->start, name_tok->length);
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

    if (first->kind == TC_TOK_PTR_STORE) {
        return tc_parse_ptr_store_stmt(tokens, &index, line_no, out, diag);
    }
    if (first->kind == TC_TOK_MEMBLOCK_STORE) {
        return tc_parse_memblock_store_stmt(tokens, &index, line_no, out, diag);
    }
    if (first->kind == TC_TOK_MEMBLOCK_COPY) {
        return tc_parse_memblock_copy_stmt(tokens, &index, line_no, out, diag);
    }
    if (first->kind == TC_TOK_MEMCOPY_UNSAFE) {
        return tc_parse_memcopy_unsafe_stmt(tokens, &index, line_no, out, diag);
    }

    if (first->kind == TC_TOK_IDENTIFIER) {
        if (index + 1 < tokens->count && tc_peek(tokens, index + 1)->kind == TC_TOK_DOT) {
            return tc_parse_field_assign_stmt(ctx, tokens, &index, line_no, out, diag);
        }
        TcAssign assign;
        assign.line = line_no;
        assign.name = tc_strndup(first->start, first->length);
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

int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message) {
    tc_diagnostic_set(diag, kind, line_no, TC_COLUMN_UNKNOWN, message);
    return -1;
}

/* 行首缩进测量：缩进只能由 ASCII 空格 U+0020 组成，每 4 个空格为一级，
 * 行首空格总数必须能被 4 整除；行首出现水平制表符 U+0009 一律报
 * TC_CE_INDENT_MIXED（无论是否与空格混用）。 */
static int tc_measure_line_indent(const char *line, int line_no, TcDiagnostic *diag,
                                  int *out_indent) {
    int spaces = 0;
    const char *cursor = line;

    while (*cursor == ' ' || *cursor == '\t') {
        if (*cursor == '\t') {
            return tc_indent_diag(diag, TC_CE_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        spaces++;
        cursor++;
    }

    if (spaces % 4 != 0) {
        return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                              "insufficient indentation in block");
    }
    *out_indent = spaces;
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

/* 块内语句缩进校验：块内语句相对块头必须恰好增加一级（固定 4 空格）。
 * 一次增加多级（如 8 空格）、或增加不足一级（如 2 空格）均报缩进错误；
 * 回退到不属于本块的级别由调用方按 end/else 对齐检查判定。 */
int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                                 TcDiagnostic *diag, int line_no) {
    int delta = indent - base_indent;

    if (delta <= 0) {
        return 0; /* 块结束或不属于本块，由调用方判定 */
    }
    if (delta != file_indent->indent_width) {
        return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                              "insufficient indentation in block");
    }
    return 0;
}

void tc_stmt_block_init(TcStmtBlock *block) {
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

void tc_stmt_block_free(TcStmtBlock *block) {
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

int tc_first_token_kind(const TcSourceLine *line) {
    if (line->tokens.count == 0) {
        return TC_TOK_EOF;
    }
    return (int)line->tokens.items[0].kind;
}

/** end 行尾随 token 检查：`end`（可选分号）后必须行尾；
 * 尾随 token 报 TC_CE_SYNTAX（附录 A：块以 `end` 收尾）。
 * 行 token 列表末尾含 TC_TOK_EOF 哨兵，需跳过。 */
int tc_end_line_check(const TcSourceLine *line, TcDiagnostic *diag) {
    size_t i = 1;

    if (line->tokens.count > 1 && line->tokens.items[1].kind == TC_TOK_SEMICOLON) {
        i = 2;
    }
    while (i < line->tokens.count && line->tokens.items[i].kind == TC_TOK_EOF) {
        i++;
    }
    if (i < line->tokens.count) {
        return tc_syntax_error(diag, line->line_no, line->tokens.items[i].column,
                               "unexpected trailing tokens after end");
    }
    return 0;
}

int tc_parse_block_body_mode(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
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
            return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, line->line_no,
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

int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
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
    if (tc_end_line_check(&lines[*index], diag) != 0) {
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
    if (tc_end_line_check(&lines[*index], diag) != 0) {
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

            if (tc_measure_line_indent(line_copy, line_no, diag, &indent) != 0) {
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

    /* 检查 UTF-8 BOM（tc_lib.c 文件读取路径已做同检查；此处覆盖 tc_compile_source 等
     * 字符串/嵌入路径，是 API 侧唯一的 BOM 防线，需保留）。
     * 先判前 3 字节非 NUL（字符串至少 3 字节）再比较，避免短源越界读。 */
    if (source[0] != '\0' && source[1] != '\0' && source[2] != '\0' &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, 1, 1, "UTF-8 BOM not allowed in source file");
        tc_program_free(program);
        return -1;
    }

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
