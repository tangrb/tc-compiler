/* TC 0.0.46 CT unit vectors (standard §3.10/§6.3-§6.6, §13.2.2).
 *
 * Every expectation is derived from the standard, not from compiler output:
 * integer strict/mode rules, IEEE strict arithmetic and exception priority,
 * cast/trunc/bitcast, exact floating `mod`, sizeof/layout and the 32/64 word
 * width matrix.  The case ids are referenced by the coverage matrix.
 */
/* mkdtemp is BSD, unlink is POSIX; strict ISO C hides both. */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frontend/ct.h"
#include "frontend/sem.h"
#include "ir/type.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va_ = (long long)(a);                                        \
        long long vb_ = (long long)(b);                                        \
        checks++;                                                              \
        if (va_ != vb_) {                                                      \
            printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, \
                   #a, #b, va_, vb_);                                          \
            failures++;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    const char *id;
    const char *structs; /* declarations inserted before the consts */
    const char *prelude; /* extra const declarations */
    const char *type;
    const char *expr;
    unsigned width;
    TcDiagCode error;      /* TC_CE_SYNTAX == 0 means success */
    uint64_t bits;
    const char *image;     /* expected byte image for struct values, or NULL */
} CtVector;

/* TC_CE_SYNTAX is the zero enumerator, so a NULL/zero error means "no CT
 * diagnostic" for this table; CT never reports SYNTAX. */
#define CT_OK TC_CE_SYNTAX

