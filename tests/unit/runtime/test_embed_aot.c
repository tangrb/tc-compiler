/*
 * test_embed_aot.c — TC-Embed AOT 模式单元测试
 *
 * 测试策略：
 *   1. 通过 libtc 编译 TC 源码
 *   2. AOT codegen 生成嵌入模式 C 代码到临时文件
 *   3. 用 host cc 编译成共享库 (.dylib/.so)
 *   4. 用 dlopen/dlsym 加载函数表，通过 tc_embed_create_aot 创建上下文
 *   5. 调用 AOT 函数并与 VM 模式结果对比
 *
 * 跳过条件：未找到 host cc 或 dlopen 失败则跳过。
 */
#include "tc_embed.h"
#include "tc_value_bridge.h"
#include "tc_lib.h"
#include "tc_aot_codegen.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_passed = 0;
static int g_failed = 0;

static void check(int condition, const char *message) {
    if (condition) {
        g_passed++;
    } else {
        g_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

/* ── 辅助：编译 #lib TC 源码 ── */
static int compile_lib(const char *source, const char *name,
                        TcTypedProgram *out, TcDiagnostic *diag) {
    char full[2048];
    (void)snprintf(full, sizeof(full), "#lib\n%s", source);
    return tc_compile_source(full, name, out, diag);
}

/*
 * 辅助：生成 AOT 嵌入模式 C 代码并编译为共享库。
 * 返回 .so 路径（调用方 free + unlink），写入 slot_count。
 * 失败返回 NULL。
 */
static char *build_aot_embed_lib(const TcTypedProgram *program,
                                  size_t *slot_count) {
    char c_template[] = "/tmp/tc_aot_embed_XXXXXX.c";
    int c_fd = -1;
    char *c_path = NULL;
    char *so_path = NULL;
    FILE *c_file = NULL;
    char cmd[8192];

    c_fd = mkstemps(c_template, 2);  /* 2 = ".c" suffix length */
    if (c_fd < 0) return NULL;
    c_path = strdup(c_template);
    if (!c_path) { close(c_fd); return NULL; }
    c_file = fdopen(c_fd, "w");
    if (!c_file) { close(c_fd); unlink(c_path); free(c_path); return NULL; }

    if (tc_aot_emit_c(c_file, program, "test.tc", 1) != 0) {
        fclose(c_file);
        unlink(c_path);
        free(c_path);
        return NULL;
    }
    fclose(c_file);

    /* 生成 .so 路径 */
    {
        size_t len = strlen(c_path);
        so_path = (char *)malloc(len + 1);
        if (!so_path) { unlink(c_path); free(c_path); return NULL; }
        memcpy(so_path, c_path, len - 1);  /* 去掉 .c */
        so_path[len - 1] = 's';
        so_path[len] = 'o';
        so_path[len + 1] = '\0';
    }

    /* 编译为共享库 */
    {
        (void)snprintf(cmd, sizeof(cmd),
            "cc -std=c99 -Wall -Werror -fPIC -shared "
            "-I\"%s/src/aot\" -I\"%s/src/vm/runtime\" -I\"%s/src/vm/embed\" "
            "\"%s\" \"%s/src/aot/tc_aot_rt.c\" "
            "\"%s/src/vm/runtime/tc_types.c\" "
            "\"%s/src/vm/runtime/tc_diagnostic.c\" "
            "\"%s/src/vm/runtime/tc_semantics.c\" "
            "\"%s/src/vm/runtime/tc_sem_int.c\" "
            "\"%s/src/vm/runtime/tc_sem_fp.c\" "
            "\"%s/src/vm/runtime/tc_sem_cast.c\" "
            "\"%s/src/vm/runtime/tc_sem_bitwise.c\" "
            "\"%s/src/vm/runtime/tc_io.c\" "
            "-o \"%s\" 2>&1",
            TC_SRC_DIR, TC_SRC_DIR, TC_SRC_DIR,
            c_path,
            TC_SRC_DIR, TC_SRC_DIR, TC_SRC_DIR,
            TC_SRC_DIR, TC_SRC_DIR, TC_SRC_DIR,
            TC_SRC_DIR, TC_SRC_DIR, TC_SRC_DIR,
            so_path);

        {
            char result[4096];
            FILE *fp = popen(cmd, "r");
            int status = 0;
            if (!fp) {
                unlink(c_path); free(c_path); free(so_path);
                return NULL;
            }
            result[0] = '\0';
            if (fread(result, 1, sizeof(result) - 1, fp) > 0) {
                result[sizeof(result) - 1] = '\0';
            }
            status = pclose(fp);
            if (status != 0) {
                fprintf(stderr, "cc compile error:\n%s\n", result);
                unlink(c_path);
                unlink(so_path);
                free(c_path);
                free(so_path);
                return NULL;
            }
        }
    }

    *slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);
    unlink(c_path);
    free(c_path);
    return so_path;
}

/*
 * 从 AOT 生成的共享库中加载函数表并创建 embed 上下文。
 */
static TcEmbedCtx *create_aot_embed_ctx(const TcTypedProgram *program,
                                         const char *so_path,
                                         size_t slot_count) {
    void *dl_handle = NULL;
    uint64_t *slots = NULL;
    const tc_aot_func_entry *func_table = NULL;
    TcEmbedCtx *ctx = NULL;
    TcDiagnostic diag;
    int (*init_fn)(TcDiagnostic *) = NULL;

    dl_handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!dl_handle) return NULL;

    slots = (uint64_t *)dlsym(dl_handle, "slots");
    if (!slots) { dlclose(dl_handle); return NULL; }

    func_table = (const tc_aot_func_entry *)dlsym(dl_handle, "tc_aot_func_table");
    if (!func_table) { dlclose(dl_handle); return NULL; }

    init_fn = (int (*)(TcDiagnostic *))dlsym(dl_handle, "tc_aot_init");
    if (!init_fn) { dlclose(dl_handle); return NULL; }

    tc_diagnostic_init(&diag);
    ctx = tc_embed_create_aot(slots, slot_count, func_table, init_fn, program, &diag);
    tc_diagnostic_clear(&diag);

    /* dl_handle 保持打开 — 生命周期由 ctx 覆盖 */
    (void)dl_handle;
    return ctx;
}

