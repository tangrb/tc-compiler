/* TC 0.0.46 reference VM: regions, objects, memory and pointer state.
 *
 * Address space layout comes from the bound profile.  Storage objects are the
 * single boundary/lifetime fact source (standard §3.12): runtime objects are
 * cloned from `TcIR.objects` and extended with field sub-objects, with frame
 * object clones appended and truncated in stack order.  Mandatory traps
 * (null, write-protected, code/device/outside, pointer diff overflow) are
 * always executed; `checked_build` additionally traps out-of-object UB
 * accesses with TC_RE_ADDR_UNAVAILABLE (design §13.3).
 */
#include "vm/internal.h"

#include <string.h>

#include "runtime/semantics.h"

/* ------------------------------------------------------------------------- */
/* Allocation and failure helpers.                                           */
/* ------------------------------------------------------------------------- */

void *tc_vm_alloc(TcVm *vm, size_t size) {
    void *ptr;
    if (vm->alloc == NULL || vm->alloc->alloc == NULL) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return NULL;
    }
    ptr = vm->alloc->alloc(vm->alloc->ctx, size == 0 ? 1u : size);
    if (ptr == NULL) {
        tc_vm_impl(vm, TC_IMPL_OOM);
    }
    return ptr;
}

void *tc_vm_calloc(TcVm *vm, size_t count, size_t size) {
    void *ptr;
    size_t total;
    if (count != 0 && size > SIZE_MAX / count) {
        tc_vm_impl(vm, TC_IMPL_OOM);
        return NULL;
    }
    total = count * size;
    ptr = tc_vm_alloc(vm, total == 0 ? 1u : total);
    if (ptr != NULL) {
        memset(ptr, 0, total == 0 ? 1u : total);
    }
    return ptr;
}

void tc_vm_free(TcVm *vm, void *ptr) {
    if (ptr != NULL && vm->alloc != NULL && vm->alloc->free != NULL) {
        vm->alloc->free(vm->alloc->ctx, ptr);
    }
}

int tc_vm_impl(TcVm *vm, TcImplError error) {
    if (vm->impl == TC_IMPL_OK) {
        vm->impl = error;
    }
    if (vm->out_impl != NULL && vm->impl != TC_IMPL_OK) {
        *vm->out_impl = vm->impl;
    }
    return 0;
}

int tc_vm_trap(TcVm *vm, const TcSpan *span, TcErrorKind kind) {
    const TcSpan *where = span != NULL ? span : &vm->current_span;
    if (vm->result != NULL && !vm->result->trapped) {
        vm->result->trapped = 1;
        vm->result->kind = kind;
        vm->result->span = *where;
        if (vm->profile->trap != NULL && vm->profile->trap->trap != NULL) {
            vm->profile->trap->trap(vm->profile->trap->ctx, kind,
                                    (int)where->line);
        }
    }
    return 0;
}

int tc_vm_failed(const TcVm *vm) {
    return (vm->result != NULL && vm->result->trapped) ||
           vm->impl != TC_IMPL_OK;
}

/* ------------------------------------------------------------------------- */
/* Type size.                                                                */
/* ------------------------------------------------------------------------- */

