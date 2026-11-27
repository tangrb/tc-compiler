#include "driver/observe.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/fd.h"
#include "driver/cli.h"
#include "ir/print.h"
#include "vm/vm.h"

static int same_identity(const struct stat *left, const struct stat *right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

int tc_observe_json_string(FILE *out, const char *value) {
    const unsigned char *cursor = (const unsigned char *)(value != NULL ? value : "");
    if (fputc('"', out) == EOF) {
        return -1;
    }
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\') {
            if (fputc('\\', out) == EOF || fputc(*cursor, out) == EOF) {
                return -1;
            }
        } else if (*cursor < 0x20u) {
            if (fprintf(out, "\\u%04x", (unsigned int)*cursor) < 0) {
                return -1;
            }
        } else if (fputc(*cursor, out) == EOF) {
            return -1;
        }
        cursor++;
    }
    return fputc('"', out) == EOF ? -1 : 0;
}

static int stream_begin(TcAotOutput *output, FILE *default_stream,
                        TcObserveStream *stream) {
    struct stat current;
    memset(stream, 0, sizeof(*stream));
    if (output->path == NULL) {
        stream->file = default_stream;
        return output->fd >= 0 ? TC_IMPL_INTERNAL : TC_IMPL_OK;
    }
    if (output->fd < 0 || fstat(output->fd, &current) != 0 ||
        !same_identity(&current, &output->identity) ||
        !S_ISREG(current.st_mode) || ftruncate(output->fd, 0) != 0 ||
        lseek(output->fd, 0, SEEK_SET) < 0) {
        return TC_IMPL_IO;
    }
    stream->file = fdopen(output->fd, "w");
    if (stream->file == NULL) {
        return TC_IMPL_IO;
    }
    output->fd = -1;
    stream->close_file = 1;
    return TC_IMPL_OK;
}

TcImplError tc_observe_stream_finish(TcObserveStream *stream) {
    int failed = stream == NULL || stream->file == NULL;
    if (stream == NULL) {
        return TC_IMPL_INTERNAL;
    }
    if (stream->file != NULL) {
        if (fflush(stream->file) != 0) {
            failed = 1;
        }
        if (stream->close_file && fclose(stream->file) != 0) {
            failed = 1;
        }
    }
    stream->file = NULL;
    return failed ? TC_IMPL_IO : TC_IMPL_OK;
}

TcImplError tc_observe_dump_ir(const TcIR *ir, TcAotOutputs *outputs) {
    TcObserveStream stream;
    TcImplError impl;
    if (ir == NULL || outputs == NULL) {
        return TC_IMPL_INTERNAL;
    }
    impl = stream_begin(&outputs->output[TC_AOT_OUTPUT_DUMP], stderr, &stream);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (tc_ir_print(ir, stream.file) != 0 || ferror(stream.file)) {
        stream.failed = 1;
    }
    if (tc_observe_stream_finish(&stream) != TC_IMPL_OK) {
        stream.failed = 1;
    }
    return stream.failed ? TC_IMPL_IO : TC_IMPL_OK;
}

TcImplError tc_observe_dump_linemap(const TcLineMap *linemap,
                                    TcAotOutputs *outputs,
                                    int include_vm_addr,
                                    uint64_t vm_code_base) {
    TcObserveStream stream;
    TcImplError impl;
    if (linemap == NULL || outputs == NULL) {
        return TC_IMPL_INTERNAL;
    }
    impl = stream_begin(&outputs->output[TC_AOT_OUTPUT_LINEMAP], stderr,
                        &stream);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    impl = tc_linemap_write_json(linemap, stream.file, include_vm_addr,
                                 vm_code_base);
    if (tc_observe_stream_finish(&stream) != TC_IMPL_OK && impl == TC_IMPL_OK) {
        impl = TC_IMPL_IO;
    }
    return impl;
}

typedef struct {
    char name[64];
    uint64_t count;
} TcOpCount;

static void count_instruction(TcOpCount *counts, size_t *count,
                              const TcInstr *instr, uint64_t amount) {
    char name[64];
    size_t i;
    if (tc_ir_instr_name(instr, name, sizeof(name)) == NULL) {
        return;
    }
    for (i = 0u; i < *count; i++) {
        if (strcmp(counts[i].name, name) == 0) {
            counts[i].count += amount;
            return;
        }
    }
    if (*count < (size_t)TC_OPCODE_COUNT * 6u) {
        memcpy(counts[*count].name, name, strlen(name) + 1u);
        counts[*count].count = amount;
        (*count)++;
    }
}

