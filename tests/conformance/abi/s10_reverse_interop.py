#!/usr/bin/env python3
"""S10 TC-caller/C-callee reverse interoperability audit for RISC-V ILP32.

The TC caller and the clang-compiled C callee are independent implementations
of the base integer calling convention.  The suite reuses the target-neutral
composite/narrow/64-bit cases and runs them under `riscv32-baremetal`.

Run:
    python3 tests/conformance/abi/s10_reverse_interop.py --check
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

CASE_ID = "TC-ABI-S10-0002"
PROFILE = "riscv32-baremetal"

EXTRA_CASES = [
    {
        "id": "TC-ABI-S10-0014",
        "name": "TC function pointer called by C",
        "lib": """#lib
public @unsafe func target(fp: fnptr<int64, int64>, x: int64) int64 then
var r: int64 = call_indirect fp, x
_ = return r
end
public @unsafe func caller() int64 then
var fp: fnptr<int64, int64> = funcaddr Self.add5
var r: int64 = call Self.target, fp, 37
_ = return r
end
public func add5(x: int64) int64 then
var r: int64 = add int64, x, 5
_ = return r
end
""",
        "c": """typedef int64_t (*FP)(int64_t);
extern int64_t tc_function_1(void);
int64_t c_callee(FP fp, int64_t x) { return fp(x); }
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "42\n",
    },
    {
        "id": "TC-ABI-S10-0013",
        "name": "TC caller sret with seven integers and a stack int64 pair",
        "lib": """#lib
public struct Big then
var a: int64
var b: int64
end
public func target(p0: int32, p1: int32, p2: int32, p3: int32, p4: int32, p5: int32, p6: int32, p7: int64) Big then
var r: Big = make Big, p7, 42
_ = return r
end
public func caller() int64 then
var r: Big = call Self.target, 1, 2, 3, 4, 5, 6, 7, 1000000000
_ = return r.a
end
""",
        "c": """typedef struct __attribute__((packed)) { int64_t a; int64_t b; } Big;
extern int64_t tc_function_1(void);
Big c_callee(int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4,
             int32_t p5, int32_t p6, int64_t p7) {
    Big r; r.a = p7 + 1; r.b = (int64_t)p0 + p1 + p2 + p3 + p4 + p5 + p6;
    return r;
}
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "1000000001\n",
    },
    {
        "id": "TC-ABI-S10-0010",
        "name": "TC caller split int64 pair after seven integers",
        "lib": """#lib
public func target(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, x: int64) int64 then
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
public func caller() int64 then
var r: int64 = call Self.target, 1, 2, 3, 4, 5, 6, 7, 1000000000
_ = return r
end
""",
        "c": """extern int64_t tc_function_1(void);
int64_t c_callee(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                 int32_t f, int32_t g, int64_t x) {
    return (int64_t)a + b + c + d + e + f + g + x;
}
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "1000000028\n",
    },
    {
        "id": "TC-ABI-S10-0011",
        "name": "TC caller split eight-byte composite after seven integers",
        "lib": """#lib
public struct Pair then
var x: int32
var y: int32
end
public func target(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, p: Pair) int32 then
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
public func caller() int32 then
var p: Pair = make Pair, 10, 20
var r: int32 = call Self.target, 1, 2, 3, 4, 5, 6, 7, p
_ = return r
end
""",
        "c": """typedef struct __attribute__((packed)) { int32_t x; int32_t y; } Pair;
extern int32_t tc_function_1(void);
int32_t c_callee(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                 int32_t f, int32_t g, Pair p) {
    return a + b + c + d + e + f + g + p.x + p.y;
}
void tc_main(void) { pi((uint64_t)(int64_t)tc_function_1()); }
""",
        "out": "58\n",
    },
    {
        "id": "TC-ABI-S10-0012",
        "name": "TC caller stack int64 pair after eight integers",
        "lib": """#lib
public func target(a: int32, b: int32, c: int32, d: int32, e: int32, f7: int32, g: int32, h: int32, x: int64) int64 then
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
public func caller() int64 then
var r: int64 = call Self.target, 1, 2, 3, 4, 5, 6, 7, 8, 1000000000
_ = return r
end
""",
        "c": """extern int64_t tc_function_1(void);
int64_t c_callee(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                 int32_t f, int32_t g, int32_t h, int64_t x) {
    return (int64_t)a + b + c + d + e + f + g + h + x;
}
void tc_main(void) { pi((uint64_t)tc_function_1()); }
""",
        "out": "1000000036\n",
    },
]


def riscv_clang() -> str | None:
    for candidate in ("/opt/homebrew/opt/llvm/bin/clang",
                      "/usr/local/opt/llvm/bin/clang"):
        if Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    return shutil.which("clang")


def load_reverse() -> object:
    path = ROOT / "tests/conformance/abi/reverse_interop_cases.py"
    spec = importlib.util.spec_from_file_location("s10_reverse_cases", path)
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
    module = load_reverse()
    module.CLANG = riscv_clang()
    if module.CLANG is None:
        print(f"{CASE_ID}: clang not found")
        return 1
    cases = [item for item in module.CASES
             if item["profile"] == "arm32-baremetal"]
    cases = cases + EXTRA_CASES
    failures = 0
    for item in cases:
        directory = engine.temporary_directory("tc-s10-rev-")
        try:
            code, stdout, diagnostic = module.build_and_run(
                PROFILE, item["lib"], item["c"], directory)
        except (OSError, Exception) as exc:  # noqa: BLE001 - report
            failures += 1
            print(f"{CASE_ID}: {item['name']}: {exc}")
            shutil.rmtree(directory, ignore_errors=True)
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
        print(f"{CASE_ID}: {failures} of {len(cases)} cases failed")
        return 1
    print(f"{CASE_ID}: {len(cases)} TC-caller/RISC-V-callee ABI cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
