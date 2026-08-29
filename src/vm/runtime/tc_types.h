/*
 * tc_types.h — 贯穿 TC 全流水线的共享数据契约
 *
 *   - TcTypeTag / TcType（标量、ptr、memblock、struct、void）
 *   - AST：TcStmtKind（24）、TcRhsKind（34）、操作数与语句 payload
 *   - 运行时：TcValue、TcSymbol、槽域、诊断
 *   - 程序：TcProgram / TcTypedProgram
 *
 * Lexer / Parser / Analyzer / Executor / AOT / Embed 均依赖此处；
 * 不得将模块私有类型放入本文件。
 */
#ifndef TC_TYPES_H
#define TC_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* INT64_MIN 的绝对值 2^63，用于字面量范围检查与 read 输入解析 */
#define TC_INT64_MIN_ABS_MAGNITUDE 9223372036854775808ULL

/*
 * 本实现固定 64-bit-only 目标字长（§3.2.1）：isize/usize/ptr/memblock 长度头均为 64 位。
 * 32 位目标另行立项。C99 编译期断言：非 64 位宿主拒绝构建。
 */
typedef char tc_target_is_64bit_only[(sizeof(void *) == 8) ? 1 : -1];
typedef char tc_uint64_is_8_bytes[(sizeof(uint64_t) == 8) ? 1 : -1];

/* IEEE 754 canonical quiet NaN / ±inf（§3.6.1 / §6.3.3）；与宿主 NAN 宏无关 */
#define TC_FLOAT32_CANONICAL_NAN_BITS UINT64_C(0x7FC00000)
#define TC_FLOAT64_CANONICAL_NAN_BITS UINT64_C(0x7FF8000000000000)
#define TC_FLOAT32_POS_INF_BITS UINT64_C(0x7F800000)
#define TC_FLOAT64_POS_INF_BITS UINT64_C(0x7FF0000000000000)
#define TC_FLOAT32_NEG_INF_BITS UINT64_C(0xFF800000)
#define TC_FLOAT64_NEG_INF_BITS UINT64_C(0xFFF0000000000000)

/* ------------------------------------------------------------------ */
/*  类型 & 运算符枚举                                                   */
/* ------------------------------------------------------------------ */

/**
 * TC 0.0.41 类型标签（TcTypeTag）。
 *
 * 标量：定宽整数、bool、浮点、isize/usize。
 * 复合：ptr / memblock / struct；void 仅作函数返回类型。
 *
 * TC_BOOL 与整数共用枚举以便统一查询，但概念上属独立类别（语言标准 §3.1）。
 * tc_type_is_integer() 对 TC_INT8～TC_UINT64 与 TC_ISIZE/TC_USIZE 返回真，不含 TC_BOOL。
 *
 * 完整类型值见下方 TcType（tag + 参数）。
 * TcTypeTag 仅为判别标签与标量叶子谓词；类型事实源是 TcType /
 * interned const TcType*（见 tc_type_tag_singleton / TcTypeTable）。
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
    TC_FLOAT64,
    TC_ISIZE,
    TC_USIZE,
    TC_VOID,      /* 仅函数返回类型 */
    TC_PTR,       /* ptr<T>，params.ptr_type */
    TC_MEMBLOCK,  /* memblock<T,N>，params.memblock_type；等价仅看 T */
    TC_STRUCT     /* 结构体，params.struct_type.struct_id */
} TcTypeTag;

/** 含 TC_STRUCT 在内的标签个数（单例表长度） */
#define TC_TYPE_TAG_COUNT ((int)TC_STRUCT + 1)

/**
 * 完整类型表示（VM 详设 §8.1）。
 * 标量与 void：仅 tag 有意义，params 为零。
 * ptr：pointee 堆分配或指向持久类型节点。
 * memblock：element + 声明 count（N）；tc_type_equals 忽略 N。
 * struct：struct_id 索引结构体表（条目按定义模块 + 结构体名定界）。
 *
 * pending_name：仅解析期使用。类型表达式中的结构体名（裸标识符或
 * Mod.Name，含嵌套在 ptr<…>/memblock<…> 内的）在 Parser 阶段无法解析
 * 为 struct_id，先以 struct_id = -1 + pending_name（堆）暂存；Analyzer
 * 注册结构体表后按当前文件的 import 列表与位置规则（语言标准 §3.9.1）
 * 解析为真实 struct_id 并释放该名字。
 * 解析成功后 pending_name 恒为 NULL。tc_type_free / tc_type_copy 负责
 * 其生命周期。
 */
typedef struct TcType {
    TcTypeTag tag;
    char *pending_name; /* 解析期未决结构体名（堆）；仅 struct_id<0 时可能非 NULL */
    union {
        struct {
            struct TcType *pointee;
        } ptr_type;
        struct {
            struct TcType *element;
            uint64_t count;
            char *pending_count_name; /* usize_operand 名（Self.N / Mod.N / 标识符）；解析后置 NULL */
        } memblock_type;
        struct {
            int struct_id;
        } struct_type;
    } params;
} TcType;

