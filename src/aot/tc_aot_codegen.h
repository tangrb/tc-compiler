/*
 * tc_aot_codegen.h — TC → C99 代码生成接口
 */
#ifndef TC_AOT_CODEGEN_H
#define TC_AOT_CODEGEN_H

#include <stdio.h>

#include "tc_types.h"

/** 将类型化程序转译为 C99 源码并写入 out 文件流 */
int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name);

#endif
