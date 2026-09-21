#ifndef TC_VERSION_H
#define TC_VERSION_H

/* 实现版本；语言规范 0.0.44 */
#define TC_VERSION_CORE "0.0.44"

/* TC-Embed 模块版本（与核心版本一致） */
#define TC_VERSION_EMBED "0.0.44"

/* tc-vm / tc-aot 可执行文件版本 */
#define TC_VM_VERSION TC_VERSION_CORE
#define TC_AOT_VERSION TC_VERSION_CORE

#endif /* TC_VERSION_H */
