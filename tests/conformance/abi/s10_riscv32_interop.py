#!/usr/bin/env python3
"""S10 RISC-V ILP32 interoperability audit: C callers calling TC callees.

Each case compiles a TC library function to RISC-V assembly, appends a global
``tc_test_call`` thunk that jumps to ``tc_function_0``, compiles a freestanding
C harness that declares the same signature, links both with the RISC-V runtime
and runs the image on qemu-system-riscv32 virt.  The C compiler is an
independent ILP32 implementation, so this checks the generated callee against
the published base integer calling convention (register pairs for int64
scalars, two-word aggregates in registers, larger aggregates by reference and
hidden indirect returns).

Run:
    python3 tests/conformance/abi/s10_riscv32_interop.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-ABI-S10-0001"
PROFILE = "riscv32-baremetal"


def f32bits(value: float) -> int:
    import struct
    return struct.unpack("<I", struct.pack("<f", value))[0]


EXTRA_CASES = [
    (
        "seven integers and a split int64 pair",
        """#lib
public func f(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, x: int64) int64 then
var s: int64 = cast int64, a
var t: int64 = cast int64, b
var u: int64 = add int64, s, t
var v: int64 = cast int64, c
var w: int64 = add int64, u, v
var q: int64 = cast int64, d
var r: int64 = add int64, w, q
var m: int64 = cast int64, e
var n: int64 = add int64, r, m
var o: int64 = cast int64, f7
var p: int64 = add int64, n, o
var t2: int64 = cast int64, g
var u2: int64 = add int64, p, t2
var z: int64 = add int64, u2, x
_ = return z
end
""",
        """extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, int64_t);
void tc_main(void) { pi((uint64_t)tc_test_call(1, 2, 3, 4, 5, 6, 7,
    1000000000LL)); }
""",
        "1000000028\n",
    ),
    (
        "eight integers and an int64 pair on the stack",
        """#lib
public func f(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, h: int32, x: int64) int64 then
var s: int64 = cast int64, a
var t: int64 = cast int64, b
var u: int64 = add int64, s, t
var v: int64 = cast int64, c
var w: int64 = add int64, u, v
var q: int64 = cast int64, d
var r: int64 = add int64, w, q
var m: int64 = cast int64, e
var n: int64 = add int64, r, m
var o: int64 = cast int64, f7
var p: int64 = add int64, n, o
var t2: int64 = cast int64, g
var u2: int64 = add int64, p, t2
var t3: int64 = cast int64, h
var u3: int64 = add int64, u2, t3
var z: int64 = add int64, u3, x
_ = return z
end
""",
        """extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, int32_t, int64_t);
void tc_main(void) { pi((uint64_t)tc_test_call(1, 2, 3, 4, 5, 6, 7, 8,
    1000000000LL)); }
""",
        "1000000036\n",
    ),
    (
        "seven integers and an eight-byte packed composite on the stack",
        """#lib
public struct Pair then
var x: int32
var y: int32
end
public func f(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, p: Pair) int32 then
var s: int32 = add int32, a, b
var t: int32 = add int32, s, c
var u: int32 = add int32, t, d
var v: int32 = add int32, u, e
var w: int32 = add int32, v, f7
var q: int32 = add int32, w, g
var r: int32 = add int32, q, p.x
var z: int32 = add int32, r, p.y
_ = return z
end
""",
        """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern int32_t tc_test_call(int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, Pair);
void tc_main(void) { Pair p; p.x = 10; p.y = 20;
    pi((uint64_t)(int64_t)tc_test_call(1, 2, 3, 4, 5, 6, 7, p)); }
""",
        "58\n",
    ),
    (
        "eight integers and a twelve-byte packed composite by reference",
        """#lib
