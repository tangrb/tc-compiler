/*
 * tc_lexer.h — 词法分析器接口
 *
 * 将 TC 源文件的一行文本切分为 Token 流。
 * 每行独立词法化，Parser 按行消费 Token 列表。
 * Token 携带种类、原文切片、行号/列号，以及解析后的类型/运算符附加信息。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_LEXER_H
#define TC_LEXER_H

#include "tc_types.h"

/* Token 种类：关键字、类型名、运算符、标识符、字面量及标点符号 */
typedef enum {
    TC_TOK_EOF,         /* 行尾结束标记 */
    TC_TOK_VAR,         /* var */
    TC_TOK_INT_TYPE,    /* int8 / uint8 / ... */
    TC_TOK_ARITH_OP,    /* add / sub / mul / div / mod */
    TC_TOK_CAST,        /* cast */
    TC_TOK_OVERFLOW,    /* overflow */
    TC_TOK_WRITE,       /* write */
    TC_TOK_WRITELN,     /* writeln */
    TC_TOK_READ,        /* read */
    TC_TOK_IDENTIFIER,  /* 用户变量名 */
    TC_TOK_INTEGER,     /* 十进制整数字面量 */
    TC_TOK_COLON,       /* : */
    TC_TOK_EQUAL,       /* = */
    TC_TOK_COMMA,       /* , */
    TC_TOK_LPAREN,      /* ( */
    TC_TOK_RPAREN,      /* ) */
    TC_TOK_SEMICOLON    /* ; （可选语句终结符） */
} TcTokenKind;

/*
 * 单个词法单元。
 * start/length 指向源行内的子串（不拷贝），由调用方保证生命周期。
 * int_type / arith_op 仅在对应 kind 时有效。
 */
typedef struct {
    TcTokenKind kind;
    const char *start;
    size_t length;
    int line;
    int column;
    TcIntType int_type;
    TcArithOp arith_op;
} TcToken;

/* 动态数组形式的 Token 列表 */
typedef struct {
    TcToken *items;
    size_t count;
    size_t capacity;
} TcTokenList;

/**
 * @brief 初始化 Token 列表为空状态
 * @param list 待初始化的 Token 列表指针
 */
void tc_token_list_init(TcTokenList *list);

/**
 * @brief 释放 Token 列表的 items 数组
 * @param list 待释放的 Token 列表指针
 */
void tc_token_list_free(TcTokenList *list);

/**
 * @brief 对单行源文本进行词法分析，结果追加到 out
 * @param line    源行文本（不含换行符）
 * @param line_no 当前行号（1-based）
 * @param out     输出参数，Token 列表
 * @param diag    诊断对象
 * @return 成功返回 0；遇到非法字符或超大字面量返回 -1 并设置 diag
 */
int tc_tokenize_line(const char *line, int line_no, TcTokenList *out,
                     TcDiagnostic *diag);

#endif
