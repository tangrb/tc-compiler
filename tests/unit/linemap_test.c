#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>

#include "backend/common/debug_emit.h"
#include "temp_file.h"

#include "frontend/sem.h"
#include "ir/linemap.h"
#include "runtime/profile.h"

static int failures;
static int checks;

#define CHECK(condition)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    TcPlatformProfile profile;
    TcModule module;
    TcFunc func;
    TcInstr top_instrs[2];
    TcInstr func_instr;
    TcBlock top_blocks[2];
    TcBlock func_block;
    TcUnit units[2];
    TcIR ir;
} Fixture;

static void init_fixture(Fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    CHECK(tc_profile_init_aarch64_linux_user(&fixture->profile));
    fixture->module.id = 0u;
    fixture->module.path = "src/main.tc";
    fixture->top_instrs[0].span.file = "src/main.tc";
    fixture->top_instrs[0].span.line = 3u;
    fixture->top_instrs[0].span.column = 5u;
    fixture->top_instrs[1].span.file = "../li\"b\\x.tc";
    fixture->top_instrs[1].span.line = 7u;
    fixture->top_instrs[1].span.column = 2u;
    fixture->func_instr.span.file = "src/main.tc";
    fixture->func_instr.span.line = 11u;
    fixture->func_instr.span.column = 1u;
    fixture->top_blocks[0].id = 0u;
    fixture->top_blocks[0].instrs = fixture->top_instrs;
    fixture->top_blocks[0].n = 1u;
    fixture->top_blocks[1].id = 1u;
    fixture->top_blocks[1].instrs = &fixture->top_instrs[1];
    fixture->top_blocks[1].n = 1u;
    fixture->func_block.id = 0u;
    fixture->func_block.instrs = &fixture->func_instr;
    fixture->func_block.n = 1u;
    fixture->units[0].func_id = TC_UNIT_TOP_LEVEL;
    fixture->units[0].blocks = fixture->top_blocks;
    fixture->units[0].nblocks = 2u;
    fixture->units[0].entry = 0u;
    fixture->units[1].func_id = 0u;
    fixture->units[1].blocks = &fixture->func_block;
    fixture->units[1].nblocks = 1u;
    fixture->units[1].entry = 0u;
    fixture->func.id = 0u;
    fixture->func.module = 0u;
    fixture->func.name = "f";
    fixture->func.body = &fixture->units[1];
    fixture->ir.modules = &fixture->module;
    fixture->ir.nmodules = 1u;
    fixture->ir.funcs = &fixture->func;
    fixture->ir.nfuncs = 1u;
    fixture->ir.units = fixture->units;
    fixture->ir.nunits = 2u;
    fixture->ir.profile = &fixture->profile;
}

static void check_json(void) {
    Fixture fixture;
    TcLineMap *linemap = NULL;
    TcInstr unrelated;
    FILE *out = tc_test_tmpfile();
    char buffer[2048];
    size_t size;
    const char *expected =
        "{\"format\":\"tc-linemap\",\"version\":3,"
        "\"profile\":\"aarch64-linux-user\",\"source\":\"src/main.tc\","
        "\"entries\":["
        "{\"kind\":\"unit\",\"module\":\"src/main.tc\",\"func\":\"<top>\","
        "\"bb\":0,\"insn\":0,\"file\":\"src/main.tc\",\"line\":3,\"col\":5},"
        "{\"kind\":\"unit\",\"module\":\"src/main.tc\",\"func\":\"<top>\","
        "\"bb\":1,\"insn\":0,\"file\":\"../li\\\"b\\\\x.tc\",\"line\":7,"
        "\"col\":2,\"external\":true},"
        "{\"kind\":\"function\",\"module\":\"src/main.tc\",\"func\":\"f\","
        "\"bb\":0,\"insn\":0,\"file\":\"src/main.tc\",\"line\":11,\"col\":1}]}\n";

    init_fixture(&fixture);
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_OK);
    CHECK(linemap != NULL);
    CHECK(out != NULL);
    if (linemap == NULL || out == NULL) {
        if (out != NULL) {
            fclose(out);
        }
        tc_linemap_destroy(linemap);
        return;
    }
    CHECK(linemap->count == 3u);
    CHECK(linemap->file_count == 2u);
    CHECK(strcmp(linemap->files[0].path, "src/main.tc") == 0);
    CHECK(linemap->files[0].id == 1u);
    CHECK(strcmp(linemap->files[1].path, "../li\"b\\x.tc") == 0);
    CHECK(linemap->files[1].id == 2u);
    CHECK(tc_linemap_find_instr(linemap, &fixture.top_instrs[0]) ==
          &linemap->entries[0]);
    CHECK(tc_linemap_find_instr(linemap, &fixture.top_instrs[1]) ==
          &linemap->entries[1]);
    CHECK(tc_linemap_find_instr(linemap, &fixture.func_instr) ==
          &linemap->entries[2]);
    memset(&unrelated, 0, sizeof(unrelated));
    CHECK(tc_linemap_find_instr(linemap, &unrelated) == NULL);
    CHECK(tc_linemap_write_json(linemap, out) == TC_IMPL_OK);
    rewind(out);
    size = fread(buffer, 1u, sizeof(buffer) - 1u, out);
    buffer[size] = '\0';
    CHECK(strcmp(buffer, expected) == 0);
    CHECK(tc_linemap_write_json(linemap, NULL) == TC_IMPL_INTERNAL);
    fclose(out);
    tc_linemap_destroy(linemap);
}

