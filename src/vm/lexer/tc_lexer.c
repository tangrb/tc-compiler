/*
 * tc_lexer.c — TC 词法分析器实现
 *
 * 逐行扫描 TC 源码，识别关键字、标识符、多进制整数字面量、
 * 格式说明符（%d/%i/%u/%x/%X/%o/%b/%t/%f/%e/%E/%g/%G）及单字符标点（:=,() 等）。
 * 每行产出 TcTokenList（含 TC_TOK_EOF），由 Parser 消费。
 */
#include "tc_lexer.h"

#include "tc_diagnostic.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Token 列表管理                                                      */
/* ------------------------------------------------------------------ */

static int tc_token_list_push(TcTokenList *list, const TcToken *token, TcDiagnostic *diag) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        TcToken *items = (TcToken *)realloc(list->items, new_cap * sizeof(TcToken));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        list->items = items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = *token;
    return 0;
}

void tc_token_list_init(TcTokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void tc_token_list_free(TcTokenList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ------------------------------------------------------------------ */
/*  字符分类辅助函数                                                    */
/* ------------------------------------------------------------------ */

/** 判断字符是否为 TC 标识符的合法起始字符（字母或下划线） */
static int tc_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int tc_is_identifier_start(char c) {
    return tc_is_letter(c);
}

/** 判断字符是否为 TC 标识符的合法后续字符（字母、数字或下划线） */
/*
 * 标识符后续字符：字母数字（下划线由 tc_is_letter 覆盖）。
 * TC 语言标识符允许首字下划线，也允许中间含有下划线，
 * 因为 tc_is_letter 包含 '_' = 0x5F（ASCII 字母集之上一位）；
 * 但数字 0-9 不在字母判定范围内，所以此处补充数字检查。
 */
static int tc_is_identifier_part(char c) {
    return tc_is_letter(c) || (c >= '0' && c <= '9');
}

/** 跳过连续的空格/制表符，同时更新列号 */
static void tc_skip_ws(const char **p, int *column) {
    while (**p == ' ' || **p == '\t') {
        (*p)++;
        if (column) {
            (*column)++;
        }
    }
}

/** 获取进制数字的数值；非法字符返回 -1 */
static int tc_digit_value(char c, int base) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (base == 16) {
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  辅助运算：uint64 溢出检测                                           */
/* ------------------------------------------------------------------ */

static int tc_mul_u64_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > UINT64_MAX / a) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static int tc_add_u64_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > UINT64_MAX - b) {
        return 1;
    }
    *out = a + b;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  进制数字串解析 & 整数字面量解析                                      */
/* ------------------------------------------------------------------ */

/*
 * @brief 解析指定进制的数字序列，支持下划线分隔
 * @param p                 当前指针（会被更新到末位之后）
 * @param base              进制（2 / 8 / 10 / 16）
 * @param allow_underscore  是否支持下划线分隔符
 * @param value             输出：解析出的整数值
 * @param diag,line,column  诊断上下文
 * @return 成功返回 0；非法格式或超范围返回 -1
 * @note 下划线不能出现在开头、结尾或连续出现
 */
static int tc_parse_radix_digits(const char **p, int base, int allow_underscore,
                                 uint64_t *value, TcDiagnostic *diag, int line, int column) {
    int has_digit = 0;
    int prev_underscore = 0;

    while (**p != '\0') {
        if (allow_underscore && **p == '_') {
            if (!has_digit || prev_underscore) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
                return -1;
            }
            prev_underscore = 1;
            (*p)++;
            continue;
        }
        prev_underscore = 0;
        {
            int digit = tc_digit_value(**p, base);
            if (digit < 0 || (base == 8 && digit > 7) || (base == 2 && digit > 1)) {
                break;
            }
            has_digit = 1;
            /* value = value * base + digit，带溢出检测 */
            {
                uint64_t next = 0;
                if (tc_mul_u64_overflow(*value, (uint64_t)base, value) ||
                    tc_add_u64_overflow(*value, (uint64_t)digit, &next)) {
                    tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, column,
                                      "integer literal too large");
                    return -1;
                }
                *value = next;
            }
            (*p)++;
        }
    }

    if (!has_digit || prev_underscore) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
        return -1;
    }
    return 0;
}

