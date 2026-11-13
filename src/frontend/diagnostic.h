/* TC 0.0.46 contract: diagnostics, first-diagnostic ordering, OOM channel.
 *
 * Standard §12.2 ordering is applied in this order:
 *   1. stage priority (LEX -> SYN -> IMPORT -> SEM -> CT -> RT)
 *   2. module_order index
 *   3. line, then column (1-based)
 *   4. operand-internal before construct-level at one position (§12.2 #9)
 *   5. same-position specific code rank (§12.2 #4)
 *   6. clause rank (§12.2 #5, e.g. INVALID -> DIV_ZERO -> OVERFLOW ->
 *      UNDERFLOW)
 *   7. code enum index, then sequence, only as deterministic final ties.
 *
 * Positions never decide which file is the primary file: a related position
 * cannot change the primary file (standard §12.5.1).
 */
#ifndef TC_FRONTEND_DIAGNOSTIC_H
#define TC_FRONTEND_DIAGNOSTIC_H

#include <stddef.h>
#include <stdint.h>

#include "frontend/diagnostic_codes.h"
#include "runtime/alloc.h"

typedef struct {
    const char *symbol;       /* "TC_CE_..." */
    const char *print_name;   /* implementation print name, standard §12.3 */
    TcStageMask stage;        /* code-table stage; LEX/SYN is a two-bit mask */
    TcStageMask report_stage; /* actual reporting stage(s); IMPORT for the 5 */
    TcDiagGroup group;
    uint8_t w_sensitive;      /* trigger instantiated by profile W */
    uint8_t same_site_rank;   /* higher wins at an identical position */
    uint8_t clause_rank;      /* lower wins within one clause ordering */
    const char *trigger;      /* normative trigger summary (standard §12.3) */
    const char *position_rule;/* normative primary position (standard §12.5.1) */
} TcDiagInfo;

extern const TcDiagInfo tc_diag_table[TC_DIAG_CODE_COUNT];

/* Separate implementation-resource channel (standard §12.5.3).  Never part of
 * the 80-code table and never compared by tc_diag_compare. */
typedef enum {
    TC_IMPL_OK = 0,
    TC_IMPL_OOM,
    TC_IMPL_IO,
    TC_IMPL_USAGE,
    TC_IMPL_UNAVAILABLE,
    TC_IMPL_INTERNAL
} TcImplError;

#define TC_ERR_OUT_OF_MEMORY_SYMBOL "TC_ERR_OUT_OF_MEMORY"
#define TC_ERR_OUT_OF_MEMORY_PRINT_NAME "OutOfMemory"
#define TC_ERR_OUT_OF_MEMORY_MESSAGE "memory allocation failed"

const char *tc_impl_error_symbol(TcImplError err);
const char *tc_impl_error_print_name(TcImplError err);
const char *tc_impl_error_message(TcImplError err);

typedef struct {
    const char *file;
    uint32_t line;   /* 1-based */
    uint32_t column; /* 1-based */
} TcDiagPos;

typedef enum {
    TC_DIAG_ORIGIN_OPERAND = 0, /* operand-internal: name/visibility/literal/type */
    TC_DIAG_ORIGIN_CONSTRUCT   /* construct-level: condition/return/branch/args/... */
} TcDiagOrigin;

/* Call/make/site ordering (§12.2 #10) is carried explicitly.  The value is
 * opaque to the comparator except for equality and rank fields; EMITTER sets
 * rank overrides where a clause requires it. */
typedef enum {
    TC_DIAG_SITE_NONE = 0,
    TC_DIAG_SITE_CALL,
    TC_DIAG_SITE_MAKE,
    TC_DIAG_SITE_ASSIGN,
    TC_DIAG_SITE_READ
} TcDiagSite;

typedef struct {
    TcDiagCode code;
    TcStage stage; /* actual reporting stage, must be in report_stage mask */
    uint32_t module_index;
    TcDiagPos primary;
    int has_related;
    TcDiagPos related;
    TcDiagOrigin origin;
    TcDiagSite site;
    uint8_t same_site_rank; /* caller override; use table value via make helper */
    uint8_t clause_rank;
    uint64_t sequence; /* deterministic emission order; final tie only */
} TcDiagnostic;

/* Fill table-derived defaults.  Returns 0 for an unknown code. */
int tc_diag_make(TcDiagnostic *out, TcDiagCode code, TcStage stage,
                 uint32_t module_index, const char *file, uint32_t line,
                 uint32_t column);

/* Negative if a is the earlier first diagnostic, 0 if identical under all
 * ordering keys, positive otherwise. */
int tc_diag_compare(const TcDiagnostic *a, const TcDiagnostic *b);

/* Buffer/stop strategy (standard §1.8/§12.1):
 *   - LEX/SYN: report the first error immediately and stop the module;
 *   - IMPORT/SEM: push every candidate, then select with tc_diag_buffer_first;
 *   - CT: stop at the first evaluation failure and report it at the target
 *     declaration.
 * The buffer itself is stage-agnostic; callers implement the stop point. */
typedef struct TcDiagBuffer TcDiagBuffer;

/* alloc NULL selects tc_allocator_libc().  The buffer owns copies; pointers
 * returned by the accessors stay valid until the next push or destroy. */
TcDiagBuffer *tc_diag_buffer_create(const TcAllocator *alloc);
void tc_diag_buffer_destroy(TcDiagBuffer *buffer);

/* Push copies the record.  Returns TC_IMPL_OK or TC_IMPL_OOM.  A malformed
 * record (unknown code, stage not allowed for the code) is rejected with
 * TC_IMPL_INTERNAL: contract violations must not masquerade as language
 * diagnostics. */
TcImplError tc_diag_buffer_push(TcDiagBuffer *buffer, const TcDiagnostic *diag);

size_t tc_diag_buffer_count(const TcDiagBuffer *buffer);
const TcDiagnostic *tc_diag_buffer_at(const TcDiagBuffer *buffer, size_t index);
const TcDiagnostic *tc_diag_buffer_first(const TcDiagBuffer *buffer);

/* Contract self-check used at startup and by L0 tests.  Returns NULL when all
 * invariants hold, otherwise a static English description. */
const char *tc_diag_contract_violation(void);

const TcDiagInfo *tc_diag_info(TcDiagCode code);

#endif /* TC_FRONTEND_DIAGNOSTIC_H */
