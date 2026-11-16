/* TC 0.0.46 reference VM unit vectors (standard §1.2-§1.3, §3.12, §6, §11,
 * §12.5.2; design §4, §7.2, §9).
 *
 * The semantic core, deterministic formatting, token input, memory/object
 * model, pointer states, call depth, device policies, I/O source states and the
 * mandatory traps are exercised directly and through complete programs.  Every
 * expectation is derived from the standard clauses cited in the section
 * comments, never from implementation output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frontend/sem.h"
#include "ir/type.h"
#include "runtime/format.h"
#include "runtime/input.h"
#include "runtime/semantics.h"
#include "vm/vm.h"

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

#define CHECK_EQ_INT(actual, expected)                                        \
    do {                                                                       \
        long long va_ = (long long)(actual);                                   \
        long long vb_ = (long long)(expected);                                 \
        checks++;                                                              \
        if (va_ != vb_) {                                                      \
            printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__,         \
                   __LINE__, #actual, #expected, va_, vb_);                    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                         \
    do {                                                                       \
        checks++;                                                              \
        if (strcmp((actual), (expected)) != 0) {                               \
            printf("FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__,      \
                   __LINE__, (actual), (expected));                            \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Temporary program directory and in-memory I/O seams.                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    TcIo io;
    TcIoSink sink;
    TcIoSrc src;
    TcTrapHandler trap;
    uint8_t out[8192];
    size_t out_len;
    const uint8_t *in;
    size_t in_len;
    size_t in_pos;
    size_t chunk; /* max bytes per source read; 0 means no limit */
    int fail_write;
    int fail_read;
    TcErrorKind trap_kind;
    int trap_line;
    int traps;
    TcPlatformProfile profile;
    char dir[128];
    int dir_ok;
} Harness;

static int h_sink_write(void *ctx, const uint8_t *bytes, size_t size) {
    Harness *h = (Harness *)ctx;
    if (h->fail_write) {
        return -1;
    }
    if (size > sizeof(h->out) - h->out_len) {
        return -1;
    }
    memcpy(h->out + h->out_len, bytes, size);
    h->out_len += size;
    return 0;
}

static int h_sink_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static int h_src_read(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                      TcIoStatus *status) {
    Harness *h = (Harness *)ctx;
    size_t avail;
    size_t take;
    if (h->fail_read) {
        *n = 0;
        *status = TC_IO_ERROR;
        return 0;
    }
    if (h->in_pos >= h->in_len) {
        *n = 0;
        *status = TC_IO_EOF;
        return 0;
    }
    avail = h->in_len - h->in_pos;
    take = avail < cap ? avail : cap;
    if (h->chunk != 0 && take > h->chunk) {
        take = h->chunk;
    }
    memcpy(buf, h->in + h->in_pos, take);
    h->in_pos += take;
    *n = take;
    *status = TC_IO_OK;
    return 0;
}

static void h_trap(void *ctx, TcErrorKind kind, int line) {
    Harness *h = (Harness *)ctx;
    h->trap_kind = kind;
    h->trap_line = line;
    h->traps++;
}

static int harness_init(Harness *h, unsigned W, TcDevicePolicy device,
                        int checked, unsigned depth_D) {
    int ok;
    memset(h, 0, sizeof(*h));
    h->sink.write = h_sink_write;
    h->sink.flush = h_sink_flush;
    h->sink.ctx = h;
    h->src.read = h_src_read;
    h->src.ctx = h;
    h->trap.trap = h_trap;
    h->trap.ctx = h;
    h->io.out = &h->sink;
    h->io.in = &h->src;
    if (W == 32) {
        ok = tc_profile_init_generic_w32(&h->profile);
    } else {
        ok = tc_profile_init_aarch64_linux_user(&h->profile);
    }
    if (!ok) {
        return 0;
    }
    h->profile.io = &h->io;
    h->profile.alloc = (TcAllocator *)tc_allocator_libc();
    h->profile.trap = &h->trap;
    h->profile.device = device;
    h->profile.checked_build = checked ? 1 : 0;
    if (depth_D != 0) {
        h->profile.depth_D = depth_D;
    }
    strcpy(h->dir, "/tmp/tc-vm-unit-XXXXXX");
    if (mkdtemp(h->dir) == NULL) {
        return 0;
    }
    h->dir_ok = 1;
    return 1;
}

