/*
 * tc_callgraph.h — 函数调用图与递归环检查（Phase 4 / 阶段 12）
 *
 * 在 Pass2 + CFG 之后运行：基于已解析的签名表构建 func_id 有向图，
 * Tarjan 求强连通分量；若存在递归（自环或 size>1 的 SCC），
 * 按「定义行最小的递归 SCC → 该 SCC 内调用边行号最小」确定性报 TC_CE_RECURSION。
 *
 * 顶层独立 funcall 的 caller 记为 TC_CALLGRAPH_NO_CALLER（不参与邻接表）。
 */
#ifndef TC_CALLGRAPH_H
#define TC_CALLGRAPH_H

#include "tc_func_check.h"
#include "tc_diagnostic.h"

/**
 * 检查入口程序可达调用是否成环。
 * @param env 须已填 prog / sigs / members（与 Pass2 同一环境）
 * @return 0 无递归；-1 有递归或 OOM（diag 已设）
 */
int tc_callgraph_check(const TcFuncCheckEnv *env, TcDiagnostic *diag);

#endif /* TC_CALLGRAPH_H */
