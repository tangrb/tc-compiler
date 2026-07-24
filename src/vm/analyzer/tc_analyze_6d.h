/*
 * tc_analyze_6d.h — 类型/模式/字面量检查
 *
 * 对所有 RHS 执行类型检查（算术/比较/逻辑/位运算/移位/浮点/cast/bitcast
 * 的 mode 验证、操作数类型兼容性、memblock/struct/ptr 的字段访问）。
 */
#ifndef TC_ANALYZE_6D_H
#define TC_ANALYZE_6D_H

#include "tc_analyzer_internal.h"

/**
 * 6d: 类型/模式/字面量语义检查，为 Pass2 的核心主体。
 * 当前实现在 tc_pass2_check_stmt 中除 6a/6c/6e 外的全部语句类型检查分支。
 *
 * 本文件作为子阶段标识占位，逻辑见 tc_analyzer_pass2.c。
 */

#endif /* TC_ANALYZE_6D_H */
