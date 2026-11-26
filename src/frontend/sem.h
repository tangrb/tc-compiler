/* TC 0.0.46 static analysis: IMPORT through SEM (standard §3–§10, §13.2).
 *
 * `tc_analyze()` loads the module closure, reports IMPORT-stage candidates
 * before SEM, then performs name resolution, type interning/freeze, literal
 * typing, conversion checking, struct layout, per-unit CFG/DI, return and call
 * checks, safety contexts and annotation validation for one word size.  A
 * successful analysis means the static stages (through SEM) accepted the
 * program; CT and code generation are later stages.
 */
#ifndef TC_FRONTEND_SEM_H
#define TC_FRONTEND_SEM_H

#include <stddef.h>
#include <stdint.h>

#include "base/path.h"
#include "diagnostic/diagnostic.h"
#include "frontend/diag.h"
#include "frontend/module.h"
#include "ir/ir.h"
#include "ir/structs.h"
#include "ir/type.h"

typedef struct TcAnalysis TcAnalysis;

typedef enum {
    TC_ANALYSIS_OK = 0,
    TC_ANALYSIS_DIAGNOSTIC,
    TC_ANALYSIS_IMPL,
    TC_ANALYSIS_ROOT_NOT_PROGRAM
} TcAnalysisStatus;

typedef struct {
    TcDiagCode code;
    TcStage stage;
    TcSpan span;
    int has_related;
    TcSpan related;
} TcAnalysisDiag;

/* `addr_width` is W and must be 32 or 64.  Diagnostics are the first
 * normative diagnostic in standard §12.2 order.  The returned analysis owns
 * `out_diag`'s strings and must be destroyed by the caller. */
TcAnalysis *tc_analyze(const char *root_path, unsigned addr_width,
                       const char *const *include_roots,
                       size_t ninclude_roots, TcAnalysisStatus *out_status,
                       TcAnalysisDiag *out_diag, TcImplError *out_impl);

/* Same analysis with an explicit platform profile.  The profile copy (and its
 * word size/endianness/region table) is bound to the analysis and later to the
 * finalized TcIR; `profile->addr_width` must equal `addr_width`.  Passing NULL
 * is equivalent to tc_analyze(). */
TcAnalysis *tc_analyze_with_profile(const char *root_path,
                                    const TcPlatformProfile *profile,
                                    unsigned addr_width,
                                    const char *const *include_roots,
                                    size_t ninclude_roots,
                                    TcAnalysisStatus *out_status,
                                    TcAnalysisDiag *out_diag,
                                    TcImplError *out_impl);

TcAnalysis *tc_analyze_with_paths(const char *root_path,
                                  const TcPlatformProfile *profile,
                                  unsigned addr_width,
                                  const char *const *include_roots,
                                  size_t ninclude_roots,
                                  TcPathContext *paths,
                                  TcAnalysisStatus *out_status,
                                  TcAnalysisDiag *out_diag,
                                  TcImplError *out_impl);

TcAnalysis *tc_analyze_source(const TcModuleRootSource *source,
                              const TcPlatformProfile *profile,
                              unsigned addr_width,
                              const char *const *include_roots,
                              size_t ninclude_roots,
                              TcPathContext *paths,
                              TcAnalysisStatus *out_status,
                              TcAnalysisDiag *out_diag,
                              TcImplError *out_impl);

/* Partial static check through SEM: type table frozen, CT/IR/codegen not run.
 * Exit status OK never claims the program is valid (CT diagnostics are not
 * computed). */
TcAnalysis *tc_analyze_sem_only(const char *root_path, unsigned addr_width,
                                const char *const *include_roots,
                                size_t ninclude_roots,
                                TcAnalysisStatus *out_status,
                                TcAnalysisDiag *out_diag,
                                TcImplError *out_impl);

TcAnalysis *tc_analyze_sem_only_with_paths(
    const char *root_path, unsigned addr_width,
    const char *const *include_roots, size_t ninclude_roots,
    TcPathContext *paths, TcAnalysisStatus *out_status,
    TcAnalysisDiag *out_diag, TcImplError *out_impl);

void tc_analysis_destroy(TcAnalysis *analysis);

const TcTypeTable *tc_analysis_type_table(const TcAnalysis *analysis);
/* Finalized IR for an OK analysis (NULL for SEM-only or failed analyses). */
const TcIR *tc_analysis_ir(const TcAnalysis *analysis);
const char *tc_analysis_failure_path(const TcAnalysis *analysis);
int tc_analysis_type_table_frozen(const TcAnalysis *analysis);
unsigned tc_analysis_addr_width(const TcAnalysis *analysis);

size_t tc_analysis_module_count(const TcAnalysis *analysis);
const char *tc_analysis_module_path(const TcAnalysis *analysis, size_t index);

/* Lookup helpers for unit tests and later stages.  Struct identity is
 * (module instance path, declaration name). */
const TcStructDef *tc_analysis_find_struct(const TcAnalysis *analysis,
                                           const char *module_path,
                                           const char *struct_name);
const char *tc_analysis_member_module(const TcAnalysis *analysis,
                                      const char *module_path,
                                      const char *member_name);
const TcFuncSig *tc_analysis_find_func(const TcAnalysis *analysis,
                                       const char *module_path,
                                       const char *func_name);

#endif /* TC_FRONTEND_SEM_H */