/**
 * 算术溢出处理模式：
 *   TC_ARITH_STRICT — 有符号溢出时报 TC_RE_INTEGER_OVERFLOW
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
 *   TC_TRUNC_STRICT   — 数值转换，不可表示时报 TC_RE_CAST_OVERFLOW
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

/* 内建逻辑运算符：and / or / xor / not，操作数与结果均为 bool */
typedef enum {
    TC_LOGIC_AND,
    TC_LOGIC_OR,
    TC_LOGIC_XOR,
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

/**
 * 完整格式说明符（§10.1）。
 * 包含标志、宽度、精度和基础转换符。
 * width/precision 为 0 表示未指定；precision_set 区分 "未指定" 与 "指定为 0"。
 */
typedef struct {
    int flag_minus;       /* '-' 左对齐 */
    int flag_plus;        /* '+' 强制符号 */
    int flag_hash;        /* '#' 备用形式 */
    int flag_zero;        /* '0' 零填充 */
    int width;            /* 字段宽度 1~65535，0 表示未指定 */
    int precision_set;    /* 是否指定精度 */
    int precision;        /* 精度值 0~65535 */
    TcFormatSpec spec;    /* 基础转换符 */
} TcFormatFullSpec;

/** 从基础转换符构建默认的完整格式说明符（无标志/宽度/精度）。 */
static inline TcFormatFullSpec tc_format_spec_make(TcFormatSpec spec) {
    TcFormatFullSpec fs = {0};

    fs.spec = spec;
    return fs;
}

/** 符号种类：变量、let、形参、static var/let（0.0.41） */
typedef enum {
    TC_SYM_VARIABLE,
    TC_SYM_CONSTANT,
    TC_SYM_PARAMETER,    /* 函数形参（只读绑定） */
    TC_SYM_STATIC_VAR,   /* #lib static var */
    TC_SYM_STATIC_LET    /* #lib static let（编译期内联，无运行时槽） */
} TcSymKind;

/**
 * 运行时槽位域。
 * let / static let 无槽；其余运行时绑定落入下列之一。
 */
typedef enum {
    TC_SLOT_TOPLEVEL = 0, /* #program 顶层 var */
    TC_SLOT_STATIC,       /* 全程序唯一 static var 槽 */
    TC_SLOT_PARAM,        /* 调用帧形参区 */
    TC_SLOT_LOCAL         /* 调用帧局部 var 区 */
} TcSlotDomain;

/* ------------------------------------------------------------------ */
/*  错误与警告枚举                                                     */
/* ------------------------------------------------------------------ */

/** 诊断错误种类，与 TC 语言标准定义的可观测错误一一对应 */
typedef enum {
    /* ---- 静态/编译期错误（TC_CE_*） ---- */
    TC_CE_SYNTAX,
    TC_CE_UNDEFINED_VARIABLE,
    TC_CE_DUPLICATE_DEFINITION,
    TC_CE_TYPE_MISMATCH,
    TC_CE_LITERAL_OUT_OF_RANGE,
    TC_CE_LITERAL_TYPE,
    TC_CE_CONSTANT_ASSIGNMENT,
    TC_CE_CONSTANT_EXPRESSION,
    TC_CE_CONSTANT_OVERFLOW,
    TC_CE_CONSTANT_DIV_ZERO,
    TC_CE_CONSTANT_CAST_OVERFLOW,
    TC_CE_COMPARISON_TYPE_MISMATCH,
    TC_CE_FORMAT_TYPE_MISMATCH,
    TC_CE_OPERAND_COUNT,
    TC_CE_INDENT_MIXED,          /* 混用空格与制表符 */
    TC_CE_INDENT_INSUFFICIENT,   /* 块内缩进不足 */
    TC_CE_INDENT_ELSE_END,       /* else/end 缩进与 if 不一致 */
    TC_CE_MISSING_END,           /* if 语句缺少 end */
    TC_CE_CONDITION_TYPE,        /* if 条件结果不是 bool */
    TC_CE_MODE_MISMATCH,         /* ieee/wrap 用于非法上下文 */
    TC_CE_UNINITIALIZED_VARIABLE,  /* §4.2 读取未初始化变量 */
    TC_CE_LABEL_NOT_FOUND,         /* §4.8.3 goto 引用未定义标签 */
    TC_CE_DUPLICATE_LABEL,         /* §4.8.3 同一作用域重定义标签 */
    TC_CE_JUMP_INTO_BLOCK,         /* §4.8.3 跳入内层子块 */
    TC_CE_VAR_MISSING_INIT,        /* §4.2 var 声明缺少初始化器 */
    TC_CE_BITCAST_WIDTH,           /* §8.6 bitcast 源/目标位宽不等 */
    TC_CE_LABEL_INSIDE_LOOP,       /* label 不得出现在 while 内 */
    TC_CE_GOTO_INSIDE_LOOP,        /* goto 不得出现在 while 内 */
    TC_CE_BREAK_OUTSIDE_LOOP,      /* break 必须出现在 while 内 */
    TC_CE_CONTINUE_OUTSIDE_LOOP,   /* continue 必须出现在 while 内 */

    /* ---- 控制流补充 ---- */
    TC_CE_GOTO_OUTSIDE_FUNCTION,
    TC_CE_LABEL_OUTSIDE_FUNCTION,
    TC_CE_JUMP_INCOMPATIBLE_BLOCK,
    TC_CE_FORMAT_SPECIFIER,        /* 格式控制项非法（异于 FORMAT_STRING） */

    /* ---- 函数诊断（§11.4.2） ---- */
    TC_CE_FUNCTION_NAME_CONFLICT,
    TC_CE_UNDEFINED_FUNCTION,
    TC_CE_DUPLICATE_PARAMETER,
    TC_CE_MISSING_ARGUMENT,
    TC_CE_DUPLICATE_ARGUMENT,
    TC_CE_UNKNOWN_ARGUMENT,
    TC_CE_ARGUMENT_ORDER,
    TC_CE_FUNCALL_POSITION,
    TC_CE_FUNCALL_RESULT_TYPE,
    TC_CE_RETURN_OUTSIDE_FUNCTION,
    TC_CE_RETURN_FORM,
    TC_CE_RETURN_TYPE,
    TC_CE_MISSING_RETURN,
    TC_CE_UNREACHABLE_STATEMENT,
    TC_CE_PARAMETER_ASSIGNMENT,
    TC_CE_FUNCTION_SCOPE_ACCESS,
    TC_CE_RECURSION,

    /* ---- memblock（§11.4.3） ---- */
    TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE,       /* 静态越界 */
    TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH,
    TC_CE_MEMBLOCK_SIZE_MISMATCH,

    /* ---- struct（§11.4.4） ---- */
    TC_CE_STRUCT_MISSING_FIELD,
    TC_CE_STRUCT_UNKNOWN_FIELD,
    TC_CE_STRUCT_DUPLICATE_FIELD,
    TC_CE_STRUCT_FIELD_ORDER,
    TC_CE_STRUCT_IMMUTABLE_FIELD,
    TC_CE_STRUCT_VALUE_SELF_REF,   /* 值位置引用正在定义的本结构体（§3.9.1） */
    TC_CE_DUPLICATE_STRUCT,
    TC_CE_UNDEFINED_STRUCT,

    /* ---- 模块（§11.4.5） ---- */
    TC_CE_MODULE_LAYER,           /* 顶层声明层序违反（import→struct→值→func/exec） */
    TC_CE_MISSING_VISIBILITY,     /* #lib 成员缺少 public/private */
    TC_CE_PROGRAM_MODE_MISUSE,    /* #program 中使用了库专用构造（func/static/Self/可见性等） */
    TC_CE_IMPORT_NOT_FOUND,       /* 找不到 import 目标 .tc */
    TC_CE_IMPORT_NOT_LIB,         /* import 目标不是 #lib */
    TC_CE_IMPORT_AMBIGUOUS,       /* 多条 -I/相对路径同时命中不同文件 */
    TC_CE_DUPLICATE_IMPORT,       /* 同一文件重复 import 同名模块 */
    TC_CE_IMPORT_NAME_CONFLICT,   /* 4b 导入名与本地声明冲突 */
    TC_CE_CIRCULAR_IMPORT,        /* 导入成环（含自引用） */
    TC_CE_PRIVATE_MEMBER_ACCESS,  /* 访问其它模块的 private 成员 */

    /* ---- 指针与 memcopy（§11.4.6） ---- */
    TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE,      /* 静态 */

    /* ---- 运行时错误（TC_RE_*，§11.4.1 / §11.4.6） ---- */
    TC_RE_DIVISION_BY_ZERO,
    TC_RE_INTEGER_OVERFLOW,
    TC_RE_NEGATIVE_SHIFT_COUNT,    /* 运行时负移位计数 */
    TC_RE_FLOAT_OVERFLOW,          /* 严格模式浮点上溢 */
    TC_RE_FLOAT_UNDERFLOW,         /* 严格模式浮点下溢 */
    TC_RE_FLOAT_INVALID,           /* 严格模式浮点无效操作（nan 等） */
    TC_RE_CAST_OVERFLOW,
    TC_RE_IO,
    TC_RE_NULL_POINTER_DEREFERENCE,
    TC_RE_NULL_POINTER_ARITHMETIC,
    TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE,    /* 运行时越界；打印名同静态 */
    TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE,   /* 运行时；打印名同静态 */

    /* ---- 实现专用错误码 ---- */
    TC_ERR_OUT_OF_MEMORY          /* 内存分配失败（实现资源失败，保留 TC_ERR_ 前缀） */
} TcErrorKind;

/*
 * 编译警告种类。语言无警告（未初始化读取为 TC_CE_UNINITIALIZED_VARIABLE）。
 * TcWarningKind / TcWarningList 保留空壳，供 ABI 与未来扩展。
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
    int radix;            /* 词法进制：2 / 8 / 10 / 16（@padding(N) 形态校验等用） */
    int is_bool;          /* 1 表示 true/false 布尔字面量；magnitude 为 0/1 */
    int is_float;         /* 1 表示浮点字面量；float_value 有效 */
    double float_value;   /* 浮点字面量双精度暂存（词法阶段） */
    int float32_suffix;   /* 1 表示 f/F 后缀 → 上下文须为 float32 */
    int is_nullptr;       /* 1 表示 nullptr；仅合法于 ptr 期望上下文 */
    int is_float_special; /* 1 表示 inf / -inf / nan */
    int float_special;    /* 0=nan, 1=+inf, -1=-inf；is_float_special 时有效 */
} TcLiteral;

