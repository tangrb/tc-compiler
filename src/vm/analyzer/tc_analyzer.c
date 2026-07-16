/*
 * tc_analyzer.c — TC 静态分析器实现
 *
 * 两遍扫描架构：
 *   Pass 1 — 符号收集：扫描所有 var/let 定义，分配运行时 slot，检测重复定义
 *   Pass 2 — 类型与语义检查：按语句顺序校验类型兼容性、字面量范围、overflow 模式合法性、
 *            let 常量编译期求值、未初始化变量静态错误
 *
 * 分析通过后产出 TcTypedProgram，Executor 可直接消费。
 * 子模块：tc_analyzer_pass1 / pass2 / dfa / repl。
 */
#include "tc_analyzer.h"
#include "tc_analyzer_internal.h"
#include "tc_analyzer_pass1.h"
#include "tc_analyzer_pass2.h"
#include "tc_cfg.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  TcTypedProgram 生命周期管理                                          */
/* ------------------------------------------------------------------ */

void tc_typed_program_init(TcTypedProgram *program) {
    tc_program_init(&program->program);
    tc_symbol_table_init(&program->symbols);
    program->cfg = NULL;
    tc_warning_list_init(&program->warnings);
}

void tc_typed_program_free(TcTypedProgram *program) {
    if (program->cfg) {
        tc_cfg_free(program->cfg);
        free(program->cfg);
        program->cfg = NULL;
    }
    tc_program_free(&program->program);
    tc_symbol_table_free(&program->symbols);
    tc_warning_list_free(&program->warnings);
}


/* ------------------------------------------------------------------ */
/*  字面量检查辅助                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 检查 TcLiteral 能否放入目标类型
 * @return 检查通过返回 0；失败返回 -1 并设置 diag
 */
int tc_check_literal(const TcLiteral *lit, TcType expected, int line,
                            TcDiagnostic *diag, TcErrorKind literal_type_err) {
    TcErrorKind err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
    if (!tc_literal_fits_context(lit, expected, &err_kind)) {
        if (err_kind == TC_ERR_LITERAL_TYPE) {
            tc_diagnostic_set(diag, literal_type_err, line, TC_COLUMN_UNKNOWN,
                              "literal type does not match context");
        } else {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                              "literal out of range for context type");
        }
        return -1;
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/*  tc_analyze — 两遍分析入口                                            */
/* ------------------------------------------------------------------ */

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    tc_typed_program_init(out);
    /*
     * 通过 struct 浅拷贝转移 program 的 items 所有权给 out->program。
     * 然后将 program 清零，避免调用方二次 free（所有权转移模式）。
     * 后续 Pass1/Pass2 失败时 tc_typed_program_free 统一回收所有资源。
     */
    out->program = *program;
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;

    if (tc_pass1_collect_symbols(&out->program, &out->symbols, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    if (tc_pass2_type_check(&out->program, &out->symbols, &out->warnings, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    out->cfg = (TcCfg *)malloc(sizeof(TcCfg));
    if (!out->cfg) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        tc_typed_program_free(out);
        return -1;
    }
    tc_cfg_init(out->cfg);
    if (tc_cfg_build(&out->program, &out->symbols, out->cfg, diag) != 0 ||
        tc_analyze_definite_init(out->cfg,
                                 tc_symbol_table_runtime_slot_count(&out->symbols), diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    return 0;
}
