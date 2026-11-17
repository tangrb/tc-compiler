#!/usr/bin/env python3
"""AAPCS64 interoperability audit: C callers calling TC-generated callees.

Each case compiles a TC library function to AArch64 assembly, appends a global
``tc_test_call`` thunk that tail-branches to ``tc_function_0``, compiles a
freestanding C harness that declares the same signature, links both with the
bundled runtime and runs the image under qemu-system-aarch64.  The C compiler is
an independent AAPCS64 implementation, so this checks the generated callee
against the published ABI rather than against the reference VM.

The TC function uses parameters and locals only (no static/global accesses);
the generated ``tc_main`` is renamed and never executed.  Results are printed
through ``tc_rt_write_scalar`` as integer bit patterns.

Run:
    python3 tests/conformance/abi/s8_interop_cases.py --check
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

CASE_ID = "TC-ABI-S8-0010"
CLANG = shutil.which("clang")
CPREFIX = r"""#include <stdint.h>
extern int tc_rt_write_scalar(uint32_t tag, uint32_t newline, uint64_t bits);
static void pi(uint64_t value) { (void)tc_rt_write_scalar(3, 1, value); }
static uint64_t db(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}
static uint64_t fb(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return (uint64_t)bits.u;
}
"""
ROOT_TC = "#program\nimport lib\n_ = writeln int64, 0\n"


def f64bits(value: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def f32bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


CASES: list[tuple[str, str, str, str]] = [
    (
        "packed composite argument in x0/x1",
        """#lib
public struct Pair then
var a: int64
var b: int64
end
public func addpair(p: Pair, x: int64) int64 then
var r: int64 = add int64, p.a, p.b
var s: int64 = add int64, r, x
_ = return s
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Pair;
extern uint64_t tc_test_call(Pair, int64_t);
void tc_main(void) { Pair p; p.a = 7; p.b = 35; pi(tc_test_call(p, 100)); }
""",
        "142\n",
    ),
    (
        "24-byte composite argument on the stack",
        """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func bigsum(b: Big, x: int64) int64 then
var r: int64 = add int64, b.a, b.b
var s: int64 = add int64, r, b.c
var t: int64 = add int64, s, x
_ = return t
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; int64_t c; } Big;
extern uint64_t tc_test_call(Big, int64_t);
void tc_main(void) { Big b; b.a = 10; b.b = 20; b.c = 30; pi(tc_test_call(b, 4)); }
""",
        "64\n",
    ),
    (
        "HFA2 argument and floating return in v registers",
        """#lib
public struct H then
var x: float64
var y: float64
end
public func hsum(h: H, z: float64) float64 then
var a: float64 = add float64, h.x, h.y
var b: float64 = add float64, a, z
_ = return b
end
""",
        """typedef struct { double x; double y; } H;
extern double tc_test_call(H, double);
void tc_main(void) { H h; h.x = 1.5; h.y = 2.25; pi(db(tc_test_call(h, 3.75))); }
""",
        f"{f64bits(7.5)}\n",
    ),
    (
        "HFA3 float32 argument and float32 return",
        """#lib
public struct Q then
var a: float32
var b: float32
var c: float32
end
public func hsum(q: Q) float32 then
var s: float32 = add float32, q.a, q.b
var t: float32 = add float32, s, q.c
_ = return t
end
""",
        """typedef struct { float a; float b; float c; } Q;
extern float tc_test_call(Q);
void tc_main(void) { Q q; q.a = 1.5f; q.b = 2.5f; q.c = 3.5f; pi(fb(tc_test_call(q))); }
""",
        f"{f32bits(7.5)}\n",
    ),
    (
        "HFA4 float64 argument",
        """#lib
public struct D4 then
var a: float64
var b: float64
var c: float64
var d: float64
end
public func hsum(x: D4) float64 then
var s: float64 = add float64, x.a, x.b
var t: float64 = add float64, s, x.c
var u: float64 = add float64, t, x.d
_ = return u
end
""",
        """typedef struct { double a; double b; double c; double d; } D4;
