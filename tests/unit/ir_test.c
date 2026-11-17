/* TC 0.0.46 IR finalization unit tests (design §3, §12.11-§12.13).
 *
 * These are L1 checks of the private analysis/IR interface.  They pin:
 *   - module/struct/function/const/object identity and module instance ids,
 *   - frozen type table binding and per-profile layout in TcObject,
 *   - flat IR operands: vreg/field index paths/const/label/func/struct,
 *     no source names, retained 1-based spans, named call/make arguments,
 *   - explicit CFG edges (br_if keeps both edges; unreachable blocks),
 *   - const inlining (no const object; aggregate const -> make at the use),
 *   - static and frame object tables, static initializer images,
 *   - the deterministic textual dump.
 *
 * Case ids are referenced by the traceability matrix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frontend/sem.h"
#include "ir/print.h"
#include "ir/structs.h"

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

typedef struct {
    char dir[64];
    char root[128];
    char lib[128];
} Fixture;

static int make_fixture(Fixture *fixture, const char *root_text,
                        const char *lib_text) {
    snprintf(fixture->dir, sizeof(fixture->dir), "/tmp/tc-ir-XXXXXX");
    if (mkdtemp(fixture->dir) == NULL) {
        return 0;
    }
    snprintf(fixture->root, sizeof(fixture->root), "%s/root.tc",
             fixture->dir);
    snprintf(fixture->lib, sizeof(fixture->lib), "%s/lib.tc", fixture->dir);
    if (!write_text_file(fixture->root, root_text) ||
        !write_text_file(fixture->lib, lib_text)) {
        return 0;
    }
    return 1;
}

static void free_fixture(Fixture *fixture) {
    unlink(fixture->root);
    unlink(fixture->lib);
    rmdir(fixture->dir);
}

static TcAnalysis *analyze_fixture(const Fixture *fixture, unsigned width,
                                   TcAnalysisStatus *status,
                                   TcAnalysisDiag *diag) {
    TcImplError impl = TC_IMPL_OK;
    return tc_analyze(fixture->root, width, NULL, 0, status, diag, &impl);
}

static const TcInstr *instr_at(const TcUnit *unit, size_t block, size_t index) {
    if (unit == NULL || block >= unit->nblocks ||
        index >= unit->blocks[block].n) {
        return NULL;
    }
    return &unit->blocks[block].instrs[index];
}

static const TcUnit *find_unit(const TcIR *ir, uint32_t func_id) {
    size_t i;
    for (i = 0; i < ir->nunits; i++) {
        if (ir->units[i].func_id == func_id) {
            return &ir->units[i];
        }
    }
    return NULL;
}

/* Structural invariants over one unit. */
static void validate_unit(const TcIR *ir, const TcUnit *unit) {
    size_t bi;
    for (bi = 0; bi < unit->nblocks; bi++) {
        const TcBlock *block = &unit->blocks[bi];
        size_t ii;
        CHECK(block->id == bi);
        for (ii = 0; ii < block->n; ii++) {
            const TcInstr *instr = &block->instrs[ii];
            size_t ai;
            CHECK(instr->span.line >= 1);
            CHECK(instr->span.column >= 1);
            CHECK(tc_instr_lhs_is_valid(instr));
            for (ai = 0; ai < instr->nargs; ai++) {
                const TcOperand *op = &instr->args[ai].value;
                if (op->kind == TC_OP_VREG) {
                    CHECK(op->vreg < unit->nvregs);
                } else if (op->kind == TC_OP_FIELD) {
                    const TcType *type;
                    size_t fi;
                    CHECK(op->field.base < unit->nvregs);
                    type = unit->vreg_types[op->field.base];
                    for (fi = 0; fi < op->field.nfields; fi++) {
                        TcStructDef *def =
                            type != NULL && type->tag == TC_TY_STRUCT &&
                                    type->struct_.struct_id < ir->nstructs
                                ? &ir->structs[type->struct_.struct_id]
                                : NULL;
                        CHECK(def != NULL);
                        if (def == NULL ||
                            op->field.fields[fi] >= def->nfields) {
                            break;
                        }
                        type = def->fields[op->field.fields[fi]].type;
                    }
                } else if (op->kind == TC_OP_CONST) {
                    CHECK(op->konst.type != NULL);
                    CHECK(tc_type_is_scalar(op->konst.type));
                } else if (op->kind == TC_OP_LABEL) {
                    CHECK(op->label < unit->nblocks);
                } else if (op->kind == TC_OP_FUNC) {
                    CHECK(op->func < ir->nfuncs);
                } else {
                    CHECK(op->kind == TC_OP_STRUCT ||
                          op->kind == TC_OP_DISCARD);
                }
            }
            if (instr->op != TC_CALL && instr->op != TC_CALL_INDIRECT &&
                instr->op != TC_MAKE) {
                for (ai = 0; ai < instr->nargs; ai++) {
                    CHECK(instr->args[ai].name == NULL);
                }
            }
            for (ai = 0; ai < instr->ntargets; ai++) {
                CHECK(instr->switch_targets[ai] < unit->nblocks);
            }
        }
        for (ii = 0; ii < block->nsucc; ii++) {
            uint32_t target = block->succs[ii];
            size_t pi;
            int found = 0;
            CHECK(target < unit->nblocks);
            if (target >= unit->nblocks) {
                continue;
            }
            for (pi = 0; pi < unit->blocks[target].npred; pi++) {
                if (unit->blocks[target].preds[pi] == block->id) {
                    found = 1;
                }
            }
            CHECK(found);
        }
    }
}

