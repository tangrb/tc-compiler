/* TC 0.0.46 contract: built-in descriptors and profile validation (design §8). */
#include "runtime/profile.h"

#include <string.h>

const TcABIDesc tc_abi_aapcs64 = {
    TC_ABI_INT, TC_ABI_FP, TC_ABI_INT, TC_ABI_INT,
    TC_ABI_INT, TC_ABI_FP,
    1,     /* hfa */
    16,    /* composite_max_bytes */
    4,     /* hfa_max_regs */
    TC_ABI_RET_HFA,
    16,    /* stack_align */
    (uint32_t)(((1u << 10) - 1u) << 19), /* x19-x28 */
    0,     /* int64_pair */
    0      /* float64_pair */
};

const TcABIDesc tc_abi_aapcs32_ilp32 = {
    TC_ABI_INT, TC_ABI_FP, TC_ABI_INT, TC_ABI_INT,
    TC_ABI_INT, TC_ABI_FP,
    1,     /* hfa */
    4,     /* composite_max_bytes: r0-r3 by-value budget */
    4,     /* hfa_max_regs */
    TC_ABI_RET_DIRECT,
    8,     /* stack_align */
    (uint32_t)(((1u << 8) - 1u) << 4), /* r4-r11 */
    1,     /* int64_pair */
    1      /* float64_pair */
};

const TcTargetDesc tc_target_aarch64_linux_user = {
    "aarch64-linux-user", 64, 1, 16, 8, &tc_abi_aapcs64
};

const TcTargetDesc tc_target_aarch64_baremetal = {
    "aarch64-baremetal", 64, 1, 16, 8, &tc_abi_aapcs64
};

static void profile_set_regions(TcPlatformProfile *p, uint64_t frame_base,
                                uint64_t frame_size, uint64_t static_base,
                                uint64_t static_size, uint64_t device_base,
                                uint64_t device_size, uint64_t code_base,
                                uint64_t code_size, uint64_t heap_base,
                                uint64_t heap_size) {
    p->region[TC_REG_FRAME].base = frame_base;
    p->region[TC_REG_FRAME].size = frame_size;
    p->region[TC_REG_STATIC].base = static_base;
    p->region[TC_REG_STATIC].size = static_size;
    p->region[TC_REG_DEVICE].base = device_base;
    p->region[TC_REG_DEVICE].size = device_size;
    p->region[TC_REG_CODE].base = code_base;
    p->region[TC_REG_CODE].size = code_size;
    p->region[TC_REG_HEAP].base = heap_base;
    p->region[TC_REG_HEAP].size = heap_size;
}

/* Region bases/sizes, stack size, D and exit statuses below are development
 * defaults for one environment definition.  They are not language semantics;
 * a port may choose different values and must document them (standard §13.1).
 * Every value is validated for W, address 0 exclusion, overlap and entry. */
int tc_profile_init_aarch64_linux_user(TcPlatformProfile *out) {
    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->addr_width = 64;
    out->little_endian = 1;
    profile_set_regions(out,
                        0x0000000070000000ull, 0x0000000000100000ull,
                        0x0000000010000000ull, 0x0000000000100000ull,
                        0x0000000020000000ull, 0x0000000000100000ull,
                        0x0000000040000000ull, 0x0000000001000000ull,
                        0x0000000080000000ull, 0x0000000010000000ull);
    out->stack_size = 0x100000;
    out->depth_D = 512;
    out->exit.normal_status = 0;
    out->exit.trap_status = 1;
    out->entry = out->region[TC_REG_CODE].base;
    out->target = &tc_target_aarch64_linux_user;
    out->device = TC_DEV_REJECT;
    out->checked_build = 0;
    out->disable_icf = 1;
    return 1;
}

