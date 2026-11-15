/* TC 0.0.46 SEM unit tests: type identity, layout and module order.
 *
 * These are L1 checks of the private analysis interface.  They pin:
 *   - interned type identity and the independent isize/usize equivalence
 *     classes (standard §3.9),
 *   - struct layout: compact fields, @padding 0x00 bytes, no implicit
 *     alignment or tail padding (standard §3.7/§6.13/§13.3),
 *   - word-size-instantiated ptr/fnptr/isize/usize sizes (§13.2.1),
 *   - type table freeze after Analyze (design §2.2),
 *   - deterministic module_order for a diamond import (standard §4.11.1).
 *
 * The case ids are referenced by tests/traceability/coverage.generated.json.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frontend/sem.h"
#include "ir/structs.h"
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

static TcAnalysis *analyze_text(const char *text, unsigned width,
                                TcAnalysisStatus *status,
                                TcAnalysisDiag *diag) {
    char path[] = "/tmp/tc-sem-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *analysis;
    if (fd < 0) {
        printf("FAIL cannot create temp file\n");
        failures++;
        return NULL;
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        unlink(path);
        printf("FAIL cannot open temp file\n");
        failures++;
        return NULL;
    }
    if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
        fclose(file);
        unlink(path);
        printf("FAIL cannot write temp file\n");
        failures++;
        return NULL;
    }
    fclose(file);
    analysis = tc_analyze(path, width, NULL, 0, status, diag, &impl);
    unlink(path);
    if (analysis == NULL) {
        printf("FAIL analysis returned NULL (impl %d)\n", (int)impl);
        failures++;
    }
    return analysis;
}

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

static TcAnalysis *analyze_lib_text(const char *text, unsigned width,
                                    TcAnalysisStatus *status,
                                    TcAnalysisDiag *diag) {
    char dir[] = "/tmp/tc-sem-lib-XXXXXX";
    char lib_path[512];
    char root_path[512];
    static const char *root = "#program\nimport lib\n";
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *analysis;
    if (mkdtemp(dir) == NULL) {
        printf("FAIL cannot create temp dir\n");
        failures++;
        return NULL;
    }
    snprintf(lib_path, sizeof(lib_path), "%s/lib.tc", dir);
    snprintf(root_path, sizeof(root_path), "%s/root.tc", dir);
    if (!write_text_file(lib_path, text) || !write_text_file(root_path, root)) {
        printf("FAIL cannot write temp lib\n");
        unlink(lib_path);
        unlink(root_path);
        rmdir(dir);
        failures++;
        return NULL;
    }
    analysis = tc_analyze(root_path, width, NULL, 0, status, diag, &impl);
    unlink(lib_path);
    unlink(root_path);
    rmdir(dir);
    if (analysis == NULL) {
        printf("FAIL lib analysis returned NULL (impl %d)\n", (int)impl);
        failures++;
    }
    return analysis;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static void test_layout(void) {
    static const char *source =
        "#program\n"
        "struct Layout then\n"
        "    var a: int8\n"
        "    var b: int32 @padding(3)\n"
        "    var c: float64\n"
        "end\n"
        "struct Ptrs then\n"
        "    var a: isize\n"
        "    var b: usize\n"
        "    var c: ptr<int32>\n"
        "    var d: fnptr<void>\n"
        "end\n";
    unsigned widths[2] = {32, 64};
    size_t i;
    for (i = 0; i < 2; i++) {
        TcAnalysisStatus status;
        TcAnalysisDiag diag;
        TcAnalysis *an = analyze_text(source, widths[i], &status, &diag);
        const TcStructDef *layout;
        const TcStructDef *ptrs;
        if (an == NULL) {
            continue;
        }
        CHECK(status == TC_ANALYSIS_OK);
        CHECK(tc_analysis_type_table_frozen(an));
        layout = tc_analysis_find_struct(an, NULL, "Layout");
        ptrs = tc_analysis_find_struct(an, NULL, "Ptrs");
        CHECK(layout != NULL);
        CHECK(ptrs != NULL);
        if (layout != NULL) {
            CHECK_EQ(layout->size, 16);
            CHECK_EQ(layout->nfields, 3);
            CHECK_EQ(layout->fields[0].offset, 0);
            CHECK_EQ(layout->fields[1].offset, 1);
            CHECK_EQ(layout->fields[1].padding, 3);
            CHECK_EQ(layout->fields[2].offset, 8);
            CHECK_EQ(layout->fields[2].padding, 0);
        }
        if (ptrs != NULL) {
            CHECK_EQ(ptrs->size, widths[i] / 8u * 4u);
            CHECK_EQ(ptrs->fields[0].offset, 0);
            CHECK_EQ(ptrs->fields[1].offset, widths[i] / 8u);
            CHECK_EQ(ptrs->fields[2].offset, widths[i] / 8u * 2u);
            CHECK_EQ(ptrs->fields[3].offset, widths[i] / 8u * 3u);
        }
        tc_analysis_destroy(an);
    }
}

static void test_type_identity(void) {
    static const char *source =
        "#program\n"
        "struct A then\n"
        "    var a: ptr<int32>\n"
        "end\n"
        "struct B then\n"
        "    var b: ptr<int32>\n"
        "end\n"
        "struct C then\n"
        "    var c: int64\n"
        "end\n"
        "struct D then\n"
        "    var d: isize\n"
        "end\n"
        "struct E then\n"
        "    var e: fnptr<int32, int32, uint32>\n"
        "end\n"
        "struct F then\n"
        "    var f: fnptr<int32, int32, int64>\n"
        "end\n"
        "struct G then\n"
        "    var g: A\n"
        "end\n"
        "struct H then\n"
        "    var h: B\n"
        "end\n";
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an = analyze_text(source, 64, &status, &diag);
    const TcStructDef *a;
    const TcStructDef *b;
    const TcStructDef *c;
    const TcStructDef *d;
    const TcStructDef *e;
    const TcStructDef *f;
    const TcStructDef *g;
    const TcStructDef *h;
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    a = tc_analysis_find_struct(an, NULL, "A");
    b = tc_analysis_find_struct(an, NULL, "B");
    c = tc_analysis_find_struct(an, NULL, "C");
    d = tc_analysis_find_struct(an, NULL, "D");
    e = tc_analysis_find_struct(an, NULL, "E");
    f = tc_analysis_find_struct(an, NULL, "F");
    g = tc_analysis_find_struct(an, NULL, "G");
    h = tc_analysis_find_struct(an, NULL, "H");
    CHECK(a != NULL && b != NULL && c != NULL && d != NULL && e != NULL &&
          f != NULL && g != NULL && h != NULL);
    if (a != NULL && b != NULL && c != NULL && d != NULL && e != NULL &&
        f != NULL && g != NULL && h != NULL) {
        /* Structs are nominal: A and B have identical fields but are distinct
         * types, as their G/H value fields show. */
        CHECK(!tc_type_equal(g->fields[0].type, h->fields[0].type));
        /* ptr<T> is interned by pointee identity. */
        CHECK(tc_type_equal(a->fields[0].type, b->fields[0].type));
        CHECK(tc_type_equal(a->fields[0].type->ptr.pointee,
                            b->fields[0].type->ptr.pointee));
        /* isize is not int64 even at W=64 (standard §3.9). */
        CHECK(!tc_type_equal(c->fields[0].type, d->fields[0].type));
        CHECK_EQ(tc_type_bits(c->fields[0].type, 64), 64);
        CHECK_EQ(tc_type_bits(d->fields[0].type, 64), 64);
        /* fnptr equivalence is per-signature. */
        CHECK(!tc_type_equal(e->fields[0].type, f->fields[0].type));
    }
    tc_analysis_destroy(an);
}

