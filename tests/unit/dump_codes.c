/* Contract test helper: dump the C diagnostic table as TSV.
 *
 * Used by tests/traceability/check_codes.py to prove the C table and the
 * machine-readable codes.json are the same closed 80-code set.  This is a test
 * executable only; it is not a product tool. */
#include <stdio.h>

#include "frontend/contract.h"
#include "frontend/diagnostic.h"

static const char *stage_name(TcStageMask mask) {
    if (mask == TC_STAGE_MASK_LEX_SYN) {
        return "LEX/SYN";
    }
    if (mask == TC_STAGE_MASK_LEX) {
        return "LEX";
    }
    if (mask == TC_STAGE_MASK_SYN) {
        return "SYN";
    }
    if (mask == TC_STAGE_MASK_IMPORT) {
        return "IMPORT";
    }
    if (mask == TC_STAGE_MASK_SEM) {
        return "SEM";
    }
    if (mask == TC_STAGE_MASK_CT) {
        return "CT";
    }
    if (mask == TC_STAGE_MASK_RT) {
        return "RT";
    }
    return "?";
}

static const char *group_name(TcDiagGroup group) {
    switch (group) {
    case TC_DIAG_GROUP_LEX_SYN:
        return "lex_syn";
    case TC_DIAG_GROUP_NAME_MODULE:
        return "name_module";
    case TC_DIAG_GROUP_TYPE_DECL_CONTROL:
        return "type_decl_control";
    case TC_DIAG_GROUP_SAFETY_CONST:
        return "safety_const";
    case TC_DIAG_GROUP_RUNTIME:
        return "runtime";
    default:
        return "?";
    }
}

int main(void) {
    size_t i;
    const char *violation = tc_contract_check();
    if (violation != NULL) {
        fprintf(stderr, "contract violation: %s\n", violation);
        return 1;
    }
    printf("code\tprint_name\tstage\treport_stage\tw_sensitive\tgroup\n");
    for (i = 0; i < TC_DIAG_CODE_COUNT; i++) {
        const TcDiagInfo *info = &tc_diag_table[i];
        printf("%s\t%s\t%s\t%s\t%d\t%s\n", info->symbol, info->print_name,
               stage_name(info->stage), stage_name(info->report_stage),
               info->w_sensitive ? 1 : 0, group_name(info->group));
    }
    return 0;
}
