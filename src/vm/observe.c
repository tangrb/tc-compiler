#include "vm/internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "ir/print.h"

static int trace_write(TcVm *vm, const char *format, ...) {
    va_list args;
    int result;
    if (vm->trace_failed) {
        return 0;
    }
    va_start(args, format);
    result = vfprintf(vm->observer->trace, format, args);
    va_end(args);
    if (result < 0 || fflush(vm->observer->trace) != 0) {
        vm->trace_failed = 1;
        return 0;
    }
    return 1;
}

static int json_string(TcVm *vm, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (!trace_write(vm, "\"")) {
        return 0;
    }
    while (*cursor != 0u) {
        unsigned char ch = *cursor++;
        if (ch == '"' || ch == '\\') {
            if (!trace_write(vm, "\\%c", ch)) {
                return 0;
            }
        } else if (ch < 0x20u) {
            if (!trace_write(vm, "\\u%04x", (unsigned)ch)) {
                return 0;
            }
        } else if (!trace_write(vm, "%c", ch)) {
            return 0;
        }
    }
    return trace_write(vm, "\"");
}

static int json_span(TcVm *vm, const TcSpan *span) {
    const unsigned char *cursor = (const unsigned char *)span->file;
    if (!trace_write(vm, "\"")) {
        return 0;
    }
    while (*cursor != 0u) {
        unsigned char ch = *cursor++;
        if (ch == '"' || ch == '\\') {
            if (!trace_write(vm, "\\%c", ch)) {
                return 0;
            }
        } else if (ch < 0x20u) {
            if (!trace_write(vm, "\\u%04x", (unsigned)ch)) {
                return 0;
            }
        } else if (!trace_write(vm, "%c", ch)) {
            return 0;
        }
    }
    return trace_write(vm, ":%u:%u\"", span->line, span->column);
}

typedef enum {
    TC_DETAIL_NONE,
    TC_DETAIL_CALL,
    TC_DETAIL_MEM,
    TC_DETAIL_MEMMOVE,
    TC_DETAIL_IO,
    TC_DETAIL_TRAP
} TcDetailKind;

typedef struct {
    TcDetailKind kind;
    const char *text;
    const char *callee;
    const char *callee_module;
    const char *region;
    const char *dst_region;
    const char *src_region;
    uint64_t offset;
    uint64_t dst_offset;
    uint64_t src_offset;
    uint64_t size;
    uint64_t bytes;
    const char *code;
} TcTraceDetail;

static const char *region_name(TcRegionKind region) {
    static const char *const names[TC_REGION_COUNT] = {
        "frame", "static", "device", "code", "heap"
    };
    return (unsigned)region < TC_REGION_COUNT ? names[region] : "?";
}

static int address_detail(TcVm *vm, uint64_t address, const char **region,
                          uint64_t *offset) {
    unsigned index;
    for (index = 0u; index < (unsigned)TC_REGION_COUNT; index++) {
        const TcRegionDesc *desc = &vm->profile->region[index];
        if (address >= desc->base && address - desc->base < desc->size) {
            *region = region_name((TcRegionKind)index);
            *offset = address - desc->base;
            return 1;
        }
    }
    return 0;
}

static void count_overflow(TcVm *vm) {
    vm->count_overflow = 1;
}

static void count_u64(TcVm *vm, uint64_t *value) {
    if (*value == UINT64_MAX) {
        count_overflow(vm);
    } else {
        (*value)++;
    }
}

static void trace_control(TcVm *vm, const char *text) {
    (void)trace_write(vm, "%s\n", text);
}

static int json_detail(TcVm *vm, const TcTraceDetail *detail) {
    if (detail->kind == TC_DETAIL_NONE) {
        return 1;
    }
    if (!trace_write(vm, ",\"detail\":{")) {
        return 0;
    }
    switch (detail->kind) {
    case TC_DETAIL_CALL:
        return trace_write(vm, "\"direction\":") &&
               json_string(vm, detail->text) &&
               trace_write(vm, ",\"callee\":") &&
               json_string(vm, detail->callee) &&
               trace_write(vm, ",\"callee_module\":") &&
               json_string(vm, detail->callee_module) &&
               trace_write(vm, "}");
    case TC_DETAIL_MEM:
        return trace_write(vm, "\"region\":") &&
               json_string(vm, detail->region) &&
               trace_write(vm, ",\"offset\":%" PRIu64 ",\"size\":%" PRIu64 "}",
                           detail->offset, detail->size);
    case TC_DETAIL_MEMMOVE:
        return trace_write(vm, "\"dst_region\":") &&
               json_string(vm, detail->dst_region) &&
               trace_write(vm, ",\"dst_offset\":%" PRIu64 ",\"src_region\":",
                           detail->dst_offset) &&
               json_string(vm, detail->src_region) &&
               trace_write(vm, ",\"src_offset\":%" PRIu64 ",\"size\":%" PRIu64 "}",
                           detail->src_offset, detail->size);
    case TC_DETAIL_IO:
        return trace_write(vm, "\"bytes\":%" PRIu64 ",\"result\":",
                           detail->bytes) &&
               json_string(vm, detail->text) && trace_write(vm, "}");
    case TC_DETAIL_TRAP:
        return trace_write(vm, "\"code\":") &&
               json_string(vm, detail->code) && trace_write(vm, "}");
    case TC_DETAIL_NONE:
        break;
    }
    return 0;
}

