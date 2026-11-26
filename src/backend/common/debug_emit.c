#include "backend/common/debug_emit.h"

#include <stdint.h>

static int asm_string(FILE *out, const char *text) {
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
        } else if (fputc(ch, out) == EOF) {
            return 0;
        }
    }
    return fputc('"', out) != EOF;
}

TcImplError tc_debug_emit_files(FILE *out, const TcLineMap *linemap) {
    size_t i;
    if (out == NULL || linemap == NULL) {
        return TC_IMPL_INTERNAL;
    }
    for (i = 0u; i < linemap->file_count; i++) {
        const TcLineMapFile *file = &linemap->files[i];
        if (file->path == NULL || file->path[0] == '\0' || file->id == 0u) {
            return TC_IMPL_INTERNAL;
        }
        if (fprintf(out, "  .file %u ", file->id) < 0 ||
            !asm_string(out, file->path) || fputc('\n', out) == EOF) {
            return TC_IMPL_IO;
        }
    }
    return ferror(out) ? TC_IMPL_IO : TC_IMPL_OK;
}

TcImplError tc_debug_emit_begin(FILE *out) {
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    if (fputs(".Ltc_debug_begin:\n", out) == EOF) {
        return TC_IMPL_IO;
    }
    return TC_IMPL_OK;
}

static int debug_string(FILE *out, const char *text) {
    if (fputc('"', out) == EOF) {
        return 0;
    }
    while (*text != '\0') {
        unsigned char ch = (unsigned char)*text++;
        if ((ch == '"' || ch == '\\') &&
            fputc('\\', out) == EOF) {
            return 0;
        }
        if (fputc(ch, out) == EOF) {
            return 0;
        }
    }
    return fputc('"', out) != EOF;
}

TcImplError tc_debug_emit_end(FILE *out, const TcLineMap *linemap,
                              unsigned pointer_size) {
    static const char abbrev[] =
        "\x01\x11\x00"
        "\x10\x17\x1b\x0e\x03\x0e\x11\x01\x12\x06"
        "\x00\x00\x00";
    unsigned unit_length;
    const char *section_byte;
    if (out == NULL || linemap == NULL || linemap->source == NULL ||
        (pointer_size != 4u && pointer_size != 8u)) {
        return TC_IMPL_INTERNAL;
    }
    section_byte = "%";
    unit_length = 2u + 4u + 1u + 1u + 4u + 4u + 4u + pointer_size + 4u;
    if (fputs(".Ltc_debug_end:\n"
              "  .section .debug_abbrev,\"\",", out) == EOF ||
        fputs(section_byte, out) == EOF ||
        fputs("progbits\n  .byte ", out) == EOF) {
        return TC_IMPL_IO;
    }
    {
        size_t i;
        for (i = 0u; i < sizeof(abbrev) - 1u; i++) {
            if (fprintf(out, "%u%s", (unsigned)(unsigned char)abbrev[i],
                        i + 1u == sizeof(abbrev) - 1u ? "\n" : ",") < 0) {
                return TC_IMPL_IO;
            }
        }
    }
    if (fprintf(out,
                "  .section .debug_str,\"MS\",%sprogbits,1\n"
                ".Ltc_debug_comp_dir:\n"
                "  .asciz \".\"\n"
                ".Ltc_debug_name:\n"
                "  .asciz ", section_byte) < 0 ||
        !debug_string(out, linemap->source) || fputc('\n', out) == EOF ||
        fprintf(out,
                "  .section .debug_info,\"\",%sprogbits\n"
                "  .long %u\n"
                "  .short 4\n"
                "  .long 0\n"
                "  .byte %u\n"
                "  .byte 1\n"
                "  .long .debug_line\n"
                "  .long .Ltc_debug_comp_dir\n"
                "  .long .Ltc_debug_name\n"
                "  .%s .Ltc_debug_begin\n"
                "  .long .Ltc_debug_end - .Ltc_debug_begin\n"
                "  .section .text\n",
                section_byte, unit_length, pointer_size,
                pointer_size == 8u ? "quad" : "long") < 0) {
        return TC_IMPL_IO;
    }
    return ferror(out) ? TC_IMPL_IO : TC_IMPL_OK;
}

TcImplError tc_debug_emit_location(FILE *out, const TcLineMap *linemap,
                                   const TcInstr *instr) {
    const TcLineMapEntry *entry;
    if (out == NULL || linemap == NULL || instr == NULL) {
        return TC_IMPL_INTERNAL;
    }
    entry = tc_linemap_find_instr(linemap, instr);
    if (entry == NULL || entry->span == NULL || entry->span->file == NULL ||
        entry->file_id == 0u || entry->span->line == 0u ||
        entry->span->column == 0u) {
        return TC_IMPL_INTERNAL;
    }
    if (fprintf(out, "  .loc %u %u %u\n", entry->file_id, entry->span->line,
                entry->span->column) < 0 ||
        fprintf(out, "  # source: %s:%u\n", entry->span->file,
                entry->span->line) < 0) {
        return TC_IMPL_IO;
    }
    return ferror(out) ? TC_IMPL_IO : TC_IMPL_OK;
}
