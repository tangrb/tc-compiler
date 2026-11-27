#include "vm/gdb_rsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/profile.h"

static size_t checks;
static size_t failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        checks++;                                                               \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__,         \
                    #condition);                                                \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void check_codec(void) {
    static const uint8_t payload[] = {'A', '#', '$', '}', '*', 0u, 0xffu};
    uint8_t frame[128];
    uint8_t decoded[128];
    char hex[32];
    size_t frame_len;
    size_t decoded_len;
    TcGdbRspPacket packet;

    frame_len = tc_gdb_rsp_frame(payload, sizeof(payload), frame, sizeof(frame));
    CHECK(frame_len != 0u);
    CHECK(frame[0] == '$' && frame[frame_len - 3u] == '#');
    CHECK(tc_gdb_rsp_decode(frame, frame_len, &packet) == TC_GDB_RSP_DECODE_OK);
    CHECK(packet.len == sizeof(payload));
    CHECK(memcmp(packet.data, payload, sizeof(payload)) == 0);

    {
        static const char bad_frame[] = "$A#00";
        CHECK(tc_gdb_rsp_decode((const uint8_t *)bad_frame, sizeof(bad_frame) - 1u,
                                &packet) == TC_GDB_RSP_DECODE_BAD_CHECKSUM);
    }
    {
        static const char invalid_frame[] = "$}#00";
        CHECK(tc_gdb_rsp_decode((const uint8_t *)invalid_frame,
                                sizeof(invalid_frame) - 1u, &packet) ==
              TC_GDB_RSP_DECODE_INVALID);
    }
    CHECK(tc_gdb_rsp_hex_encode(payload, sizeof(payload), hex,
                                sizeof(hex)) == 0);
    CHECK(tc_gdb_rsp_hex_decode(hex, sizeof(payload) * 2u, decoded,
                                sizeof(decoded), &decoded_len) == 0);
    CHECK(decoded_len == sizeof(payload));
    CHECK(memcmp(decoded, payload, sizeof(payload)) == 0);
    CHECK(tc_gdb_rsp_hex_decode("0", 1u, decoded, sizeof(decoded),
                                &decoded_len) != 0);
    CHECK(tc_gdb_rsp_hex_decode("gg", 2u, decoded, sizeof(decoded),
                                &decoded_len) != 0);
    CHECK(tc_gdb_rsp_hex_encode(payload, sizeof(payload), hex, 4u) != 0);
}

static void check_target_xml(void) {
    TcPlatformProfile profile;
    char xml[8192];
    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    CHECK(tc_gdb_rsp_target_xml(&profile, xml, sizeof(xml)) == 0);
    CHECK(strstr(xml, "<architecture>aarch64</architecture>") != NULL);
    CHECK(strstr(xml, "org.gnu.gdb.aarch64.core") != NULL);
    CHECK(strstr(xml, "name=\"pc\" bitsize=\"64\" type=\"code_ptr\" regnum=\"32\"") !=
          NULL);
    CHECK(strstr(xml, "name=\"tc_ir_pc\" bitsize=\"64\" type=\"code_ptr\" "
                      "regnum=\"34\"") != NULL);
    CHECK(strstr(xml, "org.tc.ir") != NULL);

    CHECK(tc_profile_init_arm32_baremetal(&profile));
    CHECK(tc_gdb_rsp_target_xml(&profile, xml, sizeof(xml)) == 0);
    CHECK(strstr(xml, "<architecture>arm</architecture>") != NULL);
    CHECK(strstr(xml, "org.gnu.gdb.arm.m-profile") != NULL);
    CHECK(strstr(xml, "name=\"pc\" bitsize=\"32\" type=\"code_ptr\" regnum=\"15\"") !=
          NULL);
    CHECK(strstr(xml, "regnum=\"17\"") != NULL);

    CHECK(tc_profile_init_riscv32_baremetal(&profile));
    CHECK(tc_gdb_rsp_target_xml(&profile, xml, sizeof(xml)) == 0);
    CHECK(strstr(xml, "<architecture>riscv:rv32</architecture>") != NULL);
    CHECK(strstr(xml, "org.gnu.gdb.riscv.cpu") != NULL);
    CHECK(strstr(xml, "regnum=\"33\"") != NULL);

    CHECK(tc_profile_init_generic_w32(&profile));
    CHECK(tc_gdb_rsp_target_xml(&profile, xml, sizeof(xml)) == 0);
    CHECK(strstr(xml, "<architecture>aarch64</architecture>") != NULL);
}

static void check_memory_map(void) {
    TcPlatformProfile profile;
    char xml[4096];
    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    CHECK(tc_gdb_rsp_memory_map_xml(&profile, xml, sizeof(xml)) == 0);
    CHECK(strstr(xml, "<memory-map>") != NULL);
    CHECK(strstr(xml, "type=\"ram\"") != NULL);
    CHECK(strstr(xml, "type=\"rom\"") != NULL);
    CHECK(strstr(xml, "property") == NULL);
}

int main(void) {
    check_codec();
    check_target_xml();
    check_memory_map();
    printf("gdb_rsp: %zu checks, %zu failures\n", checks, failures);
    return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
