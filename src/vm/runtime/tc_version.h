#ifndef TC_VERSION_H
#define TC_VERSION_H

/* 实现版本；语言规范见 docs/TC语言标准设计说明书-0.0.39.md */
#define TC_VERSION_CORE "0.0.40"

/* TC-Embed 模块版本（与 docs/TC-Embed详细设计说明书-0.0.39.md 同步） */
#define TC_VERSION_EMBED "0.0.40"

/* tc-vm / tc-aot 可执行文件版本 */
#define TC_VM_VERSION TC_VERSION_CORE
#define TC_AOT_VERSION TC_VERSION_CORE

#endif /* TC_VERSION_H */
