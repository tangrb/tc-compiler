/* S0 L0 contract tests: closed code table, type identity, IR shape, profiles,
 * diagnostics ordering/buffer, zero-pass baseline and OOM channel.
 *
 * These tests assert the frozen 0.0.46 contracts, not compiler behavior; no
 * .tc source is compiled in S0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/contract.h"
#include "diagnostic/diagnostic.h"
#include "contract/optimizer.h"
#include "ir/block.h"
#include "ir/instr.h"
#include "ir/ir.h"
#include "ir/type.h"
#include "runtime/alloc.h"
#include "runtime/module_loader.h"
#include "runtime/profile.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(expected, actual)                                       \
    do {                                                                     \
        long long e_ = (long long)(expected);                                \
        long long a_ = (long long)(actual);                                  \
        if (e_ != a_) {                                                      \
            fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld (%s)\n",    \
                    __FILE__, __LINE__, e_, a_, #actual);                    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_contract_and_code_table(void) {
    size_t i;
    size_t ce = 0;
    size_t re = 0;
    size_t w_sensitive = 0;
    size_t import_codes = 0;
    CHECK(tc_contract_check() == NULL);
    CHECK(tc_diag_contract_violation() == NULL);
    CHECK_EQ_INT(80, TC_DIAG_CODE_COUNT);
    CHECK_EQ_INT(65, TC_DIAG_CE_COUNT);
    CHECK_EQ_INT(15, TC_DIAG_RE_COUNT);
    CHECK_EQ_INT(TC_CE_SYNTAX, 0);
    CHECK_EQ_INT(TC_RE_INTEGER_OVERFLOW, 65);
    CHECK_EQ_INT(TC_RE_TRAP, 79);
    for (i = 0; i < TC_DIAG_CODE_COUNT; i++) {
        const TcDiagInfo *info = &tc_diag_table[i];
        CHECK(info->symbol != NULL);
        CHECK(info->print_name != NULL && info->print_name[0] != '\0');
        CHECK(info->trigger != NULL && info->trigger[0] != '\0');
        CHECK(info->position_rule != NULL && info->position_rule[0] != '\0');
        CHECK(info->stage != 0);
        CHECK(info->report_stage != 0);
        if (strncmp(info->symbol, "TC_CE_", 6) == 0) {
            ce++;
        } else {
            CHECK(strncmp(info->symbol, "TC_RE_", 6) == 0);
            re++;
        }
        if (info->w_sensitive) {
            w_sensitive++;
        }
        if (info->report_stage == TC_STAGE_MASK_IMPORT) {
            import_codes++;
            CHECK(info->stage == TC_STAGE_MASK_SEM);
        }
    }
    CHECK_EQ_INT(65, ce);
    CHECK_EQ_INT(15, re);
    CHECK_EQ_INT(12, w_sensitive);
    CHECK_EQ_INT(5, import_codes);
    CHECK(tc_diag_table[TC_CE_SYNTAX].stage == TC_STAGE_MASK_LEX_SYN);
    CHECK(tc_diag_table[TC_CE_LITERAL_LEX_RANGE].stage == TC_STAGE_MASK_LEX);
    CHECK(tc_diag_table[TC_RE_TRAP].stage == TC_STAGE_MASK_RT);
    CHECK(tc_diag_table[TC_CE_IMPORT_NOT_FOUND].report_stage == TC_STAGE_MASK_IMPORT);
    CHECK(tc_diag_table[TC_CE_IMPORT_NOT_FOUND].stage == TC_STAGE_MASK_SEM);
    CHECK(strcmp(tc_diag_table[TC_CE_SYNTAX].print_name, "SyntaxError") == 0);
    CHECK(strcmp(TC_ERR_OUT_OF_MEMORY_SYMBOL, "TC_ERR_OUT_OF_MEMORY") == 0);
    CHECK(strcmp(tc_impl_error_print_name(TC_IMPL_OOM), "OutOfMemory") == 0);
    CHECK(strcmp(tc_impl_error_message(TC_IMPL_OOM), "memory allocation failed") == 0);
    CHECK(strcmp(tc_impl_error_symbol(TC_IMPL_OOM), TC_ERR_OUT_OF_MEMORY_SYMBOL) == 0);
}

static void test_type_table(void) {
    TcTypeTable *table = tc_type_table_create(NULL);
    const TcType *i32;
    const TcType *i64;
    const TcType *isize;
    const TcType *usize;
    const TcType *void_type;
    const TcType *p32;
    const TcType *p32b;
    const TcType *p64;
    const TcType *params_i32[1];
    const TcType *fp_a;
    const TcType *fp_b;
    const TcType *fp_void;
    const TcType *s1;
    const TcType *s1b;
    const TcType *s2;
    const TcType *f64_before;
    CHECK(table != NULL);
    i32 = tc_type_scalar(table, TC_TY_I32);
    i64 = tc_type_scalar(table, TC_TY_I64);
    isize = tc_type_scalar(table, TC_TY_ISIZE);
    usize = tc_type_scalar(table, TC_TY_USIZE);
    void_type = tc_type_scalar(table, TC_TY_VOID);
    CHECK(i32 != NULL && i64 != NULL && isize != NULL && usize != NULL);
    CHECK(tc_type_scalar(table, TC_TY_I32) == i32);
    CHECK(!tc_type_equal(i32, isize));
    CHECK(!tc_type_equal(usize, i32));
    CHECK_EQ_INT(32, tc_type_bits(i32, 32));
    CHECK_EQ_INT(32, tc_type_bits(i32, 64));
    CHECK_EQ_INT(32, tc_type_bits(isize, 32));
    CHECK_EQ_INT(64, tc_type_bits(isize, 64));
    CHECK_EQ_INT(0, tc_type_bits(isize, 16));
    CHECK_EQ_INT(8, tc_type_bits(tc_type_scalar(table, TC_TY_BOOL), 64));
    CHECK_EQ_INT(0, tc_type_bits(void_type, 64));
    CHECK(tc_type_is_value(i32));
    CHECK(!tc_type_is_value(void_type));
    CHECK(tc_type_is_integer(isize));
    CHECK(!tc_type_is_integer(tc_type_scalar(table, TC_TY_F32)));
    CHECK(tc_type_is_signed_integer(isize));
    CHECK(!tc_type_is_signed_integer(usize));

    p32 = tc_type_ptr(table, i32);
    p32b = tc_type_ptr(table, i32);
    p64 = tc_type_ptr(table, i64);
    CHECK(p32 != NULL && p32 == p32b);
    CHECK(p32 != p64);
    CHECK_EQ_INT(32, tc_type_bits(p32, 32));
    CHECK_EQ_INT(64, tc_type_bits(p32, 64));
    CHECK(tc_type_ptr(table, void_type) == NULL);

    params_i32[0] = i32;
    fp_a = tc_type_fnptr(table, void_type, params_i32, 1);
    fp_b = tc_type_fnptr(table, void_type, params_i32, 1);
    fp_void = tc_type_fnptr(table, i32, NULL, 0);
    CHECK(fp_a != NULL && fp_a == fp_b);
    CHECK(fp_void != NULL && fp_void != fp_a);
    CHECK_EQ_INT(64, tc_type_bits(fp_a, 64));
    CHECK(tc_type_fnptr(table, void_type, &void_type, 1) == NULL);

    s1 = tc_type_struct(table, 1);
    s1b = tc_type_struct(table, 1);
    s2 = tc_type_struct(table, 2);
    CHECK(s1 != NULL && s1 == s1b && s1 != s2);
    CHECK(!tc_type_equal(s1, s2));

    f64_before = tc_type_scalar(table, TC_TY_F64);
    CHECK(f64_before != NULL);
    CHECK(tc_type_table_freeze(table));
    CHECK(tc_type_table_is_frozen(table));
    CHECK(tc_type_ptr(table, i64) == NULL);
    CHECK(tc_type_struct(table, 3) == NULL);
    CHECK(tc_type_scalar(table, TC_TY_I32) == i32);
    CHECK(tc_type_scalar(table, TC_TY_F64) == f64_before);
    {
        unsigned tag;
        for (tag = 0; tag < TC_TYPE_SCALAR_COUNT; tag++) {
            CHECK(tc_type_scalar(table, (TcTypeTag)tag) != NULL);
        }
    }
    tc_type_table_destroy(table);
}

static void init_w32_profile(TcPlatformProfile *p) {
    memset(p, 0, sizeof(*p));
    p->addr_width = 32;
    p->little_endian = 1;
    p->region[TC_REG_FRAME].base = 0x70000000u;
    p->region[TC_REG_FRAME].size = 0x00010000u;
    p->region[TC_REG_STATIC].base = 0x10000000u;
    p->region[TC_REG_STATIC].size = 0x00010000u;
    p->region[TC_REG_DEVICE].base = 0x20000000u;
    p->region[TC_REG_DEVICE].size = 0x00010000u;
    p->region[TC_REG_CODE].base = 0x30000000u;
    p->region[TC_REG_CODE].size = 0x00010000u;
    p->region[TC_REG_HEAP].base = 0x40000000u;
    p->region[TC_REG_HEAP].size = 0x00100000u;
    p->stack_size = 0x00010000u;
    p->depth_D = 64;
    p->exit.normal_status = 0;
    p->exit.trap_status = 1;
    p->entry = p->region[TC_REG_CODE].base;
    p->target = NULL; /* caller sets a target descriptor */
    p->device = TC_DEV_REJECT;
    p->checked_build = 0;
    p->disable_icf = 1;
}

