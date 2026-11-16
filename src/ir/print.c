/* TC 0.0.46 IR textual printing (design §3, §14 `ir/print`).
 *
 * The format is a stable, deterministic dump of one TcIR: profile, modules,
 * structs, consts, objects, functions, units/blocks/instructions.  It exists
 * for tests, diagnostics and differential debugging; it is not an input
 * format.  Source spans print as 1-based line:column; module identity is the
 * resolved path in the modules section.
 */
#include "ir/print.h"

#include <stdio.h>
#include <string.h>

static const char *const tc_opcode_names[TC_OPCODE_COUNT] = {
    "add", "sub", "mul", "div", "mod", "neg", "abs", "shl", "shr",
    "and", "or", "xor", "not", "eq", "ne", "lt", "le", "gt", "ge",
    "cast", "trunc", "bitcast", "load", "store", "memmove", "addr",
    "ptradd", "ptrsub", "ptrdiff", "ptreq", "ptrne", "ptrlt", "ptrle",
    "ptrgt", "ptrge", "sizeof", "funcaddr", "call", "call_indirect",
    "funceq", "funcne", "make", "copy", "br", "br_if", "switch",
    "return", "trap", "read", "write", "writeln"
};

static const char *tc_mode_suffix(TcOpMode mode) {
    switch (mode) {
    case TC_WRAP:
        return ".wrap";
    case TC_IEEE:
        return ".ieee";
    case TC_NSW:
        return ".nsw";
    case TC_NUW:
        return ".nuw";
    case TC_VOLATILE:
        return ".volatile";
    case TC_STRICT:
    default:
        return "";
    }
}

static const char *tc_region_name(TcRegionKind region) {
    switch (region) {
    case TC_REG_FRAME:
        return "frame";
    case TC_REG_STATIC:
        return "static";
    case TC_REG_DEVICE:
        return "device";
    case TC_REG_CODE:
        return "code";
    case TC_REG_HEAP:
        return "heap";
    default:
        return "?";
    }
}

static const char *tc_lifetime_name(TcLifetime lifetime) {
    switch (lifetime) {
    case TC_LIFE_FRAME:
        return "frame";
    case TC_LIFE_PROGRAM:
        return "program";
    case TC_LIFE_HOST:
        return "host";
    default:
        return "?";
    }
}

static void print_type(FILE *out, const TcIR *ir, const TcType *type) {
    if (type == NULL) {
        fputs("?", out);
        return;
    }
    switch (type->tag) {
    case TC_TY_I8: fputs("int8", out); return;
    case TC_TY_I16: fputs("int16", out); return;
    case TC_TY_I32: fputs("int32", out); return;
    case TC_TY_I64: fputs("int64", out); return;
    case TC_TY_U8: fputs("uint8", out); return;
    case TC_TY_U16: fputs("uint16", out); return;
    case TC_TY_U32: fputs("uint32", out); return;
    case TC_TY_U64: fputs("uint64", out); return;
    case TC_TY_ISIZE: fputs("isize", out); return;
    case TC_TY_USIZE: fputs("usize", out); return;
    case TC_TY_F32: fputs("float32", out); return;
    case TC_TY_F64: fputs("float64", out); return;
    case TC_TY_BOOL: fputs("bool", out); return;
    case TC_TY_VOID: fputs("void", out); return;
    case TC_TY_PTR:
        fputs("ptr<", out);
        print_type(out, ir, type->ptr.pointee);
        fputc('>', out);
        return;
    case TC_TY_FNPTR: {
        size_t i;
        fputs("fnptr<(", out);
        for (i = 0; i < type->fnptr.n; i++) {
            if (i != 0) {
                fputc(',', out);
            }
            print_type(out, ir, type->fnptr.params[i]);
        }
        fputs(")->", out);
        print_type(out, ir, type->fnptr.ret);
        fputc('>', out);
        return;
    }
    case TC_TY_STRUCT: {
        uint32_t id = type->struct_.struct_id;
        if (ir != NULL && id < ir->nstructs &&
            ir->structs[id].name != NULL) {
            fprintf(out, "%s#%u", ir->structs[id].name, id);
        } else {
            fprintf(out, "struct#%u", id);
        }
        return;
    }
    default:
        fputs("?", out);
        return;
    }
}

static void print_operand(FILE *out, const TcIR *ir, const TcOperand *op) {
    switch (op->kind) {
    case TC_OP_VREG:
        fprintf(out, "%%%u", op->vreg);
        break;
    case TC_OP_FIELD: {
        size_t i;
        fprintf(out, "%%%u", op->field.base);
        for (i = 0; i < op->field.nfields; i++) {
            fprintf(out, ".%u", op->field.fields[i]);
        }
        break;
    }
    case TC_OP_CONST:
        fputs("const:", out);
        print_type(out, ir, op->konst.type);
        fprintf(out, ":0x%llx", (unsigned long long)op->konst.bits);
        break;
    case TC_OP_LABEL:
        fprintf(out, "L%u", op->label);
        break;
    case TC_OP_FUNC:
        fprintf(out, "f%u", op->func);
        break;
    case TC_OP_STRUCT:
        fprintf(out, "struct#%u", op->struct_id);
        break;
    case TC_OP_DISCARD:
        fputc('_', out);
        break;
    default:
        fputc('?', out);
        break;
    }
}

