/* TC 0.0.46 reference VM: instruction interpreter (design §9).
 *
 * One instruction per step; operands are read before any target is committed
 * (standard §12.5.2).  Calls create a new frame, parameters are copied by
 * value, and the abstract depth limit D is checked before a call creates its
 * frame.  Any trap terminates the whole program immediately.
 */
#include "vm/internal.h"

#include <string.h>

#include "runtime/format.h"
#include "runtime/semantics.h"

static int read_value(TcVm *vm, TcVmFrame *frame, const TcOperand *operand,
                      TcVmValue *out, const TcType **type) {
    if (!tc_vm_read_operand(vm, frame, operand, out, type)) {
        return 0;
    }
    return 1;
}

static int write_result(TcVm *vm, TcVmFrame *frame, const TcInstr *instr,
                        const TcVmValue *value) {
    return tc_vm_write_operand(vm, frame, &instr->dst, value);
}

int tc_vm_debug_address(const TcVm *vm, const TcInstr *instr, uint64_t *out) {
    size_t index;
    if (out == NULL || vm == NULL || vm->debug_linemap == NULL ||
        !tc_linemap_instr_index(vm->debug_linemap, instr, &index) ||
        index > (UINT64_MAX - vm->debug_code_base) / 4u) {
        return 0;
    }
    *out = vm->debug_code_base + (uint64_t)index * 4u;
    return 1;
}

uint64_t tc_vm_debug_pc(const TcVm *vm) {
    const TcLineMapEntry *entry;
    if (vm == NULL) {
        return 0u;
    }
    if (vm->current_instr != NULL && vm->debug_linemap != NULL) {
        entry = tc_linemap_find_instr(vm->debug_linemap, vm->current_instr);
        if (entry != NULL) {
            return vm->debug_code_base +
                   (uint64_t)(entry - vm->debug_linemap->entries) * 4u;
        }
    }
    return vm->debug_pc;
}

static void debug_save(TcVm *vm, uint32_t frame_index, uint32_t block,
                       size_t pc) {
    TcVmFrame *frame = &vm->frames[frame_index];
    frame->resume_block = block;
    frame->resume_pc = pc;
    frame->has_resume = 1;
    vm->debug_frame = frame_index;
}

static int debug_save_next(TcVm *vm, uint32_t frame_index, uint32_t block,
                           size_t pc) {
    TcVmFrame *frame = &vm->frames[frame_index];
    debug_save(vm, frame_index, block, pc);
    vm->current_instr = NULL;
    while (block < frame->unit->nblocks) {
        const TcBlock *instructions = &frame->unit->blocks[block];
        if (pc < instructions->n) {
            uint64_t address;
            vm->current_unit = frame->unit;
            vm->current_bb = block;
            vm->current_insn = (uint32_t)pc;
            vm->current_instr = &instructions->instrs[pc];
            vm->current_span = vm->current_instr->span;
            if (!tc_vm_debug_address(vm, vm->current_instr, &address)) {
                return 0;
            }
            vm->debug_pc = address;
            return 1;
        }
        block++;
        pc = 0u;
    }
    vm->debug_pc = vm->debug_code_base +
                   (uint64_t)vm->debug_linemap->count * 4u;
    return 1;
}

static int debug_boundary(TcVm *vm, uint32_t frame_index, uint32_t block,
                          size_t pc) {
    TcVmFrame *frame = &vm->frames[frame_index];
    const TcInstr *instr;
    uint64_t address;
    unsigned flags;
    if (vm->debug == NULL) {
        return 1;
    }
    instr = &frame->unit->blocks[block].instrs[pc];
    if (!tc_vm_debug_address(vm, instr, &address)) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return -1;
    }
    vm->debug_pc = address;
    vm->debug_frame = frame_index;
    if (vm->debug_stop != 0u) {
        debug_save(vm, frame_index, block, pc);
        return 0;
    }
    flags = vm->debug->boundary(vm->debug->ctx, vm, frame_index, address);
    if (flags != 0u) {
        vm->debug_stop = flags;
        debug_save(vm, frame_index, block, pc);
        return 0;
    }
    return 1;
}

static int is_float_op(const TcType *type) {
    return type != NULL && tc_type_is_float(type);
}

/* ------------------------------------------------------------------------- */
/* Calls.                                                                    */
/* ------------------------------------------------------------------------- */

static int call_depth_ok(TcVm *vm, const TcSpan *span) {
    if (vm->depth >= vm->profile->depth_D) {
        return tc_vm_trap(vm, span, TC_EK_STACK_OVERFLOW);
    }
    return 1;
}

/* Invoke one callee.  `indirect` allows a signature mismatch to be treated as
 * UB (checked build traps; release performs the call with the callee's own
 * parameter layout). */