extern double tc_test_call(D4);
void tc_main(void) { D4 x; x.a = 1.0; x.b = 2.0; x.c = 3.0; x.d = 4.0; pi(db(tc_test_call(x))); }
""",
        f"{f64bits(10.0)}\n",
    ),
    (
        "five-float composite is not an HFA and goes on the stack",
        """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func fsum(x: F5) float64 then
var s: float64 = cast float64, x.a
var s1: float64 = cast float64, x.b
var s2: float64 = cast float64, x.c
var s3: float64 = cast float64, x.d
var s4: float64 = cast float64, x.e
var t1: float64 = add float64, s, s1
var t2: float64 = add float64, t1, s2
var t3: float64 = add float64, t2, s3
var t4: float64 = add float64, t3, s4
_ = return t4
end
""",
        """typedef struct { float a; float b; float c; float d; float e; } F5;
extern double tc_test_call(F5);
void tc_main(void) { F5 x; x.a = 1.0f; x.b = 2.0f; x.c = 3.0f; x.d = 4.0f; x.e = 5.0f; pi(db(tc_test_call(x))); }
""",
        f"{f64bits(15.0)}\n",
    ),
    (
        "mixed float/int 16-byte composite uses x0/x1",
        """#lib
public struct M then
var a: float64
var b: int32
var c: int32
end
public func msum(m: M) int64 then
var af: int64 = cast int64, m.a
var bf: int64 = cast int64, m.b
var cf: int64 = cast int64, m.c
var r: int64 = add int64, af, bf
var s: int64 = add int64, r, cf
_ = return s
end
""",
        """typedef struct __attribute__((packed)) { double a; int32_t b; int32_t c; } M;
extern uint64_t tc_test_call(M);
void tc_main(void) { M m; m.a = 1.5; m.b = 2; m.c = 3; pi(tc_test_call(m)); }
""",
        "6\n",
    ),
    (
        "mixed float32/float64 12-byte composite is not an HFA",
        """#lib
public struct N then
var a: float32
var b: float64
end
public func nsum(n: N) float64 then
var a: float64 = cast float64, n.a
var s: float64 = add float64, a, n.b
_ = return s
end
""",
        """typedef struct __attribute__((packed)) { float a; double b; } N;
extern double tc_test_call(N);
void tc_main(void) { N n; n.a = 1.5f; n.b = 2.25; pi(db(tc_test_call(n))); }
""",
        f"{f64bits(3.75)}\n",
    ),
    (
        "bool and narrow integer argument extension",
        """#lib
public func narrow(b: bool, s: int8, u: uint8) int64 then
var bi: int64 = cast int64, b
var si: int64 = cast int64, s
var ui: int64 = cast int64, u
var r: int64 = add int64, bi, si
var t: int64 = add int64, r, ui
_ = return t
end
""",
        """extern uint64_t tc_test_call(_Bool, int8_t, uint8_t);
void tc_main(void) { pi(tc_test_call(1, -5, 250)); }
""",
        "246\n",
    ),
    (
        "narrow signed return value",
        """#lib
public func narrowret(x: int64) int8 then
var r: int8 = cast int8, x
_ = return r
end
""",
        """extern int8_t tc_test_call(int64_t);
void tc_main(void) { pi((uint64_t)(int64_t)tc_test_call(-100)); }
""",
        "-100\n",
    ),
    (
        "16-byte composite return in x0/x1",
        """#lib
public struct Pair then
var a: int64
var b: int64
end
public func mkpair(a: int64, b: int64) Pair then
var p: Pair = make Pair, a, b
_ = return p
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Pair;
extern Pair tc_test_call(int64_t, int64_t);
void tc_main(void) { Pair p = tc_test_call(7, 9); pi((uint64_t)p.a); pi((uint64_t)p.b); }
""",
        "7\n9\n",
    ),
    (
        "24-byte composite return through x8",
        """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func mkbig(a: int64, b: int64, c: int64) Big then
