#!/usr/bin/env python3
"""S9 AAPCS32 interoperability audit: C callers calling TC ARM32 callees.

Each case compiles a TC library function to Cortex-M7 assembly, appends a
global ``tc_test_call`` thunk that branches to ``tc_function_0``, compiles a
freestanding C harness that declares the same signature, links both with the
ARM32 runtime and runs the image on qemu-system-arm MPS2 AN500.  The C
compiler is an independent AAPCS32 implementation, so this checks the generated
callee against the published base ABI (core-register pairs for int64/float64,
four-byte composites in a word, larger composites by reference and hidden
indirect returns).

Run:
    python3 tests/conformance/abi/s9_arm32_interop.py --check
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

CASE_ID = "TC-ABI-S9-0010"
PROFILE = "arm32-baremetal"
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
ROOT_TC = "#program\nimport lib\n_ = writeln int64, 0\n"


def f64bits(value: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def f32bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


CASES: list[tuple[str, str, str, str]] = [
    (
        "four integer arguments",
        """#lib
public func f(a: int32, b: int32, c: int32, d: int32) int64 then
var s: int64 = cast int64, a
var t: int64 = cast int64, b
var u: int64 = add int64, s, t
var v: int64 = cast int64, c
var w: int64 = add int64, u, v
var x: int64 = cast int64, d
var y: int64 = add int64, w, x
_ = return y
end
""",
        """extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t);
void tc_main(void) { pi((uint64_t)tc_test_call(1, 2, 3, 4)); }
""",
        "10\n",
    ),
    (
        "fifth integer argument on the stack",
        """#lib
public func f(a: int32, b: int32, c: int32, d: int32, e: int32) int64 then
var s: int64 = cast int64, a
var t: int64 = cast int64, b
var u: int64 = add int64, s, t
var v: int64 = cast int64, c
var w: int64 = add int64, u, v
var x: int64 = cast int64, d
var y: int64 = add int64, w, x
var z: int64 = cast int64, e
var r: int64 = add int64, y, z
_ = return r
end
""",
        """extern int64_t tc_test_call(int32_t, int32_t, int32_t, int32_t,
                            int32_t);
void tc_main(void) { pi((uint64_t)tc_test_call(1, 2, 3, 4, 5)); }
""",
        "15\n",
    ),
    (
        "int64 pair arguments and return",
        """#lib
public func f(a: int64, b: int64) int64 then
var r: int64 = mul int64, a, b
_ = return r
end
""",
        """extern int64_t tc_test_call(int64_t, int64_t);
void tc_main(void) { pi((uint64_t)tc_test_call(3037000499LL, 3037000499LL)); }
""",
        "9223372030926249001\n",
    ),
    (
        "float64 pair arguments and return",
        """#lib
public func f(a: float64, b: float64) float64 then
var r: float64 = add float64, a, b
_ = return r
end
""",
        """extern double tc_test_call(double, double);
void tc_main(void) { pi(db(tc_test_call(1.5, 2.25))); }
""",
        f"{f64bits(3.75)}\n",
    ),
    (
        "eight-byte composite by reference and indirect return",
        """#lib
public struct Pair then
var x: int64
var y: int64
end
public func f(p: Pair, n: int64) Pair then
var a: int64 = add int64, p.x, p.y
var b: int64 = add int64, a, n
var r: Pair = make Pair, b, a
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int64_t x; int64_t y; } Pair;
extern Pair tc_test_call(Pair, int64_t);
void tc_main(void) { Pair p; p.x = 7; p.y = 35;
    Pair r = tc_test_call(p, 100); pi((uint64_t)r.x); pi((uint64_t)r.y); }
""",
        "142\n42\n",
    ),
    (
        "four-byte composite in one word",
        """#lib
public struct Small then
var a: int32
end
public func f(s: Small) Small then
_ = return s
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; } Small;
extern Small tc_test_call(Small);
void tc_main(void) { Small s; s.a = 1234; pi((uint64_t)tc_test_call(s).a); }
""",
        "1234\n",
    ),
    (
        "twelve-byte composite by reference and indirect return",
        """#lib
