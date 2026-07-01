/*
 * lexer.c — TC 词法分析器实现
 *
 * 按行扫描 TC 源码，跳过空白，识别：
 *   - 单字符标点（: = , ( ) ;）
 *   - 十进制整数字面量（拒绝超过 uint64 的值）
 *   - 关键字与类型名（var, cast, overflow, int8~uint64, add~mod）
 *   - 标识符（字母/下划线开头，后跟字母数字下划线）
 * 每行末尾追加 TC_TOK_EOF 标记。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_lexer.h"

#include "tc_diagnostic.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 向 Token 列表追加一个 Token，必要时按 2 倍容量扩容
 * @param list  Token 列表指针
 * @param token 待追加的 Token 指针
 * @return 成功返回 0；内存不足返回 -1
 */
static int tc_token_list_push(TcTokenList *list, const TcToken *token) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        TcToken *items = (TcToken *)realloc(list->items, new_cap * sizeof(TcToken));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = *token;
    return 0;
}

/*
 * @brief 初始化 Token 列表为空状态
 * @param list 待初始化的 Token 列表指针
 * @note 此时 items 为 NULL，count/capacity 均为 0
 */
void tc_token_list_init(TcTokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
 * @brief 释放 Token 列表的 items 数组并将列表重置为空状态
 * @param list 待释放的 Token 列表指针
 * @note 只释放 items 数组本身，不释放 Token 内指针（Token 的 start 指向源行内存）
 */
void tc_token_list_free(TcTokenList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
 * @brief 判断字符是否为字母或下划线
 * @param c 待检测字符
 * @return 是字母或下划线返回 1；否则返回 0
 */
static int tc_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/*
 * @brief 判断字符是否为标识符起始字符（必须为字母或下划线）
 * @param c 待检测字符
 * @return 可做标识符起始字符返回 1；否则返回 0
 */
static int tc_is_identifier_start(char c) {
    return tc_is_letter(c);
}

/*
 * @brief 判断字符是否为标识符组成部分（字母、下划线或数字）
 * @param c 待检测字符
 * @return 可做标识符组成部分返回 1；否则返回 0
 */
static int tc_is_identifier_part(char c) {
    return tc_is_letter(c) || (c >= '0' && c <= '9');
}

/*
 * @brief 跳过行内空格与制表符
 * @param p 指向当前位置的指针（会前进到第一个非空白字符位置）
 */
static void tc_skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') {
        (*p)++;
     }
}

/*
 * @brief 从 start 解析十进制整数
 * @param start  数字字符串起始位置
 * @param end    输出参数，指向数字串之后的位置
 * @param value  输出参数，解析得到的无符号整数值
 * @param diag   诊断对象
 * @param line   当前行号
 * @param column 当前列号
 * @return 成功返回 0；遇到非数字字符或 uint64 溢出返回 -1 并设置 diag
 * @note 逐位累加时检测 uint64 溢出
 */
static int tc_parse_integer(const char *start, const char **end, uint64_t *value,
                            TcDiagnostic *diag, int line, int column) {
    const char *p = start;
    uint64_t result = 0;

    if (!isdigit((unsigned char)*p)) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected integer literal");
        return -1;
    }

    while (isdigit((unsigned char)*p)) {
        int digit = *p - '0';
        if (result > UINT64_MAX / 10ULL ||
            (result == UINT64_MAX / 10ULL && (uint64_t)digit > UINT64_MAX % 10ULL)) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, column,
                              "integer literal too large");
            return -1;
        }
        result = result * 10ULL + (uint64_t)digit;
        p++;
    }

    *value = result;
    *end = p;
    return 0;
}

/*
 * @brief 尝试将标识符文本识别为关键字或类型/运算符名
 * @param text  标识符起始指针
 * @param len   标识符长度
 * @param token 输出参数，识别成功时填充 token->kind 及相关字段
 * @return 识别为关键字/类型名/运算符名返回 1；普通标识符返回 0
 * @note 依次匹配：var / cast / overflow / 整数类型名 / 算术运算符
 */
static int tc_keyword_token(const char *text, size_t len, TcToken *token) {
    char buf[32];
    if (len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, text, len);
    buf[len] = '\0';

    if (strcmp(buf, "var") == 0) {
        token->kind = TC_TOK_VAR;
        return 1;
    }
    if (strcmp(buf, "cast") == 0) {
        token->kind = TC_TOK_CAST;
        return 1;
    }
    if (strcmp(buf, "overflow") == 0) {
        token->kind = TC_TOK_OVERFLOW;
        return 1;
    }
    if (strcmp(buf, "write") == 0) {
        token->kind = TC_TOK_WRITE;
        return 1;
    }
    if (strcmp(buf, "writeln") == 0) {
        token->kind = TC_TOK_WRITELN;
        return 1;
    }
    if (strcmp(buf, "read") == 0) {
        token->kind = TC_TOK_READ;
        return 1;
    }
    if (tc_type_parse(buf, &token->int_type)) {
        token->kind = TC_TOK_INT_TYPE;
        return 1;
    }
    if (tc_arith_op_parse(buf, &token->arith_op)) {
        token->kind = TC_TOK_ARITH_OP;
        return 1;
    }
    return 0;
}

