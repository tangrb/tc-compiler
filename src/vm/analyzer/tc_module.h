/*
 * tc_module.h — 模块加载、导入解析、DAG 与函数签名收集（编译阶段 4）
 *
 * 职责概览（编译器标准阶段 4a–4d）：
 *   4a  tc_module_check_structure   — 五层顺序、可见性、#program/#lib 误用
 *   4b  递归加载 import 目标 .tc     — 仅允许 #lib
 *   4c  依赖图成环检测               — 三色 DFS
 *   4d  tc_module_collect_signatures — 从入口 + deps 汇总函数签名
 *
 * 调用时机：Parser 产出 TcProgram 后，Analyzer（结构复核）与
 * tc_compile_file_opts（完整导入解析）共同使用本模块。
 */
#ifndef TC_MODULE_H
#define TC_MODULE_H

#include "tc_types.h"
#include "tc_diagnostic.h"

/**
 * 跨模块可见的函数签名摘要（阶段 4d）。
 * 供阶段 5 签名检查与 Pass2 funcall 解析消费；本阶段只收集签名，不解析调用。
 */
typedef struct {
    char *name;
    TcVisibility visibility;
    TcType return_type;
    char *return_struct_name; /* 返回类型为未解析 struct 名时非空 */
    TcFuncParam *params;
    size_t param_count;
    /*
     * 所属模块在 TcTypedProgram.deps[] 中的下标；
     * 入口模块自身为 #lib 时约定为 -1（见 tc_module_collect_signatures）。
     */
    int module_index;
    int func_id;   /* 与 AST TcFuncDef.func_id 对齐；未分配时为解析前值 */
    int def_line;
} TcFuncSignature;

typedef struct {
    TcFuncSignature *items;
    size_t count;
    size_t capacity;
} TcFuncSignatureList;

/** -I 等价的额外模块搜索目录（路径字符串由本结构拥有） */
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
 * 阶段 4a：复核单文件五层结构 / 可见性 / #program 误用。
 *
 * 五层（不可回退）：import → struct → 值声明 → func/可执行。
 * #program 将「值声明」与「可执行语句」视为同层，允许交错；
 * 禁止 func / static / 可见性修饰。
 * #lib 要求成员带 public/private，禁止非 static 的 var/let。
 *
 * Parser 已做受限恢复时，此处多为二次确认（fail-fast 单槽诊断）。
 */
int tc_module_check_structure(const TcProgram *program, TcDiagnostic *diag);

/**
 * 加载入口程序的全部可达 #lib 依赖到 out->deps，并完成 4b/4c。
 *
 * 搜索顺序：入口文件所在目录 → search（-I）路径；
 * 命中 0 个 → IMPORT_NOT_FOUND；命中多个不同路径 → IMPORT_AMBIGUOUS。
 * 已加载模块按名去重；同文件重复 import / 自引用 / 环均报错。
 *
 * @param entry_path 入口文件路径（用于相对搜索）；可为 NULL（仅内存源则通常无 import）
 * @param search     额外 -I 路径；可为 NULL
 */
int tc_module_resolve_imports(TcTypedProgram *out, const char *entry_path,
                              const TcModuleSearchPaths *search, TcDiagnostic *diag);

/**
 * 阶段 4d：从入口 + deps 收集函数签名，并分配稳定 func_id 写回 AST。
 * 仅 #lib 贡献签名（#program 不允许 func）。
 */
int tc_module_collect_signatures(TcTypedProgram *prog, TcFuncSignatureList *out,
                                 TcDiagnostic *diag);

/**
 * 计算 deps 的真拓扑注册序（importee 先于 importer），写入 out_order（长度 dep_count）。
 *
 * deps 收集为 DFS 前序（importer 先入后递归），简单逆序在菱形依赖
 * （entry→A→C 且 entry→B→C）下非法：逆序得 [B, C, A]，B 先于 C 注册结构体
 * 会使 B 引用 C.<struct> 误报 TC_CE_UNDEFINED_STRUCT。本函数给出合法拓扑序，
 * 供结构体表注册等「importee 必须先注册」的消费点使用。
 *
 * @return 0 成功；-1 内存失败或检测到环（DAG 检查应已先行拦截）。
 */
int tc_module_topological_dep_order(const TcTypedProgram *out, size_t *out_order,
                                    TcDiagnostic *diag);

#endif /* TC_MODULE_H */
