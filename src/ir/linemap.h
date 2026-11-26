#ifndef TC_IR_LINEMAP_H
#define TC_IR_LINEMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "diagnostic/diagnostic.h"
#include "ir/ir.h"

#define TC_LINEMAP_VERSION 3u

typedef struct {
    const char *module;
    const char *func;
    const TcSpan *span;
    const TcInstr *instr;
    uint32_t bb;
    uint32_t insn;
    uint32_t file_id;
    int external;
} TcLineMapEntry;

typedef struct {
    const char *path;
    uint32_t id;
} TcLineMapFile;

typedef struct {
    const char *source;
    const char *profile;
    TcLineMapEntry *entries;
    size_t count;
    TcLineMapFile *files;
    size_t file_count;
    size_t *lookup;
    size_t lookup_capacity;
    const TcAllocator *alloc;
} TcLineMap;

TcImplError tc_linemap_build(const TcIR *ir, TcLineMap **out);
void tc_linemap_destroy(TcLineMap *linemap);
const TcLineMapEntry *tc_linemap_find_instr(const TcLineMap *linemap,
                                           const TcInstr *instr);
TcImplError tc_linemap_write_json(const TcLineMap *linemap, FILE *out);

#endif
