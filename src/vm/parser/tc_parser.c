/*
 * tc_parser.c — TC 语法分析器实现
 *
 * 消费 tc_tokenize_line 产出的 TcTokenList，按 TC 语言语法规则
 * 将 Token 流解析为 TcStatement / TcProgram（AST）。
 *
 * 强制模块头：首行须为 #program 或 #lib。
 * 支持 import / struct / func / static / 可见性 / Self；顶层按五层顺序校验（TcParseLayer）。
 * 语句/类型/函数/struct/RHS 解析见 tc_parser_{stmt,type,func,struct,rhs}.c。
 * expect/operand/binding 见 tc_parser_util.c；缩进/块体见 tc_parser_indent.c。
 */
#include "tc_parser.h"
#include "tc_parser_struct.h"
#include "tc_parser_type.h"
#include "tc_parser_func.h"
#include "tc_parser_stmt.h"
#include "tc_parser_free.h"
#include "tc_parser_rhs.h"
#include "tc_parser_internal.h"
#include "tc_parser_indent.h"

#include "tc_diagnostic.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* `read(int32)` 缺目标操作数 → OPERAND_COUNT（而非笼统 SYNTAX） */
    if (tc_expect_comma_or_operand_count(tokens, index, line_no, diag) != 0) {
        return -1;
    }

    if (tc_parse_binding_name(tokens, index, line_no, &out->name, diag) != 0) {
        return -1;
    }

    /* 多余的 `,` → 操作数个数超出 */
    if (tc_expect_rparen_or_operand_count(tokens, index, line_no, diag) != 0) {
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
    /*
     * `#program` / `#lib` 指令行本身属顶层行，缩进级别必须为 0。
     */
    if (hdr->indent != 0) {
        return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, hdr->line_no,
                              "top-level lines must not be indented");
    }
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

