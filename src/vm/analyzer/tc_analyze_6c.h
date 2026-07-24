/*
 * tc_analyze_6c.h — goto/label 名称解析
 *
 * 在 6a 建立的标签表基础上，解析 goto 的目标标签，
 * 校验沿祖先链的块关系、跨控制流域和跳转合法性。
 */
#ifndef TC_ANALYZE_6C_H
#define TC_ANALYZE_6C_H

#include "tc_analyzer_internal.h"

/**
 * 6c: goto/label 名称解析和跳转合法性校验。
 * 当前实现在 tc_pass2_check_stmt 的 TC_STMT_GOTO / TC_STMT_LABEL_DEF
 * 分支中内联执行（tc_resolve_goto_label / tc_check_goto_jump）。
 *
 * 本文件作为子阶段标识占位，逻辑见 tc_analyzer_pass2.c。
 */

#endif /* TC_ANALYZE_6C_H */
