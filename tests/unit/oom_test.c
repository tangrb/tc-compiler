#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backend/aarch64/emit.h"
#include "backend/arm32/emit.h"
#include "backend/riscv32/emit.h"
#include "frontend/sem.h"
#include "ir/linemap.h"
#include "runtime/profile.h"
#include "temp_file.h"
#include "vm/vm.h"

typedef struct {
    TcAllocator allocator;
    size_t fail_after;
    size_t calls;
    size_t live;
    int armed;
} FailAllocator;

static int failures;
static size_t checks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int should_fail(FailAllocator *allocator) {
    size_t call = allocator->calls++;
    return allocator->armed && call == allocator->fail_after;
}

static void *fail_alloc(void *ctx, size_t size) {
    FailAllocator *allocator = (FailAllocator *)ctx;
    void *memory;
    if (should_fail(allocator)) {
        return NULL;
    }
    memory = malloc(size);
    if (memory != NULL) {
        allocator->live++;
    }
    return memory;
}

static void *fail_realloc(void *ctx, void *ptr, size_t size) {
    FailAllocator *allocator = (FailAllocator *)ctx;
    void *memory;
    if (should_fail(allocator)) {
        return NULL;
    }
    memory = realloc(ptr, size);
    if (memory != NULL && ptr == NULL) {
        allocator->live++;
    }
    return memory;
}

static void fail_free(void *ctx, void *ptr) {
    FailAllocator *allocator = (FailAllocator *)ctx;
    if (ptr != NULL) {
        allocator->live = allocator->live == 0 ? SIZE_MAX : allocator->live - 1u;
    }
    free(ptr);
}

static void init_allocator(FailAllocator *allocator, TcPlatformProfile *profile) {
    memset(allocator, 0, sizeof(*allocator));
    allocator->allocator.alloc = fail_alloc;
    allocator->allocator.realloc = fail_realloc;
    allocator->allocator.free = fail_free;
    allocator->allocator.ctx = allocator;
    profile->alloc = &allocator->allocator;
}

static void arm_failure(FailAllocator *allocator, size_t index) {
    allocator->fail_after = index;
    allocator->calls = 0;
    allocator->armed = 1;
}

static void disarm_failure(FailAllocator *allocator) {
    allocator->armed = 0;
}