var x: Big = make Big, a, b, c
_ = return x
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; int64_t c; } Big;
extern Big tc_test_call(int64_t, int64_t, int64_t);
void tc_main(void) { Big b = tc_test_call(10, 20, 30); pi((uint64_t)b.a); pi((uint64_t)b.b); pi((uint64_t)b.c); }
""",
        "10\n20\n30\n",
    ),
    (
        "HFA2 return in v0/v1",
        """#lib
public struct H then
var x: float64
var y: float64
end
public func mkh(a: float64, b: float64) H then
var h: H = make H, a, b
_ = return h
end
""",
        """typedef struct { double x; double y; } H;
extern H tc_test_call(double, double);
void tc_main(void) { H h = tc_test_call(1.5, 2.25); pi(db(h.x)); pi(db(h.y)); }
""",
        f"{f64bits(1.5)}\n{f64bits(2.25)}\n",
    ),
    (
        "HFA3 float32 return in v0-v2",
        """#lib
public struct Q then
var a: float32
var b: float32
var c: float32
end
public func mkq() Q then
var q: Q = make Q, 1.5f, 2.5f, 3.5f
_ = return q
end
""",
        """typedef struct { float a; float b; float c; } Q;
extern Q tc_test_call(void);
void tc_main(void) { Q q = tc_test_call(); pi(fb(q.a)); pi(fb(q.b)); pi(fb(q.c)); }
""",
        f"{f32bits(1.5)}\n{f32bits(2.5)}\n{f32bits(3.5)}\n",
    ),
    (
        "mixed register classes with a stack composite",
        """#lib
public struct Pair then
var a: int64
var b: int64
end
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func many(a: int64, b: float64, p: Pair, c: int8, d: float64, g: Big, e: int64) int64 then
var bi: int64 = cast int64, b
var ci: int64 = cast int64, c
var di: int64 = cast int64, d
var r: int64 = add int64, a, bi
var r1: int64 = add int64, r, p.a
var r2: int64 = add int64, r1, p.b
var r3: int64 = add int64, r2, ci
var r4: int64 = add int64, r3, di
var r5: int64 = add int64, r4, g.a
var r6: int64 = add int64, r5, g.b
var r7: int64 = add int64, r6, g.c
var r8: int64 = add int64, r7, e
_ = return r8
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Pair;
typedef struct __attribute__((packed)) { int64_t a; int64_t b; int64_t c; } Big;
extern uint64_t tc_test_call(int64_t, double, Pair, int8_t, double, Big, int64_t);
void tc_main(void) {
    Pair p; p.a = 3; p.b = 4;
    Big g; g.a = 7; g.b = 8; g.c = 9;
    pi(tc_test_call(1, 2.5, p, -5, 6.5, g, 10));
}
""",
        "45\n",
    ),
    (
        "five-float stack argument followed by a register integer",
        """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func f5then(x: F5, n: int64) float64 then
var s: float64 = cast float64, x.a
var s1: float64 = cast float64, x.b
var s2: float64 = cast float64, x.c
var s3: float64 = cast float64, x.d
var s4: float64 = cast float64, x.e
var t1: float64 = add float64, s, s1
var t2: float64 = add float64, t1, s2
var t3: float64 = add float64, t2, s3
var t4: float64 = add float64, t3, s4
var ni: float64 = cast float64, n
var t5: float64 = add float64, t4, ni
_ = return t5
end
""",
        """typedef struct { float a; float b; float c; float d; float e; } F5;
extern double tc_test_call(F5, int64_t);
void tc_main(void) { F5 x; x.a = 1.0f; x.b = 2.0f; x.c = 3.0f; x.d = 4.0f; x.e = 5.0f; pi(db(tc_test_call(x, 2))); }
""",
        f"{f64bits(17.0)}\n",
    ),
    (
        "20-byte non-HFA composite return through x8",
        """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func mkf5() F5 then
