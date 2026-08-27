/*
 * tc_analyzer.c — 静态分析编排（tc_analyze_ex）
 *
 * 诊断优先级按编译器标准 13 阶段模型；实现入口是本文件的一条 fail-fast 链：
 *   4a 结构 → 4b/4c import（有路径时）→ struct 表 + 类型池 → 4d 签名 →
 *   5 签名检查 → Pass1 → 成员索引 → static let/var → Pass2（含 6a–8）→
 *   多域 CFG + definite init（含依赖库）→ 12 调用图
 */
#include "tc_analyzer.h"
#include "tc_analyzer_internal.h"
#include "tc_analyzer_pass1.h"
#include "tc_analyzer_pass2.h"
#include "tc_callgraph.h"
#include "tc_cfg.h"
#include "tc_func_check.h"
#include "tc_module.h"
#include "tc_scope.h"
#include "tc_struct_check.h"
#include "tc_type_check.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  TcTypedProgram 生命周期管理                                          */
/* ------------------------------------------------------------------ */

void tc_typed_program_init(TcTypedProgram *program) {
    tc_program_init(&program->program);
    program->deps = NULL;
    program->dep_count = 0;
    program->dep_capacity = 0;
    tc_symbol_table_init(&program->symbols);
    program->cfg = NULL;
    program->cfg_set = NULL;
    tc_warning_list_init(&program->warnings);
    program->toplevel_slot_count = 0;
    program->static_slot_count = 0;
    program->struct_table = NULL;
    program->type_table = NULL;
}

void tc_typed_program_free(TcTypedProgram *program) {
    size_t i = 0;
    if (program->cfg_set) {
        tc_cfg_set_free(program->cfg_set);
        free(program->cfg_set);
        program->cfg_set = NULL;
        program->cfg = NULL;
    } else if (program->cfg) {
        tc_cfg_free(program->cfg);
        free(program->cfg);
        program->cfg = NULL;
    }
    tc_program_free(&program->program);
    for (i = 0; i < program->dep_count; i++) {
        tc_program_free(&program->deps[i]);
    }
    free(program->deps);
    program->deps = NULL;
    program->dep_count = 0;
    program->dep_capacity = 0;
    tc_symbol_table_free(&program->symbols);
    tc_warning_list_free(&program->warnings);
    if (program->struct_table) {
        tc_struct_table_free(program->struct_table);
        free(program->struct_table);
        program->struct_table = NULL;
    }
    if (program->type_table) {
        tc_type_table_free(program->type_table);
        free(program->type_table);
        program->type_table = NULL;
    }
}


/* ------------------------------------------------------------------ */
/*  字面量检查辅助                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 检查 TcLiteral 能否放入目标类型
 * @return 检查通过返回 0；失败返回 -1 并设置 diag
 */