public struct Big then
var a: int32
var b: int64
end
public func f(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, h: int32, p: Big) int64 then
var s: int64 = cast int64, a
var t: int64 = cast int64, b
var u: int64 = add int64, s, t
var v: int64 = cast int64, c
var w: int64 = add int64, u, v
var q: int64 = cast int64, d
var r: int64 = add int64, w, q
var m: int64 = cast int64, e
var n: int64 = add int64, r, m
var o: int64 = cast int64, f7
var s2: int64 = add int64, n, o
var t2: int64 = cast int64, g
var u2: int64 = add int64, s2, t2
var v2: int64 = cast int64, h
var w2: int64 = add int64, u2, v2
var x: int64 = cast int64, p.a
var y: int64 = add int64, w2, x
var z: int64 = add int64, y, p.b
_ = return z
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Big;
extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t, int32_t,
                            int32_t, int32_t, int32_t, Big);
void tc_main(void) { Big b; b.a = 4; b.b = 30;
    pi((uint64_t)tc_test_call(1, 2, 3, 4, 5, 6, 7, 8, b)); }
""",
        "70\n",
    ),
    (
        "five-byte packed composite return in a register pair",
        """#lib
public struct S5 then
var a: int32
var b: int8
end
public func f(x: int32) S5 then
var r: S5 = make S5, x, 7
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int8_t b; } S5;
extern S5 tc_test_call(int32_t);
void tc_main(void) { S5 r = tc_test_call(1234); pi((uint64_t)r.a);
    pi((uint64_t)(int64_t)r.b); }
""",
        "1234\n7\n",
    ),
    (
        "eight-byte packed composite return in a register pair",
        """#lib
public struct Pair then
var x: int32
var y: int32
end
public func f(x: int32) Pair then
var y: int32 = add int32, x, 1
var r: Pair = make Pair, x, y
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern Pair tc_test_call(int32_t);
void tc_main(void) { Pair r = tc_test_call(1234); pi((uint64_t)r.x);
    pi((uint64_t)r.y); }
""",
        "1234\n1235\n",
    ),
    (
        "two-float composite argument in a register pair",
        """#lib
public struct F2 then
var a: float32
var b: float32
end
public func f(p: F2) float32 then
var r: float32 = add float32, p.a, p.b
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { float a; float b; } F2;
extern float tc_test_call(F2);
void tc_main(void) { F2 p; p.a = 1.5f; p.b = 2.5f;
    pi(fb(tc_test_call(p))); }
""",
        f"{f32bits(4.0)}\n",
    ),
    (
        "two-float composite return in a register pair",
        """#lib
public struct F2 then
var a: float32
var b: float32
end
public func f(x: float32) F2 then
var y: float32 = add float32, x, 1.0f
var r: F2 = make F2, x, y
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { float a; float b; } F2;
extern F2 tc_test_call(float);
void tc_main(void) { F2 r = tc_test_call(1.5f); pi(fb(r.a)); pi(fb(r.b)); }
""",
        f"{f32bits(1.5)}\n{f32bits(2.5)}\n",
    ),
    (
        "C callback through a function pointer",
        """#lib
public @unsafe func f(fp: fnptr<int64, int64>, x: int64) int64 then
var r: int64 = call_indirect fp, x
_ = return r
end
""",
        """typedef int64_t (*FP)(int64_t);
static int64_t cb(int64_t x) { return x + 5; }
extern int64_t tc_test_call(FP, int64_t);
void tc_main(void) { pi((uint64_t)tc_test_call(cb, 37)); }
""",
        "42\n",
    ),
    (
        "twenty integers, a stack composite and an int64 pair",
        """#lib