var x: F5 = make F5, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f
_ = return x
end
""",
        """typedef struct { float a; float b; float c; float d; float e; } F5;
extern F5 tc_test_call(void);
void tc_main(void) { F5 x = tc_test_call(); pi(fb(x.a)); pi(fb(x.e)); }
""",
        f"{f32bits(1.5)}\n{f32bits(5.5)}\n",
    ),
    (
        "17-byte composite is passed by reference in the next x register",
        """#lib
public struct S17 then
var a: int64
var b: int8
var c: int64
end
public func s17sum(s: S17, x: int64) int64 then
var bi: int64 = cast int64, s.b
var r: int64 = add int64, s.a, bi
var t: int64 = add int64, r, s.c
var u: int64 = add int64, t, x
_ = return u
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int8_t b; int64_t c; } S17;
extern uint64_t tc_test_call(S17, int64_t);
void tc_main(void) { S17 s; s.a = 10; s.b = -3; s.c = 20; pi(tc_test_call(s, 5)); }
""",
        "32\n",
    ),
    (
        "24-byte mixed composite is passed by reference, not as an HFA",
        """#lib
public struct G then
var a: int64
var f1: float64
var f2: float64
end
public func gsum(g: G, x: int64) int64 then
var f1: int64 = cast int64, g.f1
var f2: int64 = cast int64, g.f2
var r: int64 = add int64, g.a, f1
var t: int64 = add int64, r, f2
var u: int64 = add int64, t, x
_ = return u
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; double f1; double f2; } G;
extern uint64_t tc_test_call(G, int64_t);
void tc_main(void) { G g; g.a = 1; g.f1 = 2.5; g.f2 = 3.5; pi(tc_test_call(g, 4)); }
""",
        "10\n",
    ),
    (
        "exhausted GP registers put the composite pointer and later arguments on the stack",
        """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func manygp(a0: int64, a1: int64, a2: int64, a3: int64, a4: int64, a5: int64, a6: int64, a7: int64, b: Big, c: int64) int64 then
var s: int64 = add int64, a0, a1
var s1: int64 = add int64, s, a2
var s2: int64 = add int64, s1, a3
var s3: int64 = add int64, s2, a4
var s4: int64 = add int64, s3, a5
var s5: int64 = add int64, s4, a6
var s6: int64 = add int64, s5, a7
var s7: int64 = add int64, s6, b.a
var s8: int64 = add int64, s7, b.b
var s9: int64 = add int64, s8, b.c
var s10: int64 = add int64, s9, c
_ = return s10
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; int64_t c; } Big;
extern uint64_t tc_test_call(int64_t, int64_t, int64_t, int64_t, int64_t,
                             int64_t, int64_t, int64_t, Big, int64_t);
void tc_main(void) {
    Big b; b.a = 10; b.b = 20; b.c = 30;
    pi(tc_test_call(1, 2, 3, 4, 5, 6, 7, 8, b, 9));
}
""",
        "105\n",
    ),
    (
        "three-double HFA is still passed by value in v registers",
        """#lib
public struct H3 then
var a: float64
var b: float64
var c: float64
end
public func h3sum(h: H3) float64 then
var s: float64 = add float64, h.a, h.b
var t: float64 = add float64, s, h.c
_ = return t
end
""",
        """typedef struct { double a; double b; double c; } H3;
extern double tc_test_call(H3);
void tc_main(void) { H3 h; h.a = 1.0; h.b = 2.0; h.c = 3.0; pi(db(tc_test_call(h))); }
""",
        f"{f64bits(6.0)}\n",
    ),
    (
        "17-byte composite return uses the x8 indirect buffer",
        """#lib
public struct S17 then
var a: int64
var b: int8
var c: int64
end
public func mk17() S17 then
var s: S17 = make S17, 10, -3, 20
_ = return s
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int8_t b; int64_t c; } S17;
extern S17 tc_test_call(void);
void tc_main(void) { S17 s = tc_test_call(); pi((uint64_t)s.a); pi((uint64_t)(int64_t)s.b); pi((uint64_t)s.c); }
""",
        "10\n-3\n20\n",
    ),
    (
        "72-byte composite argument is copied and passed by reference",
        """#lib
