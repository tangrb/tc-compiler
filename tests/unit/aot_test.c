/* TC 0.0.46 AArch64 subset lowering unit tests (design §10.3, test plan S7).
 *
 * The emitter is exercised directly on finalized IR.  These checks pin the
 * instruction selection shape, frame alignment, deterministic output, static
 * initializer emission, call-depth checks and explicit unsupported rejection.
 * No qemu or linker is required; end-to-end behavior is covered by the L5/L6
 * suites.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backend/aarch64/emit.h"
#include "frontend/sem.h"
#include "runtime/profile.h"

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

typedef struct {
    char dir[64];
    char root[128];
    char lib[128];
} Fixture;

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

static int make_fixture(Fixture *fixture, const char *root, const char *lib) {
    snprintf(fixture->dir, sizeof(fixture->dir), "/tmp/tc-aot-XXXXXX");
    if (mkdtemp(fixture->dir) == NULL) {
        return 0;
    }
    snprintf(fixture->root, sizeof(fixture->root), "%s/root.tc",
             fixture->dir);
    snprintf(fixture->lib, sizeof(fixture->lib), "%s/lib.tc", fixture->dir);
    if (!write_text(fixture->root, root) || !write_text(fixture->lib, lib)) {
        return 0;
    }
    return 1;
}

static void free_fixture(Fixture *fixture) {
    unlink(fixture->root);
    unlink(fixture->lib);
    rmdir(fixture->dir);
}

static char *emit_fixture(const Fixture *fixture, size_t *out_len,
                          TcAArch64EmitStatus *out_status,
                          char *error_message,
                          size_t error_message_capacity) {
    TcPlatformProfile profile;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *analysis;
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream;

    if (!tc_profile_init_aarch64_linux_user(&profile)) {
        return NULL;
    }
    analysis = tc_analyze_with_profile(fixture->root, &profile,
                                       profile.addr_width, NULL, 0, &status,
                                       &diag, &impl);
    if (analysis == NULL || status != TC_ANALYSIS_OK) {
        if (analysis != NULL) {
            tc_analysis_destroy(analysis);
        }
        return NULL;
    }
    stream = open_memstream(&buffer, &length);
    if (stream == NULL) {
        tc_analysis_destroy(analysis);
        return NULL;
    }
    *out_status = tc_aarch64_emit_asm(
        stream, tc_analysis_ir(analysis), &profile, profile.target,
        error_message, error_message_capacity);
    fclose(stream);
    tc_analysis_destroy(analysis);
    *out_len = length;
    return buffer;
}

static void test_lowering_shapes(void) {
    Fixture fixture;
    TcAArch64EmitStatus status = TC_AARCH64_EMIT_INTERNAL;
    char error_message[256];
    size_t len = 0;
    char *text;
    if (!make_fixture(
            &fixture,
            "#program\n"
            "import lib\n"
            "var a: int32 = copy int32, 7\n"
            "var b: int32 = add int32, a, 5\n"
            "_ = writeln int32, b\n"
            "var c: int32 = div int32, b, 2\n"
            "var d: int32 = mod int32, b, 2\n"
            "var e: bool = lt int32, c, d\n"
            "_ = writeln bool, e\n"
            "var f: int32 = shl int32, 1, 3\n"
            "var g: int32 = call lib.f, f\n"
            "var h: usize = sizeof int32\n"
            "_ = writeln usize, h\n"
            "var i: int32 = copy int32, -1\n"
            "_ = switch i, l0, ld\n"
            "label l0:\n"
            "_ = br fin\n"
            "label ld:\n"
            "_ = br fin\n"
            "label fin:\n",
            "#lib\n"
            "public func f(x: int32) int32 then\n"
            "var r: int32 = add int32, x, 1\n"
            "_ = return r\n"
            "end\n")) {
        CHECK(0);
        return;
    }
    text = emit_fixture(&fixture, &len, &status, error_message,
                        sizeof(error_message));
    free_fixture(&fixture);
    CHECK(text != NULL);
    CHECK(status == TC_AARCH64_EMIT_OK);
    if (text == NULL) {
        return;
    }
    CHECK(len != 0);
    CHECK(strstr(text, "adds x11, x9, x10") != NULL);
    CHECK(strstr(text, "b.vs ") != NULL);
    CHECK(strstr(text, "sdiv x12, x9, x10") != NULL);
    CHECK(strstr(text, "msub x11, x10, x12, x9") != NULL);
    CHECK(strstr(text, "cbz x10, ") != NULL);
    CHECK(strstr(text, "tbnz x10, #63") != NULL);
    CHECK(strstr(text, "cset w11, lt") != NULL);
    CHECK(strstr(text, "bl tc_function_0") != NULL);
    CHECK(strstr(text, "__tc_call_depth") != NULL);
    CHECK(strstr(text, "stp x29, x30, [sp, #-16]!") != NULL);
    CHECK(strstr(text, "adrp x20, __tc_static_region") != NULL);
    CHECK(strstr(text, "movz x11, #0x4") != NULL);
    CHECK(strstr(text, "b.eq .Lswitch") != NULL);
    CHECK(strstr(text, "tc_main:") != NULL);
    free(text);
}

static void test_frame_alignment(void) {
    Fixture fixture;
    TcAArch64EmitStatus status = TC_AARCH64_EMIT_INTERNAL;
    char error_message[256];
    size_t len = 0;
    char *text;
    const char *cursor;
    int found = 0;
    if (!make_fixture(
            &fixture,
            "#program\nimport lib\nvar r: int64 = call lib.f, 1\n"
            "_ = writeln int64, r\n",
            "#lib\n"
            "public func f(x: int64) int64 then\n"
            "var a: bool = copy bool, true\n"
            "var b: int8 = copy int8, -1\n"
            "var c: int16 = copy int16, -2\n"
            "var d: int32 = copy int32, 3\n"
            "var y: int64 = call Self.g, x\n"
            "_ = return y\n"
            "end\n"
            "public func g(x: int64) int64 then\n"
            "_ = return x\n"
            "end\n")) {
        CHECK(0);
        return;
    }
    text = emit_fixture(&fixture, &len, &status, error_message,
                        sizeof(error_message));
    free_fixture(&fixture);
    CHECK(text != NULL);
    if (text == NULL) {
        return;
    }
    cursor = strstr(text, "sub sp, sp, #");
    while (cursor != NULL) {
        unsigned value = 0;
        if (sscanf(cursor, "sub sp, sp, #%u", &value) == 1) {
            CHECK(value % 16u == 0);
            found++;
        }
        cursor = strstr(cursor + 1, "sub sp, sp, #");
    }
    CHECK(found > 0);
    free(text);
}

static void test_unsupported(void) {
    Fixture fixture;
    TcAArch64EmitStatus status = TC_AARCH64_EMIT_OK;
    char error_message[256];
    size_t len = 0;
    char *text;
    error_message[0] = '\0';
    if (!make_fixture(
            &fixture,
            "#program\nvar x: int32 = copy int32, 1\nunsafe then\n"
            "var p: ptr<int32> = addr int32, x\n"
            "var y: int32 = load int32, p\nend\n",
            "#lib\n")) {
        CHECK(0);
        return;
    }
    text = emit_fixture(&fixture, &len, &status, error_message,
                        sizeof(error_message));
    free_fixture(&fixture);
    free(text);
    CHECK(status == TC_AARCH64_EMIT_UNSUPPORTED);
    CHECK(strstr(error_message, "unsupported opcode") != NULL);
}

static void test_static_initializer(void) {
    Fixture fixture;
    TcAArch64EmitStatus status = TC_AARCH64_EMIT_INTERNAL;
    char error_message[256];
    size_t len = 0;
    char *text;
    if (!make_fixture(
            &fixture,
            "#program\nimport lib\n_ = writeln int32, lib.V\n",
            "#lib\npublic static var V: int32 = 0x12345678\n")) {
        CHECK(0);
        return;
    }
    text = emit_fixture(&fixture, &len, &status, error_message,
                        sizeof(error_message));
    free_fixture(&fixture);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(status == TC_AARCH64_EMIT_OK);
        CHECK(strstr(text, "__tc_static_region:") != NULL);
        CHECK(strstr(text, ".byte 0x78") != NULL);
        CHECK(strstr(text, ".byte 0x56") != NULL);
        CHECK(strstr(text, ".byte 0x34") != NULL);
        CHECK(strstr(text, ".byte 0x12") != NULL);
        free(text);
    }
}

static void test_deterministic(void) {
    Fixture fixture;
    TcAArch64EmitStatus status1 = TC_AARCH64_EMIT_INTERNAL;
    TcAArch64EmitStatus status2 = TC_AARCH64_EMIT_INTERNAL;
    char error_message[256];
    size_t len1 = 0;
    size_t len2 = 0;
    char *first;
    char *second;
    if (!make_fixture(
            &fixture,
            "#program\nvar a: int32 = copy int32, 1\n"
            "var b: int32 = add int32, a, 2\n_ = writeln int32, b\n",
            "#lib\n")) {
        CHECK(0);
        return;
    }
    first = emit_fixture(&fixture, &len1, &status1, error_message,
                         sizeof(error_message));
    second = emit_fixture(&fixture, &len2, &status2, error_message,
                          sizeof(error_message));
    free_fixture(&fixture);
    CHECK(first != NULL && second != NULL);
    CHECK(status1 == TC_AARCH64_EMIT_OK && status2 == TC_AARCH64_EMIT_OK);
    CHECK(len1 == len2);
    if (first != NULL && second != NULL && len1 == len2) {
        CHECK(memcmp(first, second, len1) == 0);
    }
    free(first);
    free(second);
}

int main(void) {
    test_lowering_shapes();
    test_frame_alignment();
    test_unsupported();
    test_static_initializer();
    test_deterministic();
    if (failures != 0) {
        printf("aot.unit: %d failure(s), %d checks\n", failures, checks);
        return 1;
    }
    printf("aot.unit: %d checks passed\n", checks);
    return 0;
}
