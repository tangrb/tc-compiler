#include "base/path.h"
#include "ir/linemap.h"
#include "runtime/alloc.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int valid_display_path(const char *path) {
    return path != NULL && path[0] != '\0' && path[0] != '/' &&
           tc_path_text_valid(path);
}

static int external_display_path(const char *path) {
    return strcmp(path, "..") == 0 || strncmp(path, "../", 3u) == 0 ||
           strncmp(path, "./", 2u) == 0;
}

static size_t instruction_count(const TcIR *ir, TcImplError *impl) {
    size_t total = 0u;
    size_t i;
    for (i = 0u; i < ir->nunits; i++) {
        const TcUnit *unit = &ir->units[i];
        size_t b;
        for (b = 0u; b < unit->nblocks; b++) {
            if (unit->blocks[b].n > SIZE_MAX - total) {
                *impl = TC_IMPL_INTERNAL;
                return 0u;
            }
            total += unit->blocks[b].n;
        }
    }
    return total;
}

static TcImplError validate_instruction(const TcIR *ir, const TcUnit *unit,
                                        const TcInstr *instr) {
    const char *module;
    const char *func = NULL;
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        module = ir->modules[0].path;
        func = "<top>";
    } else {
        const TcFunc *definition;
        if (unit->func_id >= ir->nfuncs) {
            return TC_IMPL_INTERNAL;
        }
        definition = &ir->funcs[unit->func_id];
        if (definition->module >= ir->nmodules ||
            definition->name == NULL || definition->body != unit) {
            return TC_IMPL_INTERNAL;
        }
        module = ir->modules[definition->module].path;
        func = definition->name;
    }
    if (!valid_display_path(module) || func == NULL || func[0] == '\0' ||
        !valid_display_path(instr->span.file) || instr->span.line == 0u ||
        instr->span.column == 0u) {
        return TC_IMPL_INTERNAL;
    }
    return TC_IMPL_OK;
}

static TcImplError add_file(TcLineMap *linemap, const char *path,
                            uint32_t *out_id) {
    size_t i;
    for (i = 0u; i < linemap->file_count; i++) {
        if (strcmp(linemap->files[i].path, path) == 0) {
            *out_id = linemap->files[i].id;
            return TC_IMPL_OK;
        }
    }
    if (linemap->file_count == UINT32_MAX ||
        linemap->file_count + 1u > SIZE_MAX / sizeof(*linemap->files)) {
        return TC_IMPL_INTERNAL;
    }
    {
        TcLineMapFile *fresh = (TcLineMapFile *)linemap->alloc->realloc(
            linemap->alloc->ctx, linemap->files,
            (linemap->file_count + 1u) * sizeof(*linemap->files));
        if (fresh == NULL) {
            return TC_IMPL_OOM;
        }
        linemap->files = fresh;
    }
    linemap->files[linemap->file_count].path = path;
    linemap->files[linemap->file_count].id =
        (uint32_t)linemap->file_count + 1u;
    *out_id = linemap->files[linemap->file_count].id;
    linemap->file_count++;
    return TC_IMPL_OK;
}

static size_t pointer_hash(const TcInstr *instr) {
    uintptr_t value = (uintptr_t)instr;
    value ^= value >> (sizeof(value) * 8u / 2u);
    value ^= value >> (sizeof(value) * 8u / 4u);
    return (size_t)value;
}

static TcImplError build_lookup(TcLineMap *linemap) {
    size_t capacity = 1u;
    size_t i;
    if (linemap->count > SIZE_MAX / 2u) {
        return TC_IMPL_INTERNAL;
    }
    while (capacity < linemap->count * 2u) {
        if (capacity > SIZE_MAX / 2u) {
            return TC_IMPL_INTERNAL;
        }
        capacity *= 2u;
    }
    linemap->lookup = (size_t *)tc_allocator_calloc(
        linemap->alloc, capacity, sizeof(*linemap->lookup));
    if (linemap->lookup == NULL) {
        return TC_IMPL_OOM;
    }
    linemap->lookup_capacity = capacity;
    for (i = 0u; i < linemap->count; i++) {
        size_t slot = pointer_hash(linemap->entries[i].instr) & (capacity - 1u);
        while (linemap->lookup[slot] != 0u) {
            size_t prior = linemap->lookup[slot] - 1u;
            if (linemap->entries[prior].instr == linemap->entries[i].instr) {
                return TC_IMPL_INTERNAL;
            }
            slot = (slot + 1u) & (capacity - 1u);
        }
        linemap->lookup[slot] = i + 1u;
    }
    return TC_IMPL_OK;
}

