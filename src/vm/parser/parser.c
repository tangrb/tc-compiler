/*
 * parser.c — TC 语法分析器实现
 *
 * 支持的语句形式：
 *   var <name>: <type> = <rhs> [;]
 *   <name> = <rhs> [;]
 *
 * RHS 形式：
 *   <integer>                                    — 字面量
 *   <op>(<type> [, overflow ,] <lhs> , <rhs>)   — 算术运算
 *   cast(<type> [, overflow ,] <source_var>)    — 类型转换
 *
 * Parser 仅做结构解析，不做类型检查（由 Analyzer 负责）。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_parser.h"

#include "tc_diagnostic.h"

#include <stdlib.h>
#include <string.h>

/*
 * @brief 复制 Token 子串为以 null 结尾的堆字符串
 * @param start 源字符串起始指针
 * @param len   要复制的字符长度
 * @return 新分配的堆字符串指针（调用方需 free）；内存不足返回 NULL
 */
static char *tc_strdup_range(const char *start, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

/*
 * @brief 将 INTEGER Token 的文本转为 uint64_t
 * @param token 整数字面量 Token 指针
 * @return 解析得到的 uint64_t 值
 * @note 词法阶段已校验格式和范围，此处不做溢出检测
 */
static uint64_t tc_parse_token_integer(const TcToken *token) {
    uint64_t value = 0;
    size_t i = 0;
    for (i = 0; i < token->length; i++) {
        value = value * 10ULL + (uint64_t)(token->start[i] - '0');
    }
    return value;
}

/*
 * @brief 便捷地设置一条语法错误诊断
 * @param diag    诊断对象
 * @param line    行号
 * @param column  列号
 * @param message 错误描述
 * @return 始终返回 -1（便于调用方链式 return）
 */
static int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, message);
    return -1;
}

/*
 * @brief 查看 Token 列表指定 index 位置的 Token
 * @param tokens Token 列表
 * @param index  要查看的位置
 * @return Token 指针；index 越界时返回最后一个（通常是 EOF）
 */
static const TcToken *tc_peek(const TcTokenList *tokens, size_t index) {
    if (index >= tokens->count) {
        return &tokens->items[tokens->count - 1];
    }
    return &tokens->items[index];
}

/*
 * @brief 解析算术/cast 的操作数：标识符（变量）或整数字面量
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcOperand
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1 并设置 diag
 * @note 变量名会被 strdup 到 TcOperand 中（调用方需通过 tc_operand_free 释放）
 */
static int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                            TcOperand *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_IDENTIFIER) {
        out->kind = TC_OPERAND_VAR;
        out->u.name = tc_strdup_range(tok->start, tok->length);
        if (!out->u.name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok->column, "out of memory");
            return -1;
        }
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tc_parse_token_integer(tok);
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected operand");
}

/*
 * @brief 期望当前 Token 为指定种类，匹配则消费并前进
 * @param tokens  Token 列表
 * @param index   当前解析位置（匹配时会前进 1）
 * @param kind    期望的 Token 种类
 * @param line_no 当前行号
 * @param diag    诊断对象
 * @return 匹配返回 0；不匹配返回 -1 并设置 diag
 */
static int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                           int line_no, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    if (tok->kind != kind) {
        return tc_syntax_error(diag, line_no, tok->column, "unexpected token");
    }
    (*index)++;
    return 0;
}

/*
 * @brief 解析算术 RHS：op(type [, overflow ,] lhs , rhs)
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcRhs（kind 设为 TC_RHS_ARITH）
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1
 * @note 示例：add(int32, overflow, a, b)
 * @note overflow 关键字为可选项，出现时 mode 设为 TC_OVERFLOW
 */
