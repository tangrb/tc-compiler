/* TC 0.0.46 contract: IR root container (design §3.4).
 *
 * One TcIR is bound to one profile.  Types may be shared across profiles, but
 * struct layout, objects, units and CT results are instantiated per profile and
 * must never be reused across 32/64.
 */
#ifndef TC_IR_IR_H
#define TC_IR_IR_H

#include <stddef.h>
#include <stdint.h>

#include "ir/address.h"
#include "ir/block.h"
#include "ir/instr.h"
#include "ir/object.h"
#include "ir/operand.h"
#include "ir/span.h"
#include "ir/structs.h"
#include "ir/type.h"
#include "runtime/profile.h"

typedef enum {
    TC_PARAM_NONNULL = 1u << 0,
    TC_PARAM_NO_ALIAS = 1u << 1,
    TC_PARAM_ALIGN = 1u << 2,
    TC_PARAM_RANGE = 1u << 3
} TcParamFlags;

/* SEM-checked optimizer preconditions.  fnptr does not carry them and the
 * runtime never checks them; violating one is UB (standard §3.6/§8.5/§8.6). */
typedef struct {
    const char *name;
    const TcType *type;
    TcParamFlags flags;
    uint32_t align; /* @align(N) when flags & ALIGN */
    uint64_t lo;    /* @range(lo,hi) bit patterns, interpreted by type */
    uint64_t hi;
} TcParam;

/* One module instance.  Identity is the resolved file path, never the module
 * name (standard §4.5): same name on different paths is a distinct instance. */
typedef struct {
    uint32_t id; /* index in TcIR.modules == module_order index */
    const char *path;
} TcModule;

typedef struct {
    const TcType *ret;
    TcParam *params;
    size_t nparams;
} TcFuncSig;

typedef enum {
    TC_FN_UNSAFE = 1u << 0,
    TC_FN_SAFE = 1u << 1,
    TC_FN_FAST = 1u << 2,
    TC_FN_FAST_MATH = 1u << 3,
    TC_FN_PURE = 1u << 4,
    TC_FN_READONLY = 1u << 5,
    TC_FN_NOUNWIND = 1u << 6,
    TC_FN_WILLRETURN = 1u << 7,
    TC_FN_NO_OVERFLOW = 1u << 8,
    TC_FN_INLINE = 1u << 9,
    TC_FN_NOINLINE = 1u << 10
} TcFuncFlags;

typedef struct {
    uint32_t id;
    uint32_t module; /* module instance id */
    const char *name;
    TcFuncSig sig;
    TcFuncFlags flags;
    TcUnit *body;
} TcFunc;

typedef struct TcInitImage {
    const uint8_t *bytes;
    size_t size;
} TcInitImage;

typedef struct {
    uint32_t id;
    uint32_t module; /* module instance id */
    const char *name;
    const TcType *type;
    uint64_t bits;             /* scalar const bit pattern */
    const TcInitImage *init;   /* aggregate const / static initializer */
} TcConst;

typedef struct {
    TcTypeTable *types; /* frozen after Analyze */
    TcModule *modules;  /* module_order; instance key = path */
    size_t nmodules;
    TcStructDef *structs;
    size_t nstructs;
    TcFunc *funcs;
    size_t nfuncs;
    TcConst *consts;
    size_t nconsts;
    TcObject *objects;
    size_t nobjects;
    TcUnit *units;
    size_t nunits;
    const TcPlatformProfile *profile; /* W/ABI/region table */
} TcIR;

#endif /* TC_IR_IR_H */