/* ================================================================ */
/*  测试用例                                                            */
/* ================================================================ */

static void test_aot_embed_scalar_call(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue vm_args[2], aot_args[2];
    TcValue vm_result, aot_result;
    int64_t vm_val = 0, aot_val = 0;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func plus(a: int32, b: int32) int32 then\n"
            "    var sum: int32 = add(int32, a, b)\n"
            "    return sum\n"
            "end\n", "math", &prog, &diag) == 0,
          "aot embed: compile plus lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot embed: vm ctx create");
    if (!vm_ctx) goto cleanup;

    vm_args[0] = tc_value_from_int32(10);
    vm_args[1] = tc_value_from_int32(32);
    check(tc_embed_call(vm_ctx, NULL, "plus", 2, vm_args, &vm_result) == 0,
          "aot embed: vm call plus");
    tc_value_to_int64(vm_result, &vm_val);
    check(vm_val == 42, "aot embed: vm plus(10, 32) == 42");

    /* 构建 AOT 嵌入库 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed: build shared lib");
    if (!so_path) goto cleanup;

    /* AOT 模式 */
    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed: aot ctx create");
    if (!aot_ctx) goto cleanup;

    aot_args[0] = tc_value_from_int32(10);
    aot_args[1] = tc_value_from_int32(32);
    check(tc_embed_call(aot_ctx, NULL, "plus", 2, aot_args, &aot_result) == 0,
          "aot embed: aot call plus");
    tc_value_to_int64(aot_result, &aot_val);
    check(aot_val == 42, "aot embed: aot plus(10, 32) == 42");

    /* 差分对比 */
    check(aot_val == vm_val, "aot embed: VM vs AOT result match");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