static int write_source(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    int ok = file != NULL;
    if (ok && fwrite(text, 1, length, file) != length) {
        ok = 0;
    }
    if (file != NULL && fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

static int discard_write(void *ctx, const uint8_t *bytes, size_t size) {
    (void)ctx;
    (void)bytes;
    (void)size;
    return 0;
}

static int discard_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static int empty_read(void *ctx, uint8_t *buf, size_t cap, size_t *size,
                      TcIoStatus *status) {
    (void)ctx;
    (void)buf;
    (void)cap;
    *size = 0;
    *status = TC_IO_EOF;
    return 0;
}

static void ignore_trap(void *ctx, TcErrorKind kind, int line) {
    (void)ctx;
    (void)kind;
    (void)line;
}

static void test_analysis_oom(const char *root, FailAllocator *allocator) {
    TcPlatformProfile profile;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl;
    TcAnalysis *analysis;
    size_t baseline;
    size_t i;

    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    init_allocator(allocator, &profile);
    disarm_failure(allocator);
    allocator->calls = 0;
    analysis = tc_analyze_with_profile(root, &profile, profile.addr_width,
                                       NULL, 0, &status, &diag, &impl);
    baseline = allocator->calls;
    CHECK(analysis != NULL);
    CHECK(status == TC_ANALYSIS_OK);
    CHECK(impl == TC_IMPL_OK);
    if (analysis != NULL) {
        tc_analysis_destroy(analysis);
    }
    CHECK(baseline != 0);
    CHECK(allocator->live == 0);
    for (i = 0; i < baseline; i++) {
        arm_failure(allocator, i);
        analysis = tc_analyze_with_profile(root, &profile, profile.addr_width,
                                           NULL, 0, &status, &diag, &impl);
        disarm_failure(allocator);
        CHECK(status == TC_ANALYSIS_IMPL);
        CHECK(impl == TC_IMPL_OOM);
        if (analysis != NULL) {
            tc_analysis_destroy(analysis);
        }
        CHECK(allocator->live == 0);
    }
}

static void test_vm_oom(const char *root, FailAllocator *allocator) {
    TcPlatformProfile profile;
    TcIo io;
    TcIoSink sink;
    TcIoSrc source;
    TcTrapHandler trap;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl;
    TcAnalysis *analysis;
    const TcIR *ir;
    TcVmResult result;
    TcVmStatus vm_status;
    size_t baseline;
    size_t analysis_live;
    size_t i;

    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    sink.write = discard_write;
    sink.flush = discard_flush;
    sink.ctx = NULL;
    source.read = empty_read;
    source.ctx = NULL;
    trap.trap = ignore_trap;
    trap.ctx = NULL;
    io.out = &sink;
    io.in = &source;
    profile.io = &io;
    profile.trap = &trap;
    init_allocator(allocator, &profile);
    disarm_failure(allocator);
    allocator->calls = 0;
    analysis = tc_analyze_with_profile(root, &profile, profile.addr_width,
                                       NULL, 0, &status, &diag, &impl);
    CHECK(analysis != NULL);
    CHECK(status == TC_ANALYSIS_OK);
    CHECK(impl == TC_IMPL_OK);
    if (analysis == NULL || status != TC_ANALYSIS_OK) {
        if (analysis != NULL) {
            tc_analysis_destroy(analysis);
        }
        return;
    }
    ir = tc_analysis_ir(analysis);
    analysis_live = allocator->live;
    allocator->calls = 0;
    vm_status = tc_vm_run(ir, &profile, &result, &impl);
    CHECK(vm_status == TC_VM_STATUS_OK);
    CHECK(impl == TC_IMPL_OK);
    CHECK(result.trapped == 0);
    CHECK(allocator->live == analysis_live);
    baseline = allocator->calls;
    CHECK(baseline != 0);
    for (i = 0; i < baseline; i++) {
        arm_failure(allocator, i);
        vm_status = tc_vm_run(ir, &profile, &result, &impl);
        disarm_failure(allocator);
        CHECK(vm_status == TC_VM_STATUS_IMPL);
        CHECK(impl == TC_IMPL_OOM);
        CHECK(allocator->live == analysis_live);
    }
    tc_analysis_destroy(analysis);
    CHECK(allocator->live == 0);
}

typedef enum {
    BACKEND_AARCH64,
    BACKEND_ARM32,
    BACKEND_RISCV32
} Backend;

static size_t emit_backend(Backend backend, const TcAnalysis *analysis,
                           const TcPlatformProfile *profile, FILE *out,
                           char *error, size_t error_capacity) {
    const TcIR *ir = tc_analysis_ir(analysis);
    if (backend == BACKEND_AARCH64) {
        return (size_t)tc_aarch64_emit_asm(out, ir, profile, profile->target, NULL,
                                          error, error_capacity, NULL);
    }
    if (backend == BACKEND_ARM32) {
        return (size_t)tc_arm32_emit_asm(out, ir, profile, profile->target, NULL,
                                        error, error_capacity, NULL);
    }
    return (size_t)tc_riscv32_emit_asm(out, ir, profile, profile->target, NULL,
                                      error, error_capacity, NULL);
}

static size_t oom_status(Backend backend) {
    if (backend == BACKEND_AARCH64) {
        return (size_t)TC_AARCH64_EMIT_OOM;
    }
    if (backend == BACKEND_ARM32) {
        return (size_t)TC_ARM32_EMIT_OOM;
    }
    return (size_t)TC_RISCV32_EMIT_OOM;
}

static size_t ok_status(Backend backend) {
    if (backend == BACKEND_AARCH64) {
        return (size_t)TC_AARCH64_EMIT_OK;
    }
    if (backend == BACKEND_ARM32) {
        return (size_t)TC_ARM32_EMIT_OK;
    }
    return (size_t)TC_RISCV32_EMIT_OK;
}

static void test_backend_oom(const char *root, FailAllocator *allocator,
                             Backend backend) {
    TcPlatformProfile profile;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl;
    TcAnalysis *analysis;
    FILE *out;
    char error[256];
    size_t baseline;
    size_t analysis_live;
    size_t i;
    int initialized = backend == BACKEND_AARCH64
                          ? tc_profile_init_aarch64_linux_user(&profile)
                          : backend == BACKEND_ARM32
                                ? tc_profile_init_arm32_baremetal(&profile)
                                : tc_profile_init_riscv32_baremetal(&profile);
    CHECK(initialized);
    if (!initialized) {
        return;
    }
    init_allocator(allocator, &profile);
    disarm_failure(allocator);
    analysis = tc_analyze_with_profile(root, &profile, profile.addr_width,
                                       NULL, 0, &status, &diag, &impl);
    CHECK(analysis != NULL);
    CHECK(status == TC_ANALYSIS_OK);
    CHECK(impl == TC_IMPL_OK);
    if (analysis == NULL || status != TC_ANALYSIS_OK) {
        if (analysis != NULL) {
            tc_analysis_destroy(analysis);
        }
        return;
    }
    out = tc_test_tmpfile();
    CHECK(out != NULL);
    if (out == NULL) {
        tc_analysis_destroy(analysis);
        return;
    }
    CHECK(setvbuf(out, NULL, _IONBF, 0) == 0);
    disarm_failure(allocator);
    allocator->calls = 0;
    analysis_live = allocator->live;
    CHECK(emit_backend(backend, analysis, &profile, out, error,
                       sizeof(error)) == ok_status(backend));
    CHECK(allocator->live == analysis_live);
    baseline = allocator->calls;
    fclose(out);
    CHECK(baseline != 0);
    for (i = 0; i < baseline; i++) {
        size_t result;
        out = tc_test_tmpfile();
        CHECK(out != NULL);
        if (out == NULL) {
            break;
        }
        CHECK(setvbuf(out, NULL, _IONBF, 0) == 0);
        arm_failure(allocator, i);
        result = emit_backend(backend, analysis, &profile, out, error,
                              sizeof(error));
        disarm_failure(allocator);
        fclose(out);
        CHECK(result == oom_status(backend));
        CHECK(allocator->live == analysis_live);
    }
    tc_analysis_destroy(analysis);
    CHECK(allocator->live == 0);
}

static void test_linemap_oom(FailAllocator *allocator) {
    TcPlatformProfile profile;
    TcModule module = {0};
    TcInstr instr;
    TcBlock block = {0};
    TcUnit unit = {0};
    TcIR ir = {0};
    TcLineMap *linemap;
    size_t baseline;
    size_t i;
    memset(&instr, 0, sizeof(instr));
    instr.span.file = "src/main.tc";
    instr.span.line = 1u;
    instr.span.column = 1u;
    module.path = "src/main.tc";
    block.instrs = &instr;
    block.n = 1u;
    unit.func_id = TC_UNIT_TOP_LEVEL;
    unit.blocks = &block;
    unit.nblocks = 1u;
    ir.modules = &module;
    ir.nmodules = 1u;
    ir.units = &unit;
    ir.nunits = 1u;
    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    init_allocator(allocator, &profile);
    ir.profile = &profile;
    disarm_failure(allocator);
    allocator->calls = 0;
    linemap = NULL;
    CHECK(tc_linemap_build(&ir, &linemap) == TC_IMPL_OK);
    CHECK(linemap != NULL);
    tc_linemap_destroy(linemap);
    CHECK(allocator->live == 0);
    baseline = allocator->calls;
    CHECK(baseline != 0);
    for (i = 0; i < baseline; i++) {
        arm_failure(allocator, i);
        linemap = NULL;
        CHECK(tc_linemap_build(&ir, &linemap) == TC_IMPL_OOM);
        disarm_failure(allocator);
        CHECK(linemap == NULL);
        CHECK(allocator->live == 0);
    }
}

int main(void) {
    char directory[] = TC_TEST_TMP_ROOT "/tc-oom-XXXXXX";
    char root[512];
    char lib[512];
    FailAllocator allocator;
    static const char *source =
        "#program\n"
        "import lib\n"
        "struct Pair then\n"
        "    var a: int32\n"
        "    var b: int64\n"
        "end\n"
        "var p0: Pair = make Pair, 1, 2\n"
        "var p1: Pair = make Pair, 3, 4\n"
        "var p2: Pair = make Pair, 5, 6\n"
        "var p3: Pair = make Pair, 7, 8\n"
        "var p4: Pair = make Pair, 9, 10\n"
        "var p5: Pair = make Pair, 11, 12\n"
        "var p6: Pair = make Pair, 13, 14\n"
        "var p7: Pair = make Pair, 15, 16\n"
        "var x: int64 = call lib.f, 1\n"
        "_ = writeln int64, x\n";
    static const char *library =
        "#lib\n"
        "public func f(value: int64) int64 then\n"
        "    var local: int64 = mul int64, value, 3\n"
        "    _ = return local\n"
        "end\n";

    if (mkdtemp(directory) == NULL) {
        printf("FAIL cannot create OOM fixture directory\n");
        return 1;
    }
    snprintf(root, sizeof(root), "%s/root.tc", directory);
    snprintf(lib, sizeof(lib), "%s/lib.tc", directory);
    if (!write_source(root, source) || !write_source(lib, library)) {
        printf("FAIL cannot write OOM fixture\n");
        unlink(root);
        unlink(lib);
        rmdir(directory);
        return 1;
    }
    test_analysis_oom(root, &allocator);
    test_linemap_oom(&allocator);
    test_vm_oom(root, &allocator);
    test_backend_oom(root, &allocator, BACKEND_AARCH64);
    test_backend_oom(root, &allocator, BACKEND_ARM32);
    test_backend_oom(root, &allocator, BACKEND_RISCV32);
    unlink(root);
    unlink(lib);
    rmdir(directory);
    if (failures != 0) {
        printf("OOM contract: %d failure(s) in %zu checks\n", failures,
               checks);
        return 1;
    }
    printf("OOM contract: all %zu checks passed\n", checks);
    return 0;
}