static const CtVector VECTORS[] = {
    /* Integer strict / unsigned wrap (standard §6.3 mode matrix). */
    {"TC-CT-UNIT-0001", NULL, NULL, "int8", "add int8, 127, 1", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0002", NULL, NULL, "int16", "sub int16, -32768, 1", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0003", NULL, NULL, "int16", "mul int16, -32768, -1", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0004", NULL, NULL, "uint8", "add uint8, 255, 1", 64, CT_OK,
     0x00, NULL},
    {"TC-CT-UNIT-0005", NULL, NULL, "uint8", "sub uint8, 0, 1", 64, CT_OK, 0xff,
     NULL},
    {"TC-CT-UNIT-0006", NULL, NULL, "uint16", "mul uint16, 65535, 65535", 64,
     CT_OK, 0x0001, NULL},
    {"TC-CT-UNIT-0007", NULL, NULL, "int8", "neg int8, -128", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0008", NULL, NULL, "int8", "abs int8, -128", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0009", NULL, NULL, "int32", "neg int32, 2147483647", 64,
     CT_OK, 0x80000001u, NULL},
    {"TC-CT-UNIT-0010", NULL, NULL, "uint32", "abs uint32, 2147483648", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0011", NULL, NULL, "uint32", "neg uint32, 1", 64, CT_OK,
     0xffffffffu, NULL},
    {"TC-CT-UNIT-0012", NULL, NULL, "int32", "neg int32, -2147483648", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0013", NULL, NULL, "uint8", "not uint8, 0", 64, CT_OK, 0xff,
     NULL},
    {"TC-CT-UNIT-0014", NULL, NULL, "bool", "and bool, true, false", 64, CT_OK,
     0, NULL},
    {"TC-CT-UNIT-0015", NULL, NULL, "bool", "or bool, false, true", 64, CT_OK,
     1, NULL},
    {"TC-CT-UNIT-0016", NULL, NULL, "bool", "xor bool, true, true", 64, CT_OK,
     0, NULL},
    {"TC-CT-UNIT-0017", NULL, NULL, "bool", "lt int32, -1, 0", 64, CT_OK, 1,
     NULL},
    {"TC-CT-UNIT-0018", NULL, NULL, "bool", "lt uint32, 0, 4294967295", 64,
     CT_OK, 1, NULL},
    /* Integer div/mod edges and priorities (standard §6.3). */
    {"TC-CT-UNIT-0019", NULL, NULL, "int32", "div int32, -7, 2", 64, CT_OK,
     0xfffffffd, NULL},
    {"TC-CT-UNIT-0020", NULL, NULL, "int32", "mod int32, -7, 2", 64, CT_OK,
     0xffffffff, NULL},
    {"TC-CT-UNIT-0021", NULL, NULL, "int32", "mod int32, 7, -2", 64, CT_OK, 1,
     NULL},
    {"TC-CT-UNIT-0022", NULL, NULL, "int32", "div int32, -2147483648, -1", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0023", NULL, NULL, "int32", "mod int32, -2147483648, -1", 64,
     CT_OK, 0, NULL},
    {"TC-CT-UNIT-0024", NULL, NULL, "int32", "div int32, 1, 0", 64,
     TC_CE_CONST_DIV_ZERO, 0, NULL},
    {"TC-CT-UNIT-0025", NULL, NULL, "uint32", "mod uint32, 1, 0", 64,
     TC_CE_CONST_DIV_ZERO, 0, NULL},
    {"TC-CT-UNIT-0026", NULL, NULL, "uint32", "div uint32, 4294967295, 2", 64,
     CT_OK, 0x7fffffff, NULL},
    {"TC-CT-UNIT-0027", NULL, NULL, "uint64",
     "mod uint64, 18446744073709551615u, 10", 64, CT_OK, 5, NULL},
    /* Shifts (standard §6.3; negative count precedes overflow). */
    {"TC-CT-UNIT-0028", NULL, NULL, "int8", "shl int8, -1, 7", 64, CT_OK, 0x80,
     NULL},
    {"TC-CT-UNIT-0029", NULL, NULL, "int8", "shl int8, 1, 7", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0030", NULL, NULL, "int8", "shl int8, 0, 8", 64, CT_OK, 0,
     NULL},
    {"TC-CT-UNIT-0031", NULL, NULL, "int8", "shl int8, 1, 8", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0032", NULL, NULL, "uint8", "shl uint8, 128, 1", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0033", NULL, NULL, "uint8", "shl uint8, 128, 0", 64, CT_OK,
     0x80, NULL},
    {"TC-CT-UNIT-0034", NULL, NULL, "int32", "shl int32, 1, -1", 64,
     TC_CE_CONST_SHIFT, 0, NULL},
    {"TC-CT-UNIT-0035", NULL, NULL, "int8", "shl int8, 0, -1", 64,
     TC_CE_CONST_SHIFT, 0, NULL},
    {"TC-CT-UNIT-0036", NULL, NULL, "int8", "shr int8, -128, 7", 64, CT_OK,
     0xff, NULL},
    {"TC-CT-UNIT-0037", NULL, NULL, "int8", "shr int8, -128, 8", 64, CT_OK, 0,
     NULL},
    {"TC-CT-UNIT-0038", NULL, NULL, "uint8", "shr uint8, 255, 4", 64, CT_OK,
     0x0f, NULL},
    {"TC-CT-UNIT-0039", NULL, NULL, "int64", "shr int64, -1, 64", 64, CT_OK, 0,
     NULL},
    /* 32/64 word-size matrix (standard §13.2.2). */
    {"TC-CT-UNIT-0040", NULL, NULL, "isize", "add isize, 2147483647, 1", 32,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0041", NULL, NULL, "isize", "add isize, 2147483647, 1", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0042", NULL, NULL, "isize", "neg isize, -2147483648", 32,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0043", NULL, NULL, "isize", "neg isize, -2147483648", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0044", NULL, NULL, "isize", "shl isize, 1, 31", 32,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0045", NULL, NULL, "isize", "shl isize, 1, 31", 64, CT_OK,
     0x80000000u, NULL},
    {"TC-CT-UNIT-0046", NULL, NULL, "isize", "cast isize, 2147483648", 32,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0047", NULL, NULL, "isize", "cast isize, 2147483648", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0048", NULL, NULL, "isize", "cast isize, 3.4e9", 32,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0049", NULL, NULL, "isize", "cast isize, 3.4e9", 64, CT_OK,
     0xcaa7e200u, NULL},
    {"TC-CT-UNIT-0050", NULL, NULL, "usize", "cast usize, 4294967296", 32,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0051", NULL, NULL, "usize", "cast usize, 4294967296", 64,
     CT_OK, 0x100000000ull, NULL},
    {"TC-CT-UNIT-0052", NULL, NULL, "int64", "add int64, 9223372036854775807, 1",
     32, TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0053", NULL, NULL, "int64", "shl int64, 1, 63", 32,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0054", NULL, NULL, "uint64", "shl uint64, 1, 63", 32, CT_OK,
     0x8000000000000000ull, NULL},
    {"TC-CT-UNIT-0055", NULL, NULL, "uint64",
     "add uint64, 18446744073709551615u, 1", 64, CT_OK, 0, NULL},
    /* sizeof (standard §6.13/§13.2.1). */
    {"TC-CT-UNIT-0056", NULL, NULL, "usize", "sizeof int32", 64, CT_OK, 4,
     NULL},
    {"TC-CT-UNIT-0057", NULL, NULL, "usize", "sizeof isize", 32, CT_OK, 4,
     NULL},
    {"TC-CT-UNIT-0058", NULL, NULL, "usize", "sizeof isize", 64, CT_OK, 8,
     NULL},
    {"TC-CT-UNIT-0059", NULL, NULL, "usize", "sizeof ptr<int8>", 32, CT_OK, 4,
     NULL},
    {"TC-CT-UNIT-0060", NULL, NULL, "usize", "sizeof ptr<int8>", 64, CT_OK, 8,
     NULL},
    {"TC-CT-UNIT-0061", NULL, NULL, "usize", "sizeof fnptr<int32>", 32, CT_OK,
     4, NULL},
    {"TC-CT-UNIT-0062", NULL, NULL, "usize", "sizeof fnptr<int32>", 64, CT_OK,
     8, NULL},
    {"TC-CT-UNIT-0063",
     "public struct P then\n"
     "    var a: int8\n"
     "    var b: int32 @padding(4)\n"
     "    var c: int16\n"
     "end\n",
     NULL, "usize", "sizeof P", 64, CT_OK, 11, NULL},
    {"TC-CT-UNIT-0064",
     "public struct Q then\n"
     "    var a: int8\n"
     "    var b: usize\n"
     "    var c: float64\n"
     "end\n",
     NULL, "usize", "sizeof Q", 32, CT_OK, 13, NULL},
    {"TC-CT-UNIT-0065",
     "public struct Q then\n"
     "    var a: int8\n"
     "    var b: usize\n"
     "    var c: float64\n"
     "end\n",
     NULL, "usize", "sizeof Q", 64, CT_OK, 17, NULL},
    /* cast (standard §6.6.1). */
    {"TC-CT-UNIT-0066", NULL, NULL, "uint8", "cast uint8, 255", 64, CT_OK,
     0xff, NULL},
    {"TC-CT-UNIT-0067", NULL, NULL, "uint8", "cast uint8, 256", 64,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0068", NULL, NULL, "int8", "cast int8, -128", 64, CT_OK, 0x80,
     NULL},
    {"TC-CT-UNIT-0069", NULL, NULL, "int8", "cast int8, 128", 64,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0070", NULL, NULL, "uint8", "cast uint8, -1", 64,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0071", NULL, NULL, "int64",
     "cast int64, 18446744073709551615u", 64, TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0072", NULL, NULL, "int32", "cast int32, 3.4e10", 64,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0073", NULL, NULL, "int32", "cast int32, -2147483648.9", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0074", NULL, NULL, "int8", "cast int8, 127.9", 64, CT_OK, 0x7f,
     NULL},
    {"TC-CT-UNIT-0075", NULL, NULL, "int8", "cast int8, -128.9", 64, CT_OK,
     0x80, NULL},
    {"TC-CT-UNIT-0076", NULL, NULL, "int8", "cast int8, 128.0", 64,
     TC_CE_CONST_CAST, 0, NULL},
    {"TC-CT-UNIT-0077", NULL, NULL, "bool", "cast bool, 2", 64, CT_OK, 1,
     NULL},
    {"TC-CT-UNIT-0078", NULL, NULL, "bool", "cast bool, -0.0", 64, CT_OK, 0,
     NULL},
    {"TC-CT-UNIT-0079", NULL, NULL, "int32", "cast int32, true", 64, CT_OK, 1,
     NULL},
    {"TC-CT-UNIT-0080", NULL, NULL, "float32", "cast float32, 16777217", 64,
     CT_OK, 0x4b800000u, NULL},
    {"TC-CT-UNIT-0081", NULL, NULL, "float32", "cast float32, 16777219", 64,
     CT_OK, 0x4b800002u, NULL},
    {"TC-CT-UNIT-0082", NULL, NULL, "float64", "cast float64, 9007199254740993",
     64, CT_OK, 0x4340000000000000ull, NULL},
    {"TC-CT-UNIT-0083", NULL, NULL, "float64", "cast float64, 9007199254740995",
     64, CT_OK, 0x4340000000000002ull, NULL},
    {"TC-CT-UNIT-0084", NULL, NULL, "float64",
     "cast float64, 9223372036854775807", 64, CT_OK,
     0x43e0000000000000ull, NULL},
    {"TC-CT-UNIT-0085", NULL, NULL, "float32", "cast float32, 1.5e-45", 64,
     CT_OK, 0x00000001u, NULL},
    {"TC-CT-UNIT-0086", NULL, NULL, "float32", "cast float32, 5.0e-46", 64,
     CT_OK, 0, NULL},
    {"TC-CT-UNIT-0087", NULL, NULL, "int8", "trunc int8, 300", 64, CT_OK,
     0x2c, NULL},
    {"TC-CT-UNIT-0088", NULL, NULL, "uint8", "trunc uint8, -1", 64, CT_OK,
     0xff, NULL},
    /* bitcast (standard §6.6.2/§6.6.3). */
    {"TC-CT-UNIT-0089", NULL, NULL, "float32", "bitcast float32, 1078530011",
     64, CT_OK, 0x40490fdbu, NULL},
    {"TC-CT-UNIT-0090", NULL,
     "public const PI: float32 = bitcast float32, 1078530011\n", "int32",
     "bitcast int32, Self.PI", 64, CT_OK, 0x40490fdbu, NULL},
    {"TC-CT-UNIT-0091", NULL, NULL, "float32",
     "bitcast float32, 3212836864u", 64, CT_OK, 0xbf800000u, NULL},
    {"TC-CT-UNIT-0092", NULL, NULL, "uint32", "bitcast uint32, -1", 64, CT_OK,
     0xffffffffu, NULL},
    {"TC-CT-UNIT-0093", NULL, NULL, "int32", "bitcast int32, 1.0f", 64, CT_OK,
     0x3f800000u, NULL},
    {"TC-CT-UNIT-0094", NULL, NULL, "int64", "bitcast int64, 1.0", 64, CT_OK,
     0x3ff0000000000000ull, NULL},
    {"TC-CT-UNIT-0095", NULL, NULL, "float32",
     "bitcast float32, 2143289344u", 64, CT_OK, 0x7fc00000u, NULL},
    /* Strict floating arithmetic (standard §6.3/§6.3.1-§6.3.3). */
    {"TC-CT-UNIT-0096", NULL, NULL, "float32", "add float32, 1.0f, 2.0f", 64,
     CT_OK, 0x40400000u, NULL},
    {"TC-CT-UNIT-0097", NULL, NULL, "float32",
     "add float32, 1.0f, 0.000000059604644775390625f", 64, CT_OK,
     0x3f800000u, NULL},
    {"TC-CT-UNIT-0098", NULL, NULL, "float32",
     "add float32, 1.0f, 0.000000178813934326171875f", 64, CT_OK,
     0x3f800002u, NULL},
    {"TC-CT-UNIT-0099", NULL, NULL, "float32",
     "add float32, 1.0e-45f, 1.0e-45f", 64, CT_OK, 0x00000002u, NULL},
    {"TC-CT-UNIT-0100", NULL, NULL, "float32",
     "mul float32, 1.0e-45f, 3.0f", 64, CT_OK, 0x00000003u, NULL},
    {"TC-CT-UNIT-0101", NULL, NULL, "float32",
     "mul float32, 1.0e-45f, 1.5f", 64, TC_CE_CONST_FLOAT_UNDERFLOW, 0, NULL},
    {"TC-CT-UNIT-0102", NULL, NULL, "float32",
     "div float32, 1.0e-45f, 2.0f", 64, TC_CE_CONST_FLOAT_UNDERFLOW, 0, NULL},
    {"TC-CT-UNIT-0103", NULL, NULL, "float32",
     "add float32, 3.4028235e38f, 3.4028235e38f", 64,
     TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0104", NULL, NULL, "float32",
     "mul float32, 3.4028235e38f, 2.0f", 64, TC_CE_CONST_OVERFLOW, 0, NULL},
    {"TC-CT-UNIT-0105", NULL, NULL, "float32",
     "mul float32, 3.4028235e38f, 0.5f", 64, CT_OK, 0x7effffffu, NULL},
    {"TC-CT-UNIT-0106", NULL, NULL, "float32", "add float32, inf, -inf", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0107", NULL, NULL, "float32", "sub float32, inf, inf", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0108", NULL, NULL, "float32", "mul float32, 0.0f, inf", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0109", NULL, NULL, "float32", "div float32, 0.0f, 0.0f", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0110", NULL, NULL, "float32", "div float32, inf, inf", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0111", NULL, NULL, "float32", "div float32, 1.0f, 0.0f", 64,
     TC_CE_CONST_DIV_ZERO, 0, NULL},
    {"TC-CT-UNIT-0112", NULL, NULL, "float32",
     "div float32, 3.4028235e38f, 0.0f", 64, TC_CE_CONST_DIV_ZERO, 0, NULL},
    {"TC-CT-UNIT-0113", NULL, NULL, "float32", "div float32, inf, 0.0f", 64,
     CT_OK, 0x7f800000u, NULL},
    {"TC-CT-UNIT-0114", NULL, NULL, "float32", "add float32, nan, 1.0f", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0115", NULL, NULL, "float32", "div float32, 1.0f, 3.0f", 64,
     CT_OK, 0x3eaaaaabu, NULL},
    {"TC-CT-UNIT-0116", NULL, NULL, "float64", "div float64, 1.0, 3.0", 64,
     CT_OK, 0x3fd5555555555555ull, NULL},
    {"TC-CT-UNIT-0117", NULL, NULL, "float32", "add float32, -0.0f, 0.0f", 64,
     CT_OK, 0, NULL},
    {"TC-CT-UNIT-0118", NULL, NULL, "float32", "add float32, -0.0f, -0.0f", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0119", NULL, NULL, "float32", "neg float32, -0.0f", 64, CT_OK,
     0, NULL},
    /* Floating mod (standard §6.3.3). */
    {"TC-CT-UNIT-0120", NULL, NULL, "float32", "mod float32, 1.0f, 3.0f", 64,
     CT_OK, 0x3f800000u, NULL},
    {"TC-CT-UNIT-0121", NULL, NULL, "float64", "mod float64, -1.0, 3.0", 64,
     CT_OK, 0xbff0000000000000ull, NULL},
    {"TC-CT-UNIT-0122", NULL, NULL, "float32", "mod float32, 7.0f, -3.0f", 64,
     CT_OK, 0x3f800000u, NULL},
    {"TC-CT-UNIT-0123", NULL, NULL, "float32", "mod float32, -0.0f, 3.0f", 64,
     CT_OK, 0x80000000u, NULL},
    {"TC-CT-UNIT-0124", NULL, NULL, "float32", "mod float32, 1.0f, 0.0f", 64,
     TC_CE_CONST_DIV_ZERO, 0, NULL},
    {"TC-CT-UNIT-0125", NULL, NULL, "float32", "mod float32, 0.0f, 0.0f", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0126", NULL, NULL, "float32", "mod float32, inf, 0.0f", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0127", NULL, NULL, "float32", "mod float32, inf, 2.0f", 64,
     TC_CE_CONST_FLOAT_INVALID, 0, NULL},
    {"TC-CT-UNIT-0128", NULL, NULL, "float32", "mod float32, 1.0f, inf", 64,
     CT_OK, 0x3f800000u, NULL},
    {"TC-CT-UNIT-0129", NULL, NULL, "float32",
     "mod float32, 100000000.0f, 3.0f", 64, CT_OK, 0x3f800000u, NULL},
    {"TC-CT-UNIT-0130", NULL,
     "public const MS: float64 = bitcast float64, 1\n", "float64",
     "mod float64, 1.0, Self.MS", 64, CT_OK, 0, NULL},
    {"TC-CT-UNIT-0131", NULL,
     "public const MS: float64 = bitcast float64, 1\n", "float64",
     "mod float64, Self.MS, 3.0", 64, CT_OK, 1, NULL},
    /* Floating comparisons with NaN (standard §6.5: no trap). */
    {"TC-CT-UNIT-0132", NULL, NULL, "bool", "eq float32, nan, nan", 64, CT_OK,
     0, NULL},
    {"TC-CT-UNIT-0133", NULL, NULL, "bool", "ne float32, nan, nan", 64, CT_OK,
     1, NULL},
    {"TC-CT-UNIT-0134", NULL, NULL, "bool", "lt float32, 1.0f, nan", 64, CT_OK,
     0, NULL},
    {"TC-CT-UNIT-0135", NULL, NULL, "bool", "ge float32, nan, inf", 64, CT_OK,
     0, NULL},
    {"TC-CT-UNIT-0136", NULL, NULL, "bool", "eq float32, -0.0f, 0.0f", 64,
     CT_OK, 1, NULL},
    /* NaN payload: sign ops only touch the sign bit. */
    {"TC-CT-UNIT-0137", NULL,
     "public const N32: float32 = bitcast float32, 2143289345u\n", "float32",
     "neg float32, Self.N32", 64, CT_OK, 0xffc00001u, NULL},
    {"TC-CT-UNIT-0138", NULL,
     "public const N32: float32 = bitcast float32, 4290772992u\n", "float32",
     "abs float32, Self.N32", 64, CT_OK, 0x7fc00000u, NULL},
    /* nullptr / bool consts and const struct field chains. */
    {"TC-CT-UNIT-0140", NULL, NULL, "ptr<int32>", "nullptr", 64, CT_OK, 0,
     NULL},
    {"TC-CT-UNIT-0141", NULL, NULL, "fnptr<int32>", "nullptr", 64, CT_OK, 0,
     NULL},
    {"TC-CT-UNIT-0142", NULL, NULL, "bool", "true", 64, CT_OK, 1, NULL},
    {"TC-CT-UNIT-0143",
     "public struct In then\n"
     "    var q: int32\n"
     "end\n"
     "public struct Out then\n"
     "    var i: In\n"
     "    var b: bool\n"
     "end\n",
     "public const I: In = make In, q: 9\n"
     "public const O: Out = make Out, i: Self.I, b: true\n",
     "int32", "add int32, Self.O.i.q, 1", 64, CT_OK, 10, NULL},
    {"TC-CT-UNIT-0144",
     "public struct P then\n"
     "    var a: int8\n"
     "    var b: int32 @padding(4)\n"
     "    var c: int16\n"
     "end\n",
     "public const P0: P = make P, 1, 2, 3\n",
     "P", "Self.P0", 64, CT_OK, 0,
     "0102000000000000000300"},
    /* Tininess after rounding: a rounded min-normal result is not tiny even
     * when the exact product was a subnormal value (standard §6.3.2). */
    {"TC-CT-UNIT-0145", NULL,
     "public const LS: float32 = bitcast float32, 8388607\n"
     "public const U1: float32 = bitcast float32, 1065353217\n",
     "float32", "mul float32, Self.LS, Self.U1", 64, CT_OK, 0x00800000u,
     NULL},
    /* Exact vs inexact binary64 subnormal results. */
    {"TC-CT-UNIT-0146", NULL,
     "public const MS: float64 = bitcast float64, 1\n", "float64",
     "mul float64, Self.MS, 3.0", 64, CT_OK, 3, NULL},
    {"TC-CT-UNIT-0147", NULL,
     "public const MS: float64 = bitcast float64, 1\n", "float64",
     "mul float64, Self.MS, 1.5", 64, TC_CE_CONST_FLOAT_UNDERFLOW, 0, NULL},
    {"TC-CT-UNIT-0148", NULL, NULL, "float32",
     "mul float32, 1.0e-45f, 0.25f", 64, TC_CE_CONST_FLOAT_UNDERFLOW, 0, NULL},
    /* Aggregate const value and image (design §3.4). */
    {"TC-CT-UNIT-0139",
     "public struct P then\n"
     "    var a: int8\n"
     "    var b: int32 @padding(4)\n"
     "    var c: int16\n"
     "end\n",
     NULL, "P", "make P, a: 1, b: 2, c: 3", 64, CT_OK, 0,
     "0102000000000000000300"},
};

