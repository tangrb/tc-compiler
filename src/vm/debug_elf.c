#include "vm/debug_elf.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ELF_ET_REL 1u
#define ELF_EM_ARM 40u
#define ELF_EM_AARCH64 183u
#define ELF_EM_RISCV 243u
#define ELF_SHN_ABS 0xfff1u

#define DW_TAG_compile_unit 0x11u
#define DW_TAG_subprogram 0x2eu
#define DW_CHILDREN_no 0u
#define DW_CHILDREN_yes 1u
#define DW_AT_name 0x03u
#define DW_AT_stmt_list 0x10u
#define DW_AT_low_pc 0x11u
#define DW_AT_high_pc 0x12u
#define DW_AT_decl_file 0x3au
#define DW_AT_decl_line 0x3bu
#define DW_FORM_addr 0x01u
#define DW_FORM_data2 0x05u
#define DW_FORM_data4 0x06u
#define DW_FORM_string 0x08u
#define DW_FORM_udata 0x0fu
#define DW_FORM_sec_offset 0x17u

#define DW_LNS_extended 0u
#define DW_LNS_copy 1u
#define DW_LNS_advance_pc 2u
#define DW_LNS_advance_line 3u
#define DW_LNS_set_file 4u
#define DW_LNS_set_column 5u
#define DW_LNE_end_sequence 1u
#define DW_LNE_set_address 2u

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} Buffer;

static void buffer_init(Buffer *buffer) {
    buffer->data = NULL;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

static void buffer_destroy(Buffer *buffer) {
    free(buffer->data);
    buffer_init(buffer);
}

static int buffer_reserve(Buffer *buffer, size_t extra) {
    size_t needed;
    size_t capacity;
    uint8_t *fresh;
    if (extra > SIZE_MAX - buffer->size) {
        return 0;
    }
    needed = buffer->size + extra;
    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    fresh = (uint8_t *)realloc(buffer->data, capacity);
    if (fresh == NULL) {
        return 0;
    }
    buffer->data = fresh;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_append(Buffer *buffer, const void *data, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return 0;
    }
    if (size != 0u) {
        memcpy(buffer->data + buffer->size, data, size);
    }
    buffer->size += size;
    return 1;
}

static int buffer_byte(Buffer *buffer, uint8_t value) {
    return buffer_append(buffer, &value, 1u);
}

static int buffer_zeros(Buffer *buffer, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return 0;
    }
    memset(buffer->data + buffer->size, 0, size);
    buffer->size += size;
    return 1;
}

static int buffer_align(Buffer *buffer, size_t alignment) {
    size_t padding;
    if (alignment == 0u) {
        return 0;
    }
    padding = (alignment - buffer->size % alignment) % alignment;
    return buffer_zeros(buffer, padding);
}

static int buffer_u16(Buffer *buffer, uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    return buffer_append(buffer, bytes, sizeof(bytes));
}

static int buffer_u32(Buffer *buffer, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    return buffer_append(buffer, bytes, sizeof(bytes));
}

static int buffer_u64(Buffer *buffer, uint64_t value) {
    uint8_t bytes[8];
    unsigned i;
    for (i = 0u; i < 8u; i++) {
        bytes[i] = (uint8_t)(value >> (8u * i));
    }
    return buffer_append(buffer, bytes, sizeof(bytes));
}

static int buffer_uleb(Buffer *buffer, uint64_t value) {
    do {
        uint8_t byte = (uint8_t)(value & 0x7fu);
        value >>= 7u;
        if (value != 0u) {
            byte |= 0x80u;
        }
        if (!buffer_byte(buffer, byte)) {
            return 0;
        }
    } while (value != 0u);
    return 1;
}