static void test_aot_embed_static_var(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue vm_result, aot_result;
    int64_t vm_val, aot_val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public static var value: int32 = 42\n"
            "public func get_value() int32 then\n"
            "    var v: int32 = Self.value\n"
            "    return v\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot embed static: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot embed static: vm ctx create");
    if (!vm_ctx) goto cleanup;

    check(tc_embed_call(vm_ctx, NULL, "get_value", 0, NULL, &vm_result) == 0,
          "aot embed static: vm call 1");
    tc_value_to_int64(vm_result, &vm_val);
    check(vm_val == 42, "aot embed static: vm value == 42");

    check(tc_embed_call(vm_ctx, NULL, "get_value", 0, NULL, &vm_result) == 0,
          "aot embed static: vm call 2");
    tc_value_to_int64(vm_result, &vm_val);
    check(vm_val == 42, "aot embed static: vm value still 42 after 2nd call");

    /* 构建 AOT 嵌入库 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed static: build shared lib");
    if (!so_path) goto cleanup;

    /* AOT 模式 */
    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed static: aot ctx create");
    if (!aot_ctx) goto cleanup;

    check(tc_embed_call(aot_ctx, NULL, "get_value", 0, NULL, &aot_result) == 0,
          "aot embed static: aot call 1");
    tc_value_to_int64(aot_result, &aot_val);
    check(aot_val == 42, "aot embed static: aot value == 42");

    check(tc_embed_call(aot_ctx, NULL, "get_value", 0, NULL, &aot_result) == 0,
          "aot embed static: aot call 2");
    tc_value_to_int64(aot_result, &aot_val);
    check(aot_val == 42, "aot embed static: aot value still 42");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

static void test_aot_embed_slot_rw(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue r;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
            "#program\nvar x: int32 = 0\n", "<test>", &prog, &diag) == 0,
          "aot embed slot: compile program");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式：验证 slot 读写 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot embed slot: vm ctx create");
    if (!vm_ctx) goto cleanup;

    check(tc_embed_slot_write(vm_ctx, 0, tc_value_from_int32(99)) == 0,
          "aot embed slot: vm write slot 0");
    check(tc_embed_slot_read(vm_ctx, 0, &r) == 0,
          "aot embed slot: vm read slot 0");
    check(r.bits == 99, "aot embed slot: vm slot 0 == 99");

    /* AOT 模式 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed slot: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed slot: aot ctx create");
    if (!aot_ctx) goto cleanup;

    check(tc_embed_slot_write(aot_ctx, 0, tc_value_from_int32(42)) == 0,
          "aot embed slot: aot write slot 0");
    check(tc_embed_slot_read(aot_ctx, 0, &r) == 0,
          "aot embed slot: aot read slot 0");
    check(r.bits == 42, "aot embed slot: aot slot 0 == 42");

    check(tc_embed_slot_write(aot_ctx, 999, tc_value_from_int32(0)) != 0,
          "aot embed slot: aot out-of-range write rejected");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：运行时错误（除零）不终止进程 ── */
static void test_aot_embed_error(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue args[2];

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func div_bad(a: int32, b: int32) int32 then\n"
            "    var result: int32 = div(int32, a, b)\n"
            "    return result\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot embed error: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* 构建 AOT 嵌入库 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed error: build shared lib");
    if (!so_path) goto cleanup;

    /* AOT 模式：调用 div_bad(10, 0) */
    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed error: aot ctx create");
    if (!aot_ctx) goto cleanup;

    args[0] = tc_value_from_int32(10);
    args[1] = tc_value_from_int32(0);
    check(tc_embed_call(aot_ctx, NULL, "div_bad", 2, args, NULL) != 0,
          "aot embed error: div by zero returns -1");
    check(tc_embed_had_error(aot_ctx) != 0,
          "aot embed error: error flag set");
    check(strstr(tc_embed_get_error(aot_ctx), "division by zero") != NULL,
          "aot embed error: message contains 'division by zero'");

    /* 验证后续正常调用仍可工作 */
    args[1] = tc_value_from_int32(2);
    {
        TcValue result;
        int64_t val;
        check(tc_embed_call(aot_ctx, NULL, "div_bad", 2, args, &result) == 0,
              "aot embed error: div_bad(10, 2) works after error");
        tc_value_to_int64(result, &val);
        check(val == 5, "aot embed error: 10/2 == 5");
    }