static void emit_record(TcVm *vm, const TcInstr *instr,
                        const TcTraceDetail *detail, unsigned caller_depth) {
    const TcLineMapEntry *entry;
    char op[64];
    uint64_t seq;
    if (vm->observer == NULL || vm->observer->trace == NULL ||
        vm->observer->kinds == 0u || vm->trace_failed) {
        return;
    }
    entry = tc_linemap_find_instr(vm->observer->linemap, instr);
    if (entry == NULL) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return;
    }
    if (vm->observer->limit != 0u &&
        vm->trace_records >= vm->observer->limit) {
        count_u64(vm, &vm->trace_dropped);
        if (!vm->trace_truncated) {
            vm->trace_truncated = 1;
            trace_control(vm, vm->observer->jsonl
                                  ? "{\"type\":\"truncated\"}"
                                  : "# tc-trace truncated");
        }
        return;
    }
    if (vm->trace_records == UINT64_MAX) {
        count_overflow(vm);
        return;
    }
    seq = ++vm->trace_records;
    (void)tc_ir_instr_name(instr, op, sizeof(op));
    if (vm->observer->jsonl) {
        if (!trace_write(vm,
                "{\"seq\":%" PRIu64 ",\"depth\":%u,\"kind\":\"%s\","
                "\"module\":",
                seq, caller_depth,
                strcmp(entry->func, "<top>") == 0 ? "unit" : "function") ||
            !json_string(vm, entry->module) ||
            !trace_write(vm, ",\"func\":") ||
            !json_string(vm, entry->func) ||
            !trace_write(vm, ",\"bb\":%u,\"insn\":%u,\"op\":",
                         entry->bb, entry->insn) ||
            !json_string(vm, op) || !trace_write(vm, ",\"span\":") ||
            !json_span(vm, entry->span) || !json_detail(vm, detail) ||
            !trace_write(vm, "}\n")) {
            return;
        }
        return;
    }
    if (!trace_write(vm, "[%04" PRIu64 "] depth=%u kind=%s module=%s func=%s "
                    "bb=%u insn=%u op=%s span=%s:%u:%u",
                    seq, caller_depth,
                    strcmp(entry->func, "<top>") == 0 ? "unit" : "function",
                    entry->module, entry->func, entry->bb, entry->insn, op,
                    entry->span->file, entry->span->line,
                    entry->span->column)) {
        return;
    }
    if (detail->kind == TC_DETAIL_CALL) {
        (void)trace_write(vm, " detail=direction:%s,callee:%s,callee_module:%s",
                          detail->text, detail->callee,
                          detail->callee_module);
    } else if (detail->kind == TC_DETAIL_MEM) {
        (void)trace_write(vm,
                          " detail=region:%s,offset:0x%" PRIx64 ",size:%" PRIu64,
                          detail->region, detail->offset, detail->size);
    } else if (detail->kind == TC_DETAIL_MEMMOVE) {
        (void)trace_write(vm,
                          " detail=dst_region:%s,dst_offset:0x%" PRIx64
                          ",src_region:%s,src_offset:0x%" PRIx64 ",size:%" PRIu64,
                          detail->dst_region, detail->dst_offset,
                          detail->src_region, detail->src_offset, detail->size);
    } else if (detail->kind == TC_DETAIL_IO) {
        (void)trace_write(vm, " detail=bytes:%" PRIu64 ",result:%s",
                          detail->bytes, detail->text);
    } else if (detail->kind == TC_DETAIL_TRAP) {
        (void)trace_write(vm, " detail=code:%s", detail->code);
    }
    (void)trace_write(vm, "\n");
}

void tc_vm_observe_ir(TcVm *vm, const TcInstr *instr) {
    TcTraceDetail detail;
    if (vm->stats != NULL) {
        size_t slot = (size_t)instr->op * 6u + (size_t)instr->mode;
        count_u64(vm, &vm->stats->executed);
        count_u64(vm, &vm->stats->op_counts[slot]);
        if (vm->depth > vm->stats->depth_max) {
            vm->stats->depth_max = vm->depth;
        }
    }
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_IR)) {
        return;
    }
    memset(&detail, 0, sizeof(detail));
    emit_record(vm, instr, &detail, vm->depth);
}

