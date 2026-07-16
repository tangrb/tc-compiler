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
/*  类型 & 运算符枚举                                                   */
/* ------------------------------------------------------------------ */

/**
 * TC 语言支持的全部标量类型：定宽整数、bool、浮点。
 *
 * TC_BOOL 与 int8～uint64 共用本枚举以便统一位宽查询与 I/O 处理，
 * 但概念上 bool 属于独立类型类别（见语言标准 §3.1）。
 * tc_type_is_integer() 仅对 TC_INT8～TC_UINT64 返回真，不含 TC_BOOL。
 */
typedef enum {
    TC_INT8,
    TC_UINT8,
    TC_INT16,
    TC_UINT16,
    TC_INT32,
    TC_UINT32,
    TC_INT64,
    TC_UINT64,
    TC_BOOL,
    TC_FLOAT32,
    TC_FLOAT64
} TcType;

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
 * 浮点运算模式：
 *   TC_FLOAT_STRICT — 检测 IEEE 754 异常并报浮点错误
 *   TC_FLOAT_IEEE   — 遵循 IEEE 754，返回 ±inf/nan，不报错
 *   TC_FLOAT_WRAP   — Parser 非法模式哨兵；成功 typed program 中不可出现
 */
typedef enum {
    TC_FLOAT_STRICT,
    TC_FLOAT_IEEE,
    TC_FLOAT_WRAP
} TcFloatMode;

/**
 * 类型转换截断模式（truncate 关键字）：
 *   TC_TRUNC_STRICT   — 数值转换，不可表示时报 TC_ERR_CAST_OVERFLOW
 *   TC_TRUNC_TRUNCATE — 仅整数到更窄整数，保留低位
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
    TC_FMT_T,
    TC_FMT_F,
    TC_FMT_E,
    TC_FMT_EU,
    TC_FMT_G,
    TC_FMT_GU
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
    TC_ERR_KEYWORD,
    TC_ERR_CONSTANT_ASSIGNMENT,
    TC_ERR_CONSTANT_EXPRESSION,
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
    TC_ERR_IO,
    TC_ERR_OUT_OF_MEMORY,         /* 内存分配失败 */
    TC_ERR_INDENT_MIXED,          /* 混用空格与制表符 */
    TC_ERR_INDENT_INSUFFICIENT,   /* 块内缩进不足 */
    TC_ERR_INDENT_ELSE_END,       /* else/end 缩进与 if 不一致 */
    TC_ERR_MISSING_END,           /* if 语句缺少 end */
    TC_ERR_ELSE_POSITION,         /* else 位置错误 */
    TC_ERR_CONDITION_TYPE,        /* if 条件结果不是 bool */
    TC_ERR_FLOAT_OVERFLOW,        /* 严格模式浮点上溢 */
    TC_ERR_FLOAT_UNDERFLOW,       /* 严格模式浮点下溢 */
    TC_ERR_FLOAT_INVALID,         /* 严格模式浮点无效操作（nan 等） */
    TC_ERR_MODE_MISMATCH,         /* ieee/wrap 用于非法上下文 */
    TC_ERR_UNINITIALIZED_VARIABLE,  /* §4.2 读取未初始化变量 */
    TC_ERR_LABEL_NOT_FOUND,         /* §4.8.3 goto 引用未定义标签 */
    TC_ERR_DUPLICATE_LABEL,         /* §4.8.3 同一作用域重定义标签 */
    TC_ERR_JUMP_INTO_BLOCK,         /* §4.8.3 跳入内层子块 */
    TC_ERR_JUMP_TO_SIBLING_BLOCK,   /* §4.8.3 跳入兄弟分支 */
    TC_ERR_VAR_MISSING_INIT,        /* §4.2 var 声明缺少初始化器 */
    TC_ERR_BITCAST_WIDTH,           /* §8.6 bitcast 源/目标位宽不等 */
    TC_ERR_LABEL_INSIDE_LOOP,       /* label 不得出现在 while 内 */
    TC_ERR_GOTO_INSIDE_LOOP,        /* goto 不得出现在 while 内 */
    TC_ERR_BREAK_OUTSIDE_LOOP,      /* break 必须出现在 while 内 */
    TC_ERR_CONTINUE_OUTSIDE_LOOP    /* continue 必须出现在 while 内 */
} TcErrorKind;

