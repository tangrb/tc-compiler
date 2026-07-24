/*
 * tc_analyzer.c — TC 静态分析器实现
 *
 * 流水线（Phase 4）：
 *   4a 结构 → 4b/4c import（可选）→ struct 表 → 4d 签名 → 5 签名检查 →
 *   Pass1 → 成员索引 → H-5/H-6 static → Pass2（含 F2/F3/F4）→
 *   多域 CFG + definite init → 12 调用图
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
}


/* ------------------------------------------------------------------ */
/*  字面量检查辅助                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 检查 TcLiteral 能否放入目标类型
 * @return 检查通过返回 0；失败返回 -1 并设置 diag
 */
int tc_check_literal(const TcLiteral *lit, TcTypeKind expected, int line,
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
/*  tc_analyze_ex — Phase 4 管线入口                                     */
/* ------------------------------------------------------------------ */

int tc_analyze_ex(TcProgram *program, TcTypedProgram *out, const char *entry_path,
                  const TcModuleSearchPaths *search, TcDiagnostic *diag) {
    TcStructTable struct_table;
    TcFuncSignatureList sigs;
    TcMemberIndex members;
    TcFuncCheckEnv func_env;

    tc_typed_program_init(out);
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
        tc_typed_program_free(out);
        return -1;
    }

    if (tc_module_check_structure(&out->program, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }

    /* 4b/4c：有路径时解析 import（须在签名收集之前） */
    if (entry_path) {
        if (tc_module_resolve_imports(out, entry_path, search, diag) != 0) {
            tc_typed_program_free(out);
            return -1;
        }
    }

    tc_struct_table_init(&struct_table);
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_struct_table_register_program(&out->deps[di], &struct_table, diag) != 0) {
                tc_struct_table_free(&struct_table);
                tc_typed_program_free(out);
                return -1;
            }
        }
    }
    if (tc_struct_table_register_program(&out->program, &struct_table, diag) != 0) {
        tc_struct_table_free(&struct_table);
        tc_typed_program_free(out);
        return -1;
    }
    tc_sizeof_bits_set_struct_width_fn(tc_struct_table_width_bits, &struct_table);

    /* 4d + 5 */
    if (tc_module_collect_signatures(out, &sigs, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_typed_program_free(out);
        return -1;
    }
    if (tc_func_check_signatures(out, &sigs, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_typed_program_free(out);
        return -1;
    }

    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_pass1_collect_symbols(&out->deps[di], &out->symbols, diag) != 0) {
                tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
                tc_struct_table_free(&struct_table);
                tc_func_signature_list_free(&sigs);
                tc_typed_program_free(out);
                return -1;
            }
        }
    }

    if (tc_pass1_collect_symbols(&out->program, &out->symbols, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_typed_program_free(out);
        return -1;
    }

    if (tc_member_index_build(&out->program, &members, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }

    /* H-5 / H-6：入口与依赖库的 static let/var 均需求值/检查，
     * 否则跨模块 Self.static_let 在运行期无 const_value。 */
    if (tc_func_eval_static_lets(&out->program, &out->symbols, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_func_eval_static_lets(&out->deps[di], &out->symbols, diag) != 0) {
                tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
                tc_struct_table_free(&struct_table);
                tc_func_signature_list_free(&sigs);
                tc_member_index_free(&members);
                tc_typed_program_free(out);
                return -1;
            }
        }
    }
    if (tc_func_check_static_vars(&out->program, &members, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_func_check_static_vars(&out->deps[di], &members, diag) != 0) {
                tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
                tc_struct_table_free(&struct_table);
                tc_func_signature_list_free(&sigs);
                tc_member_index_free(&members);
                tc_typed_program_free(out);
                return -1;
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
     * 阶段 6 (a-e)：类型与语义分析
     *
     * 6a  控制流上下文检查 (goto/label 词法祖先校验，建立标签表)
     * 6b  名称作用域预建 (符号 slot 分配 + 成员索引 + 全局名称冲突)
     * 6c  goto/label 名称解析 (标签沿祖先链查找，块关系校验)
     * 6d  类型/模式/字面量检查 (RHS 类型、memblock/struct/ptr 字段访问)
     * 6e  I/O 格式检查 (write/read 格式符兼容性)
     *
     * 当前实现中 6a-6e 由 tc_pass2_type_check() 统一驱动（6a 标签收集
     * 已提取至 tc_analyze_6a.c，6e 格式检查已提取至 tc_analyze_6e.c）。
     * 子阶段间 fail-fast：任一步骤返回 -1 则立即中止。
     */

    /* ==== 阶段 6 入口：类型与语义分析（6a→6b→6c→6d→6e） ==== */
    if (tc_pass2_type_check(&out->program, &out->symbols, &struct_table, &func_env, &out->warnings,
                            diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            if (tc_pass2_type_check(&out->deps[di], &out->symbols, &struct_table, &func_env,
                                    &out->warnings, diag) != 0) {
                tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
                tc_struct_table_free(&struct_table);
                tc_func_signature_list_free(&sigs);
                tc_member_index_free(&members);
                tc_typed_program_free(out);
                return -1;
            }
        }
    }

    out->cfg_set = (TcCfgSet *)malloc(sizeof(TcCfgSet));
    if (!out->cfg_set) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }
    tc_cfg_set_init(out->cfg_set);
    if (tc_cfg_build_all(&out->program, &out->symbols, out->cfg_set, diag) != 0 ||
        tc_analyze_definite_init_all(out->cfg_set, &out->program,
                                     tc_symbol_table_runtime_slot_count(&out->symbols),
                                     diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }
    out->cfg = &out->cfg_set->toplevel;

    /* 阶段 12：调用图 */
    if (tc_callgraph_check(&func_env, diag) != 0) {
        tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
        tc_struct_table_free(&struct_table);
        tc_func_signature_list_free(&sigs);
        tc_member_index_free(&members);
        tc_typed_program_free(out);
        return -1;
    }

    {
        TcStructTable *owned = (TcStructTable *)malloc(sizeof(TcStructTable));
        if (!owned) {
            tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
            tc_struct_table_free(&struct_table);
            tc_func_signature_list_free(&sigs);
            tc_member_index_free(&members);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            tc_typed_program_free(out);
            return -1;
        }
        *owned = struct_table;
        out->struct_table = owned;
    }

    tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
    tc_func_signature_list_free(&sigs);
    tc_member_index_free(&members);
    return 0;
}

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    return tc_analyze_ex(program, out, NULL, NULL, diag);
}