void tc_vm_observe_mem(TcVm *vm, uint64_t address, uint64_t size) {
    TcTraceDetail detail;
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_MEM)) {
        return;
    }
    memset(&detail, 0, sizeof(detail));
    detail.kind = TC_DETAIL_MEM;
    detail.size = size;
    if (!address_detail(vm, address, &detail.region, &detail.offset)) {
        return;
    }
    emit_record(vm, vm->current_instr, &detail, vm->depth);
}

void tc_vm_observe_memmove(TcVm *vm, uint64_t dst, uint64_t src,
                           uint64_t size) {
    TcTraceDetail detail;
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_MEM)) {
        return;
    }
    memset(&detail, 0, sizeof(detail));
    detail.kind = TC_DETAIL_MEMMOVE;
    detail.size = size;
    if (!address_detail(vm, dst, &detail.dst_region, &detail.dst_offset) ||
        !address_detail(vm, src, &detail.src_region, &detail.src_offset)) {
        return;
    }
    emit_record(vm, vm->current_instr, &detail, vm->depth);
}

void tc_vm_observe_io(TcVm *vm, uint64_t bytes, const char *result) {
    TcTraceDetail detail;
    if (vm->stats != NULL && strcmp(result, "ok") == 0) {
        if (bytes > UINT64_MAX - vm->stats->io_bytes) {
            count_overflow(vm);
        } else {
            vm->stats->io_bytes += bytes;
        }
    }
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_IO)) {
        return;
    }
    memset(&detail, 0, sizeof(detail));
    detail.kind = TC_DETAIL_IO;
    detail.bytes = bytes;
    detail.text = result;
    emit_record(vm, vm->current_instr, &detail, vm->depth);
}

void tc_vm_observe_call(TcVm *vm, const TcInstr *instr,
                        const TcFunc *callee, int returning) {
    TcTraceDetail detail;
    unsigned depth = vm->depth;
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_CALL)) {
        return;
    }
    if (returning && depth != 0u) {
        depth--;
    }
    memset(&detail, 0, sizeof(detail));
    detail.kind = TC_DETAIL_CALL;
    detail.text = returning ? "return" : "enter";
    detail.callee = callee->name;
    detail.callee_module = vm->ir->modules[callee->module].path;
    emit_record(vm, instr, &detail, depth);
}

void tc_vm_observe_trap(TcVm *vm, TcErrorKind kind) {
    TcTraceDetail detail;
    const TcDiagInfo *info;
    if (vm->observer == NULL || !(vm->observer->kinds & TC_TRACE_TRAP) ||
        vm->current_instr == NULL) {
        return;
    }
    info = tc_diag_info((TcDiagCode)kind);
    if (info == NULL) {
        tc_vm_impl(vm, TC_IMPL_INTERNAL);
        return;
    }
    memset(&detail, 0, sizeof(detail));
    detail.kind = TC_DETAIL_TRAP;
    detail.code = info->symbol;
    emit_record(vm, vm->current_instr, &detail, vm->depth);
}

void tc_vm_trace_header(TcVm *vm) {
    if (vm->observer == NULL || vm->observer->trace == NULL ||
        vm->observer->linemap == NULL) {
        if (vm->observer != NULL && vm->observer->trace != NULL) {
            tc_vm_impl(vm, TC_IMPL_INTERNAL);
        }
        return;
    }
    vm->trace_started = 1;
    if (vm->observer->jsonl) {
        if (!trace_write(vm, "{\"type\":\"header\",\"format\":\"tc-trace\","
                         "\"version\":3,\"tool\":\"tc-vm\",\"profile\":") ||
            !json_string(vm, vm->observer->linemap->profile) ||
            !trace_write(vm, ",\"source\":") ||
            !json_string(vm, vm->observer->linemap->source) ||
            !trace_write(vm, "}\n")) {
            return;
        }
    } else {
        (void)trace_write(vm, "# tc-trace 3 tc-vm %s %s\n",
                          vm->observer->linemap->profile,
                          vm->observer->linemap->source);
    }
}

void tc_vm_trace_footer(TcVm *vm, const char *status) {
    if (!vm->trace_started || vm->trace_failed || vm->count_overflow) {
        return;
    }
    if (vm->observer->jsonl) {
        (void)trace_write(vm, "{\"type\":\"footer\",\"records\":%" PRIu64
                          ",\"dropped\":%" PRIu64 ",\"status\":\"%s\"}\n",
                          vm->trace_records, vm->trace_dropped, status);
    } else if (vm->trace_dropped != 0u) {
        (void)trace_write(vm,
                          "# tc-trace end %" PRIu64 " %s dropped=%" PRIu64 "\n",
                          vm->trace_records, status, vm->trace_dropped);
    } else {
        (void)trace_write(vm, "# tc-trace end %" PRIu64 " %s\n",
                          vm->trace_records, status);
    }
}
