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
 * 算术溢出处理模式（仅对有符号运算生效；无符号运算始终按位宽截断）：
 *   TC_STRICT   — 溢出时报告 TC_ERR_INTEGER_OVERFLOW
 *   TC_OVERFLOW — 按目标类型位宽做环绕（wrap-around）
 */
typedef enum {
    TC_STRICT,
    TC_OVERFLOW
} TcOverflowMode;

/* 内建算术/取模运算符，对应源码中的 add / sub / mul / div / mod 关键字 */
typedef enum {
    TC_ADD,
    TC_SUB,
    TC_MUL,
    TC_DIV,
    TC_MOD
} TcArithOp;

/* 诊断错误种类，与语言标准中定义的可观测错误一一对应 */
typedef enum {
    TC_ERR_SYNTAX,              /* 词法/语法错误 */
    TC_ERR_UNDEFINED_VARIABLE,  /* 引用未定义变量 */
    TC_ERR_DUPLICATE_DEFINITION,/* 重复定义同名变量 */
    TC_ERR_TYPE_MISMATCH,       /* 类型不匹配 */
    TC_ERR_LITERAL_OUT_OF_RANGE,  /* 字面量超出目标类型范围 */
    TC_ERR_DIVISION_BY_ZERO,      /* 除零或模零 */
    TC_ERR_INTEGER_OVERFLOW,      /* 有符号 strict 模式算术溢出 */
    TC_ERR_OVERFLOW_MODE,         /* 非法 overflow 模式组合（如 div/mod 带 overflow） */
    TC_ERR_CAST_OVERFLOW,         /* strict 模式下类型转换溢出 */
    TC_ERR_IO                     /* read 输入失败（非法格式、超范围、EOF） */
} TcErrorKind;

/* 算术运算的操作数：变量引用或整数字面量 */
typedef enum {
    TC_OPERAND_VAR,  /* 变量名 */
    TC_OPERAND_LIT   /* 无符号 64 位字面量（解析阶段暂存，语义阶段再校验范围） */
} TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        char *name;     /* kind == TC_OPERAND_VAR 时有效 */
        uint64_t lit;   /* kind == TC_OPERAND_LIT 时有效 */
    } u;
} TcOperand;

/*
 * 语句右值（RHS）的三种形式：
 *   TC_RHS_LIT   — 整数字面量
 *   TC_RHS_ARITH — 内建算术表达式，如 add(int32, a, b)
 *   TC_RHS_CAST  — 类型转换，如 cast(uint8, overflow, x)
 */
typedef enum {
    TC_RHS_LIT,
    TC_RHS_ARITH,
    TC_RHS_CAST
} TcRhsKind;

typedef struct {
    TcRhsKind kind;
    union {
        uint64_t lit;
        struct {
            TcArithOp op;
            TcIntType type;       /* 运算的目标整数类型 */
            TcOverflowMode mode;  /* strict 或 overflow */
            TcOperand lhs;
            TcOperand rhs;
        } arith;
        struct {
            TcIntType target;     /* 转换目标类型 */
            TcOverflowMode mode;
            char *source;         /* 源变量名 */
        } cast;
    } u;
} TcRhs;

/* 语句种类：变量定义、赋值、I/O */
typedef enum {
    TC_STMT_VAR_DEF,  /* var name: type = rhs */
    TC_STMT_ASSIGN,   /* name = rhs */
    TC_STMT_WRITE,    /* write(type, operand) */
    TC_STMT_WRITELN,  /* writeln(type, operand) */
    TC_STMT_READ      /* read(type, identifier) */
} TcStmtKind;

/* 变量定义语句：var x: int32 = ... */
typedef struct {
    int line;
    char *name;
    TcIntType type;
    TcRhs rhs;
} TcVarDef;

/* 赋值语句：x = ... */
typedef struct {
    int line;
    char *name;
    TcRhs rhs;
} TcAssign;