static int tc_parse_arith_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcArithOp op = op_tok->arith_op;
    TcIntType type = TC_INT32;
    TcOverflowMode mode = TC_STRICT;

    if (op_tok->kind != TC_TOK_ARITH_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected arithmetic operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        type = type_tok->int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    /* overflow 关键字可选，出现则切换为 TC_OVERFLOW 模式 */
    {
        const TcToken *maybe_overflow = tc_peek(tokens, *index);
        if (maybe_overflow->kind == TC_TOK_OVERFLOW) {
            mode = TC_OVERFLOW;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        }
    }

    out->kind = TC_RHS_ARITH;
    out->u.arith.op = op;
    out->u.arith.type = type;
    out->u.arith.mode = mode;

    if (tc_parse_operand(tokens, index, line_no, &out->u.arith.lhs, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.arith.rhs, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        return -1;
    }
    return 0;
}

/*
 * @brief 解析 cast RHS：cast(type [, overflow ,] source_var)
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcRhs（kind 设为 TC_RHS_CAST）
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1
 * @note cast 的源操作数必须是变量（不能是字面量）
 * @note source 变量名会被 strdup，调用方需通过 tc_rhs_free 释放
 */
static int tc_parse_cast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                             TcRhs *out, TcDiagnostic *diag) {
    TcIntType target = TC_INT32;
    TcOverflowMode mode = TC_STRICT;

    if (tc_expect_token(tokens, index, TC_TOK_CAST, line_no, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        target = type_tok->int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *maybe_overflow = tc_peek(tokens, *index);
        if (maybe_overflow->kind == TC_TOK_OVERFLOW) {
            mode = TC_OVERFLOW;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        }
    }

    {
        const TcToken *src_tok = tc_peek(tokens, *index);
        if (src_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, src_tok->column,
                                   "cast source must be a variable");
        }
        out->kind = TC_RHS_CAST;
        out->u.cast.target = target;
        out->u.cast.mode = mode;
        out->u.cast.source = tc_strdup_range(src_tok->start, src_tok->length);
        if (!out->u.cast.source) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, src_tok->column, "out of memory");
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        return -1;
    }
    return 0;
}

static void tc_operand_free(TcOperand *operand);

/*
 * @brief 解析 write / writeln 语句：write(type, operand) [;]
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcIoWrite
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1
 */
static int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcIoWrite *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_parse_operand(tokens, index, line_no, &out->operand, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_operand_free(&out->operand);
        return -1;
    }
    return 0;
}

/*
 * @brief 解析 read 语句：read(type, identifier) [;]
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcRead
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1
 */
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
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->int_type;
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
        out->name = tc_strdup_range(name_tok->start, name_tok->length);
        if (!out->name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, name_tok->column, "out of memory");
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

/*
 * @brief 检查语句尾部：可选分号后必须紧跟 EOF
 * @param tokens  Token 列表
 * @param index   当前解析位置
 * @param line_no 当前行号
 * @param diag    诊断对象
 * @return 成功返回 0；有多余 Token 返回 -1
 */
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

/*
 * @brief RHS 分派：字面量 / 算术 / cast
 * @param tokens  Token 列表
 * @param index   当前解析位置（会前进）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcRhs
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1
 */
static int tc_parse_rhs(const TcTokenList *tokens, size_t *index, int line_no, TcRhs *out,
                        TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tc_parse_token_integer(tok);
        (*index)++;
        return 0;
    }
    if (tok->kind == TC_TOK_ARITH_OP) {
        return tc_parse_arith_rhs(tokens, index, line_no, out, diag);
    }
    if (tok->kind == TC_TOK_CAST) {
        return tc_parse_cast_rhs(tokens, index, line_no, out, diag);
    }
    return tc_syntax_error(diag, line_no, tok->column, "expected rhs expression");
}

/*
 * @brief 释放 TcOperand 中的动态内存
 * @param operand 待释放的操作数指针
 * @note 仅 TC_OPERAND_VAR 类型需要释放 u.name
 */
static void tc_operand_free(TcOperand *operand) {
    if (operand->kind == TC_OPERAND_VAR) {
        free(operand->u.name);
        operand->u.name = NULL;
    }
}

/*
 * @brief 释放 TcRhs 中的动态内存（操作数/源变量名）
 * @param rhs 待释放的 RHS 指针
 */
void tc_rhs_free(TcRhs *rhs) {
    if (!rhs) {
        return;
    }
    if (rhs->kind == TC_RHS_ARITH) {
        tc_operand_free(&rhs->u.arith.lhs);
        tc_operand_free(&rhs->u.arith.rhs);
    } else if (rhs->kind == TC_RHS_CAST) {
        free(rhs->u.cast.source);
        rhs->u.cast.source = NULL;
    }
}

/*
 * @brief 释放单条语句中的动态内存（变量名和 RHS）
 * @param stmt 待释放的语句指针
 */
void tc_statement_free(TcStatement *stmt) {
    if (!stmt) {
        return;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        free(stmt->u.var_def.name);
        stmt->u.var_def.name = NULL;
        tc_rhs_free(&stmt->u.var_def.rhs);
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        free(stmt->u.assign.name);
        stmt->u.assign.name = NULL;
        tc_rhs_free(&stmt->u.assign.rhs);
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        tc_operand_free(&stmt->u.io_write.operand);
    } else if (stmt->kind == TC_STMT_READ) {
        free(stmt->u.io_read.name);
        stmt->u.io_read.name = NULL;
    }
}