int tc_profile_init_aarch64_baremetal(TcPlatformProfile *out) {
    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->addr_width = 64;
    out->little_endian = 1;
    profile_set_regions(out,
                        0x000000007f000000ull, 0x0000000001000000ull,
                        0x0000000041000000ull, 0x0000000000100000ull,
                        0x0000000009000000ull, 0x0000000000100000ull,
                        0x0000000040000000ull, 0x0000000000100000ull,
                        0x0000000042000000ull, 0x0000000001000000ull);
    out->stack_size = 0x100000;
    out->depth_D = 256;
    out->exit.normal_status = 0;
    out->exit.trap_status = 1;
    out->entry = out->region[TC_REG_CODE].base;
    out->target = &tc_target_aarch64_baremetal;
    out->device = TC_DEV_VOLATILE;
    out->checked_build = 0;
    out->disable_icf = 1;
    return 1;
}

const char *tc_profile_error_symbol(TcProfileError err) {
    switch (err) {
    case TC_PROFILE_OK:
        return "tc_profile_ok";
    case TC_PROFILE_ERR_NULL:
        return "profile_missing";
    case TC_PROFILE_ERR_ADDR_WIDTH:
        return "profile_addr_width";
    case TC_PROFILE_ERR_ENDIAN:
        return "profile_endian";
    case TC_PROFILE_ERR_TARGET:
        return "profile_target";
    case TC_PROFILE_ERR_STACK_ALIGN:
        return "profile_stack_align";
    case TC_PROFILE_ERR_POINTER_ALIGN:
        return "profile_pointer_align";
    case TC_PROFILE_ERR_STACK_SIZE:
        return "profile_stack_size";
    case TC_PROFILE_ERR_DEPTH:
        return "profile_depth";
    case TC_PROFILE_ERR_REGION:
        return "profile_region";
    case TC_PROFILE_ERR_REGION_ZERO:
        return "profile_region_zero";
    case TC_PROFILE_ERR_REGION_OVERLAP:
        return "profile_region_overlap";
    case TC_PROFILE_ERR_ENTRY:
        return "profile_entry";
    case TC_PROFILE_ERR_DEVICE:
        return "profile_device";
    case TC_PROFILE_ERR_CHECKED:
        return "profile_checked";
    case TC_PROFILE_ERR_ICF:
        return "profile_icf";
    case TC_PROFILE_ERR_ABI:
        return "profile_abi";
    case TC_PROFILE_ERR_IO:
        return "profile_io";
    case TC_PROFILE_ERR_ALLOC:
        return "profile_alloc";
    case TC_PROFILE_ERR_TRAP:
        return "profile_trap";
    default:
        return "profile_unknown";
    }
}

static int abi_class_valid(TcABIClass c) {
    return c >= TC_ABI_INT && c <= TC_ABI_INDIRECT;
}

static TcProfileError validate_abi(const TcPlatformProfile *p) {
    const TcABIDesc *abi = p->target->abi;
    if (abi == NULL) {
        return TC_PROFILE_ERR_ABI;
    }
    if (!abi_class_valid(abi->param_int) || !abi_class_valid(abi->param_fp) ||
        !abi_class_valid(abi->param_ptr) || !abi_class_valid(abi->param_fnptr) ||
        !abi_class_valid(abi->ret_int) || !abi_class_valid(abi->ret_fp) ||
        (abi->composite_return != TC_ABI_RET_DIRECT &&
         abi->composite_return != TC_ABI_RET_INDIRECT &&
         abi->composite_return != TC_ABI_RET_HFA)) {
        return TC_PROFILE_ERR_ABI;
    }
    if ((abi->hfa != 0 && abi->hfa != 1) || abi->composite_max_bytes == 0 ||
        abi->hfa_max_regs == 0 || (abi->stack_align != 8 && abi->stack_align != 16)) {
        return TC_PROFILE_ERR_ABI;
    }
    if (abi->int64_pair != 0 && abi->int64_pair != 1) {
        return TC_PROFILE_ERR_ABI;
    }
    if (abi->float64_pair != 0 && abi->float64_pair != 1) {
        return TC_PROFILE_ERR_ABI;
    }
    if (abi->stack_align != p->target->stack_align) {
        return TC_PROFILE_ERR_STACK_ALIGN;
    }
    if (p->addr_width == 32 && (!abi->int64_pair || !abi->float64_pair)) {
        return TC_PROFILE_ERR_ABI;
    }
    return TC_PROFILE_OK;
}

