#ifndef TC_VM_GDB_RSP_H
#define TC_VM_GDB_RSP_H

#include <stddef.h>
#include <stdint.h>

#include "runtime/profile.h"

#define TC_GDB_RSP_PACKET_SIZE 16384u

typedef enum {
    TC_GDB_RSP_DECODE_OK = 0,
    TC_GDB_RSP_DECODE_BAD_CHECKSUM,
    TC_GDB_RSP_DECODE_TOO_LONG,
    TC_GDB_RSP_DECODE_INVALID
} TcGdbRspDecodeStatus;

typedef struct {
    uint8_t data[TC_GDB_RSP_PACKET_SIZE];
    size_t len;
} TcGdbRspPacket;

TcGdbRspDecodeStatus tc_gdb_rsp_decode(const uint8_t *frame, size_t frame_len,
                                      TcGdbRspPacket *out);

size_t tc_gdb_rsp_frame(const uint8_t *data, size_t len, uint8_t *out,
                        size_t out_cap);

int tc_gdb_rsp_hex_decode(const char *hex, size_t hex_len, uint8_t *out,
                          size_t out_cap, size_t *out_len);

int tc_gdb_rsp_hex_encode(const uint8_t *data, size_t len, char *out,
                          size_t out_cap);

int tc_gdb_rsp_target_xml(const TcPlatformProfile *profile, char *out,
                          size_t out_cap);

int tc_gdb_rsp_memory_map_xml(const TcPlatformProfile *profile, char *out,
                              size_t out_cap);

#endif
