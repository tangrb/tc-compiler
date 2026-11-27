#include "vm/gdb_rsp.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    TC_GDB_ARCH_AARCH64,
    TC_GDB_ARCH_ARM_M,
    TC_GDB_ARCH_RISCV32
} TcGdbArch;

static int hex_value(uint8_t c) {
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
        return (int)(c - (uint8_t)'0');
    }
    if (c >= (uint8_t)'a' && c <= (uint8_t)'f') {
        return (int)(c - (uint8_t)'a') + 10;
    }
    if (c >= (uint8_t)'A' && c <= (uint8_t)'F') {
        return (int)(c - (uint8_t)'A') + 10;
    }
    return -1;
}

static char hex_digit(unsigned value) {
    static const char digits[] = "0123456789abcdef";
    return digits[value & 15u];
}

TcGdbRspDecodeStatus tc_gdb_rsp_decode(const uint8_t *frame, size_t frame_len,
                                      TcGdbRspPacket *out) {
    size_t payload_len;
    size_t i;
    size_t decoded_len;
    uint8_t expected;
    uint8_t actual = 0;
    int high;
    int low;

    if (out == NULL) {
        return TC_GDB_RSP_DECODE_INVALID;
    }
    out->len = 0;
    if (frame == NULL || frame_len < 4u || frame[0] != (uint8_t)'$' ||
        frame[frame_len - 3u] != (uint8_t)'#') {
        return TC_GDB_RSP_DECODE_INVALID;
    }

    high = hex_value(frame[frame_len - 2u]);
    low = hex_value(frame[frame_len - 1u]);
    if (high < 0 || low < 0) {
        return TC_GDB_RSP_DECODE_INVALID;
    }
    expected = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    payload_len = frame_len - 4u;
    if (payload_len > TC_GDB_RSP_PACKET_SIZE) {
        return TC_GDB_RSP_DECODE_TOO_LONG;
    }
    for (i = 0; i < payload_len; ++i) {
        actual = (uint8_t)(actual + frame[i + 1u]);
    }

    decoded_len = 0;
    i = 0;
    while (i < payload_len) {
        uint8_t c = frame[i + 1u];
        if (c == (uint8_t)'}') {
            if (i + 1u >= payload_len) {
                return TC_GDB_RSP_DECODE_INVALID;
            }
            c = (uint8_t)(frame[i + 2u] ^ 0x20u);
            i += 2u;
        } else if (c == (uint8_t)'*') {
            size_t repeat;
            if (i + 1u >= payload_len || decoded_len == 0) {
                return TC_GDB_RSP_DECODE_INVALID;
            }
            if (frame[i + 2u] < 30u) {
                return TC_GDB_RSP_DECODE_INVALID;
            }
            repeat = (size_t)frame[i + 2u] - 29u;
            if (repeat - 1u > TC_GDB_RSP_PACKET_SIZE - decoded_len) {
                return TC_GDB_RSP_DECODE_TOO_LONG;
            }
            if (repeat > 1u) {
                memset(&out->data[decoded_len], out->data[decoded_len - 1u],
                       repeat - 1u);
            }
            decoded_len += repeat - 1u;
            i += 2u;
            continue;
        } else {
            if (c == (uint8_t)'#' || c == (uint8_t)'$') {
                return TC_GDB_RSP_DECODE_INVALID;
            }
            i += 1u;
        }
        if (decoded_len >= TC_GDB_RSP_PACKET_SIZE) {
            return TC_GDB_RSP_DECODE_TOO_LONG;
        }
        out->data[decoded_len++] = c;
    }

    if (actual != expected) {
        return TC_GDB_RSP_DECODE_BAD_CHECKSUM;
    }
    out->len = decoded_len;
    return TC_GDB_RSP_DECODE_OK;
}

static int frame_byte_size(uint8_t c, size_t *size) {
    if (c == (uint8_t)'#' || c == (uint8_t)'$' || c == (uint8_t)'}' ||
        c == (uint8_t)'*') {
        *size = 2u;
    } else {
        *size = 1u;
    }
    return 1;
}