/*
 * @brief 初始化 TcProgram 为空状态
 * @param program 待初始化的程序指针
 * @note items 初始为 NULL，count/capacity 均为 0
 */
void tc_program_init(TcProgram *program) {
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
}

/*
 * @brief 释放整个 TcProgram 包含的所有语句及其动态内存
 * @param program 待释放的程序指针
 */
void tc_program_free(TcProgram *program) {
    size_t i = 0;
    for (i = 0; i < program->count; i++) {
        tc_statement_free(&program->items[i]);
    }
    free(program->items);
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
}

/*
 * @brief 向程序末尾追加一条语句，必要时按 2 倍容量扩容
 * @param program 程序指针
 * @param stmt    待追加的语句指针（内容被复制到 items 数组中）
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_program_push(TcProgram *program, const TcStatement *stmt) {
    if (program->count == program->capacity) {
        size_t new_cap = program->capacity == 0 ? 8 : program->capacity * 2;
        TcStatement *items = (TcStatement *)realloc(program->items, new_cap * sizeof(TcStatement));
        if (!items) {
            return -1;
        }
        program->items = items;
        program->capacity = new_cap;
    }
    program->items[program->count++] = *stmt;
    return 0;
}

/*
 * @brief 语句解析入口
 * @param tokens  Token 列表（来自 tc_tokenize_line）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcStatement
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1 并设置 diag
 * @note 首 Token 为 var → 变量定义；为标识符 → 赋值；否则语法错误
 * @note 可选分号后必须紧跟 EOF，不允许多余 Token
 */
int tc_parse_statement(const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *first = tc_peek(tokens, index);

    if (first->kind == TC_TOK_VAR) {
        TcVarDef var_def;
        var_def.line = line_no;
        var_def.name = NULL;
        var_def.type = TC_INT32;

        index++;
        {
            const TcToken *name_tok = tc_peek(tokens, index);
            if (name_tok->kind != TC_TOK_IDENTIFIER) {
                return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
            }
            var_def.name = tc_strdup_range(name_tok->start, name_tok->length);
            if (!var_def.name) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, name_tok->column, "out of memory");
                return -1;
            }
            index++;
        }
        if (tc_expect_token(tokens, &index, TC_TOK_COLON, line_no, diag) != 0) {
            free(var_def.name);
            return -1;
        }
        {
            const TcToken *type_tok = tc_peek(tokens, index);
            if (type_tok->kind != TC_TOK_INT_TYPE) {
                free(var_def.name);
                return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
            }
            var_def.type = type_tok->int_type;
            index++;
        }
        if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
            free(var_def.name);
            return -1;
        }
        if (tc_parse_rhs(tokens, &index, line_no, &var_def.rhs, diag) != 0) {
            free(var_def.name);
            return -1;
        }

        {
            if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
                free(var_def.name);
                tc_rhs_free(&var_def.rhs);
                return -1;
            }
        }

        out->kind = TC_STMT_VAR_DEF;
        out->u.var_def = var_def;
        return 0;
    }

    if (first->kind == TC_TOK_WRITE || first->kind == TC_TOK_WRITELN) {
        TcIoWrite io_write;
        TcStmtKind kind = first->kind == TC_TOK_WRITE ? TC_STMT_WRITE : TC_STMT_WRITELN;

        index++;
        if (tc_parse_io_write_stmt(tokens, &index, line_no, &io_write, diag) != 0) {
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

    if (first->kind == TC_TOK_IDENTIFIER) {
        TcAssign assign;
        assign.line = line_no;
        assign.name = tc_strdup_range(first->start, first->length);
        if (!assign.name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, first->column, "out of memory");
            return -1;
        }
        index++;
        if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
            free(assign.name);
            return -1;
        }
        if (tc_parse_rhs(tokens, &index, line_no, &assign.rhs, diag) != 0) {
            free(assign.name);
            return -1;
        }
        {
            if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
                free(assign.name);
                tc_rhs_free(&assign.rhs);
                return -1;
            }
        }
        out->kind = TC_STMT_ASSIGN;
        out->u.assign = assign;
        return 0;
    }

    return tc_syntax_error(diag, line_no, first->column, "expected statement");
}