public struct Big then
var a: int32
var b: int64
end
public func f(s: Big, k: int32) Big then
var a: int64 = cast int64, s.a
var b: int64 = add int64, a, s.b
var c: int64 = cast int64, k
var d: int64 = add int64, b, c
var r: Big = make Big, s.a, d
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int64_t b; } Big;
extern Big tc_test_call(Big, int32_t);
void tc_main(void) { Big s; s.a = 4; s.b = 30;
    Big r = tc_test_call(s, 8); pi((uint64_t)r.a); pi((uint64_t)r.b); }
""",
        "4\n42\n",
    ),
    (
        "narrow integer and bool extension",
        """#lib
public func f(a: int8, b: uint8, c: int16, d: uint16, e: bool) int32 then
var s: int32 = cast int32, a
var t: int32 = cast int32, b
var u: int32 = add int32, s, t
var v: int32 = cast int32, c
var w: int32 = add int32, u, v
var x: int32 = cast int32, d
var y: int32 = add int32, w, x
var z: int32 = cast int32, e
var r: int32 = add int32, y, z
_ = return r
end
""",
        """extern int32_t tc_test_call(int8_t, uint8_t, int16_t, uint16_t,
                            bool);
void tc_main(void) { pi((uint64_t)(int64_t)tc_test_call(-1, 255, -2, 65535,
    true)); }
""",
        "65788\n",
    ),
    (
        "two-double composite by reference",
        """#lib
public struct H then
var x: float64
var y: float64
end
public func f(h: H, z: float64) float64 then
var a: float64 = add float64, h.x, h.y
var b: float64 = add float64, a, z
_ = return b
end
""",
        """typedef struct __attribute__((packed)) { double x; double y; } H;
extern double tc_test_call(H, double);
void tc_main(void) { H h; h.x = 1.5; h.y = 2.25;
    pi(db(tc_test_call(h, 3.75))); }
""",
        f"{f64bits(7.5)}\n",
    ),
    (
        "mixed int64, composite and pointer-sized arguments",
        """#lib
public struct P then
var a: int32
var b: int32
end
public func f(x: int64, p: P, y: int32) int64 then
var a: int64 = cast int64, p.a
var b: int64 = add int64, x, a
var c: int64 = cast int64, p.b
var d: int64 = add int64, b, c
var e: int64 = cast int64, y
var r: int64 = add int64, d, e
_ = return r
end
""",
        """typedef struct __attribute__((packed)) { int32_t a; int32_t b; } P;
extern int64_t tc_test_call(int64_t, P, int32_t);
void tc_main(void) { P p; p.a = 10; p.b = 20;
    pi((uint64_t)tc_test_call(1000, p, 7)); }
""",
        "1037\n",
    ),
    (
        "eight-byte composite split between r3 and the stack",
        """#lib
public struct Pair then
var x: int32
var y: int32
end
public func f(a: int32, b: int32, c: int32, p: Pair, n: int32) int32 then
var s: int32 = add int32, a, b
var t: int32 = add int32, s, c
var u: int32 = add int32, t, p.x
var v: int32 = add int32, u, p.y
var w: int32 = add int32, v, n
_ = return w
end
""",
        """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern int32_t tc_test_call(int32_t, int32_t, int32_t, Pair, int32_t);
void tc_main(void) {
    Pair p; p.x = 10; p.y = 20;
    pi((uint64_t)(int64_t)tc_test_call(1, 2, 3, p, 64));
}
""",
        "100\n",
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
        "twenty-byte five-float composite by value words",
        """#lib
public struct F5 then
var a: float32
var b: float32
var c: float32
var d: float32
var e: float32
end
public func fsum(x: F5) float64 then
var a: float64 = cast float64, x.a
var b: float64 = cast float64, x.b
var c: float64 = cast float64, x.c
var d: float64 = cast float64, x.d
var e: float64 = cast float64, x.e
var s1: float64 = add float64, a, b
var s2: float64 = add float64, c, d
var s3: float64 = add float64, s1, s2
var s4: float64 = add float64, s3, e
_ = return s4
end
""",
        """typedef struct __attribute__((packed)) {
    float a; float b; float c; float d; float e;
} F5;
extern double tc_test_call(F5);
void tc_main(void) {
    F5 x; x.a = 1.0f; x.b = 2.0f; x.c = 3.0f; x.d = 4.0f; x.e = 5.0f;
    pi(db(tc_test_call(x)));
}
""",
        f"{f64bits(15.0)}\n",
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
        """extern bool tc_test_call(int32_t);