static void test_annotation_metadata(void) {
    static const char *source =
        "#lib\n"
        "public struct S then\n"
        "    var x: int8 @padding(4294967296)\n"
        "end\n"
        "public struct P then\n"
        "    var v: int32\n"
        "end\n"
        "public const A: int32 = 8\n"
        "public const C: int32 = add int32, 7, 1\n"
        "public const L: int32 = 1\n"
        "public const H: int32 = 10\n"
        "public const BIG: uint64 = 4294967296\n"
        "public const F: float64 = add float64, 7.5, 0.5\n"
        "public const AF: int32 = cast int32, F\n"
        "public const PV: P = make P, v: 8\n"
        "public const PF: int32 = PV.v\n"
        "public const B: bool = eq int32, 8, 8\n"
        "public const BF: int32 = cast int32, B\n"
        "public @fast func a(@align(Self.A) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func c(@align(Self.C) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func b(@align(Self.BIG) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func af(@align(Self.AF) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func pf(@align(Self.PF) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func bf(@align(Self.BF) p: ptr<int32>) void then\n"
        "end\n"
        "public @fast func r(@range(Self.L, Self.H) n: int32) void then\n"
        "end\n"
        "public @fast func w(@align(9223372036854775808) p: ptr<int32>) void then\n"
        "end\n";
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an = analyze_lib_text(source, 64, &status, &diag);
    const TcFuncSig *a;
    const TcFuncSig *c;
    const TcFuncSig *b;
    const TcFuncSig *af;
    const TcFuncSig *pf;
    const TcFuncSig *bf;
    const TcFuncSig *r;
    const TcFuncSig *w;
    const TcStructDef *s;
    if (an == NULL) {
        return;
    }
    if (status != TC_ANALYSIS_OK) {
        printf("annotation metadata diagnostic code=%d line=%u column=%u\n",
               (int)diag.code, diag.span.line, diag.span.column);
    }
    CHECK(status == TC_ANALYSIS_OK);
    a = tc_analysis_find_func(an, NULL, "a");
    c = tc_analysis_find_func(an, NULL, "c");
    b = tc_analysis_find_func(an, NULL, "b");
    af = tc_analysis_find_func(an, NULL, "af");
    pf = tc_analysis_find_func(an, NULL, "pf");
    bf = tc_analysis_find_func(an, NULL, "bf");
    r = tc_analysis_find_func(an, NULL, "r");
    w = tc_analysis_find_func(an, NULL, "w");
    s = tc_analysis_find_struct(an, NULL, "S");
    CHECK(a != NULL && a->nparams == 1);
    CHECK(c != NULL && c->nparams == 1);
    CHECK(b != NULL && b->nparams == 1);
    CHECK(af != NULL && af->nparams == 1);
    CHECK(pf != NULL && pf->nparams == 1);
    CHECK(bf != NULL && bf->nparams == 1);
    CHECK(r != NULL && r->nparams == 1);
    CHECK(w != NULL && w->nparams == 1);
    CHECK(s != NULL && s->nfields == 1);
    if (a != NULL && a->nparams == 1) {
        CHECK((a->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK_EQ(a->params[0].align, 8);
    }
    if (c != NULL && c->nparams == 1) {
        CHECK((c->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK_EQ(c->params[0].align, 8);
    }
    if (b != NULL && b->nparams == 1) {
        CHECK((b->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK(b->params[0].align == UINT64_C(4294967296));
    }
    if (af != NULL && af->nparams == 1) {
        CHECK((af->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK_EQ(af->params[0].align, 8);
    }
    if (pf != NULL && pf->nparams == 1) {
        CHECK((pf->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK_EQ(pf->params[0].align, 8);
    }
    if (bf != NULL && bf->nparams == 1) {
        CHECK((bf->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK_EQ(bf->params[0].align, 1);
    }
    if (r != NULL && r->nparams == 1) {
        CHECK((r->params[0].flags & TC_PARAM_RANGE) != 0);
        CHECK_EQ(r->params[0].lo, 1);
        CHECK_EQ(r->params[0].hi, 10);
    }
    if (w != NULL && w->nparams == 1) {
        CHECK((w->params[0].flags & TC_PARAM_ALIGN) != 0);
        CHECK(w->params[0].align == (UINT64_C(1) << 63));
    }
    if (s != NULL && s->nfields == 1) {
        CHECK(s->fields[0].padding == UINT64_C(4294967296));
        CHECK(s->size == UINT64_C(4294967297));
    }
    tc_analysis_destroy(an);
}

static void test_conversion_flip(void) {
    static const char *source =
        "#program\n"
        "var s: isize = copy isize, 0\n"
        "var x: int32 = bitcast int32, s\n";
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an = analyze_text(source, 64, &status, &diag);
    if (an != NULL) {
        CHECK(status == TC_ANALYSIS_DIAGNOSTIC);
        CHECK_EQ(diag.code, TC_CE_BITCAST_WIDTH);
        CHECK_EQ(diag.stage, TC_STAGE_SEM);
        tc_analysis_destroy(an);
    }
    an = analyze_text(source, 32, &status, &diag);
    if (an != NULL) {
        CHECK(status == TC_ANALYSIS_OK);
        tc_analysis_destroy(an);
    }
}

static void test_module_order(void) {
    const char *root =
        TC_TEST_SOURCE_DIR "/conformance/static/accept_s3/tc-tool-sem-0006/root.tc";
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *an = tc_analyze(root, 64, NULL, 0, &status, &diag, &impl);
    const char *expected[] = {"root.tc", "a.tc", "c.tc", "b.tc"};
    size_t i;
    if (an == NULL) {
        printf("FAIL module_order analysis returned NULL\n");
        failures++;
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    CHECK_EQ(tc_analysis_module_count(an), 4);
    for (i = 0; i < 4 && i < tc_analysis_module_count(an); i++) {
        CHECK(strcmp(base_name(tc_analysis_module_path(an, i)),
                    expected[i]) == 0);
    }
    tc_analysis_destroy(an);
}

static void test_module_instances(void) {
    const char *root =
        TC_TEST_SOURCE_DIR
        "/conformance/static/accept_s3/tc-tool-sem-0024/root.tc";
    const char *roots[2] = {
        TC_TEST_SOURCE_DIR
        "/conformance/static/accept_s3/tc-tool-sem-0024/left",
        TC_TEST_SOURCE_DIR
        "/conformance/static/accept_s3/tc-tool-sem-0024/right",
    };
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *an = tc_analyze(root, 64, roots, 2, &status, &diag, &impl);
    const char *paths[5];
    size_t i;
    if (an == NULL) {
        printf("FAIL module_instances analysis returned NULL\n");
        failures++;
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    CHECK_EQ(tc_analysis_module_count(an), 5);
    for (i = 0; i < 5 && i < tc_analysis_module_count(an); i++) {
        paths[i] = tc_analysis_module_path(an, i);
    }
    CHECK(strstr(paths[1], "/left/a.tc") != NULL);
    CHECK(strstr(paths[2], "/left/shared.tc") != NULL);
    CHECK(strstr(paths[3], "/right/b.tc") != NULL);
    CHECK(strstr(paths[4], "/right/shared.tc") != NULL);
    /* Same module name at two resolved paths is two instances. */
    CHECK(strcmp(paths[2], paths[4]) != 0);
    tc_analysis_destroy(an);
}

int main(void) {
    test_layout();
    test_type_identity();
    test_conversion_flip();
    test_annotation_metadata();
    test_module_order();
    test_module_instances();
    if (failures != 0) {
        printf("sem.unit: %d failure(s), %d checks\n", failures, checks);
        return 1;
    }
    printf("sem.unit: %d checks passed\n", checks);
    return 0;
}