uint64_t tc_vm_type_size(TcVm *vm, const TcType *type, int *ok) {
    if (ok != NULL) {
        *ok = 0;
    }
    if (type == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_STRUCT) {
        uint32_t id = type->struct_.struct_id;
        if (vm->ir == NULL || id >= vm->ir->nstructs) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return vm->ir->structs[id].size;
    }
    {
        unsigned width = tc_sem_type_width(type, vm->W);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

static const TcStructDef *struct_def_for(TcVm *vm, const TcType *type) {
    uint32_t id;
    if (type == NULL || type->tag != TC_TY_STRUCT) {
        return NULL;
    }
    id = type->struct_.struct_id;
    if (id >= vm->ir->nstructs) {
        return NULL;
    }
    return &vm->ir->structs[id];
}

/* ------------------------------------------------------------------------- */
/* Region lookup and host backing.                                           */
/* ------------------------------------------------------------------------- */

static int region_of_address(TcVm *vm, uint64_t address, TcRegionKind *out) {
    unsigned region;
    for (region = 0; region < (unsigned)TC_REGION_COUNT; region++) {
        const TcRegionDesc *desc = &vm->profile->region[region];
        if (address >= desc->base && address - desc->base < desc->size) {
            *out = (TcRegionKind)region;
            return 1;
        }
    }
    return 0;
}

static int frame_index_for_address(TcVm *vm, uint64_t address,
                                   size_t *out_index) {
    size_t i = vm->nframes;
    while (i > 0) {
        TcVmFrame *frame;
        i--;
        frame = &vm->frames[i];
        if (!frame->valid) {
            continue;
        }
        if (address >= frame->base &&
            address - frame->base < (uint64_t)frame->mem_size) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

/* Host pointer for a live frame/static address without access checking. */
static uint8_t *storage_host(TcVm *vm, uint64_t address, uint64_t size,
                             int *ok) {
    TcRegionKind region;
    *ok = 0;
    if (!region_of_address(vm, address, &region)) {
        return NULL;
    }
    if (region == TC_REG_FRAME) {
        size_t index;
        if (!frame_index_for_address(vm, address, &index)) {
            return NULL;
        }
        if (address - vm->frames[index].base + size >
            (uint64_t)vm->frames[index].mem_size) {
            return NULL;
        }
        *ok = 1;
        return vm->frames[index].mem + (address - vm->frames[index].base);
    }
    if (region == TC_REG_STATIC) {
        if (address - vm->static_base + size > vm->static_size) {
            return NULL;
        }
        *ok = 1;
        return vm->static_mem + (address - vm->static_base);
    }
    if (region == TC_REG_DEVICE) {
        if (address - vm->device_base + size > vm->device_size) {
            return NULL;
        }
        *ok = 1;
        return vm->device_mem + (address - vm->device_base);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Object table.                                                             */
/* ------------------------------------------------------------------------- */

static int append_object(TcVm *vm, TcVmObject object, uint32_t *out_index) {
    if (vm->nobjects == vm->cap_objects) {
        size_t cap = vm->cap_objects == 0 ? 32u : vm->cap_objects * 2u;
        TcVmObject *fresh = (TcVmObject *)tc_vm_calloc(vm, cap,
                                                       sizeof(TcVmObject));
        if (fresh == NULL) {
            return 0;
        }
        if (vm->nobjects != 0) {
            memcpy(fresh, vm->objects,
                   vm->nobjects * sizeof(TcVmObject));
            tc_vm_free(vm, vm->objects);
        }
        vm->objects = fresh;
        vm->cap_objects = cap;
    }
    vm->objects[vm->nobjects] = object;
    if (out_index != NULL) {
        *out_index = (uint32_t)vm->nobjects;
    }
    vm->nobjects++;
    return 1;
}

static int add_field_objects(TcVm *vm, uint64_t start, const TcType *type,
                             int writable, uint32_t frame) {
    const TcStructDef *def = struct_def_for(vm, type);
    TcRegionKind region;
    size_t i;
    if (def == NULL) {
        if (type != NULL && type->tag == TC_TY_STRUCT) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        return 1;
    }
    if (frame == UINT32_MAX) {
        region = TC_REG_STATIC;
    } else {
        region = TC_REG_FRAME;
    }
    for (i = 0; i < def->nfields; i++) {
        const TcField *field = &def->fields[i];
        TcVmObject child;
        uint32_t index;
        int ok = 0;
        uint64_t size = tc_vm_type_size(vm, field->type, &ok);
        if (!ok) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        memset(&child, 0, sizeof(child));
        child.ir_object = UINT32_MAX;
        child.region = region;
        child.start = start + field->offset;
        child.size = size;
        child.type = field->type;
        child.writable = writable;
        child.frame = frame;
        if (!append_object(vm, child, &index)) {
            return 0;
        }
        if (!add_field_objects(vm, child.start, field->type, writable,
                               frame)) {
            return 0;
        }
    }
    return 1;
}

/* Best object match for an address: a live object containing it wins over an
 * object that merely ends at it, then the requested element type, then the
 * innermost object.  Prioritizing containment keeps a byte access into a
 * containing struct's padding legal even when a preceding field's one-past
 * address coincides with it; one-past is only used when no live object
 * contains the address. */
static TcVmObject *find_object(TcVm *vm, uint64_t address,
                               const TcType *prefer, int allow_one_past,
                               int *out_one_past) {
    TcVmObject *best = NULL;
    int best_live = 0;
    int best_type = 0;
    uint64_t best_start = 0;
    uint64_t best_size = 0;
    int best_past = 0;
    size_t i;
    if (out_one_past != NULL) {
        *out_one_past = 0;
    }
    for (i = 0; i < vm->nobjects; i++) {
        TcVmObject *object = &vm->objects[i];
        int live = address >= object->start &&
                   address - object->start < object->size;
        int one_past = allow_one_past && object->size != 0 &&
                       address == object->start + object->size;
        int type_match;
        int better;
        if (!live && !one_past) {
            continue;
        }
        type_match = prefer != NULL && object->type == prefer;
        better = best == NULL;
        if (!better && live != best_live) {
            better = live > best_live;
        } else if (!better && type_match != best_type) {
            better = type_match > best_type;
        } else if (!better && object->start != best_start) {
            better = object->start > best_start;
        } else if (!better && object->size != best_size) {
            better = object->size < best_size;
        }
        if (better) {
            best = object;
            best_live = live;
            best_type = type_match;
            best_start = object->start;
            best_size = object->size;
            best_past = one_past;
        }
    }
    if (best == NULL) {
        return NULL;
    }
    if (out_one_past != NULL) {
        *out_one_past = best_past;
    }
    return best;
}

/* Best object that fully contains an access range, or (when none) an object
 * whose one-past address matches.  Containment beats one-past so a byte access
 * into a containing struct's padding or across its fields is checked against
 * the containing object rather than a smaller sibling sub-object; the access
 * range must still fit the chosen object. */
static TcVmObject *find_access_object(TcVm *vm, uint64_t address,
                                      uint64_t size, const TcType *prefer,
                                      int *out_contained) {
    TcVmObject *best = NULL;
    int best_contained = 0;
    int best_type = 0;
    uint64_t best_start = 0;
    uint64_t best_size = 0;
    size_t i;
    if (out_contained != NULL) {
        *out_contained = 0;
    }
    for (i = 0; i < vm->nobjects; i++) {
        TcVmObject *object = &vm->objects[i];
        uint64_t relative;
        int contained;
        int one_past;
        int type_match;
        int better;
        if (address < object->start) {
            continue;
        }
        relative = address - object->start;
        if (relative > object->size) {
            continue;
        }
        contained = relative <= object->size &&
                    size <= object->size - relative;
        one_past = address == object->start + object->size;
        if (!contained && !one_past) {
            continue;
        }
        type_match = prefer != NULL && object->type == prefer;
        better = best == NULL;
        if (!better && contained != best_contained) {
            better = contained > best_contained;
        } else if (!better && type_match != best_type) {
            better = type_match > best_type;
        } else if (!better && object->start != best_start) {
            better = object->start > best_start;
        } else if (!better && object->size != best_size) {
            better = object->size < best_size;
        }
        if (better) {
            best = object;
            best_contained = contained;
            best_type = type_match;
            best_start = object->start;
            best_size = object->size;
        }
    }
    if (out_contained != NULL) {
        *out_contained = best_contained;
    }
    return best;
}

/* Any object whose live/one-past interval keeps a pointer-arithmetic result
 * inside it.  Considering every candidate avoids rejecting a defined
 * operation merely because the same address also belongs to an adjacent
 * object; pointer provenance is not tracked (see CONTRACTS.md). */
static int ptr_arith_in_bounds(TcVm *vm, uint64_t pointer, uint64_t result,
                               uint64_t count, int add) {
    size_t i;
    for (i = 0; i < vm->nobjects; i++) {
        TcVmObject *object = &vm->objects[i];
        int live;
        int one_past;
        if (pointer < object->start) {
            continue;
        }
        live = pointer - object->start < object->size;
        one_past = pointer == object->start + object->size;
        if (!live && !one_past) {
            continue;
        }
        if (one_past && add && count != 0) {
            continue;
        }
        if (result >= object->start &&
            result <= object->start + object->size) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Checked-build defined-byte tracking.                                      */
/* ------------------------------------------------------------------------- */

static int vm_defined_range(TcVm *vm, uint64_t address, uint8_t **out_shadow,
                            uint64_t *out_index) {
    TcRegionKind region;
    size_t frame_index;
    if (!vm->profile->checked_build || address == 0) {
        return 0;
    }
    if (!region_of_address(vm, address, &region)) {
        return 0;
    }
    if (region == TC_REG_STATIC) {
        uint64_t index = address - vm->static_base;
        if (vm->static_defined == NULL || index >= vm->static_size) {
            return 0;
        }
        *out_shadow = vm->static_defined;
        *out_index = index;
        return 1;
    }
    if (region == TC_REG_FRAME) {
        if (!frame_index_for_address(vm, address, &frame_index)) {
            return 0;
        }
        {
            uint64_t index = address - vm->frames[frame_index].base;
            if (vm->frames[frame_index].defined == NULL ||
                index >= (uint64_t)vm->frames[frame_index].defined_size) {
                return 0;
            }
            *out_shadow = vm->frames[frame_index].defined;
            *out_index = index;
            return 1;
        }
    }
    return 0;
}

static void vm_define_range(TcVm *vm, uint64_t address, uint64_t size) {
    uint8_t *shadow = NULL;
    uint64_t index = 0;
    uint64_t i;
    if (!vm->profile->checked_build || size == 0) {
        return;
    }
    if (!vm_defined_range(vm, address, &shadow, &index)) {
        return;
    }
    for (i = 0; i < size; i++) {
        uint64_t bit = index + i;
        shadow[bit >> 3] = (uint8_t)(shadow[bit >> 3] | (1u << (bit & 7u)));
    }
}

static int vm_range_defined(TcVm *vm, uint64_t address, uint64_t size) {
    uint8_t *shadow = NULL;
    uint64_t index = 0;
    uint64_t i;
    if (!vm->profile->checked_build || size == 0) {
        return 1;
    }
    if (!vm_defined_range(vm, address, &shadow, &index)) {
        return 1;
    }
    for (i = 0; i < size; i++) {
        uint64_t bit = index + i;
        if ((shadow[bit >> 3] & (1u << (bit & 7u))) == 0) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Access checking.                                                          */
/* ------------------------------------------------------------------------- */

static int device_allows(TcVm *vm, int write, int volatile_access) {
    switch (vm->profile->device) {
    case TC_DEV_REJECT:
        return 0;
    case TC_DEV_READ:
        return !write;
    case TC_DEV_READWRITE:
        return 1;
    case TC_DEV_VOLATILE:
        return volatile_access != 0;
    default:
        return 0;
    }
}

/* Check an access and return its host pointer.  address != 0 is assumed. */
static int check_access(TcVm *vm, uint64_t address, uint64_t size,
                        const TcType *type, int write, int volatile_access,
                        uint8_t **out_host) {
    TcRegionKind region;
    uint8_t *host = NULL;
    int ok = 0;

    if (!region_of_address(vm, address, &region)) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    if (region == TC_REG_DEVICE) {
        if (!device_allows(vm, write, volatile_access)) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
        host = storage_host(vm, address, size, &ok);
        if (!ok) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
        *out_host = host;
        return 1;
    }
    if (region == TC_REG_CODE || region == TC_REG_HEAP) {
        /* Code is not a language object; this implementation creates no heap
         * objects in the core (the allocation API is a runtime-library
         * concern).  Both are ADDR_UNAVAILABLE rather than an unchecked host
         * access. */
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    if (region == TC_REG_FRAME) {
        size_t frame_index;
        if (!frame_index_for_address(vm, address, &frame_index)) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
    }
    host = storage_host(vm, address, size, &ok);
    if (!ok) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    {
        int contained = 0;
        TcVmObject *object = find_access_object(vm, address, size, type,
                                                &contained);
        if (object != NULL && contained) {
            if (write && !object->writable) {
                return tc_vm_trap(vm, NULL, TC_EK_WRITE_PROTECTED);
            }
        } else if (vm->profile->checked_build) {
            /* One-past dereference or an access outside every object is UB;
             * only a checked build traps. */
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
    }
    *out_host = host;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Raw host value encoding.                                                  */
/* ------------------------------------------------------------------------- */

static uint64_t load_bits(const uint8_t *bytes, unsigned size, int little) {
    uint64_t value = 0;
    unsigned i;
    if (little) {
        for (i = 0; i < size; i++) {
            value |= (uint64_t)bytes[i] << (8u * i);
        }
    } else {
        for (i = 0; i < size; i++) {
            value = (value << 8) | bytes[i];
        }
    }
    return value;
}

static void store_bits(uint8_t *bytes, unsigned size, int little,
                       uint64_t value) {
    unsigned i;
    if (little) {
        for (i = 0; i < size; i++) {
            bytes[i] = (uint8_t)((value >> (8u * i)) & 0xffu);
        }
    } else {
        for (i = 0; i < size; i++) {
            bytes[size - 1u - i] = (uint8_t)((value >> (8u * i)) & 0xffu);
        }
    }
}

static int host_read_value(TcVm *vm, const uint8_t *host, const TcType *type,
                           TcVmValue *out) {
    int ok = 0;
    uint64_t size = tc_vm_type_size(vm, type, &ok);
    if (!ok) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (type->tag == TC_TY_STRUCT) {
        out->ptr = (uint8_t *)host;
        out->bits = 0;
        return 1;
    }
    out->ptr = NULL;
    if (type->tag == TC_TY_BOOL) {
        out->bits = tc_sem_bool_from_byte(host[0]);
        return 1;
    }
    out->bits = load_bits(host, (unsigned)size, vm->profile->little_endian);
    out->bits &= tc_sem_mask((unsigned)size * 8u);
    return 1;
}

int tc_vm_write_host_value(TcVm *vm, uint8_t *host, const TcType *type,
                           const TcVmValue *value) {
    int ok = 0;
    uint64_t size = tc_vm_type_size(vm, type, &ok);
    if (!ok) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (type->tag == TC_TY_STRUCT) {
        if (value->ptr == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (size != 0) {
            memmove(host, value->ptr, (size_t)size);
        }
        return 1;
    }
    if (type->tag == TC_TY_BOOL) {
        host[0] = value->bits != 0 ? 1u : 0u;
        return 1;
    }
    store_bits(host, (unsigned)size, vm->profile->little_endian,
               value->bits);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Operand typing and storage.                                               */
/* ------------------------------------------------------------------------- */

/* Field storage that also returns the target address of the field. */
static const TcType *field_storage(TcVm *vm, TcVmFrame *frame,
                                   const TcOperand *operand,
                                   uint8_t **out_host, uint64_t *out_addr) {
    const TcType *type;
    uint8_t *host;
    uint8_t *base_host = NULL;
    uint64_t base_address = 0;
    size_t i;
    int ok = 0;

    if (operand->kind != TC_OP_FIELD ||
        operand->field.base >= frame->unit->nvregs) {
        return NULL;
    }
    type = frame->unit->vreg_types[operand->field.base];
    if (frame->vreg_obj[operand->field.base] != UINT32_MAX) {
        base_address = vm->objects[frame->vreg_obj[operand->field.base]].start;
        base_host = storage_host(vm, base_address, 0, &ok);
        if (!ok) {
            return NULL;
        }
    } else {
        base_host = frame->regs[operand->field.base].ptr;
    }
    if (base_host == NULL) {
        return NULL;
    }
    host = base_host;
    for (i = 0; i < operand->field.nfields; i++) {
        const TcStructDef *def = struct_def_for(vm, type);
        const TcField *field;
        if (def == NULL || operand->field.fields[i] >= def->nfields) {
            return NULL;
        }
        field = &def->fields[operand->field.fields[i]];
        host += field->offset;
        type = field->type;
    }
    if (out_host != NULL) {
        *out_host = host;
    }
    if (out_addr != NULL) {
        if (base_address == 0) {
            *out_addr = 0;
        } else {
            *out_addr = base_address + (uint64_t)(host - base_host);
        }
    }
    return type;
}

const TcType *tc_vm_operand_type(TcVm *vm, TcVmFrame *frame,
                                 const TcOperand *operand) {
    if (operand == NULL || frame == NULL) {
        return NULL;
    }
    switch (operand->kind) {
    case TC_OP_VREG:
        if (operand->vreg >= frame->unit->nvregs) {
            return NULL;
        }
        return frame->unit->vreg_types[operand->vreg];
    case TC_OP_FIELD: {
        const TcType *type;
        if (operand->field.base >= frame->unit->nvregs) {
            return NULL;
        }
        type = frame->unit->vreg_types[operand->field.base];
        {
            size_t i;
            for (i = 0; i < operand->field.nfields; i++) {
                const TcStructDef *def = struct_def_for(vm, type);
                const TcField *field;
                if (def == NULL || operand->field.fields[i] >= def->nfields) {
                    return NULL;
                }
                field = &def->fields[operand->field.fields[i]];
                type = field->type;
            }
        }
        return type;
    }
    case TC_OP_CONST:
        return operand->konst.type;
    default:
        return NULL;
    }
}

int tc_vm_read_operand(TcVm *vm, TcVmFrame *frame, const TcOperand *operand,
                       TcVmValue *out, const TcType **out_type) {
    const TcType *type;
    if (operand == NULL || out == NULL || frame == NULL) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    memset(out, 0, sizeof(*out));
    switch (operand->kind) {
    case TC_OP_VREG: {
        const TcType *vtype = tc_vm_operand_type(vm, frame, operand);
        if (vtype == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (out_type != NULL) {
            *out_type = vtype;
        }
        if (frame->vreg_obj[operand->vreg] != UINT32_MAX) {
            TcVmObject *object =
                &vm->objects[frame->vreg_obj[operand->vreg]];
            int ok = 0;
            uint8_t *host = storage_host(vm, object->start, object->size, &ok);
            if (!ok) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            return host_read_value(vm, host, vtype, out);
        }
        if (vtype->tag == TC_TY_STRUCT) {
            out->ptr = frame->regs[operand->vreg].ptr;
            if (out->ptr == NULL) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            return 1;
        }
        out->bits = frame->regs[operand->vreg].bits &
                    tc_sem_mask(tc_sem_type_width(vtype, vm->W));
        return 1;
    }
    case TC_OP_FIELD: {
        uint8_t *host = NULL;
        type = field_storage(vm, frame, operand, &host, NULL);
        if (type == NULL || host == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (out_type != NULL) {
            *out_type = type;
        }
        return host_read_value(vm, host, type, out);
    }
    case TC_OP_CONST:
        type = operand->konst.type;
        if (type == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (out_type != NULL) {
            *out_type = type;
        }
        if (type->tag == TC_TY_STRUCT) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        out->bits = operand->konst.bits &
                    tc_sem_mask(tc_sem_type_width(type, vm->W));
        return 1;
    default:
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
}

int tc_vm_write_operand(TcVm *vm, TcVmFrame *frame, const TcOperand *operand,
                        const TcVmValue *value) {
    const TcType *type;
    if (operand == NULL || value == NULL || frame == NULL) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (operand->kind == TC_OP_DISCARD) {
        return 1;
    }
    switch (operand->kind) {
    case TC_OP_VREG: {
        type = tc_vm_operand_type(vm, frame, operand);
        if (type == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (frame->vreg_obj[operand->vreg] != UINT32_MAX) {
            TcVmObject *object =
                &vm->objects[frame->vreg_obj[operand->vreg]];
            int ok = 0;
            uint8_t *host = storage_host(vm, object->start, object->size, &ok);
            int ok_write;
            if (!ok) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            ok_write = tc_vm_write_host_value(vm, host, type, value);
            if (ok_write) {
                int type_ok = 0;
                uint64_t size = tc_vm_type_size(vm, type, &type_ok);
                if (type_ok) {
                    vm_define_range(vm, object->start, size);
                }
            }
            return ok_write;
        }
        if (type->tag == TC_TY_STRUCT) {
            int ok = 0;
            uint64_t size = tc_vm_type_size(vm, type, &ok);
            if (!ok || frame->regs[operand->vreg].ptr == NULL ||
                value->ptr == NULL) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            if (size != 0) {
                memmove(frame->regs[operand->vreg].ptr, value->ptr,
                        (size_t)size);
            }
            return 1;
        }
        frame->regs[operand->vreg].bits =
            value->bits & tc_sem_mask(tc_sem_type_width(type, vm->W));
        return 1;
    }
    case TC_OP_FIELD: {
        uint8_t *host = NULL;
        uint64_t address = 0;
        int ok_write;
        type = field_storage(vm, frame, operand, &host, &address);
        if (type == NULL || host == NULL) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        ok_write = tc_vm_write_host_value(vm, host, type, value);
        if (ok_write && address != 0) {
            int type_ok = 0;
            uint64_t size = tc_vm_type_size(vm, type, &type_ok);
            if (type_ok) {
                vm_define_range(vm, address, size);
            }
        }
        return ok_write;
    }
    default:
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
}

int tc_vm_operand_address(TcVm *vm, TcVmFrame *frame,
                          const TcOperand *operand, const TcType *target,
                          uint64_t *out_address) {
    uint64_t address;
    if (operand == NULL || out_address == NULL) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (operand->kind == TC_OP_VREG) {
        if (operand->vreg >= frame->unit->nvregs ||
            frame->vreg_obj[operand->vreg] == UINT32_MAX) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (tc_vm_operand_type(vm, frame, operand) != target) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        *out_address = vm->objects[frame->vreg_obj[operand->vreg]].start;
        return 1;
    }
    if (operand->kind == TC_OP_FIELD) {
        const TcType *type = field_storage(vm, frame, operand, NULL, &address);
        if (type == NULL || type != target) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (address == 0) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        *out_address = address;
        return 1;
    }
    return tc_vm_impl(vm, TC_IMPL_INTERNAL);
}

/* ------------------------------------------------------------------------- */
/* Load, store, memmove.                                                     */
/* ------------------------------------------------------------------------- */

int tc_vm_load_value(TcVm *vm, uint64_t address, const TcType *type,
                     int volatile_access, TcVmValue *out) {
    uint8_t *host = NULL;
    int ok = 0;
    uint64_t size;
    if (address == 0) {
        return tc_vm_trap(vm, NULL, TC_EK_NULL_POINTER);
    }
    size = tc_vm_type_size(vm, type, &ok);
    if (!ok || size == 0) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (!check_access(vm, address, size, type, 0, volatile_access, &host)) {
        return 0;
    }
    if (vm->profile->checked_build && !vm_range_defined(vm, address, size)) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    return host_read_value(vm, host, type, out);
}

int tc_vm_store_value(TcVm *vm, uint64_t address, const TcType *type,
                      const TcVmValue *value, int volatile_access) {
    uint8_t *host = NULL;
    int ok = 0;
    uint64_t size;
    if (address == 0) {
        return tc_vm_trap(vm, NULL, TC_EK_NULL_POINTER);
    }
    size = tc_vm_type_size(vm, type, &ok);
    if (!ok || size == 0) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (!check_access(vm, address, size, type, 1, volatile_access, &host)) {
        return 0;
    }
    {
        int ok_write = tc_vm_write_host_value(vm, host, type, value);
        if (ok_write) {
            vm_define_range(vm, address, size);
        }
        return ok_write;
    }
}

int tc_vm_memmove(TcVm *vm, uint64_t dst, uint64_t src, uint64_t count,
                  const TcType *type) {
    uint64_t size;
    unsigned __int128 bytes128;
    uint64_t bytes;
    uint8_t *dst_host = NULL;
    uint8_t *src_host = NULL;
    uint8_t *temp;
    int ok = 0;

    if (dst == 0 || src == 0) {
        return tc_vm_trap(vm, NULL, TC_EK_NULL_POINTER);
    }
    size = tc_vm_type_size(vm, type, &ok);
    if (!ok || size == 0) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    bytes128 = (unsigned __int128)count * size;
    if (bytes128 > (unsigned __int128)vm->addr_mask) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    bytes = (uint64_t)bytes128;
    if (bytes == 0) {
        int dst_past = 0;
        int src_past = 0;
        TcVmObject *dobj = find_object(vm, dst, type, 1, &dst_past);
        TcVmObject *sobj = find_object(vm, src, type, 1, &src_past);
        int dst_ok = dobj != NULL && !dst_past;
        int src_ok = sobj != NULL && !src_past;
        if ((!dst_ok || !src_ok) && vm->profile->checked_build) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
        return 1;
    }
    if (!check_access(vm, dst, bytes, type, 1, 0, &dst_host)) {
        return 0;
    }
    if (!check_access(vm, src, bytes, type, 0, 0, &src_host)) {
        return 0;
    }
    if (vm->profile->checked_build && !vm_range_defined(vm, src, bytes)) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    temp = (uint8_t *)tc_vm_alloc(vm, (size_t)bytes);
    if (temp == NULL) {
        return 0;
    }
    memcpy(temp, src_host, (size_t)bytes);
    memcpy(dst_host, temp, (size_t)bytes);
    tc_vm_free(vm, temp);
    vm_define_range(vm, dst, bytes);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Pointer arithmetic and ptrdiff.                                           */
/* ------------------------------------------------------------------------- */

int tc_vm_ptr_arith(TcVm *vm, uint64_t ptr, uint64_t count,
                    const TcType *element, int add, uint64_t *out) {
    TcRegionKind region;
    uint64_t size;
    unsigned __int128 delta;
    __int128 result;
    int ok = 0;

    if (ptr == 0) {
        return tc_vm_trap(vm, NULL, TC_EK_NULL_POINTER);
    }
    size = tc_vm_type_size(vm, element, &ok);
    if (!ok || size == 0) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    delta = (unsigned __int128)count * size;
    if (delta > (unsigned __int128)INT64_MAX) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    if (!region_of_address(vm, ptr, &region)) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    result = (__int128)ptr + (add ? (__int128)delta : -(__int128)delta);
    if (result < 0 || (unsigned __int128)result > (unsigned __int128)vm->addr_mask) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    if (region == TC_REG_DEVICE) {
        const TcRegionDesc *desc = &vm->profile->region[TC_REG_DEVICE];
        if (vm->profile->device == TC_DEV_REJECT) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
        if ((uint64_t)result < desc->base ||
            (uint64_t)result - desc->base > desc->size) {
            return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
        }
        *out = (uint64_t)result;
        return 1;
    }
    if (region == TC_REG_CODE) {
        return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
    }
    {
        int in_bounds = ptr_arith_in_bounds(vm, ptr, (uint64_t)result, count,
                                            add);
        if (!in_bounds) {
            if (vm->profile->checked_build) {
                return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
            }
            /* Release UB: keep the address only when it stays inside a real
             * storage block; never fabricate an address outside the profile. */
            if (region == TC_REG_FRAME) {
                size_t frame_index;
                if (!frame_index_for_address(vm, (uint64_t)result,
                                             &frame_index)) {
                    return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
                }
            } else if (region == TC_REG_STATIC) {
                if ((uint64_t)result < vm->static_base ||
                    (uint64_t)result - vm->static_base >= vm->static_size) {
                    return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
                }
            } else {
                return tc_vm_trap(vm, NULL, TC_EK_ADDR_UNAVAILABLE);
            }
        }
    }
    *out = (uint64_t)result;
    return 1;
}

int tc_vm_ptrdiff(TcVm *vm, uint64_t a, uint64_t b, uint64_t *out) {
    __int128 diff = (__int128)a - (__int128)b;
    __int128 min = -((__int128)1 << (vm->W - 1u));
    __int128 max = ((__int128)1 << (vm->W - 1u)) - 1;
    if (diff < min || diff > max) {
        return tc_vm_trap(vm, NULL, TC_EK_PTRDIFF_OVERFLOW);
    }
    *out = (uint64_t)diff & vm->addr_mask;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Function addresses.                                                       */
/* ------------------------------------------------------------------------- */

uint64_t tc_vm_func_address(const TcVm *vm, uint32_t func_id) {
    return vm->profile->region[TC_REG_CODE].base +
           ((uint64_t)func_id + 1u) * 16u;
}

int tc_vm_func_from_address(const TcVm *vm, uint64_t address,
                            uint32_t *out_func) {
    uint64_t base = vm->profile->region[TC_REG_CODE].base;
    uint64_t offset;
    uint64_t index;
    if (address < base + 16u) {
        return 0;
    }
    offset = address - base;
    if (offset % 16u != 0) {
        return 0;
    }
    index = offset / 16u - 1u;
    if (index >= vm->ir->nfuncs) {
        return 0;
    }
    if (tc_vm_func_address(vm, (uint32_t)index) != address) {
        return 0;
    }
    *out_func = (uint32_t)index;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Frames.                                                                   */
/* ------------------------------------------------------------------------- */

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1u) / alignment * alignment;
}

static int grow_frames(TcVm *vm) {
    if (vm->nframes == vm->cap_frames) {
        size_t cap = vm->cap_frames == 0 ? 16u : vm->cap_frames * 2u;
        TcVmFrame *fresh =
            (TcVmFrame *)tc_vm_calloc(vm, cap, sizeof(TcVmFrame));
        if (fresh == NULL) {
            return 0;
        }
        if (vm->nframes != 0) {
            memcpy(fresh, vm->frames, vm->nframes * sizeof(TcVmFrame));
            tc_vm_free(vm, vm->frames);
        }
        vm->frames = fresh;
        vm->cap_frames = cap;
    }
    return 1;
}

int tc_vm_push_frame(TcVm *vm, const TcUnit *unit, uint32_t func_id,
                     uint32_t *out_index) {
    size_t unit_index;
    const TcVmUnitInfo *info;
    TcVmFrame *frame;
    uint64_t alignment = vm->profile->target->stack_align;
    uint64_t base;
    uint32_t *top_indices = NULL;
    size_t i;

    if (unit == NULL) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    unit_index = SIZE_MAX;
    for (i = 0; i < vm->nunits; i++) {
        if (&vm->ir->units[i] == unit) {
            unit_index = i;
            break;
        }
    }
    if (unit_index == SIZE_MAX) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    info = &vm->units[unit_index];
    base = align_up_u64(vm->frame_top, alignment);
    if (base + info->frame_size > (uint64_t)vm->profile->stack_size ||
        base + info->frame_size > vm->frame_region_size) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    base += vm->profile->region[TC_REG_FRAME].base;
    if (!grow_frames(vm)) {
        return 0;
    }
    frame = &vm->frames[vm->nframes];
    memset(frame, 0, sizeof(*frame));
    frame->unit = unit;
    frame->info = info;
    frame->func_id = func_id;
    frame->base = base;
    frame->mem_size = info->frame_size == 0 ? 1u : (size_t)info->frame_size;
    frame->mem = (uint8_t *)tc_vm_calloc(vm, frame->mem_size, 1u);
    frame->temps_size = info->temp_size == 0 ? 1u : (size_t)info->temp_size;
    frame->temps = (uint8_t *)tc_vm_calloc(vm, frame->temps_size, 1u);
    frame->defined_size = (size_t)info->frame_size;
    if (vm->profile->checked_build && frame->defined_size != 0) {
        size_t bitmap = (frame->defined_size + 7u) / 8u;
        frame->defined = (uint8_t *)tc_vm_calloc(vm, bitmap, 1u);
    }
    frame->regs = (TcVmValue *)tc_vm_calloc(
        vm, unit->nvregs == 0 ? 1u : unit->nvregs, sizeof(TcVmValue));
    frame->vreg_obj = (uint32_t *)tc_vm_calloc(
        vm, unit->nvregs == 0 ? 1u : unit->nvregs, sizeof(uint32_t));
    frame->first_object = (uint32_t)vm->nobjects;
    if (frame->mem == NULL || frame->temps == NULL || frame->regs == NULL ||
        frame->vreg_obj == NULL ||
        (vm->profile->checked_build && frame->defined_size != 0 &&
         frame->defined == NULL)) {
        goto fail;
    }
    for (i = 0; i < unit->nvregs; i++) {
        frame->vreg_obj[i] = UINT32_MAX;
        if (info->temp_off[i] != SIZE_MAX) {
            frame->regs[i].ptr = frame->temps + info->temp_off[i];
        }
    }
    if (info->nframe_objects != 0) {
        top_indices = (uint32_t *)tc_vm_calloc(vm, info->nframe_objects,
                                               sizeof(uint32_t));
        if (top_indices == NULL) {
            goto fail;
        }
    }
    for (i = 0; i < info->nframe_objects; i++) {
        uint32_t ir_id = info->frame_objects[i];
        const TcObject *object = &vm->ir->objects[ir_id];
        TcVmObject clone;
        uint32_t index;
        memset(&clone, 0, sizeof(clone));
        clone.ir_object = ir_id;
        clone.region = TC_REG_FRAME;
        clone.start = base + object->offset;
        clone.size = object->size;
        clone.type = object->type;
        clone.writable = object->writable;
        clone.frame = (uint32_t)vm->nframes;
        if (!append_object(vm, clone, &index) ||
            !add_field_objects(vm, clone.start, clone.type, clone.writable,
                               (uint32_t)vm->nframes)) {
            goto fail;
        }
        top_indices[i] = index;
    }
    for (i = 0; i < unit->nvregs; i++) {
        uint32_t ir_id;
        if (unit->vreg_objects == NULL ||
            i >= unit->nvreg_objects ||
            unit->vreg_objects[i] == UINT32_MAX) {
            continue;
        }
        ir_id = unit->vreg_objects[i];
        if (ir_id >= vm->ir->nobjects) {
            tc_vm_impl(vm, TC_IMPL_INTERNAL);
            goto fail;
        }
        if (vm->ir->objects[ir_id].region == TC_REG_STATIC) {
            if (vm->static_index[ir_id] == UINT32_MAX) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                goto fail;
            }
            frame->vreg_obj[i] = vm->static_index[ir_id];
        } else if (vm->ir->objects[ir_id].region == TC_REG_FRAME) {
            size_t k;
            for (k = 0; k < info->nframe_objects; k++) {
                if (info->frame_objects[k] == ir_id) {
                    frame->vreg_obj[i] = top_indices[k];
                    break;
                }
            }
            if (frame->vreg_obj[i] == UINT32_MAX) {
                tc_vm_impl(vm, TC_IMPL_INTERNAL);
                goto fail;
            }
        } else {
            tc_vm_impl(vm, TC_IMPL_INTERNAL);
            goto fail;
        }
    }
    tc_vm_free(vm, top_indices);
    frame->valid = 1;
    vm->frame_top = base + info->frame_size -
                    vm->profile->region[TC_REG_FRAME].base;
    if (out_index != NULL) {
        *out_index = (uint32_t)vm->nframes;
    }
    vm->nframes++;
    return 1;

fail:
    tc_vm_free(vm, top_indices);
    if (frame->first_object <= vm->nobjects) {
        vm->nobjects = frame->first_object;
    }
    tc_vm_free(vm, frame->mem);
    tc_vm_free(vm, frame->temps);
    tc_vm_free(vm, frame->defined);
    tc_vm_free(vm, frame->regs);
    tc_vm_free(vm, frame->vreg_obj);
    memset(frame, 0, sizeof(*frame));
    return 0;
}

void tc_vm_pop_frame(TcVm *vm) {
    TcVmFrame *frame;
    if (vm->nframes == 0) {
        return;
    }
    frame = &vm->frames[vm->nframes - 1u];
    if (frame->first_object <= vm->nobjects) {
        vm->nobjects = frame->first_object;
    }
    tc_vm_free(vm, frame->mem);
    tc_vm_free(vm, frame->temps);
    tc_vm_free(vm, frame->defined);
    tc_vm_free(vm, frame->regs);
    tc_vm_free(vm, frame->vreg_obj);
    vm->frame_top = frame->base - vm->profile->region[TC_REG_FRAME].base;
    memset(frame, 0, sizeof(*frame));
    vm->nframes--;
}


/* ------------------------------------------------------------------------- */
/* Setup: unit metadata, static storage and device backing.                  */
/* ------------------------------------------------------------------------- */

static void free_unit_infos(TcVm *vm) {
    size_t i;
    if (vm->units == NULL) {
        return;
    }
    for (i = 0; i < vm->nunits; i++) {
        tc_vm_free(vm, vm->units[i].temp_off);
        tc_vm_free(vm, vm->units[i].frame_objects);
    }
    tc_vm_free(vm, vm->units);
    vm->units = NULL;
    vm->nunits = 0;
}

static int build_unit_infos(TcVm *vm) {
    size_t count = vm->ir->nunits;
    size_t i;
    if (count == 0) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    vm->units = (TcVmUnitInfo *)tc_vm_calloc(vm, count,
                                             sizeof(TcVmUnitInfo));
    if (vm->units == NULL) {
        return 0;
    }
    vm->nunits = count;
    for (i = 0; i < count; i++) {
        const TcUnit *unit = &vm->ir->units[i];
        TcVmUnitInfo *info = &vm->units[i];
        size_t v;
        uint64_t max_end = 0;
        size_t temp_size = 0;
        size_t frame_cap = 0;
        uint64_t alignment = vm->profile->target->stack_align;
        info->temp_off = (size_t *)tc_vm_calloc(
            vm, unit->nvregs == 0 ? 1u : unit->nvregs, sizeof(size_t));
        if (info->temp_off == NULL) {
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            info->temp_off[v] = SIZE_MAX;
        }
        for (v = 0; v < unit->nvregs; v++) {
            uint32_t object_id = UINT32_MAX;
            const TcType *type = unit->vreg_types[v];
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects) {
                object_id = unit->vreg_objects[v];
            }
            if (type == NULL) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            if (object_id == UINT32_MAX) {
                if (type->tag == TC_TY_STRUCT) {
                    int ok = 0;
                    uint64_t size;
                    temp_size = (size_t)align_up_u64(temp_size, 8u);
                    info->temp_off[v] = temp_size;
                    size = tc_vm_type_size(vm, type, &ok);
                    if (!ok || size > SIZE_MAX - temp_size) {
                        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    }
                    temp_size += (size_t)size;
                }
                continue;
            }
            if (object_id >= vm->ir->nobjects) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            if (vm->ir->objects[object_id].region == TC_REG_FRAME) {
                size_t k;
                int found = 0;
                for (k = 0; k < info->nframe_objects; k++) {
                    if (info->frame_objects[k] == object_id) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (info->nframe_objects == frame_cap) {
                        size_t cap = frame_cap == 0 ? 8u : frame_cap * 2u;
                        uint32_t *fresh = (uint32_t *)tc_vm_calloc(
                            vm, cap, sizeof(uint32_t));
                        if (fresh == NULL) {
                            return 0;
                        }
                        if (info->nframe_objects != 0) {
                            memcpy(fresh, info->frame_objects,
                                   info->nframe_objects * sizeof(uint32_t));
                            tc_vm_free(vm, info->frame_objects);
                        }
                        info->frame_objects = fresh;
                        frame_cap = cap;
                    }
                    info->frame_objects[info->nframe_objects++] = object_id;
                }
                {
                    const TcObject *object = &vm->ir->objects[object_id];
                    if (object->offset > UINT64_MAX - object->size) {
                        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
                    }
                    if (object->offset + object->size > max_end) {
                        max_end = object->offset + object->size;
                    }
                }
            } else if (vm->ir->objects[object_id].region == TC_REG_STATIC) {
                continue;
            } else {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
        }
        info->frame_size = align_up_u64(max_end, alignment);
        info->temp_size = temp_size;
    }
    if (vm->ir->nfuncs != 0) {
        const TcRegionDesc *code = &vm->profile->region[TC_REG_CODE];
        if ((uint64_t)vm->ir->nfuncs + 1u > code->size / 16u) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
    }
    return 1;
}

static int build_static_storage(TcVm *vm) {
    const TcRegionDesc *desc = &vm->profile->region[TC_REG_STATIC];
    uint64_t max_end = 0;
    size_t i;
    vm->static_base = desc->base;
    vm->static_size = 0;
    vm->static_index = (uint32_t *)tc_vm_calloc(
        vm, vm->ir->nobjects == 0 ? 1u : vm->ir->nobjects,
        sizeof(uint32_t));
    if (vm->static_index == NULL) {
        return 0;
    }
    for (i = 0; i < vm->ir->nobjects; i++) {
        vm->static_index[i] = UINT32_MAX;
    }
    for (i = 0; i < vm->ir->nobjects; i++) {
        const TcObject *object = &vm->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > desc->size ||
            object->size > desc->size - object->offset) {
            return tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        if (object->offset + object->size > max_end) {
            max_end = object->offset + object->size;
        }
    }
    vm->static_size = max_end == 0 ? 1u : max_end;
    vm->static_mem = (uint8_t *)tc_vm_calloc(vm, (size_t)vm->static_size, 1u);
    if (vm->static_mem == NULL) {
        return 0;
    }
    if (vm->profile->checked_build) {
        size_t bitmap = (size_t)((vm->static_size + 7u) / 8u);
        vm->static_defined = (uint8_t *)tc_vm_calloc(vm, bitmap, 1u);
        if (vm->static_defined == NULL) {
            return 0;
        }
    }
    for (i = 0; i < vm->ir->nobjects; i++) {
        const TcObject *object = &vm->ir->objects[i];
        TcVmObject clone;
        uint32_t index;
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        memset(&clone, 0, sizeof(clone));
        clone.ir_object = (uint32_t)i;
        clone.region = TC_REG_STATIC;
        clone.start = vm->static_base + object->offset;
        clone.size = object->size;
        clone.type = object->type;
        clone.writable = object->writable;
        clone.frame = UINT32_MAX;
        if (!append_object(vm, clone, &index)) {
            return 0;
        }
        vm->static_index[i] = index;
        if (!add_field_objects(vm, clone.start, clone.type, clone.writable,
                               UINT32_MAX)) {
            return 0;
        }
        if (object->init != NULL) {
            if (object->init->size > object->size) {
                return tc_vm_impl(vm, TC_IMPL_INTERNAL);
            }
            if (object->init->size != 0) {
                if (object->init->bytes == NULL) {
                    return tc_vm_impl(vm, TC_IMPL_INTERNAL);
                }
                memcpy(vm->static_mem + object->offset, object->init->bytes,
                       object->init->size);
            }
            vm_define_range(vm, clone.start, object->size);
        }
    }
    return 1;
}

static int build_device_storage(TcVm *vm) {
    const TcRegionDesc *desc = &vm->profile->region[TC_REG_DEVICE];
    if (desc->size > SIZE_MAX) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    vm->device_base = desc->base;
    vm->device_size = desc->size;
    vm->device_mem = (uint8_t *)tc_vm_calloc(vm, (size_t)desc->size, 1u);
    return vm->device_mem != NULL;
}

int tc_vm_setup(TcVm *vm, const TcIR *ir, const TcPlatformProfile *profile,
                TcVmResult *result, TcImplError *out_impl) {
    TcProfileError perr;
    memset(vm, 0, sizeof(*vm));
    vm->ir = ir;
    vm->profile = profile;
    vm->result = result;
    vm->out_impl = out_impl;
    vm->impl = TC_IMPL_OK;
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (out_impl != NULL) {
        *out_impl = TC_IMPL_OK;
    }
    if (ir == NULL || profile == NULL || result == NULL) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    perr = tc_profile_validate_runtime(profile);
    if (perr != TC_PROFILE_OK) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (profile->addr_width != 32u && profile->addr_width != 64u) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    if (ir->profile != NULL && ir->profile->addr_width != profile->addr_width) {
        return tc_vm_impl(vm, TC_IMPL_INTERNAL);
    }
    vm->W = profile->addr_width;
    vm->addr_mask = vm->W == 64u ? UINT64_MAX
                                 : (UINT64_C(1) << vm->W) - 1u;
    vm->alloc = profile->alloc;
    vm->frame_region_size = profile->region[TC_REG_FRAME].size;
    if (!build_unit_infos(vm) || !build_static_storage(vm) ||
        !build_device_storage(vm)) {
        return 0;
    }
    tc_input_init(&vm->input, profile->io->in, vm->alloc);
    if (vm->input.token == NULL) {
        return tc_vm_impl(vm, TC_IMPL_OOM);
    }
    vm->frame_top = 0;
    vm->nframes = 0;
    vm->cap_frames = 0;
    vm->frames = NULL;
    vm->depth = 0;
    return 1;
}

void tc_vm_teardown(TcVm *vm) {
    size_t i;
    if (vm->frames != NULL) {
        for (i = 0; i < vm->nframes; i++) {
            tc_vm_free(vm, vm->frames[i].mem);
            tc_vm_free(vm, vm->frames[i].temps);
            tc_vm_free(vm, vm->frames[i].defined);
            tc_vm_free(vm, vm->frames[i].regs);
            tc_vm_free(vm, vm->frames[i].vreg_obj);
        }
        tc_vm_free(vm, vm->frames);
    }
    free_unit_infos(vm);
    tc_vm_free(vm, vm->objects);
    tc_vm_free(vm, vm->static_index);
    tc_vm_free(vm, vm->static_mem);
    tc_vm_free(vm, vm->static_defined);
    tc_vm_free(vm, vm->device_mem);
    tc_input_destroy(&vm->input);
    vm->objects = NULL;
    vm->static_index = NULL;
    vm->static_mem = NULL;
    vm->device_mem = NULL;
    vm->frames = NULL;
    vm->nframes = 0;
    vm->cap_frames = 0;
    vm->nobjects = 0;
    vm->cap_objects = 0;
}