static TcVmFlow do_call(TcVm *vm, uint32_t caller_frame, const TcFunc *callee,
                        const TcInstr *instr, size_t first_arg, int indirect,
                        TcVmValue *ret, int *has_ret) {
    TcVmFrame *frame;
    TcVmValue *values = NULL;
    size_t nargs;
    size_t i;
    uint32_t callee_frame = 0;
    TcVmFlow flow;
    TcVmValue call_ret;
    int call_has_ret = 0;
    const TcUnit *caller_unit;
    const TcInstr *caller_instr;
    TcSpan caller_span;
    uint32_t caller_bb;
    uint32_t caller_insn;

    if (callee == NULL || callee->body == NULL) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return TC_VM_FLOW_FAIL;
    }
    if (instr->nargs < first_arg) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return TC_VM_FLOW_FAIL;
    }
    nargs = instr->nargs - first_arg;
    if (nargs != callee->sig.nparams) {
        if (indirect) {
            if (vm->profile->checked_build) {
                tc_vm_trap(vm, &instr->span, TC_EK_ADDR_UNAVAILABLE);
                return TC_VM_FLOW_FAIL;
            }
            if (nargs > callee->sig.nparams) {
                nargs = callee->sig.nparams;
            }
        } else {
            tc_vm_impl(vm, TC_IMPL_INTERNAL);
            return TC_VM_FLOW_FAIL;
        }
    }
    if (nargs != 0) {
        values = (TcVmValue *)tc_vm_calloc(vm, nargs, sizeof(TcVmValue));
        if (values == NULL) {
            return TC_VM_FLOW_FAIL;
        }
    }
    for (i = 0; i < nargs; i++) {
        frame = &vm->frames[caller_frame];
        if (!read_value(vm, frame, &instr->args[first_arg + i].value,
                        &values[i], NULL)) {
            tc_vm_free(vm, values);
            return TC_VM_FLOW_FAIL;
        }
    }
    if (!tc_vm_push_frame(vm, callee->body, (uint32_t)(callee - vm->ir->funcs),
                          &callee_frame)) {
        tc_vm_free(vm, values);
        return TC_VM_FLOW_FAIL;
    }
    for (i = 0; i < nargs; i++) {
        TcOperand dst;
        memset(&dst, 0, sizeof(dst));
        dst.kind = TC_OP_VREG;
        dst.vreg = (uint32_t)i;
        frame = &vm->frames[callee_frame];
        if (!tc_vm_write_operand(vm, frame, &dst, &values[i])) {
            tc_vm_free(vm, values);
            return TC_VM_FLOW_FAIL;
        }
    }
    tc_vm_free(vm, values);
    caller_unit = vm->current_unit;
    caller_instr = vm->current_instr;
    caller_span = vm->current_span;
    caller_bb = vm->current_bb;
    caller_insn = vm->current_insn;
    frame = &vm->frames[caller_frame];
    frame->pending_callee = callee_frame;
    frame->pending_call = instr;
    frame->resume_block = caller_bb;
    frame->resume_pc = caller_insn;
    frame->has_resume = 1;
    memset(&call_ret, 0, sizeof(call_ret));
    tc_vm_observe_call(vm, instr, callee, 0);
    vm->depth++;
    if (vm->stats != NULL && vm->depth > vm->stats->depth_max) {
        vm->stats->depth_max = vm->depth;
    }
    if (vm->debug_single) {
        vm->debug_stop |= TC_VM_DEBUG_STOP;
    }
    flow = tc_vm_execute(vm, callee_frame, &call_ret, &call_has_ret);
    frame = &vm->frames[caller_frame];
    if (flow == TC_VM_FLOW_FAIL || flow == TC_VM_FLOW_PAUSE) {
        return flow;
    }
    if (ret != NULL) {
        *ret = call_ret;
    }
    if (has_ret != NULL) {
        *has_ret = call_has_ret;
    }
    frame->pending_callee = UINT32_MAX;
    frame->pending_call = NULL;
    frame->has_resume = 0;
    vm->current_unit = caller_unit;
    vm->current_instr = caller_instr;
    vm->current_span = caller_span;
    vm->current_bb = caller_bb;
    vm->current_insn = caller_insn;
    tc_vm_observe_call(vm, instr, callee, 1);
    vm->depth--;
    tc_vm_pop_frame(vm);
    if (callee->sig.ret != NULL && callee->sig.ret->tag != TC_TY_VOID &&
        !*has_ret) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return TC_VM_FLOW_FAIL;
    }
    return flow;
}

/* ------------------------------------------------------------------------- */
/* I/O.                                                                      */
/* ------------------------------------------------------------------------- */