static void test_profiles(void) {
    TcPlatformProfile linux_user;
    TcPlatformProfile baremetal;
    TcPlatformProfile w32;
    TcPlatformProfile bad;
    TcABIDesc bad_abi;
    TcTargetDesc w32_target;

    CHECK(tc_profile_init_aarch64_linux_user(&linux_user));
    CHECK(tc_profile_validate(&linux_user) == TC_PROFILE_OK);
    CHECK_EQ_INT(64, linux_user.addr_width);
    CHECK(linux_user.region[TC_REG_CODE].size != 0);
    CHECK(linux_user.entry >= linux_user.region[TC_REG_CODE].base);
    CHECK_EQ_INT(1, linux_user.disable_icf);
    CHECK(linux_user.depth_D >= 1);

    CHECK(tc_profile_init_aarch64_baremetal(&baremetal));
    CHECK(tc_profile_validate(&baremetal) == TC_PROFILE_OK);
    CHECK(baremetal.device == TC_DEV_VOLATILE);

    bad = linux_user;
    bad.depth_D = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_DEPTH);
    bad = linux_user;
    bad.disable_icf = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ICF);
    bad = linux_user;
    bad.addr_width = 40;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ADDR_WIDTH);
    bad = linux_user;
    bad.little_endian = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ENDIAN);
    {
        TcTargetDesc bad_target = tc_target_aarch64_linux_user;
        bad_target.stack_align = 4;
        bad = linux_user;
        bad.target = &bad_target;
        CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_STACK_ALIGN);
        bad_target = tc_target_aarch64_linux_user;
        bad_target.pointer_align = 4;
        bad = linux_user;
        bad.target = &bad_target;
        CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_POINTER_ALIGN);
    }
    bad = linux_user;
    bad.region[TC_REG_HEAP].size = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_REGION);
    bad = linux_user;
    bad.region[TC_REG_HEAP].base = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_REGION_ZERO);
    bad = linux_user;
    bad.region[TC_REG_HEAP].base = linux_user.region[TC_REG_CODE].base + 16;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_REGION_OVERLAP);
    bad = linux_user;
    bad.entry = 0;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ENTRY);
    bad = linux_user;
    bad.entry = linux_user.region[TC_REG_STATIC].base;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ENTRY);
    bad = linux_user;
    bad.checked_build = 2;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_CHECKED);

    /* W=32 is a first-class profile contract even though the target itself is
     * selected in S9.  A test-local target/ABI proves validation. */
    bad_abi = tc_abi_aapcs32_ilp32;
    w32_target.name = "w32-contract-test";
    w32_target.addr_width = 32;
    w32_target.little_endian = 1;
    w32_target.stack_align = 8;
    w32_target.pointer_align = 4;
    w32_target.abi = &bad_abi;
    init_w32_profile(&w32);
    w32.target = &w32_target;
    CHECK(tc_profile_validate(&w32) == TC_PROFILE_OK);
    CHECK_EQ_INT(4, w32.target->pointer_align);

    bad = w32;
    bad_abi = tc_abi_aapcs32_ilp32;
    bad_abi.int64_pair = 0;
    bad.target = &w32_target;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_ABI);
    bad_abi = tc_abi_aapcs32_ilp32;
    bad = w32;
    bad.region[TC_REG_HEAP].base = 0xffff0000u;
    bad.region[TC_REG_HEAP].size = 0x00020000u;
    CHECK(tc_profile_validate(&bad) == TC_PROFILE_ERR_REGION);

    CHECK(tc_profile_validate_runtime(&linux_user) == TC_PROFILE_ERR_IO);
    CHECK(tc_profile_validate(NULL) == TC_PROFILE_ERR_NULL);
}