TcImplError tc_linemap_build(const TcIR *ir, TcLineMap **out) {
    TcLineMap *linemap;
    TcImplError impl = TC_IMPL_OK;
    size_t count;
    size_t index = 0u;
    size_t i;

    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    if (ir == NULL || ir->nmodules == 0u || ir->modules == NULL ||
        ir->profile == NULL || ir->profile->target == NULL ||
        ir->profile->target->name == NULL ||
        !valid_display_path(ir->modules[0].path)) {
        return TC_IMPL_INTERNAL;
    }
    count = instruction_count(ir, &impl);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    const TcAllocator *alloc = ir->profile->alloc != NULL
                                   ? ir->profile->alloc
                                   : tc_allocator_libc();
    linemap = (TcLineMap *)tc_allocator_calloc(
        alloc, 1u, sizeof(*linemap));
    if (linemap == NULL) {
        return TC_IMPL_OOM;
    }
    linemap->alloc = alloc;
    linemap->source = ir->modules[0].path;
    linemap->profile = ir->profile->target->name;
    linemap->count = count;
    if (count != 0u) {
        if (count > SIZE_MAX / sizeof(*linemap->entries)) {
            impl = TC_IMPL_INTERNAL;
            goto fail;
        }
        linemap->entries = (TcLineMapEntry *)tc_allocator_calloc(
            linemap->alloc, count, sizeof(*linemap->entries));
        if (linemap->entries == NULL) {
            impl = TC_IMPL_OOM;
            goto fail;
        }
    }
    for (i = 0u; i < ir->nunits; i++) {
        const TcUnit *unit = &ir->units[i];
        const char *func;
        const char *module;
        uint32_t func_id;
        size_t b;
        if (unit->func_id == TC_UNIT_TOP_LEVEL) {
            func = "<top>";
            module = ir->modules[0].path;
            func_id = TC_UNIT_TOP_LEVEL;
        } else {
            const TcFunc *definition;
            if (unit->func_id >= ir->nfuncs) {
                impl = TC_IMPL_INTERNAL;
                goto fail;
            }
            definition = &ir->funcs[unit->func_id];
            if (definition->module >= ir->nmodules ||
                definition->name == NULL || definition->body != unit) {
                impl = TC_IMPL_INTERNAL;
                goto fail;
            }
            func = definition->name;
            module = ir->modules[definition->module].path;
            func_id = definition->id;
        }
        for (b = 0u; b < unit->nblocks; b++) {
            const TcBlock *block = &unit->blocks[b];
            size_t prior;
            size_t j;
            for (prior = 0u; prior < b; prior++) {
                if (unit->blocks[prior].id == block->id) {
                    impl = TC_IMPL_INTERNAL;
                    goto fail;
                }
            }
            if (block->n > UINT32_MAX) {
                impl = TC_IMPL_INTERNAL;
                goto fail;
            }
            for (j = 0u; j < block->n; j++) {
                TcLineMapEntry *entry = &linemap->entries[index];
                impl = validate_instruction(ir, unit, &block->instrs[j]);
                if (impl != TC_IMPL_OK) {
                    goto fail;
                }
                entry->module = module;
                entry->func = func;
                entry->func_id = func_id;
                entry->span = &block->instrs[j].span;
                entry->instr = &block->instrs[j];
                entry->bb = block->id;
                entry->insn = (uint32_t)j;
                entry->external = external_display_path(entry->span->file);
                impl = add_file(linemap, entry->span->file, &entry->file_id);
                if (impl != TC_IMPL_OK) {
                    goto fail;
                }
                index++;
            }
        }
    }
    impl = build_lookup(linemap);
    if (impl != TC_IMPL_OK) {
        goto fail;
    }
    *out = linemap;
    return TC_IMPL_OK;

fail:
    tc_linemap_destroy(linemap);
    return impl;
}

void tc_linemap_destroy(TcLineMap *linemap) {
    if (linemap == NULL) {
        return;
    }
    linemap->alloc->free(linemap->alloc->ctx, linemap->lookup);
    linemap->alloc->free(linemap->alloc->ctx, linemap->files);
    linemap->alloc->free(linemap->alloc->ctx, linemap->entries);
    linemap->alloc->free(linemap->alloc->ctx, linemap);
}