void tc_main(void) { pi(tc_test_call(-1) ? 1u : 0u); }
""",
        "1\n",
    ),
]


def runtime_archive(profile: str = PROFILE) -> Path:
    if profile == "arm32-baremetal":
        return engine.runtime_archive(32)
    return engine.runtime_archive_riscv32()


def linker_script(profile: str = PROFILE) -> Path:
    if profile == "arm32-baremetal":
        return ROOT / "src/backend/arm32/link.ld"
    return ROOT / "src/backend/riscv32/link.ld"


def compile_flags(profile: str = PROFILE) -> list[str]:
    if profile == "arm32-baremetal":
        return ["-target", "thumbv7em-none-eabihf", "-mcpu=cortex-m7",
                "-mfloat-abi=softfp", "-mfpu=fpv5-d16", "-ffreestanding",
                "-nostdlib", "-O2", "-ffp-contract=off",
                "-Wno-unused-command-line-argument"]
    return ["-target", "riscv32-unknown-elf", "-march=rv32imafdc",
            "-mabi=ilp32", "-mno-relax", "-msmall-data-limit=0",
            "-mstrict-align", "-ffreestanding", "-nostdlib", "-O2",
            "-ffp-contract=off", "-Wno-unused-command-line-argument"]


def run_elf(profile: str, elf: Path, directory: Path):
    if profile == "arm32-baremetal":
        return engine.run_arm32_elf(elf, b"", directory)
    return engine.run_riscv32_elf(elf, b"", directory)


def build_and_run(lib: str, harness: str, directory: Path,
                  profile: str = PROFILE) -> tuple[int, bytes,
                                                   str]:
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
    text = assembly.read_text(encoding="utf-8").replace(
        "tc_main", "tc_program_main")
    if profile == "arm32-baremetal":
        text += """
  .text
  .p2align 2
  .thumb_func
  .globl tc_test_call
  .type tc_test_call, %function
tc_test_call:
  b tc_function_0
  .size tc_test_call, .-tc_test_call
"""
    else:
        text += """
  .text
  .p2align 2
  .globl tc_test_call
  .type tc_test_call, @function
tc_test_call:
  j tc_function_0
  .size tc_test_call, .-tc_test_call
"""
    assembly.write_text(text, encoding="utf-8")
    (directory / "harness.c").write_text(CPREFIX + harness, encoding="utf-8")
    flags = compile_flags(profile)
    objects = []
    for source, name in ((assembly, "gen.o"),
                         (directory / "harness.c", "h.o")):
        obj = directory / name
        proc = subprocess.run([CLANG, *flags, "-c", str(source), "-o",
                               str(obj)],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        if proc.returncode != 0:
            return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
        objects.append(obj)
    elf = directory / "out.elf"
    linker_emulation = ("armelf" if profile == "arm32-baremetal"
                        else "elf32lriscv")
    linker_flags = ([] if profile == "arm32-baremetal" else ["--no-relax"])
    proc = subprocess.run(
        [engine.lld_path(), "-m", linker_emulation, *linker_flags,
         "-T", str(linker_script(profile)), "--icf=none",
         *[str(obj) for obj in objects], str(runtime_archive(profile)),
         "-o", str(elf)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return proc.returncode, b"", proc.stderr.decode("utf-8", "replace")
    result = run_elf(profile, elf, directory)
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
    archive = runtime_archive(PROFILE)
    if not archive.exists():
        print(f"{CASE_ID}: runtime archive not found at {archive}")
        return 1
    failures = 0
    for name, lib, harness, expected in CASES:
        directory = engine.temporary_directory("tc-s9-interop-")
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
        elif code != 0 or "none" not in diagnostic:
            failures += 1
            print(f"{CASE_ID}: {name}: exit {code}, record {diagnostic!r}")
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        print(f"{CASE_ID}: {failures} of {len(CASES)} cases failed")
        return 1
    print(f"{CASE_ID}: {len(CASES)} C-caller/ARM32-callee AAPCS32 cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