int tc_check_literal(const TcLiteral *lit, TcTypeTag expected, int line,
                            TcDiagnostic *diag, TcErrorKind literal_type_err) {
    TcErrorKind err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
    if (!tc_literal_fits_context(lit, expected, &err_kind)) {
        if (err_kind == TC_CE_LITERAL_TYPE) {
            tc_diagnostic_set(diag, literal_type_err, line, TC_COLUMN_UNKNOWN,
                              "literal type does not match context");
        } else {
            tc_diagnostic_set(diag, TC_CE_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                              "literal out of range for context type");
        }
        return -1;
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/*  tc_analyze_ex — 文件模式分析入口                                      */
/* ------------------------------------------------------------------ */

int tc_analyze_ex(TcProgram *program, TcTypedProgram *out, const char *entry_path,
                  const TcModuleSearchPaths *search, TcDiagnostic *diag) {
    TcStructTable struct_table;
    TcFuncSignatureList sigs;
    TcMemberIndex members;
    TcFuncCheckEnv func_env;
    char *saved_file = NULL;
    char *saved_source = NULL;
    int ret = -1;

    tc_typed_program_init(out);
    tc_struct_table_init(&struct_table);
    tc_func_signature_list_init(&sigs);
    tc_member_index_init(&members);

    out->program = *program;
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
    program->mode = TC_MODULE_UNSET;
    program->module_name = NULL;
    program->source_path = NULL;

    if (out->program.mode == TC_MODULE_UNSET) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, 1, TC_COLUMN_UNKNOWN,
                          "expected #program or #lib");
        goto fail;
    }

    if (tc_module_check_structure(&out->program, diag) != 0) {
        goto fail;
    }

    /* 4b/4c：有路径时解析 import（须在签名收集之前） */
    if (entry_path) {
        if (tc_module_resolve_imports(out, entry_path, search, diag) != 0) {
            goto fail;
        }
    }

    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_struct_table_register_program(&out->deps[di], &struct_table, diag) != 0) {
                goto fail;
            }
        }
    }
    if (tc_struct_table_register_program(&out->program, &struct_table, diag) != 0) {
        goto fail;
    }

    /* 类型池：Pass1 起 intern；生命周期归属 TcTypedProgram */
    {
        TcTypeTable *types = (TcTypeTable *)malloc(sizeof(TcTypeTable));
        if (!types) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            goto fail;
        }
        tc_type_table_init(types);
        out->type_table = types;
    }

    /* 4d + 5 */
    if (tc_module_collect_signatures(out, &sigs, diag) != 0) {
        goto fail;
    }
    if (tc_func_check_signatures(out, &sigs, diag) != 0) {
        goto fail;
    }

    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_pass1_collect_symbols(&out->deps[di], &out->symbols, out->type_table,
                                         diag) != 0) {
                goto fail;
            }
        }
    }

    if (tc_pass1_collect_symbols(&out->program, &out->symbols, out->type_table, diag) != 0) {
        goto fail;
    }

    if (tc_member_index_build(&out->program, &members, diag) != 0) {
        goto fail;
    }

    /* H-5 / H-6：入口与依赖库的 static let/var 均需求值/检查，
     * 否则跨模块 Self.static_let 在运行期无 const_value。 */
    if (tc_func_eval_static_lets(&out->program, &out->symbols, &struct_table, diag) != 0) {
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_func_eval_static_lets(&out->deps[di], &out->symbols, &struct_table, diag) != 0) {
                goto fail;
            }
        }
    }
    if (tc_func_check_static_vars(&out->program, &members, diag) != 0) {
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_func_check_static_vars(&out->deps[di], &members, diag) != 0) {
                goto fail;
            }
        }
    }

    memset(&func_env, 0, sizeof(func_env));
    func_env.prog = out;
    func_env.sigs = &sigs;
    func_env.members = &members;
    func_env.current_func = NULL;
    func_env.struct_table = &struct_table;

    /*
     * 阶段 6 — 语义分析
     *
     * 6b（名称作用域预建 + 成员索引 + static let 求值）已在上面完成：
     *    tc_pass1_collect_symbols → tc_member_index_build → tc_func_eval_static_lets
     *
     * 6a（控制流上下文：goto/label 词法祖先校验）在 tc_pass2_type_check 内部
     *    通过 tc_analyze_6a_collect_labels 执行（tc_analyze_6a.c）。
     *
     * 6c（goto/label 名称解析）和 6d（类型/模式/字面量检查）为
     *    tc_pass2_check_stmt 主体；6e（I/O 格式检查）委托 tc_analyze_6e.c。
     *
     * 子阶段间 fail-fast：任一步骤返回 -1 则立即中止。
     */

    /* ==== 阶段 6 入口：类型与语义分析（6a→6b→6c→6d→6e） ==== */
    if (tc_pass2_type_check(&out->program, &out->symbols, &struct_table, &func_env, &out->warnings,
                            diag) != 0) {
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_pass2_type_check(&out->deps[di], &out->symbols, &struct_table, &func_env,
                                    &out->warnings, diag) != 0) {
                goto fail;
            }
        }
    }

    out->cfg_set = (TcCfgSet *)malloc(sizeof(TcCfgSet));
    if (!out->cfg_set) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        goto fail;
    }
    tc_cfg_set_init(out->cfg_set);
    if (tc_cfg_build_all(&out->program, &out->symbols, out->cfg_set, diag) != 0 ||
        tc_analyze_definite_init_all(out->cfg_set, &out->program,
                                     tc_symbol_table_runtime_slot_count(&out->symbols),
                                     diag) != 0) {
        goto fail;
    }
    out->cfg = &out->cfg_set->toplevel;

    /* 阶段 11：依赖库函数体同样执行 CFG 数据流检查（确定初始化 / 不可达 /
     * MISSING_RETURN）。此前仅对入口模块构建 CFG，导入库的函数体缺陷
     * （如 goto 跳过初始化、缺少 return）会静默通过。依赖库诊断定位到
     * 其自身文件（fail-fast：出错即返回，无需恢复入口 source）。 */
    {
        const char *entry_file = NULL;
        const char *entry_source = NULL;
        size_t di = 0;

        tc_diagnostic_get_source(diag, &entry_file, &entry_source);
        /* 入口 path/source 会在首次 set_source 时被 diag 释放，恢复前必须
         * 持有独立副本（此前直接保存内部指针，恢复时 heap-use-after-free） */
        if (entry_file) {
            saved_file = strdup(entry_file);
            if (!saved_file) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
        }
        if (entry_source) {
            saved_source = strdup(entry_source);
            if (!saved_source) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
        }
        for (di = 0; di < out->dep_count; di++) {
            TcCfgSet dep_set;
            int dep_rc = 0;

            if (tc_diagnostic_set_source(diag, out->deps[di].source_path, NULL) != 0) {
                goto fail;
            }
            tc_cfg_set_init(&dep_set);
            if (tc_cfg_build_all(&out->deps[di], &out->symbols, &dep_set, diag) != 0) {
                dep_rc = -1;
            } else if (tc_analyze_definite_init_all(
                           &dep_set, &out->deps[di],
                           tc_symbol_table_runtime_slot_count(&out->symbols), diag) != 0) {
                dep_rc = -1;
            }
            tc_cfg_set_free(&dep_set);
            if (dep_rc != 0) {
                goto fail;
            }
        }
        /* 恢复入口 source，供调用图等后续诊断使用 */
        if (tc_diagnostic_set_source(diag, saved_file, saved_source) != 0) {
            goto fail;
        }
    }

    /* 阶段 12：调用图 */
    if (tc_callgraph_check(&func_env, diag) != 0) {
        goto fail;
    }

    {
        TcStructTable *owned = (TcStructTable *)malloc(sizeof(TcStructTable));
        if (!owned) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            goto fail;
        }
        *owned = struct_table;
        out->struct_table = owned;
    }

    ret = 0;

fail:
    if (ret != 0) {
        tc_struct_table_free(&struct_table);
        tc_typed_program_free(out);
    }
    free(saved_file);
    free(saved_source);
    tc_func_signature_list_free(&sigs);
    tc_member_index_free(&members);
    return ret;
}

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    return tc_analyze_ex(program, out, NULL, NULL, diag);
}