static int write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static char *read_image_hex(const TcInitImage *image) {
    static char buffer[4096];
    size_t i;
    if (image == NULL || image->size * 2u + 1u > sizeof(buffer)) {
        return NULL;
    }
    for (i = 0; i < image->size; i++) {
        snprintf(buffer + i * 2u, 3, "%02x", image->bytes[i]);
    }
    buffer[image->size * 2u] = '\0';
    return buffer;
}

static void run_vector(const CtVector *vector) {
    char dir[] = "/tmp/tc-ct-XXXXXX";
    char lib_path[512];
    char root_path[512];
    char source[8192];
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    size_t const_count = 0;
    const char *scan;
    size_t column = 0;
    size_t lineno;
    if (mkdtemp(dir) == NULL) {
        printf("FAIL %s: cannot create temp dir\n", vector->id);
        failures++;
        return;
    }
    snprintf(lib_path, sizeof(lib_path), "%s/lib.tc", dir);
    snprintf(root_path, sizeof(root_path), "%s/root.tc", dir);
    snprintf(source, sizeof(source), "#lib\n%s%spublic const V: %s = %s\n",
             vector->structs != NULL ? vector->structs : "",
             vector->prelude != NULL ? vector->prelude : "", vector->type,
             vector->expr);
    if (!write_text_file(lib_path, source) ||
        !write_text_file(root_path, "#program\nimport lib\n")) {
        printf("FAIL %s: cannot write sources\n", vector->id);
        failures++;
        unlink(lib_path);
        unlink(root_path);
        rmdir(dir);
        return;
    }
    for (scan = source; (scan = strstr(scan, "public const ")) != NULL;
         scan += 1) {
        const_count++;
    }
    /* Position of the target declaration name (V). */
    {
        const char *marker = strstr(source, "public const V:");
        lineno = 1;
        for (scan = source; scan < marker; scan++) {
            if (*scan == '\n') {
                lineno++;
            }
        }
        column = (size_t)(marker - source) + strlen("public const ") + 1u;
        {
            const char *line_start = marker;
            while (line_start > source && line_start[-1] != '\n') {
                line_start--;
            }
            column = (size_t)(marker - line_start) + 1u +
                     strlen("public const ");
        }
    }
    an = tc_analyze(root_path, vector->width, NULL, 0, &status, &diag, &impl);
    if (an == NULL) {
        printf("FAIL %s: analysis NULL impl=%d\n", vector->id, (int)impl);
        failures++;
        unlink(lib_path);
        unlink(root_path);
        rmdir(dir);
        return;
    }
    if (vector->error != CT_OK) {
        checks++;
        if (status != TC_ANALYSIS_DIAGNOSTIC ||
            diag.stage != TC_STAGE_CT || diag.code != vector->error ||
            diag.span.line != lineno || diag.span.column != column) {
            printf("FAIL %s: expected CT code %d at %zu:%zu, got status=%d "
                   "code=%d stage=%d at %u:%u\n",
                   vector->id, (int)vector->error, lineno, column,
                   (int)status, (int)diag.code, (int)diag.stage,
                   diag.span.line, diag.span.column);
            failures++;
        }
        tc_analysis_destroy(an);
    } else {
        const TcCtValue *value;
        checks++;
        if (status != TC_ANALYSIS_OK) {
            printf("FAIL %s: expected OK, got status=%d code=%d at %u:%u\n",
                   vector->id, (int)status, (int)diag.code, diag.span.line,
                   diag.span.column);
            failures++;
            tc_analysis_destroy(an);
            unlink(lib_path);
            unlink(root_path);
            rmdir(dir);
            return;
        }
        value = tc_ct_module_value(an, 1, const_count - 1u);
        checks++;
        if (value == NULL) {
            printf("FAIL %s: no CT value\n", vector->id);
            failures++;
        } else if (vector->image != NULL) {
            int ok = 0;
            const TcInitImage *image = tc_ct_value_image(an, value, &ok);
            char *hex = read_image_hex(image);
            checks++;
            if (!ok || hex == NULL || strcmp(hex, vector->image) != 0) {
                printf("FAIL %s: image %s, expected %s\n", vector->id,
                       hex != NULL ? hex : "<none>", vector->image);
                failures++;
            }
        } else {
            checks++;
            if (value->kind != TC_CT_SCALAR || value->bits != vector->bits) {
                printf("FAIL %s: bits 0x%llx, expected 0x%llx\n", vector->id,
                       (unsigned long long)value->bits,
                       (unsigned long long)vector->bits);
                failures++;
            }
        }
        tc_analysis_destroy(an);
    }
    unlink(lib_path);
    unlink(root_path);
    rmdir(dir);
}

