/*
 * tc_aot_codegen.h — TC → C99 代码生成接口
 *
 * embed_mode=0：独立程序 — 生成完整 main()，符号均为 static。
 * embed_mode=1：嵌入库 — 非 static 符号 + 函数表 + tc_aot_init/cleanup，无 main()。
 */
#ifndef TC_AOT_CODEGEN_H
#define TC_AOT_CODEGEN_H

#include <stdio.h>

#include "tc_types.h"

/** 将类型化程序转译为 C99 源码并写入 out 文件流 */
int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name,
                  int embed_mode);

/** 嵌入模式：生成头文件（声明 slots、tc_aot_init/cleanup、函数表） */
int tc_aot_emit_embed_header(FILE *out, const TcTypedProgram *program,
                              const char *source_name);

#endif
