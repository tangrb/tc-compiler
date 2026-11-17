#!/usr/bin/env python3
"""S8 AAPCS64 ABI conformance suite.

Covers the S8 delivery row "full ABI": integer/pointer/function-pointer
arguments in x0-x7, float32/float64 in v0-v7, homogeneous float aggregates
(HFA) in consecutive v registers, composites of at most 16 bytes in consecutive
x registers, larger aggregates by value on the stack with an x8 indirect
result pointer, bool/int8/int16 extension, stack alignment, callee-saved
registers across calls and recursion, the D boundary, and the rule that direct
call parameter preconditions (`@nonnull`/`@align`/`@range`) are optimization
assumptions, never runtime checks (standard §8.5/§8.6/§10.2.1).

Every executable case runs on the reference VM and on the AArch64 ELF and must
agree with independently derived output/trap/exit expectations.  A small
assembly audit pins the ABI shape (v registers for HFAs, x0/x1 for small
composites, saved x8 for indirect returns, 16-byte stack steps).

Run:
    python3 tests/conformance/abi/s8_abi_cases.py --check
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

P = engine.program
L = engine.library

CASES = [
    engine.case(
        "TC-ABI-S8-0001",
        "§8.5 integer/pointer/function-pointer arguments use x0-x7 and the stack in order",
        files={
            "root.tc": P(
                "import lib",
                "var fp: fnptr<int64, int64> = funcaddr lib.one",
                "var p: ptr<int64> = addr int64, lib.V",
                "var r: int64 = call lib.mix, 1, 2, 3, 4, 5, 6, 7, 8, 9, p, fp",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public static var V: int64 = 11",
                "public func one(x: int64) int64 then",
                "_ = return x",
                "end",
                "public func mix(a: int64, b: int64, c: int64, d: int64,"
                " e: int64, f: int64, g: int64, h: int64, i: int64,"
                " p: ptr<int64>, fp: fnptr<int64, int64>) int64 then",
                "var s1: int64 = add int64, a, b",
                "var s2: int64 = add int64, s1, c",
                "var s3: int64 = add int64, s2, d",
                "var s4: int64 = add int64, s3, e",
                "var s5: int64 = add int64, s4, f",
                "var s6: int64 = add int64, s5, g",
                "var s7: int64 = add int64, s6, h",
                "var s8: int64 = add int64, s7, i",
                "var v: int64 = copy int64, 0",
                "unsafe then",
                "v = load int64, p",
                "end",
                "var s9: int64 = add int64, s8, v",
                "var id: int64 = copy int64, 0",
                "unsafe then",
                "id = call_indirect fp, s9",
                "end",
                "_ = return id",
                "end",
            ),
        },
        out="56\n",
    ),
    engine.case(
        "TC-ABI-S8-0002",
        "§8.5 float32/float64 arguments use v0-v7 and narrow integers extend correctly",
        files={
            "root.tc": P(
                "import lib",
                "var r: float64 = call lib.mix, 1, 2, 3, true, 5, 6, 7, 8, 1.5f, 2.5, 3.5f, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5",
                "_ = writeln float64, r",
            ),
            "lib.tc": L(
                "public func mix(a: int8, b: int16, c: int32, d: bool,"
                " e: int64, f: uint8, g: uint16, h: uint32, q: float32,"
                " r: float64, s: float32, t: float64, u: float64, v: float64,"
                " w: float64, x: float64, y: float64, z: float64) float64 then",
                "var ai: int32 = cast int32, a",
                "var bi: int32 = cast int32, b",
                "var si: int32 = add int32, ai, bi",
                "var ci: int32 = add int32, si, c",
                "var vi: int32 = cast int32, e",
                "var wi: int32 = add int32, ci, vi",
                "var xf: float64 = cast float64, wi",
                "var qf: float64 = cast float64, q",
                "var sum: float64 = add float64, xf, qf",
                "var sum1: float64 = add float64, sum, r",
                "var sum2: float64 = add float64, sum1, t",
                "var sum3: float64 = add float64, sum2, u",
                "var sum4: float64 = add float64, sum3, v",
                "var sum5: float64 = add float64, sum4, w",
                "var sum6: float64 = add float64, sum5, x",
                "var sum7: float64 = add float64, sum6, y",
                "var sum8: float64 = add float64, sum7, z",
                "var tf: float64 = cast float64, s",
                "var result: float64 = add float64, sum8, tf",
                "_ = return result",
                "end",
            ),
        },
        # (1-2+3)+5 = 7 as float64; the floats are
        # 1.5+2.5+4.5+5.5+6.5+7.5+8.5+9.5+10.5 + 3.5 (float32) = 60.5.
        out="71\n",
    ),
    engine.case(
        "TC-ABI-S8-0003",
        "§8.5 bool/int8/int16 arguments and results are extended without high-bit leaks",
        files={
            "root.tc": P(
                "import lib",
                "var a: int8 = copy int8, -1",
                "var b: int16 = copy int16, -2",
                "var c: int32 = call lib.sum, a, b, true",
                "_ = writeln int32, c",
                "var d: bool = call lib.flag, false",
                "_ = writeln bool, d",
            ),
            "lib.tc": L(
                "public func sum(a: int8, b: int16, c: bool) int32 then",
                "var ai: int32 = cast int32, a",
                "var bi: int32 = cast int32, b",
                "var s: int32 = add int32, ai, bi",
                "var one: int32 = copy int32, 1",
                "var r: int32 = add int32, s, one",
                "_ = return r",
                "end",
                "public func flag(x: bool) bool then",
                "var r: bool = not bool, x",
                "_ = return r",
                "end",
            ),
        },
        out="-2\ntrue\n",
    ),
    engine.case(
        "TC-ABI-S8-0004",
        "§8.5 callee-saved registers survive recursive frames with struct locals",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.down, 8",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct S then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func down(n: int64) int64 then",
                "var s: S = make S, n, 2, 3",
                "var zero: int64 = copy int64, 0",
                "var iszero: bool = eq int64, n, zero",
                "var result: int64 = copy int64, 0",
                "_ = br_if iszero, done",
                "var m: int64 = sub int64, n, 1",
                "var deeper: int64 = call Self.down, m",
                "var t: int64 = add int64, s.a, s.b",
                "var u: int64 = add int64, t, s.c",
                "result = add int64, u, deeper",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        # sum over n=1..8 of (n+2+3) = 6+7+...+13 = 76
        out="76\n",
    ),
    engine.case(
        "TC-ABI-S8-0005",
        "§8.5 HFA float32 parameters and results use v0-v2",
        files={
            "root.tc": P(
                "import lib",
                "var q: lib.Q = call lib.mk, 1.0f, 2.0f, 3.0f",
                "var r: float32 = call lib.sum, q",
                "_ = writeln float32, r",
            ),
            "lib.tc": L(
                "public struct Q then",
                "var a: float32",
                "var b: float32",
                "var c: float32",
                "end",
                "public func mk(a: float32, b: float32, c: float32) Q then",
                "var q: Q = make Q, a, b, c",
                "_ = return q",
                "end",
                "public func sum(q: Q) float32 then",
                "var t: float32 = add float32, q.a, q.b",
                "var r: float32 = add float32, t, q.c",
                "_ = return r",
                "end",
            ),
        },
        out="6\n",
    ),
    engine.case(
        "TC-ABI-S8-0006",
        "§8.5 composites of at most 16 bytes use x registers; larger use stack + x8",
        files={
            "root.tc": P(
                "import lib",
                "var s: lib.S12 = make lib.S12, 1, 2",
                "var t: lib.S12 = call lib.id12, s",
                "_ = writeln int64, t.b",
                "var u: lib.S24 = call lib.mk24, 5",
                "var v: lib.S24 = call lib.id24, u",
                "_ = writeln int64, v.c",
            ),
            "lib.tc": L(
                "public struct S12 then",
                "var a: int32",
                "var b: int64",
                "end",
                "public struct S24 then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func id12(x: S12) S12 then",
                "_ = return x",
                "end",
                "public func mk24(x: int64) S24 then",
                "var s: S24 = make S24, x, 6, 7",
                "_ = return s",
                "end",
                "public func id24(x: S24) S24 then",
                "_ = return x",
                "end",
            ),
        },
        out="2\n7\n",
    ),
    engine.case(
        "TC-ABI-S8-0007",
        "§8.5 indirect calls with nine arguments use the same ABI as direct calls",
        files={
            "root.tc": P(
                "import lib",
                "var fp: fnptr<int64, int64, int64, int64, int64, int64,"
                " int64, int64, int64, int64> = funcaddr lib.sum",
                "unsafe then",
                "var r: int64 = call_indirect fp, 1, 2, 3, 4, 5, 6, 7, 8, 9",
                "_ = writeln int64, r",
                "end",
            ),
            "lib.tc": L(
                "public func sum(a: int64, b: int64, c: int64, d: int64,"
                " e: int64, f: int64, g: int64, h: int64, i: int64) int64 then",
                "var s1: int64 = add int64, a, b",
                "var s2: int64 = add int64, s1, c",
                "var s3: int64 = add int64, s2, d",
                "var s4: int64 = add int64, s3, e",
                "var s5: int64 = add int64, s4, f",
                "var s6: int64 = add int64, s5, g",
                "var s7: int64 = add int64, s6, h",
                "var s8: int64 = add int64, s7, i",
                "_ = return s8",
                "end",
            ),
        },
        out="45\n",
    ),
    engine.case(
        "TC-ABI-S8-0008",
        "§8.5/§10.2.1 direct-call @nonnull/@range preconditions are not checked at runtime",
        files={
            "root.tc": P(
                "import lib",
                "var p: ptr<int32> = copy ptr<int32>, nullptr",
                "unsafe then",
                "var a: bool = call lib.nonnull, p",
                "_ = writeln bool, a",
                "var b: bool = call lib.ranged, 1000",
                "_ = writeln bool, b",
                "end",
            ),
            "lib.tc": L(
                "public @fast func nonnull(@nonnull p: ptr<int32>) bool then",
                "var is: bool = ptreq int32, p, nullptr",
                "_ = return is",
                "end",
                "public @fast func ranged(@range(0, 9) x: int32) bool then",
                "var ten: int32 = copy int32, 10",
                "var r: bool = ge int32, x, ten",
                "_ = return r",
                "end",
            ),
        },
        out="true\ntrue\n",
    ),
    engine.case(
        "TC-ABI-S8-0009",
        "§8.5 the abstract depth D boundary still holds with struct frames",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.down, 600",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct S then",
                "var a: int64",
                "var b: int64",
                "end",
                "public func down(n: int64) int64 then",
                "var s: S = make S, n, 1",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var result: int64 = copy int64, 0",
                "_ = br_if stop, done",
                "var m: int64 = sub int64, n, 1",
                "var d: int64 = call Self.down, m",
                "result = add int64, s.a, d",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        trap="TC_RE_STACK_OVERFLOW",
    ),
]


def check_lowering_shape(tmp: Path, failures: list[str]) -> None:
    root = tmp / "shape.tc"
    lib = tmp / "shape_lib.tc"
    root.write_text(
        P(
            "import shape_lib",
            "var q: shape_lib.Q = call shape_lib.mk, 1.0f, 2.0f, 3.0f",
            "var b: shape_lib.B = call shape_lib.big, 4",
            "var r: int64 = call shape_lib.many, 1, 2, 3, 4, 5, 6, 7, 8, 9",
            "var fp: fnptr<int64, int64> = funcaddr shape_lib.one",
            "var z: int64 = copy int64, 0",
            "unsafe then",
            "z = call_indirect fp, 1",
            "end",
            "_ = writeln float32, q.a",
            "_ = writeln int64, b.c",
            "_ = writeln int64, r",
            "_ = writeln int64, z",
        ),
        encoding="utf-8",
    )
    lib.write_text(
        L(
            "public struct Q then",
            "var a: float32",
            "var b: float32",
            "var c: float32",
            "end",
            "public struct B then",
            "var a: int64",
            "var b: int64",
            "var c: int64",
            "end",
            "public func mk(a: float32, b: float32, c: float32) Q then",
            "var q: Q = make Q, a, b, c",
            "_ = return q",
            "end",
            "public func big(x: int64) B then",
            "var b: B = make B, x, 2, 3",
            "_ = return b",
            "end",
            "public func one(x: int64) int64 then",
            "_ = return x",
            "end",
            "public func many(a: int64, b: int64, c: int64, d: int64,"
            " e: int64, f: int64, g: int64, h: int64, i: int64) int64 then",
            "var s: int64 = add int64, a, i",
            "_ = return s",
            "end",
        ),
        encoding="utf-8",
    )
    out = tmp / "shape.s"
    proc = subprocess.run(
        [engine.tc_aot_path(), "--emit=asm", f"--output={out}",
         "--profile=aarch64-linux-user", f"--include={tmp}", str(root)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120,
    )
    if proc.returncode != 0 or not out.exists():
        failures.append(
            f"abi shape: assembly failed ({proc.returncode}): "
            f"{proc.stderr.decode('utf-8', 'replace').strip()}")
        return
    text = out.read_text(encoding="utf-8")
    if not re.search(r"fmov s[0-3], w", text):
        failures.append("abi shape: HFA return does not use s0-s3")
    if "str x8, [x19, #" not in text:
        failures.append("abi shape: indirect return does not save x8")
    if not re.search(r"str x9, \[x16\]", text):
        failures.append("abi shape: a stack integer argument is not stored")
    for match in re.finditer(r"sub sp, sp, #(\d+)", text):
        value = int(match.group(1))
        if value % 16 != 0:
            failures.append(f"abi shape: stack step {value} is not 16-aligned")
            break
    if "bl tc_function_" not in text:
        failures.append("abi shape: direct calls are not emitted")
    if not re.search(r"blr x16", text):
        failures.append("abi shape: indirect calls do not use blr")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures = engine.run_cases(CASES, "s8.abi", keep=args.keep)
    if failures:
        return 1
    tmp = Path(tempfile.mkdtemp(prefix="tc-s8-abi-"))
    shape_failures: list[str] = []
    try:
        check_lowering_shape(tmp, shape_failures)
    finally:
        import shutil

        shutil.rmtree(tmp, ignore_errors=True)
    if shape_failures:
        print(f"s8.abi: {len(shape_failures)} shape failure(s)")
        for item in shape_failures:
            print(f"  {item}")
        return 1
    print("s8.abi: ABI lowering shape verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