public struct R then
var a: int64 @padding(56)
var b: int64
end
public func rsum(r: R, x: int64) int64 then
var s: int64 = add int64, r.a, r.b
var t: int64 = add int64, s, x
_ = return t
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; unsigned char pad[56]; int64_t b; } R;
extern uint64_t tc_test_call(R, int64_t);
void tc_main(void) {
    R r;
    r.a = 100;
    for (int i = 0; i < 56; i++) r.pad[i] = 0;
    r.b = 200;
    pi(tc_test_call(r, 7));
}
""",
        "307\n",
    ),
    (
        "HFA2 spills to the stack after all v registers are used",
        """#lib
public struct H then
var x: float64
var y: float64
end
public func hfastack(a0: float64, a1: float64, a2: float64, a3: float64, a4: float64, a5: float64, a6: float64, a7: float64, h: H) float64 then
var s: float64 = add float64, a0, a1
var s1: float64 = add float64, s, a2
var s2: float64 = add float64, s1, a3
var s3: float64 = add float64, s2, a4
var s4: float64 = add float64, s3, a5
var s5: float64 = add float64, s4, a6
var s6: float64 = add float64, s5, a7
var s7: float64 = add float64, s6, h.x
var s8: float64 = add float64, s7, h.y
_ = return s8
end
""",
        """typedef struct { double x; double y; } H;
extern double tc_test_call(double, double, double, double, double, double,
                           double, double, H);
void tc_main(void) {
    H h; h.x = 1.5; h.y = 2.5;
    pi(db(tc_test_call(1, 2, 3, 4, 5, 6, 7, 8, h)));
}
""",
        f"{f64bits(40.0)}\n",
    ),
    (
        "16-byte composite spills to the stack after all x registers are used",
        """#lib
public struct Pair then
var a: int64
var b: int64
end
public func smallstack(a0: int64, a1: int64, a2: int64, a3: int64, a4: int64, a5: int64, a6: int64, a7: int64, p: Pair) int64 then
var s: int64 = add int64, a0, a1
var s1: int64 = add int64, s, a2
var s2: int64 = add int64, s1, a3
var s3: int64 = add int64, s2, a4
var s4: int64 = add int64, s3, a5
var s5: int64 = add int64, s4, a6
var s6: int64 = add int64, s5, a7
var s7: int64 = add int64, s6, p.a
var s8: int64 = add int64, s7, p.b
_ = return s8
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Pair;
extern uint64_t tc_test_call(int64_t, int64_t, int64_t, int64_t, int64_t,
                             int64_t, int64_t, int64_t, Pair);
void tc_main(void) { Pair p; p.a = 3; p.b = 4; pi(tc_test_call(1,2,3,4,5,6,7,8,p)); }
""",
        "43\n",
    ),
    (
        "HFA4 return in v0-v3",
        """#lib
public struct D4 then
var a: float64
var b: float64
var c: float64
var d: float64
end
public func mkd4() D4 then
var x: D4 = make D4, 1.0, 2.0, 3.0, 4.0
_ = return x
end
""",
        """typedef struct { double a; double b; double c; double d; } D4;
extern D4 tc_test_call(void);
void tc_main(void) { D4 x = tc_test_call(); pi(db(x.a)); pi(db(x.b)); pi(db(x.c)); pi(db(x.d)); }
""",
        f"{f64bits(1.0)}\n{f64bits(2.0)}\n{f64bits(3.0)}\n{f64bits(4.0)}\n",
    ),
    (
        "int16/uint16 argument extension",
        """#lib