/*
 * @brief 解析 TC 整数字面量
 * @param start     源码起始位置
 * @param end       输出：解析结束位置
 * @param lit       输出：解析结果 TcLiteral
 * @param diag,line,column  诊断上下文
 * @return 成功返回 0；失败返回 -1 并设置 diag
 *
 * 支持的格式：
 *   [-]数字[数字|_]*[uU]        十进制
 *   0x十六进制[0-9a-fA-F_]*[uU] 十六进制
 *   0b二进制[01_]*[uU]           二进制
 *   0o八进制[0-7_]*[uU]          八进制
 */
static int tc_parse_integer_literal(const char *start, const char **end, TcLiteral *lit,
                                    TcDiagnostic *diag, int line, int column) {
    const char *p = start;
    int negative = 0;

    lit->magnitude = 0;
    lit->negative = 0;
    lit->unsigned_suffix = 0;
    lit->is_bool = 0;
    lit->is_float = 0;
    lit->float_value = 0.0;
    lit->float32_suffix = 0;

    if (*p == '-') {
        negative = 1;
        p++;
        if (*p == '\0') {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected integer literal");
            return -1;
        }
    }

    /* 判断进制前缀并分派解析 */
    if (*p == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            p += 2;
            if (tc_parse_radix_digits(&p, 16, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] == 'b' || p[1] == 'B') {
            p += 2;
            if (tc_parse_radix_digits(&p, 2, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] == 'o' || p[1] == 'O') {
            p += 2;
            if (tc_parse_radix_digits(&p, 8, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] >= '0' && p[1] <= '9') {
            /* 前导零后跟十进制数字（如 0123）——TC 语言视为非法。
             * C 系语言的八进制前缀在 TC 中由 0o 显式表示，
             * 0 后直接跟数字的歧义格式不被允许。 */
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
            return -1;
        } else {
            lit->magnitude = 0;
            p++;
        }
    } else if (isdigit((unsigned char)*p)) {
        if (tc_parse_radix_digits(&p, 10, 1, &lit->magnitude, diag, line, column) != 0) {
            return -1;
        }
    } else {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected integer literal");
        return -1;
    }

    /* 处理可选的 u/U 无符号后缀 */
    if (*p == 'u' || *p == 'U') {
        if (negative) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, column,
                              "negative value cannot use unsigned suffix");
            return -1;
        }
        lit->unsigned_suffix = 1;
        p++;
    }

    if (negative && !lit->unsigned_suffix) {
        lit->negative = 1;
    }

    *end = p;
    return 0;
}

/*
 * 判断从 start 起的数字串是否应解析为浮点字面量（含 - 前缀）。
 * 十六进制/二进制/八进制前缀走整数字面量路径。
 */
static int tc_peek_float_literal(const char *start) {
    const char *p = start;

    if (*p == '-') {
        p++;
    }
    if (*p == '\0') {
        return 0;
    }
    if (*p == '0' && p[1] != '\0' &&
        (p[1] == 'x' || p[1] == 'X' || p[1] == 'b' || p[1] == 'B' ||
         p[1] == 'o' || p[1] == 'O')) {
        return 0;
    }
    while (*p != '\0') {
        if (*p == '.') {
            return 1;
        }
        if (*p == 'e' || *p == 'E') {
            return 1;
        }
        if (!isdigit((unsigned char)*p)) {
            break;
        }
        p++;
    }
    return 0;
}

/*
 * @brief 解析 TC 浮点字面量（十进制、科学计数法、inf/-inf/nan、f/F 后缀）
 */