static TcDiagnostic make_diag(TcDiagCode code, TcStage stage, uint32_t module,
                              uint32_t line, uint32_t column) {
    TcDiagnostic d;
    CHECK(tc_diag_make(&d, code, stage, module, "m.tc", line, column));
    d.sequence = 0;
    return d;
}

static int first_is(const TcDiagnostic *a, const TcDiagnostic *b) {
    return tc_diag_compare(a, b) < 0;
}

static TcDiagnostic make_site_diag(TcDiagCode code, TcDiagSite site, uint32_t line) {
    TcDiagnostic d = make_diag(code, TC_STAGE_SEM, 0, line, 1);
    d.site = site;
    d.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    return d;
}

/* TC-L0-DIAG-0003: §12.2 #10 call argument order. */
static void test_call_argument_order(void) {
    static const TcDiagCode order[] = {
        TC_CE_DUPLICATE_ARGUMENT,
        TC_CE_UNKNOWN_ARGUMENT,
        TC_CE_MISSING_ARGUMENT,
        TC_CE_ARGUMENT_COUNT,
        TC_CE_ARGUMENT_ORDER,
        TC_CE_TYPE_MISMATCH,
    };
    size_t i;
    for (i = 0; i + 1 < sizeof(order) / sizeof(order[0]); i++) {
        TcDiagnostic earlier = make_site_diag(order[i], TC_DIAG_SITE_CALL, 10);
        TcDiagnostic later = make_site_diag(order[i + 1], TC_DIAG_SITE_CALL, 10);
        CHECK(first_is(&earlier, &later));
    }
    {
        TcDiagnostic literal = make_site_diag(TC_CE_LITERAL_TYPE, TC_DIAG_SITE_CALL, 10);
        TcDiagnostic type = make_site_diag(TC_CE_TYPE_MISMATCH, TC_DIAG_SITE_CALL, 10);
        CHECK(first_is(&literal, &type));
    }
}