static void print_init(FILE *out, const TcInitImage *init) {
    size_t i;
    if (init == NULL) {
        fputs("-", out);
        return;
    }
    for (i = 0; i < init->size; i++) {
        fprintf(out, "%02x", init->bytes[i]);
    }
}

static void print_instruction(FILE *out, const TcIR *ir, const TcInstr *instr) {
    size_t i;
    print_operand(out, ir, &instr->dst);
    fputs(" = ", out);
    fputs(tc_opcode_names[instr->op], out);
    fputs(tc_mode_suffix(instr->mode), out);
    if (instr->type != NULL || instr->nargs != 0 || instr->ntargets != 0) {
        fputc(' ', out);
    }
    if (instr->type != NULL) {
        print_type(out, ir, instr->type);
        if (instr->nargs != 0 || instr->ntargets != 0) {
            fputs(", ", out);
        }
    }
    for (i = 0; i < instr->nargs; i++) {
        if (i != 0) {
            fputs(", ", out);
        }
        if (instr->args[i].name != NULL) {
            fprintf(out, "%s:", instr->args[i].name);
        }
        print_operand(out, ir, &instr->args[i].value);
    }
    if (instr->ntargets != 0) {
        fputs(" [", out);
        for (i = 0; i < instr->ntargets; i++) {
            if (i != 0) {
                fputc(',', out);
            }
            fprintf(out, "L%u", instr->switch_targets[i]);
        }
        fputc(']', out);
    }
    fprintf(out, " @%u:%u", instr->span.line, instr->span.column);
}

static void print_flags(FILE *out, TcFuncFlags flags) {
    static const struct {
        TcFuncFlags bit;
        const char *name;
    } names[] = {
        {TC_FN_UNSAFE, "unsafe"},       {TC_FN_SAFE, "safe"},
        {TC_FN_FAST, "fast"},           {TC_FN_FAST_MATH, "fast_math"},
        {TC_FN_PURE, "pure"},           {TC_FN_READONLY, "readonly"},
        {TC_FN_NOUNWIND, "nounwind"},   {TC_FN_WILLRETURN, "willreturn"},
        {TC_FN_NO_OVERFLOW, "no_overflow"}, {TC_FN_INLINE, "inline"},
        {TC_FN_NOINLINE, "noinline"},
    };
    size_t i;
    int first = 1;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if ((flags & names[i].bit) != 0) {
            if (!first) {
                fputc(',', out);
            }
            fputs(names[i].name, out);
            first = 0;
        }
    }
    if (first) {
        fputc('-', out);
    }
}