static int tc_parse_float_literal(const char *start, const char **end, TcLiteral *lit,
                                  TcDiagnostic *diag, int line, int column) {
    const char *p = start;
    char buf[256];
    size_t len = 0;
    char *endptr = NULL;
    double value = 0.0;

    lit->magnitude = 0;
    lit->negative = 0;
    lit->unsigned_suffix = 0;
    lit->is_bool = 0;
    lit->is_float = 1;
    lit->float_value = 0.0;
    lit->float32_suffix = 0;

    if (*p == '-') {
        lit->negative = 1;
        p++;
    }

    if (*p == '\0') {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected float literal");
        return -1;
    }

    if (strncmp(p, "inf", 3) == 0 && !tc_is_identifier_part(p[3])) {
        lit->float_value = lit->negative ? -INFINITY : INFINITY;
        *end = p + 3;
        return 0;
    }

    if (*p == '.') {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid float literal");
        return -1;
    }

    while (*p != '\0' && len + 1 < sizeof(buf)) {
        if (*p == 'f' || *p == 'F') {
            if (len == 0) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid float literal");
                return -1;
            }
            lit->float32_suffix = 1;
            p++;
            break;
        }
        if (*p == 'u' || *p == 'U') {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, column,
                              "float literal cannot use unsigned suffix");
            return -1;
        }
        if (!isdigit((unsigned char)*p) && *p != '.' && *p != 'e' && *p != 'E' &&
            *p != '+' && *p != '-') {
            break;
        }
        buf[len++] = *p;
        p++;
    }
    buf[len] = '\0';

    if (len == 0) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected float literal");
        return -1;
    }

    errno = 0;
    value = strtod(buf, &endptr);
    if (endptr == buf || errno == ERANGE) {
        tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, column,
                          "float literal out of range");
        return -1;
    }
    if (*endptr != '\0') {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column,
                          "invalid float literal");
        return -1;
    }

    if (lit->negative) {
        value = -value;
    }
    lit->float_value = value;

    if (lit->float32_suffix) {
        double min_subnormal = ldexp(1.0, -149);
        if (isfinite(value) &&
            (fabs(value) > (double)FLT_MAX ||
             (value != 0.0 && fabs(value) < min_subnormal))) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, column,
                              "float literal out of float32 range");
            return -1;
        }
    }

    *end = p;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  关键字识别 & Token 发射                                             */
/* ------------------------------------------------------------------ */