public struct Pair then
var x: int32
var y: int32
end
public func f(p0: int32, p1: int32, p2: int32, p3: int32, p4: int32, p5: int32, p6: int32, p7: int32, p8: int32, p9: int32, p10: int32, p11: int32, p12: int32, p13: int32, p14: int32, p15: int32, p16: int32, p17: int32, p18: int32, p19: int32, p: Pair, x: int64) int64 then
var s: int64 = cast int64, p0
var t0: int64 = cast int64, p1
var s1: int64 = add int64, s, t0
var t1: int64 = cast int64, p2
var s2: int64 = add int64, s1, t1
var t2: int64 = cast int64, p3
var s3: int64 = add int64, s2, t2
var t3: int64 = cast int64, p4
var s4: int64 = add int64, s3, t3
var t4: int64 = cast int64, p5
var s5: int64 = add int64, s4, t4
var t5: int64 = cast int64, p6
var s6: int64 = add int64, s5, t5
var t6: int64 = cast int64, p7
var s7: int64 = add int64, s6, t6
var t7: int64 = cast int64, p8
var s8: int64 = add int64, s7, t7
var t8: int64 = cast int64, p9
var s9: int64 = add int64, s8, t8
var t9: int64 = cast int64, p10
var s10: int64 = add int64, s9, t9
var t10: int64 = cast int64, p11
var s11: int64 = add int64, s10, t10
var t11: int64 = cast int64, p12
var s12: int64 = add int64, s11, t11
var t12: int64 = cast int64, p13
var s13: int64 = add int64, s12, t12
var t13: int64 = cast int64, p14
var s14: int64 = add int64, s13, t13
var t14: int64 = cast int64, p15
var s15: int64 = add int64, s14, t14
var t15: int64 = cast int64, p16
var s16: int64 = add int64, s15, t15
var t16: int64 = cast int64, p17
var s17: int64 = add int64, s16, t16
var t17: int64 = cast int64, p18
var s18: int64 = add int64, s17, t17
var t18: int64 = cast int64, p19
var s19: int64 = add int64, s18, t18
var px: int64 = cast int64, p.x
var s20: int64 = add int64, s19, px
var py: int64 = cast int64, p.y
var s21: int64 = add int64, s20, py
var r: int64 = add int64, s21, x
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, Pair,
    int64_t);
void tc_main(void) { Pair p; p.x = 10; p.y = 20;
    pi((uint64_t)tc_test_call(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, p, 1000000000LL)); }
""",
        "1000000240\n",
    ),
    (
        "nested packed composite argument",
        """#lib
public struct Inner then
var a: int32
var b: int64
end
public struct Outer then
var x: Inner
var c: uint8
end
public func f(p: Outer) int64 then
var s: int64 = cast int64, p.x.a
var t: int64 = add int64, s, p.x.b
var u: int64 = cast int64, p.c
var r: int64 = add int64, t, u
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Inner;
typedef struct __attribute__((packed)) { Inner x; uint8_t c; } Outer;
extern int64_t tc_test_call(Outer);
void tc_main(void) { Outer o; o.x.a = 3; o.x.b = 100; o.c = 7;
    pi((uint64_t)tc_test_call(o)); }
""",
        "110\n",
    ),
    (
        "nested packed composite return",
        """#lib
public struct Inner then
var a: int32
var b: int64
end
public struct Outer then
var x: Inner
var c: uint8
end
public func f() Outer then
var i: Inner = make Inner, 3, 100
var o: Outer = make Outer, i, 7
_ = return o
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Inner;
typedef struct __attribute__((packed)) { Inner x; uint8_t c; } Outer;
extern Outer tc_test_call(void);
void tc_main(void) { Outer o = tc_test_call(); pi((uint64_t)(int64_t)o.x.a);
    pi((uint64_t)o.x.b); pi((uint64_t)o.c); }
""",
        "3\n100\n7\n",
    ),
    (
        "sret with seven integers and a stack int64 pair",
        """#lib
public struct Big then
var a: int64
var b: int64
end
public func f(p0: int32, p1: int32, p2: int32, p3: int32, p4: int32, p5: int32, p6: int32, p7: int64) Big then
var r: Big = make Big, p7, 42
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Big;
extern Big tc_test_call(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, int64_t);
void tc_main(void) { Big r = tc_test_call(1, 2, 3, 4, 5, 6, 7, 1000000000LL);
    pi((uint64_t)r.a); pi((uint64_t)r.b); }