/* Cache and demand-order tests (standard §4.10). */
static TcAnalysis *analyze_diamond(TcAnalysisStatus *status,
                                   TcAnalysisDiag *diag) {
    char dir[] = "/tmp/tc-ct-cache-XXXXXX";
    char root[512];
    char a[512];
    char b[512];
    char c[512];
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    if (mkdtemp(dir) == NULL) {
        return NULL;
    }
    snprintf(root, sizeof(root), "%s/root.tc", dir);
    snprintf(a, sizeof(a), "%s/a.tc", dir);
    snprintf(b, sizeof(b), "%s/b.tc", dir);
    snprintf(c, sizeof(c), "%s/c.tc", dir);
    write_text_file(root, "#program\nimport a\nimport b\n");
    write_text_file(a,
                    "#lib\nimport c\npublic const A: int32 = add int32, "
                    "c.K, 1\n");
    write_text_file(b,
                    "#lib\nimport c\npublic const B: int32 = add int32, "
                    "c.K, 2\n");
    write_text_file(c, "#lib\npublic const K: int32 = add int32, 40, 2\n");
    an = tc_analyze(root, 64, NULL, 0, status, diag, &impl);
    unlink(root);
    unlink(a);
    unlink(b);
    unlink(c);
    rmdir(dir);
    return an;
}