/*
 * 将标识符文本匹配为关键字 Token，匹配顺序如下：
 *   1. 语言关键字：var / let / cast / wrap / truncate / write / writeln / read
 *   2. 布尔字面量：true / false（TC_TOK_BOOL_LIT，带 is_bool 标志）
 *   3. 类型名 & 运算符：int8 / uint32 / add / sub / eq / and / xor / shl ……
 *      （委托 tc_types.h 中的 tc_*_parse 系列函数）
 * 未匹配时返回 0，调用方应按普通标识符（TC_TOK_IDENTIFIER）处理。
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
    if (strcmp(buf, "let") == 0) {
        token->kind = TC_TOK_LET;
        return 1;
    }
    if (strcmp(buf, "cast") == 0) {
        token->kind = TC_TOK_CAST;
        return 1;
    }
    if (strcmp(buf, "bitcast") == 0) {
        token->kind = TC_TOK_BITCAST;
        return 1;
    }
    if (strcmp(buf, "wrap") == 0) {
        token->kind = TC_TOK_WRAP;
        return 1;
    }
    if (strcmp(buf, "truncate") == 0) {
        token->kind = TC_TOK_TRUNCATE;
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
    if (strcmp(buf, "if") == 0) {
        token->kind = TC_TOK_IF;
        return 1;
    }
    if (strcmp(buf, "while") == 0) {
        token->kind = TC_TOK_WHILE;
        return 1;
    }
    if (strcmp(buf, "break") == 0) {
        token->kind = TC_TOK_BREAK;
        return 1;
    }
    if (strcmp(buf, "continue") == 0) {
        token->kind = TC_TOK_CONTINUE;
        return 1;
    }
    if (strcmp(buf, "then") == 0) {
        token->kind = TC_TOK_THEN;
        return 1;
    }
    if (strcmp(buf, "else") == 0) {
        token->kind = TC_TOK_ELSE;
        return 1;
    }
    if (strcmp(buf, "end") == 0) {
        token->kind = TC_TOK_END;
        return 1;
    }
    if (strcmp(buf, "goto") == 0) {
        token->kind = TC_TOK_GOTO;
        return 1;
    }
    if (strcmp(buf, "label") == 0) {
        token->kind = TC_TOK_LABEL;
        return 1;
    }
    if (strcmp(buf, "true") == 0) {
        token->kind = TC_TOK_BOOL_LIT;
        token->u.literal.is_bool = 1;
        token->u.literal.magnitude = 1;
        token->u.literal.negative = 0;
        token->u.literal.unsigned_suffix = 0;
        token->u.literal.is_float = 0;
        token->u.literal.float_value = 0.0;
        token->u.literal.float32_suffix = 0;
        return 1;
    }
    if (strcmp(buf, "false") == 0) {
        token->kind = TC_TOK_BOOL_LIT;
        token->u.literal.is_bool = 1;
        token->u.literal.magnitude = 0;
        token->u.literal.negative = 0;
        token->u.literal.unsigned_suffix = 0;
        token->u.literal.is_float = 0;
        token->u.literal.float_value = 0.0;
        token->u.literal.float32_suffix = 0;
        return 1;
    }
    if (strcmp(buf, "inf") == 0) {
        token->kind = TC_TOK_FLOAT_LIT;
        token->u.literal.is_float = 1;
        token->u.literal.float_value = INFINITY;
        token->u.literal.negative = 0;
        token->u.literal.unsigned_suffix = 0;
        token->u.literal.is_bool = 0;
        token->u.literal.float32_suffix = 0;
        return 1;
    }
    if (strcmp(buf, "nan") == 0) {
        token->kind = TC_TOK_FLOAT_LIT;
        token->u.literal.is_float = 1;
        token->u.literal.float_value = NAN;
        token->u.literal.negative = 0;
        token->u.literal.unsigned_suffix = 0;
        token->u.literal.is_bool = 0;
        token->u.literal.float32_suffix = 0;
        return 1;
    }
    if (strcmp(buf, "ieee") == 0) {
        token->kind = TC_TOK_IEEE;
        return 1;
    }
    if (strcmp(buf, "ptr") == 0) {
        token->kind = TC_TOK_PTR;
        return 1;
    }
    if (strcmp(buf, "memblock") == 0) {
        token->kind = TC_TOK_MEMBLOCK;
        return 1;
    }
    if (strcmp(buf, "struct") == 0) {
        token->kind = TC_TOK_STRUCT;
        return 1;
    }
    if (strcmp(buf, "func") == 0) {
        token->kind = TC_TOK_FUNC;
        return 1;
    }
    if (strcmp(buf, "funcall") == 0) {
        token->kind = TC_TOK_FUNCALL;
        return 1;
    }
    if (strcmp(buf, "return") == 0) {
        token->kind = TC_TOK_RETURN;
        return 1;
    }
    if (strcmp(buf, "void") == 0) {
        token->kind = TC_TOK_VOID;
        return 1;
    }
    if (strcmp(buf, "Self") == 0) {
        token->kind = TC_TOK_SELF;
        return 1;
    }
    if (strcmp(buf, "public") == 0) {
        token->kind = TC_TOK_PUBLIC;
        return 1;
    }
    if (strcmp(buf, "private") == 0) {
        token->kind = TC_TOK_PRIVATE;
        return 1;
    }
    if (strcmp(buf, "static") == 0) {
        token->kind = TC_TOK_STATIC;
        return 1;
    }
    if (strcmp(buf, "import") == 0) {
        token->kind = TC_TOK_IMPORT;
        return 1;
    }
    if (strcmp(buf, "nullptr") == 0) {
        token->kind = TC_TOK_NULLPTR;
        return 1;
    }
    if (strcmp(buf, "padding") == 0) {
        token->kind = TC_TOK_PADDING;
        return 1;
    }
    if (strcmp(buf, "float32") == 0) {
        token->kind = TC_TOK_FLOAT_TYPE;
        token->u.int_type = TC_FLOAT32;
        return 1;
    }
    if (strcmp(buf, "float64") == 0) {
        token->kind = TC_TOK_FLOAT_TYPE;
        token->u.int_type = TC_FLOAT64;
        return 1;
    }
    /* 类型名和运算符也是关键字（以标识符形式出现） */
    if (tc_type_parse(buf, &token->u.int_type)) {
        token->kind = TC_TOK_INT_TYPE;
        return 1;
    }
    if (tc_arith_op_parse(buf, &token->u.arith_op)) {
        token->kind = TC_TOK_ARITH_OP;
        return 1;
    }
    if (tc_unary_op_parse(buf, &token->u.unary_op)) {
        token->kind = TC_TOK_UNARY_OP;
        return 1;
    }
    if (tc_compare_op_parse(buf, &token->u.compare_op)) {
        token->kind = TC_TOK_COMPARE_OP;
        return 1;
    }
    if (strcmp(buf, "xor") == 0) {
        token->kind = TC_TOK_BITWISE_OP;
        token->u.bitwise_op = TC_BIT_XOR;
        return 1;
    }
    if (tc_shift_op_parse(buf, &token->u.shift_op)) {
        token->kind = TC_TOK_SHIFT_OP;
        return 1;
    }
    if (tc_logic_op_parse(buf, &token->u.logic_op)) {
        token->kind = TC_TOK_LOGIC_OP;
        return 1;
    }
    return 0;
}

