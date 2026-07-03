/*
 * tc_types.h — TC-VM 核心类型定义
 *
 * 本头文件定义 TC 虚拟机在整个编译/执行流水线中共享的数据结构：
 * 整数类型、运算符、AST 节点（语句/表达式）、符号表、运行时值及错误种类。
 * 各模块（Lexer / Parser / Analyzer / Executor）均依赖此处的统一表示。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_TYPES_H
#define TC_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* INT64_MIN 的绝对值（9223372036854775808），即 2^63 */
#define TC_INT64_MIN_ABS_MAGNITUDE 9223372036854775808ULL

/* TC 语言支持的 8 种定宽整数类型 */
typedef enum {
    TC_INT8,
    TC_UINT8,
    TC_INT16,
    TC_UINT16,
    TC_INT32,
    TC_UINT32,
    TC_INT64,
    TC_UINT64
} TcIntType;

/*
 * 算术溢出处理模式：
 *   TC_ARITH_STRICT — 有符号溢出时报 TC_ERR_INTEGER_OVERFLOW
 *   TC_ARITH_WRAP   — 按目标类型位宽做环绕
 */
typedef enum {
    TC_ARITH_STRICT,
    TC_ARITH_WRAP
} TcWrapMode;

/*
 * 类型转换截断模式（truncate 关键字）：
 *   TC_TRUNC_STRICT   — 不可表示时报 TC_ERR_CAST_OVERFLOW
 *   TC_TRUNC_TRUNCATE — 按位模式截断/扩展，不报错
 */
typedef enum {
    TC_TRUNC_STRICT,
    TC_TRUNC_TRUNCATE
} TcTruncateMode;

/* 内建算术/取模运算符，对应源码中的 add / sub / mul / div / mod 关键字 */
typedef enum {
    TC_ADD,
    TC_SUB,
    TC_MUL,
    TC_DIV,
    TC_MOD
} TcArithOp;

/* 符号种类：变量或 let 常量 */
typedef enum {
    TC_SYM_VARIABLE,
    TC_SYM_CONSTANT
} TcSymKind;

/* 诊断错误种类，与语言标准中定义的可观测错误一一对应 */
typedef enum {
    TC_ERR_SYNTAX,
    TC_ERR_UNDEFINED_VARIABLE,
    TC_ERR_DUPLICATE_DEFINITION,
    TC_ERR_TYPE_MISMATCH,
    TC_ERR_LITERAL_OUT_OF_RANGE,
    TC_ERR_LITERAL_TYPE,
    TC_ERR_OVERFLOW_MODE,
    TC_ERR_KEYWORD,
    TC_ERR_CONSTANT_ASSIGNMENT,
    TC_ERR_CONSTANT_EXPRESSION,
    TC_ERR_DIVISION_BY_ZERO,
    TC_ERR_INTEGER_OVERFLOW,
    TC_ERR_CAST_OVERFLOW,
    TC_ERR_IO
} TcErrorKind;

/* 编译警告种类（不阻止执行） */
typedef enum {
    TC_WARN_UNINITIALIZED_VARIABLE
} TcWarningKind;

/*
 * 整数字面量词法表示（解析阶段暂存，Analyzer 按上下文类型校验）。
 * magnitude 为绝对值；negative 表示负号前缀；unsigned_suffix 表示 u/U 后缀。
 */
typedef struct {
    uint64_t magnitude;
    int negative;
    int unsigned_suffix;
} TcLiteral;

/* 算术运算的操作数：变量引用或整数字面量 */
typedef enum {
    TC_OPERAND_VAR,
    TC_OPERAND_LIT
} TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        char *name;
        TcLiteral lit;
    } u;
} TcOperand;

typedef enum {
    TC_RHS_LIT,
    TC_RHS_ARITH,
    TC_RHS_CAST
} TcRhsKind;

typedef struct {
    TcRhsKind kind;
    union {
        TcLiteral lit;
        struct {
            TcArithOp op;
            TcIntType type;
            TcWrapMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } arith;
        struct {
            TcIntType target;
            TcTruncateMode mode;
            char *source;
        } cast;
    } u;
} TcRhs;

typedef enum {
    TC_STMT_VAR_DEF,
    TC_STMT_CONST_DEF,
    TC_STMT_ASSIGN,
    TC_STMT_WRITE,
    TC_STMT_WRITELN,
    TC_STMT_READ
} TcStmtKind;

typedef struct {
    int line;
    char *name;
    TcIntType type;
    int has_rhs;
    TcRhs rhs;
} TcVarDef;

typedef struct {
    int line;
    char *name;
    TcIntType type;
    TcRhs rhs;
} TcConstDef;

typedef struct {
    int line;
    char *name;
    TcRhs rhs;
} TcAssign;

typedef struct {
    int line;
    TcIntType type;
    TcOperand operand;
} TcIoWrite;

typedef struct {
    int line;
    TcIntType type;
    char *name;
} TcRead;

typedef struct {
    TcStmtKind kind;
    union {
        TcVarDef var_def;
        TcConstDef const_def;
        TcAssign assign;
        TcIoWrite io_write;
        TcRead io_read;
    } u;
} TcStatement;

typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcProgram;

typedef struct {
    TcIntType type;
    uint64_t bits;
} TcValue;

typedef struct {
    char *name;
    TcIntType type;
    int slot;
    int def_line;
    int def_stmt_index;
    TcSymKind sym_kind;
    int initialized;
    int has_const_value;
    TcValue const_value;
} TcSymbol;

typedef struct {
    TcSymbol *symbols;
    size_t count;
    size_t capacity;
} TcSymbolTable;

typedef struct {
    TcWarningKind kind;
    char *message;
    int line;
} TcWarning;

typedef struct {
    TcWarning *items;
    size_t count;
    size_t capacity;
} TcWarningList;

typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
    TcWarningList warnings;
} TcTypedProgram;

typedef struct {
    TcErrorKind kind;
    char *message;
    char *filename;
    char *snippet;
    const char *source;
    int line;
    int column;
} TcDiagnostic;

#define TC_COLUMN_UNKNOWN (-1)

int tc_type_bit_width(TcIntType type);
int tc_type_is_signed(TcIntType type);
int tc_type_parse(const char *text, TcIntType *out);
int tc_arith_op_parse(const char *text, TcArithOp *out);
const char *tc_error_kind_name(TcErrorKind kind);
const char *tc_warning_kind_name(TcWarningKind kind);
const char *tc_int_type_name(TcIntType type);

#endif