size_t tc_gdb_rsp_frame(const uint8_t *data, size_t len, uint8_t *out,
                        size_t out_cap) {
    size_t payload_len = 0;
    size_t required;
    size_t i;
    size_t pos;
    uint8_t sum = 0;

    if ((data == NULL && len != 0u) || out == NULL) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        size_t width;
        (void)frame_byte_size(data[i], &width);
        if (payload_len > SIZE_MAX - width) {
            return 0;
        }
        payload_len += width;
    }
    if (payload_len > SIZE_MAX - 4u) {
        return 0;
    }
    required = payload_len + 4u;
    if (out_cap < required) {
        return 0;
    }

    out[0] = (uint8_t)'$';
    pos = 1u;
    for (i = 0; i < len; ++i) {
        uint8_t c = data[i];
        size_t width;
        (void)frame_byte_size(c, &width);
        if (width == 2u) {
            out[pos++] = (uint8_t)'}';
            c ^= 0x20u;
        }
        out[pos++] = c;
        sum = (uint8_t)(sum + c);
        if (width == 2u) {
            sum = (uint8_t)(sum + (uint8_t)'}');
        }
    }
    out[pos++] = (uint8_t)'#';
    out[pos++] = (uint8_t)hex_digit((unsigned)sum >> 4);
    out[pos++] = (uint8_t)hex_digit((unsigned)sum);
    return pos;
}

int tc_gdb_rsp_hex_decode(const char *hex, size_t hex_len, uint8_t *out,
                          size_t out_cap, size_t *out_len) {
    size_t count;
    size_t i;

    if (out_len == NULL) {
        return -1;
    }
    *out_len = 0;
    if (hex == NULL || (hex_len & 1u) != 0u || (out == NULL && hex_len != 0u)) {
        return -1;
    }
    count = hex_len / 2u;
    if (count > out_cap) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        int high = hex_value((uint8_t)hex[i * 2u]);
        int low = hex_value((uint8_t)hex[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return -1;
        }
        out[i] = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    }
    *out_len = count;
    return 0;
}

int tc_gdb_rsp_hex_encode(const uint8_t *data, size_t len, char *out,
                          size_t out_cap) {
    size_t i;
    if (out == NULL || out_cap == 0u || len > (out_cap - 1u) / 2u ||
        len > (SIZE_MAX - 1u) / 2u || (data == NULL && len != 0u)) {
        return -1;
    }
    for (i = 0; i < len; ++i) {
        out[i * 2u] = hex_digit((unsigned)data[i] >> 4);
        out[i * 2u + 1u] = hex_digit((unsigned)data[i]);
    }
    out[len * 2u] = '\0';
    return 0;
}

static int appendf(char *out, size_t out_cap, size_t *out_len, const char *format,
                   ...) {
    va_list args;
    int count;
    size_t available;

    if (*out_len >= out_cap) {
        return -1;
    }
    available = out_cap - *out_len;
    va_start(args, format);
    count = vsnprintf(out + *out_len, available, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= available) {
        return -1;
    }
    *out_len += (size_t)count;
    return 0;
}

static int profile_arch(const TcPlatformProfile *profile, TcGdbArch *arch) {
    const char *name;
    if (profile == NULL || profile->target == NULL || profile->target->name == NULL) {
        return 0;
    }
    name = profile->target->name;
    if (strcmp(name, "aarch64-linux-user") == 0 ||
        strcmp(name, "aarch64-baremetal") == 0 ||
        strcmp(name, "generic-w32") == 0 || strcmp(name, "w32") == 0) {
        *arch = TC_GDB_ARCH_AARCH64;
        return 1;
    }
    if (strcmp(name, "arm32-baremetal") == 0) {
        *arch = TC_GDB_ARCH_ARM_M;
        return 1;
    }
    if (strcmp(name, "riscv32-baremetal") == 0) {
        *arch = TC_GDB_ARCH_RISCV32;
        return 1;
    }
    return 0;
}

static int append_reg(char *out, size_t out_cap, size_t *out_len,
                      const char *name, unsigned bits, const char *type,
                      unsigned regnum) {
    return appendf(out, out_cap, out_len,
                   "    <reg name=\"%s\" bitsize=\"%u\" type=\"%s\" regnum=\"%u\"/>\n",
                   name, bits, type, regnum);
}

