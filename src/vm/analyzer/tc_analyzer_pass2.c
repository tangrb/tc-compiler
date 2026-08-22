/*
 * tc_analyzer_pass2.c — Pass2 类型与语义检查（6c+6d 主体：goto 解析+类型/mode 检查）
 *
 * 6a 标签收集已提取到 tc_analyze_6a.c，6e 格式检查已提取到 tc_analyze_6e.c。
 * 本文件保留 6c（goto/label 名称解析+跳转合法性）和 6d（类型/模式/字面量检查）。
 */
#include "tc_analyzer_pass2.h"
#include "tc_analyzer_pass2_rhs.h"
#include "tc_analyze_6a.h"
#include "tc_analyze_6e.h"
#include "tc_type_check.h"
#include "tc_memblock_check.h"
#include "tc_ptr_check.h"
#include "tc_struct_check.h"
#include "tc_func_check.h"
#include "tc_analyzer_dfa.h"

#include "tc_const_eval.h"
#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"
#include "tc_stmt_index.h"
#include "tc_symbol.h"
#include "tc_warning.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Pass2：将 cast/bitcast 目标定型为单例或 type_table intern 指针。
 * Executor/AOT 只读使用，不得再写入 type_table。
 */
int tc_pass2_resolve_target_type(TcInitHistory *hist, const TcType *owned,
                                        const TcType **out, int line, TcDiagnostic *diag) {
    TcTypeTable *table = hist ? hist->type_table : NULL;
    const TcType *interned = NULL;

    if (!owned || !out) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "internal error: missing cast/bitcast target type");
        return -1;
    }
    if (owned->tag != TC_PTR && owned->tag != TC_MEMBLOCK && owned->tag != TC_STRUCT) {
        *out = tc_type_tag_singleton(owned->tag);
        return 0;
    }
    if (!table) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "internal error: missing type table for cast/bitcast target");
        return -1;
    }
    interned = tc_type_intern(table, owned, diag);
    if (!interned) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    *out = interned;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  goto 解析与跳转合法性                                                */
/* ------------------------------------------------------------------ */

/*
 * §7.3：每个函数独立标签表。仅解析当前函数（func_id）内的标签；
 * 跨函数同名标签互不相关，函数内无同名标签 → 调用方报 TC_CE_LABEL_NOT_FOUND。
 */
static const TcLabelEntry *tc_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                                 int func_id, const TcBlockPath *goto_path) {
    const TcLabelEntry *best_same = NULL;
    const TcLabelEntry *best_ancestor = NULL;
    const TcLabelEntry *any = NULL;
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0 || entry->func_id != func_id) {
            continue;
        }
        any = entry;
        if (entry->block_depth == goto_path->depth &&
            tc_paths_equal_prefix(entry->block_path, goto_path->path, entry->block_depth)) {
            best_same = entry;
        } else if (entry->block_depth < goto_path->depth &&
                   tc_paths_equal_prefix(entry->block_path, goto_path->path,
                                         entry->block_depth)) {
            if (!best_ancestor || entry->block_depth > best_ancestor->block_depth) {
                best_ancestor = entry;
            }
        }
    }
    if (best_same) {
        return best_same;
    }
    if (best_ancestor) {
        return best_ancestor;
    }
    return any;
}

