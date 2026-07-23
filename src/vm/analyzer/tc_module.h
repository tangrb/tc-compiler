/*
 * tc_module.h — 模块加载、导入解析、DAG 与函数签名收集（编译阶段 4）
 */
#ifndef TC_MODULE_H
#define TC_MODULE_H

#include "tc_types.h"
#include "tc_diagnostic.h"

typedef struct {
    char *name;
    TcVisibility visibility;
    TcType return_type;
    char *return_struct_name;
    TcFuncParam *params;
    size_t param_count;
    int module_index; /* 在 deps/entry 中的模块下标；入口为 -1 约定见实现 */
    int func_id;
    int def_line;
} TcFuncSignature;

typedef struct {
    TcFuncSignature *items;
    size_t count;
    size_t capacity;
} TcFuncSignatureList;

typedef struct {
    char **paths;
    size_t count;
} TcModuleSearchPaths;

void tc_module_search_paths_init(TcModuleSearchPaths *paths);
void tc_module_search_paths_free(TcModuleSearchPaths *paths);
int tc_module_search_paths_set(TcModuleSearchPaths *paths, char *const *items, size_t count,
                               TcDiagnostic *diag);

void tc_func_signature_list_init(TcFuncSignatureList *list);
void tc_func_signature_list_free(TcFuncSignatureList *list);

/**
 * 阶段 4a：单文件五层结构 / 可见性 / #program 误用复核。
 * Parser 已做受限恢复时此处多为二次确认。
 */
int tc_module_check_structure(const TcProgram *program, TcDiagnostic *diag);

/**
 * 加载入口程序的全部可达 #lib 依赖到 out->deps，并做 4b/4c/4d。
 * @param entry_path 入口文件路径（用于搜索相对目录）；可为 NULL（仅内存源）
 * @param search     额外 -I 路径；可为 NULL
 */
int tc_module_resolve_imports(TcTypedProgram *out, const char *entry_path,
                              const TcModuleSearchPaths *search, TcDiagnostic *diag);

/** 阶段 4d：从入口 + deps 收集函数签名 */
int tc_module_collect_signatures(const TcTypedProgram *prog, TcFuncSignatureList *out,
                                 TcDiagnostic *diag);

#endif /* TC_MODULE_H */
