/*
 * tc_parser.h — 语法分析器接口
 *
 * 将 Token 流解析为 TcStatement（变量/常量定义、赋值、I/O、if 控制流等）。
 * AST 释放与 TcProgram 管理见 tc_parser_free.h；RHS 解析见 tc_parser_rhs.h。
 *
 * TC 语言语法概要（分号可选）：
 *   var id: type [= rhs]        变量定义
 *   let id: type = literal      常量定义
 *   id = rhs                    赋值
 *   write/writeln(type [,fmt,] operand)  输出
 *   read(type, id)              输入
 *   if cond then / 缩进块 / [else 块] / end
 */
#ifndef TC_PARSER_H
#define TC_PARSER_H

#include "tc_types.h"
#include "tc_lexer.h"
#include "tc_parser_free.h"

/** 递归 RHS 解析最大深度，超过此深度报 "expression too complex" */
#define TC_PARSER_MAX_DEPTH 256

/**
 * Parser 上下文，承载递归深度计数器等解析状态。
 * 调用方在每次 tc_parse_statement 调用前初始化 { .depth = 0 }。
 */
typedef struct {
    int depth; /* 当前 RHS 递归深度 */
} TcParserCtx;

/** 缩进上下文：文件级或当前 if 块的基准缩进 */
typedef struct {
    int base_column;     /* 当前块级基准缩进（列数） */
    int indent_width;    /* 每级缩进宽度（空格数；制表符模式下为 1） */
    char indent_char;    /* ' ' 或 '\t' */
} TcIndentCtx;

/** 源文件一行：行号、前导缩进列数、行文本副本、词法 Token 流 */
typedef struct {
    int line_no;
    int indent;
    char *text; /* 行文本副本，供 token->start 在 parse 阶段有效 */
    TcTokenList tokens;
} TcSourceLine;

/** 文件级缩进配置：一级恒为 4 个 ASCII 空格（U+0020），行首不允许制表符 */
typedef struct {
    int indent_width; /* 每级缩进宽度（恒为 4 空格） */
} TcFileIndent;

typedef TcBlockKind TcParserBlockKind;

/**
 * 解析单条语句（不含 if；if 由 tc_parse_source_to_program 多行处理）。
 * @param ctx     Parser 上下文（含递归深度计数器）
 * @param tokens  Token 列表（来自 tc_tokenize_line，含行尾 TC_TOK_EOF）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcStatement
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1 并设置 diag
 */
int tc_parse_statement(TcParserCtx *ctx, const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag);

/**
 * 将完整源文本解析为 TcProgram（两遍：收集行 + 含 if 的多行 parse）。
 * @param source  以 '\\0' 结尾的源文本
 * @param program 输出程序（调用前无需 init；失败时内部 tc_program_free）
 * @param diag    诊断对象
 * @return 成功返回 0；词法/语法/缩进错误返回 -1
 */
int tc_parse_source_to_program(const char *source, TcProgram *program, TcDiagnostic *diag);

/**
 * 解析 if-then-[else]-end 语句（含缩进检查）。
 * @param ctx          Parser 上下文
 * @param lines        非空行序列
 * @param line_count   行数
 * @param index        当前行下标（入参指向 if 行；成功出参指向 end 下一行）
 * @param file_indent  文件级缩进配置
 * @param out          输出 TC_STMT_IF 语句
 * @param diag         诊断对象
 * @return 成功返回 0；失败返回 -1
 */
int tc_parse_if_stmt(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count, size_t *index,
                     const TcFileIndent *file_indent, TcStatement *out, TcDiagnostic *diag);

/** 解析 while-then-end 语句（含缩进检查）。 */
int tc_parse_while_stmt(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count, size_t *index,
                        const TcFileIndent *file_indent, TcStatement *out, TcDiagnostic *diag);

#endif /* TC_PARSER_H */
