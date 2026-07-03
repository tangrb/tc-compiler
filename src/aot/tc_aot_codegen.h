#ifndef TC_AOT_CODEGEN_H
#define TC_AOT_CODEGEN_H

#include <stdio.h>

#include "tc_types.h"

int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name);

#endif