static void check_debug_emission(void) {
    Fixture fixture;
    TcLineMap *linemap = NULL;
    FILE *out = tc_test_tmpfile();
    FILE *failing;
    TcInstr unrelated;
    char buffer[4096];
    size_t size;
    init_fixture(&fixture);
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_OK);
    CHECK(out != NULL);
    if (linemap == NULL || out == NULL) {
        if (out != NULL) {
            fclose(out);
        }
        tc_linemap_destroy(linemap);
        return;
    }
    CHECK(tc_debug_emit_files(out, linemap) == TC_IMPL_OK);
    CHECK(tc_debug_emit_begin(out) == TC_IMPL_OK);
    CHECK(tc_debug_emit_location(out, linemap, &fixture.top_instrs[0]) ==
          TC_IMPL_OK);
    CHECK(tc_debug_emit_location(out, linemap, &fixture.top_instrs[1]) ==
          TC_IMPL_OK);
    CHECK(tc_debug_emit_end(out, linemap, 8u) == TC_IMPL_OK);
    rewind(out);
    size = fread(buffer, 1u, sizeof(buffer) - 1u, out);
    buffer[size] = '\0';
    CHECK(strstr(buffer, ".file 1 \"src/main.tc\"") != NULL);
    CHECK(strstr(buffer, ".file 2 \"../li\\\"b\\\\x.tc\"") != NULL);
    CHECK(strstr(buffer, ".loc 1 3 5") != NULL);
    CHECK(strstr(buffer, "# source: ../li\"b\\x.tc:7") != NULL);
    CHECK(strstr(buffer, ".Ltc_debug_begin:") != NULL);
    CHECK(strstr(buffer, ".Ltc_debug_end:") != NULL);
    CHECK(strstr(buffer, ".section .debug_info,\"\",%progbits") != NULL);
    CHECK(strstr(buffer, ".long 32\n") != NULL);
    CHECK(strstr(buffer, ".long .debug_line\n") != NULL);
    CHECK(strstr(buffer, ".quad .Ltc_debug_begin\n") != NULL);
    fclose(out);

    out = tc_test_tmpfile();
    CHECK(out != NULL);
    if (out != NULL) {
        memset(&unrelated, 0, sizeof(unrelated));
        CHECK(tc_debug_emit_location(out, linemap, &unrelated) ==
              TC_IMPL_INTERNAL);
        CHECK(tc_debug_emit_end(out, NULL, 8u) == TC_IMPL_INTERNAL);
        CHECK(tc_debug_emit_end(out, linemap, 3u) == TC_IMPL_INTERNAL);
        fclose(out);
    }

    failing = fopen(".", "r");
    CHECK(failing != NULL);
    if (failing != NULL) {
        CHECK(tc_debug_emit_files(failing, linemap) == TC_IMPL_IO);
        clearerr(failing);
        CHECK(tc_debug_emit_location(failing, linemap,
                                     &fixture.top_instrs[0]) == TC_IMPL_IO);
        clearerr(failing);
        CHECK(tc_debug_emit_begin(failing) == TC_IMPL_IO);
        clearerr(failing);
        CHECK(tc_debug_emit_end(failing, linemap, 8u) == TC_IMPL_IO);
        fclose(failing);
    }
    tc_linemap_destroy(linemap);
}

static void check_rejections(void) {
    Fixture fixture;
    TcLineMap *linemap = (TcLineMap *)(uintptr_t)1u;

    init_fixture(&fixture);
    fixture.top_instrs[0].span.file = "";
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    init_fixture(&fixture);
    fixture.top_instrs[0].span.file = "/absolute/main.tc";
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    init_fixture(&fixture);
    fixture.top_instrs[0].span.line = 0u;
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    init_fixture(&fixture);
    fixture.units[1].func_id = 9u;
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    init_fixture(&fixture);
    fixture.top_blocks[1].id = fixture.top_blocks[0].id;
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    init_fixture(&fixture);
    fixture.func_block.instrs = fixture.top_instrs;
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_INTERNAL);
    CHECK(linemap == NULL);
    CHECK(tc_linemap_build(NULL, &linemap) == TC_IMPL_INTERNAL);
    CHECK(tc_linemap_build(&fixture.ir, NULL) == TC_IMPL_INTERNAL);
    init_fixture(&fixture);
    fixture.top_instrs[1].span.file = "./fallback.tc#1";
    CHECK(tc_linemap_build(&fixture.ir, &linemap) == TC_IMPL_OK);
    CHECK(linemap != NULL);
    if (linemap != NULL) {
        CHECK(linemap->entries[1].external);
        tc_linemap_destroy(linemap);
    }
}

int main(void) {
    check_json();
    check_debug_emission();
    check_rejections();
    if (failures != 0) {
        printf("linemap: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("linemap: %d checks passed\n", checks);
    return 0;
}