static int tc_parser_token_is_import_qual(const TcParserCtx *ctx, const TcToken *tok) {
    size_t i = 0;

    if (!ctx || !ctx->program || !tok || tok->kind != TC_TOK_IDENTIFIER || !tok->start) {
        return 0;
    }
    for (i = 0; i < ctx->program->count; i++) {
        const char *name = NULL;

        if (ctx->program->items[i].kind != TC_STMT_IMPORT) {
            continue;
        }
        name = ctx->program->items[i].u.import_stmt.module_name;
        if (name && strlen(name) == tok->length &&
            strncmp(name, tok->start, tok->length) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  tc_parse_statement — 语法分析入口                                   */
/* ------------------------------------------------------------------ */

int tc_parse_statement(TcParserCtx *ctx, const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag) {
    return tc_parse_statement_mode(ctx, tokens, line_no, TC_MODULE_PROGRAM, out, diag);
}

int tc_parse_statement_mode(TcParserCtx *ctx, const TcTokenList *tokens, int line_no,
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

    if (first->kind == TC_TOK_SELF) {
        /*
         * 赋值左侧允许 `Self.<名>` 作为整绑定目标、`Self.<名>.<字段…>` 作为
         * 字段目标（语言标准 §6.2）。`#lib` 函数体内访问模块 static 必须写
         * `Self.`（§4.3），因此这里是可写 `static var` 的唯一入口。
         */
        if (index + 1 >= tokens->count || tc_peek(tokens, index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, first->column, "expected . after Self");
        }
        if (index + 3 < tokens->count && tc_peek(tokens, index + 3)->kind == TC_TOK_DOT) {
            return tc_parse_field_assign_stmt(ctx, tokens, &index, line_no, out, diag);
        }
        {
            TcAssign assign;
            char *qualified = NULL;

            if (tc_parse_field_access_base(tokens, &index, line_no, &qualified, diag) != 0) {
                return -1;
            }
            assign.line = line_no;
            assign.name = qualified;
            if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
                free(qualified);
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
    }

    if (first->kind == TC_TOK_IDENTIFIER) {
        if (index + 1 < tokens->count && tc_peek(tokens, index + 1)->kind == TC_TOK_DOT) {
            /*
             * 附录 A assignment 含 imported_member_name。单点 `Qual.Name =` 且 Qual
             * 已出现在本文件 import 列表 → 解析期整绑定赋值（与 `Self.<名> =` 同形）。
             * 多点字段写或未导入前缀仍走字段赋值；分析器保留重分类作兜底。
             */
            if (index + 3 < tokens->count && tc_peek(tokens, index + 3)->kind != TC_TOK_DOT &&
                tc_parser_token_is_import_qual(ctx, first)) {
                TcAssign assign;
                char *qualified = NULL;

                if (tc_parse_binding_name(tokens, &index, line_no, &qualified, diag) != 0) {
                    return -1;
                }
                assign.line = line_no;
                assign.name = qualified;
                if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
                    free(qualified);
                    return -1;
                }
                memset(&assign.rhs, 0, sizeof(assign.rhs));
                if (tc_peek(tokens, index)->kind == TC_TOK_FUNCALL) {
                    if (tc_parse_funcall_rhs(ctx, tokens, &index, line_no, &assign.rhs, diag) !=
                        0) {
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
    if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, "if",
                            &then_block, diag) != 0) {
        goto fail;
    }

    if (*index >= line_count) {
        tc_indent_diag(diag, TC_CE_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }

    if (tc_first_token_kind(&lines[*index]) == TC_TOK_ELSE) {
        size_t else_tok_index = 0;

        if (lines[*index].indent != base_indent) {
            tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                           "else indentation does not match if");
            goto fail;
        }
        /*
         * 2.6.4：`else` 行只允许 `else` 本身（可选 `;`）。附录 A 的 `if_stmt` 要求
         * `else` 后换行 + 缩进 + `suite`，语言标准 §7.1.1 不支持单行 `else if`。
         */
        if (tc_peek(&lines[*index].tokens, else_tok_index)->kind == TC_TOK_ELSE) {
            else_tok_index++;
        }
        if (tc_expect_stmt_end(&lines[*index].tokens, &else_tok_index, lines[*index].line_no,
                               diag) != 0) {
            goto fail;
        }
        (*index)++;
        if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, "if",
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
    if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, "while",
                            &body, diag) != 0) {
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

        /*
         * 顶层行（`#program` 的 import / 类型 / 声明 /
         * 顶层语句与 `#lib` 的 import / 类型 / static / func）缩进级别必须为 0。
         * 附录 A 的顶层产生式（import_region、program_module、program_exec_region、
         * library_module）不消费 `INDENT`，只有 `suite` 消费（附录 A.2、A.3）。
         */
        if (line->indent != 0) {
            return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line->line_no,
                                  "top-level lines must not be indented");
        }

        if (tc_classify_top_layer(line, program->mode, &stmt_layer, diag) != 0) {
            return -1;
        }
        if (tc_check_layer(stmt_layer, &cur_layer, line->line_no, diag) != 0) {
            return -1;
        }

        /*
         * 2.6.5：`#lib` 没有可执行语句区（附录 A：library_module =
         * type_region, static_region, function_region），顶层可执行语句属语法
         * 拒绝。`goto` / `label` / `break` / `continue` / `return` 例外：附录 A
         * 明示其顶层出现由后续静态语义报专用码（GOTO_/LABEL_OUTSIDE_FUNCTION、
         * BREAK_/CONTINUE_OUTSIDE_LOOP、RETURN_OUTSIDE_FUNCTION），故放行。
         */
        if (program->mode == TC_MODULE_LIB && stmt_layer == TC_PARSE_LAYER_EXEC &&
            first->kind != TC_TOK_GOTO && first->kind != TC_TOK_LABEL &&
            first->kind != TC_TOK_BREAK && first->kind != TC_TOK_CONTINUE &&
            first->kind != TC_TOK_RETURN) {
            return tc_syntax_error(diag, line->line_no, first->column,
                                   "executable statement is not allowed in #lib");
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
        } else if (program->mode == TC_MODULE_LIB &&
                   (first->kind == TC_TOK_VAR || first->kind == TC_TOK_LET)) {
            /*
             * 附录 A 的 library_module 只接受带可见性的 static 成员，
             * `#lib` 顶层裸 var/let 在第 3 阶段报 TC_CE_SYNTAX。
             */
            return tc_syntax_error(diag, line->line_no, first->column,
                                   "non-static value declaration is not allowed in #lib");
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

    /*
     * `#program` 中的 `Self` 属结构类语法阶段诊断（TC_CE_PROGRAM_MODE_MISUSE，
     * §4.2/§1.3）。分析器虽也检查，但那属 SEM——若同一文件后面还有语法错误，会
     * 因「阶段优先」而抢先报出（§11 第 1 条）。此处按**源序**在语法阶段一次性扫描
     * 全部 Token 行，保证最早的 `Self` 先报。
     */
    if (program->mode == TC_MODULE_PROGRAM) {
        size_t li = 0;

        for (li = start_index; li < line_count; li++) {
            size_t ti = 0;

            for (ti = 0; ti < lines[li].tokens.count; ti++) {
                if (lines[li].tokens.items[ti].kind == TC_TOK_SELF) {
                    (void)tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, lines[li].line_no,
                                         lines[li].tokens.items[ti].column,
                                         "Self is not allowed in #program");
                    tc_source_lines_free(lines, line_count);
                    tc_program_free(program);
                    return -1;
                }
            }
        }
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.program = program;
    rc = tc_parse_module_body(&ctx, lines, line_count, start_index, &file_indent, program, diag);
    tc_source_lines_free(lines, line_count);
    if (rc != 0) {
        tc_program_free(program);
        return -1;
    }
    return 0;
}
