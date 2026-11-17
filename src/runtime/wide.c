/* TC 0.0.46 exact wide-integer helpers (standard §6.3/§10.6). */
#include "runtime/wide.h"

TcWide tc_wide_from_u64(uint64_t value) {
    TcWide result;
    result.hi = 0;
    result.lo = value;
    return result;
}

uint64_t tc_wide_low_u64(TcWide value) {
    return value.lo;
}

int tc_wide_cmp(TcWide a, TcWide b) {
    if (a.hi != b.hi) {
        return a.hi < b.hi ? -1 : 1;
    }
    if (a.lo != b.lo) {
        return a.lo < b.lo ? -1 : 1;
    }
    return 0;
}

int tc_wide_eq_u64(TcWide a, uint64_t b) {
    return a.hi == 0 && a.lo == b;
}

int tc_wide_gt_u64(TcWide a, uint64_t b) {
    return a.hi != 0 || a.lo > b;
}

TcWide tc_wide_mul_u64(uint64_t a, uint64_t b) {
    uint64_t a0 = a & UINT64_C(0xffffffff);
    uint64_t a1 = a >> 32;
    uint64_t b0 = b & UINT64_C(0xffffffff);
    uint64_t b1 = b >> 32;
    uint64_t p0 = a0 * b0;
    uint64_t p1 = a1 * b0;
    uint64_t p2 = a0 * b1;
    uint64_t p3 = a1 * b1;
    uint64_t middle = (p0 >> 32) + (p1 & UINT64_C(0xffffffff)) +
                      (p2 & UINT64_C(0xffffffff));
    TcWide result;
    result.lo = (p0 & UINT64_C(0xffffffff)) | (middle << 32);
    result.hi = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
    return result;
}

int tc_wide_shl_checked(TcWide value, unsigned shift, TcWide *out) {
    if (shift >= 128u) {
        if (value.hi != 0 || value.lo != 0) {
            return 0;
        }
        out->hi = 0;
        out->lo = 0;
        return 1;
    }
    if (shift >= 64u) {
        unsigned high_shift = shift - 64u;
        if (value.hi != 0) {
            return 0;
        }
        if (high_shift != 0u && (value.lo >> (64u - high_shift)) != 0) {
            return 0;
        }
        out->hi = value.lo << high_shift;
        out->lo = 0;
        return 1;
    }
    if (shift != 0u && (value.hi >> (64u - shift)) != 0) {
        return 0;
    }
    out->hi = (value.hi << shift) |
              (shift == 0u ? 0u : (value.lo >> (64u - shift)));
    out->lo = value.lo << shift;
    return 1;
}

int tc_wide_add_checked(TcWide a, TcWide b, TcWide *out) {
    uint64_t lo = a.lo + b.lo;
    uint64_t carry = lo < a.lo ? 1u : 0u;
    uint64_t hi = a.hi + b.hi;
    uint64_t hi_carry = hi < a.hi ? 1u : 0u;
    uint64_t hi2 = hi + carry;
    if (hi_carry != 0u || hi2 < hi) {
        return 0;
    }
    out->hi = hi2;
    out->lo = lo;
    return 1;
}

TcWide tc_wide_sub(TcWide a, TcWide b) {
    TcWide result;
    uint64_t borrow = a.lo < b.lo ? 1u : 0u;
    result.lo = a.lo - b.lo;
    result.hi = a.hi - b.hi - borrow;
    return result;
}