/* TC-L0-DIAG-0004: §12.2 #10 make field order. */
static void test_make_field_order(void) {
    static const TcDiagCode order[] = {
        TC_CE_DUPLICATE_FIELD,
        TC_CE_UNKNOWN_FIELD,
        TC_CE_MISSING_FIELD,
        TC_CE_OPERAND_COUNT,
        TC_CE_FIELD_ORDER,
        TC_CE_TYPE_MISMATCH,
    };
    size_t i;
    for (i = 0; i + 1 < sizeof(order) / sizeof(order[0]); i++) {
        TcDiagnostic earlier = make_site_diag(order[i], TC_DIAG_SITE_MAKE, 11);
        TcDiagnostic later = make_site_diag(order[i + 1], TC_DIAG_SITE_MAKE, 11);
        CHECK(first_is(&earlier, &later));
    }
}

/* TC-L0-DIAG-0005: §8.8.2/§9 call safety precedes arguments and
 * SAFE_VIOLATION outranks the non-@safe safety codes. */
static void test_call_safety_first(void) {
    TcDiagnostic unsafe_call =
        make_site_diag(TC_CE_UNSAFE_CALL, TC_DIAG_SITE_CALL, 12);
    TcDiagnostic fast_call =
        make_site_diag(TC_CE_FAST_CALL, TC_DIAG_SITE_CALL, 12);
    TcDiagnostic duplicate =
        make_site_diag(TC_CE_DUPLICATE_ARGUMENT, TC_DIAG_SITE_CALL, 12);
    TcDiagnostic safe =
        make_site_diag(TC_CE_SAFE_VIOLATION, TC_DIAG_SITE_CALL, 12);
    CHECK(first_is(&unsafe_call, &duplicate));
    CHECK(first_is(&fast_call, &duplicate));
    CHECK(first_is(&safe, &unsafe_call));
    CHECK(first_is(&safe, &fast_call));
}