cleanup:
    tc_embed_destroy(aot_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：ptr 数组遍历（常量偏移，与 VM test_embed_ptr_load_sum 等效） ── */
static void test_aot_embed_call_ptr_array(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    int data_base = 0;
    int i = 0;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func sum_five(data: ptr<int32>) int32 then\n"
            "    var a: int32 = ptr_load(int32, data)\n"
            "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
            "    var b: int32 = ptr_load(int32, p1)\n"
            "    var p2: ptr<int32> = ptr_add(int32, data, 2)\n"
            "    var c: int32 = ptr_load(int32, p2)\n"
            "    var p3: ptr<int32> = ptr_add(int32, data, 3)\n"
            "    var d: int32 = ptr_load(int32, p3)\n"
            "    var p4: ptr<int32> = ptr_add(int32, data, 4)\n"
            "    var e: int32 = ptr_load(int32, p4)\n"
            "    var s1: int32 = add(int32, a, b)\n"
            "    var s2: int32 = add(int32, c, d)\n"
            "    var s3: int32 = add(int32, s1, s2)\n"
            "    var total: int32 = add(int32, s3, e)\n"
            "    return total\n"
            "end\n", "stats", &prog, &diag) == 0,
          "aot embed ptr array: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* ── VM 模式 ── */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot embed ptr array: vm ctx create");
    if (!vm_ctx) goto cleanup;

    {
        const TcEmbedFuncInfo *info = tc_embed_func_info(vm_ctx, NULL, "sum_five");
        TcValue args[1];
        TcValue result;
        int64_t val;

        check(info != NULL, "aot embed ptr array: vm find sum_five");
        check(info->param_count == 1, "aot embed ptr array: sum_five has 1 param");
        slot_count = tc_embed_slot_count(vm_ctx);
        data_base = (int)slot_count - 5;  /* 5 data slots */

        /* 写入 5 个 int32 数据 */
        for (i = 0; i < 5; i++) {
            tc_embed_slot_write(vm_ctx, data_base + i,
                                tc_value_from_int32(i + 1));
        }

        args[0] = tc_embed_ptr_encode(data_base);
        check(tc_embed_call(vm_ctx, NULL, "sum_five", 1, args, &result) == 0,
              "aot embed ptr array: vm sum_five 1..5");
        tc_value_to_int64(result, &val);
        check(val == 15, "aot embed ptr array: vm sum_five(1..5) == 15");
    }

    /* ── AOT 模式 ── */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed ptr array: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed ptr array: aot ctx create");
    if (!aot_ctx) goto cleanup;

    {
        const TcEmbedFuncInfo *info = tc_embed_func_info(aot_ctx, NULL, "sum_five");
        TcValue args[1];
        TcValue result;
        int64_t val;

        check(info != NULL, "aot embed ptr array: aot find sum_five");
        check(info->param_count == 1, "aot embed ptr array: sum_five has 1 param");
        data_base = (int)tc_embed_slot_count(aot_ctx) - 5;

        for (i = 0; i < 5; i++) {
            tc_embed_slot_write(aot_ctx, data_base + i,
                                tc_value_from_int32(i + 1));
        }

        args[0] = tc_embed_ptr_encode(data_base);
        check(tc_embed_call(aot_ctx, NULL, "sum_five", 1, args, &result) == 0,
              "aot embed ptr array: aot sum_five 1..5");
        tc_value_to_int64(result, &val);
        check(val == 15, "aot embed ptr array: aot sum_five(1..5) == 15");
    }

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：嵌套 funcall ── */
static void test_aot_embed_call_nested(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "private func inner(x: int32) int32 then\n"
            "    var r: int32 = add(int32, x, 1)\n"
            "    return r\n"
            "end\n"
            "public func outer(x: int32) int32 then\n"
            "    var r: int32 = funcall(Self.inner, x: x)\n"
            "    return r\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot embed nested: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot embed nested: vm ctx create");
    if (!vm_ctx) goto cleanup;

    {
        TcValue args[1];
        TcValue result;
        int64_t val;
        args[0] = tc_value_from_int32(41);
        check(tc_embed_call(vm_ctx, NULL, "outer", 1, args, &result) == 0,
              "aot embed nested: vm outer(41)");
        tc_value_to_int64(result, &val);
        check(val == 42, "aot embed nested: vm outer(41) == 42");
    }

    /* 构建 AOT 嵌入库 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed nested: build shared lib");
    if (!so_path) goto cleanup;

    /* AOT 模式 */
    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot embed nested: aot ctx create");
    if (!aot_ctx) goto cleanup;

    {
        TcValue args[1];
        TcValue result;
        int64_t val;
        args[0] = tc_value_from_int32(41);
        check(tc_embed_call(aot_ctx, NULL, "outer", 1, args, &result) == 0,
              "aot embed nested: aot outer(41)");
        tc_value_to_int64(result, &val);
        check(val == 42, "aot embed nested: aot outer(41) == 42");
    }

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：tc_aot_init 幂等性 + 多次创建/销毁 ── */
static void test_aot_embed_init_cleanup(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx_a = NULL;
    TcEmbedCtx *ctx_b = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    int static_slot = -1;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public static var counter: int32 = 42\n"
            "public func get_counter() int32 then\n"
            "    var v: int32 = Self.counter\n"
            "    return v\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot embed init cleanup: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* 构建 AOT 嵌入库 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot embed init cleanup: build shared lib");
    if (!so_path) goto cleanup;

    /* ── 第一次创建 ── */
    ctx_a = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(ctx_a != NULL, "aot embed init cleanup: ctx_a create");
    if (!ctx_a) goto cleanup;

    {
        TcValue result;
        int64_t val;

        check(tc_embed_call(ctx_a, NULL, "get_counter", 0, NULL, &result) == 0,
              "aot embed init cleanup: first call");
        tc_value_to_int64(result, &val);
        check(val == 42, "aot embed init cleanup: counter == 42 after init");
    }

    /* 找到 static var slot，写入新值 */
    static_slot = tc_embed_self_var_slot(ctx_a, "counter");
    check(static_slot >= 0, "aot embed init cleanup: find counter slot");
    if (static_slot >= 0) {
        tc_embed_slot_write(ctx_a, static_slot,
                            tc_value_from_int32(99));

        TcValue result;
        int64_t val;
        check(tc_embed_call(ctx_a, NULL, "get_counter", 0, NULL, &result) == 0,
              "aot embed init cleanup: call after slot write");
        tc_value_to_int64(result, &val);
        check(val == 99, "aot embed init cleanup: counter == 99 after slot write");
    }

    /* ── 销毁 ctx_a ── */
    tc_embed_destroy(ctx_a);
    ctx_a = NULL;

    /* ── 第二次创建 — 验证 static var 被重新初始化 ── */
    ctx_b = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(ctx_b != NULL, "aot embed init cleanup: ctx_b create");
    if (!ctx_b) goto cleanup;

    {
        TcValue result;
        int64_t val;

        check(tc_embed_call(ctx_b, NULL, "get_counter", 0, NULL, &result) == 0,
              "aot embed init cleanup: call after re-init");
        tc_value_to_int64(result, &val);
        check(val == 42, "aot embed init cleanup: counter reset to 42 after re-init");
    }

