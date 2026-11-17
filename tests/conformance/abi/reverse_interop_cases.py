#!/usr/bin/env python3
"""Reverse ABI interop: TC callers calling independently compiled C callees.

The existing interop suites use C as the caller and TC as the callee, so they
check callee-side classification.  A TC-to-TC call uses the same argument
planner on both sides, which can hide a caller-side classification error.  This
suite compiles the TC *caller* to assembly, redirects its single call to
``tc_function_0`` at a C ``c_callee`` symbol, and checks the published AAPCS
from the C side.

The TC library always has two functions: ``target`` (a never-executed stub) and
``caller`` (forwards its parameters to ``target``).  The test harness rewrites
``bl tc_function_0`` to ``bl c_callee`` and makes ``tc_function_1`` global, so a
C ``tc_main`` can invoke the TC caller.  This is test infrastructure only; the
language has no extern/FFI declaration.

Run:
    python3 tests/conformance/abi/reverse_interop_cases.py --check
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-ABI-REV-0001"
CLANG = shutil.which("clang")

CPREFIX = r"""#include <stdint.h>
#include <stdbool.h>
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

ROOT_TC = "#program\nimport lib\n_ = writeln int32, 0\n"


def f64bits(value: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", value))[0]


CASES: list[dict] = [
    {
        "id": "TC-ABI-REV-0001",
        "profile": "aarch64-baremetal",
        "name": "two integer arguments",
        "lib": """#lib
public func target(a: int32, b: int32) int32 then
var r: int32 = add int32, a, b
_ = return r
end
public func caller(a: int32, b: int32) int32 then
var r: int32 = call Self.target, a, b
_ = return r
end
""",
        "c": """extern int32_t tc_function_1(int32_t, int32_t);
int32_t c_callee(int32_t a, int32_t b) { return a * 100 + b; }
void tc_main(void) { pi((uint64_t)(int64_t)tc_function_1(3, 4)); }
""",
        "out": "304\n",
    },
    {
        "id": "TC-ABI-REV-0002",
        "profile": "aarch64-baremetal",
        "name": "nine integer arguments through the stack",
        "lib": """#lib
public func target(a: int32, b: int32, c: int32, d: int32, e: int32, f: int32, g: int32, h: int32, i: int32) int64 then
var s: int64 = cast int64, a
_ = return s
end
public func caller(a: int32, b: int32, c: int32, d: int32, e: int32, f: int32, g: int32, h: int32, i: int32) int64 then
var s: int64 = call Self.target, a, b, c, d, e, f, g, h, i
_ = return s
end
""",
        "c": """extern int64_t tc_function_1(int32_t, int32_t, int32_t, int32_t,
                             int32_t, int32_t, int32_t, int32_t, int32_t);
int64_t c_callee(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                 int32_t f, int32_t g, int32_t h, int32_t i) {
    return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 +
           h * 8 + i * 9;
}
void tc_main(void) {
    pi((uint64_t)tc_function_1(1, 2, 3, 4, 5, 6, 7, 8, 9));
}
""",
        "out": "285\n",
    },
    {
        "id": "TC-ABI-REV-0003",
        "profile": "aarch64-baremetal",
        "name": "int64 pair arguments",
        "lib": """#lib
public func target(a: int64, b: int64, c: int32) int64 then
var s: int64 = add int64, a, b
_ = return s
end
public func caller(a: int64, b: int64, c: int32) int64 then
var s: int64 = call Self.target, a, b, c
_ = return s
end
""",
        "c": """extern int64_t tc_function_1(int64_t, int64_t, int32_t);
int64_t c_callee(int64_t a, int64_t b, int32_t c) {
    return a + b + (int64_t)c;
}
void tc_main(void) {
    pi((uint64_t)tc_function_1(10000000000LL, 20000000000LL, 3));
}
""",
        "out": "30000000003\n",
    },
    {
        "id": "TC-ABI-REV-0004",
        "profile": "aarch64-baremetal",
        "name": "float64 and float32 scalar arguments",
        "lib": """#lib
public func target(a: float64, b: float32) float64 then
var c: float64 = cast float64, b
var r: float64 = add float64, a, c
_ = return r
end
public func caller(a: float64, b: float32) float64 then
var r: float64 = call Self.target, a, b
_ = return r
end
""",
        "c": """extern double tc_function_1(double, float);
double c_callee(double a, float b) { return a + (double)b; }
void tc_main(void) { pi(db(tc_function_1(1.5, 2.5f))); }
""",
        "out": f"{f64bits(4.0)}\n",
    },
    {
        "id": "TC-ABI-REV-0005",
        "profile": "aarch64-baremetal",
        "name": "HFA2 built by the caller and returned",
        "lib": """#lib
public struct H then
var x: float64
var y: float64
end
public func target(h: H) H then
_ = return h
end
public func caller(x: float64, y: float64) float64 then
var h: H = make H, x, y
var r: H = call Self.target, h
var s: float64 = add float64, r.x, r.y
_ = return s
end
""",
        "c": """typedef struct { double x; double y; } H;
extern double tc_function_1(double, double);
H c_callee(H h) { H r; r.x = h.x + 1.0; r.y = h.y + 2.0; return r; }
void tc_main(void) { pi(db(tc_function_1(1.5, 2.25))); }
""",
        "out": f"{f64bits(6.75)}\n",
    },
    {
        "id": "TC-ABI-REV-0006",
        "profile": "aarch64-baremetal",
        "name": "24-byte composite built by the caller",
        "lib": """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func target(b: Big) int64 then
var s: int64 = add int64, b.a, b.b
var t: int64 = add int64, s, b.c
_ = return t
end
public func caller(a: int64, b: int64, c: int64) int64 then
var x: Big = make Big, a, b, c
var r: int64 = call Self.target, x
_ = return r
end
""",
        "c": """typedef struct __attribute__((packed)) {
    int64_t a; int64_t b; int64_t c;
} Big;
extern int64_t tc_function_1(int64_t, int64_t, int64_t);
int64_t c_callee(Big b) { return b.a + b.b + b.c; }
void tc_main(void) { pi((uint64_t)tc_function_1(10, 20, 30)); }
""",
        "out": "60\n",
    },
    {
        "id": "TC-ABI-REV-0007",
        "profile": "aarch64-baremetal",
        "name": "five-float composite built by the caller",
        "lib": """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func target(f: F5) int32 then
var s: int32 = cast int32, f.a
_ = return s
end
public func caller(a: float32, b: float32, c: float32, d: float32, e: float32) int32 then
var f: F5 = make F5, a, b, c, d, e
var r: int32 = call Self.target, f
_ = return r
end
""",
        "c": """typedef struct { float a; float b; float c; float d; float e; } F5;
extern int32_t tc_function_1(float, float, float, float, float);
int32_t c_callee(F5 f) {
    return (int32_t)f.a + (int32_t)f.b + (int32_t)f.c +
           (int32_t)f.d + (int32_t)f.e;
}
void tc_main(void) { pi((uint64_t)(int64_t)tc_function_1(1, 2, 3, 4, 5)); }
""",
        "out": "15\n",
    },
    {
        "id": "TC-ABI-REV-0008",
        "profile": "aarch64-baremetal",
        "name": "HFA spills after seven float registers",
        "lib": """#lib
public struct H then
var x: float64
var y: float64
end
public func target(a0: float64, a1: float64, a2: float64, a3: float64, a4: float64, a5: float64, a6: float64, h: H) float64 then
var s: float64 = add float64, a0, a1
_ = return s
end
public func caller(a0: float64, a1: float64, a2: float64, a3: float64, a4: float64, a5: float64, a6: float64, x: float64, y: float64) float64 then
var h: H = make H, x, y
var r: float64 = call Self.target, a0, a1, a2, a3, a4, a5, a6, h
_ = return r
end
""",
        "c": """typedef struct { double x; double y; } H;
extern double tc_function_1(double, double, double, double, double, double,
                            double, double, double);
double c_callee(double a0, double a1, double a2, double a3, double a4,
                double a5, double a6, H h) {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + h.x + h.y;
}
void tc_main(void) { pi(db(tc_function_1(1, 2, 3, 4, 5, 6, 7, 8, 9))); }
""",
        "out": f"{f64bits(45.0)}\n",
    },
    {
        "id": "TC-ABI-REV-0009",
        "profile": "aarch64-baremetal",
        "name": "narrow integer and bool arguments and bool return",
        "lib": """#lib
public func target(a: int8, b: uint8, c: int16, d: uint16, e: bool) bool then
_ = return e
end
public func caller(a: int8, b: uint8, c: int16, d: uint16, e: bool) bool then
var r: bool = call Self.target, a, b, c, d, e
_ = return r
end
""",
        "c": """extern bool tc_function_1(int8_t, uint8_t, int16_t, uint16_t, bool);
bool c_callee(int8_t a, uint8_t b, int16_t c, uint16_t d, bool e) {
    return a == -1 && b == 255 && c == -2 && d == 65535 && e;
}
void tc_main(void) {
    pi(tc_function_1(-1, 255, -2, 65535, true) ? 1u : 0u);
}
""",
        "out": "1\n",
    },
    {
        "id": "TC-ABI-REV-0010",
        "profile": "aarch64-baremetal",
        "name": "24-byte composite return through the hidden pointer",
        "lib": """#lib
public struct Big then
var a: int64
var b: int64
var c: int64
end
public func target() Big then
var b: Big = make Big, 0, 0, 0
_ = return b
end
public func caller() int64 then
var r: Big = call Self.target
var s: int64 = add int64, r.a, r.b
var t: int64 = add int64, s, r.c
_ = return t
end
""",
        "c": """typedef struct __attribute__((packed)) {
    int64_t a; int64_t b; int64_t c;
} Big;
extern int64_t tc_function_1(void);
Big c_callee(void) { Big b; b.a = 7; b.b = 8; b.c = 9; return b; }
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "24\n",
    },
    {
        "id": "TC-ABI-REV-0011",
        "profile": "arm32-baremetal",
        "name": "five integer arguments and stack word",
        "lib": """#lib
public func target(a: int32, b: int32, c: int32, d: int32, e: int32) int64 then
var s: int64 = cast int64, a
_ = return s
end
public func caller(a: int32, b: int32, c: int32, d: int32, e: int32) int64 then
var s: int64 = call Self.target, a, b, c, d, e
_ = return s
end
""",
        "c": """extern int64_t tc_function_1(int32_t, int32_t, int32_t, int32_t,
                             int32_t);
int64_t c_callee(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e) {
    return a + b * 2 + c * 3 + d * 4 + e * 5;
}
void tc_main(void) { pi((uint64_t)tc_function_1(1, 2, 3, 4, 5)); }
""",
        "out": "55\n",
    },
    {
        "id": "TC-ABI-REV-0012",
        "profile": "arm32-baremetal",
        "name": "int64 pair arguments and return",
        "lib": """#lib
public func target(a: int64, b: int64, c: int32) int64 then
var s: int64 = add int64, a, b
_ = return s
end
public func caller(a: int64, b: int64, c: int32) int64 then
var s: int64 = call Self.target, a, b, c
_ = return s
end
""",
        "c": """extern int64_t tc_function_1(int64_t, int64_t, int32_t);
int64_t c_callee(int64_t a, int64_t b, int32_t c) {
    return a + b + (int64_t)c;
}
void tc_main(void) {
    pi((uint64_t)tc_function_1(10000000000LL, 20000000000LL, 3));
}
""",
        "out": "30000000003\n",
    },
    {
        "id": "TC-ABI-REV-0013",
        "profile": "arm32-baremetal",
        "name": "eight-byte composite split between r3 and the stack",
        "lib": """#lib
public struct Pair then
var x: int32
var y: int32
end
public func target(a: int32, b: int32, c: int32, p: Pair, n: int32) int32 then
var s: int32 = add int32, a, b
_ = return s
end
public func caller(a: int32, b: int32, c: int32, x: int32, y: int32, n: int32) int32 then
var p: Pair = make Pair, x, y
var r: int32 = call Self.target, a, b, c, p, n
_ = return r
end
""",
        "c": """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern int32_t tc_function_1(int32_t, int32_t, int32_t, int32_t, int32_t,
                             int32_t);
int32_t c_callee(int32_t a, int32_t b, int32_t c, Pair p, int32_t n) {
    return a + b + c + p.x + p.y + n;
}
void tc_main(void) {
    pi((uint64_t)(int64_t)tc_function_1(1, 2, 3, 10, 20, 64));
}
""",
        "out": "100\n",
    },
    {
        "id": "TC-ABI-REV-0014",
        "profile": "arm32-baremetal",
        "name": "twelve-byte composite built by the caller",
        "lib": """#lib
public struct Big then
var a: int32
var b: int64
end
public func target(s: Big) int64 then
var a: int64 = cast int64, s.a
var r: int64 = add int64, a, s.b
_ = return r
end
public func caller(a: int32, b: int64) int64 then
var s: Big = make Big, a, b
var r: int64 = call Self.target, s
_ = return r
end
""",
        "c": """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Big;
extern int64_t tc_function_1(int32_t, int64_t);
int64_t c_callee(Big s) { return (int64_t)s.a + s.b; }
void tc_main(void) { pi((uint64_t)tc_function_1(4, 30)); }
""",
        "out": "34\n",
    },
    {
        "id": "TC-ABI-REV-0015",
        "profile": "arm32-baremetal",
        "name": "float64 and float32 scalar arguments",
        "lib": """#lib
public func target(a: float64, b: float32) float64 then
var c: float64 = cast float64, b
var r: float64 = add float64, a, c
_ = return r
end
public func caller(a: float64, b: float32) float64 then
var r: float64 = call Self.target, a, b
_ = return r
end
""",
        "c": """extern double tc_function_1(double, float);
double c_callee(double a, float b) { return a + (double)b; }
void tc_main(void) { pi(db(tc_function_1(1.5, 2.5f))); }
""",
        "out": f"{f64bits(4.0)}\n",
    },
    {
        "id": "TC-ABI-REV-0016",
        "profile": "arm32-baremetal",
        "name": "twenty-byte five-float composite built by the caller",
        "lib": """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func target(f: F5) int32 then
var s: int32 = cast int32, f.a
_ = return s
end
public func caller(a: float32, b: float32, c: float32, d: float32, e: float32) int32 then
var f: F5 = make F5, a, b, c, d, e
var r: int32 = call Self.target, f
_ = return r
end
""",
        "c": """typedef struct __attribute__((packed)) {
    float a; float b; float c; float d; float e;
} F5;
extern int32_t tc_function_1(float, float, float, float, float);
int32_t c_callee(F5 f) {
    return (int32_t)f.a + (int32_t)f.b + (int32_t)f.c +
           (int32_t)f.d + (int32_t)f.e;
}
void tc_main(void) { pi((uint64_t)(int64_t)tc_function_1(1, 2, 3, 4, 5)); }
""",
        "out": "15\n",
    },
    {
        "id": "TC-ABI-REV-0017",
        "profile": "arm32-baremetal",
        "name": "sixteen-byte composite argument and indirect return",
        "lib": """#lib
public struct H then
var x: float64
var y: float64
end
public func target(h: H) H then
_ = return h
end
public func caller(x: float64, y: float64) float64 then
var h: H = make H, x, y
var r: H = call Self.target, h
var s: float64 = add float64, r.x, r.y
_ = return s
end
""",
        "c": """typedef struct __attribute__((packed)) { double x; double y; } H;
extern double tc_function_1(double, double);
H c_callee(H h) { H r; r.x = h.x + 1.0; r.y = h.y + 2.0; return r; }
void tc_main(void) { pi(db(tc_function_1(1.5, 2.25))); }
""",
        "out": f"{f64bits(6.75)}\n",
    },
    {
        "id": "TC-ABI-REV-0018",
        "profile": "arm32-baremetal",
        "name": "narrow integer and bool arguments and bool return",
        "lib": """#lib
public func target(a: int8, b: uint8, c: int16, d: uint16, e: bool) bool then
_ = return e
end
public func caller(a: int8, b: uint8, c: int16, d: uint16, e: bool) bool then
var r: bool = call Self.target, a, b, c, d, e
_ = return r
end
""",
        "c": """extern bool tc_function_1(int8_t, uint8_t, int16_t, uint16_t, bool);
bool c_callee(int8_t a, uint8_t b, int16_t c, uint16_t d, bool e) {
    return a == -1 && b == 255 && c == -2 && d == 65535 && e;
}
void tc_main(void) {
    pi(tc_function_1(-1, 255, -2, 65535, true) ? 1u : 0u);
}
""",
        "out": "1\n",
    },
    {
        "id": "TC-ABI-REV-0019",
        "profile": "arm32-baremetal",
        "name": "twelve-byte composite return through the hidden pointer",
        "lib": """#lib
public struct Big then
var a: int32
var b: int64
end
public func target() Big then
var b: Big = make Big, 0, 0
_ = return b
end
public func caller() int64 then
var r: Big = call Self.target
var a: int64 = cast int64, r.a
var s: int64 = add int64, a, r.b
_ = return s
end
""",
        "c": """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Big;
extern int64_t tc_function_1(void);
Big c_callee(void) { Big b; b.a = 4; b.b = 30; return b; }
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "34\n",
    },
]



def runtime_archive(profile: str) -> Path:
    if profile == "arm32-baremetal":
        return engine.runtime_archive(32)
    return engine.runtime_archive(64, baremetal=True)


def linker_script(profile: str) -> Path:
    if profile == "arm32-baremetal":
        return ROOT / "src/backend/arm32/link.ld"
    return ROOT / "src/backend/aarch64/link.ld"


def compile_flags(profile: str) -> list[str]:
    if profile == "arm32-baremetal":
        return ["-target", "thumbv7em-none-eabihf", "-mcpu=cortex-m7",
                "-mfloat-abi=softfp", "-mfpu=fpv5-d16", "-ffreestanding",
                "-nostdlib", "-O2", "-ffp-contract=off",
                "-Wno-unused-command-line-argument"]
    return ["-target", "aarch64-none-elf", "-ffreestanding", "-nostdlib",
            "-O2", "-ffp-contract=off",
            "-Wno-unused-command-line-argument"]


def build_and_run(profile: str, lib: str, c_code: str,
                  directory: Path) -> tuple[int, bytes, str]:
    (directory / "lib.tc").write_text(lib, encoding="utf-8")
    (directory / "root.tc").write_text(ROOT_TC, encoding="utf-8")
    assembly = directory / "gen.s"
    proc = subprocess.run(
        [engine.tc_aot_path(), "--emit=asm", f"--output={assembly}",
         f"--profile={profile}", f"--include={directory}",
         str(directory / "root.tc")],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if proc.returncode != 0:
        return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
    text = assembly.read_text(encoding="utf-8")
    if "tc_function_0:" not in text or "tc_function_1:" not in text:
        return 1, b"", "emitter did not produce target/caller functions"
    calls = text.count("bl tc_function_0")
    if calls != 1:
        return 1, b"", f"expected one caller to target call, found {calls}"
    text = text.replace("tc_main", "tc_program_main")
    if profile == "arm32-baremetal":
        marker = ("  .thumb_func\n  .type tc_function_1, %function\n")
        if marker not in text:
            return 1, b"", "ARM32 caller symbol shape not found"
        text = text.replace(marker, "  .globl tc_function_1\n" + marker)
    else:
        marker = "  .type tc_function_1, %function\n"
        if marker not in text:
            return 1, b"", "AArch64 caller symbol shape not found"
        text = text.replace(marker, "  .globl tc_function_1\n" + marker)
    text = text.replace("bl tc_function_0", "bl c_callee")
    assembly.write_text(text, encoding="utf-8")
    (directory / "harness.c").write_text(CPREFIX + c_code, encoding="utf-8")
    flags = compile_flags(profile)
    objects = []
    for source, name in ((assembly, "gen.o"),
                         (directory / "harness.c", "harness.o")):
        obj = directory / name
        proc = subprocess.run([CLANG, *flags, "-c", str(source), "-o",
                               str(obj)],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60)
        if proc.returncode != 0:
            return proc.returncode, b"", proc.stderr.decode("utf-8",
                                                            "replace")
        objects.append(obj)
    elf = directory / "out.elf"
    proc = subprocess.run(
        [engine.lld_path(), "-m",
         "armelf" if profile == "arm32-baremetal" else "aarch64elf",
         "-T", str(linker_script(profile)), "--icf=none",
         *[str(obj) for obj in objects], str(runtime_archive(profile)),
         "-o", str(elf)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if proc.returncode != 0:
        return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
    if profile == "arm32-baremetal":
        result = engine.run_arm32_elf(elf, b"", directory)
    else:
        result = engine.run_aarch64_baremetal_elf(elf, b"", directory)
    return result.exit, result.stdout, result.diagnostic


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if CLANG is None:
        print(f"{CASE_ID}: clang not found")
        return 1
    failures = 0
    for item in CASES:
        directory = engine.temporary_directory("tc-rev-interop-")
        try:
            code, stdout, diagnostic = build_and_run(
                item["profile"], item["lib"], item["c"], directory)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"{CASE_ID}: {item['name']}: {exc}")
            failures += 1
            continue
        actual = stdout.decode("utf-8", "replace")
        if actual != item["out"]:
            failures += 1
            print(f"{CASE_ID}: {item['name']}: stdout {actual!r} != "
                  f"{item['out']!r}")
            print(f"  exit {code}; diagnostic {diagnostic!r}")
        elif code != 0 or "none" not in diagnostic:
            failures += 1
            print(f"{CASE_ID}: {item['name']}: exit {code}, record "
                  f"{diagnostic!r}")
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        print(f"{CASE_ID}: {failures} of {len(CASES)} cases failed")
        return 1
    print(f"{CASE_ID}: {len(CASES)} TC-caller/C-callee reverse ABI cases "
          f"passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