""",
        "1000000000\n42\n",
    ),
    (
        "sret with six integers and a split int64",
        """#lib
public struct Big then
var a: int64
var b: int64
end
public func f(p0: int32, p1: int32, p2: int32, p3: int32, p4: int32, p5: int32, p6: int64) Big then
var r: Big = make Big, p6, 7
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Big;
extern Big tc_test_call(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                        int64_t);
void tc_main(void) { Big r = tc_test_call(1, 2, 3, 4, 5, 6, 1000000000LL);
    pi((uint64_t)r.a); pi((uint64_t)r.b); }
""",
        "1000000000\n7\n",
    ),
    (
        "signed int8 return extension",
        """#lib
public func f(x: int32) int8 then
var r: int8 = trunc int8, x
_ = return r
end
""",
        """extern int8_t tc_test_call(int32_t);
void tc_main(void) { pi((uint64_t)(int64_t)tc_test_call(-1)); }
""",
        "-1\n",
    ),
    (
        "unsigned uint8 return extension",
        """#lib
public func f(x: int32) uint8 then
var r: uint8 = trunc uint8, x
_ = return r
end
""",
        """extern uint8_t tc_test_call(int32_t);
void tc_main(void) { pi((uint64_t)tc_test_call(255)); }
""",
        "255\n",
    ),
    (
        "signed int16 return extension",
        """#lib
public func f(x: int32) int16 then
var r: int16 = trunc int16, x
_ = return r
end
""",
        """extern int16_t tc_test_call(int32_t);
void tc_main(void) { pi((uint64_t)(int64_t)tc_test_call(-2)); }
""",
        "-2\n",
    ),
    (
        "unsigned uint16 return extension",
        """#lib
public func f(x: int32) uint16 then
var r: uint16 = trunc uint16, x
_ = return r
end
""",
        """extern uint16_t tc_test_call(int32_t);
void tc_main(void) { pi((uint64_t)tc_test_call(65534)); }
""",
        "65534\n",
    ),
]


def riscv_clang() -> str | None:
    for candidate in ("/opt/homebrew/opt/llvm/bin/clang",
                      "/usr/local/opt/llvm/bin/clang"):
        if Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    return shutil.which("clang")


def load_arm32_interop():
    path = ROOT / "tests/conformance/abi/s9_arm32_interop.py"
    spec = importlib.util.spec_from_file_location("s10_interop_cases", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    cases = load_arm32_interop()
    cases.CLANG = riscv_clang()
    if cases.CLANG is None:
        print(f"{CASE_ID}: clang not found")
        return 1
    archive = cases.runtime_archive(PROFILE)
    if not archive.exists():
        print(f"{CASE_ID}: runtime archive not found at {archive}")
        return 1
    failures = 0
    total = 0
    all_cases = list(cases.CASES) + EXTRA_CASES
    for name, lib, harness, expected in all_cases:
        directory = engine.temporary_directory("tc-s10-interop-")
        try:
            code, stdout, diagnostic = cases.build_and_run(
                lib, harness, directory, PROFILE)
        except (OSError, Exception) as exc:  # noqa: BLE001 - report
            print(f"{CASE_ID}: {name}: {exc}")
            failures += 1
            shutil.rmtree(directory, ignore_errors=True)
            continue
        total += 1
        actual = stdout.decode("utf-8", "replace")
        if actual != expected:
            failures += 1
            print(f"{CASE_ID}: {name}: stdout {actual!r} != {expected!r}")
            print(f"  exit {code}; diagnostic {diagnostic!r}")
        elif code != 0 or "none" not in diagnostic:
            failures += 1
            print(f"{CASE_ID}: {name}: exit {code}, record {diagnostic!r}")
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        print(f"{CASE_ID}: {failures} of {total} cases failed")
        return 1
    print(f"{CASE_ID}: {total} C-caller/RISC-V-callee ILP32 ABI cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