cleanup:
    tc_embed_destroy(ctx_b);
    tc_embed_destroy(ctx_a);
    if (so_path) {
        unlink(so_path);
        free(so_path);
    }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：float64 VM/AOT 差分 ── */
static void test_aot_embed_float64(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue vm_args[2], aot_args[2];
    TcValue vm_result, aot_result;
    double vm_val, aot_val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func mult(a: float64, b: float64) float64 then\n"
            "    var prod: float64 = mul(float64, a, b)\n"
            "    return prod\n"
            "end\n", "fp", &prog, &diag) == 0,
          "aot float64: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot float64: vm ctx create");
    if (!vm_ctx) goto cleanup;

    vm_args[0] = tc_value_from_double(2.5);
    vm_args[1] = tc_value_from_double(4.0);
    check(tc_embed_call(vm_ctx, NULL, "mult", 2, vm_args, &vm_result) == 0,
          "aot float64: vm call mult");
    tc_value_to_double(vm_result, &vm_val);
    check(vm_val == 10.0, "aot float64: vm mult(2.5, 4.0) == 10.0");

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot float64: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot float64: aot ctx create");
    if (!aot_ctx) goto cleanup;

    aot_args[0] = tc_value_from_double(2.5);
    aot_args[1] = tc_value_from_double(4.0);
    check(tc_embed_call(aot_ctx, NULL, "mult", 2, aot_args, &aot_result) == 0,
          "aot float64: aot call mult");
    tc_value_to_double(aot_result, &aot_val);
    check(aot_val == 10.0, "aot float64: aot mult(2.5, 4.0) == 10.0");
    check(aot_val == vm_val, "aot float64: VM vs AOT match");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：bool VM/AOT 差分 ── */
