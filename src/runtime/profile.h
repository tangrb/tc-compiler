/* TC 0.0.46 contract: platform profile, target and ABI descriptors (design §8).
 *
 * A profile is one environment definition: W, endianness, five-region table,
 * stack/D, I/O, allocator, trap, exit policy, include roots, device policy,
 * checked-build flag and mandatory ICF disabling.  A TcIR binds exactly one
 * profile; 32-bit and 64-bit results are never compared across profiles.
 *
 * The built-in constructors provide the two 64-bit instances from design §8
 * (aarch64-linux-user, aarch64-baremetal).  The first 32-bit target is selected
 * in S9; tc_profile_validate() already accepts and validates any W=32 profile
 * supplied by tests or a future target.
 */
#ifndef TC_RUNTIME_PROFILE_H
#define TC_RUNTIME_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "ir/region.h"
#include "runtime/alloc.h"
#include "runtime/io.h"
#include "runtime/trap.h"

typedef enum {
    TC_DEV_REJECT = 0, /* device region inaccessible -> ADDR_UNAVAILABLE */
    TC_DEV_READ,       /* readable, writes rejected */
    TC_DEV_READWRITE,  /* readable and writable */
    TC_DEV_VOLATILE    /* only load.volatile/store.volatile */
} TcDevicePolicy;

typedef struct {
    int normal_status; /* normal termination exit status (environment-defined) */
    int trap_status;   /* exit status after the first trap */
} TcExitPolicy;

typedef enum {
    TC_ABI_INT = 0,
    TC_ABI_FP,
    TC_ABI_STACK,
    TC_ABI_INDIRECT
} TcABIClass;

typedef enum {
    TC_ABI_RET_DIRECT = 0,
    TC_ABI_RET_INDIRECT,
    TC_ABI_RET_HFA
} TcABIRetClass;

typedef struct {
    TcABIClass param_int, param_fp, param_ptr, param_fnptr;
    TcABIClass ret_int, ret_fp;
    int hfa;                     /* floating aggregates use HFA rules */
    uint32_t composite_max_bytes;/* AAPCS64 = 16; by-value composite threshold */
    uint32_t hfa_max_regs;       /* AAPCS64 = 4 */
    TcABIRetClass composite_return;
    unsigned stack_align;        /* 8/16 */
    uint32_t callee_saved;       /* register bit mask */
    int int64_pair;              /* 32-bit targets: int64 passed as a pair */
    int float64_pair;            /* 32-bit targets: float64 pair/soft-float */
} TcABIDesc;

typedef struct {
    const char *name;            /* e.g. aarch64-linux-user */
    unsigned addr_width;         /* 32 or 64 */
    int little_endian;
    unsigned stack_align;        /* 8/16 */
    unsigned pointer_align;      /* addr_width / 8 */
    const TcABIDesc *abi;
} TcTargetDesc;

typedef struct {
    uint64_t base;
    uint64_t size;
} TcRegionDesc;

typedef struct TcPlatformProfile {
    unsigned addr_width;          /* 32 or 64 */
    int little_endian;
    TcRegionDesc region[TC_REGION_COUNT]; /* frame/static/device/code/heap */
    size_t stack_size;            /* must allow D abstract frames */
    unsigned depth_D;             /* abstract call-depth limit, >= 1 */
    TcIo *io;                     /* stdout + stdin seams */
    TcAllocator *alloc;           /* host (heap) region allocator */
    TcTrapHandler *trap;
    TcExitPolicy exit;            /* normal/trap exit statuses */
    uint64_t entry;               /* reset entry / loader-provided entry */
    const TcTargetDesc *target;   /* ABI/word size/endianness */
    const char *const *include_roots; /* import search roots; may be NULL */
    size_t ninclude_roots;
    TcDevicePolicy device;
    int checked_build;            /* UB checks only; never changes mandatory traps */
    int disable_icf;              /* must be 1: no ICF/entry merging */
} TcPlatformProfile;

/* Built-in descriptors.  The 32-bit ABI descriptor is a contract description
 * only; it is not bound to a target until S9 selects one. */
extern const TcABIDesc tc_abi_aapcs64;
extern const TcABIDesc tc_abi_aapcs32_ilp32;
extern const TcTargetDesc tc_target_aarch64_linux_user;
extern const TcTargetDesc tc_target_aarch64_baremetal;

/* Fill a 64-bit profile.  I/O, allocator, trap and include roots are left NULL
 * for the caller/platform instance to supply; validate_runtime checks them. */
int tc_profile_init_aarch64_linux_user(TcPlatformProfile *out);
int tc_profile_init_aarch64_baremetal(TcPlatformProfile *out);

typedef enum {
    TC_PROFILE_OK = 0,
    TC_PROFILE_ERR_NULL,
    TC_PROFILE_ERR_ADDR_WIDTH,
    TC_PROFILE_ERR_ENDIAN,
    TC_PROFILE_ERR_TARGET,
    TC_PROFILE_ERR_STACK_ALIGN,
    TC_PROFILE_ERR_POINTER_ALIGN,
    TC_PROFILE_ERR_STACK_SIZE,
    TC_PROFILE_ERR_DEPTH,
    TC_PROFILE_ERR_REGION,
    TC_PROFILE_ERR_REGION_ZERO,
    TC_PROFILE_ERR_REGION_OVERLAP,
    TC_PROFILE_ERR_ENTRY,
    TC_PROFILE_ERR_DEVICE,
    TC_PROFILE_ERR_CHECKED,
    TC_PROFILE_ERR_ICF,
    TC_PROFILE_ERR_ABI,
    TC_PROFILE_ERR_IO,
    TC_PROFILE_ERR_ALLOC,
    TC_PROFILE_ERR_TRAP
} TcProfileError;

/* Structural validation: all contract fields, region table, entry, ABI and
 * stack alignment.  Seams may be NULL at this stage. */
TcProfileError tc_profile_validate(const TcPlatformProfile *profile);

/* Structural validation plus non-NULL/well-formed I/O, allocator and trap
 * seams, required before executing a VM/AOT unit. */
TcProfileError tc_profile_validate_runtime(const TcPlatformProfile *profile);

const char *tc_profile_error_symbol(TcProfileError err);

#endif /* TC_RUNTIME_PROFILE_H */
