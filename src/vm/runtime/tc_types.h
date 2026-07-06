/*
 * tc_types.h — 贯穿 TC 全流水线的共享类型定义
 *
 * 本头文件是 TC 虚拟机唯一的数据契约，涵盖：
 *   - 8 种定宽整数类型及运算符枚举
 *   - AST 节点：语句（TcStatement）、右值（TcRhs）、操作数（TcOperand）
 *   - 运行时值（TcValue）、符号表（TcSymbol）、诊断（TcDiagnostic）
 *   - 程序表示（TcProgram / TcTypedProgram）
 *
 * 各模块（Lexer / Parser / Analyzer / Executor / AOT）均依赖此处的统一表示，
 * 不得将模块私有类型放入本文件。
 */
#ifndef TC_TYPES_H
#define TC_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* INT64_MIN 的绝对值 2^63，用于字面量范围检查与 read 输入解析 */
#define TC_INT64_MIN_ABS_MAGNITUDE 9223372036854775808ULL

/* ------------------------------------------------------------------ */
/*  整数类型 & 运算符枚举                                               */
/* ------------------------------------------------------------------ */

/** TC 语言支持的定宽整数类型与 bool */
typedef enum {
    TC_INT8,
    TC_UINT8,
    TC_INT16,
    TC_UINT16,
    TC_INT32,
    TC_UINT32,
    TC_INT64,
    TC_UINT64,
    TC_BOOL
} TcIntType;

/**
 * 算术溢出处理模式：
 *   TC_ARITH_STRICT — 有符号溢出时报 TC_ERR_INTEGER_OVERFLOW
 *   TC_ARITH_WRAP   — 按目标类型位宽做二进制环绕
 */
typedef enum {
    TC_ARITH_STRICT,
    TC_ARITH_WRAP
} TcWrapMode;

/**
 * 类型转换截断模式（truncate 关键字）：
 *   TC_TRUNC_STRICT   — 不可表示时报 TC_ERR_CAST_OVERFLOW
 *   TC_TRUNC_TRUNCATE — 按位模式截断/扩展，永不报错
 */
typedef enum {
    TC_TRUNC_STRICT,
    TC_TRUNC_TRUNCATE
} TcTruncateMode;

/* 内建双目算术/取模运算符，对应 add / sub / mul / div / mod 关键字 */
typedef enum {
    TC_ADD,
    TC_SUB,
    TC_MUL,
    TC_DIV,
    TC_MOD
} TcArithOp;

/* 内建单目算术运算符：abs / neg */
typedef enum {
    TC_UNARY_ABS,
    TC_UNARY_NEG
} TcUnaryOp;

/* 内建比较运算符：eq / ne / lt / le / gt / ge，返回 bool */
typedef enum {
    TC_CMP_EQ,
    TC_CMP_NE,
    TC_CMP_LT,
    TC_CMP_LE,
    TC_CMP_GT,
    TC_CMP_GE
} TcCompareOp;

/* 内建逻辑运算符：and / or / not，操作数与结果均为 bool */
typedef enum {
    TC_LOGIC_AND,
    TC_LOGIC_OR,
    TC_LOGIC_NOT
} TcLogicOp;

/* 内建按位运算符：and / or / xor（整数类型参数；not 见 TC_RHS_BITWISE_UN） */
typedef enum {
    TC_BIT_AND,
    TC_BIT_OR,
    TC_BIT_XOR
} TcBitwiseOp;

/* 内建移位运算符：shl / shr */
typedef enum {
    TC_SHIFT_SHL,
    TC_SHIFT_SHR
} TcShiftOp;

/**
 * I/O 格式化符号。
 * TC_FMT_NONE 表示无格式，按类型默认输出（有符号用 %d，无符号用 %u）。
 */
typedef enum {
    TC_FMT_NONE = 0,
    TC_FMT_D,
    TC_FMT_I,
    TC_FMT_U,
    TC_FMT_X,
    TC_FMT_XU,
    TC_FMT_O,
    TC_FMT_B,
    TC_FMT_T
} TcFormatSpec;

/** 符号种类：变量或 let 常量 */
typedef enum {
    TC_SYM_VARIABLE,
    TC_SYM_CONSTANT
} TcSymKind;

/* ------------------------------------------------------------------ */
/*  错误与警告枚举                                                     */
/* ------------------------------------------------------------------ */

/** 诊断错误种类，与 TC 语言标准定义的可观测错误一一对应 */
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
    TC_ERR_CONSTANT_CIRCULAR,
    TC_ERR_CONSTANT_OVERFLOW,
    TC_ERR_CONSTANT_DIV_ZERO,
    TC_ERR_CONSTANT_CAST_OVERFLOW,
    TC_ERR_COMPARISON_TYPE_MISMATCH,
    TC_ERR_FORMAT_STRING,
    TC_ERR_FORMAT_TYPE_MISMATCH,
    TC_ERR_OPERAND_COUNT,
    TC_ERR_DIVISION_BY_ZERO,
    TC_ERR_INTEGER_OVERFLOW,
    TC_ERR_CAST_OVERFLOW,
    TC_ERR_IO
} TcErrorKind;