static int buffer_sleb(Buffer *buffer, int64_t value) {
    int more = 1;
    while (more) {
        uint8_t byte = (uint8_t)((uint64_t)value & 0x7fu);
        value = (int64_t)(((uint64_t)value >> 7u) |
                          (value < 0 ? UINT64_MAX << 57u : 0u));
        if ((value == 0 && (byte & 0x40u) == 0u) ||
            (value == -1 && (byte & 0x40u) != 0u)) {
            more = 0;
        } else {
            byte |= 0x80u;
        }
        if (!buffer_byte(buffer, byte)) {
            return 0;
        }
    }
    return 1;
}

static int buffer_cstring(Buffer *buffer, const char *text) {
    size_t size;
    if (text == NULL) {
        return 0;
    }
    size = strlen(text);
    if (size == SIZE_MAX) {
        return 0;
    }
    return buffer_append(buffer, text, size + 1u);
}

static int write_all(int fd, const uint8_t *bytes, size_t size) {
    while (size != 0u) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (written == 0) {
            return 0;
        }
        bytes += (size_t)written;
        size -= (size_t)written;
    }
    return 1;
}

TcImplError tc_vm_debug_code_size(const TcPlatformProfile *profile,
                                  const TcLineMap *linemap,
                                  uint64_t *out_size) {
    uint64_t base;
    uint64_t code_size;
    unsigned i;
    if (profile == NULL || linemap == NULL || out_size == NULL ||
        (profile->addr_width != 32u && profile->addr_width != 64u)) {
        return TC_IMPL_INTERNAL;
    }
    base = profile->addr_width == 64u ? TC_DEBUG_CODE_BASE64
                                      : TC_DEBUG_CODE_BASE32;
    if (linemap->count > (UINT64_MAX - base) / 4u) {
        return TC_IMPL_INTERNAL;
    }
    code_size = (uint64_t)linemap->count * 4u;
    if (code_size > UINT64_MAX - base - 4u ||
        (profile->addr_width == 32u &&
         code_size >= UINT64_C(0x100000000) - base)) {
        return TC_IMPL_INTERNAL;
    }
    for (i = 0u; i < (unsigned)TC_REGION_COUNT; i++) {
        const TcRegionDesc *region = &profile->region[i];
        uint64_t end;
        if (region->size > UINT64_MAX - region->base) {
            return TC_IMPL_INTERNAL;
        }
        end = region->base + region->size;
        if (base < end && region->base < base + code_size) {
            return TC_IMPL_INTERNAL;
        }
    }
    *out_size = code_size;
    return TC_IMPL_OK;
}

static int target_encoding(const TcPlatformProfile *profile, uint16_t *machine,
                           uint8_t *elf_class) {
    if (profile->target == &tc_target_aarch64_linux_user ||
        profile->target == &tc_target_aarch64_baremetal) {
        *machine = ELF_EM_AARCH64;
        *elf_class = 2u;
        return 1;
    }
    if (profile->target == &tc_target_arm32_baremetal) {
        *machine = ELF_EM_ARM;
        *elf_class = 1u;
        return 1;
    }
    if (profile->target == &tc_target_riscv32_baremetal) {
        *machine = ELF_EM_RISCV;
        *elf_class = 1u;
        return 1;
    }
    return 0;
}

static int function_range(const TcLineMap *linemap, size_t index,
                          size_t *out_end) {
    const TcLineMapEntry *entry = &linemap->entries[index];
    size_t end;
    if (entry->func_id == TC_UNIT_TOP_LEVEL) {
        return 0;
    }
    end = index + 1u;
    while (end < linemap->count &&
           linemap->entries[end].func_id == entry->func_id &&
           linemap->entries[end].func_id != TC_UNIT_TOP_LEVEL) {
        end++;
    }
    *out_end = end;
    return 1;
}