static int compare_counts(const void *left, const void *right) {
    const TcOpCount *a = (const TcOpCount *)left;
    const TcOpCount *b = (const TcOpCount *)right;
    return strcmp(a->name, b->name);
}

static TcImplError write_op_counts(FILE *out, const TcOpCount *counts,
                                   size_t count) {
    size_t i;
    qsort((void *)counts, count, sizeof(*counts), compare_counts);
    for (i = 0u; i < count; i++) {
        if (tc_cli_write(out, "op.%s=%" PRIu64 "\n", counts[i].name,
                         counts[i].count) != 0) {
            return TC_IMPL_IO;
        }
    }
    return TC_IMPL_OK;
}

TcImplError tc_observe_stats_begin_ir(const TcIR *ir,
                                      const TcPlatformProfile *profile,
                              const TcCliOptions *options,
                              TcAotOutputs *outputs,
                              TcAotObserveStats *stats) {
    size_t i;
    size_t j;
    TcImplError impl;
    if (stats == NULL) {
        return TC_IMPL_INTERNAL;
    }
    memset(stats, 0, sizeof(*stats));
    if (!options->stats) {
        return TC_IMPL_OK;
    }
    if (ir == NULL || profile == NULL || outputs == NULL) {
        return TC_IMPL_INTERNAL;
    }
    stats->ir = ir;
    stats->funcs = ir->nfuncs;
    for (i = 0u; i < ir->nunits; i++) {
        const TcUnit *unit = &ir->units[i];
        for (j = 0u; j < unit->nblocks; j++) {
            const TcBlock *block = &unit->blocks[j];
            stats->ir_instrs += block->n;
        }
    }
    for (i = 0u; i < ir->nobjects; i++) {
        if (ir->objects[i].region == TC_REG_STATIC) {
            stats->static_bytes += ir->objects[i].size;
        }
    }
    impl = stream_begin(&outputs->output[TC_AOT_OUTPUT_STATS], stderr,
                        &stats->stream);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (tc_cli_write(stats->stream.file, "# tc-stats 1 tc-aot %s\n"
                     "funcs=%" PRIu64 "\n"
                     "ir_instrs=%" PRIu64 "\n"
                     "static_bytes=%" PRIu64 "\n",
                     profile->target != NULL ? profile->target->name : "-",
                     stats->funcs, stats->ir_instrs, stats->static_bytes) != 0) {
        impl = TC_IMPL_IO;
    } else {
        /* Keep the stream open so target fields can precede op counts. */
        return TC_IMPL_OK;
    }
    (void)tc_observe_stream_finish(&stats->stream);
    return impl;
}

TcImplError tc_observe_stats_finish_aot(TcAotObserveStats *stats,
                                        const TcTargetStats *target) {
    TcOpCount *counts;
    size_t count = 0u;
    size_t i;
    size_t j;
    TcImplError impl = TC_IMPL_OK;
    if (stats == NULL) {
        return TC_IMPL_INTERNAL;
    }
    if (stats->ir == NULL) {
        return TC_IMPL_OK;
    }
    counts = (TcOpCount *)calloc((size_t)TC_OPCODE_COUNT * 6u, sizeof(*counts));
    if (counts == NULL) {
        impl = TC_IMPL_OOM;
    } else {
        for (i = 0u; i < stats->ir->nunits; i++) {
            const TcUnit *unit = &stats->ir->units[i];
            for (j = 0u; j < unit->nblocks; j++) {
                const TcBlock *block = &unit->blocks[j];
                size_t k;
                for (k = 0u; k < block->n; k++) {
                    count_instruction(counts, &count, &block->instrs[k], 1u);
                }
            }
        }
        if (target != NULL &&
            tc_cli_write(stats->stream.file, "frame_max=%" PRIu64 "\n"
                         "code_bytes=%" PRIu64 "\n",
                         target->frame_max, target->code_bytes) != 0) {
            impl = TC_IMPL_IO;
        } else {
            impl = write_op_counts(stats->stream.file, counts, count);
        }
        free(counts);
    }
    if (tc_observe_stream_finish(&stats->stream) != TC_IMPL_OK) {
        impl = TC_IMPL_IO;
    }
    return impl;
}