TcProfileError tc_profile_validate(const TcPlatformProfile *profile) {
    unsigned i;
    unsigned j;
    uint64_t limit;
    const TcTargetDesc *target;
    TcProfileError err;
    if (profile == NULL) {
        return TC_PROFILE_ERR_NULL;
    }
    if (profile->addr_width != 32 && profile->addr_width != 64) {
        return TC_PROFILE_ERR_ADDR_WIDTH;
    }
    if (profile->little_endian != 0 && profile->little_endian != 1) {
        return TC_PROFILE_ERR_ENDIAN;
    }
    target = profile->target;
    if (target == NULL || target->name == NULL || target->name[0] == '\0') {
        return TC_PROFILE_ERR_TARGET;
    }
    if (target->addr_width != profile->addr_width) {
        return TC_PROFILE_ERR_TARGET;
    }
    if (target->little_endian != profile->little_endian) {
        return TC_PROFILE_ERR_ENDIAN;
    }
    if (target->stack_align != 8 && target->stack_align != 16) {
        return TC_PROFILE_ERR_STACK_ALIGN;
    }
    if (target->pointer_align != profile->addr_width / 8) {
        return TC_PROFILE_ERR_POINTER_ALIGN;
    }
    if (target->stack_align < target->pointer_align) {
        return TC_PROFILE_ERR_STACK_ALIGN;
    }
    if (profile->stack_size == 0) {
        return TC_PROFILE_ERR_STACK_SIZE;
    }
    if (profile->depth_D < 1) {
        return TC_PROFILE_ERR_DEPTH;
    }
    if (profile->checked_build != 0 && profile->checked_build != 1) {
        return TC_PROFILE_ERR_CHECKED;
    }
    if (profile->disable_icf != 1) {
        return TC_PROFILE_ERR_ICF;
    }
    if ((unsigned)profile->device > (unsigned)TC_DEV_VOLATILE) {
        return TC_PROFILE_ERR_DEVICE;
    }
    err = validate_abi(profile);
    if (err != TC_PROFILE_OK) {
        return err;
    }

    limit = profile->addr_width == 32 ? 0x100000000ull : UINT64_MAX;
    for (i = 0; i < (unsigned)TC_REGION_COUNT; i++) {
        const TcRegionDesc *r = &profile->region[i];
        if (r->size == 0) {
            return TC_PROFILE_ERR_REGION;
        }
        if (r->base == 0) {
            return TC_PROFILE_ERR_REGION_ZERO; /* address 0 is reserved */
        }
        if (r->base >= limit) {
            return TC_PROFILE_ERR_REGION;
        }
        if (profile->addr_width == 64 && r->size > UINT64_MAX - r->base) {
            return TC_PROFILE_ERR_REGION;
        }
        if (profile->addr_width == 32 && r->size > limit - r->base) {
            return TC_PROFILE_ERR_REGION;
        }
        for (j = i + 1; j < (unsigned)TC_REGION_COUNT; j++) {
            const TcRegionDesc *s = &profile->region[j];
            uint64_t rend = r->base + r->size;
            uint64_t send = s->base + s->size;
            if (r->base < send && s->base < rend) {
                return TC_PROFILE_ERR_REGION_OVERLAP;
            }
        }
    }
    {
        const TcRegionDesc *code = &profile->region[TC_REG_CODE];
        if (profile->entry == 0 || profile->entry < code->base ||
            profile->entry >= code->base + code->size) {
            return TC_PROFILE_ERR_ENTRY;
        }
    }
    return TC_PROFILE_OK;
}

TcProfileError tc_profile_validate_runtime(const TcPlatformProfile *profile) {
    TcProfileError err = tc_profile_validate(profile);
    if (err != TC_PROFILE_OK) {
        return err;
    }
    if (profile->io == NULL || profile->io->out == NULL ||
        profile->io->out->write == NULL || profile->io->in == NULL ||
        profile->io->in->read == NULL) {
        return TC_PROFILE_ERR_IO;
    }
    if (!tc_allocator_is_valid(profile->alloc)) {
        return TC_PROFILE_ERR_ALLOC;
    }
    if (profile->trap == NULL || profile->trap->trap == NULL) {
        return TC_PROFILE_ERR_TRAP;
    }
    return TC_PROFILE_OK;
}
