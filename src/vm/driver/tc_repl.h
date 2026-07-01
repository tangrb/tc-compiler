/*
 * tc_repl.h — TC-VM 交互式 REPL 接口
 *
 * 提供有状态会话：逐行输入 TC 源码，变量跨行保留。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_REPL_H
#define TC_REPL_H

#include "tc_types.h"

/**
 * @brief REPL 会话状态（符号表 + 运行时变量槽）
 */
typedef struct {
    TcSymbolTable symbols;
    TcValue *slots;
    size_t slots_capacity;
    int line_no;
} TcReplSession;

/**
 * @brief 进入交互式 REPL 主循环
 * @param diag 诊断对象
 * @return 正常退出返回 0
 */
int tc_repl_run(TcDiagnostic *diag);

#endif
