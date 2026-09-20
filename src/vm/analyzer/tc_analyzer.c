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

    /*
     * B-22：语言标准 §11 第 3 条「专用码优先于通用码」——字面量类型不匹配在任何
     * 位置都报 TC_CE_LITERAL_TYPE，不得被调用点传入的比较/条件类通用码覆盖
     *（形参保留以维持既有调用签名）。
     */
    (void)literal_type_err;
    if (!tc_literal_fits_context(lit, expected, &err_kind)) {
        if (err_kind == TC_CE_LITERAL_TYPE) {
            tc_diagnostic_set(diag, TC_CE_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
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
/*  B-2：SEM 类诊断的源序选择                                           */
/* ------------------------------------------------------------------ */

/*
 * [语言标准 §11] 第 1、2 条：先按诊断类阶段（LT → SYN → SEM → CT）取前者；
 * 同一诊断类阶段内按源位置（行升序，同行列升序）取首个。编译器标准 §1.3 把
 * 第 4–8、11、12 阶段同归 SEM，故这五个内部阶段之间必须按源序比较。
 *
 * 本实现的 SEM 检查按内部阶段 fail-fast：Pass1 → Pass2（6a–8）→ CFG（11）
 * → 调用图（12）。为满足源序要求，较早阶段失败后仍尝试运行更晚的 SEM 阶段：
 * 若其首个诊断的源位置更靠前，则改报它。
 *
 * 安全性依据：CFG 读集按符号槽位展开（tc_cfg.c 不依赖类型解析结果），调用图
 * 只需函数签名（4d 已收集）与已记录的调用边；因此在 Pass2 失败后运行这两个
 * 阶段不会解引用未解析类型。
 *
 * 作用域：仅对入口编译单元比较源序；依赖库文件的位置序不在标准定义范围内。
 */

/* 候选诊断是否比当前保留诊断更靠前（行升序；同行须双方列号已知才比较）。 */
static int tc_sem_diag_earlier(int cand_line, int cand_column, int keep_line, int keep_column) {
    if (cand_line <= 0) {
        return 0;
    }
    if (cand_line != keep_line) {
        return cand_line < keep_line;
    }
    if (cand_column < 0 || keep_column < 0) {
        return 0;
    }
    return cand_column < keep_column;
}

/* 用 tmp 的（更早）诊断替换 diag；保留原 source 绑定以便片段与后续诊断。 */
static void tc_sem_take_diag(TcDiagnostic *diag, const TcDiagnostic *cand, const char *file,
                             const char *source) {
    char *msg = cand->message ? strdup(cand->message) : NULL;
    /*
     * B-40：`tc_diagnostic_clear` 会释放挂起的 SEM 类诊断，但那是解析期记录、
     * 尚未与其它 SEM 诊断按源序竞争的候选，此处必须先摘出再恢复（先置零以避免
     * clear 释放其字符串）。
     */
    TcDeferredDiagnostic keep_sem = diag->deferred_sem;

    memset(&diag->deferred_sem, 0, sizeof(diag->deferred_sem));
    tc_diagnostic_clear(diag);
    diag->deferred_sem = keep_sem;
    if (file || source) {
        if (tc_diagnostic_set_source(diag, file, source) != 0) {
            return;
        }
    }
    if (msg) {
        tc_diagnostic_set(diag, cand->kind, cand->line, cand->column, msg);
        free(msg);
    }
}

/*
 * 在较早 SEM 阶段失败后尝试更晚的 SEM 阶段（try_cfg=1 时含阶段 11，否则只跑
 * 阶段 12）。返回 1 表示已替换为更早的诊断，0 表示保持原诊断不变。
 */
static int tc_sem_salvage(TcTypedProgram *out, const TcSymbolTable *symbols,
                          TcFuncCheckEnv *func_env, TcDiagnostic *diag, int try_cfg) {
    TcDiagnostic tmp;
    const char *file_ptr = NULL;
    const char *source_ptr = NULL;
    char *file = NULL;
    char *source = NULL;
    int replaced = 0;

    if (!tc_diagnostic_is_set(diag)) {
        return 0;
    }
    tc_diagnostic_get_source(diag, &file_ptr, &source_ptr);
    if (file_ptr) {
        file = strdup(file_ptr);
    }
    if (source_ptr) {
        source = strdup(source_ptr);
    }

    tc_diagnostic_init(&tmp);
    if (file || source) {
        if (tc_diagnostic_set_source(&tmp, file, source) != 0) {
            goto done;
        }
    }

    /* 阶段 12：调用图（签名已在 4d 收集；调用边为 Pass2 已处理前缀） */
    if (tc_callgraph_check(func_env, &tmp) != 0 && tc_diagnostic_is_set(&tmp)) {
        if (tc_sem_diag_earlier(tmp.line, tmp.column, diag->line, diag->column)) {
            tc_sem_take_diag(diag, &tmp, file, source);
            replaced = 1;
            goto done;
        }
    }

    /* 阶段 11：CFG 构建 + 确定初始化（入口单元） */
    if (try_cfg) {
        TcCfgSet cfg_set;

        tc_diagnostic_clear(&tmp);
        if (file || source) {
            if (tc_diagnostic_set_source(&tmp, file, source) != 0) {
                goto done;
            }
        }
        tc_cfg_set_init(&cfg_set);
        if (tc_cfg_build_all(&out->program, symbols, &cfg_set, &tmp) != 0 ||
            tc_analyze_definite_init_all(
                &cfg_set, &out->program, tc_symbol_table_runtime_slot_count(symbols),
                &tmp) != 0) {
            if (tc_diagnostic_is_set(&tmp) &&
                tc_sem_diag_earlier(tmp.line, tmp.column, diag->line, diag->column)) {
                tc_sem_take_diag(diag, &tmp, file, source);
                replaced = 1;
            }
        }
        tc_cfg_set_free(&cfg_set);
    }

done:
    tc_diagnostic_clear(&tmp);
    free(file);
    free(source);
    return replaced;
}


/* ------------------------------------------------------------------ */
/*  tc_analyze_ex — 文件模式分析入口                                      */
/* ------------------------------------------------------------------ */

/** 把诊断定位切到某个模块自身（B-14：依赖模块诊断不得定位到入口文件）。 */
static int tc_diag_use_module(TcDiagnostic *diag, const TcProgram *prog) {
    return tc_diagnostic_use_source(diag, prog ? prog->source_path : NULL,
                                    prog ? prog->source_text : NULL);
}

static int tc_analyze_impl(TcProgram *program, TcTypedProgram *out, const char *entry_path,
                           const char *entry_module_name, const TcModuleSearchPaths *search,
                           TcDiagnostic *diag) {
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
    program->source_text = NULL;
    /*
     * B-14：保留入口源文本，供「入口 ↔ 依赖模块」之间的诊断定位切换。调用方
     *（libtc / 驱动）不一定设置它，故退回到诊断对象里已绑定的入口源文本。
     */
    if (!out->program.source_text) {
        const char *entry_file = NULL;
        const char *entry_text = NULL;

        tc_diagnostic_get_source(diag, &entry_file, &entry_text);
        (void)entry_file;
        if (entry_text) {
            out->program.source_text = strdup(entry_text);
            if (!out->program.source_text) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
        }
    }

    /*
     * 入口诊断定位副本：依赖模块阶段会临时把诊断 source 切到模块自身（B-14），
     * 每个依赖阶段结束后都要切回入口。set_source 会释放旧文本，故此处 strdup。
     */
    {
        const char *entry_file = NULL;
        const char *entry_text = NULL;

        tc_diagnostic_get_source(diag, &entry_file, &entry_text);
        if (entry_file) {
            saved_file = strdup(entry_file);
            if (!saved_file) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
        }
        if (entry_text) {
            saved_source = strdup(entry_text);
            if (!saved_source) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
        }
    }

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
        if (tc_module_resolve_imports_ex(out, entry_path, entry_module_name, search, diag) != 0) {
            goto fail;
        }
    }

    {
        size_t di = 0;
        size_t *dep_order = NULL;

        /*
         * deps 收集为 DFS 前序（importer 先入后递归），简单逆序在菱形依赖
         * （entry→A→C 且 entry→B→C）下非法：逆序得 [B, C, A]，B 先于 C
         * 注册会使 B 引用 C.<struct> 误报 TC_CE_UNDEFINED_STRUCT。
         * 结构体名解析须 importee 先入表，故用真拓扑序注册。
         */
        if (out->dep_count > 0) {
            dep_order = (size_t *)malloc(out->dep_count * sizeof(size_t));
            if (!dep_order) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                goto fail;
            }
            if (tc_module_topological_dep_order(out, dep_order, diag) != 0) {
                free(dep_order);
                goto fail;
            }
            for (di = 0; di < out->dep_count; di++) {
                if (tc_diag_use_module(diag, &out->deps[dep_order[di]]) != 0) {
                    free(dep_order);
                    goto fail;
                }
                if (tc_struct_table_register_program(&out->deps[dep_order[di]], &struct_table,
                                                     diag) != 0) {
                    free(dep_order);
                    goto fail;
                }
            }
            free(dep_order);
            if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
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
            if (tc_diag_use_module(diag, &out->deps[di]) != 0) {
                goto fail;
            }
            if (tc_pass1_collect_symbols(&out->deps[di], &out->symbols, out->type_table,
                                         diag) != 0) {
                goto fail;
            }
        }
        if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
            goto fail;
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
    if (tc_func_eval_static_lets(&out->program, &out->symbols, &struct_table,
                                     out->type_table, &members, diag) != 0) {
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            TcMemberIndex dep_members;
            int rc = 0;

            /* 依赖模块的 static let 同样按**该模块**成员索引解析 `Self.<名>`（§4.3、§4.4） */
            tc_member_index_init(&dep_members);
            if (tc_member_index_build(&out->deps[di], &dep_members, diag) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            if (tc_diag_use_module(diag, &out->deps[di]) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            rc = tc_func_eval_static_lets(&out->deps[di], &out->symbols, &struct_table,
                                          out->type_table, &dep_members, diag);
            tc_member_index_free(&dep_members);
            if (rc != 0) {
                goto fail;
            }
        }
        if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
            goto fail;
        }
    }
    if (tc_func_check_static_vars(&out->program, &members, &out->symbols, &struct_table,
                                  diag) != 0) {
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            TcMemberIndex dep_members;

            tc_member_index_init(&dep_members);
            if (tc_member_index_build(&out->deps[di], &dep_members, diag) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            if (tc_diag_use_module(diag, &out->deps[di]) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            if (tc_func_check_static_vars(&out->deps[di], &dep_members, &out->symbols,
                                          &struct_table, diag) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            tc_member_index_free(&dep_members);
        }
        if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
            goto fail;
        }
    }

    memset(&func_env, 0, sizeof(func_env));
    func_env.prog = out;
    func_env.sigs = &sigs;
    func_env.members = &members;
    func_env.current_func = NULL;
    func_env.struct_table = &struct_table;
    func_env.module_index = tc_func_env_module_index(&out->program);

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
        /* B-2：Pass2（6a–8）与 CFG（11）、调用图（12）同属 SEM，按源序选首个 */
        (void)tc_sem_salvage(out, &out->symbols, &func_env, diag, 1);
        goto fail;
    }
    {
        size_t di = 0;
        for (di = 0; di < out->dep_count; di++) {
            /*
             * B-56：依赖模块的 `Self.<函数名>` 必须按**该模块**的成员索引与签名
             * module_index 解析。此前一律沿用入口的 members / 硬编码 -1，导致
             * `#lib` 内 `funcall(Self.f, …)` 一旦被 import 就报 UNDEFINED_FUNCTION。
             */
            TcMemberIndex dep_members;
            int rc = 0;

            tc_member_index_init(&dep_members);
            if (tc_member_index_build(&out->deps[di], &dep_members, diag) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            func_env.members = &dep_members;
            func_env.module_index = (int)di;
            /* B-14：依赖模块体内的诊断定位到模块自身文件与源文本。 */
            if (tc_diag_use_module(diag, &out->deps[di]) != 0) {
                tc_member_index_free(&dep_members);
                goto fail;
            }
            rc = tc_pass2_type_check(&out->deps[di], &out->symbols, &struct_table, &func_env,
                                     &out->warnings, diag);
            tc_member_index_free(&dep_members);
            if (rc != 0) {
                goto fail;
            }
        }
        func_env.members = &members;
        func_env.module_index = tc_func_env_module_index(&out->program);
        if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
            goto fail;
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
        /* B-2：阶段 11 属 SEM；若阶段 12（调用图）的诊断源位置更靠前，改报它 */
        (void)tc_sem_salvage(out, &out->symbols, &func_env, diag, 0);
        goto fail;
    }
    out->cfg = &out->cfg_set->toplevel;

    /* 阶段 11：依赖库函数体同样执行 CFG 数据流检查（确定初始化 / 不可达 /
     * MISSING_RETURN）。此前仅对入口模块构建 CFG，导入库的函数体缺陷
     * （如 goto 跳过初始化、缺少 return）会静默通过。依赖库诊断定位到
     * 其自身文件（fail-fast：出错即返回，无需恢复入口 source）。 */
    {
        size_t di = 0;

        for (di = 0; di < out->dep_count; di++) {
            TcCfgSet dep_set;
            int dep_rc = 0;

            if (tc_diag_use_module(diag, &out->deps[di]) != 0) {
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
        if (tc_diagnostic_use_source(diag, saved_file, saved_source) != 0) {
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
        /*
         * 所有权已转移：此后若因挂起诊断（B-40 的 SEM 项 / 第 9、10 阶段 CT 项）
         * 失败，`fail:` 会同时释放局部表与 `out->struct_table`。将局部表复位为零，
         * 避免对同一 items/fields 双重释放。
         */
        tc_struct_table_init(&struct_table);
    }

    /*
     * 语言标准 §11「阶段优先」：全部 SEM 类检查（第 6c～6e、
     * 11、12 阶段）均无触发，此时才发布第 9/10 阶段挂起的 CT 类诊断
     * （常量求值与静态三态判定）。若有挂起诊断，本次分析按失败返回，
     * 调用方不得进入执行阶段。
     */
    /*
     * B-40：解析期挂起的 **SEM 类**诊断须先于 CT 类发布（§11 阶段优先）。
     * 此处仅在有真实 SEM 诊断时失败；否则继续走 CT 挂起项的发布。
     */
    if (tc_diagnostic_publish_deferred_sem(diag) != 0) {
        goto fail;
    }
    if (tc_diagnostic_flush_deferred(diag) != 0) {
        goto fail;
    }

    ret = 0;

fail:
    /*
     * B-40：失败路径同样要让解析期挂起的 SEM 诊断参与竞争——若它与已报告的
     * 诊断同属一个源文件且源序更靠前，则替换（§11 第 2 条）。
     */
    if (ret != 0 && tc_diagnostic_publish_deferred_sem(diag) != 0) {
        ret = -1;
    }
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

int tc_analyze_ex(TcProgram *program, TcTypedProgram *out, const char *entry_path,
                  const TcModuleSearchPaths *search, TcDiagnostic *diag) {
    return tc_analyze_impl(program, out, entry_path, NULL, search, diag);
}

int tc_analyze_memory(TcProgram *program, TcTypedProgram *out, const char *display_name,
                      const char *entry_module_name, const TcModuleSearchPaths *search,
                      TcDiagnostic *diag) {
    return tc_analyze_impl(program, out, display_name, entry_module_name, search, diag);
}

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    return tc_analyze_ex(program, out, NULL, NULL, diag);
}