/*
 * @brief 构造 Token 并追加到列表末尾
 * @param kind       Token 种类
 * @param start      源码起始位置
 * @param len        Token 长度
 * @param line,column 位置信息
 * @param int_type,arith_op,literal  语义值（未使用的参数传默认值）
 * @return 成功返回 0；内存不足返回 -1
 */
static int tc_emit_token(TcTokenList *out, TcTokenKind kind, const char *start, size_t len,
                         int line, int column, TcTypeKind int_type, TcArithOp arith_op,
                         const TcLiteral *literal, TcDiagnostic *diag) {
    TcToken token;
    token.kind = kind;
    token.start = start;
    token.length = len;
    token.line = line;
    token.column = column;
    if (literal) {
        token.u.literal = *literal;
    } else {
        token.u.int_type = int_type;
        token.u.arith_op = arith_op;
    }
    return tc_token_list_push(out, &token, diag);
}

/* ------------------------------------------------------------------ */
/*  词法分析入口                                                       */
/* ------------------------------------------------------------------ */

int tc_tokenize_line(const char *line, int line_no, TcTokenList *out, TcDiagnostic *diag) {
    const char *p = line;
    int column = 1;

    tc_skip_ws(&p, &column);

    while (*p != '\0' && *p != '\r' && *p != '\n') {
        const char *start = p;
        int tok_column = column;

        /* 单字符标点符号 */
        if (*p == ':') {
            if (tc_emit_token(out, TC_TOK_COLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '=') {
            if (tc_emit_token(out, TC_TOK_EQUAL, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ',') {
            if (tc_emit_token(out, TC_TOK_COMMA, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '(') {
            if (tc_emit_token(out, TC_TOK_LPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ')') {
            if (tc_emit_token(out, TC_TOK_RPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ';') {
            /* ; 兼具语句结束符和行注释作用，忽略同行后续所有字符 */
            if (tc_emit_token(out, TC_TOK_SEMICOLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            break;
        }
        if (*p == '#') {
            const char *directive_start = p;
            const char *name_start = NULL;
            size_t name_len = 0;
            TcTokenKind directive_kind;

            p++;
            column++;
            if (!tc_is_identifier_start(*p)) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "expected module directive");
                return -1;
            }
            name_start = p;
            p++;
            column++;
            while (tc_is_identifier_part(*p)) {
                p++;
                column++;
            }
            name_len = (size_t)(p - name_start);
            if (name_len == 7 && strncmp(name_start, "program", 7) == 0) {
                directive_kind = TC_TOK_PROGRAM;
            } else if (name_len == 3 && strncmp(name_start, "lib", 3) == 0) {
                directive_kind = TC_TOK_LIB;
            } else {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "invalid module directive");
                return -1;
            }
            if (tc_emit_token(out, directive_kind, directive_start,
                              (size_t)(p - directive_start), line_no, tok_column,
                              TC_INT32, TC_ADD, NULL, diag) != 0) {
                return -1;
            }
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '@') {
            if (tc_emit_token(out, TC_TOK_AT, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '.') {
            if (isdigit((unsigned char)p[1])) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "invalid float literal");
                return -1;
            }
            if (tc_emit_token(out, TC_TOK_DOT, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '<') {
            if (tc_emit_token(out, TC_TOK_LT, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '>') {
            if (tc_emit_token(out, TC_TOK_GT, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL, diag) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }

        /* 格式说明符：%d / %u / %x / %X / %o / %b */
        if (*p == '%') {
            char spec_buf[4];
            TcFormatSpec fmt = TC_FMT_NONE;
            if (p[1] == '\0') {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "unexpected character");
                return -1;
            }
            spec_buf[0] = '%';
            spec_buf[1] = p[1];
            spec_buf[2] = '\0';
            if (!tc_format_spec_parse(spec_buf, &fmt)) {
                tc_diagnostic_set(diag, TC_ERR_FORMAT_STRING, line_no, tok_column,
                                  "invalid format specifier");
                return -1;
            }
            {
                TcToken token;
                token.kind = TC_TOK_FORMAT_SPEC;
                token.start = start;
                token.length = 2;
                token.line = line_no;
                token.column = tok_column;
                token.u.format_spec = fmt;
                if (tc_token_list_push(out, &token, diag) != 0) {
                    return -1;
                }
            }
            p += 2;
            column += 2;
            tc_skip_ws(&p, &column);
            continue;
        }

        /* 整数字面量或浮点字面量：以数字或负号开头 */
        if (*p == '-' || isdigit((unsigned char)*p)) {
            TcLiteral lit;
            const char *end = NULL;

            if (*p == '-' && strncmp(p + 1, "inf", 3) == 0 &&
                !tc_is_identifier_part(p[4])) {
                if (tc_parse_float_literal(p, &end, &lit, diag, line_no, tok_column) != 0) {
                    return -1;
                }
                if (tc_emit_token(out, TC_TOK_FLOAT_LIT, start, (size_t)(end - start), line_no,
                                  tok_column, TC_INT32, TC_ADD, &lit, diag) != 0) {
                    return -1;
                }
            } else if (tc_peek_float_literal(p)) {
                if (tc_parse_float_literal(p, &end, &lit, diag, line_no, tok_column) != 0) {
                    return -1;
                }
                if (tc_emit_token(out, TC_TOK_FLOAT_LIT, start, (size_t)(end - start), line_no,
                                  tok_column, TC_INT32, TC_ADD, &lit, diag) != 0) {
                    return -1;
                }
            } else {
                if (tc_parse_integer_literal(p, &end, &lit, diag, line_no, tok_column) != 0) {
                    return -1;
                }
                if (tc_emit_token(out, TC_TOK_INTEGER, start, (size_t)(end - start), line_no,
                                  tok_column, TC_INT32, TC_ADD, &lit, diag) != 0) {
                    return -1;
                }
            }
            column += (int)(end - p);
            p = end;
            tc_skip_ws(&p, &column);
            continue;
        }

        /* 标识符或关键字 */
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
                    if (tc_token_list_push(out, &token, diag) != 0) {
                        return -1;
                    }
                } else if (tc_emit_token(out, TC_TOK_IDENTIFIER, start, len, line_no, tok_column,
                                         TC_INT32, TC_ADD, NULL, diag) != 0) {
                    return -1;
                }
            }
            tc_skip_ws(&p, &column);
            continue;
        }

        /* 遇到无法识别的字符 */
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "unexpected character");
        return -1;
    }

    /* 行末注入 EOF Token */
    {
        TcToken eof;
        eof.kind = TC_TOK_EOF;
        eof.start = p;
        eof.length = 0;
        eof.line = line_no;
        eof.column = column;
        eof.u.int_type = TC_INT32;
        if (tc_token_list_push(out, &eof, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

const char *tc_token_kind_name(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_EOF:
        return "EOF";
    case TC_TOK_VAR:
        return "VAR";
    case TC_TOK_LET:
        return "LET";
    case TC_TOK_INT_TYPE:
        return "INT_TYPE";
    case TC_TOK_FLOAT_TYPE:
        return "FLOAT_TYPE";
    case TC_TOK_ARITH_OP:
        return "ARITH_OP";
    case TC_TOK_UNARY_OP:
        return "UNARY_OP";
    case TC_TOK_COMPARE_OP:
        return "COMPARE_OP";
    case TC_TOK_LOGIC_OP:
        return "LOGIC_OP";
    case TC_TOK_BITWISE_OP:
        return "BITWISE_OP";
    case TC_TOK_SHIFT_OP:
        return "SHIFT_OP";
    case TC_TOK_FORMAT_SPEC:
        return "FORMAT_SPEC";
    case TC_TOK_CAST:
        return "CAST";
    case TC_TOK_BITCAST:
        return "BITCAST";
    case TC_TOK_WRAP:
        return "WRAP";
    case TC_TOK_TRUNCATE:
        return "TRUNCATE";
    case TC_TOK_WRITE:
        return "WRITE";
    case TC_TOK_WRITELN:
        return "WRITELN";
    case TC_TOK_READ:
        return "READ";
    case TC_TOK_IF:
        return "IF";
    case TC_TOK_WHILE:
        return "WHILE";
    case TC_TOK_BREAK:
        return "BREAK";
    case TC_TOK_CONTINUE:
        return "CONTINUE";
    case TC_TOK_THEN:
        return "THEN";
    case TC_TOK_ELSE:
        return "ELSE";
    case TC_TOK_END:
        return "END";
    case TC_TOK_GOTO:
        return "GOTO";
    case TC_TOK_LABEL:
        return "LABEL";
    case TC_TOK_IDENTIFIER:
        return "IDENTIFIER";
    case TC_TOK_INTEGER:
        return "INTEGER";
    case TC_TOK_FLOAT_LIT:
        return "FLOAT_LIT";
    case TC_TOK_BOOL_LIT:
        return "BOOL_LIT";
    case TC_TOK_IEEE:
        return "IEEE";
    case TC_TOK_COLON:
        return "COLON";
    case TC_TOK_EQUAL:
        return "EQUAL";
    case TC_TOK_COMMA:
        return "COMMA";
    case TC_TOK_LPAREN:
        return "LPAREN";
    case TC_TOK_RPAREN:
        return "RPAREN";
    case TC_TOK_PTR:
        return "PTR";
    case TC_TOK_MEMBLOCK:
        return "MEMBLOCK";
    case TC_TOK_STRUCT:
        return "STRUCT";
    case TC_TOK_FUNC:
        return "FUNC";
    case TC_TOK_FUNCALL:
        return "FUNCALL";
    case TC_TOK_RETURN:
        return "RETURN";
    case TC_TOK_VOID:
        return "VOID";
    case TC_TOK_SELF:
        return "SELF";
    case TC_TOK_PUBLIC:
        return "PUBLIC";
    case TC_TOK_PRIVATE:
        return "PRIVATE";
    case TC_TOK_STATIC:
        return "STATIC";
    case TC_TOK_IMPORT:
        return "IMPORT";
    case TC_TOK_PROGRAM:
        return "PROGRAM";
    case TC_TOK_LIB:
        return "LIB";
    case TC_TOK_NULLPTR:
        return "NULLPTR";
    case TC_TOK_AT:
        return "AT";
    case TC_TOK_LT:
        return "LT";
    case TC_TOK_GT:
        return "GT";
    case TC_TOK_DOT:
        return "DOT";
    case TC_TOK_PADDING:
        return "PADDING";
    case TC_TOK_SEMICOLON:
        return "SEMICOLON";
    }
    return "UNKNOWN";
}