public func narrow16(a: int16, b: uint16, c: int8, d: uint8) int64 then
var ai: int64 = cast int64, a
var bi: int64 = cast int64, b
var ci: int64 = cast int64, c
var di: int64 = cast int64, d
var r: int64 = add int64, ai, bi
var s: int64 = add int64, r, ci
var t: int64 = add int64, s, di
_ = return t
end
""",
        """extern uint64_t tc_test_call(int16_t, uint16_t, int8_t, uint8_t);
void tc_main(void) { pi(tc_test_call(-1000, 60000, -5, 250)); }
""",
        "59245\n",
    ),
    (
        "pointer argument spills while later composites use the stack",
        """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func ptrspill(a0: int64, a1: int64, a2: int64, a3: int64, a4: int64, a5: int64, a6: int64, b: Big, c: int64, d: Big) int64 then
var s: int64 = add int64, a0, a1
var s1: int64 = add int64, s, a2
var s2: int64 = add int64, s1, a3
var s3: int64 = add int64, s2, a4
var s4: int64 = add int64, s3, a5
var s5: int64 = add int64, s4, a6
var s6: int64 = add int64, s5, b.a
var s7: int64 = add int64, s6, b.b
var s8: int64 = add int64, s7, b.c
var s9: int64 = add int64, s8, c
var s10: int64 = add int64, s9, d.a
var s11: int64 = add int64, s10, d.b
var s12: int64 = add int64, s11, d.c
_ = return s12
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; int64_t c; } Big;
extern uint64_t tc_test_call(int64_t, int64_t, int64_t, int64_t, int64_t,
                             int64_t, int64_t, Big, int64_t, Big);
void tc_main(void) {
    Big b; b.a = 1; b.b = 2; b.c = 3;
    Big d; d.a = 4; d.b = 5; d.c = 6;
    pi(tc_test_call(1, 1, 1, 1, 1, 1, 1, b, 10, d));
}
""",
        "38\n",
    ),
    (
        "mixed 16-byte composite return in x0/x1",
        """#lib
public struct M then
var a: float64
var b: int32
var c: int32
end
public func mkm(x: int64) M then
var af: float64 = cast float64, x
var ah: float64 = add float64, af, 0.5
var bi: int32 = cast int32, x
var b1: int32 = add int32, bi, 1
var c1: int32 = add int32, bi, 2
var m: M = make M, ah, b1, c1
_ = return m
end
""",
        """typedef struct __attribute__((packed)) { double a; int32_t b; int32_t c; } M;
extern M tc_test_call(int64_t);
void tc_main(void) { M m = tc_test_call(3); pi(db(m.a)); pi((uint64_t)(int64_t)m.b); pi((uint64_t)(int64_t)m.c); }
""",
        f"{f64bits(3.5)}\n4\n5\n",
    ),
    (
        "single-float HFA argument and return in s0",
        """#lib
public struct F1 then
var a: float32
end
public func id1(x: F1) F1 then
_ = return x
end
""",
        """typedef struct { float a; } F1;
extern F1 tc_test_call(F1);
void tc_main(void) { F1 x; x.a = 2.5f; pi(fb(tc_test_call(x).a)); }
""",
        f"{f32bits(2.5)}\n",
    ),
    (
        "float32 scalar argument and return",
        """#lib
public func bump(x: float32) float32 then
var one: float32 = copy float32, 1.0f
var r: float32 = add float32, x, one
_ = return r
end
""",
        """extern float tc_test_call(float);
void tc_main(void) { pi(fb(tc_test_call(1.5f))); }
""",
        f"{f32bits(2.5)}\n",
    ),
    (
        "bool return value",
        """#lib
public func negative(x: int32) bool then
var z: int32 = copy int32, 0
var r: bool = lt int32, x, z
_ = return r
end
""",
        """extern _Bool tc_test_call(int32_t);
void tc_main(void) { pi(tc_test_call(-1) ? 1u : 0u); }
""",
        "1\n",
    ),
    (
        "uint16 return value",
        """#lib
public func wide16(x: int64) uint16 then
var r: uint16 = trunc uint16, x
_ = return r
end
""",
        """extern uint16_t tc_test_call(int64_t);