static void harness_cleanup(Harness *h) {
    const char *names[] = {"root.tc", "lib.tc", "lib2.tc", "mod.tc"};
    size_t i;
    if (!h->dir_ok) {
        return;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", h->dir, names[i]);
        unlink(path);
    }
    rmdir(h->dir);
    h->dir_ok = 0;
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t len = strlen(text);
    if (file == NULL) {
        return 0;
    }
    if (fwrite(text, 1, len, file) != len) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

/* Analyze and run `source` as root.tc.  Extra modules are written by the
 * caller before invoking this helper. */
static int run_source(Harness *h, const char *source, TcVmResult *result,
                      TcVmStatus *status) {
    char path[256];
    TcAnalysis *an;
    TcAnalysisStatus astatus;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    const TcIR *ir;

    h->out_len = 0;
    h->traps = 0;
    h->trap_kind = (TcErrorKind)0;
    h->trap_line = 0;
    h->in_pos = 0;
    snprintf(path, sizeof(path), "%s/root.tc", h->dir);
    if (!write_text(path, source)) {
        return 0;
    }
    an = tc_analyze_with_profile(path, &h->profile, h->profile.addr_width, NULL,
                                 0, &astatus, &diag, &impl);
    if (an == NULL || astatus != TC_ANALYSIS_OK) {
        printf("analysis failed: status=%d code=%d at %u:%u impl=%d\n---\n%s---\n",
               (int)astatus, an != NULL ? (int)diag.code : -1,
               an != NULL ? diag.span.line : 0,
               an != NULL ? diag.span.column : 0, (int)impl, source);
        if (an != NULL) {
            tc_analysis_destroy(an);
        }
        return 0;
    }
    ir = tc_analysis_ir(an);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return 0;
    }
    *status = tc_vm_run(ir, &h->profile, result, &impl);
    if (h->out_len < sizeof(h->out)) {
        h->out[h->out_len] = '\0';
    }
    tc_analysis_destroy(an);
    return 1;
}

static void expect_clean_run(Harness *h, const char *source,
                             const char *expected_out) {
    TcVmResult result;
    TcVmStatus status = TC_VM_STATUS_IMPL;
    if (!run_source(h, source, &result, &status)) {
        failures++;
        return;
    }
    CHECK_EQ_STR((const char *)h->out, expected_out);
    CHECK(status == TC_VM_STATUS_OK);
    CHECK_EQ_INT(result.trapped, 0);
    CHECK_EQ_INT(h->traps, 0);
}

static void expect_trap(Harness *h, const char *source, TcErrorKind kind,
                        int line) {
    TcVmResult result;
    TcVmStatus status = TC_VM_STATUS_OK;
    memset(&result, 0, sizeof(result));
    if (!run_source(h, source, &result, &status)) {
        failures++;
        return;
    }
    CHECK(status == TC_VM_STATUS_TRAP);
    CHECK_EQ_INT(result.trapped, 1);
    CHECK_EQ_INT(result.kind, kind);
    CHECK_EQ_INT(result.span.line, line);
    CHECK_EQ_INT(h->traps, 1);
    CHECK_EQ_INT(h->trap_kind, kind);
}

/* ------------------------------------------------------------------------- */
/* Semantic core.                                                            */
/* ------------------------------------------------------------------------- */

