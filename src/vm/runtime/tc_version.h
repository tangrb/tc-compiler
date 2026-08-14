#ifndef TC_VERSION_H
#define TC_VERSION_H

/* 版本号唯一事实源；语言权威仍为 docs/ 下 *-0.0.37.md，本版增量见
 * docs/TC-0.0.38-变更说明.md */
#define TC_VERSION_CORE "0.0.38"

/* TC-Embed 模块版本（与实现同步；Embed 详设基线仍为 0.0.37） */
#define TC_VERSION_EMBED "0.0.38"

/* tc-vm / tc-aot 可执行文件版本 */
#define TC_VM_VERSION TC_VERSION_CORE
#define TC_AOT_VERSION TC_VERSION_CORE

#endif /* TC_VERSION_H */
