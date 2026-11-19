/* TC 0.0.46 contract: closed 80-code language diagnostic set (standard §12.3).
 *
 * The enum order is the normative table order and is generated from
 * tests/traceability/codes.json by tests/traceability/gen_c_table.py:
 * indices 0..64 are the 65 TC_CE_* codes, 65..79 the 15 TC_RE_* codes.
 *
 * TC_ERR_OUT_OF_MEMORY is NOT in this enum.  It is an implementation resource
 * error (standard §12.5.3) with its own channel and never participates in
 * language first-diagnostic ordering.
 */
#ifndef TC_DIAGNOSTIC_CODES_H
#define TC_DIAGNOSTIC_CODES_H

#include <stdint.h>

#include "base/assert.h"

#define TC_DIAG_CE_COUNT 65u
#define TC_DIAG_RE_COUNT 15u
#define TC_DIAG_CODE_COUNT 80u

typedef enum {
    TC_STAGE_LEX = 0,
    TC_STAGE_SYN,
    TC_STAGE_IMPORT,
    TC_STAGE_SEM,
    TC_STAGE_CT,
    TC_STAGE_RT,
    TC_STAGE_COUNT
} TcStage;

typedef uint32_t TcStageMask;

#define TC_STAGE_BIT(stage) ((TcStageMask)1u << (unsigned)(stage))
#define TC_STAGE_MASK_LEX TC_STAGE_BIT(TC_STAGE_LEX)
#define TC_STAGE_MASK_SYN TC_STAGE_BIT(TC_STAGE_SYN)
#define TC_STAGE_MASK_LEX_SYN (TC_STAGE_MASK_LEX | TC_STAGE_MASK_SYN)
#define TC_STAGE_MASK_IMPORT TC_STAGE_BIT(TC_STAGE_IMPORT)
#define TC_STAGE_MASK_SEM TC_STAGE_BIT(TC_STAGE_SEM)
#define TC_STAGE_MASK_CT TC_STAGE_BIT(TC_STAGE_CT)
#define TC_STAGE_MASK_RT TC_STAGE_BIT(TC_STAGE_RT)

typedef enum {
    TC_DIAG_GROUP_LEX_SYN = 0,
    TC_DIAG_GROUP_NAME_MODULE,
    TC_DIAG_GROUP_TYPE_DECL_CONTROL,
    TC_DIAG_GROUP_SAFETY_CONST,
    TC_DIAG_GROUP_RUNTIME,
    TC_DIAG_GROUP_COUNT
} TcDiagGroup;

typedef enum {
#define TC_DIAG_ENTRY(name, stage, report_stage, group, w, rank, clause, print_name, trigger, \
                           position)                                                                \
    name,
#include "diagnostic/codes.inc"
#undef TC_DIAG_ENTRY
    TC_DIAG_CODE_COUNT_ENUM
} TcDiagCode;

TC_STATIC_ASSERT(TC_DIAG_CODE_COUNT_ENUM == (int)TC_DIAG_CODE_COUNT, __LINE__);
TC_STATIC_ASSERT(TC_DIAG_CE_COUNT + TC_DIAG_RE_COUNT == TC_DIAG_CODE_COUNT, __LINE__);

/* First (lowest-numbered) stage present in a stage mask; TC_STAGE_COUNT when
 * the mask is empty. */
TcStage tc_stage_first(TcStageMask mask);

#endif /* TC_DIAGNOSTIC_CODES_H */