static void test_aot_embed_bool(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue vm_args[1], aot_args[1];
    TcValue vm_result, aot_result;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func ident_bool(x: bool) bool then\n"
            "    return x\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot bool: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot bool: vm ctx create");
    if (!vm_ctx) goto cleanup;

    vm_args[0] = tc_value_from_bool(1);
    check(tc_embed_call(vm_ctx, NULL, "ident_bool", 1, vm_args, &vm_result) == 0,
          "aot bool: vm ident_bool(true)");
    check(tc_value_to_bool(vm_result) != 0, "aot bool: vm ident_bool(true) == true");

    vm_args[0] = tc_value_from_bool(0);
    check(tc_embed_call(vm_ctx, NULL, "ident_bool", 1, vm_args, &vm_result) == 0,
          "aot bool: vm ident_bool(false)");
    check(tc_value_to_bool(vm_result) == 0, "aot bool: vm ident_bool(false) == false");

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot bool: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot bool: aot ctx create");
    if (!aot_ctx) goto cleanup;

    aot_args[0] = tc_value_from_bool(1);
    check(tc_embed_call(aot_ctx, NULL, "ident_bool", 1, aot_args, &aot_result) == 0,
          "aot bool: aot ident_bool(true)");
    check(tc_value_to_bool(aot_result) != 0, "aot bool: aot ident_bool(true) == true");

    aot_args[0] = tc_value_from_bool(0);
    check(tc_embed_call(aot_ctx, NULL, "ident_bool", 1, aot_args, &aot_result) == 0,
          "aot bool: aot ident_bool(false)");
    check(tc_value_to_bool(aot_result) == 0, "aot bool: aot ident_bool(false) == false");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：无参 void 函数 AOT 调用 ── */