/* TC-L0-DIAG-0006: §12.2 #5 negative shift precedes strict shl overflow. */
static void test_negative_shift_first(void) {
    TcDiagnostic overflow = make_diag(TC_RE_INTEGER_OVERFLOW, TC_STAGE_RT, 0, 13, 1);
    TcDiagnostic negative = make_diag(TC_RE_NEGATIVE_SHIFT, TC_STAGE_RT, 0, 13, 1);
    CHECK(first_is(&negative, &overflow));
}

static void test_diag_ordering(void) {
    TcDiagnostic earlier_stage = make_diag(TC_CE_CONST_OVERFLOW, TC_STAGE_CT, 0, 1, 1);
    TcDiagnostic later_stage = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 9, 99, 99);
    TcDiagnostic module0 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 9, 9);
    TcDiagnostic module1 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 1, 1, 1);
    TcDiagnostic line1 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 1, 9);
    TcDiagnostic line2 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 2, 1);
    TcDiagnostic col1 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 1, 1);
    TcDiagnostic col2 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 1, 2);
    TcDiagnostic operand = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 3, 1);
    TcDiagnostic construct = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 3, 1);
    TcDiagnostic literal = make_diag(TC_CE_LITERAL_TYPE, TC_STAGE_SEM, 0, 4, 1);
    TcDiagnostic mismatch = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 4, 1);
    TcDiagnostic invalid = make_diag(TC_RE_FLOAT_INVALID, TC_STAGE_RT, 0, 5, 1);
    TcDiagnostic overflow = make_diag(TC_RE_FLOAT_OVERFLOW, TC_STAGE_RT, 0, 5, 1);

    CHECK(first_is(&later_stage, &earlier_stage));
    CHECK(first_is(&module0, &module1));
    CHECK(first_is(&line1, &line2));
    CHECK(first_is(&col1, &col2));
    construct.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    operand.origin = TC_DIAG_ORIGIN_OPERAND;
    CHECK(first_is(&operand, &construct));
    CHECK(first_is(&literal, &mismatch));
    CHECK(first_is(&invalid, &overflow));
}

/* TC-L0-DIAG-0001: §12.2 #9 origin dominates §12.2 #4 rank.  This is a
 * comparator pin, not a reachable return-site candidate pair: standard §8.3
 * gives return-operand mismatch only TC_CE_RETURN_TYPE. */
static void test_diag_origin_rank_conflict(void) {
    TcDiagnostic operand = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 6, 1);
    TcDiagnostic construct = make_diag(TC_CE_RETURN_TYPE, TC_STAGE_SEM, 0, 6, 1);
    construct.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    CHECK(first_is(&operand, &construct));
    CHECK(!first_is(&construct, &operand));
}

/* TC-L0-DIAG-0002: standard §12.3/§9 SAFE_VIOLATION priority over
 * UNSAFE_* / FAST_CALL is encoded in the frozen same-site rank.  The
 * diagnostics are construct-level; a real emitter makes the four codes
 * mutually exclusive by context (standard §9.1/§9.3). */
