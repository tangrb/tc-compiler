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
#include "ir/pointer_state.h"
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
    CHECK(tc_type_is_scalar(i32));
    CHECK(!tc_type_is_scalar(void_type)); /* void is not a scalar value type */
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
    d.site_id = 1;
    d.site_phase = TC_DIAG_PHASE_ARGUMENTS;
    d.origin = TC_DIAG_ORIGIN_CONSTRUCT;
    return d;
}

static TcDiagnostic make_phase_diag(TcDiagCode code, TcDiagSite site,
                                    TcDiagPhase phase, uint32_t line) {
    TcDiagnostic d = make_site_diag(code, site, line);
    d.site_phase = phase;
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
    /* Cross-position pin: §12.2 #10 applies inside the same call site even
     * when the diagnostic columns differ.  Unknown (rank 25) must beat
     * missing (rank 20) at column 1. */
    {
        TcDiagnostic missing = make_site_diag(TC_CE_MISSING_ARGUMENT, TC_DIAG_SITE_CALL, 20);
        TcDiagnostic unknown = make_site_diag(TC_CE_UNKNOWN_ARGUMENT, TC_DIAG_SITE_CALL, 20);
        missing.site_id = 77;
        unknown.site_id = 77;
        missing.primary.column = 1;
        unknown.primary.column = 20;
        CHECK(first_is(&unknown, &missing));
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
    /* Cross-position pin for make: unknown field before missing field. */
    {
        TcDiagnostic missing = make_site_diag(TC_CE_MISSING_FIELD, TC_DIAG_SITE_MAKE, 21);
        TcDiagnostic unknown = make_site_diag(TC_CE_UNKNOWN_FIELD, TC_DIAG_SITE_MAKE, 21);
        missing.site_id = 78;
        unknown.site_id = 78;
        missing.primary.column = 1;
        unknown.primary.column = 20;
        CHECK(first_is(&unknown, &missing));
    }
}

/* TC-L0-DIAG-0005: §8.8.2/§9 call safety precedes arguments and
 * SAFE_VIOLATION outranks the non-@safe safety codes. */
static void test_call_safety_first(void) {
    TcDiagnostic unsafe_call =
        make_phase_diag(TC_CE_UNSAFE_CALL, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_SAFETY, 12);
    TcDiagnostic fast_call =
        make_phase_diag(TC_CE_FAST_CALL, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_SAFETY, 12);
    TcDiagnostic duplicate =
        make_phase_diag(TC_CE_DUPLICATE_ARGUMENT, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_ARGUMENTS, 12);
    TcDiagnostic safe =
        make_phase_diag(TC_CE_SAFE_VIOLATION, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_SAFETY, 12);
    CHECK(first_is(&unsafe_call, &duplicate));
    CHECK(first_is(&fast_call, &duplicate));
    CHECK(first_is(&safe, &unsafe_call));
    CHECK(first_is(&safe, &fast_call));
}

/* TC-L0-DIAG-0007: §8.8.2 call/make check phase order. */
static void test_call_check_phase_order(void) {
    TcDiagnostic target =
        make_phase_diag(TC_CE_UNDEFINED_FUNCTION, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_TARGET, 14);
    TcDiagnostic private_access =
        make_phase_diag(TC_CE_PRIVATE_ACCESS, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_TARGET, 14);
    TcDiagnostic unsafe_call =
        make_phase_diag(TC_CE_UNSAFE_CALL, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_SAFETY, 14);
    TcDiagnostic result_shape =
        make_phase_diag(TC_CE_TYPE_MISMATCH, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_RESULT_SHAPE, 14);
    TcDiagnostic receiver =
        make_phase_diag(TC_CE_TYPE_MISMATCH, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_RECEIVER, 14);
    TcDiagnostic duplicate =
        make_phase_diag(TC_CE_DUPLICATE_ARGUMENT, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_ARGUMENTS, 14);
    TcDiagnostic missing =
        make_phase_diag(TC_CE_MISSING_ARGUMENT, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_ARGUMENTS, 14);
    TcDiagnostic order =
        make_phase_diag(TC_CE_ARGUMENT_ORDER, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_ARGUMENTS, 14);
    TcDiagnostic void_result =
        make_phase_diag(TC_CE_VOID, TC_DIAG_SITE_CALL, TC_DIAG_PHASE_RESULT_SHAPE, 14);

    CHECK(first_is(&target, &duplicate));
    CHECK(first_is(&private_access, &missing));
    CHECK(first_is(&unsafe_call, &duplicate));
    CHECK(first_is(&result_shape, &order));
    CHECK(first_is(&void_result, &duplicate));
    CHECK(first_is(&duplicate, &receiver));
    CHECK(first_is(&missing, &receiver));

    {
        TcDiagnostic make_target =
            make_phase_diag(TC_CE_UNDEFINED_NAME, TC_DIAG_SITE_MAKE, TC_DIAG_PHASE_TARGET, 15);
        TcDiagnostic make_duplicate =
            make_phase_diag(TC_CE_DUPLICATE_FIELD, TC_DIAG_SITE_MAKE, TC_DIAG_PHASE_ARGUMENTS, 15);
        CHECK(first_is(&make_target, &make_duplicate));
    }
}

/* TC-L0-DIAG-0008: site_id scopes #10; zero is rejected and different sites
 * fall back to source order. */
static void test_site_id_scope(void) {
    TcDiagnostic missing = make_site_diag(TC_CE_MISSING_ARGUMENT, TC_DIAG_SITE_CALL, 16);
    TcDiagnostic unknown = make_site_diag(TC_CE_UNKNOWN_ARGUMENT, TC_DIAG_SITE_CALL, 16);
    missing.site_id = 1;
    unknown.site_id = 2;
    missing.primary.column = 1;
    unknown.primary.column = 20;
    CHECK(first_is(&missing, &unknown)); /* different sites: source order */

    {
        TcDiagBuffer *buffer = tc_diag_buffer_create(NULL);
        TcDiagnostic bad = make_site_diag(TC_CE_MISSING_ARGUMENT, TC_DIAG_SITE_CALL, 17);
        bad.site_id = 0;
        CHECK(buffer != NULL);
        CHECK_EQ_INT(TC_IMPL_INTERNAL, tc_diag_buffer_push(buffer, &bad));
        bad = make_site_diag(TC_CE_MISSING_ARGUMENT, TC_DIAG_SITE_CALL, 17);
        bad.site_phase = TC_DIAG_PHASE_NONE;
        CHECK_EQ_INT(TC_IMPL_INTERNAL, tc_diag_buffer_push(buffer, &bad));
        tc_diag_buffer_destroy(buffer);
    }
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

    {
        TcDiagBuffer *owned = tc_diag_buffer_create(NULL);
        char mutable_file[16] = "tmp.tc";
        TcDiagnostic d = make_diag(TC_CE_UNDEFINED_NAME, TC_STAGE_SEM, 0, 2, 3);
        d.primary.file = mutable_file;
        CHECK(owned != NULL);
        CHECK_EQ_INT(TC_IMPL_OK, tc_diag_buffer_push(owned, &d));
        strcpy(mutable_file, "changed.tc");
        CHECK(strcmp(tc_diag_buffer_at(owned, 0)->primary.file, "tmp.tc") == 0);
        tc_diag_buffer_destroy(owned);
    }

    tc_diag_buffer_destroy(a);
    tc_diag_buffer_destroy(b);
    tc_diag_buffer_destroy(NULL);
}

/* TC-L0-CONTRACT-0001: frozen struct/enum/field/default/unit checks. */
static void test_contract_declarations(void) {
    TcTypeTable *types = tc_type_table_create(NULL);
    const TcType *i32;
    TcSpan span;
    TcOperand op;
    TcArg arg;
    TcField field;
    TcStructDef sd;
    TcObject obj;
    TcAddress addr;
    TcBlock block;
    TcUnit unit;
    TcIR ir;
    TcParam param;
    TcFunc func;
    TcInitImage image;

    CHECK(types != NULL);
    i32 = tc_type_scalar(types, TC_TY_I32);
    CHECK(i32 != NULL);
    CHECK_EQ_INT(17, TC_TY_COUNT);
    CHECK_EQ_INT(14, TC_TYPE_SCALAR_COUNT);
    CHECK_EQ_INT(5, TC_REGION_COUNT);
    CHECK_EQ_INT(5, TC_REG_COUNT);
    CHECK_EQ_INT(0, TC_OP_VREG);
    CHECK_EQ_INT(6, TC_OP_DISCARD);
    CHECK_EQ_INT(0, TC_STRICT);
    CHECK_EQ_INT(5, TC_VOLATILE);
    CHECK_EQ_INT(2, TC_PARAM_NO_ALIAS);
    CHECK_EQ_INT(4, TC_PARAM_ALIGN);
    CHECK_EQ_INT(8, TC_PARAM_RANGE);
    CHECK_EQ_INT(1u << 0, TC_FN_UNSAFE);
    CHECK_EQ_INT(1u << 10, TC_FN_NOINLINE);
    CHECK_EQ_INT(1, TC_SPAN_FIRST_LINE);
    CHECK_EQ_INT(1, TC_SPAN_FIRST_COLUMN);
    CHECK_EQ_INT(UINT32_MAX, TC_SPAN_NO_INVALID_BYTE);
    CHECK_EQ_INT(UINT32_MAX, TC_UNIT_TOP_LEVEL);
    CHECK_EQ_INT(UINT32_MAX, TC_OBJECT_NO_PARENT);
    CHECK_EQ_INT(UINT32_MAX, TC_OBJECT_NO_MODULE);
    CHECK_EQ_INT(UINT32_MAX, TC_ADDRESS_NO_OBJECT);
    CHECK(TC_IO_OK == 0 && TC_IO_EOF == 1 && TC_IO_ERROR == 2);
    CHECK(TC_DEV_REJECT == 0 && TC_DEV_VOLATILE == 3);
    CHECK((int)TC_EK_TRAP == (int)TC_RE_TRAP);

    memset(&span, 0, sizeof(span));
    span.file = "m.tc";
    span.line = 3;
    span.column = 7;
    span.valid_prefix_bytes = 2;
    span.invalid_byte_offset = 5;
    CHECK(span.line == 3 && span.column == 7);
    CHECK(span.valid_prefix_bytes == 2);
    CHECK(span.invalid_byte_offset == 5);
    CHECK(span.line >= TC_SPAN_FIRST_LINE && span.column >= TC_SPAN_FIRST_COLUMN);

    memset(&op, 0, sizeof(op));
    op.kind = TC_OP_FIELD;
    op.field.base = 4;
    op.field.nfields = 0;
    CHECK(op.kind == TC_OP_FIELD && op.field.base == 4);

    memset(&arg, 0, sizeof(arg));
    arg.name = NULL;
    arg.value = op;
    CHECK(arg.name == NULL && arg.value.kind == TC_OP_FIELD);

    memset(&field, 0, sizeof(field));
    field.name = "x";
    field.readonly = 1;
    field.padding = 4; /* bytes of 0x00 after this field */
    field.offset = 8;  /* byte offset in the current profile */
    CHECK(field.readonly == 1 && field.padding == 4 && field.offset == 8);

    memset(&sd, 0, sizeof(sd));
    sd.id = 1;
    sd.module = 2;
    sd.name = "S";
    sd.fields = NULL;
    sd.nfields = 0;
    sd.size = 16; /* sizeof S in bytes under one profile */
    CHECK(sd.id == 1 && sd.module == 2 && sd.size == 16);
    CHECK(sd.fields == NULL && sd.nfields == 0);

    memset(&obj, 0, sizeof(obj));
    obj.id = 1;
    obj.module = TC_OBJECT_NO_MODULE;
    obj.region = TC_REG_STATIC;
    obj.lifetime = TC_LIFE_PROGRAM;
    obj.writable = 1;
    obj.parent = TC_OBJECT_NO_PARENT;
    obj.offset = 0; /* bytes */
    obj.size = 8;   /* bytes */
    obj.type = i32;
    obj.init = NULL;
    CHECK(obj.region == TC_REG_STATIC);
    CHECK(obj.lifetime == TC_LIFE_PROGRAM);
    CHECK(obj.parent == TC_OBJECT_NO_PARENT);
    CHECK(obj.offset == 0 && obj.size == 8);
    CHECK(obj.writable == 1 && obj.init == NULL && obj.type == i32);

    memset(&addr, 0, sizeof(addr));
    addr.region = TC_REG_STATIC;
    addr.object = 1;
    addr.offset = 0;
    addr.type = i32;
    CHECK(addr.region == TC_REG_STATIC && addr.object == 1);
    CHECK(addr.offset == 0 && addr.type == i32);

    memset(&block, 0, sizeof(block));
    block.id = 0;
    block.n = 0;
    block.nsucc = 0;
    block.npred = 0;
    CHECK(block.id == 0 && block.n == 0);
    CHECK(block.instrs == NULL && block.succs == NULL && block.preds == NULL);

    memset(&unit, 0, sizeof(unit));
    unit.func_id = TC_UNIT_TOP_LEVEL;
    unit.nblocks = 0;
    unit.entry = 0;
    unit.nvregs = 0;
    unit.nvreg_objects = 0;
    CHECK(unit.func_id == TC_UNIT_TOP_LEVEL);
    CHECK(unit.blocks == NULL && unit.entry == 0);
    CHECK(unit.vreg_types == NULL && unit.vreg_objects == NULL);

    memset(&ir, 0, sizeof(ir));
    ir.nmodules = 0;
    ir.nstructs = 0;
    ir.nfuncs = 0;
    ir.nconsts = 0;
    ir.nobjects = 0;
    ir.nunits = 0;
    CHECK(ir.nmodules == 0 && ir.profile == NULL);
    CHECK(ir.types == NULL && ir.modules == NULL && ir.structs == NULL);
    CHECK(ir.funcs == NULL && ir.consts == NULL && ir.objects == NULL);
    CHECK(ir.units == NULL);
    CHECK(ir.profile == NULL);

    memset(&param, 0, sizeof(param));
    param.name = "p";
    param.type = i32;
    param.flags = TC_PARAM_NONNULL | TC_PARAM_ALIGN;
    param.align = 8;
    param.lo = 0x10;
    param.hi = 0x20;
    CHECK((param.flags & TC_PARAM_NONNULL) != 0);
    CHECK((param.flags & TC_PARAM_ALIGN) != 0);
    CHECK(param.type == i32 && param.align == 8);
    CHECK(param.lo == 0x10 && param.hi == 0x20);

    memset(&func, 0, sizeof(func));
    func.id = 3;
    func.module = 2;
    func.name = "f";
    func.flags = TC_FN_INLINE | TC_FN_NOINLINE;
    func.sig.ret = i32;
    func.sig.params = NULL;
    func.sig.nparams = 0;
    func.body = NULL;
    CHECK((func.flags & TC_FN_INLINE) != 0);
    CHECK((func.flags & TC_FN_NOINLINE) != 0);
    CHECK(func.id == 3 && func.module == 2 && func.sig.ret == i32);
    CHECK(func.body == NULL);

    memset(&image, 0, sizeof(image));
    image.bytes = NULL;
    image.size = 0;
    CHECK(image.bytes == NULL && image.size == 0);

    {
        TcModule mod;
        TcConst konst;
        TcIoSink sink;
        TcIoSrc src;
        TcTrapHandler trap;
        memset(&mod, 0, sizeof(mod));
        mod.id = 2;
        mod.path = "lib/mod.tc";
        CHECK(mod.id == 2 && strcmp(mod.path, "lib/mod.tc") == 0);
        memset(&konst, 0, sizeof(konst));
        konst.id = 4;
        konst.module = 2;
        konst.name = "K";
        konst.type = i32;
        konst.bits = 0x2a;
        konst.init = NULL;
        CHECK(konst.bits == 0x2a && konst.init == NULL);
        memset(&sink, 0, sizeof(sink));
        sink.ctx = NULL;
        CHECK(sink.write == NULL && sink.flush == NULL);
        memset(&src, 0, sizeof(src));
        src.ctx = NULL;
        CHECK(src.read == NULL);
        memset(&trap, 0, sizeof(trap));
        trap.ctx = NULL;
        CHECK(trap.trap == NULL);
    }
    tc_type_table_destroy(types);
}

/* TC-L0-OBJ-0001: standard §3.12 pointer-state permission matrix. */
static void test_pointer_states(void) {
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_COPY, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_COMPARE, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_PTRDIFF, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_LOAD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_STORE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_MEMMOVE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_PTRADD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_PTRSUB, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_CALL_INDIRECT, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_NULL, TC_PTR_OP_BITCAST, 0));

    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_LOAD, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_STORE, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_MEMMOVE, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_PTRADD, 1));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_PTRSUB, 1));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_CALL_INDIRECT, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_LIVE, TC_PTR_OP_BITCAST, 0));

    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_COPY, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_COMPARE, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_PTRDIFF, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_PTRADD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_PTRADD, 1));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_PTRSUB, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_LOAD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_STORE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_MEMMOVE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_ONE_PAST, TC_PTR_OP_BITCAST, 0));

    CHECK(tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_COPY, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_COMPARE, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_PTRDIFF, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_LOAD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_STORE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_MEMMOVE, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_PTRADD, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_PTRSUB, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_CALL_INDIRECT, 0));
    CHECK(tc_ptr_state_allows(TC_PTR_STATE_DANGLING, TC_PTR_OP_BITCAST, 0));

    CHECK(!tc_ptr_state_allows((TcPtrState)99, TC_PTR_OP_COPY, 0));
    CHECK(!tc_ptr_state_allows(TC_PTR_STATE_LIVE, (TcPtrOp)99, 0));
    CHECK(strcmp(tc_ptr_state_name(TC_PTR_STATE_ONE_PAST), "one-past") == 0);
    CHECK(strcmp(tc_ptr_op_name(TC_PTR_OP_CALL_INDIRECT), "call_indirect") == 0);
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
    test_call_check_phase_order();
    test_site_id_scope();
    test_negative_shift_first();
    test_diag_buffer();
    test_contract_declarations();
    test_pointer_states();
    test_ir_shape_and_zero_pass();
    if (failures != 0) {
        fprintf(stderr, "contract tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("contract tests: all checks passed\n");
    return 0;
}
