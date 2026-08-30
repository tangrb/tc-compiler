/*
 * test_endianness.c — A1 端序无关布局单元测试（§3.5 / TC-0.0.42 契约）
 *
 * 验证固定 LE 字节约定：memblock 头部 usize 与标量元素、struct 标量字段的
 * 字节区间内一律低字节在前（显式位组装，不依赖宿主字节序）。通过 Embed
 * VM 模式读取 static var 的堆块原始字节断言布局。
 */
#include "tc_embed.h"
#include "tc_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static int compile_lib(const char *source, const char *name, TcTypedProgram *out,
                       TcDiagnostic *diag) {
    char full[2048];
    (void)snprintf(full, sizeof(full), "#lib\n%s", source);
    return tc_compile_source(full, name, out, diag);
}

static void test_memblock_le_layout(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx = NULL;
    int slot = -1;
    TcValue value;
    const uint8_t *bytes = NULL;
    int i = 0;

    tc_diagnostic_init(&diag);
    check(compile_lib(
              "public static var mb: memblock<int32, 3> = memblock(int32, count: 3, 0x01020304, 0x05060708, 0x0A0B0C0D)\n"
              "public func get(v: int32) int32 then\n"
              "    return v\n"
              "end\n",
              "endian_mb", &prog, &diag) == 0,
          "compile memblock lib");
    if (diag.domain != TC_DIAG_NONE) {
        tc_diagnostic_clear(&diag);
        return;
    }

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create embed ctx");
    if (!ctx) {
        tc_typed_program_free(&prog);
        tc_diagnostic_clear(&diag);
        return;
    }

    slot = tc_embed_self_var_slot(ctx, "mb");
    check(slot >= 0, "find static var mb slot");
    if (slot >= 0) {
        check(tc_embed_slot_read(ctx, slot, &value) == 0, "read mb slot");
        check(value.bits != 0 && value.type && value.type->tag == TC_MEMBLOCK,
              "mb slot holds memblock heap pointer");
        if (value.bits != 0) {
            bytes = (const uint8_t *)(uintptr_t)value.bits;

            /* 头部 usize = 3，LE：{03,00,00,00,00,00,00,00} */
            check(bytes[0] == 0x03 && bytes[1] == 0x00 && bytes[7] == 0x00,
                  "memblock header count stored little-endian");
            /* 元素 i 自偏移 8 起，每个 4 字节 LE */
            {
                static const uint32_t elems[3] = {0x01020304u, 0x05060708u, 0x0A0B0C0Du};
                for (i = 0; i < 3; i++) {
                    const uint8_t *e = bytes + 8 + i * 4;
                    check(e[0] == (uint8_t)(elems[i]) && e[1] == (uint8_t)(elems[i] >> 8) &&
                              e[2] == (uint8_t)(elems[i] >> 16) &&
                              e[3] == (uint8_t)(elems[i] >> 24),
                          "memblock element little-endian layout");
                }
            }
        }
    }

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

static void test_struct_field_le_layout(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx = NULL;
    int slot = -1;
    TcValue value;
    const uint8_t *bytes = NULL;

    tc_diagnostic_init(&diag);
    check(compile_lib(
              "public struct P then\n"
              "    var v: int32\n"
              "    var w: int16\n"
              "end\n"
              "public static var st: P = P(v: 0x11223344, w: 0x5566)\n"
              "public func id(x: int32) int32 then\n"
              "    return x\n"
              "end\n",
              "endian_st", &prog, &diag) == 0,
          "compile struct lib");
    if (diag.domain != TC_DIAG_NONE) {
        tc_diagnostic_clear(&diag);
        return;
    }

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create embed ctx");
    if (!ctx) {
        tc_typed_program_free(&prog);
        tc_diagnostic_clear(&diag);
        return;
    }

    slot = tc_embed_self_var_slot(ctx, "st");
    check(slot >= 0, "find static var st slot");
    if (slot >= 0) {
        check(tc_embed_slot_read(ctx, slot, &value) == 0, "read st slot");
        check(value.bits != 0 && value.type && value.type->tag == TC_STRUCT,
              "st slot holds struct heap pointer");
        if (value.bits != 0) {
            bytes = (const uint8_t *)(uintptr_t)value.bits;
            /* v: int32 @ offset 0，LE；w: int16 @ offset 4，LE */
            check(bytes[0] == 0x44 && bytes[1] == 0x33 && bytes[2] == 0x22 && bytes[3] == 0x11,
                  "struct int32 field little-endian");
            check(bytes[4] == 0x66 && bytes[5] == 0x55,
                  "struct int16 field little-endian");
        }
    }

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_memblock_le_layout();
    test_struct_field_le_layout();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