static void test_safe_violation_priority(void) {
    TcDiagnostic safe = make_diag(TC_CE_SAFE_VIOLATION, TC_STAGE_SEM, 0, 7, 1);
    TcDiagnostic unsafe_op = make_diag(TC_CE_UNSAFE_OPERATION, TC_STAGE_SEM, 0, 7, 1);
    TcDiagnostic unsafe_call = make_diag(TC_CE_UNSAFE_CALL, TC_STAGE_SEM, 0, 7, 1);
    TcDiagnostic fast_call = make_diag(TC_CE_FAST_CALL, TC_STAGE_SEM, 0, 7, 1);
    TcDiagnostic operand_type;
    TcDiagnostic safe_construct;
    safe.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    unsafe_op.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    unsafe_call.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    fast_call.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    CHECK(tc_diag_info(TC_CE_SAFE_VIOLATION)->same_site_rank >
          tc_diag_info(TC_CE_UNSAFE_OPERATION)->same_site_rank);
    CHECK(tc_diag_info(TC_CE_SAFE_VIOLATION)->same_site_rank >
          tc_diag_info(TC_CE_UNSAFE_CALL)->same_site_rank);
    CHECK(tc_diag_info(TC_CE_SAFE_VIOLATION)->same_site_rank >
          tc_diag_info(TC_CE_FAST_CALL)->same_site_rank);
    CHECK(first_is(&safe, &unsafe_op));
    CHECK(first_is(&safe, &unsafe_call));
    CHECK(first_is(&safe, &fast_call));

    /* §12.2 #9 still wins over the SAFE_VIOLATION rank for operand-internal
     * diagnostics; rank only breaks ties within the same origin class. */
    operand_type = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 8, 1);
    safe_construct = make_diag(TC_CE_SAFE_VIOLATION, TC_STAGE_SEM, 0, 8, 1);
    safe_construct.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    CHECK(first_is(&operand_type, &safe_construct));
}

typedef struct {
    int fail_after;
    int calls;
} FailCtx;

static void *fail_alloc(void *ctx, size_t size) {
    FailCtx *c = (FailCtx *)ctx;
    if (c->calls++ >= c->fail_after) {
        return NULL;
    }
    return malloc(size);
}

static void *fail_realloc(void *ctx, void *ptr, size_t size) {
    FailCtx *c = (FailCtx *)ctx;
    if (c->calls++ >= c->fail_after) {
        return NULL;
    }
    return realloc(ptr, size);
}

static void fail_free(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

static void test_diag_buffer(void) {
    TcDiagBuffer *a = tc_diag_buffer_create(NULL);
    TcDiagBuffer *b = tc_diag_buffer_create(NULL);
    TcDiagnostic d1 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 2, 8, 3);
    TcDiagnostic d2 = make_diag(TC_CE_UNDEFINED_NAME, TC_STAGE_SEM, 1, 20, 1);
    TcDiagnostic d3 = make_diag(TC_CE_CONST_OVERFLOW, TC_STAGE_CT, 0, 1, 1);
    TcDiagnostic d4 = make_diag(TC_CE_TYPE_MISMATCH, TC_STAGE_SEM, 0, 9, 1);
    TcDiagnostic bad = make_diag(TC_CE_CONST_OVERFLOW, TC_STAGE_CT, 0, 1, 1);
    const TcDiagnostic *first;
    TcAllocator failing;
    FailCtx ctx;
    TcDiagBuffer *oom;
    CHECK(a != NULL && b != NULL);
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(a, &d1));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(a, &d2));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(a, &d3));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(a, &d4));
    CHECK_EQ_INT(4, tc_diag_buffer_count(a));
    first = tc_diag_buffer_first(a);
    CHECK(first != NULL);
    CHECK(first->code == TC_CE_TYPE_MISMATCH);
    CHECK_EQ_INT(0, first->module_index);
    CHECK_EQ_INT(9, first->primary.line);
    CHECK(tc_diag_buffer_at(a, 4) == NULL);
    CHECK(tc_diag_buffer_at(a, 0) != NULL);

    /* Same set, reversed insertion: first diagnostic is unchanged. */
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(b, &d4));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(b, &d3));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(b, &d2));
    CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(b, &d1));
    CHECK(tc_diag_buffer_first(a)->code == tc_diag_buffer_first(b)->code);
    CHECK(tc_diag_buffer_first(a)->primary.line == tc_diag_buffer_first(b)->primary.line);
    CHECK(tc_diag_buffer_first(a)->primary.column == tc_diag_buffer_first(b)->primary.column);

    bad.stage = TC_STAGE_LEX;
    CHECK_EQ_INT(TC_IMPL_INTERNAL, tc_diag_buffer_push(a, &bad));
    CHECK_EQ_INT(TC_IMPL_INTERNAL, tc_diag_buffer_push(NULL, &d1));

    ctx.fail_after = 1;
    ctx.calls = 0;
    failing.alloc = fail_alloc;
    failing.realloc = fail_realloc;
    failing.free = fail_free;
    failing.ctx = &ctx;
    oom = tc_diag_buffer_create(&failing);
    CHECK(oom != NULL);
    CHECK_EQ_INT(TC_IMPL_OOM, tc_diag_buffer_push(oom, &d1));
    tc_diag_buffer_destroy(oom);

    tc_diag_buffer_destroy(a);
    tc_diag_buffer_destroy(b);
    tc_diag_buffer_destroy(NULL);
}