int tc_gdb_rsp_target_xml(const TcPlatformProfile *profile, char *out,
                          size_t out_cap) {
    TcGdbArch arch;
    size_t len = 0;
    unsigned i;
    unsigned tc_base;

    if (out == NULL || out_cap == 0u) {
        return -1;
    }
    out[0] = '\0';
    if (!profile_arch(profile, &arch)) {
        return -1;
    }
    if (appendf(out, out_cap, &len,
                "<?xml version=\"1.0\"?>\n"
                "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                "<target version=\"1.0\">\n") != 0) {
        return -1;
    }

    if (arch == TC_GDB_ARCH_AARCH64) {
        if (appendf(out, out_cap, &len,
                    "  <architecture>aarch64</architecture>\n"
                    "  <feature name=\"org.gnu.gdb.aarch64.core\">\n") != 0) {
            return -1;
        }
        for (i = 0; i <= 30u; ++i) {
            char name[8];
            (void)snprintf(name, sizeof(name), "x%u", i);
            if (append_reg(out, out_cap, &len, name, 64u, "int64", i) != 0) {
                return -1;
            }
        }
        if (append_reg(out, out_cap, &len, "sp", 64u, "data_ptr", 31u) != 0 ||
            append_reg(out, out_cap, &len, "pc", 64u, "code_ptr", 32u) != 0 ||
            appendf(out, out_cap, &len,
                    "    <flags id=\"cpsr_flags\" size=\"4\">\n"
                    "      <field name=\"mode\" start=\"0\" end=\"4\"/>\n"
                    "    </flags>\n") != 0 ||
            append_reg(out, out_cap, &len, "cpsr", 32u, "cpsr_flags", 33u) != 0 ||
            appendf(out, out_cap, &len, "  </feature>\n") != 0) {
            return -1;
        }
        tc_base = 34u;
    } else if (arch == TC_GDB_ARCH_ARM_M) {
        if (appendf(out, out_cap, &len,
                    "  <architecture>arm</architecture>\n"
                    "  <feature name=\"org.gnu.gdb.arm.m-profile\">\n") != 0) {
            return -1;
        }
        for (i = 0; i <= 12u; ++i) {
            char name[8];
            (void)snprintf(name, sizeof(name), "r%u", i);
            if (append_reg(out, out_cap, &len, name, 32u, "int32", i) != 0) {
                return -1;
            }
        }
        if (append_reg(out, out_cap, &len, "sp", 32u, "data_ptr", 13u) != 0 ||
            append_reg(out, out_cap, &len, "lr", 32u, "int32", 14u) != 0 ||
            append_reg(out, out_cap, &len, "pc", 32u, "code_ptr", 15u) != 0 ||
            append_reg(out, out_cap, &len, "xpsr", 32u, "int32", 16u) != 0 ||
            appendf(out, out_cap, &len, "  </feature>\n") != 0) {
            return -1;
        }
        tc_base = 17u;
    } else {
        if (appendf(out, out_cap, &len,
                    "  <architecture>riscv:rv32</architecture>\n"
                    "  <feature name=\"org.gnu.gdb.riscv.cpu\">\n") != 0) {
            return -1;
        }
        for (i = 0; i <= 31u; ++i) {
            char name[8];
            (void)snprintf(name, sizeof(name), "x%u", i);
            if (append_reg(out, out_cap, &len, name, 32u, "int32", i) != 0) {
                return -1;
            }
        }
        if (append_reg(out, out_cap, &len, "pc", 32u, "code_ptr", 32u) != 0 ||
            appendf(out, out_cap, &len, "  </feature>\n") != 0) {
            return -1;
        }
        tc_base = 33u;
    }

    if (appendf(out, out_cap, &len,
                "  <feature name=\"org.tc.ir\">\n") != 0 ||
        append_reg(out, out_cap, &len, "tc_ir_pc", 64u, "uint64", tc_base) != 0 ||
        append_reg(out, out_cap, &len, "tc_ir_depth", 64u, "uint64",
                   tc_base + 1u) != 0 ||
        append_reg(out, out_cap, &len, "tc_ir_sp", 64u, "uint64",
                   tc_base + 2u) != 0 ||
        append_reg(out, out_cap, &len, "tc_ir_insn", 64u, "uint64",
                   tc_base + 3u) != 0 ||
        appendf(out, out_cap, &len,
                "  </feature>\n"
                "</target>\n") != 0) {
        return -1;
    }
    return 0;
}

int tc_gdb_rsp_memory_map_xml(const TcPlatformProfile *profile, char *out,
                              size_t out_cap) {
    static const TcRegionKind order[TC_REGION_COUNT] = {
        TC_REG_FRAME, TC_REG_STATIC, TC_REG_DEVICE, TC_REG_HEAP, TC_REG_CODE
    };
    static const char *const types[TC_REGION_COUNT] = {
        "ram", "ram", "rom", "rom", "ram"
    };
    size_t len = 0;
    size_t i;

    if (profile == NULL || out == NULL || out_cap == 0u ||
        (profile->addr_width != 32u && profile->addr_width != 64u)) {
        return -1;
    }
    out[0] = '\0';
    if (appendf(out, out_cap, &len,
                "<?xml version=\"1.0\"?>\n"
                "<memory-map>\n") != 0) {
        return -1;
    }
    for (i = 0; i < TC_REGION_COUNT; ++i) {
        TcRegionKind region = order[i];
        if (appendf(out, out_cap, &len,
                    "  <memory type=\"%s\" start=\"0x%" PRIx64
                    "\" length=\"0x%" PRIx64 "\"/>\n",
                    types[region], profile->region[region].base,
                    profile->region[region].size) != 0) {
            return -1;
        }
    }
    if (appendf(out, out_cap, &len, "</memory-map>\n") != 0) {
        return -1;
    }
    return 0;
}