/*
 * 编译警告种类（不阻止执行，仅输出 warning 信息）。
 * v0.0.26：TC_WARN_UNINITIALIZED_VARIABLE 已升级为 TC_ERR_UNINITIALIZED_VARIABLE。
 * TcWarningKind / TcWarningList 保留空壳供未来警告类型使用。
 */
typedef enum {
    TC_WARN_NONE = 0  /* 占位；当前无活跃警告种类 */
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
    int is_bool;          /* 1 表示 true/false 布尔字面量；magnitude 为 0/1 */
    int is_float;         /* 1 表示浮点字面量；float_value 有效 */
    double float_value;   /* 浮点字面量双精度暂存（词法阶段） */
    int float32_suffix;   /* 1 表示 f/F 后缀 → 上下文须为 float32 */
} TcLiteral;

/* ------------------------------------------------------------------ */
/*  AST 节点：操作数 → 右值 → 语句                                      */
/* ------------------------------------------------------------------ */

/** 算术/一元运算的操作数：变量引用或整数字面量 */
typedef enum {
    TC_OPERAND_VAR,
    TC_OPERAND_LIT
} TcOperandKind;

/** Analyzer 持久化的绑定结果；Executor/AOT 不再按名称重新解析。 */
typedef struct {
    int resolved;
    int slot;             /* var 的固定运行时槽；let 为 -1 */
    int is_const;         /* let 常量绑定时为 1 */
    TcType type;          /* Analyzer 解析的声明类型 */
    uint64_t const_bits;  /* let 的规范化 TcValue.bits */
} TcResolvedBinding;

typedef struct {
    TcOperandKind kind;
    union {
        char *name;     /* TC_OPERAND_VAR：变量名，堆分配 */
        TcLiteral lit;  /* TC_OPERAND_LIT：字面量 */
    } u;
    TcResolvedBinding binding; /* TC_OPERAND_VAR 分析成功后有效 */
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
    TC_RHS_CAST,        /* 运行时数值转换（源为变量或字面量） */
    TC_RHS_CONST_CAST,  /* 编译期 cast（源为常量操作数） */
    TC_RHS_FLOAT_ARITH,   /* 浮点双目算术 */
    TC_RHS_FLOAT_UNARY,   /* 浮点单目运算 */
    TC_RHS_FLOAT_COMPARE, /* 浮点比较 */
    TC_RHS_BITCAST        /* 等宽整数/浮点位重解释 */
} TcRhsKind;

typedef struct {
    TcType target;
    TcType source_type;
    int source_type_resolved;
    TcOperand source;
} TcBitcastRhs;

typedef struct {
    TcType target;
    TcTruncateMode mode;
    TcType source_type;
    int source_type_resolved;
    TcOperand source;
} TcCastRhs;

typedef struct {
    TcRhsKind kind;
    union {
        TcLiteral lit;           /* TC_RHS_LIT */
        struct {
            TcArithOp op;
            TcType type;
            TcWrapMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } arith;                 /* TC_RHS_ARITH */
        struct {
            TcUnaryOp op;
            TcType type;
            TcWrapMode mode;
            TcOperand operand;
        } unary;                 /* TC_RHS_UNARY */
        struct {
            TcCompareOp op;
            TcType type;
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
            TcType type;
            TcOperand lhs;
            TcOperand rhs;
        } bitwise_bin;           /* TC_RHS_BITWISE_BIN */
        struct {
            TcType type;
            TcOperand operand;
        } bitwise_un;              /* TC_RHS_BITWISE_UN（恒为 not） */
        struct {
            TcShiftOp op;
            TcType type;
            TcWrapMode mode;     /* 仅 shl 使用；shr 恒为 TC_ARITH_STRICT */
            TcOperand value;
            TcOperand count;
        } shift;                 /* TC_RHS_SHIFT */
        TcCastRhs cast;          /* TC_RHS_CAST */
        TcCastRhs const_cast;    /* TC_RHS_CONST_CAST */
        struct {
            char *name;          /* 已定义的 let 常量名，堆分配 */
            TcResolvedBinding binding; /* 分析成功后的直接绑定 */
        } const_ref;             /* TC_RHS_CONST_REF */
        struct {
            TcArithOp op;
            TcType type;      /* TC_FLOAT32 或 TC_FLOAT64 */
            TcFloatMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } float_arith;           /* TC_RHS_FLOAT_ARITH */
        struct {
            TcUnaryOp op;
            TcType type;
            TcFloatMode mode;
            TcOperand operand;
        } float_unary;           /* TC_RHS_FLOAT_UNARY */
        struct {
            TcCompareOp op;
            TcType type;
            TcFloatMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } float_compare;         /* TC_RHS_FLOAT_COMPARE */
        TcBitcastRhs bitcast;    /* TC_RHS_BITCAST */
    } u;
} TcRhs;