static void test_aot_embed_void_noarg(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func noop() void then\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot void: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot void: vm ctx create");
    if (!vm_ctx) goto cleanup;

    check(tc_embed_call(vm_ctx, NULL, "noop", 0, NULL, NULL) == 0,
          "aot void: vm call noop");
    check(!tc_embed_had_error(vm_ctx), "aot void: vm no error after noop");

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot void: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot void: aot ctx create");
    if (!aot_ctx) goto cleanup;

    check(tc_embed_call(aot_ctx, NULL, "noop", 0, NULL, NULL) == 0,
          "aot void: aot call noop");
    check(!tc_embed_had_error(aot_ctx), "aot void: aot no error after noop");

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：ptr_store 写入后 C 侧读回（AOT 模式 + 差分） ── */
static void test_aot_embed_ptr_store_inplace(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    int data_base;
    int i;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func fill_three(data: ptr<int32>, v: int32) void then\n"
            "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
            "    var p2: ptr<int32> = ptr_add(int32, data, 2)\n"
            "    ptr_store(int32, data, v)\n"
            "    ptr_store(int32, p1, v)\n"
            "    ptr_store(int32, p2, v)\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot ptr store: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot ptr store: vm ctx create");
    if (!vm_ctx) goto cleanup;

    slot_count = tc_embed_slot_count(vm_ctx);
    data_base = (int)slot_count - 4;
    for (i = 0; i < 3; i++) {
        tc_embed_slot_write(vm_ctx, data_base + i,
                            tc_value_from_int32(0));
    }

    {
        TcValue args[2];
        args[0] = tc_embed_ptr_encode(data_base);
        args[1] = tc_value_from_int32(88);
        check(tc_embed_call(vm_ctx, NULL, "fill_three", 2, args, NULL) == 0,
              "aot ptr store: vm call fill_three");
    }
    {
        TcValue r; int64_t v;
        for (i = 0; i < 3; i++) {
            tc_embed_slot_read(vm_ctx, data_base + i, &r);
            tc_value_to_int64(r, &v);
            check(v == 88, "aot ptr store: vm all slots == 88");
        }
    }

    /* AOT 模式 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot ptr store: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot ptr store: aot ctx create");
    if (!aot_ctx) goto cleanup;

    {
        TcValue args[2];
        data_base = (int)tc_embed_slot_count(aot_ctx) - 4;
        for (i = 0; i < 3; i++) {
            tc_embed_slot_write(aot_ctx, data_base + i,
                                tc_value_from_int32(0));
        }
        args[0] = tc_embed_ptr_encode(data_base);
        args[1] = tc_value_from_int32(88);
        check(tc_embed_call(aot_ctx, NULL, "fill_three", 2, args, NULL) == 0,
              "aot ptr store: aot call fill_three");
    }
    {
        TcValue r; int64_t v;
        for (i = 0; i < 3; i++) {
            tc_embed_slot_read(aot_ctx, data_base + i, &r);
            tc_value_to_int64(r, &v);
            check(v == 88, "aot ptr store: aot all slots == 88");
        }
    }

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：static var 递增（C 侧写 slot，AOT 模式 + 差分） ── */
static void test_aot_embed_static_var_increment(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *vm_ctx = NULL;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    int slot, call;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public static var counter: int32 = 0\n"
            "public func get_counter() int32 then\n"
            "    var v: int32 = Self.counter\n"
            "    return v\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot incr: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    /* VM 模式 */
    vm_ctx = tc_embed_create(&prog, &diag);
    check(vm_ctx != NULL, "aot incr: vm ctx create");
    if (!vm_ctx) goto cleanup;

    slot = tc_embed_self_var_slot(vm_ctx, "counter");
    check(slot >= 0, "aot incr: vm self_var_slot valid");

    for (call = 1; call <= 4; call++) {
        TcValue result;
        int64_t val;
        tc_embed_slot_write(vm_ctx, slot,
                            tc_value_from_int32(call * 10));
        check(tc_embed_call(vm_ctx, NULL, "get_counter", 0, NULL, &result) == 0,
              "aot incr: vm call get_counter");
        tc_value_to_int64(result, &val);
        check(val == (int64_t)(call * 10), "aot incr: vm sequential value");
    }

    /* AOT 模式 */
    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot incr: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot incr: aot ctx create");
    if (!aot_ctx) goto cleanup;

    slot = tc_embed_self_var_slot(aot_ctx, "counter");
    check(slot >= 0, "aot incr: aot self_var_slot valid");

    for (call = 1; call <= 4; call++) {
        TcValue result;
        int64_t val;
        tc_embed_slot_write(aot_ctx, slot,
                            tc_value_from_int32(call * 10));
        check(tc_embed_call(aot_ctx, NULL, "get_counter", 0, NULL, &result) == 0,
              "aot incr: aot call get_counter");
        tc_value_to_int64(result, &val);
        check(val == (int64_t)(call * 10), "aot incr: aot sequential value");
    }

cleanup:
    tc_embed_destroy(aot_ctx);
    tc_embed_destroy(vm_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：AOT 模式下 ptr encode/decode/null API ── */
static void test_aot_embed_ptr_encode_decode(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    TcValue v;
    int slot, i;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func noop() void then\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot ptr api: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot ptr api: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot ptr api: aot ctx create");
    if (!aot_ctx) goto cleanup;

    /* nullptr */
    v.bits = 0;
    v.type = TC_PTR;
    check(tc_embed_ptr_is_null(v) != 0, "aot ptr api: is_null(0)");

    /* encode/decode往返 */
    for (i = 0; i < 50; i++) {
        v = tc_embed_ptr_encode(i);
        check(v.type == TC_PTR, "aot ptr api: encode type == TC_PTR");
        check(tc_embed_ptr_is_null(v) == 0, "aot ptr api: !is_null(encoded)");
        check(tc_embed_ptr_decode_slot(v, &slot) == 0,
              "aot ptr api: decode ok");
        check(slot == i, "aot ptr api: decode returns correct slot");
    }

    /* 无效 ptr 拒绝 */
    v.bits = 6;
    v.type = TC_PTR;
    check(tc_embed_ptr_decode_slot(v, &slot) != 0,
          "aot ptr api: decode rejects invalid ptr");

cleanup:
    tc_embed_destroy(aot_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：AOT 模式下 func_info 查询 ── */
static void test_aot_embed_func_info_query(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    const TcEmbedFuncInfo *info;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public func plus(a: int32, b: int32) int32 then\n"
            "    var sum: int32 = add(int32, a, b)\n"
            "    return sum\n"
            "end\n"
            "public func greet(x: bool) void then\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot func info: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot func info: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot func info: aot ctx create");
    if (!aot_ctx) goto cleanup;

    info = tc_embed_func_info(aot_ctx, NULL, "plus");
    check(info != NULL, "aot func info: find plus");
    check(info->has_return != 0, "aot func info: plus has return");
    check(info->param_count == 2, "aot func info: plus has 2 params");

    info = tc_embed_func_info(aot_ctx, NULL, "greet");
    check(info != NULL, "aot func info: find greet");
    check(info->has_return == 0, "aot func info: greet void return");
    check(info->param_count == 1, "aot func info: greet has 1 param");
    check(info->param_types[0] == TC_BOOL, "aot func info: greet param is bool");

    info = tc_embed_func_info(aot_ctx, NULL, "nonexistent");
    check(info == NULL, "aot func info: nonexistent returns NULL");

cleanup:
    tc_embed_destroy(aot_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：AOT 模式下变量 slot 查询 ── */
static void test_aot_embed_var_slot_query(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *aot_ctx = NULL;
    char *so_path = NULL;
    size_t slot_count = 0;
    int slot;

    tc_diagnostic_init(&diag);
    check(compile_lib(
            "public static var score: int32 = 100\n"
            "public func get_score() int32 then\n"
            "    var v: int32 = Self.score\n"
            "    return v\n"
            "end\n", "test", &prog, &diag) == 0,
          "aot var slot: compile lib");
    if (diag.domain != TC_DIAG_NONE) {
        fprintf(stderr, "  diag: %s\n", diag.message);
        tc_diagnostic_clear(&diag);
        return;
    }

    so_path = build_aot_embed_lib(&prog, &slot_count);
    check(so_path != NULL, "aot var slot: build shared lib");
    if (!so_path) goto cleanup;

    aot_ctx = create_aot_embed_ctx(&prog, so_path, slot_count);
    check(aot_ctx != NULL, "aot var slot: aot ctx create");
    if (!aot_ctx) goto cleanup;

    /* self_var_slot 查询 */
    slot = tc_embed_self_var_slot(aot_ctx, "score");
    check(slot >= 0, "aot var slot: find score by self_var_slot");

    /* 通过 slot 读验证值 */
    {
        TcValue r; int64_t v;
        check(tc_embed_slot_read(aot_ctx, slot, &r) == 0,
              "aot var slot: read score slot");
        tc_value_to_int64(r, &v);
        check(v == 100, "aot var slot: score == 100");
    }

    /* 写新值 → 通过函数验证 */
    {
        TcValue result; int64_t val;
        tc_embed_slot_write(aot_ctx, slot, tc_value_from_int32(200));
        check(tc_embed_call(aot_ctx, NULL, "get_score", 0, NULL, &result) == 0,
              "aot var slot: call get_score after write");
        tc_value_to_int64(result, &val);
        check(val == 200, "aot var slot: get_score() == 200");
    }

    slot = tc_embed_self_var_slot(aot_ctx, "nonexistent");
    check(slot == -1, "aot var slot: nonexistent returns -1");

cleanup:
    tc_embed_destroy(aot_ctx);
    if (so_path) { unlink(so_path); free(so_path); }
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ================================================================ */

int main(void) {
    test_aot_embed_scalar_call();
    test_aot_embed_static_var();
    test_aot_embed_slot_rw();
    test_aot_embed_error();
    test_aot_embed_call_ptr_array();
    test_aot_embed_call_nested();
    test_aot_embed_init_cleanup();
    test_aot_embed_float64();
    test_aot_embed_bool();
    test_aot_embed_void_noarg();
    test_aot_embed_ptr_store_inplace();
    test_aot_embed_static_var_increment();
    test_aot_embed_ptr_encode_decode();
    test_aot_embed_func_info_query();
    test_aot_embed_var_slot_query();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
