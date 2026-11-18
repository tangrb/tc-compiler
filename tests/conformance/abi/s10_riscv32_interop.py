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
