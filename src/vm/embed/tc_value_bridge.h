/*
 * tc_value_bridge.h — 值桥接辅助函数族（v0.0.36）
 *
 * 纯数据构造/解构，不涉及 heap 分配或 I/O。全部 static inline。
 */
#ifndef TC_VALUE_BRIDGE_H
#define TC_VALUE_BRIDGE_H

#include <string.h>

#include "tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── int64 ── */
static inline TcValue tc_value_from_int64(int64_t val) {
    TcValue v = { .type = TC_INT64, .bits = (uint64_t)val };
    return v;
}

static inline int tc_value_to_int64(TcValue v, int64_t *out) {
    *out = (int64_t)v.bits;
    return 0;
}

/* ── uint64 ── */
static inline TcValue tc_value_from_uint64(uint64_t val) {
    TcValue v = { .type = TC_UINT64, .bits = val };
    return v;
}

static inline int tc_value_to_uint64(TcValue v, uint64_t *out) {
    *out = v.bits;
    return 0;
}

/* ── int32 ── */
static inline TcValue tc_value_from_int32(int32_t val) {
    TcValue v = { .type = TC_INT32, .bits = (uint64_t)(uint32_t)val };
    return v;
}

static inline int tc_value_to_int32(TcValue v, int32_t *out) {
    *out = (int32_t)(uint32_t)v.bits;
    return 0;
}

/* ── uint32 ── */
static inline TcValue tc_value_from_uint32(uint32_t val) {
    TcValue v = { .type = TC_UINT32, .bits = (uint64_t)val };
    return v;
}

static inline int tc_value_to_uint32(TcValue v, uint32_t *out) {
    *out = (uint32_t)v.bits;
    return 0;
}

/* ── int16 ── */
static inline TcValue tc_value_from_int16(int16_t val) {
    TcValue v = { .type = TC_INT16, .bits = (uint64_t)(uint16_t)(int16_t)val };
    return v;
}

static inline int tc_value_to_int16(TcValue v, int16_t *out) {
    *out = (int16_t)(uint16_t)v.bits;
    return 0;
}

/* ── uint16 ── */
static inline TcValue tc_value_from_uint16(uint16_t val) {
    TcValue v = { .type = TC_UINT16, .bits = (uint64_t)val };
    return v;
}

static inline int tc_value_to_uint16(TcValue v, uint16_t *out) {
    *out = (uint16_t)v.bits;
    return 0;
}

/* ── int8 ── */
static inline TcValue tc_value_from_int8(int8_t val) {
    TcValue v = { .type = TC_INT8, .bits = (uint64_t)(uint8_t)(int8_t)val };
    return v;
}

static inline int tc_value_to_int8(TcValue v, int8_t *out) {
    *out = (int8_t)(uint8_t)v.bits;
    return 0;
}

/* ── uint8 ── */
static inline TcValue tc_value_from_uint8(uint8_t val) {
    TcValue v = { .type = TC_UINT8, .bits = (uint64_t)val };
    return v;
}

static inline int tc_value_to_uint8(TcValue v, uint8_t *out) {
    *out = (uint8_t)v.bits;
    return 0;
}

/* ── float64 ── */
static inline TcValue tc_value_from_double(double val) {
    TcValue v = { .type = TC_FLOAT64 };
    memcpy(&v.bits, &val, sizeof(double));
    return v;
}

static inline int tc_value_to_double(TcValue v, double *out) {
    memcpy(out, &v.bits, sizeof(double));
    return 0;
}

/* ── float32 ── */
static inline TcValue tc_value_from_float(float val) {
    TcValue v = { .type = TC_FLOAT32 };
    uint32_t bits;
    memcpy(&bits, &val, sizeof(float));
    v.bits = (uint64_t)bits;
    return v;
}

static inline int tc_value_to_float(TcValue v, float *out) {
    uint32_t bits = (uint32_t)v.bits;
    memcpy(out, &bits, sizeof(float));
    return 0;
}

/* ── bool ── */
static inline TcValue tc_value_from_bool(int val) {
    TcValue v = { .type = TC_BOOL, .bits = val ? 1ULL : 0ULL };
    return v;
}

static inline int tc_value_to_bool(TcValue v) {
    return v.bits != 0;
}

/* v0.0.36 reserved: memblock/struct 互操作 */
/* TcValue tc_value_wrap_external_memblock(void *data, size_t elem_size,
                                           uint64_t count, TcTypeKind elem_type,
                                           TcEmbedCtx *ctx); */
/* TcValue tc_value_from_struct_raw(const void *data, size_t byte_size); */

#ifdef __cplusplus
}
#endif

#endif /* TC_VALUE_BRIDGE_H */