static int do_write(TcVm *vm, TcVmFrame *frame, const TcInstr *instr,
                    int newline) {
    TcVmValue value;
    char text[512];
    size_t len;

    if (!read_value(vm, frame, &instr->args[0].value, &value, NULL)) {
        return 0;
    }
    if (!tc_format_scalar(vm->W, instr->type, value.bits, text, sizeof(text))) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    len = strlen(text);
    if (newline) {
        if (len + 1u >= sizeof(text)) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        text[len++] = '\n';
    }
    {
        int written = vm->profile->io == NULL ||
                      vm->profile->io->out == NULL ||
                      vm->profile->io->out->write == NULL
                          ? -1
                          : vm->profile->io->out->write(
                                vm->profile->io->out->ctx,
                                (const uint8_t *)text, len);
        if (written != 0) {
            tc_vm_observe_io(vm, 0u, "error");
            return tc_vm_trap(vm, &instr->span, TC_EK_IO);
        }
        tc_vm_observe_io(vm, (uint64_t)len, "ok");
    }
    return 1;
}

static int do_read(TcVm *vm, TcVmFrame *frame, const TcInstr *instr) {
    TcVmValue value;
    TcInputStatus status;
    memset(&value, 0, sizeof(value));
    status = tc_input_read_scalar(&vm->input, vm->W, instr->type, &value.bits);
    if (vm->input.consumed_overflow) {
        tc_vm_observe_io(vm, 0u, "error");
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (status == TC_INPUT_OOM) {
        tc_vm_observe_io(vm, 0u, "error");
        return tc_vm_impl(vm, TC_IMPL_OOM);
    }
    if (status == TC_INPUT_PAUSE) {
        return -2;
    }
    if (status != TC_INPUT_OK) {
        tc_vm_observe_io(vm, 0u, vm->input.last_eof ? "eof" : "error");
        return tc_vm_trap(vm, &instr->span, TC_EK_IO);
    }
    tc_vm_observe_io(vm, vm->input.last_consumed, "ok");
    if (instr->dst.kind == TC_OP_DISCARD) {
        return 1;
    }
    return tc_vm_write_operand(vm, frame, &instr->dst, &value);
}

/* ------------------------------------------------------------------------- */
/* make/copy helpers.                                                        */
/* ------------------------------------------------------------------------- */

static int do_make(TcVm *vm, TcVmFrame *frame, const TcInstr *instr) {
    const TcType *type = instr->type;
    uint32_t struct_id;
    const TcStructDef *def;
    uint8_t *buffer;
    int ok = 0;
    uint64_t size;
    size_t i;
    TcVmValue result;

    if (type == NULL || type->tag != TC_TY_STRUCT) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    struct_id = type->struct_.struct_id;
    if (struct_id >= vm->ir->nstructs) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    def = &vm->ir->structs[struct_id];
    if (instr->nargs != def->nfields) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    size = tc_vm_type_size(vm, type, &ok);
    if (!ok || size > SIZE_MAX) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    buffer = (uint8_t *)tc_vm_calloc(vm, (size_t)size, 1u);
    if (buffer == NULL) {
        return 0;
    }
    for (i = 0; i < def->nfields; i++) {
        TcVmValue field_value;
        const TcType *field_type = def->fields[i].type;
        if (!read_value(vm, frame, &instr->args[i].value, &field_value,
                        NULL)) {
            tc_vm_free(vm, buffer);
            return 0;
        }
        if (!tc_vm_write_host_value(vm, buffer + def->fields[i].offset,
                                   field_type, &field_value)) {
            tc_vm_free(vm, buffer);
            return 0;
        }
    }
    result.ptr = buffer;
    result.bits = 0;
    if (!write_result(vm, frame, instr, &result)) {
        tc_vm_free(vm, buffer);
        return 0;
    }
    tc_vm_free(vm, buffer);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Frame execution.                                                          */
/* ------------------------------------------------------------------------- */

TcVmFlow tc_vm_execute(TcVm *vm, uint32_t frame_index, TcVmValue *ret,
                       int *has_ret) {
    uint32_t block;
    size_t pc = 0;
    TcVmFrame *starting_frame;

    if (has_ret != NULL) {
        *has_ret = 0;
    }
    if (ret != NULL) {
        memset(ret, 0, sizeof(*ret));
    }
    if (frame_index >= vm->nframes) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return TC_VM_FLOW_FAIL;
    }
    starting_frame = &vm->frames[frame_index];
    if (starting_frame->has_resume) {
        block = starting_frame->resume_block;
        pc = starting_frame->resume_pc;
        starting_frame->has_resume = 0;
    } else {
        block = starting_frame->unit->entry;
    }
    for (;;) {
        TcVmFrame *frame = &vm->frames[frame_index];
        const TcUnit *unit = frame->unit;
        const TcBlock *instrs;
        const TcInstr *instr;
        int jumped = 0;

        if (block >= unit->nblocks) {
            return TC_VM_FLOW_END;
        }
        instrs = &unit->blocks[block];
        if (pc >= instrs->n) {
            if ((size_t)block + 1u < unit->nblocks) {
                block++;
                pc = 0;
                continue;
            }
            return TC_VM_FLOW_END;
        }
        instr = &instrs->instrs[pc];
        vm->current_span = instr->span;
        vm->current_unit = unit;
        vm->current_instr = instr;
        vm->current_bb = block;
        vm->current_insn = (uint32_t)pc;
        if (vm->debug != NULL) {
            int boundary = debug_boundary(vm, frame_index, block, pc);
            if (boundary < 0) {
                return TC_VM_FLOW_FAIL;
            }
            if (boundary == 0) {
                return TC_VM_FLOW_PAUSE;
            }
        }
        if (vm->debug == NULL || !frame->debug_counted) {
            tc_vm_observe_ir(vm, instr);
            if (vm->debug != NULL) {
                frame->debug_counted = 1;
                if (vm->debug_insns != UINT64_MAX) {
                    vm->debug_insns++;
                } else {
                    vm->count_overflow = 1;
                }
            }
        }

        switch (instr->op) {
        case TC_ADD:
        case TC_SUB:
        case TC_MUL:
        case TC_DIV:
        case TC_MOD:
        case TC_NEG:
        case TC_ABS:
        case TC_SHL:
        case TC_SHR:
        case TC_AND:
        case TC_OR:
        case TC_XOR:
        case TC_NOT: {
            const TcType *type = instr->type;
            TcVmValue a;
            TcVmValue b;
            TcVmValue result;
            TcErrorKind trap = (TcErrorKind)0;
            TcOpMode mode = instr->mode == TC_NSW || instr->mode == TC_NUW
                                ? TC_STRICT
                                : instr->mode;
            int produced;
            if (type == NULL) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (!read_value(vm, frame, &instr->args[0].value, &a, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            memset(&result, 0, sizeof(result));
            if ((instr->op == TC_ADD || instr->op == TC_SUB ||
                 instr->op == TC_MUL || instr->op == TC_DIV ||
                 instr->op == TC_MOD) &&
                is_float_op(type)) {
                if (instr->nargs < 2 ||
                    !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                    return TC_VM_FLOW_FAIL;
                }
                produced = tc_sem_float_binop(vm->W, type, instr->op, mode,
                                              a.bits, b.bits, &result.bits,
                                              &trap);
            } else if ((instr->op == TC_NEG || instr->op == TC_ABS) &&
                       is_float_op(type)) {
                produced = tc_sem_float_unop(vm->W, type, instr->op, a.bits,
                                             &result.bits);
            } else if (instr->op == TC_NEG || instr->op == TC_ABS ||
                       instr->op == TC_NOT) {
                produced = tc_sem_int_unop(vm->W, type, instr->op, mode,
                                           a.bits, &result.bits, &trap);
            } else {
                if (instr->nargs < 2 ||
                    !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                    return TC_VM_FLOW_FAIL;
                }
                produced = tc_sem_int_binop(vm->W, type, instr->op, mode,
                                            a.bits, b.bits, &result.bits,
                                            &trap);
            }
            if (!produced) {
                if (trap == (TcErrorKind)0) {
                    tc_vm_impl(vm, TC_IMPL_INTERNAL);
                } else {
                    tc_vm_trap(vm, &instr->span, trap);
                }
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_EQ:
        case TC_NE:
        case TC_LT:
        case TC_LE:
        case TC_GT:
        case TC_GE: {
            TcVmValue a;
            TcVmValue b;
            TcVmValue result;
            if (instr->type == NULL ||
                !read_value(vm, frame, &instr->args[0].value, &a, NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_sem_compare(vm->W, instr->type, instr->op, a.bits, b.bits,
                                &result.bits)) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_CAST:
        case TC_TRUNC:
        case TC_BITCAST: {
            const TcType *source_type = NULL;
            TcVmValue source;
            TcVmValue result;
            TcErrorKind trap = (TcErrorKind)0;
            if (!read_value(vm, frame, &instr->args[0].value, &source,
                            &source_type) ||
                source_type == NULL || instr->type == NULL) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            memset(&result, 0, sizeof(result));
            if (instr->op == TC_CAST) {
                if (!tc_sem_cast(vm->W, source_type, instr->type, source.bits,
                                 &result.bits, &trap)) {
                    if (trap == (TcErrorKind)0) {
                        tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    } else {
                        tc_vm_trap(vm, &instr->span, trap);
                    }
                    return TC_VM_FLOW_FAIL;
                }
            } else if (instr->op == TC_TRUNC) {
                if (!tc_sem_trunc(vm->W, source_type, instr->type, source.bits,
                                  &result.bits)) {
                    tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    return TC_VM_FLOW_FAIL;
                }
            } else {
                if (!tc_sem_bitcast(vm->W, instr->type, source.bits,
                                    &result.bits)) {
                    tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    return TC_VM_FLOW_FAIL;
                }
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_LOAD: {
            TcVmValue pointer;
            TcVmValue result;
            if (instr->type == NULL ||
                !read_value(vm, frame, &instr->args[0].value, &pointer,
                            NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_load_value(vm, pointer.bits, instr->type,
                                  instr->mode == TC_VOLATILE, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_STORE: {
            TcVmValue pointer;
            TcVmValue value;
            if (instr->type == NULL ||
                !read_value(vm, frame, &instr->args[0].value, &pointer,
                            NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &value, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_store_value(vm, pointer.bits, instr->type, &value,
                                   instr->mode == TC_VOLATILE)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_MEMMOVE: {
            TcVmValue dst;
            TcVmValue src;
            TcVmValue count;
            if (instr->type == NULL ||
                !read_value(vm, frame, &instr->args[0].value, &dst, NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &src, NULL) ||
                !read_value(vm, frame, &instr->args[2].value, &count, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_memmove(vm, dst.bits, src.bits, count.bits,
                               instr->type)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_ADDR: {
            TcVmValue result;
            if (!tc_vm_operand_address(vm, frame, &instr->args[0].value,
                                       instr->type, &result.bits)) {
                return TC_VM_FLOW_FAIL;
            }
            result.bits &= vm->addr_mask;
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_PTRADD:
        case TC_PTRSUB: {
            TcVmValue pointer;
            TcVmValue count;
            TcVmValue result;
            if (!read_value(vm, frame, &instr->args[0].value, &pointer,
                            NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &count, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_ptr_arith(vm, pointer.bits, count.bits, instr->type,
                                 instr->op == TC_PTRADD, &result.bits)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_PTRDIFF: {
            TcVmValue a;
            TcVmValue b;
            TcVmValue result;
            if (!read_value(vm, frame, &instr->args[0].value, &a, NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_ptrdiff(vm, a.bits, b.bits, &result.bits)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_PTREQ:
        case TC_PTRNE:
        case TC_PTRLT:
        case TC_PTRLE:
        case TC_PTRGT:
        case TC_PTRGE: {
            TcVmValue a;
            TcVmValue b;
            TcVmValue result;
            uint64_t ua;
            uint64_t ub;
            int truth = 0;
            if (!read_value(vm, frame, &instr->args[0].value, &a, NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            ua = a.bits & vm->addr_mask;
            ub = b.bits & vm->addr_mask;
            switch (instr->op) {
            case TC_PTREQ: truth = ua == ub; break;
            case TC_PTRNE: truth = ua != ub; break;
            case TC_PTRLT: truth = ua < ub; break;
            case TC_PTRLE: truth = ua <= ub; break;
            case TC_PTRGT: truth = ua > ub; break;
            default: truth = ua >= ub; break;
            }
            result.bits = truth ? 1u : 0u;
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_SIZEOF: {
            TcVmValue result;
            int ok = 0;
            result.bits = tc_vm_type_size(vm, instr->type, &ok);
            if (!ok) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_FUNCADDR: {
            TcVmValue result;
            if (instr->args[0].value.kind != TC_OP_FUNC ||
                instr->args[0].value.func >= vm->ir->nfuncs) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            result.bits = tc_vm_func_address(vm, instr->args[0].value.func) &
                          vm->addr_mask;
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_CALL: {
            const TcFunc *callee;
            TcVmValue ret_value;
            int has_ret = 0;
            TcVmFlow flow;
            if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC ||
                instr->args[0].value.func >= vm->ir->nfuncs) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (!call_depth_ok(vm, &instr->span)) {
                return TC_VM_FLOW_FAIL;
            }
            callee = &vm->ir->funcs[instr->args[0].value.func];
            flow = do_call(vm, frame_index, callee, instr, 1, 0, &ret_value,
                           &has_ret);
            if (flow == TC_VM_FLOW_FAIL || flow == TC_VM_FLOW_PAUSE) {
                return flow;
            }
            if (instr->dst.kind != TC_OP_DISCARD && has_ret) {
                frame = &vm->frames[frame_index];
                if (!tc_vm_write_operand(vm, frame, &instr->dst,
                                         &ret_value)) {
                    if (ret_value.ptr != NULL) {
                        tc_vm_free(vm, ret_value.ptr);
                    }
                    return TC_VM_FLOW_FAIL;
                }
            }
            if (ret_value.ptr != NULL) {
                tc_vm_free(vm, ret_value.ptr);
            }
            break;
        }
        case TC_CALL_INDIRECT: {
            TcVmValue pointer;
            if (instr->nargs < 1) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            uint32_t func_id = 0;
            const TcFunc *callee;
            TcVmValue ret_value;
            int has_ret = 0;
            TcVmFlow flow;
            if (!read_value(vm, frame, &instr->args[0].value, &pointer,
                            NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if ((pointer.bits & vm->addr_mask) == 0) {
                tc_vm_trap(vm, &instr->span, TC_EK_NULL_FUNCTION_POINTER);
                return TC_VM_FLOW_FAIL;
            }
            if (!tc_vm_func_from_address(vm, pointer.bits & vm->addr_mask,
                                         &func_id)) {
                tc_vm_trap(vm, &instr->span, TC_EK_ADDR_UNAVAILABLE);
                return TC_VM_FLOW_FAIL;
            }
            if (!call_depth_ok(vm, &instr->span)) {
                return TC_VM_FLOW_FAIL;
            }
            callee = &vm->ir->funcs[func_id];
            flow = do_call(vm, frame_index, callee, instr, 1, 1, &ret_value,
                           &has_ret);
            if (flow == TC_VM_FLOW_FAIL || flow == TC_VM_FLOW_PAUSE) {
                return flow;
            }
            if (instr->dst.kind != TC_OP_DISCARD && has_ret) {
                frame = &vm->frames[frame_index];
                if (!tc_vm_write_operand(vm, frame, &instr->dst,
                                         &ret_value)) {
                    if (ret_value.ptr != NULL) {
                        tc_vm_free(vm, ret_value.ptr);
                    }
                    return TC_VM_FLOW_FAIL;
                }
            }
            if (ret_value.ptr != NULL) {
                tc_vm_free(vm, ret_value.ptr);
            }
            break;
        }
        case TC_FUNCEQ:
        case TC_FUNCNE: {
            TcVmValue a;
            TcVmValue b;
            TcVmValue result;
            int truth;
            if (!read_value(vm, frame, &instr->args[0].value, &a, NULL) ||
                !read_value(vm, frame, &instr->args[1].value, &b, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            truth = (a.bits & vm->addr_mask) == (b.bits & vm->addr_mask);
            if (instr->op == TC_FUNCNE) {
                truth = !truth;
            }
            result.bits = truth ? 1u : 0u;
            if (!write_result(vm, frame, instr, &result)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_MAKE:
            if (!do_make(vm, frame, instr)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        case TC_COPY: {
            TcVmValue value;
            if (!read_value(vm, frame, &instr->args[0].value, &value, NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (!write_result(vm, frame, instr, &value)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_BR: {
            if (instr->args[0].value.kind != TC_OP_LABEL ||
                instr->args[0].value.label >= unit->nblocks) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            block = instr->args[0].value.label;
            pc = 0;
            jumped = 1;
            break;
        }
        case TC_BR_IF: {
            TcVmValue condition;
            if (!read_value(vm, frame, &instr->args[0].value, &condition,
                            NULL)) {
                return TC_VM_FLOW_FAIL;
            }
            if (condition.bits != 0) {
                if (instr->args[1].value.kind != TC_OP_LABEL ||
                    instr->args[1].value.label >= unit->nblocks) {
                    tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    return TC_VM_FLOW_FAIL;
                }
                block = instr->args[1].value.label;
                pc = 0;
                jumped = 1;
            }
            break;
        }
        case TC_SWITCH: {
            TcVmValue selector;
            const TcType *type;
            uint64_t index;
            uint32_t target;
            if (instr->ntargets == 0 ||
                !read_value(vm, frame, &instr->args[0].value, &selector,
                            &type) ||
                type == NULL) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (tc_type_is_signed_integer(type)) {
                unsigned width = tc_sem_type_width(type, vm->W);
                uint64_t sign = UINT64_C(1) << (width - 1u);
                uint64_t masked = selector.bits & tc_sem_mask(width);
                if ((masked & sign) != 0) {
                    index = instr->ntargets - 1u;
                } else {
                    index = masked;
                }
            } else {
                index = selector.bits & tc_sem_mask(tc_sem_type_width(type,
                                                                      vm->W));
            }
            if (index >= instr->ntargets - 1u) {
                target = instr->switch_targets[instr->ntargets - 1u];
            } else {
                target = instr->switch_targets[index];
            }
            if (target >= unit->nblocks) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            block = target;
            pc = 0;
            jumped = 1;
            break;
        }
        case TC_RETURN: {
            TcVmValue value;
            if (instr->nargs == 0) {
                return TC_VM_FLOW_RETURN;
            }
            if (ret == NULL ||
                !read_value(vm, frame, &instr->args[0].value, &value,
                            NULL)) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                return TC_VM_FLOW_FAIL;
            }
            if (tc_vm_operand_type(vm, frame, &instr->args[0].value) != NULL &&
                tc_vm_operand_type(vm, frame, &instr->args[0].value)->tag ==
                    TC_TY_STRUCT) {
                int ok = 0;
                const TcType *type =
                    tc_vm_operand_type(vm, frame, &instr->args[0].value);
                uint64_t size = tc_vm_type_size(vm, type, &ok);
                uint8_t *copy;
                if (!ok || size > SIZE_MAX) {
                    tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    return TC_VM_FLOW_FAIL;
                }
                copy = (uint8_t *)tc_vm_alloc(vm, (size_t)size);
                if (copy == NULL) {
                    return TC_VM_FLOW_FAIL;
                }
                memcpy(copy, value.ptr, (size_t)size);
                ret->ptr = copy;
            } else {
                ret->bits = value.bits;
            }
            if (has_ret != NULL) {
                *has_ret = 1;
            }
            return TC_VM_FLOW_RETURN;
        }
        case TC_TRAP:
            tc_vm_trap(vm, &instr->span, TC_EK_TRAP);
            return TC_VM_FLOW_FAIL;
        case TC_READ: {
            int read_status = do_read(vm, frame, instr);
            if (read_status == -2) {
                debug_save(vm, frame_index, block, pc);
                vm->debug_stop |= TC_VM_DEBUG_INTERRUPT;
                return TC_VM_FLOW_PAUSE;
            }
            if (read_status <= 0) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        }
        case TC_WRITE:
        case TC_WRITELN:
            if (!do_write(vm, frame, instr, instr->op == TC_WRITELN)) {
                return TC_VM_FLOW_FAIL;
            }
            break;
        default:
            tc_vm_impl(vm, TC_IMPL_INTERNAL);
            return TC_VM_FLOW_FAIL;
        }
        if (!jumped) {
            pc++;
        }
        if (vm->debug != NULL) {
            frame = &vm->frames[frame_index];
            frame->debug_counted = 0;
            if (vm->debug_single || vm->debug_stop != 0u) {
                if (!debug_save_next(vm, frame_index, block, pc)) {
                    return TC_VM_FLOW_FAIL;
                }
                return TC_VM_FLOW_PAUSE;
            }
        }
    }
}

static int complete_paused_call(TcVm *vm, uint32_t callee_frame,
                                const TcVmValue *returned, int has_returned) {
    TcVmFrame *caller;
    TcVmFrame *callee;
    const TcInstr *instr;
    const TcFunc *func;
    if (callee_frame == 0u || callee_frame >= vm->nframes) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    caller = &vm->frames[callee_frame - 1u];
    callee = &vm->frames[callee_frame];
    instr = caller->pending_call;
    if (caller->pending_callee != callee_frame || instr == NULL ||
        callee->func_id >= vm->ir->nfuncs) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    func = &vm->ir->funcs[callee->func_id];
    vm->current_unit = caller->unit;
    vm->current_instr = instr;
    vm->current_span = instr->span;
    vm->current_bb = caller->resume_block;
    vm->current_insn = (uint32_t)caller->resume_pc;
    tc_vm_observe_call(vm, instr, func, 1);
    vm->depth--;
    tc_vm_pop_frame(vm);
    caller = &vm->frames[callee_frame - 1u];
    caller->pending_callee = UINT32_MAX;
    caller->pending_call = NULL;
    caller->resume_pc++;
    caller->has_resume = 1;
    caller->debug_counted = 0;
    if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID &&
        !has_returned) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (instr->dst.kind != TC_OP_DISCARD && has_returned) {
        if (!tc_vm_write_operand(vm, caller, &instr->dst, returned)) {
            if (returned->ptr != NULL) {
                tc_vm_free(vm, returned->ptr);
            }
            return 0;
        }
    }
    if (returned->ptr != NULL) {
        tc_vm_free(vm, returned->ptr);
    }
    return 1;
}

TcVmFlow tc_vm_execute_resume(TcVm *vm, uint32_t frame_index, int single) {
    vm->debug_single = single;
    vm->debug_stop = 0u;
    for (;;) {
        TcVmValue ret;
        int has_ret = 0;
        TcVmFlow flow;
        memset(&ret, 0, sizeof(ret));
        flow = tc_vm_execute(vm, frame_index, &ret, &has_ret);
        if (flow != TC_VM_FLOW_RETURN && flow != TC_VM_FLOW_END) {
            return flow;
        }
        if (frame_index == 0u) {
            return flow;
        }
        if (!complete_paused_call(vm, frame_index, &ret, has_ret)) {
            return TC_VM_FLOW_FAIL;
        }
        frame_index--;
        vm->debug_frame = frame_index;
        if (vm->debug_single || vm->debug_stop != 0u) {
            TcVmFrame *caller = &vm->frames[frame_index];
            if (!debug_save_next(vm, frame_index, caller->resume_block,
                                 caller->resume_pc)) {
                return TC_VM_FLOW_FAIL;
            }
            return TC_VM_FLOW_PAUSE;
        }
    }
}

const char *tc_vm_status_symbol(TcVmStatus status) {
    switch (status) {
    case TC_VM_STATUS_OK:
        return "tc_vm_ok";
    case TC_VM_STATUS_TRAP:
        return "tc_vm_trap";
    case TC_VM_STATUS_IMPL:
    default:
        return "tc_vm_impl_error";
    }
}

static TcVmStatus tc_vm_run_internal(const TcIR *ir,
                                     const TcPlatformProfile *profile,
                                     TcVmResult *out_result,
                                     TcImplError *out_impl,
                                     const TcVmObserver *observer,
                                     TcVmStats *out_stats) {
    TcVm vm;
    TcVmStatus status;
    uint32_t top_unit = UINT32_MAX;
    uint32_t frame_index = 0;
    TcVmValue ret_value;
    int has_ret = 0;
    TcVmFlow flow;
    size_t i;

    memset(&ret_value, 0, sizeof(ret_value));
    memset(&vm, 0, sizeof(vm));
    if (!tc_vm_setup(&vm, ir, profile, out_result, out_impl)) {
        status = TC_VM_STATUS_IMPL;
        tc_vm_teardown(&vm);
        return status;
    }
    if (observer != NULL) {
        vm.observer = observer;
        if (observer->trace != NULL && observer->linemap == NULL) {
            tc_vm_impl(&vm, TC_IMPL_INTERNAL);
            tc_vm_teardown(&vm);
            return TC_VM_STATUS_IMPL;
        }
        if (observer->stats) {
            if (out_stats == NULL) {
                tc_vm_impl(&vm, TC_IMPL_INTERNAL);
                tc_vm_teardown(&vm);
                return TC_VM_STATUS_IMPL;
            }
            memset(out_stats, 0, sizeof(*out_stats));
            vm.stats = out_stats;
            out_stats->region_peak[TC_REG_CODE] =
                ((uint64_t)ir->nfuncs + 1u) * 16u;
            for (i = 0u; i < ir->nobjects; i++) {
                const TcObject *object = &ir->objects[i];
                if (object->region == TC_REG_STATIC &&
                    object->offset + object->size >
                        out_stats->region_peak[TC_REG_STATIC]) {
                    out_stats->region_peak[TC_REG_STATIC] =
                        object->offset + object->size;
                }
            }
        }
    }
    for (i = 0; i < ir->nunits; i++) {
        if (ir->units[i].func_id == TC_UNIT_TOP_LEVEL) {
            top_unit = (uint32_t)i;
            break;
        }
    }
    if (top_unit == UINT32_MAX) {
        tc_vm_impl(&vm, TC_IMPL_INTERNAL);
        status = TC_VM_STATUS_IMPL;
        tc_vm_teardown(&vm);
        return status;
    }
    if (!tc_vm_push_frame(&vm, &ir->units[top_unit], TC_UNIT_TOP_LEVEL,
                          &frame_index)) {
        status = TC_VM_STATUS_IMPL;
        tc_vm_teardown(&vm);
        return status;
    }
    tc_vm_trace_header(&vm);
    flow = tc_vm_execute(&vm, frame_index, &ret_value, &has_ret);
    if (flow == TC_VM_FLOW_FAIL) {
        status = out_result != NULL && out_result->trapped
                     ? TC_VM_STATUS_TRAP
                     : TC_VM_STATUS_IMPL;
    } else if (flow == TC_VM_FLOW_END) {
        status = TC_VM_STATUS_OK;
    } else {
        /* `return` is statically rejected at #program top level. */
        tc_vm_impl(&vm, TC_IMPL_INTERNAL);
        status = TC_VM_STATUS_IMPL;
    }
    if (ret_value.ptr != NULL) {
        tc_vm_free(&vm, ret_value.ptr);
    }
    if (vm.impl != TC_IMPL_OK) {
        status = TC_VM_STATUS_IMPL;
    }
    if (vm.stats != NULL) {
        vm.stats->trapped = out_result != NULL && out_result->trapped;
        vm.stats->first_trap = out_result != NULL ? out_result->kind
                                                  : (TcErrorKind)0;
    }
    tc_vm_trace_footer(&vm, status == TC_VM_STATUS_OK ? "ok"
                               : status == TC_VM_STATUS_TRAP ? "trap"
                                                             : "error");
    if (vm.trace_failed) {
        tc_vm_impl(&vm, TC_IMPL_IO);
    } else if (vm.count_overflow) {
        tc_vm_impl(&vm, TC_IMPL_INTERNAL);
    }
    if (vm.impl != TC_IMPL_OK) {
        status = TC_VM_STATUS_IMPL;
    }
    tc_vm_teardown(&vm);
    if (out_impl != NULL && status == TC_VM_STATUS_IMPL &&
        *out_impl == TC_IMPL_OK) {
        *out_impl = TC_IMPL_INTERNAL;
    }
    return status;
}

TcVmStatus tc_vm_run(const TcIR *ir, const TcPlatformProfile *profile,
                     TcVmResult *out_result, TcImplError *out_impl) {
    return tc_vm_run_internal(ir, profile, out_result, out_impl, NULL, NULL);
}

TcVmStatus tc_vm_run_observed(const TcIR *ir,
                              const TcPlatformProfile *profile,
                              TcVmResult *out_result, TcImplError *out_impl,
                              const TcVmObserver *observer,
                              TcVmStats *out_stats) {
    return tc_vm_run_internal(ir, profile, out_result, out_impl, observer,
                              out_stats);
}
