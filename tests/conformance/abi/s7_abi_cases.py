#!/usr/bin/env python3
"""S7 AAPCS64 subset ABI conformance cases (design §10.2, test plan S7).

The subset covers integer/pointer x0-x7 arguments, x0 scalar return, bool and
narrow-integer extension, callee-saved frame/static bases, 16-byte stack
alignment and the abstract depth limit D.  Every case is executed on the
reference VM and on the AOT ELF; stdout, first trap and exit status must match
the independently derived expectation and each other.

Run:
    python3 tests/conformance/abi/s7_abi_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

LIB = engine.library(
    "public func eight(a: int32, b: int32, c: int32, d: int32, e: int32, "
    "f: int32, g: int32, h: int32) int32 then",
    "var s: int32 = add int32, a, b",
    "s = add int32, s, c",
    "s = add int32, s, d",
    "s = add int32, s, e",
    "s = add int32, s, f",
    "s = add int32, s, g",
    "s = add int32, s, h",
    "_ = return s",
    "end",
    "public func idb(x: bool) bool then",
    "_ = return x",
    "end",
    "public func id8(x: int8) int8 then",
    "_ = return x",
    "end",
    "public func id16(x: int16) int16 then",
    "_ = return x",
    "end",
    "public func fact(n: int32) int32 then",
    "var z: bool = le int32, n, 1",
    "_ = br_if z, base",
    "var m: int32 = sub int32, n, 1",
    "var r: int32 = call Self.fact, m",
    "var p: int32 = mul int32, n, r",
    "_ = return p",
    "label base:",
    "_ = return 1",
    "end",
    "public func down(n: int32) int32 then",
    "var z: bool = eq int32, n, 0",
    "_ = br_if z, base",
    "var m: int32 = sub int32, n, 1",
    "var r: int32 = call Self.down, m",
    "_ = return r",
    "label base:",
    "_ = return 0",
    "end",
    "public func inner(x: int32) int32 then",
    "var y: int32 = add int32, x, 100",
    "_ = return y",
    "end",
    "public func outer(x: int32) int32 then",
    "var a: int32 = call Self.inner, x",
    "var b: int32 = add int32, x, a",
    "_ = return b",
    "end",
    "public func inner8(x: int8) int32 then",
    "var y: int8 = add int8, x, 1",
    "var r: int32 = copy int32, 7",
    "_ = return r",
    "end",
    "public func outer8(x: int8) int32 then",
    "var pad: int8 = add int8, x, 2",
    "var r: int32 = call Self.inner8, pad",
    "var s: int32 = add int32, r, 1",
    "_ = return s",
    "end",
    "public func narrow(a: bool, b: int8, c: int16, d: int64) int64 then",
    "var x: int64 = copy int64, d",
    "_ = return x",
    "end",
)


def lib_case(case_id: str, spec: str, root: str, out: str = "",
             trap: str | None = None, stdin: str = "") -> engine.Case:
    return engine.case(case_id, spec,
                       files={"root.tc": root, "lib.tc": LIB},
                       out=out, trap=trap, stdin=stdin, category="abi")


CASES = [
    lib_case(
        "TC-AOT-ABI-0001",
        "design §10.2 AAPCS64 integer arguments in x0-x7 and x0 return",
        engine.program(
            "import lib",
            "var r: int32 = call lib.eight, 1, 2, 3, 4, 5, 6, 7, 8",
            "_ = writeln int32, r",
        ),
        out="36\n",
    ),
    lib_case(
        "TC-AOT-ABI-0002",
        "design §10.2 bool/int8/int16 extension on arguments and returns",
        engine.program(
            "import lib",
            "var a: bool = call lib.idb, true",
            "_ = writeln bool, a",
            "var b: int8 = call lib.id8, -5",
            "_ = writeln int8, b",
            "var c: int16 = call lib.id16, -300",
            "_ = writeln int16, c",
        ),
        out="true\n-5\n-300\n",
    ),
    lib_case(
        "TC-AOT-ABI-0003",
        "design §10.2 callee-saved x19/x20 survive a nested call",
        engine.program(
            "import lib",
            "var r: int32 = call lib.outer, 7",
            "_ = writeln int32, r",
        ),
        out="114\n",
    ),
    lib_case(
        "TC-AOT-ABI-0004",
        "design §10.2 recursive direct calls and by-value parameters",
        engine.program(
            "import lib",
            "var r: int32 = call lib.fact, 5",
            "_ = writeln int32, r",
        ),
        out="120\n",
    ),
    lib_case(
        "TC-AOT-ABI-0005",
        "standard §1.2 depth D=512: 512 active TC frames are allowed",
        engine.program(
            "import lib",
            "var r: int32 = call lib.down, 511",
            "_ = writeln int32, r",
        ),
        out="0\n",
    ),
    lib_case(
        "TC-AOT-ABI-0006",
        "standard §1.2 depth D=512: the 513th active frame traps",
        engine.program(
            "import lib",
            "var r: int32 = call lib.down, 512",
            "_ = writeln int32, r",
        ),
        trap="TC_RE_STACK_OVERFLOW",
    ),
    lib_case(
        "TC-AOT-ABI-0007",
        "design §10.2 16-byte-aligned frame with narrow locals and a nested call",
        engine.program(
            "import lib",
            "var r: int32 = call lib.outer8, 3",
            "_ = writeln int32, r",
        ),
        out="8\n",
    ),
    lib_case(
        "TC-AOT-ABI-0008",
        "design §10.2 mixed bool/int8/int16/int64 parameters and int64 return",
        engine.program(
            "import lib",
            "var r: int64 = call lib.narrow, false, -1, -2, "
            "-9223372036854775808",
            "_ = writeln int64, r",
        ),
        out="-9223372036854775808\n",
    ),
    lib_case(
        "TC-AOT-ABI-0009",
        "design §10.2 all eight integer argument registers carry distinct values",
        engine.program(
            "import lib",
            "var r: int32 = call lib.eight, -1, -2, -3, -4, 5, 6, 7, 8",
            "_ = writeln int32, r",
        ),
        out="16\n",
    ),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    return engine.run_cases(CASES, "s7.abi", keep=args.keep)


if __name__ == "__main__":
    raise SystemExit(main())