static void test_integer_semantics(TcTypeTable *types) {
    const TcType *i8 = tc_type_scalar(types, TC_TY_I8);
    const TcType *u8 = tc_type_scalar(types, TC_TY_U8);
    const TcType *i32 = tc_type_scalar(types, TC_TY_I32);
    const TcType *u32 = tc_type_scalar(types, TC_TY_U32);
    const TcType *i64 = tc_type_scalar(types, TC_TY_I64);
    const TcType *u64 = tc_type_scalar(types, TC_TY_U64);
    const TcType *isize = tc_type_scalar(types, TC_TY_ISIZE);
    uint64_t out = 0;
    TcErrorKind trap = (TcErrorKind)0;

    /* Strict signed overflow (standard §6.3). */
    CHECK(!tc_sem_int_binop(64, i8, TC_ADD, TC_STRICT, 127, 1, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(!tc_sem_int_binop(64, i8, TC_SUB, TC_STRICT, 0x80, 1, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(!tc_sem_int_binop(64, i8, TC_MUL, TC_STRICT, 0x80, 0xff, &out,
                            &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(!tc_sem_int_unop(64, i8, TC_NEG, TC_STRICT, 0x80, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(!tc_sem_int_unop(64, i8, TC_ABS, TC_STRICT, 0x80, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);

    /* .wrap modulo 2^n and unsigned fixed modulo. */
    CHECK(tc_sem_int_binop(64, i8, TC_ADD, TC_WRAP, 127, 1, &out, &trap));
    CHECK_EQ_INT(out, 0x80);
    CHECK(tc_sem_int_binop(64, u8, TC_ADD, TC_STRICT, 255, 1, &out, &trap));
    CHECK_EQ_INT(out, 0x00);
    CHECK(tc_sem_int_binop(64, u8, TC_SUB, TC_STRICT, 0, 1, &out, &trap));
    CHECK_EQ_INT(out, 0xff);
    CHECK(tc_sem_int_unop(64, u8, TC_NEG, TC_STRICT, 1, &out, &trap));
    CHECK_EQ_INT(out, 0xff);

    /* Division/modulo edges: truncation toward zero, remainder sign. */
    CHECK(tc_sem_int_binop(64, i32, TC_DIV, TC_STRICT, 0xfffffffd, 2, &out,
                           &trap));
    CHECK_EQ_INT(out, 0xffffffff);
    CHECK(tc_sem_int_binop(64, i32, TC_MOD, TC_STRICT, 0xfffffffd, 2, &out,
                           &trap));
    CHECK_EQ_INT(out, 0xffffffff);
    CHECK(tc_sem_int_binop(64, i32, TC_MOD, TC_STRICT, 7, 0xfffffffe, &out,
                           &trap));
    CHECK_EQ_INT(out, 1);
    CHECK(!tc_sem_int_binop(64, i32, TC_DIV, TC_STRICT, 0x80000000, 0xffffffff,
                            &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(tc_sem_int_binop(64, i32, TC_MOD, TC_STRICT, 0x80000000, 0xffffffff,
                           &out, &trap));
    CHECK_EQ_INT(out, 0);
    CHECK(!tc_sem_int_binop(64, i32, TC_DIV, TC_STRICT, 1, 0, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_DIV_ZERO);
    CHECK(!tc_sem_int_binop(64, u32, TC_MOD, TC_STRICT, 1, 0, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_DIV_ZERO);

    /* Shift rules, including negative count precedence and the k>=n rule. */
    CHECK(tc_sem_int_binop(64, i8, TC_SHL, TC_STRICT, 0xff, 7, &out, &trap));
    CHECK_EQ_INT(out, 0x80);
    CHECK(!tc_sem_int_binop(64, i8, TC_SHL, TC_STRICT, 1, 7, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(tc_sem_int_binop(64, i8, TC_SHL, TC_STRICT, 0, 8, &out, &trap));
    CHECK_EQ_INT(out, 0);
    CHECK(!tc_sem_int_binop(64, i8, TC_SHL, TC_STRICT, 1, 8, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(tc_sem_int_binop(64, i8, TC_SHL, TC_WRAP, 1, 8, &out, &trap));
    CHECK_EQ_INT(out, 0);
    CHECK(!tc_sem_int_binop(64, i32, TC_SHL, TC_STRICT, 1, 0xffffffff, &out,
                            &trap));
    CHECK_EQ_INT(trap, TC_EK_NEGATIVE_SHIFT);
    CHECK(tc_sem_int_binop(64, i8, TC_SHR, TC_STRICT, 0x80, 7, &out, &trap));
    CHECK_EQ_INT(out, 0xff);
    CHECK(tc_sem_int_binop(64, i8, TC_SHR, TC_STRICT, 0x80, 8, &out, &trap));
    CHECK_EQ_INT(out, 0);
    CHECK(tc_sem_int_binop(64, u8, TC_SHR, TC_STRICT, 0xff, 4, &out, &trap));
    CHECK_EQ_INT(out, 0x0f);

    /* W-dependent isize/usize and 64-bit semantics on the 32-bit profile. */
    CHECK(!tc_sem_int_binop(32, isize, TC_ADD, TC_STRICT, 0x7fffffff, 1, &out,
                            &trap));
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);
    CHECK(tc_sem_int_binop(64, isize, TC_ADD, TC_STRICT, 0x7fffffff, 1, &out,
                           &trap));
    CHECK_EQ_INT(out, 0x80000000);
    CHECK(tc_sem_int_binop(32, u64, TC_MUL, TC_STRICT,
                           0xffffffffffffffffu, 0xffffffffffffffffu, &out,
                           &trap));
    CHECK_EQ_INT(out, 1);
    CHECK(tc_sem_int_binop(64, i64, TC_DIV, TC_STRICT,
                           0x8000000000000000u, 0xffffffffffffffffu, &out,
                           &trap) == 0);
    CHECK_EQ_INT(trap, TC_EK_INTEGER_OVERFLOW);

    /* Comparisons. */
    CHECK(tc_sem_compare(64, i32, TC_LT, 0xffffffff, 0, &out));
    CHECK_EQ_INT(out, 1);
    CHECK(tc_sem_compare(64, u32, TC_LT, 0xffffffff, 0, &out));
    CHECK_EQ_INT(out, 0);
    CHECK(tc_sem_compare(64, tc_type_scalar(types, TC_TY_BOOL), TC_NE, 0, 1,
                         &out));
    CHECK_EQ_INT(out, 1);
}

static void test_float_semantics(TcTypeTable *types) {
    const TcType *f32 = tc_type_scalar(types, TC_TY_F32);
    const TcType *f64 = tc_type_scalar(types, TC_TY_F64);
    uint64_t out = 0;
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t one = UINT64_C(0x3ff0000000000000);
    uint64_t zero = 0;
    uint64_t inf = UINT64_C(0x7ff0000000000000);
    uint64_t nan = UINT64_C(0x7ff8000000000000);
    uint64_t big = UINT64_C(0x7fe0000000000000); /* 1e308 */

    /* Strict exception priority: INVALID before DIV_ZERO. */
    CHECK(!tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, nan, zero, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);
    CHECK(!tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, zero, zero, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);
    CHECK(!tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, inf, inf, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);
    CHECK(!tc_sem_float_binop(64, f64, TC_ADD, TC_STRICT, inf, inf ^
                              UINT64_C(0x8000000000000000), &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);
    CHECK(!tc_sem_float_binop(64, f64, TC_MUL, TC_STRICT, inf, zero, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);
    CHECK(!tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, one, zero, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_DIV_ZERO);
    CHECK(!tc_sem_float_binop(64, f64, TC_ADD, TC_STRICT, big, big, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_OVERFLOW);

    /* .ieee returns inf/NaN and never traps. */
    CHECK(tc_sem_float_binop(64, f64, TC_ADD, TC_IEEE, big, big, &out, &trap));
    CHECK_EQ_INT(out, inf);
    CHECK(tc_sem_float_binop(64, f64, TC_DIV, TC_IEEE, one, zero, &out, &trap));
    CHECK_EQ_INT(out, inf);
    CHECK(tc_sem_float_binop(64, f64, TC_DIV, TC_IEEE, zero, zero, &out,
                             &trap));
    CHECK_EQ_INT(out, tc_sem_nan_bits(64));

    /* Exact floating mod (standard §6.3.3): exact rational quotient. */
    CHECK(tc_sem_float_binop(64, f64, TC_MOD, TC_STRICT,
                             UINT64_C(0x4016000000000000), /* 5.5 */
                             UINT64_C(0x4000000000000000), /* 2.0 */
                             &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x3ff8000000000000)); /* 1.5 */
    CHECK(tc_sem_float_binop(64, f64, TC_MOD, TC_STRICT,
                             UINT64_C(0xc016000000000000), /* -5.5 */
                             UINT64_C(0x4000000000000000), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0xbff8000000000000)); /* -1.5 */
    CHECK(tc_sem_float_binop(64, f64, TC_MOD, TC_STRICT, one, inf, &out,
                             &trap));
    CHECK_EQ_INT(out, one);
    CHECK(!tc_sem_float_binop(64, f64, TC_MOD, TC_STRICT, inf, one, &out,
                              &trap));
    CHECK_EQ_INT(trap, TC_EK_FLOAT_INVALID);

    /* Exact subnormal results do not underflow; inexact tiny results do. */
    {
        uint64_t min_sub = 1;
        CHECK(tc_sem_float_binop(64, f64, TC_MUL, TC_STRICT, min_sub, one, &out,
                                 &trap));
        CHECK_EQ_INT(out, min_sub);
        CHECK(tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, min_sub, one, &out,
                                 &trap));
        CHECK_EQ_INT(out, min_sub);
        CHECK(!tc_sem_float_binop(64, f64, TC_DIV, TC_STRICT, min_sub,
                                  UINT64_C(0x4000000000000000), &out, &trap));
        CHECK_EQ_INT(trap, TC_EK_FLOAT_UNDERFLOW);
    }

    /* abs/neg are sign-bit operations and never trap. */
    CHECK(tc_sem_float_unop(64, f64, TC_ABS, UINT64_C(0xfff8000000000000),
                            &out));
    CHECK_EQ_INT(out, UINT64_C(0x7ff8000000000000));
    CHECK(tc_sem_float_unop(64, f64, TC_NEG, UINT64_C(0x8000000000000000),
                            &out));
    CHECK_EQ_INT(out, 0);
    CHECK_EQ_INT(tc_sem_nan_bits(32), UINT64_C(0x7fc00000));

    /* NaN comparisons never trap. */
    CHECK(tc_sem_compare(64, f64, TC_EQ, nan, nan, &out));
    CHECK_EQ_INT(out, 0);
    CHECK(tc_sem_compare(64, f64, TC_NE, nan, nan, &out));
    CHECK_EQ_INT(out, 1);
    CHECK(tc_sem_compare(64, f32, TC_LT, tc_sem_nan_bits(32), 0, &out));
    CHECK_EQ_INT(out, 0);
}

static void test_conversions(TcTypeTable *types) {
    const TcType *i8 = tc_type_scalar(types, TC_TY_I8);
    const TcType *u8 = tc_type_scalar(types, TC_TY_U8);
    const TcType *i32 = tc_type_scalar(types, TC_TY_I32);
    const TcType *u32 = tc_type_scalar(types, TC_TY_U32);
    const TcType *u64 = tc_type_scalar(types, TC_TY_U64);
    const TcType *f32 = tc_type_scalar(types, TC_TY_F32);
    const TcType *f64 = tc_type_scalar(types, TC_TY_F64);
    const TcType *isize = tc_type_scalar(types, TC_TY_ISIZE);
    uint64_t out = 0;
    TcErrorKind trap = (TcErrorKind)0;

    CHECK(tc_sem_cast(64, i32, i8, 127, &out, &trap));
    CHECK_EQ_INT(out, 127);
    CHECK(!tc_sem_cast(64, i32, i8, 128, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(!tc_sem_cast(64, i32, u8, 0xffffffff, &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(tc_sem_cast(64, u8, i32, 255, &out, &trap));
    CHECK_EQ_INT(out, 255);

    /* float -> int truncates toward zero; NaN/inf/out of range trap. */
    CHECK(tc_sem_cast(64, f64, i32, UINT64_C(0x3ff8000000000000), &out, &trap));
    CHECK_EQ_INT(out, 1);
    CHECK(tc_sem_cast(64, f64, i32, UINT64_C(0xbff8000000000000), &out, &trap));
    CHECK_EQ_INT(out, 0xffffffff);
    CHECK(tc_sem_cast(64, f64, u32, UINT64_C(0xbfe0000000000000), &out, &trap));
    CHECK_EQ_INT(out, 0);
    CHECK(!tc_sem_cast(64, f64, u32, UINT64_C(0xbff0000000000000), &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(!tc_sem_cast(64, f64, i32, tc_sem_nan_bits(64), &out, &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(!tc_sem_cast(64, f64, i32, UINT64_C(0x7ff0000000000000), &out,
                       &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(tc_sem_cast(64, f64, u64, UINT64_C(0x43e0000000000000), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x8000000000000000));

    /* float32 -> float64 is exact; float64 -> float32 overflow traps. */
    CHECK(tc_sem_cast(64, f32, f64, UINT64_C(0x3f800000), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x3ff0000000000000));
    CHECK(!tc_sem_cast(64, f64, f32, UINT64_C(0x47effffff0000000), &out,
                       &trap));
    CHECK_EQ_INT(trap, TC_EK_CAST_OVERFLOW);
    CHECK(tc_sem_cast(64, f64, f32, UINT64_C(0x3ff0000000000001), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x3f800000));
    /* Same-type cast preserves NaN payload. */
    CHECK(tc_sem_cast(64, f64, f64, UINT64_C(0x7ff8000000000001), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x7ff8000000000001));
    /* Cross-type NaN becomes canonical quiet NaN. */
    CHECK(tc_sem_cast(64, f64, f32, UINT64_C(0x7ff8000000000001), &out, &trap));
    CHECK_EQ_INT(out, UINT64_C(0x7fc00000));

    /* bool conversions: nonzero (including NaN) is true. */
    CHECK(tc_sem_cast(64, f64, tc_type_scalar(types, TC_TY_BOOL),
                      tc_sem_nan_bits(64), &out, &trap));
    CHECK_EQ_INT(out, 1);
    CHECK(tc_sem_cast(64, tc_type_scalar(types, TC_TY_BOOL), i32, 1, &out,
                      &trap));
    CHECK_EQ_INT(out, 1);

    /* trunc keeps low bits and never traps; bitcast keeps the bit string. */
    CHECK(tc_sem_trunc(64, i32, u8, 0x1234, &out));
    CHECK_EQ_INT(out, 0x34);
    CHECK(tc_sem_bitcast(64, u32, 0xffffffff, &out));
    CHECK_EQ_INT(out, 0xffffffffu);
    CHECK(tc_sem_bitcast(32, isize, 0xffffffff, &out));
    CHECK_EQ_INT(out, 0xffffffffu);
}

/* ------------------------------------------------------------------------- */
/* Deterministic formatting and token input.                                 */
/* ------------------------------------------------------------------------- */

static void check_format(unsigned W, const TcType *type, uint64_t bits,
                         const char *expected) {
    char out[128];
    CHECK(tc_format_scalar(W, type, bits, out, sizeof(out)));
    CHECK_EQ_STR(out, expected);
}

static void test_formatting(TcTypeTable *types) {
    const TcType *i8 = tc_type_scalar(types, TC_TY_I8);
    const TcType *u64 = tc_type_scalar(types, TC_TY_U64);
    const TcType *i64 = tc_type_scalar(types, TC_TY_I64);
    const TcType *f32 = tc_type_scalar(types, TC_TY_F32);
    const TcType *f64 = tc_type_scalar(types, TC_TY_F64);
    const TcType *b = tc_type_scalar(types, TC_TY_BOOL);

    check_format(64, i8, 0xff, "-1");
    check_format(64, u64, UINT64_MAX, "18446744073709551615");
    check_format(64, i64, UINT64_C(0x8000000000000000),
                 "-9223372036854775808");
    check_format(64, b, 1, "true");
    check_format(64, b, 0, "false");
    check_format(64, f64, UINT64_C(0x3ff0000000000000), "1");
    check_format(64, f64, UINT64_C(0x3fb999999999999a), "0.1");
    check_format(64, f64, UINT64_C(0x3fd5555555555555), "0.333333");
    check_format(64, f64, UINT64_C(0x8000000000000000), "-0");
    check_format(64, f64, UINT64_C(0x7ff0000000000000), "inf");
    check_format(64, f64, UINT64_C(0xfff0000000000000), "-inf");
    check_format(64, f64, tc_sem_nan_bits(64), "nan");
    check_format(64, f64, UINT64_C(0x412e848000000000), "1e+06");
    check_format(64, f64, UINT64_C(0x3ee4f8b588e368f1), "1e-05");
    check_format(64, f64, UINT64_C(0x3f1a36e2eb1c432d), "0.0001");
    check_format(64, f64, UINT64_C(0x0010000000000000), "2.22507e-308");
    check_format(64, f64, 1, "4.94066e-324");
    check_format(32, f32, UINT64_C(0x3eaaaaab), "0.333333");
    check_format(32, f32, UINT64_C(0x80000000), "-0");
    check_format(32, f32, UINT64_C(0x7f7fffff), "3.40282e+38");
}

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t pos;
    size_t chunk;
    int error;
} FakeSrc;

static int fake_read(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                     TcIoStatus *status) {
    FakeSrc *src = (FakeSrc *)ctx;
    size_t take;
    if (src->error) {
        *n = 0;
        *status = TC_IO_ERROR;
        return 0;
    }
    if (src->pos >= src->size) {
        *n = 0;
        *status = TC_IO_EOF;
        return 0;
    }
    take = src->size - src->pos;
    if (take > cap) {
        take = cap;
    }
    if (src->chunk != 0 && take > src->chunk) {
        take = src->chunk;
    }
    memcpy(buf, src->bytes + src->pos, take);
    src->pos += take;
    *n = take;
    *status = TC_IO_OK;
    return 0;
}

static TcInputStatus read_token(TcTypeTable *types, unsigned W,
                                const char *token, int error_before,
                                size_t chunk, const TcType *type,
                                uint64_t *bits) {
    FakeSrc src;
    TcIoSrc seam;
    TcInput input;
    TcInputStatus status;
    src.bytes = (const uint8_t *)token;
    src.size = strlen(token);
    src.pos = 0;
    src.chunk = chunk;
    src.error = error_before;
    seam.read = fake_read;
    seam.ctx = &src;
    tc_input_init(&input, &seam, tc_allocator_libc());
    status = tc_input_read_scalar(&input, W, type, bits);
    tc_input_destroy(&input);
    (void)types;
    return status;
}

static void test_input(TcTypeTable *types) {
    const TcType *i32 = tc_type_scalar(types, TC_TY_I32);
    const TcType *u32 = tc_type_scalar(types, TC_TY_U32);
    const TcType *b = tc_type_scalar(types, TC_TY_BOOL);
    const TcType *f64 = tc_type_scalar(types, TC_TY_F64);
    const TcType *isize = tc_type_scalar(types, TC_TY_ISIZE);
    uint64_t bits = 0;

    /* Complete tokens, leading whitespace and leading zeros are accepted. */
    CHECK(read_token(types, 64, "  -42 ", 0, 0, i32, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT((int32_t)bits, -42);
    CHECK(read_token(types, 64, "0007", 0, 1, i32, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT(bits, 7);
    CHECK(read_token(types, 64, "true", 0, 1, b, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT(bits, 1);
    CHECK(read_token(types, 64, "0.1", 0, 1, f64, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT(bits, UINT64_C(0x3fb999999999999a));
    CHECK(read_token(types, 64, "-0", 0, 1, f64, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT(bits, UINT64_C(0x8000000000000000));
    CHECK(read_token(types, 64, "nan", 0, 1, f64, &bits) == TC_INPUT_OK);
    CHECK_EQ_INT(bits, tc_sem_nan_bits(64));

    /* Invalid tokens: incomplete match, '+'/f suffix, short token, range. */
    CHECK(read_token(types, 64, "12abc", 0, 0, i32, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "+1", 0, 0, i32, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1.0f", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1.", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, ".5", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1e+5", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "-nan", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1e400", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1e-400", 0, 0, f64, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "2147483648", 0, 0, i32, &bits) ==
          TC_INPUT_IO);
    CHECK(read_token(types, 64, "-1", 0, 0, u32, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 32, "4294967296", 0, 0, isize, &bits) ==
          TC_INPUT_IO);

    /* EOF / error / no token. */
    CHECK(read_token(types, 64, "   ", 0, 0, i32, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "", 0, 0, i32, &bits) == TC_INPUT_IO);
    CHECK(read_token(types, 64, "1", 1, 0, i32, &bits) == TC_INPUT_IO);
}

/* ------------------------------------------------------------------------- */
/* Complete-program VM behavior.                                             */
/* ------------------------------------------------------------------------- */

static void test_program_arithmetic_and_io(Harness *h) {
    expect_clean_run(
        h,
        "#program\n"
        "var i: int32 = copy int32, 0\n"
        "var s: int32 = copy int32, 0\n"
        "label loop:\n"
        "var c: bool = lt int32, i, 5\n"
        "_ = br_if c, body\n"
        "_ = br done\n"
        "label body:\n"
        "s = add int32, s, i\n"
        "i = add int32, i, 1\n"
        "_ = br loop\n"
        "label done:\n"
        "_ = writeln int32, s\n",
        "10\n");
}

static void test_program_calls_structs(Harness *h) {
    expect_clean_run(
        h,
        "#program\n"
        "struct S then\n"
        "    var a: int32\n"
        "    var b: int32\n"
        "end\n"
        "var s: S = make S, 3, 4\n"
        "var p: ptr<int32> = addr int32, s.b\n"
        "unsafe then\n"
        "_ = store int32, p, 9\n"
        "end\n"
        "_ = writeln int32, s.a\n"
        "_ = writeln int32, s.b\n",
        "3\n9\n");
}

static void test_program_traps(Harness *h) {
    expect_trap(h,
                "#program\n"
                "var z: int32 = copy int32, 0\n"
                "var x: int32 = div int32, 1, z\n",
                TC_EK_DIV_ZERO, 3);
    expect_trap(h,
                "#program\n"
                "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
                "unsafe then\n"
                "var x: int32 = load int32, p\n"
                "end\n",
                TC_EK_NULL_POINTER, 4);
    expect_trap(h,
                "#program\n"
                "_ = writeln int32, 7\n"
                "_ = trap\n",
                TC_EK_TRAP, 3);
    /* Committed output before a trap is not rolled back (§12.5.2). */
    CHECK_EQ_STR((const char *)h->out, "7\n");
}

static void test_program_trap_positions(Harness *h) {
    const char *lib =
        "#lib\n"
        "public func f(x: int32) int32 then\n"
        "var p: ptr<int32> = addr int32, x\n"
        "unsafe then\n"
        "_ = store int32, p, 5\n"
        "end\n"
        "_ = return x\n"
        "end\n";
    char path[256];

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    expect_trap(h,
                "#program\n"
                "var k: int32 = copy int32, -1\n"
                "var x: int32 = shl int32, 1, k\n",
                TC_EK_NEGATIVE_SHIFT, 3);
    expect_trap(h,
                "#program\n"
                "var n: float64 = copy float64, nan\n"
                "var x: float64 = div float64, n, 1.0\n",
                TC_EK_FLOAT_INVALID, 3);
    expect_trap(h,
                "#program\n"
                "var a: float64 = copy float64, 1.0e308\n"
                "var x: float64 = add float64, a, a\n",
                TC_EK_FLOAT_OVERFLOW, 3);
    expect_trap(h,
                "#program\n"
                "var a: float64 = copy float64, 1.0e-300\n"
                "var x: float64 = mul float64, a, a\n",
                TC_EK_FLOAT_UNDERFLOW, 3);
    expect_trap(h,
                "#program\n"
                "var a: float64 = copy float64, 1.0e300\n"
                "var x: int64 = cast int64, a\n",
                TC_EK_CAST_OVERFLOW, 3);
    expect_trap(h,
                "#program\n"
                "var u: usize = copy usize, 18446744073709551615u\n"
                "var z: ptr<int32> = copy ptr<int32>, nullptr\n"
                "unsafe then\n"
                "var p: ptr<int32> = bitcast ptr<int32>, u\n"
                "var d: isize = ptrdiff int32, p, z\n"
                "end\n",
                TC_EK_PTRDIFF_OVERFLOW, 6);
    expect_trap(h,
                "#program\n"
                "var a: usize = copy usize, 0x40000000\n"
                "unsafe then\n"
                "var p: ptr<int32> = bitcast ptr<int32>, a\n"
                "var x: int32 = load int32, p\n"
                "end\n",
                TC_EK_ADDR_UNAVAILABLE, 5);
    expect_trap(h,
                "#program\n"
                "var fp: fnptr<int32, int32> = copy fnptr<int32, int32>, nullptr\n"
                "unsafe then\n"
                "var x: int32 = call_indirect fp, 1\n"
                "end\n",
                TC_EK_NULL_FUNCTION_POINTER, 4);

    snprintf(path, sizeof(path), "%s/lib.tc", h->dir);
    CHECK(write_text(path, lib));
    expect_trap(h,
                "#program\n"
                "import lib\n"
                "var x: int32 = call lib.f, 3\n",
                TC_EK_WRITE_PROTECTED, 5);
    harness_cleanup(h);
}

static void test_program_depth(void) {
    Harness local;
    const char *lib =
        "#lib\n"
        "public func down(n: int32) int32 then\n"
        "var c: bool = eq int32, n, 0\n"
        "_ = br_if c, fin\n"
        "var m: int32 = sub int32, n, 1\n"
        "var r: int32 = call Self.down, m\n"
        "_ = return r\n"
        "label fin:\n"
        "_ = return n\n"
        "end\n";
    char root[256];
    char path[256];

    CHECK(harness_init(&local, 64, TC_DEV_REJECT, 0, 3));
    snprintf(path, sizeof(path), "%s/lib.tc", local.dir);
    CHECK(write_text(path, lib));
    snprintf(root, sizeof(root),
             "#program\n"
             "import lib\n"
             "var r: int32 = call lib.down, 2\n"
             "_ = writeln int32, r\n");
    expect_clean_run(&local, root, "0\n");
    snprintf(root, sizeof(root),
             "#program\n"
             "import lib\n"
             "var r: int32 = call lib.down, 3\n"
             "_ = writeln int32, r\n");
    expect_trap(&local, root, TC_EK_STACK_OVERFLOW, 6);
    harness_cleanup(&local);
}

static void test_program_device(Harness *h) {
    char source[512];
    unsigned base = 0x20000000u;

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "var x: int32 = load int32, p\n"
             "end\n",
             base);
    expect_trap(h, source, TC_EK_ADDR_UNAVAILABLE, 5);

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_READ, 0, 0));
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "var x: int32 = load int32, p\n"
             "_ = writeln int32, x\n"
             "end\n",
             base);
    expect_clean_run(h, source, "0\n");
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "_ = store int32, p, 1\n"
             "end\n",
             base);
    expect_trap(h, source, TC_EK_ADDR_UNAVAILABLE, 5);

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_READWRITE, 0, 0));
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "_ = store int32, p, 7\n"
             "var x: int32 = load int32, p\n"
             "_ = writeln int32, x\n"
             "end\n",
             base);
    expect_clean_run(h, source, "7\n");

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_VOLATILE, 0, 0));
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "var x: int32 = load int32, p\n"
             "end\n",
             base);
    expect_trap(h, source, TC_EK_ADDR_UNAVAILABLE, 5);
    snprintf(source, sizeof(source),
             "#program\n"
             "var a: usize = copy usize, 0x%xu\n"
             "unsafe then\n"
             "var p: ptr<int32> = bitcast ptr<int32>, a\n"
             "var x: int32 = load.volatile int32, p\n"
             "_ = writeln int32, x\n"
             "end\n",
             base);
    expect_clean_run(h, source, "0\n");
}

static void test_program_io_states(Harness *h) {
    const char *source =
        "#program\n"
        "var x: int32 = read int32\n"
        "var y: float64 = read float64\n"
        "_ = writeln int32, x\n"
        "_ = writeln float64, y\n";
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    h->in = (const uint8_t *)"  -42 0.5";
    h->in_len = strlen((const char *)h->in);
    h->chunk = 1; /* short reads must not split tokens */
    expect_clean_run(h, source, "-42\n0.5\n");

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    h->in = (const uint8_t *)"12abc";
    h->in_len = strlen((const char *)h->in);
    expect_trap(h, source, TC_EK_IO, 2);

    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    h->in = (const uint8_t *)"42";
    h->in_len = strlen((const char *)h->in);
    expect_trap(h, source, TC_EK_IO, 3);

    /* Source error before a token is TC_RE_IO. */
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    h->fail_read = 1;
    expect_trap(h, source, TC_EK_IO, 2);

    /* Sink failure maps to TC_RE_IO and contributes no bytes. */
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    h->fail_write = 1;
    expect_trap(h,
                "#program\n"
                "_ = writeln int32, 1\n",
                TC_EK_IO, 2);
    CHECK_EQ_INT(h->out_len, 0);
}

static void test_program_w32_w64(Harness *h) {
    const char *source =
        "#program\n"
        "var a: isize = copy isize, 2147483647\n"
        "_ = writeln isize, a\n"
        "var b: usize = sizeof ptr<int32>\n"
        "_ = writeln usize, b\n"
        "var c: usize = sizeof isize\n"
        "_ = writeln usize, c\n";
    harness_cleanup(h);
    CHECK(harness_init(h, 32, TC_DEV_REJECT, 0, 0));
    expect_clean_run(h, source, "2147483647\n4\n4\n");
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    expect_clean_run(h, source, "2147483647\n8\n8\n");

    /* isize overflow only on the 32-bit profile. */
    harness_cleanup(h);
    CHECK(harness_init(h, 32, TC_DEV_REJECT, 0, 0));
    expect_trap(h,
                "#program\n"
                "var a: isize = copy isize, 2147483647\n"
                "var b: isize = add isize, a, 1\n",
                TC_EK_INTEGER_OVERFLOW, 3);
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 0, 0));
    expect_clean_run(h,
                     "#program\n"
                     "var a: isize = copy isize, 2147483647\n"
                     "var b: isize = add isize, a, 1\n"
                     "_ = writeln isize, b\n",
                     "2147483648\n");

    /* checked build only adds UB access checks; mandatory traps are unchanged. */
    harness_cleanup(h);
    CHECK(harness_init(h, 64, TC_DEV_REJECT, 1, 0));
    expect_trap(h,
                "#program\n"
                "var a: int32 = copy int32, 1\n"
                "var p: ptr<int32> = addr int32, a\n"
                "unsafe then\n"
                "var q: ptr<int32> = ptradd int32, p, 2\n"
                "var x: int32 = load int32, q\n"
                "end\n",
                TC_EK_ADDR_UNAVAILABLE, 5);
}

int main(void) {
    TcTypeTable *types = tc_type_table_create(tc_allocator_libc());
    Harness h;
    if (types == NULL) {
        printf("vm.unit: cannot create type table\n");
        return 1;
    }
    test_integer_semantics(types);
    test_float_semantics(types);
    test_conversions(types);
    test_formatting(types);
    test_input(types);

    CHECK(harness_init(&h, 64, TC_DEV_REJECT, 0, 0));
    test_program_arithmetic_and_io(&h);
    harness_cleanup(&h);
    CHECK(harness_init(&h, 64, TC_DEV_REJECT, 0, 0));
    test_program_calls_structs(&h);
    harness_cleanup(&h);
    CHECK(harness_init(&h, 64, TC_DEV_REJECT, 0, 0));
    test_program_traps(&h);
    harness_cleanup(&h);
    test_program_depth();
    test_program_trap_positions(&h);
    test_program_device(&h);
    test_program_io_states(&h);
    test_program_w32_w64(&h);
    harness_cleanup(&h);

    tc_type_table_destroy(types);
    if (failures != 0) {
        printf("vm.unit: %d failure(s), %d checks\n", failures, checks);
        return 1;
    }
    printf("vm.unit: %d checks passed\n", checks);
    return 0;
}
