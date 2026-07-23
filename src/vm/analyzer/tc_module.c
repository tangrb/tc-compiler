/*
 * tc_module.c — 模块结构检查、导入解析、DAG 与签名收集
 */
#include "tc_module.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_scope.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tc_module_search_paths_init(TcModuleSearchPaths *paths) {
    paths->paths = NULL;
    paths->count = 0;
}

void tc_module_search_paths_free(TcModuleSearchPaths *paths) {
    size_t i = 0;
    if (!paths) {
        return;
    }
    for (i = 0; i < paths->count; i++) {
        free(paths->paths[i]);
    }
    free(paths->paths);
    paths->paths = NULL;
    paths->count = 0;
}

int tc_module_search_paths_set(TcModuleSearchPaths *paths, char *const *items, size_t count,
                               TcDiagnostic *diag) {
    size_t i = 0;
    char **copy;

    tc_module_search_paths_free(paths);
    if (count == 0) {
        return 0;
    }
    copy = (char **)calloc(count, sizeof(char *));
    if (!copy) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    for (i = 0; i < count; i++) {
        copy[i] = strdup(items[i] ? items[i] : "");
        if (!copy[i]) {
            while (i > 0) {
                i--;
                free(copy[i]);
            }
            free(copy);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
    }
    paths->paths = copy;
    paths->count = count;
    return 0;
}

void tc_func_signature_list_init(TcFuncSignatureList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void tc_func_signature_list_free(TcFuncSignatureList *list) {
    size_t i = 0;
    size_t j = 0;
    if (!list) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].return_struct_name);
        tc_type_free(&list->items[i].return_type);
        for (j = 0; j < list->items[i].param_count; j++) {
            free(list->items[i].params[j].name);
            free(list->items[i].params[j].struct_type_name);
            tc_type_free(&list->items[i].params[j].type);
        }
        free(list->items[i].params);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

typedef enum {
    TC_LAYER_IMPORT = 0,
    TC_LAYER_STRUCT,
    TC_LAYER_VALUE,
    TC_LAYER_FUNC_OR_EXEC
} TcModuleLayer;

static TcModuleLayer tc_stmt_layer(const TcStatement *stmt) {
    switch (stmt->kind) {
    case TC_STMT_IMPORT:
        return TC_LAYER_IMPORT;
    case TC_STMT_STRUCT_DEF:
        return TC_LAYER_STRUCT;
    case TC_STMT_VAR_DEF:
    case TC_STMT_CONST_DEF:
    case TC_STMT_STATIC_VAR_DEF:
    case TC_STMT_STATIC_LET_DEF:
        return TC_LAYER_VALUE;
    default:
        return TC_LAYER_FUNC_OR_EXEC;
    }
}

static int tc_stmt_line(const TcStatement *stmt) {
    switch (stmt->kind) {
    case TC_STMT_IMPORT:
        return stmt->u.import_stmt.line;
    case TC_STMT_STRUCT_DEF:
        return stmt->u.struct_def.line;
    case TC_STMT_VAR_DEF:
        return stmt->u.var_def.line;
    case TC_STMT_CONST_DEF:
        return stmt->u.const_def.line;
    case TC_STMT_STATIC_VAR_DEF:
        return stmt->u.static_var_def.line;
    case TC_STMT_STATIC_LET_DEF:
        return stmt->u.static_let_def.line;
    case TC_STMT_FUNC_DEF:
        return stmt->u.func_def.line;
    default:
        return 1;
    }
}

int tc_module_check_structure(const TcProgram *program, TcDiagnostic *diag) {
    size_t i = 0;
    TcModuleLayer max_layer = TC_LAYER_IMPORT;

    if (!program || program->mode == TC_MODULE_UNSET) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 1, TC_COLUMN_UNKNOWN,
                          "expected #program or #lib");
        return -1;
    }

    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        TcModuleLayer layer = tc_stmt_layer(stmt);
        TcModuleLayer norm_layer = layer;
        TcModuleLayer norm_max = max_layer;
        int line = tc_stmt_line(stmt);

        /* #program：值声明与可执行语句同层，允许交错 */
        if (program->mode == TC_MODULE_PROGRAM) {
            if (norm_layer == TC_LAYER_FUNC_OR_EXEC) {
                norm_layer = TC_LAYER_VALUE;
            }
            if (norm_max == TC_LAYER_FUNC_OR_EXEC) {
                norm_max = TC_LAYER_VALUE;
            }
        }

        if (norm_layer < norm_max) {
            tc_diagnostic_set(diag, TC_ERR_MODULE_LAYER, line, TC_COLUMN_UNKNOWN,
                              "module layer order violated");
            return -1;
        }
        max_layer = layer;

        if (program->mode == TC_MODULE_PROGRAM) {
            if (stmt->kind == TC_STMT_FUNC_DEF || stmt->kind == TC_STMT_STATIC_VAR_DEF ||
                stmt->kind == TC_STMT_STATIC_LET_DEF) {
                tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, line, TC_COLUMN_UNKNOWN,
                                  "construct not allowed in #program");
                return -1;
            }
            if (stmt->kind == TC_STMT_STRUCT_DEF &&
                stmt->u.struct_def.visibility != TC_VIS_NONE) {
                tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, line, TC_COLUMN_UNKNOWN,
                                  "visibility not allowed in #program");
                return -1;
            }
        } else {
            if (stmt->kind == TC_STMT_STRUCT_DEF &&
                stmt->u.struct_def.visibility == TC_VIS_NONE) {
                tc_diagnostic_set(diag, TC_ERR_MISSING_VISIBILITY, line, TC_COLUMN_UNKNOWN,
                                  "library member requires public or private");
                return -1;
            }
            if (stmt->kind == TC_STMT_FUNC_DEF &&
                stmt->u.func_def.visibility == TC_VIS_NONE) {
                tc_diagnostic_set(diag, TC_ERR_MISSING_VISIBILITY, line, TC_COLUMN_UNKNOWN,
                                  "library member requires public or private");
                return -1;
            }
            if (stmt->kind == TC_STMT_STATIC_VAR_DEF &&
                stmt->u.static_var_def.visibility == TC_VIS_NONE) {
                tc_diagnostic_set(diag, TC_ERR_MISSING_VISIBILITY, line, TC_COLUMN_UNKNOWN,
                                  "library member requires public or private");
                return -1;
            }
            if (stmt->kind == TC_STMT_STATIC_LET_DEF &&
                stmt->u.static_let_def.visibility == TC_VIS_NONE) {
                tc_diagnostic_set(diag, TC_ERR_MISSING_VISIBILITY, line, TC_COLUMN_UNKNOWN,
                                  "library member requires public or private");
                return -1;
            }
            if (stmt->kind == TC_STMT_VAR_DEF || stmt->kind == TC_STMT_CONST_DEF) {
                tc_diagnostic_set(diag, TC_ERR_MODULE_LAYER, line, TC_COLUMN_UNKNOWN,
                                  "non-static value declaration not allowed in #lib");
                return -1;
            }
        }
    }

    if (tc_scope_check_self_usage(program, diag) != 0) {
        return -1;
    }
    return 0;
}

