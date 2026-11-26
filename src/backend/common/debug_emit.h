#ifndef TC_BACKEND_COMMON_DEBUG_EMIT_H
#define TC_BACKEND_COMMON_DEBUG_EMIT_H

#include <stdio.h>

#include "diagnostic/diagnostic.h"
#include "ir/linemap.h"

TcImplError tc_debug_emit_files(FILE *out, const TcLineMap *linemap);
TcImplError tc_debug_emit_location(FILE *out, const TcLineMap *linemap,
                                   const TcInstr *instr);
TcImplError tc_debug_emit_begin(FILE *out);
TcImplError tc_debug_emit_end(FILE *out, const TcLineMap *linemap,
                              unsigned pointer_size);

#endif