/* ------------------------------------------------------------------ */
/*  AST 节点：操作数 → 右值 → 语句                                      */
/* ------------------------------------------------------------------ */

/** 算术/一元运算的操作数：变量引用、字面量或字段链读取 */
typedef enum {
    TC_OPERAND_VAR,
    TC_OPERAND_LIT,
    TC_OPERAND_FIELD_READ
} TcOperandKind;

/** Analyzer 持久化的绑定结果；Executor/AOT 不再按名称重新解析。 */
typedef struct {
    int resolved;
    int slot;             /* var 的固定运行时槽；let 为 -1 */
    int is_const;         /* let 常量绑定时为 1 */
    const TcType *type;   /* 指向单例或类型池 / 符号 type 的稳定节点 */
    uint64_t const_bits;  /* let 的规范化 TcValue.bits */
} TcResolvedBinding;

/** Pass2 固化的字段访问；parse 期字符串释放后由 Executor/AOT 直接消费。 */
typedef struct {
    int resolved;
    int base_slot;              /* 基址运行时槽；let 基址为 -1 */
    uint64_t const_bits;        /* let / static let 基址的规范化位模式 */
    const TcType *field_type;   /* 末字段声明类型 */
    uint32_t *offsets;          /* 各级字段相对其结构体的位偏移 */
    size_t field_count;
    int is_memblock_count;      /* 1 = memblock .count 语义 */
} TcResolvedFieldAccess;

