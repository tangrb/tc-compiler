/*
 * tc_lexer.h — 词法分析器接口
 *
 * 将一行 TC 源码文本转换为 TcTokenList（Token 流），供 Parser 消费。
 * 支持关键字、标识符、整数字面量（含多进制）、格式说明符及标点符号。
 * Phase 2：增加模块相关 Token（#program/#lib、import、func、struct、
 * public/private/static、Self 等）。
 */
#ifndef TC_LEXER_H
#define TC_LEXER_H

#include "tc_types.h"

/** Token 种类枚举 */
typedef enum {
    TC_TOK_EOF,         /* 行结束 */
    TC_TOK_VAR,         /* 'var' 关键字 */
    TC_TOK_LET,         /* 'let' 关键字 */
    TC_TOK_INT_TYPE,    /* 类型名（int8 / uint32 / bool 等） */
    TC_TOK_FLOAT_TYPE,  /* 浮点类型名（float32 / float64） */
    TC_TOK_ARITH_OP,    /* 算术运算符（add/sub/mul/div/mod） */
    TC_TOK_UNARY_OP,    /* 单目运算符（abs/neg） */
    TC_TOK_COMPARE_OP,  /* 比较运算符（eq/ne/lt/le/gt/ge） */
    TC_TOK_LOGIC_OP,    /* 逻辑运算符（and/or/not） */
    TC_TOK_BITWISE_OP,  /* 按位运算符（xor；and/or 由 parser 按类型分派） */
    TC_TOK_SHIFT_OP,    /* 移位运算符（shl/shr） */
    TC_TOK_FORMAT_SPEC, /* 格式说明符（%d/%u/%x/%X/%o/%b/%t） */
    TC_TOK_CAST,        /* 'cast' 关键字 */
    TC_TOK_BITCAST,     /* 'bitcast' 关键字 */
    TC_TOK_WRAP,        /* 'wrap' 关键字 */
    TC_TOK_TRUNCATE,    /* 'truncate' 关键字 */
    TC_TOK_WRITE,       /* 'write' 关键字 */
    TC_TOK_WRITELN,     /* 'writeln' 关键字 */
    TC_TOK_READ,        /* 'read' 关键字 */
    TC_TOK_IF,          /* 'if' 关键字 */
    TC_TOK_WHILE,       /* 'while' 关键字 */
    TC_TOK_BREAK,       /* 'break' 关键字 */
    TC_TOK_CONTINUE,    /* 'continue' 关键字 */
    TC_TOK_THEN,        /* 'then' 关键字 */
    TC_TOK_ELSE,        /* 'else' 关键字 */
    TC_TOK_END,         /* 'end' 关键字 */
    TC_TOK_GOTO,        /* 'goto' 关键字 */
    TC_TOK_LABEL,       /* 'label' 关键字 */
    TC_TOK_IDENTIFIER,  /* 用户定义标识符（变量名） */
    TC_TOK_INTEGER,     /* 整数字面量 */
    TC_TOK_FLOAT_LIT,   /* 浮点字面量（含 inf / nan） */
    TC_TOK_BOOL_LIT,    /* 布尔字面量 true/false */
    TC_TOK_IEEE,        /* 'ieee' 关键字（浮点运算模式） */
    TC_TOK_COLON,       /* ':' */
    TC_TOK_EQUAL,       /* '=' */
    TC_TOK_COMMA,       /* ',' */
    TC_TOK_LPAREN,      /* '(' */
    TC_TOK_RPAREN,      /* ')' */
    TC_TOK_PTR,         /* 'ptr' 关键字 */
    TC_TOK_MEMBLOCK,    /* 'memblock' 关键字 */
    TC_TOK_STRUCT,      /* 'struct' 关键字 */
    TC_TOK_FUNC,        /* 'func' 关键字 */
    TC_TOK_FUNCALL,     /* 'funcall' 关键字 */
    TC_TOK_RETURN,      /* 'return' 关键字 */
    TC_TOK_VOID,        /* 'void' 关键字 */
    TC_TOK_SELF,        /* 'Self' 关键字 */
    TC_TOK_PUBLIC,      /* 'public' 关键字 */
    TC_TOK_PRIVATE,     /* 'private' 关键字 */
    TC_TOK_STATIC,      /* 'static' 关键字 */
    TC_TOK_IMPORT,      /* 'import' 关键字 */
    TC_TOK_PROGRAM,     /* '#program' 模块指令 */
    TC_TOK_LIB,         /* '#lib' 模块指令 */
    TC_TOK_NULLPTR,     /* 'nullptr' 关键字 */
    TC_TOK_AT,          /* '@' */
    TC_TOK_LT,          /* '<'（类型参数） */
    TC_TOK_GT,          /* '>'（类型参数） */
    TC_TOK_DOT,         /* '.'（Self.x / a.b） */
    TC_TOK_PADDING,     /* 'padding'（@padding） */
    TC_TOK_SEMICOLON    /* ';'（兼具语句结束与行注释作用） */
} TcTokenKind;

/** 单个 Token 的表示：种类 + 源码位置 + 语义值联合体 */
typedef struct {
    TcTokenKind kind;
    const char *start;  /* 源码中的起始位置（仅引用） */
    size_t length;      /* 长度 */
    int line;
    int column;
    union {
        TcTypeKind int_type;
        TcArithOp arith_op;
        TcUnaryOp unary_op;
        TcCompareOp compare_op;
        TcLogicOp logic_op;
        TcBitwiseOp bitwise_op;
        TcShiftOp shift_op;
        TcFormatSpec format_spec;
        TcLiteral literal;
    } u;
} TcToken;

/** Token 动态数组，由 tc_tokenize_line 逐行填充 */
typedef struct {
    TcToken *items;
    size_t count;
    size_t capacity;
} TcTokenList;

void tc_token_list_init(TcTokenList *list);
void tc_token_list_free(TcTokenList *list);

/**
 * 对一行源码进行词法分析。
 * @param line    待分析的源码行（不包含换行符）
 * @param line_no 当前行号（1-based）
 * @param out     输出 Token 列表（始终包含 TC_TOK_EOF 结尾）
 * @param diag    诊断对象
 * @return 成功返回 0；词法错误返回 -1 并设置 diag
 */
int tc_tokenize_line(const char *line, int line_no, TcTokenList *out,
                     TcDiagnostic *diag);

/** Token 种类名称（调试/诊断用） */
const char *tc_token_kind_name(TcTokenKind kind);

#endif