/*
 * @brief 构造 Token 并追加到输出列表
 * @param out      目标 Token 列表
 * @param kind     Token 种类
 * @param start    Token 在源行中的起始指针
 * @param len      Token 文本长度
 * @param line     行号
 * @param column   列号
 * @param int_type 整数类型（用于 TC_TOK_INT_TYPE）
 * @param arith_op 算术运算符（用于 TC_TOK_ARITH_OP）
 * @return 成功返回 0；列表扩容失败返回 -1
 */
static int tc_emit_token(TcTokenList *out, TcTokenKind kind, const char *start, size_t len,
                         int line, int column, TcIntType int_type, TcArithOp arith_op) {
    TcToken token;
    token.kind = kind;
    token.start = start;
    token.length = len;
    token.line = line;
    token.column = column;
    token.int_type = int_type;
    token.arith_op = arith_op;
    return tc_token_list_push(out, &token);
}

/*
 * @brief 词法分析主函数：扫描一行源文本直至换行或字符串结束
 * @param line    源行文本（不含换行符）
 * @param line_no 当前行号（1-based）
 * @param out     输出参数，Token 列表
 * @param diag    诊断对象
 * @return 成功返回 0；遇到非法字符或超大字面量返回 -1 并设置 diag
 * @note column 从 1 开始计数，与诊断输出一致
 * @note 行尾自动追加一个 TC_TOK_EOF 标记，供 Parser 检测语句结束
 */
int tc_tokenize_line(const char *line, int line_no, TcTokenList *out, TcDiagnostic *diag) {
    const char *p = line;
    int column = 1;

    tc_skip_ws(&p);

    while (*p != '\0' && *p != '\r' && *p != '\n') {
        const char *start = p;
        int tok_column = column;

        /* 单字符标点符号 */
        if (*p == ':') {
            if (tc_emit_token(out, TC_TOK_COLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }
        if (*p == '=') {
            if (tc_emit_token(out, TC_TOK_EQUAL, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }
        if (*p == ',') {
            if (tc_emit_token(out, TC_TOK_COMMA, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }
        if (*p == '(') {
            if (tc_emit_token(out, TC_TOK_LPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }
        if (*p == ')') {
            if (tc_emit_token(out, TC_TOK_RPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }
        if (*p == ';') {
            if (tc_emit_token(out, TC_TOK_SEMICOLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p);
            continue;
        }

        /* 数字开头 → 整数字面量 */
        if (isdigit((unsigned char)*p)) {
            uint64_t value = 0;
            const char *end = NULL;
            if (tc_parse_integer(p, &end, &value, diag, line_no, tok_column) != 0) {
                return -1;
            }
            if (tc_emit_token(out, TC_TOK_INTEGER, start, (size_t)(end - start), line_no, tok_column,
                              TC_INT32, TC_ADD) != 0) {
                return -1;
            }
            column += (int)(end - p);
            p = end;
            tc_skip_ws(&p);
            continue;
        }

        /* 字母/下划线开头 → 关键字或标识符 */
        if (tc_is_identifier_start(*p)) {
            p++;
            column++;
            while (tc_is_identifier_part(*p)) {
                p++;
                column++;
            }
            {
                TcToken token;
                size_t len = (size_t)(p - start);
                if (tc_keyword_token(start, len, &token)) {
                    token.start = start;
                    token.length = len;
                    token.line = line_no;
                    token.column = tok_column;
                    if (tc_token_list_push(out, &token) != 0) {
                        return -1;
                    }
                } else if (tc_emit_token(out, TC_TOK_IDENTIFIER, start, len, line_no, tok_column,
                                         TC_INT32, TC_ADD) != 0) {
                    return -1;
                }
            }
            tc_skip_ws(&p);
            continue;
        }

        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "unexpected character");
        return -1;
    }

    /* 行尾 EOF 标记，供 Parser 检测语句结束 */
    {
        TcToken eof;
        eof.kind = TC_TOK_EOF;
        eof.start = p;
        eof.length = 0;
        eof.line = line_no;
        eof.column = column;
        eof.int_type = TC_INT32;
        eof.arith_op = TC_ADD;
        if (tc_token_list_push(out, &eof) != 0) {
            return -1;
        }
    }
    return 0;
}