TcImplError tc_observe_stats_vm(const TcCliOptions *options,
                                const TcPlatformProfile *profile,
                        const TcVmStats *stats, TcAotOutputs *outputs) {
    TcObserveStream stream;
    TcOpCount counts[TC_VM_OP_SLOT_COUNT];
    size_t count = 0u;
    size_t i;
    TcImplError impl;
    if (!options->stats) {
        return TC_IMPL_OK;
    }
    if (profile == NULL || stats == NULL || outputs == NULL) {
        return TC_IMPL_INTERNAL;
    }
    impl = stream_begin(&outputs->output[TC_AOT_OUTPUT_STATS], stderr, &stream);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (tc_cli_write(stream.file,
                     "# tc-stats 1 tc-vm %s\n"
                     "executed=%" PRIu64 "\n"
                     "depth_max=%" PRIu64 "\n"
                     "region_peak.frame=%" PRIu64 "\n"
                     "region_peak.static=%" PRIu64 "\n"
                     "region_peak.device=%" PRIu64 "\n"
                     "region_peak.heap=%" PRIu64 "\n"
                     "region_peak.code=%" PRIu64 "\n"
                     "allocations=%" PRIu64 "\n"
                     "io_bytes=%" PRIu64 "\n",
                     profile->target != NULL ? profile->target->name : "-",
                     stats->executed, stats->depth_max,
                     stats->region_peak[TC_REG_FRAME],
                     stats->region_peak[TC_REG_STATIC],
                     stats->region_peak[TC_REG_DEVICE],
                     stats->region_peak[TC_REG_HEAP],
                     stats->region_peak[TC_REG_CODE], stats->allocations,
                     stats->io_bytes) != 0) {
        impl = TC_IMPL_IO;
    } else if (stats->trapped &&
               tc_cli_write(stream.file, "first_trap=%s\n",
                            tc_diag_info((TcDiagCode)stats->first_trap) != NULL
                                ? tc_diag_info((TcDiagCode)stats->first_trap)->symbol
                                : "TC_RE_TRAP") != 0) {
        impl = TC_IMPL_IO;
    } else {
        for (i = 0u; i < (size_t)TC_VM_OP_SLOT_COUNT; i++) {
            if (stats->op_counts[i] != 0u) {
                TcInstr instr;
                memset(&instr, 0, sizeof(instr));
                instr.op = (TcOpcode)(i / 6u);
                instr.mode = (TcOpMode)(i % 6u);
                count_instruction(counts, &count, &instr,
                                 stats->op_counts[i]);
            }
        }
        if (impl == TC_IMPL_OK) {
            impl = write_op_counts(stream.file, counts, count);
        }
    }
    if (tc_observe_stream_finish(&stream) != TC_IMPL_OK) {
        impl = TC_IMPL_IO;
    }
    return impl;
}

TcImplError tc_observe_trace_begin(TcAotOutputs *outputs,
                                   TcObserveStream *stream) {
    if (outputs == NULL || stream == NULL) {
        return TC_IMPL_INTERNAL;
    }
    return stream_begin(&outputs->output[TC_AOT_OUTPUT_TRACE], stderr, stream);
}

void tc_observe_instr_name(const TcVmResult *result, const TcLineMap *linemap,
                           char *buffer, size_t capacity) {
    size_t i;
    if (result == NULL || buffer == NULL || capacity == 0u) {
        return;
    }
    if (linemap != NULL) {
        for (i = 0u; i < linemap->count; i++) {
            const TcLineMapEntry *entry = &linemap->entries[i];
            if (entry->instr != NULL && entry->span != NULL &&
                entry->span->line == result->span.line &&
                entry->span->column == result->span.column &&
                entry->bb == result->bb && entry->insn == result->insn &&
                entry->instr->op == result->op &&
                entry->module != NULL && result->module != NULL &&
                strcmp(entry->module, result->module) == 0 &&
                ((result->entity_kind == 0 &&
                  strcmp(entry->func, "<top>") == 0) ||
                 (result->entity_kind == 1 && result->func != NULL &&
                  strcmp(entry->func, result->func) == 0))) {
                (void)tc_ir_instr_name(entry->instr, buffer, capacity);
                return;
            }
        }
    }
    if (tc_ir_op_name(result->op) != NULL) {
        (void)snprintf(buffer, capacity, "%s", tc_ir_op_name(result->op));
    } else {
        (void)snprintf(buffer, capacity, "?");
    }
}