/** 编译警告种类（不阻止执行，仅输出 warning 信息） */
typedef enum {
    TC_WARN_UNINITIALIZED_VARIABLE
} TcWarningKind;

/* ------------------------------------------------------------------ */
/*  整数字面量词法表示                                                  */
/* ------------------------------------------------------------------ */

/**
 * 整数字面量的词法阶段暂存表示。
 * magnitude 为绝对值；negative 表示负号前缀；unsigned_suffix 表示 u/U 后缀。
 * Analyzer 按上下文类型做最终范围校验。
 */
typedef struct {
    uint64_t magnitude;
    int negative;
    int unsigned_suffix;
    int is_bool; /* 1 表示 true/false 布尔字面量；magnitude 为 0/1 */
} TcLiteral;

/* ------------------------------------------------------------------ */
/*  AST 节点：操作数 → 右值 → 语句                                      */
/* ------------------------------------------------------------------ */

/** 算术/一元运算的操作数：变量引用或整数字面量 */
typedef enum {
    TC_OPERAND_VAR,
    TC_OPERAND_LIT
} TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        char *name;     /* TC_OPERAND_VAR：变量名，堆分配 */
        TcLiteral lit;  /* TC_OPERAND_LIT：字面量 */
    } u;
} TcOperand;

/** 右值表达式种类 */
typedef enum {
    TC_RHS_LIT,        /* 字面量 */
    TC_RHS_CONST_REF,  /* let 常量引用（编译期） */
    TC_RHS_ARITH,      /* 双目算术（add/sub/mul/div/mod） */
    TC_RHS_UNARY,      /* 单目运算（abs/neg） */
    TC_RHS_COMPARE,    /* 比较运算（eq/ne/lt/le/gt/ge） */
    TC_RHS_LOGIC_BIN,   /* 双目逻辑（and/or） */
    TC_RHS_LOGIC_UN,    /* 单目逻辑（not） */
    TC_RHS_BITWISE_BIN, /* 双目按位（and/or/xor，整数类型参数） */
    TC_RHS_BITWISE_UN,  /* 单目按位（not，整数类型参数） */
    TC_RHS_SHIFT,       /* 移位（shl/shr；shl 可选 wrap） */
    TC_RHS_CAST,        /* 类型转换（运行时：源为变量） */
    TC_RHS_CONST_CAST   /* 编译期 cast（源为常量操作数） */
} TcRhsKind;

typedef struct {
    TcRhsKind kind;
    union {
        TcLiteral lit;           /* TC_RHS_LIT */
        struct {
            TcArithOp op;
            TcIntType type;
            TcWrapMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } arith;                 /* TC_RHS_ARITH */
        struct {
            TcUnaryOp op;
            TcIntType type;
            TcWrapMode mode;
            TcOperand operand;
        } unary;                 /* TC_RHS_UNARY */
        struct {
            TcCompareOp op;
            TcIntType type;
            TcOperand lhs;
            TcOperand rhs;
        } compare;               /* TC_RHS_COMPARE */
        struct {
            TcLogicOp op;
            TcOperand lhs;
            TcOperand rhs;
        } logic_bin;             /* TC_RHS_LOGIC_BIN */
        struct {
            TcLogicOp op;
            TcOperand operand;
        } logic_un;              /* TC_RHS_LOGIC_UN */
        struct {
            TcBitwiseOp op;
            TcIntType type;
            TcOperand lhs;
            TcOperand rhs;
        } bitwise_bin;           /* TC_RHS_BITWISE_BIN */
        struct {
            TcIntType type;
            TcOperand operand;
        } bitwise_un;              /* TC_RHS_BITWISE_UN（恒为 not） */
        struct {
            TcShiftOp op;
            TcIntType type;
            TcWrapMode mode;     /* 仅 shl 使用；shr 恒为 TC_ARITH_STRICT */
            TcOperand value;
            TcOperand count;
        } shift;                 /* TC_RHS_SHIFT */
        struct {
            TcIntType target;
            TcTruncateMode mode;
            char *source;        /* 源变量名，堆分配 */
        } cast;                  /* TC_RHS_CAST */
        struct {
            TcIntType target;
            TcOperand source;    /* 编译期操作数（字面量或 let 引用） */
        } const_cast;            /* TC_RHS_CONST_CAST */
        struct {
            char *name;          /* 已定义的 let 常量名，堆分配 */
        } const_ref;             /* TC_RHS_CONST_REF */
    } u;
} TcRhs;

