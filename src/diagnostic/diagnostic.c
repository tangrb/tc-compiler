/* TC 0.0.46 contract: diagnostics and first-diagnostic ordering (standard §12). */
#include "diagnostic/diagnostic.h"

#include <string.h>

TcStage tc_stage_first(TcStageMask mask) {
    unsigned i;
    for (i = 0; i < (unsigned)TC_STAGE_COUNT; i++) {
        if (mask & TC_STAGE_BIT(i)) {
            return (TcStage)i;
        }
    }
    return TC_STAGE_COUNT;
}

const TcDiagInfo *tc_diag_info(TcDiagCode code) {
    if ((unsigned)code >= TC_DIAG_CODE_COUNT) {
        return NULL;
    }
    return &tc_diag_table[code];
}

const char *tc_impl_error_symbol(TcImplError err) {
    switch (err) {
    case TC_IMPL_OOM:
        return TC_ERR_OUT_OF_MEMORY_SYMBOL;
    case TC_IMPL_IO:
        return "tc_error_io";
    case TC_IMPL_USAGE:
        return "tc_error_usage";
    case TC_IMPL_UNAVAILABLE:
        return "tc_error_unavailable";
    case TC_IMPL_INTERNAL:
        return "tc_error_internal";
    case TC_IMPL_OK:
    default:
        return "tc_ok";
    }
}

const char *tc_impl_error_print_name(TcImplError err) {
    switch (err) {
    case TC_IMPL_OOM:
        return TC_ERR_OUT_OF_MEMORY_PRINT_NAME;
    case TC_IMPL_IO:
        return "IOError";
    case TC_IMPL_USAGE:
        return "UsageError";
    case TC_IMPL_UNAVAILABLE:
        return "UnavailableError";
    case TC_IMPL_INTERNAL:
        return "InternalError";
    case TC_IMPL_OK:
    default:
        return "OK";
    }
}

const char *tc_impl_error_message(TcImplError err) {
    switch (err) {
    case TC_IMPL_OOM:
        return TC_ERR_OUT_OF_MEMORY_MESSAGE;
    case TC_IMPL_IO:
        return "host I/O failed";
    case TC_IMPL_USAGE:
        return "invalid command line";
    case TC_IMPL_UNAVAILABLE:
        return "requested capability is not implemented in this phase";
    case TC_IMPL_INTERNAL:
        return "internal contract violation";
    case TC_IMPL_OK:
    default:
        return "";
    }
}