/* write / writeln 语句：write(type, operand) */
typedef struct {
    int line;
    TcIntType type;
    TcOperand operand;
} TcIoWrite;

/* read 语句：read(type, identifier) */
typedef struct {
    int line;
    TcIntType type;
    char *name;
} TcRead;

/*
 * 单条语句的通用表示（StatementRecord）。
 * 与源文件中一行语句 1:1 对应，是 Executor 直接 dispatch 的单位。
 */
typedef struct {
    TcStmtKind kind;
    union {
        TcVarDef var_def;
        TcAssign assign;
        TcIoWrite io_write;
        TcRead io_read;
    } u;
} TcStatement;

/* 程序 = 按源文件顺序排列的语句列表 */
typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcProgram;

/*
 * 符号表条目：记录变量名、静态类型、运行时槽位索引及定义行号。
 * slot 用于 Executor 在变量槽数组中定位该变量的 TcValue。
 */
typedef struct {
    char *name;
    TcIntType type;
    int slot;
    int def_line;
} TcSymbol;

typedef struct {
    TcSymbol *symbols;
    size_t count;
    size_t capacity;
} TcSymbolTable;

/* Analyzer 产出：已类型化的程序 + 全局符号表 */
typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
} TcTypedProgram;

/*
 * 运行时值：以无符号位模式（bits）存储，配合 type 解释为有符号或无符号。
 * 所有算术结果最终都归一化为目标类型的位宽掩码。
 */
typedef struct {
    TcIntType type;
    uint64_t bits;
} TcValue;

/* 诊断信息：错误种类 + 人类可读消息 + 源位置 + 可选源文件上下文 */
typedef struct {
    TcErrorKind kind;
    char *message;
    char *filename;      /* 源文件路径（堆分配，由 diagnostic 模块管理） */
    char *snippet;       /* 出错行源码副本（堆分配，设置诊断时捕获） */
    const char *source;  /* 完整源文本（非拥有指针，仅在 run 期间有效） */
    int line;
    int column;
} TcDiagnostic;

/* 列号未知时的占位值（如运行时错误无法精确定位列） */
#define TC_COLUMN_UNKNOWN (-1)

/* 类型/运算符工具函数（实现在 types.c） */

/**
 * @brief 返回整数类型的位宽
 * @param type 整数类型枚举值
 * @return 位宽值：8 / 16 / 32 / 64；非法类型返回 0
 */
int tc_type_bit_width(TcIntType type);

/**
 * @brief 判断类型是否为有符号整数
 * @param type 整数类型枚举值
 * @return 有符号类型返回 1；无符号类型返回 0
 */
int tc_type_is_signed(TcIntType type);

/**
 * @brief 将字符串解析为 TcIntType 枚举值
 * @param text 类型名字符串（如 "int8"、"uint32"）
 * @param out  输出参数，写入解析后的 TcIntType
 * @return 解析成功返回 1；无法识别返回 0
 */
int tc_type_parse(const char *text, TcIntType *out);

/**
 * @brief 将算术运算符关键字解析为 TcArithOp 枚举值
 * @param text 运算符关键字（"add"/"sub"/"mul"/"div"/"mod"）
 * @param out  输出参数，写入解析后的 TcArithOp
 * @return 解析成功返回 1；无法识别返回 0
 */
int tc_arith_op_parse(const char *text, TcArithOp *out);

/**
 * @brief 将错误种类枚举转为对外展示的名称
 * @param kind 错误种类枚举值
 * @return 错误名称字符串（如 "SyntaxError"、"DivisionByZero"）
 */
const char *tc_error_kind_name(TcErrorKind kind);

/**
 * @brief 将 TcIntType 转为源码中的类型名字符串
 * @param type 整数类型枚举值
 * @return 类型名字符串（如 "int8"、"uint32"）；非法类型返回 "unknown"
 */
const char *tc_int_type_name(TcIntType type);

#endif