static char *tc_dirname_dup(const char *path) {
    const char *slash;
    size_t len;
    char *out;

    if (!path || !path[0]) {
        return strdup(".");
    }
    slash = strrchr(path, '/');
    if (!slash) {
        return strdup(".");
    }
    len = (size_t)(slash - path);
    if (len == 0) {
        return strdup("/");
    }
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char *tc_module_stem(const char *path) {
    const char *base;
    const char *dot;
    size_t len;
    char *out;

    if (!path) {
        return NULL;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

static int tc_read_file_text(const char *path, char **out_text, TcDiagnostic *diag) {
    FILE *fp;
    long size;
    char *buf;
    size_t nread;

    *out_text = NULL;
    fp = fopen(path, "rb");
    if (!fp) {
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "failed to read module file");
        return -1;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    nread = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[nread] = '\0';
    *out_text = buf;
    return 0;
}

static int tc_join_module_path(const char *dir, const char *name, char **out,
                               TcDiagnostic *diag) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    char *path = (char *)malloc(dlen + 1 + nlen + 4);
    if (!path) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    if (dlen > 0 && dir[dlen - 1] == '/') {
        sprintf(path, "%s%s.tc", dir, name);
    } else {
        sprintf(path, "%s/%s.tc", dir, name);
    }
    *out = path;
    return 0;
}

static int tc_file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int tc_locate_module_file(const char *name, const char *entry_dir,
                                 const TcModuleSearchPaths *search, char **out_path,
                                 TcDiagnostic *diag) {
    size_t i = 0;
    char *first = NULL;
    int hits = 0;

    *out_path = NULL;

    {
        char *candidate = NULL;
        if (tc_join_module_path(entry_dir ? entry_dir : ".", name, &candidate, diag) != 0) {
            return -1;
        }
        if (tc_file_exists(candidate)) {
            first = candidate;
            hits = 1;
        } else {
            free(candidate);
        }
    }

    if (search) {
        for (i = 0; i < search->count; i++) {
            char *candidate = NULL;
            if (tc_join_module_path(search->paths[i], name, &candidate, diag) != 0) {
                free(first);
                return -1;
            }
            if (tc_file_exists(candidate)) {
                if (!first) {
                    first = candidate;
                    hits = 1;
                } else if (strcmp(first, candidate) != 0) {
                    hits++;
                    free(candidate);
                } else {
                    free(candidate);
                }
            } else {
                free(candidate);
            }
        }
    }

    if (hits == 0) {
        tc_diagnostic_set(diag, TC_ERR_IMPORT_NOT_FOUND, 1, TC_COLUMN_UNKNOWN,
                          "import module not found");
        return -1;
    }
    if (hits > 1) {
        free(first);
        tc_diagnostic_set(diag, TC_ERR_IMPORT_AMBIGUOUS, 1, TC_COLUMN_UNKNOWN,
                          "import module path is ambiguous");
        return -1;
    }
    *out_path = first;
    return 0;
}

static int tc_load_lib_file(const char *path, const char *expected_name, TcProgram *out,
                            TcDiagnostic *diag) {
    char *text = NULL;
    int rc;

    rc = tc_read_file_text(path, &text, diag);
    if (rc != 0) {
        if (rc > 0) {
            tc_diagnostic_set(diag, TC_ERR_IMPORT_NOT_FOUND, 1, TC_COLUMN_UNKNOWN,
                              "import module not found");
        }
        return -1;
    }
    rc = tc_parse_source_to_program(text, out, diag);
    free(text);
    if (rc != 0) {
        return -1;
    }
    if (out->mode != TC_MODULE_LIB) {
        tc_program_free(out);
        tc_diagnostic_set(diag, TC_ERR_IMPORT_NOT_LIB, 1, TC_COLUMN_UNKNOWN,
                          "imported module is not #lib");
        return -1;
    }
    free(out->source_path);
    out->source_path = strdup(path);
    free(out->module_name);
    out->module_name = expected_name ? strdup(expected_name) : tc_module_stem(path);
    if (!out->source_path || !out->module_name) {
        tc_program_free(out);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    if (tc_module_check_structure(out, diag) != 0) {
        tc_program_free(out);
        return -1;
    }
    return 0;
}

static int tc_typed_push_dep(TcTypedProgram *out, TcProgram *mod, TcDiagnostic *diag) {
    if (out->dep_count == out->dep_capacity) {
        size_t new_cap = out->dep_capacity == 0 ? 4 : out->dep_capacity * 2;
        TcProgram *deps = (TcProgram *)realloc(out->deps, new_cap * sizeof(TcProgram));
        if (!deps) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        out->deps = deps;
        out->dep_capacity = new_cap;
    }
    out->deps[out->dep_count++] = *mod;
    memset(mod, 0, sizeof(*mod));
    return 0;
}

typedef struct {
    int *deps;
    size_t count;
    size_t capacity;
    int color;
} TcDagNode;

static int tc_dag_add_edge(TcDagNode *node, int dep, TcDiagnostic *diag) {
    size_t i = 0;
    int *deps;
    for (i = 0; i < node->count; i++) {
        if (node->deps[i] == dep) {
            return 0;
        }
    }
    if (node->count == node->capacity) {
        size_t new_cap = node->capacity == 0 ? 4 : node->capacity * 2;
        deps = (int *)realloc(node->deps, new_cap * sizeof(int));
        if (!deps) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        node->deps = deps;
        node->capacity = new_cap;
    }
    node->deps[node->count++] = dep;
    return 0;
}

static int tc_dag_dfs(TcDagNode *nodes, size_t n, int idx, TcDiagnostic *diag) {
    size_t i = 0;
    nodes[idx].color = 1;
    for (i = 0; i < nodes[idx].count; i++) {
        int dep = nodes[idx].deps[i];
        if (dep < 0 || (size_t)dep >= n) {
            continue;
        }
        if (nodes[dep].color == 1) {
            tc_diagnostic_set(diag, TC_ERR_CIRCULAR_IMPORT, 1, TC_COLUMN_UNKNOWN,
                              "circular import");
            return -1;
        }
        if (nodes[dep].color == 0 && tc_dag_dfs(nodes, n, dep, diag) != 0) {
            return -1;
        }
    }
    nodes[idx].color = 2;
    return 0;
}

static int tc_find_dep_index(const TcTypedProgram *out, const char *name) {
    size_t i = 0;
    for (i = 0; i < out->dep_count; i++) {
        if (out->deps[i].module_name && strcmp(out->deps[i].module_name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int tc_collect_imports_recursive(TcTypedProgram *out, TcProgram *current,
                                        const char *entry_dir, const TcModuleSearchPaths *search,
                                        TcDiagnostic *diag) {
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < current->count; i++) {
        const TcStatement *stmt = &current->items[i];
        char *path = NULL;
        TcProgram loaded;
        const char *mod_name;

        if (stmt->kind != TC_STMT_IMPORT) {
            continue;
        }
        mod_name = stmt->u.import_stmt.module_name;

        /* duplicate import in same file */
        for (j = 0; j < i; j++) {
            if (current->items[j].kind == TC_STMT_IMPORT &&
                strcmp(current->items[j].u.import_stmt.module_name, mod_name) == 0) {
                tc_diagnostic_set(diag, TC_ERR_DUPLICATE_IMPORT, stmt->u.import_stmt.line,
                                  TC_COLUMN_UNKNOWN, "duplicate import");
                return -1;
            }
        }

        /* self-import */
        if (current->module_name && strcmp(current->module_name, mod_name) == 0) {
            tc_diagnostic_set(diag, TC_ERR_CIRCULAR_IMPORT, stmt->u.import_stmt.line,
                              TC_COLUMN_UNKNOWN, "circular import");
            return -1;
        }

        if (tc_find_dep_index(out, mod_name) >= 0) {
            continue;
        }

        if (tc_locate_module_file(mod_name, entry_dir, search, &path, diag) != 0) {
            return -1;
        }
        tc_program_init(&loaded);
        if (tc_load_lib_file(path, mod_name, &loaded, diag) != 0) {
            free(path);
            return -1;
        }
        free(path);
        if (tc_typed_push_dep(out, &loaded, diag) != 0) {
            tc_program_free(&loaded);
            return -1;
        }
        /* recurse into newly loaded module */
        if (tc_collect_imports_recursive(out, &out->deps[out->dep_count - 1], entry_dir, search,
                                         diag) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tc_build_and_check_dag(TcTypedProgram *out, TcDiagnostic *diag) {
    size_t n = out->dep_count;
    size_t i = 0;
    size_t j = 0;
    TcDagNode *nodes;
    int rc = 0;

    if (n == 0) {
        return 0;
    }
    nodes = (TcDagNode *)calloc(n, sizeof(TcDagNode));
    if (!nodes) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    for (i = 0; i < n; i++) {
        TcProgram *mod = &out->deps[i];
        for (j = 0; j < mod->count; j++) {
            int dep_idx;
            if (mod->items[j].kind != TC_STMT_IMPORT) {
                continue;
            }
            dep_idx = tc_find_dep_index(out, mod->items[j].u.import_stmt.module_name);
            if (dep_idx < 0) {
                continue;
            }
            if (tc_dag_add_edge(&nodes[i], dep_idx, diag) != 0) {
                rc = -1;
                goto done;
            }
        }
    }

    for (i = 0; i < n; i++) {
        if (nodes[i].color == 0 && tc_dag_dfs(nodes, n, (int)i, diag) != 0) {
            rc = -1;
            goto done;
        }
    }

done:
    for (i = 0; i < n; i++) {
        free(nodes[i].deps);
    }
    free(nodes);
    return rc;
}

int tc_module_resolve_imports(TcTypedProgram *out, const char *entry_path,
                              const TcModuleSearchPaths *search, TcDiagnostic *diag) {
    char *entry_dir = NULL;
    int rc;

    if (!out) {
        return -1;
    }
    if (tc_module_check_structure(&out->program, diag) != 0) {
        return -1;
    }

    entry_dir = tc_dirname_dup(entry_path);
    if (!entry_dir) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    if (entry_path) {
        free(out->program.source_path);
        out->program.source_path = strdup(entry_path);
        if (!out->program.module_name) {
            out->program.module_name = tc_module_stem(entry_path);
        }
        if (!out->program.source_path || !out->program.module_name) {
            free(entry_dir);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
    }

    rc = tc_collect_imports_recursive(out, &out->program, entry_dir, search, diag);
    free(entry_dir);
    if (rc != 0) {
        return -1;
    }
    return tc_build_and_check_dag(out, diag);
}

static int tc_sig_push_from_func(TcFuncSignatureList *out, const TcFuncDef *fn, int module_index,
                                 TcDiagnostic *diag) {
    TcFuncSignature *items;
    TcFuncSignature sig;
    size_t i = 0;

    memset(&sig, 0, sizeof(sig));
    sig.name = strdup(fn->name);
    sig.visibility = fn->visibility;
    /* Phase 2：签名表保留 kind / struct_id；嵌套 ptr/memblock 深度克隆留后续阶段 */
    sig.return_type = tc_type_scalar(fn->return_type.kind);
    if (fn->return_type.kind == TC_STRUCT) {
        sig.return_type = tc_type_make_struct(fn->return_type.params.struct_type.struct_id);
    }
    if (fn->return_struct_name) {
        sig.return_struct_name = strdup(fn->return_struct_name);
    }
    sig.param_count = fn->param_count;
    sig.module_index = module_index;
    sig.func_id = fn->func_id;
    sig.def_line = fn->line;
    if (!sig.name) {
        free(sig.return_struct_name);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    if (fn->param_count > 0) {
        sig.params = (TcFuncParam *)calloc(fn->param_count, sizeof(TcFuncParam));
        if (!sig.params) {
            free(sig.name);
            free(sig.return_struct_name);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < fn->param_count; i++) {
            sig.params[i].name = strdup(fn->params[i].name);
            sig.params[i].type = tc_type_scalar(fn->params[i].type.kind);
            if (fn->params[i].type.kind == TC_STRUCT) {
                sig.params[i].type =
                    tc_type_make_struct(fn->params[i].type.params.struct_type.struct_id);
            }
            if (fn->params[i].struct_type_name) {
                sig.params[i].struct_type_name = strdup(fn->params[i].struct_type_name);
            }
            if (!sig.params[i].name) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
        }
    }

    if (out->count == out->capacity) {
        size_t new_cap = out->capacity == 0 ? 8 : out->capacity * 2;
        items = (TcFuncSignature *)realloc(out->items, new_cap * sizeof(TcFuncSignature));
        if (!items) {
            free(sig.name);
            free(sig.return_struct_name);
            for (i = 0; i < sig.param_count; i++) {
                free(sig.params[i].name);
                free(sig.params[i].struct_type_name);
            }
            free(sig.params);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        out->items = items;
        out->capacity = new_cap;
    }
    out->items[out->count++] = sig;
    return 0;
}

static int tc_collect_from_program(const TcProgram *prog, int module_index,
                                   TcFuncSignatureList *out, TcDiagnostic *diag) {
    size_t i = 0;
    for (i = 0; i < prog->count; i++) {
        if (prog->items[i].kind == TC_STMT_FUNC_DEF) {
            if (tc_sig_push_from_func(out, &prog->items[i].u.func_def, module_index, diag) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int tc_module_collect_signatures(const TcTypedProgram *prog, TcFuncSignatureList *out,
                                 TcDiagnostic *diag) {
    size_t i = 0;
    tc_func_signature_list_init(out);
    if (!prog) {
        return 0;
    }
    /* 仅 #lib 模块贡献签名；#program 无 func */
    for (i = 0; i < prog->dep_count; i++) {
        if (prog->deps[i].mode == TC_MODULE_LIB) {
            if (tc_collect_from_program(&prog->deps[i], (int)i, out, diag) != 0) {
                tc_func_signature_list_free(out);
                return -1;
            }
        }
    }
    if (prog->program.mode == TC_MODULE_LIB) {
        if (tc_collect_from_program(&prog->program, -1, out, diag) != 0) {
            tc_func_signature_list_free(out);
            return -1;
        }
    }
    (void)diag;
    return 0;
}