static int tc_check_goto_jump(const TcBlockPath *goto_path, const TcLabelEntry *label,
                              int line, TcDiagnostic *diag) {
    TcBlockPath label_path;

    label_path.path = label->block_path;
    label_path.depth = label->block_depth;
    label_path.capacity = label->block_depth;

    if (goto_path->depth == label_path.depth) {
        if (tc_paths_equal_prefix(goto_path->path, label_path.path, goto_path->depth)) {
            return 0; /* 平级跳转 */
        }
        tc_diagnostic_set(diag, TC_CE_JUMP_INCOMPATIBLE_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into incompatible block");
        return -1;
    }

    if (goto_path->depth > label_path.depth) {
        if (tc_paths_equal_prefix(goto_path->path, label_path.path, label_path.depth)) {
            return 0; /* 向外跳转（祖先） */
        }
        tc_diagnostic_set(diag, TC_CE_JUMP_INCOMPATIBLE_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into incompatible block");
        return -1;
    }

    /* goto 更浅：若 label 在 goto 的子路径上 → 跳入子块，否则兄弟 */
    if (tc_paths_equal_prefix(label_path.path, goto_path->path, goto_path->depth)) {
        tc_diagnostic_set(diag, TC_CE_JUMP_INTO_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into inner block");
        return -1;
    }
    tc_diagnostic_set(diag, TC_CE_JUMP_INCOMPATIBLE_BLOCK, line, TC_COLUMN_UNKNOWN,
                      "cannot jump into incompatible block");
    return -1;
}


/* ------------------------------------------------------------------ */
/*  绑定解析                                                             */
/* ------------------------------------------------------------------ */

void tc_resolved_binding_set(TcResolvedBinding *binding, const TcSymbol *symbol) {
    binding->resolved = 1;
    binding->slot = symbol->sym_kind == TC_SYM_VARIABLE ? symbol->slot : -1;
    binding->is_const = symbol->sym_kind == TC_SYM_CONSTANT;
    binding->type = symbol->type;
    binding->const_bits = symbol->has_const_value ? symbol->const_value.bits : 0;
}

const TcSymbol *tc_resolve_visible_symbol(const TcSymbolTable *visible,
                                                 const TcSymbolTable *global, const char *name,
                                                 size_t stmt_index, int line,
                                                 TcDiagnostic *diag) {
    const TcSymbol *symbol = tc_symbol_table_find(visible, name);
    char msg[128];

    if (symbol) {
        return symbol;
    }
    if (global) {
        const TcSymbol *block_sym =
            tc_symbol_for_assign_target(global, name, (int)stmt_index);

        if (block_sym && block_sym->scope_end_stmt_index >= 0) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Pass2 语句遍历（类型 / 模式 / goto）                                 */
/* ------------------------------------------------------------------ */


static int tc_pass2_check_stmt(TcStatement *stmt, TcSymbolTable *symbols,
                               TcSymbolTable *visible, TcStructTable *struct_table,
                               TcAnalyzeCtx *ctx, TcInitHistory *hist, TcWarningList *warnings,
                               TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_WHILE) {
        TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        TcSymbolTable visible_body;
        TcInitState *before_loop = NULL;
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);
        int saved_loop_id = ctx->current_loop_id;
        int saved_reachable = ctx->path_reachable;

        if (ctx->num_slots > 0) {
            before_loop = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            if (!before_loop) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, while_stmt->line,
                                  TC_COLUMN_UNKNOWN, "memory allocation failed");
                return -1;
            }
            tc_init_states_copy(before_loop, ctx->init_states, ctx->num_slots);
        }
        if (tc_check_condition(&while_stmt->condition, visible, symbols, hist,
                               (size_t)while_stmt_index, while_stmt->line, "while", diag,
                               warnings) != 0) {
            free(before_loop);
            return -1;
        }
        if (tc_block_path_push(&ctx->block_path, tc_block_id_while(while_stmt_index), diag) != 0 ||
            tc_visible_copy_from(visible, &visible_body, diag) != 0) {
            free(before_loop);
            return -1;
        }
        ctx->loop_depth++;
        ctx->current_loop_id = while_stmt->loop_id;
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_pass2_check_stmt(&while_stmt->body[i], symbols, &visible_body, struct_table,
                                    ctx, hist, warnings, diag) != 0) {
                ctx->current_loop_id = saved_loop_id;
                ctx->loop_depth--;
                tc_symbol_table_free(&visible_body);
                tc_block_path_pop(&ctx->block_path);
                free(before_loop);
                return -1;
            }
        }
        ctx->current_loop_id = saved_loop_id;
        ctx->loop_depth--;
        tc_symbol_table_free(&visible_body);
        tc_block_path_pop(&ctx->block_path);
        if (ctx->num_slots > 0) {
            tc_init_states_copy(ctx->init_states, before_loop, ctx->num_slots);
        }
        ctx->path_reachable = saved_reachable;
        if (hist) {
            hist->check_init = saved_reachable;
        }
        free(before_loop);
        return 0;
    }

    if (stmt->kind == TC_STMT_IF) {
        TcIfStmt *if_stmt = &stmt->u.if_stmt;
        TcSymbolTable visible_then;
        TcInitState *before_then = NULL;
        TcInitState *after_then = NULL;
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);
        size_t stmt_index = (size_t)if_stmt_index;
        int then_reachable = 1;
        int else_reachable = 1;
        int saved_reachable = ctx->path_reachable;

        if (ctx->num_slots > 0) {
            before_then = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            after_then = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            if (!before_then || !after_then) {
                free(before_then);
                free(after_then);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, if_stmt->line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            tc_init_states_copy(before_then, ctx->init_states, ctx->num_slots);
        }

        if (tc_check_condition(&if_stmt->condition, visible, symbols, hist, stmt_index,
                               if_stmt->line, "if", diag, warnings) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }

        if (tc_block_path_push(&ctx->block_path, tc_block_id_then(if_stmt_index), diag) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }
        if (tc_visible_copy_from(visible, &visible_then, diag) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass2_check_stmt(&if_stmt->then_body[i], symbols, &visible_then, struct_table,
                                    ctx, hist, warnings, diag) != 0) {
                tc_symbol_table_free(&visible_then);
                free(before_then);
                free(after_then);
                return -1;
            }
        }
        tc_symbol_table_free(&visible_then);
        tc_block_path_pop(&ctx->block_path);
        then_reachable = ctx->path_reachable;
        if (ctx->num_slots > 0) {
            tc_init_states_copy(after_then, ctx->init_states, ctx->num_slots);
            tc_init_states_copy(ctx->init_states, before_then, ctx->num_slots);
        }
        ctx->path_reachable = saved_reachable;
        if (hist) {
            hist->check_init = saved_reachable;
        }

        if (if_stmt->else_count > 0) {
            TcSymbolTable visible_else;

            if (tc_block_path_push(&ctx->block_path, tc_block_id_else(if_stmt_index), diag) !=
                0) {
                free(before_then);
                free(after_then);
                return -1;
            }
            if (tc_visible_copy_from(visible, &visible_else, diag) != 0) {
                free(before_then);
                free(after_then);
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass2_check_stmt(&if_stmt->else_body[i], symbols, &visible_else,
                                        struct_table, ctx, hist, warnings, diag) != 0) {
                    tc_symbol_table_free(&visible_else);
                    free(before_then);
                    free(after_then);
                    return -1;
                }
            }
            tc_symbol_table_free(&visible_else);
            tc_block_path_pop(&ctx->block_path);
            else_reachable = ctx->path_reachable;
            if (ctx->num_slots > 0) {
                if (then_reachable && else_reachable) {
                    tc_init_states_merge(ctx->init_states, after_then, ctx->init_states,
                                         ctx->num_slots);
                } else if (then_reachable) {
                    tc_init_states_copy(ctx->init_states, after_then, ctx->num_slots);
                }
                /* 仅 else 可达：ctx 已是 else 结束状态；均不可达：保留 else 快照 */
            }
            ctx->path_reachable =
                saved_reachable ? (then_reachable || else_reachable) : 0;
        } else {
            if (ctx->num_slots > 0) {
                if (then_reachable) {
                    tc_init_states_merge(ctx->init_states, after_then, before_then,
                                         ctx->num_slots);
                } else {
                    tc_init_states_copy(ctx->init_states, before_then, ctx->num_slots);
                }
            }
            /* 无 else 时可跳过 then，合流点仍可达 */
            ctx->path_reachable = saved_reachable;
        }
        if (hist) {
            hist->check_init = ctx->path_reachable;
        }
        free(before_then);
        free(after_then);
        return 0;
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        TcLoopControlStmt *control = stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt
                                                                 : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (ctx->current_loop_id < 0) {
            TcErrorKind kind = stmt->kind == TC_STMT_BREAK ? TC_CE_BREAK_OUTSIDE_LOOP
                                                            : TC_CE_CONTINUE_OUTSIDE_LOOP;
            const char *message = stmt->kind == TC_STMT_BREAK ? "break used outside while"
                                                               : "continue used outside while";

            tc_diagnostic_set(diag, kind, control->line, TC_COLUMN_UNKNOWN, message);
            return -1;
        }
        control->loop_id = ctx->current_loop_id;
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_GOTO) {
        TcGoto *goto_stmt = &stmt->u.goto_stmt;
        const TcLabelEntry *entry = NULL;
        char msg[128];

        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_CE_GOTO_OUTSIDE_FUNCTION, goto_stmt->line,
                              TC_COLUMN_UNKNOWN, "goto is only allowed inside a function");
            return -1;
        }
        entry = tc_resolve_goto_label(symbols, goto_stmt->target, ctx->current_func_id,
                                      &ctx->block_path);
        if (!entry) {
            (void)snprintf(msg, sizeof(msg), "label '%s' not found", goto_stmt->target);
            tc_diagnostic_set(diag, TC_CE_LABEL_NOT_FOUND, goto_stmt->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_check_goto_jump(&ctx->block_path, entry, goto_stmt->line, diag) != 0) {
            return -1;
        }
        goto_stmt->resolved_target_stmt_index = entry->stmt_index;
        goto_stmt->resolved = 1;
        /* 终止当前顺序路径：其后赋值不更新 init（前向跳过初始化） */
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        TcSymbolTable visible_body;
        size_t i = 0;
        const TcFuncSignature *saved_func = NULL;
        int saved_func_id = ctx->current_func_id;

        (void)tc_stmt_index_take(&ctx->index);
        tc_symbol_table_init(&visible_body);
        ctx->func_depth++;
        ctx->current_func_id = func->func_id;
        if (ctx->func_env) {
            saved_func = ctx->func_env->current_func;
            ctx->func_env->current_func = NULL;
            if (ctx->func_env->sigs) {
                size_t si = 0;
                for (si = 0; si < ctx->func_env->sigs->count; si++) {
                    if (ctx->func_env->sigs->items[si].func_id == func->func_id) {
                        ctx->func_env->current_func = &ctx->func_env->sigs->items[si];
                        break;
                    }
                }
            }
        }
        for (i = 0; i < func->param_count; i++) {
            const TcFuncParam *param = &func->params[i];
            const TcSymbol *sym = NULL;
            size_t si = 0;

            /* Pass1 已 pop 函数作用域；按 def_line + PARAM 域找回形参符号 */
            for (si = 0; si < symbols->count; si++) {
                const TcSymbol *cand = &symbols->symbols[si];

                if (cand->slot_domain == TC_SLOT_PARAM && cand->def_line == func->line &&
                    cand->name && param->name && strcmp(cand->name, param->name) == 0) {
                    sym = cand;
                    break;
                }
            }

            if (sym && tc_visible_add_from_global(symbols, param->name, sym->def_stmt_index,
                                                    &visible_body, diag) != 0) {
                if (ctx->func_env) {
                    ctx->func_env->current_func = saved_func;
                }
                ctx->func_depth--;
                ctx->current_func_id = saved_func_id;
                tc_symbol_table_free(&visible_body);
                return -1;
            }
            /* 形参入口已初始化 */
            if (sym && ctx->init_states && sym->slot >= 0 && sym->slot < ctx->num_slots) {
                ctx->init_states[sym->slot] = TC_INIT_INIT;
            }
        }
        for (i = 0; i < func->body_count; i++) {
            if (tc_pass2_check_stmt(&func->body[i], symbols, &visible_body, struct_table, ctx,
                                    hist, warnings, diag) != 0) {
                if (ctx->func_env) {
                    ctx->func_env->current_func = saved_func;
                }
                ctx->func_depth--;
                ctx->current_func_id = saved_func_id;
                tc_symbol_table_free(&visible_body);
                return -1;
            }
        }
        if (ctx->func_env) {
            ctx->func_env->current_func = saved_func;
        }
        ctx->func_depth--;
        ctx->current_func_id = saved_func_id;
        tc_symbol_table_free(&visible_body);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_CE_LABEL_OUTSIDE_FUNCTION, stmt->u.label_def.line,
                              TC_COLUMN_UNKNOWN, "label is only allowed inside a function");
            return -1;
        }
        /* 标签合流点：恢复可达（简化：不合并多入边状态） */
        ctx->path_reachable = 1;
        if (hist) {
            hist->check_init = 1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNCALL) {
        TcFuncallStmt *call = &stmt->u.funcall_stmt;
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (!ctx->func_env) {
            return 0;
        }
        return tc_func_check_funcall(ctx->func_env, call->is_self, call->qualifier,
                                     call->member_name, call->target, call->args, call->arg_count,
                                     0, NULL, call->line, visible, symbols, hist, stmt_index,
                                     warnings, &call->resolved_func_id, diag);
    }

    if (stmt->kind == TC_STMT_RETURN) {
        TcReturnStmt *ret = &stmt->u.return_stmt;
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (!ctx->func_env) {
            return 0;
        }
        if (tc_func_check_return(ctx->func_env, ret, visible, symbols, hist, stmt_index, warnings,
                                 diag) != 0) {
            return -1;
        }
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    {
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (stmt->kind == TC_STMT_VAR_DEF) {
            TcVarDef *var_def = &stmt->u.var_def;
            const TcSymbol *sym = NULL;

            if (var_def->rhs.kind == TC_RHS_FUNCALL_EXPR && ctx->func_env) {
                if (tc_pass2_check_funcall_rhs(&var_def->rhs, &var_def->full_type, 1, ctx,
                                               visible, symbols, hist, stmt_index, var_def->line,
                                               warnings, diag) != 0) {
                    return -1;
                }
            } else if (tc_type_check_rhs(&var_def->rhs, &var_def->full_type, visible, symbols,
                                         struct_table, hist, stmt_index, var_def->line, diag,
                                         warnings, var_def->name) != 0) {
                return -1;
            }
            if (tc_visible_add_from_global(symbols, var_def->name, (int)stmt_index, visible,
                                           diag) != 0) {
                return -1;
            }
            sym = tc_find_symbol_by_def_index(symbols, var_def->name, (int)stmt_index);
            if (sym && ctx->init_states && ctx->path_reachable && sym->slot >= 0 &&
                sym->slot < ctx->num_slots) {
                ctx->init_states[sym->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            const TcConstDef *const_def = &stmt->u.const_def;
            TcSymbol *global_sym =
                (TcSymbol *)tc_find_symbol_by_def_index(symbols, const_def->name, (int)stmt_index);

            if (const_def->rhs.kind == TC_RHS_CAST) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, const_def->line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant initializer must be a constant expression");
                return -1;
            }
            if (global_sym == NULL) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, const_def->line, TC_COLUMN_UNKNOWN,
                                  "internal analyzer error");
                return -1;
            }
            if (const_def->rhs.kind == TC_RHS_STRUCT_CONSTRUCTOR) {
                if (tc_type_check_rhs((TcRhs *)&const_def->rhs, &const_def->full_type, visible,
                                      symbols, struct_table, hist, stmt_index, const_def->line,
                                      diag, warnings, NULL) != 0) {
                    return -1;
                }
            }
            if (tc_resolve_const_value(global_sym, &const_def->rhs, visible, symbols,
                                       const_def->line, diag) != 0) {
                return -1;
            }
            if (tc_visible_add_from_global(symbols, const_def->name, (int)stmt_index, visible,
                                           diag) != 0) {
                return -1;
            }
            if (global_sym && ctx->init_states && global_sym->slot >= 0 &&
                global_sym->slot < ctx->num_slots) {
                ctx->init_states[global_sym->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            TcIoWrite *io_write = &stmt->u.io_write;

            if (tc_check_io_format(io_write->type->tag, &io_write->fmt, io_write->line, diag) != 0) {
                return -1;
            }
            return tc_check_operand(&io_write->operand, io_write->type->tag, visible, symbols, hist,
                                    stmt_index, io_write->line, diag, warnings, NULL,
                                    TC_CE_TYPE_MISMATCH);
        }

        if (stmt->kind == TC_STMT_READ) {
            TcRead *io_read = &stmt->u.io_read;
            const TcSymbol *target =
                tc_resolve_visible_symbol_scoped(visible, symbols, io_read->name, stmt_index,
                                                 io_read->line, diag,
                                                 ctx->func_env ? ctx->func_env->members : NULL,
                                                 ctx->func_depth > 0);

            if (!target) {
                return -1;
            }
            if (tc_func_check_writable_target(target, io_read->line, diag) != 0) {
                return -1;
            }
            if (tc_type_tag_of(target->type) != io_read->type->tag) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, io_read->line, TC_COLUMN_UNKNOWN,
                                  "read type does not match variable type");
                return -1;
            }
            tc_resolved_binding_set(&io_read->binding, target);
            if (ctx->init_states && ctx->path_reachable && target->slot >= 0 &&
                target->slot < ctx->num_slots) {
                ctx->init_states[target->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_ASSIGN) {
            TcAssign *assign = &stmt->u.assign;
            const TcSymbol *target =
                tc_resolve_visible_symbol_scoped(visible, symbols, assign->name, stmt_index,
                                                 assign->line, diag,
                                                 ctx->func_env ? ctx->func_env->members : NULL,
                                                 ctx->func_depth > 0);

            if (!target) {
                return -1;
            }
            if (target->sym_kind == TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, assign->line,
                                  TC_COLUMN_UNKNOWN, "cannot assign to constant");
                return -1;
            }
            if (tc_func_check_writable_target(target, assign->line, diag) != 0) {
                return -1;
            }
            tc_resolved_binding_set(&assign->binding, target);
            if (assign->rhs.kind == TC_RHS_FUNCALL_EXPR && ctx->func_env) {
                if (tc_pass2_check_funcall_rhs(&assign->rhs, target->type, 1, ctx, visible,
                                               symbols, hist, stmt_index, assign->line, warnings,
                                               diag) != 0) {
                    return -1;
                }
            } else if (tc_type_check_rhs(&assign->rhs, target->type, visible, symbols,
                                         struct_table, hist, stmt_index, assign->line, diag,
                                         warnings, NULL) != 0) {
                return -1;
            }
            if (ctx->init_states && ctx->path_reachable && target->slot >= 0 &&
                target->slot < ctx->num_slots) {
                ctx->init_states[target->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
            return tc_struct_check_field_assign(&stmt->u.field_assign, struct_table, visible,
                                                symbols, hist, stmt_index, diag, warnings);
        }

        /* 指针 / memblock 语句侧检查 */
        if (stmt->kind == TC_STMT_PTR_STORE) {
            return tc_ptr_check_store(&stmt->u.ptr_store, visible, symbols, hist, stmt_index, diag,
                                      warnings);
        }

        if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
            return tc_memblock_check_store(&stmt->u.memblock_store, visible, symbols, hist,
                                           stmt_index, diag, warnings);
        }

        if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
            return tc_memblock_check_copy(&stmt->u.memblock_copy, visible, symbols, hist,
                                          stmt_index, diag, warnings);
        }

        if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
            return tc_memblock_check_memcopy_unsafe(&stmt->u.memcopy_unsafe, visible, symbols,
                                                    hist, stmt_index, diag, warnings);
        }
    }

    return 0;
}