static TcImplError build_symbols(const TcLineMap *linemap, uint64_t base,
                                 unsigned addr_size, Buffer *symtab,
                                 Buffer *strtab, size_t *out_count) {
    size_t index = 0u;
    size_t count = 1u;
    size_t pointer_size = addr_size / 8u;
    (void)base;
    if (!buffer_zeros(symtab, pointer_size == 8u ? 24u : 16u) ||
        !buffer_byte(strtab, 0u)) {
        return TC_IMPL_OOM;
    }
    while (index < linemap->count) {
        const TcLineMapEntry *entry = &linemap->entries[index];
        size_t end;
        size_t name_offset;
        uint32_t value32;
        uint64_t value64;
        uint32_t size32;
        uint64_t size64;
        if (!function_range(linemap, index, &end)) {
            index++;
            continue;
        }
        if (strtab->size > UINT32_MAX || end - index > UINT32_MAX / 4u ||
            strlen(entry->func) > UINT32_MAX - strtab->size - 1u) {
            return TC_IMPL_INTERNAL;
        }
        name_offset = (uint32_t)strtab->size;
        if (!buffer_cstring(strtab, entry->func) ||
            !buffer_u32(symtab, (uint32_t)name_offset)) {
            return TC_IMPL_OOM;
        }
        if (pointer_size == 8u) {
            value64 = base + (uint64_t)index * 4u;
            size64 = (uint64_t)(end - index) * 4u;
            if (!buffer_byte(symtab, 0x12u) || !buffer_byte(symtab, 0u) ||
                !buffer_u16(symtab, ELF_SHN_ABS) ||
                !buffer_u64(symtab, value64) ||
                !buffer_u64(symtab, size64)) {
                return TC_IMPL_OOM;
            }
        } else {
            value32 = (uint32_t)(base + (uint64_t)index * 4u);
            size32 = (uint32_t)((end - index) * 4u);
            if (!buffer_u32(symtab, value32) || !buffer_u32(symtab, size32) ||
                !buffer_byte(symtab, 0x12u) || !buffer_byte(symtab, 0u) ||
                !buffer_u16(symtab, ELF_SHN_ABS)) {
                return TC_IMPL_OOM;
            }
        }
        count++;
        index = end;
    }
    *out_count = count;
    return TC_IMPL_OK;
}

static TcImplError build_abbrev(Buffer *abbrev) {
    return buffer_byte(abbrev, 1u) &&
                   buffer_uleb(abbrev, DW_TAG_compile_unit) &&
                   buffer_byte(abbrev, DW_CHILDREN_yes) &&
                   buffer_uleb(abbrev, DW_AT_name) &&
                   buffer_uleb(abbrev, DW_FORM_string) &&
                   buffer_uleb(abbrev, DW_AT_low_pc) &&
                   buffer_uleb(abbrev, DW_FORM_addr) &&
                   buffer_uleb(abbrev, DW_AT_high_pc) &&
                   buffer_uleb(abbrev, DW_FORM_data4) &&
                   buffer_uleb(abbrev, DW_AT_stmt_list) &&
                   buffer_uleb(abbrev, DW_FORM_sec_offset) &&
                   buffer_byte(abbrev, 0u) && buffer_byte(abbrev, 0u) &&
                   buffer_byte(abbrev, 2u) &&
                   buffer_uleb(abbrev, DW_TAG_subprogram) &&
                   buffer_byte(abbrev, DW_CHILDREN_no) &&
                   buffer_uleb(abbrev, DW_AT_name) &&
                   buffer_uleb(abbrev, DW_FORM_string) &&
                   buffer_uleb(abbrev, DW_AT_decl_file) &&
                   buffer_uleb(abbrev, DW_FORM_udata) &&
                   buffer_uleb(abbrev, DW_AT_decl_line) &&
                   buffer_uleb(abbrev, DW_FORM_data4) &&
                   buffer_uleb(abbrev, DW_AT_low_pc) &&
                   buffer_uleb(abbrev, DW_FORM_addr) &&
                   buffer_uleb(abbrev, DW_AT_high_pc) &&
                   buffer_uleb(abbrev, DW_FORM_data4) &&
                   buffer_byte(abbrev, 0u) && buffer_byte(abbrev, 0u) &&
                   buffer_byte(abbrev, 0u)
               ? TC_IMPL_OK
               : TC_IMPL_OOM;
}

