/*
 * tc_lib.h — libtc 嵌入 API
 *
 * 允许外部程序嵌入 TC 的编译（解析 + 静态分析）与执行能力。
 * 单次编译可以独立于调用方存在——编译时复制或转交所有权，执行后释放资源。
 *
 * 典型使用：
 *   TcTypedProgram prog;
 *   TcCompileOptions opts = {0};
 *   if (tc_compile_file_opts("input.tc", &opts, &prog, &diag) == 0) {
 *       tc_run_program(&prog, &diag);
 *       tc_typed_program_free(&prog);
 *   }
 *
 * 内存所有权约定见 docs/libtc-api-0.0.41.md。
 */
#ifndef TC_LIB_H
#define TC_LIB_H

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_types.h"

/**
 * 编译会话选项：本次编译的 -I 等价的模块搜索路径（会话内生效）。
 * 无进程级全局状态；同一进程内多个编译单元可各自携带搜索路径（嵌入场景）。
 * search_paths 数组仅调用期间须有效（内部借用，不复制）。
 * opts 为 NULL 或 search_paths 为空 = 本次编译无额外搜索路径。
 */
typedef struct {
    const char *const *search_paths;
    size_t search_path_count;
} TcCompileOptions;

/**
 * 将源字符串编译为已类型化程序（Parse + Analyze）。
 * @param source 源字符串（仅调用期间须有效；返回后可立即释放）
 * @param name   源码名称（用于诊断显示；不可为 NULL）
 * @param out    输出参数，成功时写入 TcTypedProgram（调用方须 tc_typed_program_free）
 * @param diag   诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag
 * @note 仅成功时写入 out 并转移所有权。任一失败阶段都不修改 out，
 *       调用方无需也不得释放本次调用的输出。
 * @note 无路径的内存源仅做结构检查、不解析 import，故无搜索路径参数；
 *       文件编译（含 import 解析）见 tc_compile_file_opts。
 */
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out, TcDiagnostic *diag);

/**
 * 从文件路径读取源码并编译；自动解析 import 的 #lib，搜索路径来自
 * 会话选项 opts（NULL = 无额外路径）。多编译单元在同一进程内使用不同
 * -I 时各自传 opts，无进程级全局污染。
 * @param path 源文件路径
 * @param opts 会话级搜索路径（可为 NULL）
 * @param out  输出参数
 * @param diag 诊断对象
 * @return 成功返回 0；I/O 或编译失败返回 -1
 */
int tc_compile_file_opts(const char *path, const TcCompileOptions *opts,
                         TcTypedProgram *out, TcDiagnostic *diag);

/**
 * 执行已类型化程序（含 static var 拓扑初始化）。
 * @param program 已通过静态分析的程序
 * @param diag    诊断对象
 * @return 成功返回 0；运行时失败返回 -1
 */
int tc_run_program(const TcTypedProgram *program, TcDiagnostic *diag);

#endif