typedef struct {
    char *base;
    char **fields;
    size_t field_count;
    TcResolvedFieldAccess resolved;
} TcFieldAccess;

typedef struct {
    TcOperandKind kind;
    union {
        char *name;              /* TC_OPERAND_VAR：变量名，堆分配 */
        TcLiteral lit;           /* TC_OPERAND_LIT：字面量 */
        TcFieldAccess field_read; /* TC_OPERAND_FIELD_READ */
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
    TC_RHS_BITCAST,       /* 等宽整数/浮点位重解释 */

    /* ---- 复合 / 调用 RHS ---- */
    TC_RHS_MEMBLOCK_LOAD,
    TC_RHS_MEMBLOCK_CONSTRUCTOR,
    TC_RHS_MEMBLOCK_COUNT,
    TC_RHS_STRUCT_CONSTRUCTOR,
    TC_RHS_FIELD_READ,
    TC_RHS_PTR_LOAD,
    TC_RHS_PTR_ADDRESS,
    TC_RHS_PTR_ADD,
    TC_RHS_PTR_SUB,
    TC_RHS_PTR_EQ,
    TC_RHS_PTR_NE,
    TC_RHS_PTR_LT,
    TC_RHS_PTR_LE,
    TC_RHS_PTR_GT,
    TC_RHS_PTR_GE,
    TC_RHS_PTR_SIZE,
    TC_RHS_FUNCALL_EXPR,   /* 表达式位置的函数调用（含命名实参） */
    TC_RHS_SELF_MEMBER     /* Self.member；仅 #lib */
} TcRhsKind;

typedef struct {
    TcType target; /* 解析期拥有；可为标量或 ptr<T> 等完整类型 */
    const TcType *target_type; /* Pass2：单例或 type_table intern；执行期只读 */
    int target_type_resolved;
    const TcType *source_type;
    int source_type_resolved;
    TcOperand source;
} TcBitcastRhs;

typedef struct {
    TcType target; /* 解析期拥有；可为标量或 ptr<T> 等完整类型 */
    const TcType *target_type; /* Pass2：单例或 type_table intern；执行期只读 */
    int target_type_resolved;
    TcTruncateMode mode;
    const TcType *source_type;
    int source_type_resolved;
    TcOperand source;
} TcCastRhs;

typedef struct {
    TcRhsKind kind;
    union {
        TcLiteral lit;           /* TC_RHS_LIT */
        struct {
            TcArithOp op;
            const TcType *type;
            TcWrapMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } arith;                 /* TC_RHS_ARITH */
        struct {
            TcUnaryOp op;
            const TcType *type;
            TcWrapMode mode;
            TcOperand operand;
        } unary;                 /* TC_RHS_UNARY */
        struct {
            TcCompareOp op;
            const TcType *type;
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
            const TcType *type;
            TcOperand lhs;
            TcOperand rhs;
        } bitwise_bin;           /* TC_RHS_BITWISE_BIN */
        struct {
            const TcType *type;
            TcOperand operand;
        } bitwise_un;              /* TC_RHS_BITWISE_UN（恒为 not） */
        struct {
            TcShiftOp op;
            const TcType *type;
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
            const TcType *type;  /* TC_FLOAT32 或 TC_FLOAT64 */
            TcFloatMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } float_arith;           /* TC_RHS_FLOAT_ARITH */
        struct {
            TcUnaryOp op;
            const TcType *type;
            TcFloatMode mode;
            TcOperand operand;
        } float_unary;           /* TC_RHS_FLOAT_UNARY */
        struct {
            TcCompareOp op;
            const TcType *type;
            TcFloatMode mode;
            TcOperand lhs;
            TcOperand rhs;
        } float_compare;         /* TC_RHS_FLOAT_COMPARE */
        TcBitcastRhs bitcast;    /* TC_RHS_BITCAST */
        /* 复合/调用 RHS payload（完整 typedef 见语句区后的同名类型） */
        struct {
            TcType element_type;
            TcOperand memblock;
            TcOperand index;
        } memblock_load;
        struct {
            TcType element_type;
            uint64_t count;
            char *count_name;
            int is_fill;
            TcOperand fill_value;
            TcOperand *values;
            size_t value_count;
        } memblock_ctor;
        struct {
            char *memblock_name;
            TcResolvedBinding binding; /* Pass2 解析后的绑定（Executor/AOT 直接取槽） */
        } memblock_count;
        struct {
            char *struct_name;
            struct {
                char *param_name;
                /* value filled after TcRhs self-complete — see named args on stmt */
                TcOperand value_op;
                int has_rhs;
                struct TcRhs *value_rhs;
            } *fields;
            size_t field_count;
        } struct_ctor;
        struct {
            char *base;
            char **fields;
            size_t field_count;
            TcResolvedFieldAccess resolved;
        } field_read;
        struct {
            TcType pointee_type;
            TcOperand ptr;
        } ptr_load;
        struct {
            TcType pointee_type;
            char *name;
        } ptr_address;
        struct {
            TcType pointee_type;
            TcOperand ptr;
            TcOperand offset;
        } ptr_arith;
        struct {
            TcType pointee_type;
            TcOperand lhs;
            TcOperand rhs;
        } ptr_compare;
        struct {
            TcType pointee_type;
            TcOperand ptr;
        } ptr_size;
        struct {
            char *target;
            int is_self;
            char *qualifier;
            char *member_name;
            struct {
                char *param_name;
                struct TcRhs *value;
            } *args;
            size_t arg_count;
            int resolved_func_id; /* Analyzer 解析；-1 未定 */
        } funcall_expr;
        struct {
            char *member_name;
        } self_member;
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
    TC_STMT_CONTINUE,    /* 继续最内层 while */

    /* ---- 模块 / 函数 / 复合语句 ---- */
    TC_STMT_FIELD_ASSIGN,       /* a.b = rhs */
    TC_STMT_FUNC_DEF,           /* #lib 函数定义 */
    TC_STMT_FUNCALL,            /* funcall 语句（可 Self./限定名） */
    TC_STMT_RETURN,             /* return / return operand */
    TC_STMT_MEMBLOCK_STORE,
    TC_STMT_MEMBLOCK_COPY,
    TC_STMT_PTR_STORE,
    TC_STMT_MEMCOPY_UNSAFE,
    TC_STMT_STRUCT_DEF,         /* struct 定义（#lib 须带可见性） */
    TC_STMT_STATIC_VAR_DEF,     /* #lib static var */
    TC_STMT_STATIC_LET_DEF,     /* #lib static let */
    TC_STMT_IMPORT              /* import ModuleName; */
} TcStmtKind;

typedef enum {
    TC_VIS_NONE = 0, /* #program 顶层无可见性修饰 */
    TC_VIS_PUBLIC,   /* #lib 对外可见 */
    TC_VIS_PRIVATE   /* #lib 仅本库可见 */
} TcVisibility;

/** 源文件模块模式：由首行 #program / #lib 设定；UNSET 表示尚未解析头。
 *  FUNC_BODY 仅供 parser 内部标识函数体语句上下文（非源文件模式），
 *  用于禁止 public/private 等模块级修饰出现在函数体内。 */
typedef enum {
    TC_MODULE_UNSET = 0,
    TC_MODULE_PROGRAM,
    TC_MODULE_LIB,
    TC_MODULE_FUNC_BODY
} TcModuleMode;

typedef struct {
    int line;
    char *name;      /* 变量名，堆分配 */
    TcType full_type; /* 完整类型（含 ptr/memblock/struct 参数） */
    char *struct_type_name; /* 未解析 struct 名（堆）；非 struct 为 NULL */
    TcRhs rhs;
    TcResolvedBinding binding; /* 定义对应的固定 slot */
} TcVarDef;

typedef struct {
    int line;
    char *name;      /* 常量名，堆分配 */
    TcType full_type;
    char *struct_type_name;
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
    const TcType *type;
    TcFormatFullSpec fmt;       /* TC_FMT_NONE 表示默认十进制输出 */
    TcOperand operand;
} TcIoWrite;

typedef struct {
    int line;
    const TcType *type;
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

/* ---- 模块 / 函数 / 复合语句与 RHS payload ---- */

typedef struct {
    int line;
    char *module_name; /* import 目标模块名，堆分配 */
} TcImportStmt;

typedef struct {
    char *name;              /* 字段名，堆分配 */
    int is_var;              /* 1=var 字段，0=let 字段 */
    TcType type;
    char *struct_type_name;  /* 未解析 struct 类型名；否则 NULL */
    uint64_t padding;        /* @padding(N)；缺省 0 */
} TcStructField;

typedef struct {
    int line;
    TcVisibility visibility; /* #lib 必填；#program 为 NONE */
    char *name;              /* 结构体名，堆分配 */
    TcStructField *fields;
    size_t field_count;
    int struct_id;           /* Analyzer 分配；解析后为 -1 */
} TcStructDef;

typedef struct {
    char *name;              /* 形参名，堆分配 */
    TcType type;
    char *struct_type_name;
} TcFuncParam;

typedef struct {
    int line;
    TcVisibility visibility;
    char *name;              /* 函数名，堆分配 */
    TcFuncParam *params;
    size_t param_count;
    TcType return_type;      /* 可为 TC_VOID */
    char *return_struct_name;
    TcStatement *body;
    size_t body_count;
    int func_id;             /* Analyzer；解析后为 -1 */
} TcFuncDef;

typedef struct {
    char *param_name; /* 命名实参形参名，堆分配 */
    TcRhs value;
} TcNamedArg;

typedef struct {
    int line;
    char *target;            /* 调用目标文本（裸名 / Self.x / mod.x），堆分配 */
    int is_self;             /* target 以 Self. 开头时为 1 */
    char *qualifier;         /* 限定前缀（Self 或模块名）；无则 NULL */
    char *member_name;       /* 限定后的成员名；裸名时与 target 相同逻辑由 Analyzer 解析 */
    TcNamedArg *args;
    size_t arg_count;
    int resolved_func_id; /* Analyzer 解析；-1 未定 */
} TcFuncallStmt;

typedef struct {
    int line;
    int has_value;           /* 1=return operand；0=裸 return */
    TcOperand value;
} TcReturnStmt;

typedef struct {
    int line;
    char *base;              /* 最左标识符，堆分配 */
    char **fields;           /* 字段链 a.b.c → ["b","c"] */
    size_t field_count;
    TcRhs rhs;
} TcFieldAssign;

typedef struct {
    int line;
    TcVisibility visibility;
    char *name;
    TcType type;
    char *struct_type_name;
    TcRhs rhs;
    int static_slot;         /* Analyzer 分配的 static 槽；-1 未定 */
} TcStaticVarDef; /* #lib：public/private static var */

typedef struct {
    int line;
    TcVisibility visibility;
    char *name;
    TcType type;
    char *struct_type_name;
    TcRhs rhs;
} TcStaticLetDef; /* #lib：public/private static let */

typedef struct {
    int line;
    TcType element_type;
    char *memblock_name;     /* 目标 memblock 绑定名 */
    TcResolvedBinding binding; /* Pass2 解析后的目标绑定（Executor/AOT 直接取槽） */
    TcOperand index;
    TcOperand value;
} TcMemblockStoreStmt;

typedef struct {
    int line;
    TcType element_type;
    char *dst_name;
    TcResolvedBinding dst_binding; /* Pass2 解析后的目标绑定 */
    TcOperand dst_index;
    char *src_name;
    TcResolvedBinding src_binding; /* Pass2 解析后的源绑定 */
    TcOperand src_index;
    TcOperand length;
} TcMemblockCopyStmt;

typedef struct {
    int line;
    TcType pointee_type;
    TcOperand ptr;
    TcOperand value;
} TcPtrStoreStmt;

typedef struct {
    int line;
    TcType element_type;
    TcOperand dst_ptr;
    TcOperand dst_index;
    TcOperand src_ptr;
    TcOperand src_index;
    TcOperand length;
} TcMemcopyUnsafeStmt;

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
        TcImportStmt import_stmt;
        TcStructDef struct_def;
        TcFuncDef func_def;
        TcFuncallStmt funcall_stmt;
        TcReturnStmt return_stmt;
        TcFieldAssign field_assign;
        TcStaticVarDef static_var_def;
        TcStaticLetDef static_let_def;
        TcMemblockStoreStmt memblock_store;
        TcMemblockCopyStmt memblock_copy;
        TcPtrStoreStmt ptr_store;
        TcMemcopyUnsafeStmt memcopy_unsafe;
    } u;
};

/* ------------------------------------------------------------------ */
/*  程序 & 符号表 & 运行时值                                            */
/* ------------------------------------------------------------------ */

/** 未类型化的原始程序 / 单模块（Parser 产出，Analyzer 消费） */
typedef struct {
    TcModuleMode mode;
    char *module_name; /* #lib 由文件名推导；#program 可为 NULL */
    char *source_path; /* 源路径（导入解析用）；可为 NULL */
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcProgram;

/** 运行时值：完整类型指针 + 位模式载体。
 *
 * type 指向标量单例或分析期 intern / AST 稳定 TcType 节点（禁止指向可 realloc 缓冲）。
 * 标量 / ptr：bits 存抽象位模式。
 * memblock / struct：bits 存堆块指针（uintptr 转 uint64_t；本实现 64-bit-only）。
 */
typedef struct {
    const TcType *type;
    uint64_t bits;
} TcValue;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(TcValue) == 16, "TcValue must stay 16 bytes (ptr + bits)");
#endif

/**
 * 程序级运行时槽组。
 * Executor / AOT 共享：顶层槽 + 全程序 static 槽；
 * 形参/局部槽在调用帧内分配（见 TcCallFrame）。
 * memblock_storage / struct_storage 跟踪堆块以便释放。
 */
typedef struct {
    TcValue *toplevel_slots;
    size_t toplevel_count;
    TcValue *static_slots;
    size_t static_count;
    void **memblock_storage;
    size_t memblock_storage_count;
    size_t memblock_storage_capacity;
    void **struct_storage;
    size_t struct_storage_count;
    size_t struct_storage_capacity;
} TcRuntimeSlots;

/** 符号表条目：一个变量或 let 常量的元信息 */
typedef struct {
    char *name;          /* 堆分配 */
    int slot;            /* 运行时变量槽索引（在所属 slot_domain 内） */
    TcSlotDomain slot_domain; /* 顶层 / static / 形参 / 局部 */
    int def_line;
    int def_stmt_index;  /* 定义位置对应的语句序号 */
    TcSymKind sym_kind;
    int initialized;     /* 定义时是否有初始化值 */
    int has_const_value; /* let 常量编译期求值的结果是否有效 */
    TcValue const_value; /* let 常量编译期求值结果 */
    int scope_level;     /* 作用域层级：0=全局，1=if 块，2=内层 if…… */
    int scope_end_stmt_index; /* 块内符号可见上界（不含）；-1 表示全局/始终可见 */
    const TcType *type;  /* 完整类型：单例或 TcTypeTable intern；随符号释放时不 free */
    int ptr_target_readonly; /* ptr 绑定：所指外层为 let/static let/形参时为 1 */
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

/** 标签表条目；Pass1 按深度 pop；Pass2 保留全部并带块路径 */
typedef struct {
    char *name;              /* 标签名，堆分配 */
    int func_id;             /* 所属函数 func_id（4d 稳定分配，全局唯一）；顶层为 -1 */
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

    /* 标签表（块退出时 pop 当前深度条目） */
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
typedef struct TcCfgSet TcCfgSet;

/** 结构体定义表（定义见 tc_struct_check.h；Analyzer 拥有并移交 TypedProgram） */
struct TcStructTable;

/** 类型池：分析期 intern 复合类型，返回稳定 const TcType* */
typedef struct TcTypeTable {
    TcType **nodes; /* 各节点独立 malloc；标量不入池（走单例） */
    size_t count;
    size_t capacity;
} TcTypeTable;

/** Analyzer 分析通过后的完整程序：语句 + 符号表 + CFG + 警告 */
typedef struct {
    TcProgram program;       /* 入口模块 */
    TcProgram *deps;         /* 已加载的依赖 #lib（不含入口） */
    size_t dep_count;
    size_t dep_capacity;
    TcSymbolTable symbols;
    TcCfg *cfg;              /* 顶层域（兼容既有测试） */
    TcCfgSet *cfg_set;       /* 多域集合；非 NULL 时 cfg == &cfg_set->toplevel */
    TcWarningList warnings;
    /* A-8：程序级槽元数据（Executor/AOT 消费；帧内槽由调用帧管理） */
    size_t toplevel_slot_count;
    size_t static_slot_count;
    struct TcStructTable *struct_table; /* 拥有；供 Executor/AOT 布局与字段偏移 */
    TcTypeTable *type_table;            /* 拥有；复合类型 intern 池 */
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

/* 标量标签查询（现网路径） */
int tc_type_bit_width(TcTypeTag type);
int tc_type_is_signed(TcTypeTag type);
int tc_type_is_bool(TcTypeTag type);
int tc_type_is_integer(TcTypeTag type);
int tc_type_is_float(TcTypeTag type);
int tc_type_is_void(TcTypeTag type);
int tc_type_is_ptr_tag(TcTypeTag type);
int tc_type_is_memblock_tag(TcTypeTag type);
int tc_type_is_struct_tag(TcTypeTag type);
int tc_type_parse(const char *text, TcTypeTag *out);

/* 完整类型（A-2～A-4） */
TcType tc_type_scalar(TcTypeTag tag);
/** 标量标签 → 完整类型（tc_type_scalar 的规范命名） */
TcType tc_type_from_tag(TcTypeTag tag);
/** 完整类型 → 标量标签；标量/void 时即 tag 本身 */
TcTypeTag tc_type_scalar_tag(const TcType *type);
/**
 * 返回标签对应的进程内规范单例（永不释放）。
 * 标量/void/复合「仅标签」场景均可用；复合完整类型请走 TcTypeTable。
 */
const TcType *tc_type_tag_singleton(TcTypeTag tag);
/** 单例表（供静态初始化器取地址；优先用 tc_type_tag_singleton） */
extern const TcType tc_type_singletons[TC_TYPE_TAG_COUNT];
/** 静态初始化用：&tc_type_singletons[TC_INT32] */
#define TC_TYPE_PTR(tag) (&tc_type_singletons[(tag)])
/** 从完整类型取标签；NULL 不安全，调用方须保证非空 */
static inline TcTypeTag tc_type_tag_of(const TcType *type) {
    return type->tag;
}

/** memblock 声明 N；非 memblock 或 NULL 返回 0 */
static inline uint64_t tc_type_memblock_count(const TcType *type) {
    if (!type || type->tag != TC_MEMBLOCK) {
        return 0;
    }
    return type->params.memblock_type.count;
}

/**
 * memblock 规划个数 N 是否不一致。仅用于赋值、传参、返回、初始化等
 * 「N 必须一致」的上下文（TC_CE_MEMBLOCK_SIZE_MISMATCH）；类型等价判定
 * 本身仍由 tc_type_equals（忽略 N）决定。非 memblock 或 NULL 返回 0。
 */
static inline int tc_type_memblock_count_mismatch(const TcType *a, const TcType *b) {
    if (!a || !b || a->tag != TC_MEMBLOCK || b->tag != TC_MEMBLOCK) {
        return 0;
    }
    return a->params.memblock_type.count != b->params.memblock_type.count;
}

/** struct_id；非 struct 或 NULL 返回 -1 */
static inline int tc_type_struct_id(const TcType *type) {
    if (!type || type->tag != TC_STRUCT) {
        return -1;
    }
    return type->params.struct_type.struct_id;
}

void tc_type_table_init(TcTypeTable *table);
void tc_type_table_free(TcTypeTable *table);
/**
 * 将类型 intern 进池并返回稳定指针。
 * 标量/void 返回单例（不入池）。复合类型深拷贝子树；memblock 按 T+N 区分节点
 *（equals 仍忽略 N；指针相等是更强关系）。
 * @return 非 NULL 成功；OOM 时返回 NULL 并写 diag
 */
const TcType *tc_type_intern(TcTypeTable *table, const TcType *type, TcDiagnostic *diag);

TcType tc_type_make_ptr(TcType *pointee);
TcType tc_type_make_memblock(TcType *element, uint64_t count);
TcType tc_type_make_struct(int struct_id);
/** 构造未决结构体类型（struct_id=-1 + pending_name）；仅 Parser 阶段使用 */
TcType tc_type_make_struct_pending(const char *name);
void tc_type_free(TcType *type);
/** 深拷贝完整类型；失败返回 -1（OOM 时 *out 未修改） */
int tc_type_copy(const TcType *src, TcType *out, TcDiagnostic *diag);
int tc_type_equals(const TcType *a, const TcType *b);
size_t tc_target_ptr_width_bits(void);
size_t tc_sizeof_bits(const TcType *type);
/* 结构体宽度表回调；tc_sizeof_bits_ex 显式传入，struct 未注册时宽度为 0 */
typedef size_t (*TcStructWidthFn)(int struct_id, void *userdata);
size_t tc_sizeof_bits_ex(const TcType *type, TcStructWidthFn fn, void *userdata);

int tc_float_mode_parse(const char *text, TcFloatMode *out);
int tc_arith_op_parse(const char *text, TcArithOp *out);
int tc_unary_op_parse(const char *text, TcUnaryOp *out);
int tc_compare_op_parse(const char *text, TcCompareOp *out);
int tc_logic_op_parse(const char *text, TcLogicOp *out);
int tc_bitwise_op_parse(const char *text, TcBitwiseOp *out);
int tc_shift_op_parse(const char *text, TcShiftOp *out);
int tc_format_spec_parse(const char *text, TcFormatFullSpec *out);
const char *tc_bitwise_op_name(TcBitwiseOp op);
const char *tc_shift_op_name(TcShiftOp op);
const char *tc_format_spec_name(TcFormatSpec fmt);
const char *tc_error_kind_name(TcErrorKind kind);
const char *tc_api_error_code_name(TcApiErrorCode code);
const char *tc_warning_kind_name(TcWarningKind kind);
const char *tc_type_name(TcTypeTag type);

/* A-8 运行时槽组 */
void tc_runtime_slots_init(TcRuntimeSlots *slots);
void tc_runtime_slots_free(TcRuntimeSlots *slots);

#endif