static int emit_address(Buffer *body, uint64_t address, unsigned addr_size) {
    size_t start = body->size;
    if (!buffer_byte(body, 0u) ||
        !buffer_uleb(body, (uint64_t)addr_size + 1u) ||
        !buffer_byte(body, DW_LNE_set_address)) {
        return 0;
    }
    if (addr_size == 8u) {
        if (!buffer_u64(body, address)) {
            return 0;
        }
    } else if (!buffer_u32(body, (uint32_t)address)) {
        return 0;
    }
    return body->size - start == (size_t)addr_size + 3u;
}

static int emit_line_row(Buffer *program, const TcLineMapEntry *entry,
                         size_t index, uint64_t base, unsigned addr_size,
                         uint32_t *previous_file, int64_t *previous_line) {
    int64_t line = (int64_t)entry->span->line;
    int64_t delta = line - *previous_line;
    if (!emit_address(program, base + (uint64_t)index * 4u, addr_size)) {
        return 0;
    }
    if (entry->file_id != *previous_file) {
        if (!buffer_byte(program, DW_LNS_set_file) ||
            !buffer_uleb(program, entry->file_id)) {
            return 0;
        }
    }
    if (!buffer_byte(program, DW_LNS_set_column) ||
        !buffer_uleb(program, entry->span->column)) {
        return 0;
    }
    if (delta != 0) {
        if (!buffer_byte(program, DW_LNS_advance_line) ||
            !buffer_sleb(program, delta)) {
            return 0;
        }
    }
    if (!buffer_byte(program, DW_LNS_copy)) {
        return 0;
    }
    *previous_file = entry->file_id;
    *previous_line = line;
    return 1;
}

static TcImplError build_line(const TcLineMap *linemap, uint64_t base,
                              uint64_t code_size, unsigned addr_size,
                              Buffer *line) {
    Buffer body;
    Buffer program;
    size_t index;
    uint32_t previous_file = 1u;
    int64_t previous_line = 1;
    uint32_t unit_length;
    buffer_init(&body);
    buffer_init(&program);
    if (!buffer_byte(&body, 1u) || !buffer_byte(&body, 1u) ||
        !buffer_byte(&body, 1u) || !buffer_byte(&body, 0xfbu) ||
        !buffer_byte(&body, 0x0eu) || !buffer_byte(&body, 13u) ||
        !buffer_append(&body, "\0\1\1\1\1\0\0\0\1\0\0\1", 12u) ||
        !buffer_byte(&body, 0u)) {
        buffer_destroy(&body);
        buffer_destroy(&program);
        return TC_IMPL_OOM;
    }
    for (index = 0u; index < linemap->file_count; index++) {
        if (linemap->files[index].id != (uint32_t)index + 1u) {
            buffer_destroy(&body);
            buffer_destroy(&program);
            return TC_IMPL_INTERNAL;
        }
        if (!buffer_cstring(&body, linemap->files[index].path) ||
            !buffer_uleb(&body, 0u) || !buffer_uleb(&body, 0u) ||
            !buffer_uleb(&body, 0u)) {
            buffer_destroy(&body);
            buffer_destroy(&program);
            return TC_IMPL_OOM;
        }
    }
    if (!buffer_byte(&body, 0u)) {
        buffer_destroy(&body);
        buffer_destroy(&program);
        return TC_IMPL_OOM;
    }
    for (index = 0u; index < linemap->count; index++) {
        if (!emit_line_row(&program, &linemap->entries[index], index, base,
                           addr_size, &previous_file, &previous_line)) {
            buffer_destroy(&body);
            buffer_destroy(&program);
            return TC_IMPL_OOM;
        }
    }
    if (!emit_address(&program, base + code_size, addr_size) ||
        !buffer_byte(&program, 0u) || !buffer_uleb(&program, 1u) ||
        !buffer_byte(&program, DW_LNE_end_sequence)) {
        buffer_destroy(&body);
        buffer_destroy(&program);
        return TC_IMPL_OOM;
    }
    if (body.size > UINT32_MAX - 6u ||
        program.size > UINT32_MAX - 6u - body.size) {
        buffer_destroy(&body);
        buffer_destroy(&program);
        return TC_IMPL_INTERNAL;
    }
    unit_length = (uint32_t)(body.size + 6u + program.size);
    if (!buffer_u32(line, unit_length) || !buffer_u16(line, 4u) ||
        !buffer_u32(line, (uint32_t)body.size) ||
        !buffer_append(line, body.data, body.size) ||
        !buffer_append(line, program.data, program.size)) {
        buffer_destroy(&body);
        buffer_destroy(&program);
        return TC_IMPL_OOM;
    }
    buffer_destroy(&body);
    buffer_destroy(&program);
    return TC_IMPL_OK;
}