int tc_diag_make(TcDiagnostic *out, TcDiagCode code, TcStage stage,
                 uint32_t module_index, const char *file, uint32_t line,
                 uint32_t column) {
    const TcDiagInfo *info;
    if (out == NULL) {
        return 0;
    }
    info = tc_diag_info(code);
    if (info == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->code = code;
    out->stage = stage;
    out->module_index = module_index;
    out->primary.file = file;
    out->primary.line = line;
    out->primary.column = column;
    out->origin = TC_DIAG_ORIGIN_OPERAND;
    out->site = TC_DIAG_SITE_NONE;
    out->same_site_rank = info->same_site_rank;
    out->clause_rank = info->clause_rank;
    return 1;
}

static int cmp_u32(uint32_t a, uint32_t b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

int tc_diag_compare(const TcDiagnostic *a, const TcDiagnostic *b) {
    int c;
    if (a == b) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    c = cmp_u32((uint32_t)a->stage, (uint32_t)b->stage);
    if (c != 0) {
        return c;
    }
    c = cmp_u32(a->module_index, b->module_index);
    if (c != 0) {
        return c;
    }
    /* §12.2 #10: at the same call/make site, the fixed argument/field check
     * order and call-safety priority apply before source position and before
     * the generic origin/rank keys.  site_id scopes the rule to one call/make
     * construct; separate sites fall back to source order. */
    if (a->site == b->site && a->site_id != 0 && a->site_id == b->site_id &&
        (a->site == TC_DIAG_SITE_CALL || a->site == TC_DIAG_SITE_MAKE)) {
        if (a->site_phase != b->site_phase) {
            return a->site_phase < b->site_phase ? -1 : 1;
        }
        if (a->same_site_rank != b->same_site_rank) {
            return a->same_site_rank > b->same_site_rank ? -1 : 1;
        }
    }
    c = cmp_u32(a->primary.line, b->primary.line);
    if (c != 0) {
        return c;
    }
    c = cmp_u32(a->primary.column, b->primary.column);
    if (c != 0) {
        return c;
    }
    /* §12.2 #9: operand-internal diagnostics precede construct-level ones. */
    c = cmp_u32((uint32_t)a->origin, (uint32_t)b->origin);
    if (c != 0) {
        return c;
    }
    /* §12.2 #4: the more specific code wins at the same position. */
    if (a->same_site_rank != b->same_site_rank) {
        return a->same_site_rank > b->same_site_rank ? -1 : 1;
    }
    /* §12.2 #5: clause-internal order (lower rank first). */
    if (a->clause_rank != b->clause_rank) {
        return a->clause_rank < b->clause_rank ? -1 : 1;
    }
    /* Deterministic final ties; no observable difference otherwise. */
    c = cmp_u32((uint32_t)a->code, (uint32_t)b->code);
    if (c != 0) {
        return c;
    }
    if (a->sequence != b->sequence) {
        return a->sequence < b->sequence ? -1 : 1;
    }
    return 0;
}

struct TcDiagBuffer {
    const TcAllocator *alloc;
    TcDiagnostic *items;
    size_t count;
    size_t cap;
    uint64_t next_sequence;
};

TcDiagBuffer *tc_diag_buffer_create(const TcAllocator *alloc) {
    TcDiagBuffer *buffer;
    if (alloc == NULL) {
        alloc = tc_allocator_libc();
    }
    if (!tc_allocator_is_valid(alloc)) {
        return NULL;
    }
    buffer = (TcDiagBuffer *)alloc->alloc(alloc->ctx, sizeof(TcDiagBuffer));
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->alloc = alloc;
    return buffer;
}

void tc_diag_buffer_destroy(TcDiagBuffer *buffer) {
    size_t i;
    if (buffer == NULL) {
        return;
    }
    for (i = 0; i < buffer->count; i++) {
        buffer->alloc->free(buffer->alloc->ctx,
                            (void *)buffer->items[i].primary.file);
        if (buffer->items[i].has_related) {
            buffer->alloc->free(buffer->alloc->ctx,
                                (void *)buffer->items[i].related.file);
        }
    }
    if (buffer->items != NULL) {
        buffer->alloc->free(buffer->alloc->ctx, buffer->items);
    }
    buffer->alloc->free(buffer->alloc->ctx, buffer);
}

static int site_phase_valid(TcDiagPhase phase) {
    return phase == TC_DIAG_PHASE_TARGET || phase == TC_DIAG_PHASE_SAFETY ||
           phase == TC_DIAG_PHASE_RESULT_SHAPE ||
           phase == TC_DIAG_PHASE_ARGUMENTS || phase == TC_DIAG_PHASE_RECEIVER;
}

static int record_is_valid(const TcDiagnostic *diag) {
    const TcDiagInfo *info;
    if (diag == NULL) {
        return 0;
    }
    info = tc_diag_info(diag->code);
    if (info == NULL) {
        return 0;
    }
    if ((unsigned)diag->stage >= (unsigned)TC_STAGE_COUNT) {
        return 0;
    }
    if ((info->report_stage & TC_STAGE_BIT(diag->stage)) == 0) {
        return 0;
    }
    if (diag->primary.file == NULL || diag->primary.line < 1 || diag->primary.column < 1) {
        return 0;
    }
    if (diag->has_related &&
        (diag->related.file == NULL || diag->related.line < 1 || diag->related.column < 1)) {
        return 0;
    }
    if ((diag->site == TC_DIAG_SITE_CALL || diag->site == TC_DIAG_SITE_MAKE) &&
        (diag->site_id == 0 || !site_phase_valid(diag->site_phase))) {
        return 0; /* a call/make diagnostic must identify a valid site phase */
    }
    return 1;
}

static char *dup_cstring(TcDiagBuffer *buffer, const char *text) {
    size_t n;
    char *copy;
    if (text == NULL) {
        return NULL;
    }
    n = strlen(text) + 1;
    copy = (char *)buffer->alloc->alloc(buffer->alloc->ctx, n);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, n);
    return copy;
}

TcImplError tc_diag_buffer_push(TcDiagBuffer *buffer, const TcDiagnostic *diag) {
    TcDiagnostic copy;
    char *primary_file;
    char *related_file = NULL;
    if (buffer == NULL || !record_is_valid(diag)) {
        return TC_IMPL_INTERNAL;
    }
    /* The buffer owns copies of all strings reachable from a record. */
    primary_file = dup_cstring(buffer, diag->primary.file);
    if (primary_file == NULL) {
        return TC_IMPL_OOM;
    }
    if (diag->has_related) {
        related_file = dup_cstring(buffer, diag->related.file);
        if (related_file == NULL) {
            buffer->alloc->free(buffer->alloc->ctx, primary_file);
            return TC_IMPL_OOM;
        }
    }
    if (buffer->count == buffer->cap) {
        size_t cap = buffer->cap ? buffer->cap * 2 : 8;
        TcDiagnostic *items = (TcDiagnostic *)buffer->alloc->realloc(
            buffer->alloc->ctx, buffer->items, cap * sizeof(TcDiagnostic));
        if (items == NULL) {
            buffer->alloc->free(buffer->alloc->ctx, primary_file);
            buffer->alloc->free(buffer->alloc->ctx, related_file);
            return TC_IMPL_OOM;
        }
        buffer->items = items;
        buffer->cap = cap;
    }
    copy = *diag;
    copy.primary.file = primary_file;
    copy.related.file = related_file;
    if (copy.sequence == 0) {
        copy.sequence = ++buffer->next_sequence;
    } else if (copy.sequence >= buffer->next_sequence) {
        buffer->next_sequence = copy.sequence + 1;
    }
    buffer->items[buffer->count++] = copy;
    return TC_IMPL_OK;
}

size_t tc_diag_buffer_count(const TcDiagBuffer *buffer) {
    return buffer == NULL ? 0 : buffer->count;
}

const TcDiagnostic *tc_diag_buffer_at(const TcDiagBuffer *buffer, size_t index) {
    if (buffer == NULL || index >= buffer->count) {
        return NULL;
    }
    return &buffer->items[index];
}

const TcDiagnostic *tc_diag_buffer_first(const TcDiagBuffer *buffer) {
    size_t i;
    const TcDiagnostic *best = NULL;
    if (buffer == NULL) {
        return NULL;
    }
    for (i = 0; i < buffer->count; i++) {
        if (best == NULL || tc_diag_compare(&buffer->items[i], best) < 0) {
            best = &buffer->items[i];
        }
    }
    return best;
}

static int stage_mask_valid(TcStageMask mask) {
    TcStageMask allowed = (TcStageMask)((TC_STAGE_BIT(TC_STAGE_COUNT)) - 1u);
    return mask != 0 && (mask & ~allowed) == 0;
}

const char *tc_diag_contract_violation(void) {
    size_t i;
    size_t ce = 0;
    size_t re = 0;
    size_t w_sensitive = 0;
    size_t import_codes = 0;
    for (i = 0; i < TC_DIAG_CODE_COUNT; i++) {
        const TcDiagInfo *info = &tc_diag_table[i];
        size_t j;
        int is_ce = strncmp(info->symbol, "TC_CE_", 6) == 0;
        int is_re = strncmp(info->symbol, "TC_RE_", 6) == 0;
        if (!is_ce && !is_re) {
            return "diagnostic symbol is not TC_CE_/TC_RE_";
        }
        if (is_ce) {
            ce++;
        } else {
            re++;
        }
        if (info->print_name == NULL || info->print_name[0] == '\0') {
            return "diagnostic print name is empty";
        }
        if (!stage_mask_valid(info->stage) || !stage_mask_valid(info->report_stage)) {
            return "diagnostic stage mask is invalid";
        }
        if (info->report_stage == TC_STAGE_MASK_IMPORT) {
            if (info->stage != TC_STAGE_MASK_SEM) {
                return "only SEM-substage codes may report in IMPORT";
            }
            import_codes++;
        } else if (info->report_stage != info->stage) {
            return "diagnostic report stage differs from its code-table stage";
        }
        if ((unsigned)info->group >= (unsigned)TC_DIAG_GROUP_COUNT) {
            return "diagnostic group is invalid";
        }
        if (info->trigger == NULL || info->trigger[0] == '\0' ||
            info->position_rule == NULL || info->position_rule[0] == '\0') {
            return "diagnostic trigger/position summary is empty";
        }
        if (info->w_sensitive) {
            w_sensitive++;
        }
        for (j = i + 1; j < TC_DIAG_CODE_COUNT; j++) {
            if (strcmp(info->print_name, tc_diag_table[j].print_name) == 0) {
                return "duplicate diagnostic print name";
            }
            if (strcmp(info->symbol, tc_diag_table[j].symbol) == 0) {
                return "duplicate diagnostic symbol";
            }
        }
    }
    if (ce != TC_DIAG_CE_COUNT) {
        return "TC_CE_* count is not 65";
    }
    if (re != TC_DIAG_RE_COUNT) {
        return "TC_RE_* count is not 15";
    }
    if (w_sensitive != 12) {
        return "word-size-sensitive code count is not 12";
    }
    if (import_codes != 5) {
        return "IMPORT-reporting code count is not 5";
    }
    if (tc_diag_table[0].stage != TC_STAGE_MASK_LEX_SYN) {
        return "TC_CE_SYNTAX must be the first LEX/SYN code";
    }
    if ((unsigned)TC_CE_SYNTAX != 0 ||
        (unsigned)TC_RE_INTEGER_OVERFLOW != TC_DIAG_CE_COUNT ||
        (unsigned)TC_RE_TRAP + 1u != TC_DIAG_CODE_COUNT) {
        return "code enum order does not match the standard table";
    }
    if (tc_stage_first(TC_STAGE_MASK_LEX_SYN) != TC_STAGE_LEX ||
        tc_stage_first(TC_STAGE_MASK_IMPORT) != TC_STAGE_IMPORT ||
        tc_stage_first(0) != TC_STAGE_COUNT) {
        return "stage mask helper is inconsistent";
    }
    return NULL;
}