static void test_ct_cache(void) {
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an = analyze_diamond(&status, &diag);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    /* K is evaluated once even though a and b both depend on it. */
    CHECK_EQ(tc_ct_eval_count(an), 3);
    {
        const TcCtValue *k = tc_ct_module_value(an, 2, 0);
        const TcCtValue *a = tc_ct_module_value(an, 1, 0);
        const TcCtValue *b = tc_ct_module_value(an, 3, 0);
        CHECK(k != NULL && k->bits == 42);
        CHECK(a != NULL && a->bits == 43);
        CHECK(b != NULL && b->bits == 44);
    }
    tc_analysis_destroy(an);
}

static void test_ct_failure_cache(void) {
    char dir[] = "/tmp/tc-ct-fail-XXXXXX";
    char lib[512];
    char root[512];
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    if (mkdtemp(dir) == NULL) {
        CHECK(0);
        return;
    }
    snprintf(lib, sizeof(lib), "%s/lib.tc", dir);
    snprintf(root, sizeof(root), "%s/root.tc", dir);
    write_text_file(
        lib,
        "#lib\n"
        "public const A: int32 = div int32, 1, 0\n"
        "public const B: int32 = add int32, Self.A, Self.A\n");
    write_text_file(root, "#program\nimport lib\n");
    an = tc_analyze(root, 64, NULL, 0, &status, &diag, &impl);
    unlink(lib);
    unlink(root);
    rmdir(dir);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    /* First failure stops CT; the failing declaration is evaluated once. */
    CHECK(status == TC_ANALYSIS_DIAGNOSTIC);
    CHECK(diag.stage == TC_STAGE_CT);
    CHECK(diag.code == TC_CE_CONST_DIV_ZERO);
    CHECK_EQ(diag.span.line, 2);
    CHECK_EQ(tc_ct_eval_count(an), 1);
    CHECK(tc_analysis_ir(an) == NULL);
    tc_analysis_destroy(an);
}