static void test_ir_shape(void) {
    static const char *root =
        "#program\n"
        "import lib\n"
        "var g: int32 = copy int32, 3\n"
        "var s: lib.S = make lib.S, x: 4\n"
        "label L:\n"
        "_ = write int32, g\n"
        "_ = br_if true, L\n"
        "_ = write int32, s.x\n"
        "_ = write int32, lib.S2.x\n"
        "_ = write int32, lib.counter\n";
    static const char *lib =
        "#lib\n"
        "public struct S then\n"
        "    var x: int32\n"
        "end\n"
        "public const K: int32 = add int32, 1, 2\n"
        "public const S2: S = make S, x: 7\n"
        "public static var counter: int32 = 5\n"
        "public func f(a: int32) int32 then\n"
        "    var y: int32 = copy int32, 1\n"
        "    _ = write int32, y\n"
        "    _ = return Self.K\n"
        "end\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    const TcUnit *top;
    const TcUnit *fun;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    CHECK(tc_analysis_type_table_frozen(an));
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    CHECK(ir->types == tc_analysis_type_table(an));
    CHECK(ir->profile != NULL && ir->profile->addr_width == 64);

    /* Modules in module_order; instance key is the resolved path. */
    CHECK_EQ(ir->nmodules, 2);
    CHECK(strstr(ir->modules[0].path, "root.tc") != NULL);
    CHECK(strstr(ir->modules[1].path, "lib.tc") != NULL);
    CHECK_EQ(ir->modules[0].id, 0);
    CHECK_EQ(ir->modules[1].id, 1);

    /* Struct definitions with the frozen per-profile layout. */
    CHECK_EQ(ir->nstructs, 1);
    CHECK_EQ(ir->structs[0].module, 1);
    CHECK(strcmp(ir->structs[0].name, "S") == 0);
    CHECK_EQ(ir->structs[0].nfields, 1);
    CHECK(strcmp(ir->structs[0].fields[0].name, "x") == 0);
    CHECK_EQ(ir->structs[0].fields[0].offset, 0);
    CHECK_EQ(ir->structs[0].size, 4);

    /* Consts: successful module consts only, static var is an object. */
    CHECK_EQ(ir->nconsts, 2);
    CHECK_EQ(ir->consts[0].module, 1);
    CHECK(strcmp(ir->consts[0].name, "K") == 0);
    CHECK_EQ(ir->consts[0].bits, 3);
    CHECK(ir->consts[0].init == NULL);
    CHECK(strcmp(ir->consts[1].name, "S2") == 0);
    CHECK(ir->consts[1].init != NULL);
    CHECK_EQ(ir->consts[1].init->size, 4);
    CHECK_EQ(ir->consts[1].init->bytes[0], 7);
    CHECK_EQ(ir->consts[1].init->bytes[1], 0);

    /* Static objects first (program vars then lib static var), then frames. */
    CHECK_EQ(ir->nobjects, 5);
    CHECK_EQ(ir->objects[0].module, 0);
    CHECK_EQ(ir->objects[0].region, TC_REG_STATIC);
    CHECK_EQ(ir->objects[0].offset, 0);
    CHECK_EQ(ir->objects[0].size, 4);
    CHECK_EQ(ir->objects[1].offset, 8);
    CHECK_EQ(ir->objects[1].size, 4);
    CHECK_EQ(ir->objects[2].module, 1);
    CHECK_EQ(ir->objects[2].region, TC_REG_STATIC);
    CHECK_EQ(ir->objects[2].offset, 16);
    CHECK_EQ(ir->objects[2].lifetime, TC_LIFE_PROGRAM);
    CHECK(ir->objects[2].writable);
    CHECK(ir->objects[2].init != NULL);
    CHECK_EQ(ir->objects[2].init->bytes[0], 5);
    CHECK_EQ(ir->objects[3].region, TC_REG_FRAME);
    CHECK_EQ(ir->objects[3].lifetime, TC_LIFE_FRAME);
    CHECK_EQ(ir->objects[3].writable, 0); /* parameter */
    CHECK_EQ(ir->objects[4].writable, 1); /* local var */

    /* Functions carry their module instance, signature and body unit. */
    CHECK_EQ(ir->nfuncs, 1);
    CHECK_EQ(ir->funcs[0].module, 1);
    CHECK(strcmp(ir->funcs[0].name, "f") == 0);
    CHECK_EQ(ir->funcs[0].sig.nparams, 1);
    CHECK(tc_type_equal(ir->funcs[0].sig.ret,
                        tc_type_scalar((TcTypeTable *)ir->types, TC_TY_I32)));
    CHECK(ir->funcs[0].body != NULL);
    CHECK_EQ(ir->funcs[0].body->func_id, ir->funcs[0].id);

    CHECK_EQ(ir->nunits, 2);
    top = find_unit(ir, TC_UNIT_TOP_LEVEL);
    fun = find_unit(ir, ir->funcs[0].id);
    CHECK(top != NULL && fun != NULL);
    if (top == NULL || fun == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    CHECK_EQ(top->nvregs, 3);
    CHECK_EQ(top->nvreg_objects, 3);
    CHECK(ir->objects[top->vreg_objects[2]].id == 2);
    validate_unit(ir, top);
    validate_unit(ir, fun);

    /* CFG: br_if keeps the true target and the false fallthrough. */
    CHECK_EQ(top->nblocks, 3);
    CHECK_EQ(top->blocks[0].nsucc, 1);
    CHECK_EQ(top->blocks[0].succs[0], 1);
    CHECK_EQ(top->blocks[1].nsucc, 2);
    CHECK_EQ(top->blocks[1].succs[0], 1);
    CHECK_EQ(top->blocks[1].succs[1], 2);
    CHECK_EQ(top->blocks[1].npred, 2);
    {
        const TcInstr *br_if = instr_at(top, 1, 1);
        CHECK(br_if != NULL && br_if->op == TC_BR_IF);
        if (br_if != NULL) {
            CHECK_EQ(br_if->nargs, 2);
            CHECK(br_if->args[0].value.kind == TC_OP_CONST);
            CHECK(br_if->args[0].value.konst.bits == 1);
            CHECK(br_if->args[1].value.kind == TC_OP_LABEL);
            CHECK_EQ(br_if->args[1].value.label, 1);
        }
    }
    {
        const TcInstr *field_write = instr_at(top, 2, 0);
        CHECK(field_write != NULL && field_write->op == TC_WRITE);
        if (field_write != NULL) {
            CHECK(field_write->args[0].value.kind == TC_OP_FIELD);
            CHECK_EQ(field_write->args[0].value.field.base, 1);
            CHECK_EQ(field_write->args[0].value.field.nfields, 1);
            CHECK_EQ(field_write->args[0].value.field.fields[0], 0);
        }
    }
    {
        const TcInstr *const_write = instr_at(top, 2, 1);
        CHECK(const_write != NULL && const_write->op == TC_WRITE);
        if (const_write != NULL) {
            CHECK(const_write->args[0].value.kind == TC_OP_CONST);
            CHECK(const_write->args[0].value.konst.bits == 7);
        }
    }
    {
        const TcInstr *make = instr_at(top, 0, 1);
        CHECK(make != NULL && make->op == TC_MAKE);
        if (make != NULL) {
            CHECK(make->args[0].name != NULL);
            CHECK(strcmp(make->args[0].name, "x") == 0);
            CHECK(make->args[0].value.kind == TC_OP_CONST);
            CHECK(make->args[0].value.konst.bits == 4);
        }
    }
    {
        const TcInstr *ret = instr_at(fun, 0, 2);
        CHECK(ret != NULL && ret->op == TC_RETURN);
        if (ret != NULL) {
            CHECK(ret->args[0].value.kind == TC_OP_CONST);
            CHECK(ret->args[0].value.konst.bits == 3);
        }
    }
    tc_analysis_destroy(an);
}

static void test_ir_control_flow(void) {
    static const char *root =
        "#program\n"
        "_ = switch 1, L0, L1, LD\n"
        "label L0:\n"
        "_ = write int32, 10\n"
        "_ = br LD\n"
        "label L1:\n"
        "_ = write int32, 11\n"
        "label LD:\n"
        "_ = trap\n"
        "label DEAD:\n"
        "_ = write int32, 99\n";
    static const char *lib = "#lib\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    const TcUnit *top;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    top = find_unit(ir, TC_UNIT_TOP_LEVEL);
    CHECK(top != NULL);
    if (top == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    validate_unit(ir, top);
    CHECK_EQ(top->nblocks, 5);
    {
        const TcInstr *sw = instr_at(top, 0, 0);
        CHECK(sw != NULL && sw->op == TC_SWITCH);
        if (sw != NULL) {
            CHECK_EQ(sw->ntargets, 3);
            CHECK_EQ(sw->switch_targets[0], 1);
            CHECK_EQ(sw->switch_targets[1], 2);
            CHECK_EQ(sw->switch_targets[2], 3);
            CHECK_EQ(sw->nargs, 1);
            CHECK(sw->args[0].value.kind == TC_OP_CONST);
            CHECK_EQ(sw->args[0].value.konst.type->tag, TC_TY_I64);
        }
        CHECK_EQ(top->blocks[0].nsucc, 3);
    }
    CHECK_EQ(top->blocks[1].nsucc, 1);
    CHECK_EQ(top->blocks[1].succs[0], 3);
    CHECK_EQ(top->blocks[3].n, 1);
    CHECK_EQ(top->blocks[3].instrs[0].op, TC_TRAP);
    CHECK_EQ(top->blocks[3].nsucc, 0);
    /* Unreachable code is retained but has no predecessors. */
    CHECK_EQ(top->blocks[4].npred, 0);
    CHECK_EQ(top->blocks[4].n, 1);
    CHECK_EQ(top->blocks[4].instrs[0].op, TC_WRITE);
    tc_analysis_destroy(an);
}

/* A switch's successor list is unbounded: more than eight targets must still
 * produce a complete, unique CFG. */
static void test_ir_switch_successor_capacity(void) {
    static const char *root =
        "#program\n"
        "var k: int32 = copy int32, 5\n"
        "_ = switch k, L0, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, LD\n"
        "label L0:\n_ = br FIN\n"
        "label L1:\n_ = br FIN\n"
        "label L2:\n_ = br FIN\n"
        "label L3:\n_ = br FIN\n"
        "label L4:\n_ = br FIN\n"
        "label L5:\n_ = br FIN\n"
        "label L6:\n_ = br FIN\n"
        "label L7:\n_ = br FIN\n"
        "label L8:\n_ = br FIN\n"
        "label L9:\n_ = br FIN\n"
        "label L10:\n_ = br FIN\n"
        "label LD:\n_ = br FIN\n"
        "label FIN:\n_ = writeln int32, 0\n";
    static const char *lib = "#lib\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    const TcUnit *top;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    top = find_unit(ir, TC_UNIT_TOP_LEVEL);
    CHECK(top != NULL);
    if (top != NULL) {
        const TcInstr *sw = NULL;
        size_t switch_block = 0;
        size_t bi;
        size_t i;
        for (bi = 0; bi < top->nblocks && sw == NULL; bi++) {
            for (i = 0; i < top->blocks[bi].n; i++) {
                if (top->blocks[bi].instrs[i].op == TC_SWITCH) {
                    sw = &top->blocks[bi].instrs[i];
                    switch_block = bi;
                    break;
                }
            }
        }
        CHECK(sw != NULL);
        if (sw != NULL) {
            CHECK_EQ(sw->ntargets, 12);
            CHECK_EQ(top->blocks[switch_block].nsucc, 12);
            for (i = 0; i < top->blocks[switch_block].nsucc; i++) {
                size_t j;
                for (j = i + 1u;
                     j < top->blocks[switch_block].nsucc; j++) {
                    CHECK(top->blocks[switch_block].succs[i] !=
                          top->blocks[switch_block].succs[j]);
                }
            }
        }
        validate_unit(ir, top);
    }
    tc_analysis_destroy(an);
}

static void test_ir_aggregate_const(void) {
    static const char *root =
        "#program\n"
        "import lib\n"
        "var c: lib.Inner = copy lib.Inner, lib.OI\n"
        "var o: lib.Outer = copy lib.Outer, lib.O\n";
    static const char *lib =
        "#lib\n"
        "public struct Inner then\n"
        "    var q: int32\n"
        "end\n"
        "public struct Outer then\n"
        "    var i: Inner\n"
        "    var s: int8 @padding(2)\n"
        "end\n"
        "public const OI: Inner = make Inner, q: 5\n"
        "public const O: Outer = make Outer, i: Self.OI, s: 3\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    const TcUnit *top;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    CHECK_EQ(ir->nconsts, 2);
    CHECK(ir->consts[1].init != NULL);
    CHECK_EQ(ir->consts[1].init->size, 7);
    CHECK_EQ(ir->consts[1].init->bytes[0], 5);
    CHECK_EQ(ir->consts[1].init->bytes[4], 3);
    CHECK_EQ(ir->consts[1].init->bytes[5], 0);
    top = find_unit(ir, TC_UNIT_TOP_LEVEL);
    CHECK(top != NULL);
    if (top == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    validate_unit(ir, top);
    /* Const has no object: two program vars only. */
    CHECK_EQ(ir->nobjects, 2);
    CHECK_EQ(top->nblocks, 1);
    /* copy Inner, OI -> make then copy. */
    CHECK_EQ(top->blocks[0].n, 5);
    {
        const TcInstr *make = instr_at(top, 0, 0);
        const TcInstr *copy = instr_at(top, 0, 1);
        CHECK(make != NULL && make->op == TC_MAKE);
        CHECK(copy != NULL && copy->op == TC_COPY);
        if (make != NULL && copy != NULL) {
            CHECK_EQ(make->type->tag, TC_TY_STRUCT);
            CHECK_EQ(make->nargs, 1);
            CHECK(make->args[0].value.kind == TC_OP_CONST);
            CHECK_EQ(make->args[0].value.konst.bits, 5);
            CHECK(copy->args[0].value.kind == TC_OP_VREG);
            CHECK_EQ(copy->args[0].value.vreg, make->dst.vreg);
        }
    }
    {
        const TcInstr *nested = instr_at(top, 0, 2);
        const TcInstr *outer = instr_at(top, 0, 3);
        const TcInstr *copy = instr_at(top, 0, 4);
        CHECK(nested != NULL && nested->op == TC_MAKE);
        CHECK(outer != NULL && outer->op == TC_MAKE);
        CHECK(copy != NULL && copy->op == TC_COPY);
        if (nested != NULL && outer != NULL && copy != NULL) {
            CHECK_EQ(outer->nargs, 2);
            CHECK(outer->args[0].value.kind == TC_OP_VREG);
            CHECK_EQ(outer->args[0].value.vreg, nested->dst.vreg);
            CHECK_EQ(outer->args[1].value.kind, TC_OP_CONST);
            CHECK_EQ(copy->args[0].value.vreg, outer->dst.vreg);
        }
    }
    {
        const TcInstr *copy_o = instr_at(top, 0, 4);
        CHECK(copy_o != NULL && copy_o->op == TC_COPY);
        if (copy_o != NULL) {
            CHECK(copy_o->dst.kind == TC_OP_VREG);
        }
    }
    tc_analysis_destroy(an);
}

static void test_ir_word_sizes(void) {
    static const char *root =
        "#program\n"
        "import lib\n"
        "var p: lib.P = make lib.P, n: 1, f: nullptr\n";
    static const char *lib =
        "#lib\n"
        "public struct P then\n"
        "    var n: usize\n"
        "    var f: fnptr<int32>\n"
        "end\n"
        "public const NI: usize = sizeof isize\n"
        "public const NP: usize = sizeof ptr<int8>\n"
        "public const NF: usize = sizeof fnptr<int32>\n"
        "public static var sp: ptr<int32> = nullptr\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an32;
    TcAnalysis *an64;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an32 = analyze_fixture(&fixture, 32, &status, &diag);
    CHECK(an32 != NULL && status == TC_ANALYSIS_OK);
    if (an32 != NULL && status == TC_ANALYSIS_OK) {
        const TcIR *ir = tc_analysis_ir(an32);
        CHECK(ir != NULL);
        if (ir != NULL) {
            size_t i;
            CHECK(ir->profile != NULL && ir->profile->addr_width == 32);
            CHECK_EQ(ir->consts[0].bits, 4);
            CHECK_EQ(ir->consts[1].bits, 4);
            CHECK_EQ(ir->consts[2].bits, 4);
            /* struct P: usize(4) + fnptr(4) */
            for (i = 0; i < ir->nstructs; i++) {
                if (strcmp(ir->structs[i].name, "P") == 0) {
                    CHECK_EQ(ir->structs[i].size, 8);
                }
            }
            CHECK(ir->nobjects >= 2);
            CHECK_EQ(ir->objects[0].size, 8); /* program var p */
            CHECK_EQ(ir->objects[1].offset, 8); /* static var sp aligned */
            CHECK_EQ(ir->objects[1].size, 4);
        }
        tc_analysis_destroy(an32);
    }
    an64 = analyze_fixture(&fixture, 64, &status, &diag);
    CHECK(an64 != NULL && status == TC_ANALYSIS_OK);
    if (an64 != NULL && status == TC_ANALYSIS_OK) {
        const TcIR *ir = tc_analysis_ir(an64);
        CHECK(ir != NULL);
        if (ir != NULL) {
            size_t i;
            CHECK(ir->profile != NULL && ir->profile->addr_width == 64);
            CHECK_EQ(ir->consts[0].bits, 8);
            CHECK_EQ(ir->consts[1].bits, 8);
            CHECK_EQ(ir->consts[2].bits, 8);
            for (i = 0; i < ir->nstructs; i++) {
                if (strcmp(ir->structs[i].name, "P") == 0) {
                    CHECK_EQ(ir->structs[i].size, 16);
                }
            }
            CHECK(ir->nobjects >= 2);
            CHECK_EQ(ir->objects[0].size, 16);
            CHECK_EQ(ir->objects[1].offset, 16);
            CHECK_EQ(ir->objects[1].size, 8);
        }
        tc_analysis_destroy(an64);
    }
    free_fixture(&fixture);
}

static void test_ir_forms(void) {
    static const char *root =
        "#program\n"
        "import lib\n"
        "unsafe then\n"
        "    _ = call lib.forms\n"
        "end\n";
    static const char *lib =
        "#lib\n"
        "public struct S then\n"
        "    var f: int32\n"
        "end\n"
        "public static var gs: S = make S, f: 1\n"
        "public @fast func callee(@range(1,10) n: int32) int32 then\n"
        "    _ = return n\n"
        "end\n"
        "public @unsafe func forms() int32 then\n"
        "    var s: S = make S, f: 2\n"
        "    s.f = copy int32, 3\n"
        "    var p: ptr<int32> = addr int32, s.f\n"
        "    var q: ptr<int32> = addr int32, Self.gs.f\n"
        "    var fp: fnptr<int32, int32> = funcaddr Self.callee\n"
        "    var r: int32 = call Self.callee, n: 4\n"
        "    var t: int32 = call_indirect fp, 5\n"
        "    var n: usize = sizeof S\n"
        "    var ni: int32 = cast int32, n\n"
        "    unsafe then\n"
        "        _ = store int32, p, r\n"
        "    end\n"
        "    _ = write int32, t\n"
        "    _ = write int32, ni\n"
        "    _ = write int32, Self.gs.f\n"
        "    _ = return ni\n"
        "end\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    const TcUnit *unit;
    uint32_t callee_id = UINT32_MAX;
    int seen_addr = 0;
    int seen_addr_static = 0;
    int seen_funcaddr = 0;
    int seen_call_named = 0;
    int seen_indirect = 0;
    int seen_sizeof = 0;
    int seen_field_assign = 0;
    if (!make_fixture(&fixture, root, lib)) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    {
        size_t i;
        for (i = 0; i < ir->nfuncs; i++) {
            if (strcmp(ir->funcs[i].name, "callee") == 0) {
                callee_id = ir->funcs[i].id;
            }
        }
    }
    CHECK(callee_id != UINT32_MAX);
    unit = find_unit(ir, callee_id == UINT32_MAX ? UINT32_MAX : 9999u);
    unit = NULL;
    {
        size_t i;
        for (i = 0; i < ir->nunits; i++) {
            if (ir->units[i].func_id != TC_UNIT_TOP_LEVEL &&
                strcmp(ir->funcs[ir->units[i].func_id].name, "forms") == 0) {
                unit = &ir->units[i];
            }
        }
    }
    CHECK(unit != NULL);
    if (unit == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    validate_unit(ir, unit);
    {
        size_t bi;
        for (bi = 0; bi < unit->nblocks; bi++) {
            const TcBlock *block = &unit->blocks[bi];
            size_t ii;
            for (ii = 0; ii < block->n; ii++) {
                const TcInstr *instr = &block->instrs[ii];
                if (instr->op == TC_COPY && instr->dst.kind == TC_OP_FIELD) {
                    seen_field_assign++;
                    CHECK_EQ(instr->dst.field.nfields, 1);
                }
                if (instr->op == TC_ADDR) {
                    seen_addr++;
                    CHECK_EQ(instr->nargs, 1);
                    CHECK(instr->args[0].value.kind == TC_OP_FIELD);
                    if (instr->args[0].value.kind == TC_OP_FIELD &&
                        instr->args[0].value.field.base < unit->nvregs) {
                        uint32_t object = unit->vreg_objects
                            [instr->args[0].value.field.base];
                        if (object != UINT32_MAX && object < ir->nobjects &&
                            ir->objects[object].region == TC_REG_STATIC) {
                            seen_addr_static++;
                        }
                    }
                }
                if (instr->op == TC_FUNCADDR) {
                    seen_funcaddr++;
                    CHECK_EQ(instr->nargs, 1);
                    CHECK(instr->args[0].value.kind == TC_OP_FUNC);
                    CHECK_EQ(instr->args[0].value.func, callee_id);
                    CHECK(instr->type != NULL &&
                          instr->type->tag == TC_TY_FNPTR);
                }
                if (instr->op == TC_CALL) {
                    CHECK_EQ(instr->args[0].value.kind, TC_OP_FUNC);
                    if (instr->args[0].value.func == callee_id &&
                        instr->nargs == 2 && instr->args[1].name != NULL &&
                        strcmp(instr->args[1].name, "n") == 0) {
                        seen_call_named++;
                    }
                }
                if (instr->op == TC_CALL_INDIRECT) {
                    seen_indirect++;
                    CHECK (instr->args[0].value.kind == TC_OP_VREG);
                    CHECK(instr->args[0].value.vreg < unit->nvregs);
                    CHECK(instr->args[1].value.kind == TC_OP_CONST);
                }
                if (instr->op == TC_SIZEOF) {
                    seen_sizeof++;
                    CHECK_EQ(instr->nargs, 0);
                    CHECK(instr->type != NULL &&
                          instr->type->tag == TC_TY_STRUCT);
                }
            }
        }
    }
    CHECK(seen_field_assign >= 1);
    CHECK(seen_addr == 2);
    CHECK(seen_addr_static == 1);
    CHECK(seen_funcaddr == 1);
    CHECK(seen_call_named == 1);
    CHECK(seen_indirect == 1);
    CHECK(seen_sizeof == 1);
    tc_analysis_destroy(an);
}

static int write_named(const char *dir, const char *name, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.tc", dir, name);
    return write_text_file(path, text);
}

/* Static layout: import-DAG reverse topological order (importers first, ties
 * by module_order), then module source order (standard §13.3). */
static void test_ir_static_layout(void) {
    char dir[] = "/tmp/tc-ir-layout-XXXXXX";
    char root[512];
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    const TcIR *ir;
    if (mkdtemp(dir) == NULL) {
        CHECK(0);
        return;
    }
    snprintf(root, sizeof(root), "%s/root.tc", dir);
    write_text_file(root,
                    "#program\n"
                    "import a\n"
                    "import b\n"
                    "var g: int32 = copy int32, 0\n");
    write_named(dir, "a",
                "#lib\n"
                "import c\n"
                "public static var SA: int32 = 1\n");
    write_named(dir, "b",
                "#lib\n"
                "import c\n"
                "public static var SB: int32 = 2\n");
    write_named(dir, "c", "#lib\npublic static var SC: int32 = 3\n");
    an = tc_analyze(root, 64, NULL, 0, &status, &diag, &impl);
    {
        char path[512];
        const char *names[] = {"root", "a", "b", "c"};
        size_t i;
        for (i = 0; i < 4; i++) {
            snprintf(path, sizeof(path), "%s/%s.tc", dir, names[i]);
            unlink(path);
        }
    }
    rmdir(dir);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir == NULL) {
        tc_analysis_destroy(an);
        return;
    }
    CHECK_EQ(ir->nmodules, 4);
    /* module_order is import DFS order: root, a, c, b. */
    CHECK(strstr(ir->modules[0].path, "root.tc") != NULL);
    CHECK(strstr(ir->modules[1].path, "a.tc") != NULL);
    CHECK(strstr(ir->modules[2].path, "c.tc") != NULL);
    CHECK(strstr(ir->modules[3].path, "b.tc") != NULL);
    /* Static objects: importers first (root, a, b), then c; one instance. */
    CHECK_EQ(ir->nobjects, 4);
    CHECK_EQ(ir->objects[0].module, 0);
    CHECK_EQ(ir->objects[0].offset, 0);
    CHECK_EQ(ir->objects[1].module, 1);
    CHECK_EQ(ir->objects[1].offset, 8);
    CHECK_EQ(ir->objects[2].module, 3);
    CHECK_EQ(ir->objects[2].offset, 16);
    CHECK_EQ(ir->objects[3].module, 2);
    CHECK_EQ(ir->objects[3].offset, 24);
    CHECK_EQ(ir->objects[3].init->bytes[0], 3);
    tc_analysis_destroy(an);
}

static char *print_to_string(const TcIR *ir) {
    static char buffer[65536];
    FILE *file = tmpfile();
    size_t size;
    if (file == NULL) {
        return NULL;
    }
    if (tc_ir_print(ir, file) != 0) {
        fclose(file);
        return NULL;
    }
    fflush(file);
    rewind(file);
    size = fread(buffer, 1, sizeof(buffer) - 1u, file);
    fclose(file);
    buffer[size] = '\0';
    return buffer;
}

static void test_ir_print(void) {
    static const char *root =
        "#program\n"
        "var x: int32 = copy int32, 1\n";
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    char expected[8192];
    char *text;
    if (!make_fixture(&fixture, root, "#lib\n")) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    CHECK(an != NULL && status == TC_ANALYSIS_OK);
    if (an == NULL || status != TC_ANALYSIS_OK) {
        free_fixture(&fixture);
        return;
    }
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    text = ir != NULL ? print_to_string(ir) : NULL;
    CHECK(text != NULL);
    snprintf(expected, sizeof(expected),
             "tc-ir 0.0.46 profile=aarch64-linux-user w=64 endian=little\n"
             "modules:\n"
             "  m0 \"%s\"\n"
             "structs:\n"
             "consts:\n"
             "objects:\n"
             "  o0 m0 region=static offset=0 size=4 type=int32 "
             "lifetime=program writable=1 parent=- init=-\n"
             "funcs:\n"
             "units:\n"
             "  u0 func=top vregs=1\n"
             "    vreg_types=[%%0:int32]\n"
             "    vreg_objects=[%%0:o0]\n"
             "    b0 preds=[] succs=[]\n"
             "      %%0 = copy int32, const:int32:0x1 @2:16\n",
             ir->modules[0].path);
    if (text == NULL || strcmp(text, expected) != 0) {
        printf("FAIL TC-UNIT-IR-0006: printed IR mismatch\n--- got ---\n%s"
               "--- expected ---\n%s",
               text != NULL ? text : "<null>", expected);
        failures++;
    }
    /* Printing is deterministic. */
    if (text != NULL) {
        char copy[65536];
        char *again;
        snprintf(copy, sizeof(copy), "%s", text);
        again = print_to_string(ir);
        CHECK(again != NULL);
        if (again != NULL) {
            CHECK(strcmp(copy, again) == 0);
        }
    }
    tc_analysis_destroy(an);
    free_fixture(&fixture);
}

static void test_ir_sem_only(void) {
    static const char *root = "#program\n";
    char dir[] = "/tmp/tc-ir-sem-XXXXXX";
    char path[256];
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    TcImplError impl = TC_IMPL_OK;
    if (mkdtemp(dir) == NULL) {
        CHECK(0);
        return;
    }
    snprintf(path, sizeof(path), "%s/root.tc", dir);
    write_text_file(path, root);
    an = tc_analyze_sem_only(path, 64, NULL, 0, &status, &diag, &impl);
    unlink(path);
    rmdir(dir);
    CHECK(an != NULL);
    if (an != NULL) {
        CHECK(status == TC_ANALYSIS_OK);
        CHECK(tc_analysis_ir(an) == NULL);
        tc_analysis_destroy(an);
    }
}


/* Multiple library functions: `units` grows while functions are built, so each
 * TcFunc.body pointer must still refer to the final unit array. */
static void test_ir_multi_function_bodies(void) {
    Fixture fixture;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcAnalysis *an;
    const TcIR *ir;
    size_t i;
    if (!make_fixture(&fixture,
                      "#program\n"
                      "import lib\n"
                      "var a: int32 = call lib.f0, 1\n"
                      "var b: int32 = call lib.f1, a\n"
                      "var c: int32 = call lib.f2, b\n",
                      "#lib\n"
                      "public func f0(x: int32) int32 then\n"
                      "var r: int32 = add int32, x, 1\n"
                      "_ = return r\n"
                      "end\n"
                      "public func f1(x: int32) int32 then\n"
                      "var r: int32 = add int32, x, 2\n"
                      "_ = return r\n"
                      "end\n"
                      "public func f2(x: int32) int32 then\n"
                      "var r: int32 = add int32, x, 3\n"
                      "_ = return r\n"
                      "end\n")) {
        CHECK(0);
        return;
    }
    an = analyze_fixture(&fixture, 64, &status, &diag);
    free_fixture(&fixture);
    CHECK(an != NULL);
    if (an == NULL) {
        return;
    }
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(an);
    CHECK(ir != NULL);
    if (ir != NULL) {
        CHECK_EQ(ir->nfuncs, 3);
        for (i = 0; i < ir->nfuncs; i++) {
            const TcFunc *func = &ir->funcs[i];
            CHECK(func->body != NULL);
            if (func->body == NULL) {
                continue;
            }
            CHECK(func->body >= ir->units &&
                  func->body < ir->units + ir->nunits);
            CHECK_EQ(func->body->func_id, func->id);
        }
    }
    tc_analysis_destroy(an);
}

int main(void) {
    test_ir_shape();
    test_ir_control_flow();
    test_ir_switch_successor_capacity();
    test_ir_aggregate_const();
    test_ir_forms();
    test_ir_static_layout();
    test_ir_word_sizes();
    test_ir_print();
    test_ir_sem_only();
    test_ir_multi_function_bodies();
    if (failures != 0) {
        printf("ir.unit: %d failure(s), %d checks\n", failures, checks);
        return 1;
    }
    printf("ir.unit: %d checks passed\n", checks);
    return 0;
}