/** 语句种类 */
typedef enum {
    TC_STMT_VAR_DEF,     /* var 定义 */
    TC_STMT_CONST_DEF,   /* let 常量定义 */
    TC_STMT_ASSIGN,      /* 赋值 */
    TC_STMT_WRITE,       /* write 输出 */
    TC_STMT_WRITELN,     /* writeln 输出 */
    TC_STMT_READ,        /* read 输入 */
    TC_STMT_IF,          /* if-then-else 控制流 */
    TC_STMT_LABEL_DEF,   /* label name: 定义标签 */
    TC_STMT_GOTO,        /* goto name  无条件跳转 */
    TC_STMT_WHILE,       /* while-then-end 结构化循环 */
    TC_STMT_BREAK,       /* 退出最内层 while */
    TC_STMT_CONTINUE     /* 继续最内层 while */
} TcStmtKind;

typedef struct {
    int line;
    char *name;      /* 变量名，堆分配 */
    TcType type;
    TcRhs rhs;
    TcResolvedBinding binding; /* 定义对应的固定 slot */
} TcVarDef;

typedef struct {
    int line;
    char *name;      /* 常量名，堆分配 */
    TcType type;
    TcRhs rhs;       /* let 初始化必须为编译期常量表达式（由 Analyzer 确保） */
} TcConstDef;

typedef struct {
    int line;
    char *name;      /* 赋值目标变量名，堆分配 */
    TcRhs rhs;
    TcResolvedBinding binding; /* Analyzer 解析的赋值目标 */
} TcAssign;

typedef struct {
    int line;
    TcType type;
    TcFormatSpec fmt;       /* TC_FMT_NONE 表示默认十进制输出 */
    TcOperand operand;
} TcIoWrite;

typedef struct {
    int line;
    TcType type;
    char *name;      /* 读取目标变量名，堆分配 */
    TcResolvedBinding binding; /* Analyzer 解析的读取目标 */
} TcRead;

typedef struct TcStatement TcStatement;

typedef struct {
    int line;
    TcRhs condition;           /* bool 类型条件表达式 */
    TcStatement *then_body;    /* then 块语句数组，堆分配 */
    size_t then_count;
    TcStatement *else_body;    /* else 块语句数组（可为空），堆分配 */
    size_t else_count;
} TcIfStmt;

typedef struct {
    int line;
    int loop_id;                /* Analyzer 成功后 >= 0 */
    TcRhs condition;            /* bool 类型条件表达式 */
    TcStatement *body;          /* 循环体语句数组，堆分配 */
    size_t body_count;
} TcWhileStmt;

typedef struct {
    int line;
    int loop_id;                /* Analyzer 成功后指向最内层 while */
} TcLoopControlStmt;

typedef struct {
    int line;
    char *name;        /* 标签名，堆分配 */
} TcLabelDef;

typedef struct {
    int line;
    char *target;      /* 目标标签名，堆分配 */
    int resolved_target_stmt_index; /* Analyzer 解析的标签 stmt_index */
    int resolved;      /* 分析成功后为 1 */
} TcGoto;

/** 统一语句表示，kind 决定活跃的 u 成员 */
struct TcStatement {
    TcStmtKind kind;
    union {
        TcVarDef var_def;
        TcConstDef const_def;
        TcAssign assign;
        TcIoWrite io_write;
        TcRead io_read;
        TcIfStmt if_stmt;
        TcWhileStmt while_stmt;
        TcLoopControlStmt break_stmt;
        TcLoopControlStmt continue_stmt;
        TcLabelDef label_def;
        TcGoto goto_stmt;
    } u;
};

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
    TcType type;
    uint64_t bits;
} TcValue;