static TcImplError build_info(const TcLineMap *linemap, uint64_t base,
                              uint64_t code_size, unsigned addr_size,
                              Buffer *info) {
    Buffer body;
    size_t index = 0u;
    buffer_init(&body);
    if (!buffer_uleb(&body, 1u) ||
        !buffer_cstring(&body, linemap->source)) {
        buffer_destroy(&body);
        return TC_IMPL_OOM;
    }
    if (addr_size == 8u) {
        if (!buffer_u64(&body, base)) {
            buffer_destroy(&body);
            return TC_IMPL_OOM;
        }
    } else if (!buffer_u32(&body, (uint32_t)base)) {
        buffer_destroy(&body);
        return TC_IMPL_OOM;
    }
    if (code_size > UINT32_MAX) {
        buffer_destroy(&body);
        return TC_IMPL_INTERNAL;
    }
    if (!buffer_u32(&body, (uint32_t)code_size) || !buffer_u32(&body, 0u)) {
        buffer_destroy(&body);
        return TC_IMPL_OOM;
    }
    while (index < linemap->count) {
        const TcLineMapEntry *entry = &linemap->entries[index];
        size_t end;
        if (!function_range(linemap, index, &end)) {
            index++;
            continue;
        }
        if (entry->span->line > UINT32_MAX ||
            (end - index) * 4u > UINT32_MAX) {
            buffer_destroy(&body);
            return TC_IMPL_INTERNAL;
        }
        if (!buffer_uleb(&body, 2u) ||
            !buffer_cstring(&body, entry->func) ||
            !buffer_uleb(&body, entry->file_id) ||
            !buffer_u32(&body, entry->span->line)) {
            buffer_destroy(&body);
            return TC_IMPL_OOM;
        }
        if (addr_size == 8u) {
            if (!buffer_u64(&body, base + (uint64_t)index * 4u)) {
                buffer_destroy(&body);
                return TC_IMPL_OOM;
            }
        } else if (!buffer_u32(&body, (uint32_t)(base + (uint64_t)index * 4u))) {
            buffer_destroy(&body);
            return TC_IMPL_OOM;
        }
        if (!buffer_u32(&body, (uint32_t)((end - index) * 4u))) {
            buffer_destroy(&body);
            return TC_IMPL_OOM;
        }
        index = end;
    }
    if (body.size > UINT32_MAX - 8u) {
        buffer_destroy(&body);
        return TC_IMPL_INTERNAL;
    }
    if (!buffer_byte(&body, 0u)) {
        buffer_destroy(&body);
        return TC_IMPL_OOM;
    }
    {
        uint32_t unit_length = (uint32_t)(body.size + 7u);
        uint8_t header[11];
        header[0] = (uint8_t)unit_length;
        header[1] = (uint8_t)(unit_length >> 8);
        header[2] = (uint8_t)(unit_length >> 16);
        header[3] = (uint8_t)(unit_length >> 24);
        header[4] = 4u;
        header[5] = 0u;
        header[6] = 0u;
        header[7] = 0u;
        header[8] = 0u;
        header[9] = 0u;
        header[10] = (uint8_t)addr_size;
        if (!buffer_append(info, header, sizeof(header)) ||
            !buffer_append(info, body.data, body.size)) {
            buffer_destroy(&body);
            return TC_IMPL_OOM;
        }
    }
    buffer_destroy(&body);
    return TC_IMPL_OK;
}