void tc_main(void) { pi((uint64_t)tc_test_call(0x1ffff)); }
""",
        "65535\n",
    ),
]


def runtime_archive() -> Path:
    driver = Path(engine.tc_aot_path()).resolve()
    return driver.parent.parent / "aarch64-runtime" / "libtc_aarch64_runtime.a"


def build_and_run(lib: str, harness: str, directory: Path) -> tuple[int, bytes, str]:
    (directory / "lib.tc").write_text(lib, encoding="utf-8")
    (directory / "root.tc").write_text(ROOT_TC, encoding="utf-8")
    assembly = directory / "gen.s"
    proc = subprocess.run(
        [engine.tc_aot_path(), "--emit=asm", f"--output={assembly}",
         "--profile=aarch64-linux-user", f"--include={directory}",
         str(directory / "root.tc")],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if proc.returncode != 0:
        return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
    text = assembly.read_text(encoding="utf-8").replace(
        "tc_main", "tc_program_main")
    text += """
  .text
  .globl tc_test_call
  .type tc_test_call, %function
tc_test_call:
  b tc_function_0
  .size tc_test_call, .-tc_test_call
"""
    assembly.write_text(text, encoding="utf-8")
    (directory / "harness.c").write_text(CPREFIX + harness, encoding="utf-8")
    flags = ["-target", "aarch64-none-elf", "-ffreestanding", "-nostdlib",
             "-march=armv8-a", "-O2", "-mstrict-align",
             "-Wno-unused-command-line-argument"]
    objects = []
    for source, name in ((assembly, "gen.o"), (directory / "harness.c", "h.o")):
        obj = directory / name
        proc = subprocess.run([CLANG, *flags, "-c", str(source), "-o", str(obj)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
        objects.append(obj)
    elf = directory / "out.elf"
    proc = subprocess.run(
        [engine.lld_path(), "-T", str(ROOT / "src/backend/aarch64/link.ld"),
         "--icf=none", *[str(obj) for obj in objects],
         str(runtime_archive()), "-o", str(elf)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
    image = directory / "stdin.img"
    image.write_bytes(struct.pack("<Q", 0))
    diag = directory / "diag.txt"
    proc = subprocess.run(
        [engine.qemu_path(), "-M", "virt", "-cpu", "cortex-a53", "-m", "256M",
         "-display", "none", "-monitor", "none",
         "-semihosting-config", "enable=on,target=native,chardev=semio",
         "-chardev", "stdio,id=semio", "-serial", f"file:{diag}",
         "-device", f"loader,file={image},addr=0x41000000,force-raw=on",
         "-kernel", str(elf)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    text = diag.read_text(encoding="utf-8", errors="replace") if diag.exists() else ""
    return proc.returncode, proc.stdout, text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if CLANG is None:
        print(f"{CASE_ID}: clang not found")
        return 1
    archive = runtime_archive()
    if not archive.exists():
        print(f"{CASE_ID}: runtime archive not found at {archive}")
        return 1
    failures = 0
    for name, lib, harness, expected in CASES:
        directory = Path(tempfile.mkdtemp(prefix="tc-interop-"))
        try:
            code, stdout, diagnostic = build_and_run(lib, harness, directory)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"{CASE_ID}: {name}: {exc}")
            failures += 1
            continue
        actual = stdout.decode("utf-8", "replace")
        if actual != expected:
            failures += 1
            print(f"{CASE_ID}: {name}: stdout {actual!r} != {expected!r}")
            print(f"  exit {code}; diagnostic {diagnostic!r}")
        elif "none" not in diagnostic:
            failures += 1
            print(f"{CASE_ID}: {name}: exit record {diagnostic!r}")
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        print(f"{CASE_ID}: {failures} of {len(CASES)} cases failed")
        return 1
    print(f"{CASE_ID}: {len(CASES)} C-caller/AArch64-callee AAPCS64 cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