/** 语句种类 */
typedef enum {
    TC_STMT_VAR_DEF,     /* var 定义 */
    TC_STMT_CONST_DEF,   /* let 常量定义 */
    TC_STMT_ASSIGN,      /* 赋值 */
    TC_STMT_WRITE,       /* write 输出 */
    TC_STMT_WRITELN,     /* writeln 输出 */
    TC_STMT_READ         /* read 输入 */
} TcStmtKind;

typedef struct {
    int line;
    char *name;      /* 变量名，堆分配 */
    TcIntType type;
    int has_rhs;     /* 是否有初始化表达式 */
    TcRhs rhs;
} TcVarDef;

typedef struct {
    int line;
    char *name;      /* 常量名，堆分配 */
    TcIntType type;
    TcRhs rhs;       /* let 初始化必须为编译期常量表达式（由 Analyzer 确保） */
} TcConstDef;

typedef struct {
    int line;
    char *name;      /* 赋值目标变量名，堆分配 */
    TcRhs rhs;
} TcAssign;

typedef struct {
    int line;
    TcIntType type;
    TcFormatSpec fmt;       /* TC_FMT_NONE 表示默认十进制输出 */
    TcOperand operand;
} TcIoWrite;

typedef struct {
    int line;
    TcIntType type;
    char *name;      /* 读取目标变量名，堆分配 */
} TcRead;

/** 统一语句表示，kind 决定活跃的 u 成员 */
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

/* ------------------------------------------------------------------ */
/*  程序 & 符号表 & 运行时值                                            */
/* ------------------------------------------------------------------ */

/** 未类型化的原始程序（Parser 产出，Analyzer 消费） */
typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcProgram;

/** 运行时值：类型 + 位模式，统一 uint64_t 存储 */
typedef struct {
    TcIntType type;
    uint64_t bits;
} TcValue;

/** 符号表条目：一个变量或 let 常量的元信息 */
typedef struct {
    char *name;          /* 堆分配 */
    TcIntType type;
    int slot;            /* 运行时变量槽索引 */
    int def_line;
    int def_stmt_index;  /* 定义位置对应的语句序号 */
    TcSymKind sym_kind;
    int initialized;     /* 定义时是否有初始化值 */
    int has_const_value; /* let 常量编译期求值的结果是否有效 */
    TcValue const_value; /* let 常量编译期求值结果 */
} TcSymbol;

typedef struct {
    TcSymbol *symbols;
    size_t count;
    size_t capacity;
} TcSymbolTable;

/* ------------------------------------------------------------------ */
/*  警告列表                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    TcWarningKind kind;
    char *message;   /* 堆分配 */
    int line;
} TcWarning;

typedef struct {
    TcWarning *items;
    size_t count;
    size_t capacity;
} TcWarningList;

/* ------------------------------------------------------------------ */
/*  类型化程序（Analyzer 产出）与诊断                                   */
/* ------------------------------------------------------------------ */

/** Analyzer 分析通过后的完整程序：语句 + 符号表 + 警告 */
typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
    TcWarningList warnings;
} TcTypedProgram;

/**
 * 单槽诊断对象（fail-fast 模式下仅保存第一条错误）。
 * 调用方负责保证 source 在 tc_diagnostic_print 前有效。
 */
typedef struct {
    TcErrorKind kind;
    char *message;    /* 堆分配 */
    char *filename;   /* 堆分配 */
    char *snippet;    /* 堆分配，出错行源码 */
    const char *source; /* 仅引用，调用方管理生命周期 */
    int line;
    int column;
} TcDiagnostic;

/* 无列号时的占位值 */
#define TC_COLUMN_UNKNOWN (-1)

/* ------------------------------------------------------------------ */
/*  公共工具函数声明                                                    */
/* ------------------------------------------------------------------ */

int tc_type_bit_width(TcIntType type);
int tc_type_is_signed(TcIntType type);
int tc_type_is_bool(TcIntType type);
int tc_type_is_integer(TcIntType type);
int tc_type_parse(const char *text, TcIntType *out);
int tc_arith_op_parse(const char *text, TcArithOp *out);
int tc_unary_op_parse(const char *text, TcUnaryOp *out);
int tc_compare_op_parse(const char *text, TcCompareOp *out);
int tc_logic_op_parse(const char *text, TcLogicOp *out);
int tc_bitwise_op_parse(const char *text, TcBitwiseOp *out);
int tc_shift_op_parse(const char *text, TcShiftOp *out);
int tc_format_spec_parse(const char *text, TcFormatSpec *out);
const char *tc_bitwise_op_name(TcBitwiseOp op);
const char *tc_shift_op_name(TcShiftOp op);
const char *tc_format_spec_name(TcFormatSpec fmt);
const char *tc_error_kind_name(TcErrorKind kind);
const char *tc_warning_kind_name(TcWarningKind kind);
const char *tc_int_type_name(TcIntType type);

#endif