const TcLineMapEntry *tc_linemap_find_instr(const TcLineMap *linemap,
                                           const TcInstr *instr) {
    size_t slot;
    if (linemap == NULL || linemap->lookup == NULL || instr == NULL) {
        return NULL;
    }
    slot = pointer_hash(instr) & (linemap->lookup_capacity - 1u);
    while (linemap->lookup[slot] != 0u) {
        const TcLineMapEntry *entry =
            &linemap->entries[linemap->lookup[slot] - 1u];
        if (entry->instr == instr) {
            return entry;
        }
        slot = (slot + 1u) & (linemap->lookup_capacity - 1u);
    }
    return NULL;
}

int tc_linemap_instr_index(const TcLineMap *linemap, const TcInstr *instr,
                           size_t *out_index) {
    const TcLineMapEntry *entry;
    if (out_index == NULL) {
        return 0;
    }
    *out_index = 0u;
    entry = tc_linemap_find_instr(linemap, instr);
    if (entry == NULL) {
        return 0;
    }
    *out_index = (size_t)(entry - linemap->entries);
    return 1;
}

static int json_string(FILE *out, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (fputc('"', out) == EOF) {
        return 0;
    }
    while (*cursor != 0u) {
        unsigned char ch = *cursor++;
        if (ch == '"' || ch == '\\') {
            if (fputc('\\', out) == EOF || fputc(ch, out) == EOF) {
                return 0;
            }
        } else if (ch < 0x20u) {
            if (fprintf(out, "\\u%04x", (unsigned)ch) < 0) {
                return 0;
            }
        } else if (fputc(ch, out) == EOF) {
            return 0;
        }
    }
    return fputc('"', out) != EOF;
}

TcImplError tc_linemap_write_json(const TcLineMap *linemap, FILE *out,
                                  int include_vm_addr, uint64_t vm_code_base) {
    size_t i;
    if (linemap == NULL || out == NULL || linemap->source == NULL ||
        linemap->profile == NULL) {
        return TC_IMPL_INTERNAL;
    }
    if (include_vm_addr) {
        for (i = 0u; i < linemap->count; i++) {
            if (i > (UINT64_MAX - vm_code_base) / 4u) {
                return TC_IMPL_INTERNAL;
            }
        }
    }
    if (fprintf(out,
                "{\"format\":\"tc-linemap\",\"version\":%u,\"profile\":",
                TC_LINEMAP_VERSION) < 0 ||
        !json_string(out, linemap->profile) || fputs(",\"source\":", out) == EOF ||
        !json_string(out, linemap->source) ||
        fputs(",\"entries\":[", out) == EOF) {
        return TC_IMPL_IO;
    }
    for (i = 0u; i < linemap->count; i++) {
        const TcLineMapEntry *entry = &linemap->entries[i];
        if ((i != 0u && fputc(',', out) == EOF) ||
            fputs("{\"kind\":\"", out) == EOF ||
            fputs(strcmp(entry->func, "<top>") == 0 ? "unit" : "function",
                  out) == EOF ||
            fputs("\",\"module\":", out) == EOF ||
            !json_string(out, entry->module) ||
            fputs(",\"func\":", out) == EOF || !json_string(out, entry->func) ||
            fprintf(out, ",\"bb\":%u,\"insn\":%u,\"file\":",
                    entry->bb, entry->insn) < 0 ||
            !json_string(out, entry->span->file) ||
            fprintf(out, ",\"line\":%u,\"col\":%u",
                    entry->span->line, entry->span->column) < 0) {
            return TC_IMPL_IO;
        }
        if (include_vm_addr &&
            fprintf(out, ",\"vm_addr\":%" PRIu64,
                    vm_code_base + 4u * (uint64_t)i) < 0) {
            return TC_IMPL_IO;
        }
        if (entry->external && fputs(",\"external\":true", out) == EOF) {
            return TC_IMPL_IO;
        }
        if (fputc('}', out) == EOF) {
            return TC_IMPL_IO;
        }
    }
    if (fputs("]}\n", out) == EOF || fflush(out) == EOF || ferror(out)) {
        return TC_IMPL_IO;
    }
    return TC_IMPL_OK;
}