int tc_ir_print(const TcIR *ir, FILE *out) {
    size_t i;
    if (ir == NULL || out == NULL) {
        return -1;
    }
    fprintf(out, "tc-ir 0.0.46 profile=%s w=%u endian=%s\n",
            ir->profile != NULL && ir->profile->target != NULL
                ? ir->profile->target->name
                : "-",
            ir->profile != NULL ? ir->profile->addr_width : 0,
            ir->profile != NULL && ir->profile->little_endian ? "little"
                                                             : "big");
    fputs("modules:\n", out);
    for (i = 0; i < ir->nmodules; i++) {
        fprintf(out, "  m%u \"%s\"\n", ir->modules[i].id,
                ir->modules[i].path != NULL ? ir->modules[i].path : "");
    }
    fputs("structs:\n", out);
    for (i = 0; i < ir->nstructs; i++) {
        const TcStructDef *def = &ir->structs[i];
        size_t k;
        fprintf(out, "  struct#%u m%u %s size=%llu fields=[", def->id,
                def->module, def->name != NULL ? def->name : "",
                (unsigned long long)def->size);
        for (k = 0; k < def->nfields; k++) {
            if (k != 0) {
                fputc(',', out);
            }
            fprintf(out, "%s:", def->fields[k].name != NULL
                               ? def->fields[k].name
                               : "");
            print_type(out, ir, def->fields[k].type);
            fprintf(out, "@%llu", (unsigned long long)def->fields[k].offset);
            if (def->fields[k].padding != 0) {
                fprintf(out, "+pad%llu",
                        (unsigned long long)def->fields[k].padding);
            }
            if (def->fields[k].readonly) {
                fputs(":readonly", out);
            }
        }
        fputs("]\n", out);
    }
    fputs("consts:\n", out);
    for (i = 0; i < ir->nconsts; i++) {
        const TcConst *konst = &ir->consts[i];
        fprintf(out, "  c%u m%u %s type=", konst->id, konst->module,
                konst->name != NULL ? konst->name : "");
        print_type(out, ir, konst->type);
        fprintf(out, " bits=0x%llx init=",
                (unsigned long long)konst->bits);
        print_init(out, konst->init);
        fputc('\n', out);
    }
    fputs("objects:\n", out);
    for (i = 0; i < ir->nobjects; i++) {
        const TcObject *object = &ir->objects[i];
        fprintf(out, "  o%u m", object->id);
        if (object->module == TC_OBJECT_NO_MODULE) {
            fputc('-', out);
        } else {
            fprintf(out, "%u", object->module);
        }
        fprintf(out, " region=%s offset=%llu size=%llu type=",
                tc_region_name(object->region),
                (unsigned long long)object->offset,
                (unsigned long long)object->size);
        print_type(out, ir, object->type);
        fprintf(out, " lifetime=%s writable=%d parent=",
                tc_lifetime_name(object->lifetime), object->writable);
        if (object->parent == TC_OBJECT_NO_PARENT) {
            fputc('-', out);
        } else {
            fprintf(out, "o%u", object->parent);
        }
        fputs(" init=", out);
        print_init(out, object->init);
        fputc('\n', out);
    }
    fputs("funcs:\n", out);
    for (i = 0; i < ir->nfuncs; i++) {
        const TcFunc *func = &ir->funcs[i];
        size_t k;
        fprintf(out, "  f%u m%u %s sig=(", func->id, func->module,
                func->name != NULL ? func->name : "");
        for (k = 0; k < func->sig.nparams; k++) {
            if (k != 0) {
                fputc(',', out);
            }
            print_type(out, ir, func->sig.params[k].type);
        }
        fputs(")->", out);
        print_type(out, ir, func->sig.ret);
        fputs(" flags=", out);
        print_flags(out, func->flags);
        if (func->body != NULL) {
            fprintf(out, " body=u%u\n",
                    (unsigned)(func->body - ir->units));
        } else {
            fputs(" body=-\n", out);
        }
        for (k = 0; k < func->sig.nparams; k++) {
            if (func->sig.params[k].flags != 0) {
                fprintf(out, "    param%u flags=0x%x align=%llu range=[0x%llx,"
                             "0x%llx]\n",
                        (unsigned)k, (unsigned)func->sig.params[k].flags,
                        (unsigned long long)func->sig.params[k].align,
                        (unsigned long long)func->sig.params[k].lo,
                        (unsigned long long)func->sig.params[k].hi);
            }
        }
    }
    fputs("units:\n", out);
    for (i = 0; i < ir->nunits; i++) {
        const TcUnit *unit = &ir->units[i];
        size_t k;
        if (unit->func_id == TC_UNIT_TOP_LEVEL) {
            fprintf(out, "  u%u func=top", (unsigned)i);
        } else {
            fprintf(out, "  u%u func=f%u", (unsigned)i, unit->func_id);
        }
        fprintf(out, " vregs=%u\n", (unsigned)unit->nvregs);
        if (unit->nvregs != 0) {
            fputs("    vreg_types=[", out);
            for (k = 0; k < unit->nvregs; k++) {
                if (k != 0) {
                    fputc(',', out);
                }
                fprintf(out, "%%%u:", (unsigned)k);
                print_type(out, ir, unit->vreg_types[k]);
            }
            fputs("]\n", out);
            fputs("    vreg_objects=[", out);
            for (k = 0; k < unit->nvreg_objects; k++) {
                if (k != 0) {
                    fputc(',', out);
                }
                fprintf(out, "%%%u:", (unsigned)k);
                if (unit->vreg_objects[k] == UINT32_MAX) {
                    fputc('-', out);
                } else {
                    fprintf(out, "o%u", unit->vreg_objects[k]);
                }
            }
            fputs("]\n", out);
        }
        for (k = 0; k < unit->nblocks; k++) {
            const TcBlock *block = &unit->blocks[k];
            size_t bi;
            size_t ii;
            fprintf(out, "    b%u preds=[", block->id);
            for (bi = 0; bi < block->npred; bi++) {
                if (bi != 0) {
                    fputc(',', out);
                }
                fprintf(out, "b%u", block->preds[bi]);
            }
            fputs("] succs=[", out);
            for (bi = 0; bi < block->nsucc; bi++) {
                if (bi != 0) {
                    fputc(',', out);
                }
                fprintf(out, "b%u", block->succs[bi]);
            }
            fputs("]\n", out);
            for (ii = 0; ii < block->n; ii++) {
                fputs("      ", out);
                print_instruction(out, ir, &block->instrs[ii]);
                fputc('\n', out);
            }
        }
    }
    return 0;
}