/* Local const evaluation and its cache (standard §5.2). */
static void test_local_const(void) {
    char dir[] = "/tmp/tc-ct-local-XXXXXX";
    char lib[512];
    char root[512];
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    if (mkdtemp(dir) == NULL) {
        CHECK(0);
        return;
    }
    snprintf(lib, sizeof(lib), "%s/lib.tc", dir);
    snprintf(root, sizeof(root), "%s/root.tc", dir);
    write_text_file(
        lib,
        "#lib\n"
        "public const A: int32 = add int32, 1, 2\n"
        "public func f() int32 then\n"
        "    const X: int32 = add int32, Self.A, 4\n"
        "    const Y: int32 = add int32, X, 5\n"
        "    var r: int32 = copy int32, Y\n"
        "    _ = return r\n"
        "end\n");
    write_text_file(root, "#program\nimport lib\n");
    an = tc_analyze(root, 64, NULL, 0, &status, &diag, &impl);
    unlink(lib);
    unlink(root);
    rmdir(dir);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    CHECK_EQ(tc_ct_eval_count(an), 3);
    {
        const TcCtValue *x = tc_ct_local_value(an, 1, 1, 0);
        const TcCtValue *y = tc_ct_local_value(an, 1, 1, 1);
        CHECK(x != NULL && x->bits == 7);
        CHECK(y != NULL && y->bits == 12);
    }
    tc_analysis_destroy(an);
}

int main(void) {
    size_t i;
    for (i = 0; i < sizeof(VECTORS) / sizeof(VECTORS[0]); i++) {
        run_vector(&VECTORS[i]);
    }
    test_ct_cache();
    test_ct_failure_cache();
    test_local_const();
    if (failures != 0) {
        printf("ct.unit: %d failure(s), %d checks\n", failures, checks);
        return 1;
    }
    printf("ct.unit: %d checks passed\n", checks);
    return 0;
}