static void test_ir_shape_and_zero_pass(void) {
    TcInstr instr;
    TcPassPipeline o0;
    TcPassPipeline o1;
    TcOptLevel level;
    CHECK_EQ_INT(51, TC_OPCODE_COUNT);
    CHECK(!tc_opcode_has_result(TC_STORE));
    CHECK(!tc_opcode_has_result(TC_MEMMOVE));
    CHECK(!tc_opcode_has_result(TC_BR));
    CHECK(!tc_opcode_has_result(TC_BR_IF));
    CHECK(!tc_opcode_has_result(TC_SWITCH));
    CHECK(!tc_opcode_has_result(TC_RETURN));
    CHECK(!tc_opcode_has_result(TC_TRAP));
    CHECK(!tc_opcode_has_result(TC_WRITE));
    CHECK(!tc_opcode_has_result(TC_WRITELN));
    CHECK(tc_opcode_has_result(TC_LOAD));
    CHECK(tc_opcode_has_result(TC_CALL));
    CHECK(tc_opcode_has_result(TC_MAKE));
    CHECK(tc_opcode_has_result(TC_READ));
    CHECK(tc_opcode_is_terminator(TC_BR));
    CHECK(tc_opcode_is_terminator(TC_TRAP));
    CHECK(!tc_opcode_is_terminator(TC_CALL));
    memset(&instr, 0, sizeof(instr));
    instr.op = TC_STORE;
    instr.dst.kind = TC_OP_VREG;
    CHECK(!tc_instr_lhs_is_valid(&instr));
    instr.dst.kind = TC_OP_DISCARD;
    CHECK(tc_instr_lhs_is_valid(&instr));
    instr.op = TC_ADD;
    CHECK(tc_instr_lhs_is_valid(&instr)); /* _ = add: result discarded */
    instr.dst.kind = TC_OP_VREG;
    CHECK(tc_instr_lhs_is_valid(&instr));
    instr.op = TC_READ;
    instr.dst.kind = TC_OP_DISCARD;
    CHECK(tc_instr_lhs_is_valid(&instr)); /* _ = read: result discarded */
    instr.op = TC_CALL;
    CHECK(tc_instr_lhs_is_valid(&instr)); /* _ = call: result discarded */

    CHECK(tc_pipeline_build(TC_OPT_O0, &o0));
    CHECK(tc_pipeline_build(TC_OPT_O2, &o1));
    CHECK_EQ_INT(0, o0.pass_count);
    CHECK_EQ_INT(0, o1.pass_count);
    CHECK(tc_pipeline_is_zero_pass(&o0));
    CHECK(tc_pipeline_levels_equivalent(&o0, &o1));
    CHECK(tc_opt_level_parse("O0", &level) && level == TC_OPT_O0);
    CHECK(tc_opt_level_parse("-O2", &level) && level == TC_OPT_O2);
    CHECK(!tc_opt_level_parse("O1", &level));
    CHECK(tc_module_loader_absent.resolve(NULL, "a", "b", NULL) == TC_MODULE_ERROR);
}

int main(void) {
    test_contract_and_code_table();
    test_type_table();
    test_profiles();
    test_diag_ordering();
    test_diag_origin_rank_conflict();
    test_safe_violation_priority();
    test_call_argument_order();
    test_make_field_order();
    test_call_safety_first();
    test_negative_shift_first();
    test_diag_buffer();
    test_ir_shape_and_zero_pass();
    if (failures != 0) {
        fprintf(stderr, "contract tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("contract tests: all checks passed\n");
    return 0;
}