static int add_shstr(Buffer *shstrtab, const char *name, uint32_t *out_offset) {
    *out_offset = (uint32_t)shstrtab->size;
    return buffer_cstring(shstrtab, name);
}

static void store16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void store32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void store64(uint8_t *p, uint64_t value) {
    unsigned i;
    for (i = 0u; i < 8u; i++) {
        p[i] = (uint8_t)(value >> (8u * i));
    }
}

TcImplError tc_vm_write_debug_elf(int fd, const TcLineMap *linemap,
                                  const TcPlatformProfile *profile) {
    Buffer image;
    Buffer symtab;
    Buffer strtab;
    Buffer shstrtab;
    Buffer abbrev;
    Buffer info;
    Buffer line;
    const Buffer *sections[6];
    size_t offsets[6];
    uint32_t name_offsets[6];
    size_t shoff;
    size_t symbol_count = 1u;
    size_t ehsize;
    size_t shentsize;
    size_t pointer_size;
    size_t index;
    uint64_t base;
    uint64_t code_size;
    uint16_t machine;
    uint8_t elf_class;
    TcImplError impl;
    if (fd < 0 || linemap == NULL || profile == NULL ||
        linemap->source == NULL || linemap->profile == NULL ||
        !profile->little_endian ||
        !target_encoding(profile, &machine, &elf_class)) {
        return TC_IMPL_UNAVAILABLE;
    }
    impl = tc_vm_debug_code_size(profile, linemap, &code_size);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    base = profile->addr_width == 64u ? TC_DEBUG_CODE_BASE64
                                      : TC_DEBUG_CODE_BASE32;
    ehsize = elf_class == 2u ? 64u : 52u;
    shentsize = elf_class == 2u ? 64u : 40u;
    pointer_size = elf_class == 2u ? 8u : 4u;
    buffer_init(&image);
    buffer_init(&symtab);
    buffer_init(&strtab);
    buffer_init(&shstrtab);
    buffer_init(&abbrev);
    buffer_init(&info);
    buffer_init(&line);
    impl = build_symbols(linemap, base, profile->addr_width, &symtab,
                         &strtab, &symbol_count);
    if (impl == TC_IMPL_OK) {
        impl = build_abbrev(&abbrev);
    }
    if (impl == TC_IMPL_OK) {
        impl = build_info(linemap, base, code_size, profile->addr_width / 8u,
                          &info);
    }
    if (impl == TC_IMPL_OK) {
        impl = build_line(linemap, base, code_size, profile->addr_width / 8u,
                          &line);
    }
    if (impl != TC_IMPL_OK) {
        goto done;
    }
    if (!buffer_byte(&shstrtab, 0u) ||
        !add_shstr(&shstrtab, ".symtab", &name_offsets[0]) ||
        !add_shstr(&shstrtab, ".strtab", &name_offsets[1]) ||
        !add_shstr(&shstrtab, ".shstrtab", &name_offsets[2]) ||
        !add_shstr(&shstrtab, ".debug_abbrev", &name_offsets[3]) ||
        !add_shstr(&shstrtab, ".debug_info", &name_offsets[4]) ||
        !add_shstr(&shstrtab, ".debug_line", &name_offsets[5])) {
        impl = TC_IMPL_OOM;
        goto done;
    }
    sections[0] = &symtab;
    sections[1] = &strtab;
    sections[2] = &shstrtab;
    sections[3] = &abbrev;
    sections[4] = &info;
    sections[5] = &line;
    if (!buffer_zeros(&image, ehsize)) {
        impl = TC_IMPL_OOM;
        goto done;
    }
    for (index = 0u; index < 6u; index++) {
        if (!buffer_align(&image, pointer_size)) {
            impl = TC_IMPL_OOM;
            goto done;
        }
        offsets[index] = image.size;
        if (!buffer_append(&image, sections[index]->data,
                           sections[index]->size)) {
            impl = TC_IMPL_OOM;
            goto done;
        }
    }
    for (index = 0u; index < 6u; index++) {
        if (elf_class == 1u && sections[index]->size > UINT32_MAX) {
            impl = TC_IMPL_INTERNAL;
            goto done;
        }
    }
    if (!buffer_align(&image, pointer_size)) {
        impl = TC_IMPL_OOM;
        goto done;
    }
    if (elf_class == 1u &&
        (image.size > UINT32_MAX ||
         shentsize * 7u > UINT32_MAX - image.size)) {
        impl = TC_IMPL_INTERNAL;
        goto done;
    }
    shoff = image.size;
    if (!buffer_zeros(&image, shentsize * 7u)) {
        impl = TC_IMPL_OOM;
        goto done;
    }
    for (index = 0u; index < 6u; index++) {
        uint8_t *sh = image.data + shoff + (index + 1u) * shentsize;
        uint32_t type = index == 0u ? 2u : (index <= 2u ? 3u : 1u);
        uint32_t link = index == 0u ? 2u : 0u;
        uint32_t info = index == 0u ? 1u : 0u;
        uint64_t alignment = index == 0u ? (uint64_t)pointer_size : 1u;
        uint64_t entsize = index == 0u
                               ? (uint64_t)(pointer_size == 8u ? 24u : 16u)
                               : 0u;
        store32(sh, name_offsets[index]);
        store32(sh + 4u, type);
        if (pointer_size == 8u) {
            store64(sh + 8u, 0u);
            store64(sh + 16u, 0u);
            store64(sh + 24u, (uint64_t)offsets[index]);
            store64(sh + 32u, (uint64_t)sections[index]->size);
            store32(sh + 40u, link);
            store32(sh + 44u, info);
            store64(sh + 48u, alignment);
            store64(sh + 56u, entsize);
        } else {
            store32(sh + 8u, 0u);
            store32(sh + 12u, 0u);
            store32(sh + 16u, (uint32_t)offsets[index]);
            store32(sh + 20u, (uint32_t)sections[index]->size);
            store32(sh + 24u, link);
            store32(sh + 28u, info);
            store32(sh + 32u, (uint32_t)alignment);
            store32(sh + 36u, (uint32_t)entsize);
        }
    }
    {
        uint8_t *header = image.data;
        memcpy(header, "\177ELF", 4u);
        header[4] = elf_class;
        header[5] = 1u;
        header[6] = 1u;
        store16(header + 16u, ELF_ET_REL);
        store16(header + 18u, machine);
        store32(header + 20u, 1u);
        if (elf_class == 2u) {
            store64(header + 40u, (uint64_t)shoff);
            store16(header + 52u, (uint16_t)ehsize);
            store16(header + 58u, (uint16_t)shentsize);
            store16(header + 60u, 7u);
            store16(header + 62u, 3u);
        } else {
            store32(header + 32u, (uint32_t)shoff);
            store16(header + 40u, (uint16_t)ehsize);
            store16(header + 46u, (uint16_t)shentsize);
            store16(header + 48u, 7u);
            store16(header + 50u, 3u);
        }
    }
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
        impl = TC_IMPL_IO;
        goto done;
    }
    if (!write_all(fd, image.data, image.size)) {
        impl = TC_IMPL_IO;
        goto done;
    }
    impl = TC_IMPL_OK;
    (void)symbol_count;

done:
    buffer_destroy(&line);
    buffer_destroy(&info);
    buffer_destroy(&abbrev);
    buffer_destroy(&shstrtab);
    buffer_destroy(&strtab);
    buffer_destroy(&symtab);
    buffer_destroy(&image);
    return impl;
}
