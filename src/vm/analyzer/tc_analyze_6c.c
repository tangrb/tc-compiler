/*
 * tc_analyze_6c.c — goto/label 名称解析
 *
 * 6c 子阶段：在 6a 建立的标签表基础上，解析 goto 的目标标签，
 * 校验沿祖先链的块关系、跨控制流域和跳转合法性。
 * 逻辑包含在 tc_pass2_check_stmt 的 TC_STMT_GOTO / TC_STMT_LABEL_DEF
 * 分支中（tc_resolve_goto_label / tc_check_goto_jump）。
 */
#include "tc_analyze_6c.h"

/*
 * 6c 的 goto 解析在 tc_pass2_type_check 主遍历中内联执行
 * （TC_STMT_GOTO 分支调用 tc_resolve_goto_label + tc_check_goto_jump）。
 * 本文件作为子阶段标识占位，具体逻辑见 tc_analyzer_pass2.c。
 */
