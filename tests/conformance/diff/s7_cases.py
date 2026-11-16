#!/usr/bin/env python3
"""S7 VM/AOT differential conformance cases.

The subset is the design §10.3 initial lowering set: scalar integer/bit/
comparison operations, br/br_if/switch/return/trap, direct calls, integer/bool
read/write/writeln, sizeof and copy.  Cases that need pointers, structs,
casts, floating point or indirect calls are explicitly outside S7 and are
rejected by the backend (see tests/conformance/tools/s7_toolchain_cases.py).

Existing S6 runtime fixtures are reused when their source is already inside the
subset; their expectations were independently derived for the reference VM.
New cases cover integer/bool formatting, token input, 64-bit arithmetic and
static storage.  Expectations below follow standard §6.3/§6.5/§6.10/§6.13 and
§11.2/§11.3, not AOT output.

Run:
    python3 tests/conformance/diff/s7_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

# S6 fixtures whose sources stay inside the S7 subset and use the W=64 profile.
S6_SUBSET = [
    "TC-RT-INT-0001",  # all integer/bit/compare forms
    "TC-RT-INT-0002",  # unsigned wrap and .wrap shifts
    "TC-RT-INT-0003",  # INT_MIN/INT_MAX, mod(INT_MIN,-1)
    "TC-RT-INT-0004",  # signed strict add overflow
    "TC-RT-INT-0005",  # div by zero
    "TC-RT-INT-0006",  # div(INT_MIN,-1)
    "TC-RT-INT-0007",  # neg(INT_MIN)
    "TC-RT-INT-0008",  # abs(INT_MIN)
    "TC-RT-INT-0009",  # negative shift count
    "TC-RT-INT-0010",  # strict/wrap shl, k>=n
    "TC-RT-INT-0011",  # mod by zero
    "TC-RT-INT-0012",  # signed mul overflow
    "TC-RT-INT-0013",  # negative shift under .wrap
    "TC-RT-INT-0014",  # negative shr count
    "TC-RT-INT-0015",  # signed subtract overflow
    "TC-RT-CTRL-0001",  # loop, switch and default target
    "TC-RT-CTRL-0002",  # explicit trap after committed output
    "TC-RT-CALL-0001",  # recursion and by-value parameters
    "TC-RT-CALL-0002",  # abstract depth D trap
    "TC-RT-IO-0003",  # invalid input token -> TC_RE_IO
    "TC-RT-IO-0004",  # EOF before a complete token -> TC_RE_IO
    "TC-RT-SIZEOF-0001",  # sizeof pointer/fnptr/isize/int64
    "TC-RT-W-0002",  # 64-bit isize addition in range
    "TC-RT-OPT-0001",  # zero-pass O2 produces the same output
]

CASES = [engine.load_s6_case(case_id) for case_id in S6_SUBSET] + [
    engine.case(
        "TC-DIFF-A64-0001",
        "§11.2 integer/bool output formats across all widths",
        root=engine.program(
            "var a: int8 = copy int8, -128",
            "_ = writeln int8, a",
            "var b: int16 = copy int16, -32768",
            "_ = writeln int16, b",
            "var c: int32 = copy int32, -2147483648",
            "_ = writeln int32, c",
            "var d: int64 = copy int64, -9223372036854775808",
            "_ = writeln int64, d",
            "var e: uint64 = copy uint64, 18446744073709551615u",
            "_ = writeln uint64, e",
            "var f: bool = copy bool, false",
            "_ = writeln bool, f",
            "var g: usize = copy usize, 18446744073709551615",
            "_ = writeln usize, g",
            "var h: isize = copy isize, 9223372036854775807",
            "_ = writeln isize, h",
        ),
        out=("-128\n-32768\n-2147483648\n-9223372036854775808\n"
             "18446744073709551615\nfalse\n18446744073709551615\n"
             "9223372036854775807\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0002",
        "§11.3 integer/bool token input: whitespace, leading zeros and ranges",
        root=engine.program(
            "var a: int32 = read int32",
            "var b: bool = read bool",
            "var c: int32 = read int32",
            "var d: int64 = read int64",
            "var e: uint16 = read uint16",
            "var f: uint64 = read uint64",
            "_ = writeln int32, a",
            "_ = writeln bool, b",
            "_ = writeln int32, c",
            "_ = writeln int64, d",
            "_ = writeln uint16, e",
            "_ = writeln uint64, f",
        ),
        stdin="  -42\ntrue\n0007\n-9223372036854775808\n65535\n"
              "18446744073709551615\n",
        out=("-42\ntrue\n7\n-9223372036854775808\n65535\n"
             "18446744073709551615\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0003",
        "§3.10/§4.7 static var storage is shared by all functions of a module",
        files={
            "root.tc": engine.program(
                "import lib",
                "var a: int32 = call lib.get",
                "_ = writeln int32, a",
                "var b: int32 = call lib.bump",
                "_ = writeln int32, b",
                "var c: int32 = call lib.get",
                "_ = writeln int32, c",
            ),
            "lib.tc": engine.library(
                "public static var V: int32 = 5",
                "public func get() int32 then",
                "_ = return Self.V",
                "end",
                "public func bump() int32 then",
                "Self.V = add int32, Self.V, 1",
                "_ = return Self.V",
                "end",
            ),
        },
        out="5\n6\n6\n",
    ),
    engine.case(
        "TC-DIFF-A64-0004",
        "§6.3 64-bit unsigned wrap and signed mod at the INT64_MIN edge",
        root=engine.program(
            "var a: uint64 = copy uint64, 18446744073709551615u",
            "var b: uint64 = add uint64, a, 1",
            "_ = writeln uint64, b",
            "var c: uint64 = sub uint64, 0, 1",
            "_ = writeln uint64, c",
            "var d: int64 = copy int64, -9223372036854775808",
            "var e: int64 = mod int64, d, -1",
            "_ = writeln int64, e",
            "var f: int64 = mul int64, 3037000499, 3037000499",
            "_ = writeln int64, f",
        ),
        out=("0\n18446744073709551615\n0\n9223372030926249001\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0005",
        "§6.3 64-bit strict add overflow and 64-bit multiplication overflow",
        root=engine.program(
            "var a: int64 = copy int64, 9223372036854775807",
            "var b: int64 = add int64, a, 1",
        ),
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0006",
        "§6.3 64-bit shifts, .wrap and arithmetic/logical right shift",
        root=engine.program(
            "var a: int64 = shl int64, 1, 62",
            "_ = writeln int64, a",
            "var b: uint64 = shl.wrap uint64, 1, 63",
            "_ = writeln uint64, b",
            "var c: int64 = shr int64, -8, 1",
            "_ = writeln int64, c",
            "var d: uint64 = shr uint64, 18446744073709551615u, 63",
            "_ = writeln uint64, d",
        ),
        out=("4611686018427387904\n9223372036854775808\n-4\n1\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0007",
        "§6.3 shl strict out-of-range at the 64-bit boundary",
        root=engine.program(
            "var a: int64 = shl int64, 1, 63",
        ),
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0008",
        "§6.4 bool logical and/or/xor/not and eq/ne",
        root=engine.program(
            "var a: bool = and bool, true, false",
            "_ = writeln bool, a",
            "var b: bool = or bool, false, true",
            "_ = writeln bool, b",
            "var c: bool = xor bool, true, true",
            "_ = writeln bool, c",
            "var d: bool = not bool, false",
            "_ = writeln bool, d",
            "var e: bool = ne bool, true, false",
            "_ = writeln bool, e",
        ),
        out="false\ntrue\nfalse\ntrue\ntrue\n",
    ),
    engine.case(
        "TC-DIFF-A64-0009",
        "§6.10 switch on a 64-bit selector with negative default selection",
        root=engine.program(
            "var k: int64 = copy int64, -1",
            "_ = switch k, l0, l1, ld",
            "label l0:",
            "_ = writeln int32, 0",
            "_ = br fin",
            "label l1:",
            "_ = writeln int32, 1",
            "_ = br fin",
            "label ld:",
            "_ = writeln int32, 2",
            "_ = br fin",
            "label fin:",
        ),
        out="2\n",
    ),
    engine.case(
        "TC-DIFF-A64-0010",
        "§3.4/§3.12 static bool/int8/int16 initializer images and reads",
        files={
            "root.tc": engine.program(
                "import lib",
                "var b: bool = call lib.getB",
                "_ = writeln bool, b",
                "var x: int8 = call lib.getX",
                "_ = writeln int8, x",
                "var y: int16 = call lib.getY",
                "_ = writeln int16, y",
            ),
            "lib.tc": engine.library(
                "public static var B: bool = true",
                "public static var X: int8 = -5",
                "public static var Y: int16 = -300",
                "public func getB() bool then",
                "_ = return Self.B",
                "end",
                "public func getX() int8 then",
                "_ = return Self.X",
                "end",
                "public func getY() int16 then",
                "_ = return Self.Y",
                "end",
            ),
        },
        out="true\n-5\n-300\n",
    ),
    engine.case(
        "TC-DIFF-A64-0011",
        "§6.3 64-bit signed multiplication overflow",
        root=engine.program(
            "var a: int64 = copy int64, -9223372036854775808",
            "var b: int64 = mul int64, a, -1",
        ),
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0012",
        "§6.3 64-bit unsigned strict shl beyond the representable range",
        root=engine.program(
            "var a: uint64 = copy uint64, 18446744073709551615u",
            "var b: uint64 = shl uint64, a, 1",
        ),
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0013",
        "§6.3 int8 div(INT_MIN,-1) overflow at the narrow width",
        root=engine.program(
            "var a: int8 = copy int8, -128",
            "var b: int8 = div int8, a, -1",
        ),
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
]

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    return engine.run_cases(CASES, "s7.diff", keep=args.keep)


if __name__ == "__main__":
    raise SystemExit(main())