/** 符号表条目：一个变量或 let 常量的元信息 */
typedef struct {
    char *name;          /* 堆分配 */
    TcType type;
    int slot;            /* 运行时变量槽索引 */
    int def_line;
    int def_stmt_index;  /* 定义位置对应的语句序号 */
    TcSymKind sym_kind;
    int initialized;     /* 定义时是否有初始化值 */
    int has_const_value; /* let 常量编译期求值的结果是否有效 */
    TcValue const_value; /* let 常量编译期求值结果 */
    int scope_level;     /* 作用域层级：0=全局，1=if 块，2=内层 if…… */
    int scope_end_stmt_index; /* 块内符号可见上界（不含）；-1 表示全局/始终可见 */
} TcSymbol;

/** 作用域栈帧：记录某层级符号在 symbols[] 中的索引区间 [start_index, end_index) */
typedef struct {
    size_t start_index;  /* 当前作用域在 symbols[] 中的起始索引 */
    size_t end_index;    /* 结束索引（开区间）；TC_SCOPE_END_OPEN 表示仍活跃 */
    int level;
} TcScope;

#define TC_SCOPE_END_OPEN ((size_t)-1)

typedef enum {
    TC_BLOCK_GLOBAL,
    TC_BLOCK_IF_THEN,
    TC_BLOCK_IF_ELSE,
    TC_BLOCK_WHILE
} TcBlockKind;

typedef struct {
    int owner_stmt_index;
    TcBlockKind kind;
} TcBlockId;

/** 标签表条目（v0.0.26）；Pass1 按深度 pop；Pass2 保留全部并带块路径 */
typedef struct {
    char *name;              /* 标签名，堆分配 */
    int stmt_index;          /* 标签语句的扁平序号 */
    int block_depth;         /* 标签所在的作用域深度 / 块路径长度 */
    TcBlockId *block_path;   /* 块路径（堆分配），长度 = block_depth；Pass1 可为 NULL */
    int def_line;            /* 定义行号（用于错误报告） */
} TcLabelEntry;

typedef struct {
    TcSymbol *symbols;
    size_t count;
    size_t capacity;

    TcScope *scopes;          /* 作用域栈 */
    size_t scope_count;
    size_t scope_capacity;

    /* v0.0.26：标签表（块退出时 pop 当前深度条目） */
    TcLabelEntry *labels;
    size_t label_count;
    size_t label_capacity;
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

typedef struct TcCfg TcCfg;

/** Analyzer 分析通过后的完整程序：语句 + 符号表 + CFG + 警告 */
typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
    TcCfg *cfg;
    TcWarningList warnings;
} TcTypedProgram;

typedef enum {
    TC_DIAG_NONE,
    TC_DIAG_LANGUAGE,
    TC_DIAG_API,
    TC_DIAG_IMPLEMENTATION
} TcDiagnosticDomain;

typedef enum {
    TC_API_ERR_NONE,
    TC_API_ERR_INVALID_ARGUMENT,
    TC_API_ERR_FILE_OPEN,
    TC_API_ERR_FILE_READ
} TcApiErrorCode;

/**
 * 单槽诊断对象（fail-fast 模式下仅保存第一条错误）。
 * 调用方通过 tc_diagnostic_set_source 绑定源文本；source 由诊断模块 strdup 管理。
 */
typedef struct {
    TcDiagnosticDomain domain;
    TcApiErrorCode api_code;
    TcErrorKind kind;
    char *message;    /* 由 diagnostic 模块管理；OOM 可使用免分配静态回退 */
    char *filename;   /* 堆分配 */
    char *snippet;    /* 堆分配，出错行源码 */
    char *source;     /* 堆分配，完整源文本 */
    int line;
    int column;
} TcDiagnostic;

/* 无列号时的占位值 */
#define TC_COLUMN_UNKNOWN (-1)

/* ------------------------------------------------------------------ */
/*  公共工具函数声明                                                    */
/* ------------------------------------------------------------------ */

int tc_type_bit_width(TcType type);
int tc_type_is_signed(TcType type);
int tc_type_is_bool(TcType type);
int tc_type_is_integer(TcType type);
int tc_type_is_float(TcType type);
int tc_type_parse(const char *text, TcType *out);
int tc_float_mode_parse(const char *text, TcFloatMode *out);
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
const char *tc_api_error_code_name(TcApiErrorCode code);
const char *tc_warning_kind_name(TcWarningKind kind);
const char *tc_type_name(TcType type);

#endif
