/*
 * tc_repl.h — TC-VM 交互式 REPL 接口
 *
 * 提供有状态会话：逐行输入 TC 源码，变量跨行保留。
 * 内置命令以 ':' 开头：:quit/:reset/:vars/:help。
 */
#ifndef TC_REPL_H
#define TC_REPL_H

#include "tc_analyzer.h"
#include "tc_types.h"

/**
 * REPL 会话状态。
 * 包含符号表、轻量初始化历史追踪、运行时变量槽和当前行号。
 */
typedef struct {
    TcSymbolTable symbols;
    TcReplAnalyzeCtx analyze_ctx;
    TcValue *slots;
    size_t slots_capacity;
    int line_no;
} TcReplSession;

/**
 * 进入交互式 REPL 主循环。
 * @param diag 诊断对象
 * @return 正常退出返回 0
 */
int tc_repl_run(TcDiagnostic *diag);

#endif
