/*
 * tc_analyze_6d.c — 类型/模式/字面量检查
 *
 * 6d 子阶段：对所有 RHS 执行类型检查（tc_check_rhs / tc_type_check_rhs），
 * 包括算术/比较/逻辑/位运算/移位/浮点/cast/bitcast 的 mode 验证、
 * 操作数类型兼容性、memblock/struct/ptr 的字段访问和存储。
 * 委托原有的 tc_pass2_type_check 执行（去除 6a/6e 已拆分部分）。
 */
#include "tc_analyze_6d.h"

/*
 * 6d 为 Pass2 的核心主体（tc_pass2_check_stmt 中除 6a/6c/6e 外的
 * 全部语句类型检查），占 pass2 70%+ 代码量。
 * 因深度依赖 TcAnalyzeCtx / TcInitHistory / visible 表等共享状态，
 * 保留在 tc_analyzer_pass2.c 中通过 tc_pass2_type_check 调用。
 * 本文件作为子阶段标识占位。
 */