int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcStructTable *struct_table,
                        TcFuncCheckEnv *func_env, TcWarningList *warnings, TcDiagnostic *diag) {
    TcSymbolTable visible;
    TcAnalyzeCtx ctx;
    TcInitHistory hist;
    int *last_init = NULL;
    TcInitState *init_states = NULL;
    size_t i = 0;

    tc_symbol_table_init(&visible);
    memset(&ctx, 0, sizeof(ctx));
    ctx.program = program;
    ctx.func_env = func_env;
    tc_stmt_index_reset(&ctx.index);
    tc_block_path_init(&ctx.block_path);
    ctx.path_reachable = 1;
    ctx.current_loop_id = -1;
    ctx.loop_depth = 0;
    ctx.func_depth = 0;
    ctx.current_func_id = -1;
    ctx.num_slots = (int)tc_symbol_table_runtime_slot_count(symbols);

    if (ctx.num_slots > 0) {
        last_init = (int *)malloc((size_t)ctx.num_slots * sizeof(int));
        init_states = (TcInitState *)malloc((size_t)ctx.num_slots * sizeof(TcInitState));
        if (!last_init || !init_states) {
            free(last_init);
            free(init_states);
            tc_symbol_table_free(&visible);
            tc_block_path_free(&ctx.block_path);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < (size_t)ctx.num_slots; i++) {
            last_init[i] = -1;
        }
        tc_init_states_reset(init_states, ctx.num_slots, TC_INIT_UNINIT);
        for (i = 0; i < symbols->count; i++) {
            const TcSymbol *sym = &symbols->symbols[i];

            if (sym->slot >= 0 && sym->slot < ctx.num_slots) {
                if (sym->sym_kind == TC_SYM_CONSTANT || sym->initialized) {
                    init_states[sym->slot] = TC_INIT_INIT;
                }
            }
        }
    }
    ctx.last_init = last_init;
    ctx.init_states = init_states;
    tc_prescan_init_history(program, symbols, &ctx);

    /* 6a: 收集全部标签（带块路径，支持前向 goto） */
    tc_symbol_table_clear_labels(symbols);
    tc_stmt_index_reset(&ctx.index);
    ctx.block_path.depth = 0;
    ctx.loop_depth = 0;
    ctx.func_depth = 0;
    if (tc_analyze_6a_collect_labels(program, symbols, &ctx, diag) != 0) {
        tc_symbol_table_free(&visible);
        tc_block_path_free(&ctx.block_path);
        free(last_init);
        free(init_states);
        return -1;
    }

    tc_stmt_index_reset(&ctx.index);
    ctx.block_path.depth = 0;
    ctx.path_reachable = 1;
    ctx.current_loop_id = -1;
    ctx.loop_depth = 0;
    hist.program = program;
    hist.last_init_stmt_index = NULL; /* 文件模式走路径敏感 init_states */
    hist.init_states = init_states;
    hist.num_slots = ctx.num_slots;
    hist.check_init = 1;
    hist.defer_to_cfg = 1;
    hist.type_table = (func_env && func_env->prog) ? func_env->prog->type_table : NULL;
    ctx.type_table = hist.type_table;

    for (i = 0; i < program->count; i++) {
        if (tc_pass2_check_stmt(&program->items[i], symbols, &visible, struct_table, &ctx, &hist,
                                warnings, diag) != 0) {
            tc_symbol_table_free(&visible);
            tc_block_path_free(&ctx.block_path);
            free(last_init);
            free(init_states);
            return -1;
        }
    }

    tc_symbol_table_free(&visible);
    tc_block_path_free(&ctx.block_path);
    free(last_init);
    free(init_states);
    return 0;
}
