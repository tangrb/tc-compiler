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
    engine.case(
        "TC-DIFF-A64-0014",
        "§6.3 narrow signed strict overflow after committed wrap output",
        root=engine.program(
            "var a: int8 = add.wrap int8, 127, 1",
            "_ = writeln int8, a",
            "var b: int16 = sub.wrap int16, -32768, 1",
            "_ = writeln int16, b",
            "var c: int8 = copy int8, 127",
            "var d: int8 = add int8, c, 1",
        ),
        out="-128\n32767\n",
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0015",
        "§6.3 unsigned fixed-mod arithmetic and neg at the U64MAX edge",
        root=engine.program(
            "var a: uint8 = add uint8, 255, 1",
            "_ = writeln uint8, a",
            "var b: uint8 = sub uint8, 0, 1",
            "_ = writeln uint8, b",
            "var c: uint8 = mul uint8, 255, 255",
            "_ = writeln uint8, c",
            "var d: uint16 = mul uint16, 256, 256",
            "_ = writeln uint16, d",
            "var e: uint64 = neg uint64, 18446744073709551615u",
            "_ = writeln uint64, e",
            "var f: int8 = neg.wrap int8, -128",
            "_ = writeln int8, f",
        ),
        out="0\n255\n1\n0\n1\n-128\n",
    ),
    engine.case(
        "TC-DIFF-A64-0016",
        "§6.3 div/mod truncation toward zero with sign following the dividend",
        root=engine.program(
            "var a: int8 = div int8, -7, 2",
            "_ = writeln int8, a",
            "var b: int8 = mod int8, -7, 2",
            "_ = writeln int8, b",
            "var c: int8 = div int8, 7, -2",
            "_ = writeln int8, c",
            "var d: int8 = mod int8, 7, -2",
            "_ = writeln int8, d",
            "var e: uint8 = div uint8, 7, 2",
            "_ = writeln uint8, e",
            "var f: uint8 = mod uint8, 7, 2",
            "_ = writeln uint8, f",
            "var g: uint8 = div uint8, 0, 5",
            "_ = writeln uint8, g",
        ),
        out="-3\n-1\n-3\n1\n3\n1\n0\n",
    ),
    engine.case(
        "TC-DIFF-A64-0017",
        "§6.3/§6.5 narrow bitwise ops and signed vs unsigned comparison",
        root=engine.program(
            "var a: int8 = not int8, 0",
            "_ = writeln int8, a",
            "var b: int8 = not int8, -1",
            "_ = writeln int8, b",
            "var c: int8 = and int8, -1, 15",
            "_ = writeln int8, c",
            "var d: uint16 = or uint16, 65534, 1",
            "_ = writeln uint16, d",
            "var e: uint16 = xor uint16, 4096, 4096",
            "_ = writeln uint16, e",
            "var f: bool = lt int8, -1, 1",
            "_ = writeln bool, f",
            "var g: bool = lt uint8, 255, 1",
            "_ = writeln bool, g",
            "var h: bool = gt uint8, 255, 1",
            "_ = writeln bool, h",
            "var i: bool = le int16, -32768, 32767",
            "_ = writeln bool, i",
        ),
        out="-1\n0\n15\n65535\n0\ntrue\nfalse\ntrue\ntrue\n",
    ),
    engine.case(
        "TC-DIFF-A64-0018",
        "§6.3 shift k>=n special rules and arithmetic vs logical shr",
        root=engine.program(
            "var a: int64 = shl.wrap int64, -1, 64",
            "_ = writeln int64, a",
            "var b: int64 = shr int64, -1, 64",
            "_ = writeln int64, b",
            "var c: int8 = shr int8, -1, 8",
            "_ = writeln int8, c",
            "var d: uint64 = shr uint64, 18446744073709551615u, 64",
            "_ = writeln uint64, d",
            "var e: uint8 = shr uint8, 255, 8",
            "_ = writeln uint8, e",
            "var f: int8 = shr int8, -1, 1",
            "_ = writeln int8, f",
            "var g: int64 = shl int64, 0, 64",
            "_ = writeln int64, g",
            "var h: uint8 = shl uint8, 0, 9",
            "_ = writeln uint8, h",
        ),
        out="0\n0\n0\n0\n0\n-1\n0\n0\n",
    ),
    engine.case(
        "TC-DIFF-A64-0019",
        "§6.3 unsigned narrow strict shl overflow after committed output",
        root=engine.program(
            "var a: uint8 = shl.wrap uint8, 128, 1",
            "_ = writeln uint8, a",
            "var b: uint16 = shl uint16, 32768, 1",
        ),
        out="0\n",
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0020",
        "§6.3 immediate materialization at 12-bit and 64-bit boundaries",
        root=engine.program(
            "var a: int64 = add int64, 100, 4095",
            "_ = writeln int64, a",
            "var b: int64 = add int64, 100, 4096",
            "_ = writeln int64, b",
            "var c: int64 = add int64, 100, 65535",
            "_ = writeln int64, c",
            "var d: int64 = add int64, 100, 65536",
            "_ = writeln int64, d",
            "var e: int64 = sub int64, 100, 4096",
            "_ = writeln int64, e",
            "var f: int64 = mul int64, 81985529216486895, 2",
            "_ = writeln int64, f",
            "var g: int64 = mul.wrap int64, 4294967296, 4294967296",
            "_ = writeln int64, g",
        ),
        out=("4195\n4196\n65635\n65636\n-3996\n163971058432973790\n0\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0021",
        "§6.10 switch with nine targets mixes dispatch and default hits",
        root=engine.program(
            "var k: int32 = copy int32, -2",
            "label loop:",
            "_ = switch k, c0, c1, c2, c3, c4, c5, c6, c7, dflt",
            "label c0:",
            "_ = writeln int32, 0",
            "_ = br next",
            "label c1:",
            "_ = writeln int32, 1",
            "_ = br next",
            "label c2:",
            "_ = writeln int32, 2",
            "_ = br next",
            "label c3:",
            "_ = writeln int32, 3",
            "_ = br next",
            "label c4:",
            "_ = writeln int32, 4",
            "_ = br next",
            "label c5:",
            "_ = writeln int32, 5",
            "_ = br next",
            "label c6:",
            "_ = writeln int32, 6",
            "_ = br next",
            "label c7:",
            "_ = writeln int32, 7",
            "_ = br next",
            "label dflt:",
            "_ = writeln int32, 999",
            "_ = br next",
            "label next:",
            "var hit_hi: bool = eq int32, k, 19",
            "_ = br_if hit_hi, done",
            "var one: int32 = copy int32, 1",
            "k = add int32, k, one",
            "_ = br loop",
            "label done:",
        ),
        out=("999\n999\n0\n1\n2\n3\n4\n5\n6\n7\n999\n999\n999\n999\n"
             "999\n999\n999\n999\n999\n999\n999\n999\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0022",
        "§6.10 switch with 34 targets exercises every dispatch slot and the "
        "default",
        root=engine.program(
            "var k: int32 = copy int32, 0",
            "label loop:",
            "_ = switch k, " + ", ".join(f"c{i}" for i in range(33)) +
            ", dflt",
            *[line
              for i in range(33)
              for line in (f"label c{i}:",
                           f"_ = writeln int32, {i}",
                           "_ = br next")],
            "label dflt:",
            "_ = writeln int32, 999",
            "_ = br next",
            "label next:",
            "var hit_hi: bool = eq int32, k, 39",
            "_ = br_if hit_hi, done",
            "var one: int32 = copy int32, 1",
            "k = add int32, k, one",
            "_ = br loop",
            "label done:",
        ),
        out="".join(
            (f"{v}\n" if v < 33 else "999\n") for v in range(40)),
    ),
    engine.case(
        "TC-DIFF-A64-0023",
        "§6.10 switch selects the default for extreme uint64/int64 selectors",
        root=engine.program(
            "var a: uint64 = copy uint64, 1099511627776",
            "_ = switch a, z0, z1, zd",
            "label z0:",
            "_ = writeln int32, 0",
            "_ = br m1",
            "label z1:",
            "_ = writeln int32, 1",
            "_ = br m1",
            "label zd:",
            "_ = writeln int32, 999",
            "_ = br m1",
            "label m1:",
            "var b: uint64 = copy uint64, 18446744073709551615u",
            "_ = switch b, y0, y1, yd",
            "label y0:",
            "_ = writeln int32, 0",
            "_ = br m2",
            "label y1:",
            "_ = writeln int32, 1",
            "_ = br m2",
            "label yd:",
            "_ = writeln int32, 999",
            "_ = br m2",
            "label m2:",
            "var c: int64 = copy int64, -1",
            "_ = switch c, x0, x1, xd",
            "label x0:",
            "_ = writeln int32, 0",
            "_ = br m3",
            "label x1:",
            "_ = writeln int32, 1",
            "_ = br m3",
            "label xd:",
            "_ = writeln int32, 999",
            "_ = br m3",
            "label m3:",
            "var d: int64 = copy int64, 1",
            "_ = switch d, w0, w1, wd",
            "label w0:",
            "_ = writeln int32, 0",
            "_ = br m4",
            "label w1:",
            "_ = writeln int32, 1",
            "_ = br m4",
            "label wd:",
            "_ = writeln int32, 999",
            "_ = br m4",
            "label m4:",
        ),
        out="999\n999\n999\n1\n",
    ),
    engine.case(
        "TC-DIFF-A64-0024",
        "§8.5 mixed-width call using all eight integer argument registers",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: int64 = call lib.mix, -128, 255u, -32768, 32767,"
                " -2147483648, 2147483647u, -9223372036854775808,"
                " 9223372036854775807",
                "_ = writeln int64, r",
            ),
            "lib.tc": engine.library(
                "public func mix(a: int8, b: uint16, c: int16, d: uint16,"
                " e: int32, f: uint32, g: int64, h: int64) int64 then",
                "_ = writeln int8, a",
                "_ = writeln uint16, b",
                "_ = writeln int16, c",
                "_ = writeln uint16, d",
                "_ = writeln int32, e",
                "_ = writeln uint32, f",
                "_ = writeln int64, g",
                "_ = writeln int64, h",
                "var zero: int64 = copy int64, 0",
                "_ = return zero",
                "end",
            ),
        },
        out=("-128\n255\n-32768\n32767\n-2147483648\n2147483647\n"
             "-9223372036854775808\n9223372036854775807\n0\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0025",
        "§6.12/§8.5 side-effecting calls commit output in source order",
        files={
            "root.tc": engine.program(
                "import lib",
                "var g1: int32 = call lib.g, 1",
                "var g2: int32 = call lib.g, 2",
                "var g3: int32 = call lib.g, 3",
                "var r: int32 = call lib.f2, g1, g2, g3",
                "_ = writeln int32, r",
            ),
            "lib.tc": engine.library(
                "public func g(n: int32) int32 then",
                "_ = writeln int32, n",
                "_ = return n",
                "end",
                "public func f2(a: int32, b: int32, c: int32) int32 then",
                "var s1: int32 = add int32, a, b",
                "var s2: int32 = add int32, s1, c",
                "_ = return s2",
                "end",
            ),
        },
        out="1\n2\n3\n6\n",
    ),
    engine.case(
        "TC-DIFF-A64-0026",
        "§8.5 mutual recursion through even/odd with a static call counter",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: bool = call lib.even, 200",
                "_ = writeln bool, r",
                "var c: int32 = call lib.count",
                "_ = writeln int32, c",
            ),
            "lib.tc": engine.library(
                "public static var Calls: int32 = 0",
                "public func even(n: int32) bool then",
                "Self.Calls = add int32, Self.Calls, 1",
                "var z: bool = eq int32, n, 0",
                "_ = br_if z, yes",
                "var m: int32 = sub int32, n, 1",
                "var r: bool = call Self.odd, m",
                "_ = return r",
                "label yes:",
                "_ = return true",
                "end",
                "public func odd(n: int32) bool then",
                "Self.Calls = add int32, Self.Calls, 1",
                "var z: bool = eq int32, n, 0",
                "_ = br_if z, yes",
                "var m: int32 = sub int32, n, 1",
                "var r: bool = call Self.even, m",
                "_ = return r",
                "label yes:",
                "_ = return false",
                "end",
                "public func count() int32 then",
                "_ = return Self.Calls",
                "end",
            ),
        },
        out="true\n201\n",
    ),
    engine.case(
        "TC-DIFF-A64-0027",
        "§1.2/§8.5 recursion depth 300 writes one line per frame",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: int64 = call lib.down, 300",
                "_ = writeln int64, r",
            ),
            "lib.tc": engine.library(
                "public func down(n: int64) int64 then",
                "_ = writeln int64, n",
                "var z: bool = eq int64, n, 0",
                "_ = br_if z, base",
                "var m: int64 = sub int64, n, 1",
                "var r: int64 = call Self.down, m",
                "var s: int64 = add int64, r, 1",
                "_ = return s",
                "label base:",
                "_ = return n",
                "end",
            ),
        },
        out="\n".join(str(v) for v in range(300, -1, -1)) + "\n300\n",
    ),
    engine.case(
        "TC-DIFF-A64-0028",
        "§1.2 depth D boundary keeps committed per-frame output before the "
        "stack overflow trap",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: int64 = call lib.down, 600",
                "_ = writeln int64, r",
            ),
            "lib.tc": engine.library(
                "public func down(n: int64) int64 then",
                "_ = writeln int64, n",
                "var z: bool = eq int64, n, 0",
                "_ = br_if z, base",
                "var m: int64 = sub int64, n, 1",
                "var r: int64 = call Self.down, m",
                "_ = return r",
                "label base:",
                "_ = return n",
                "end",
            ),
        },
        out="\n".join(str(v) for v in range(600, 88, -1)) + "\n",
        trap="TC_RE_STACK_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-A64-0029",
        "§11.3/§12.4 output committed before a read failure is preserved",
        root=engine.program(
            "_ = writeln int32, 7",
            "_ = writeln int32, 8",
            "var b: int32 = read int32",
            "_ = writeln int32, b",
        ),
        stdin="",
        out="7\n8\n",
        trap="TC_RE_IO",
    ),
    engine.case(
        "TC-DIFF-A64-0030",
        "§11.3 extreme tokens round-trip with unsigned wrap arithmetic",
        root=engine.program(
            "var a: uint64 = read uint64",
            "_ = writeln uint64, a",
            "var b: int64 = read int64",
            "_ = writeln int64, b",
            "var c: uint64 = add uint64, a, 1",
            "_ = writeln uint64, c",
            "var d: int64 = sub.wrap int64, b, 1",
            "_ = writeln int64, d",
        ),
        stdin="18446744073709551615 -9223372036854775808\n",
        out=("18446744073709551615\n-9223372036854775808\n0\n"
             "9223372036854775807\n"),
    ),
    engine.case(
        "TC-DIFF-A64-0031",
        "§11.3 bool token forms and signed/unsigned read ranges",
        root=engine.program(
            "var a: bool = read bool",
            "_ = writeln bool, a",
            "var b: bool = read bool",
            "_ = writeln bool, b",
            "var c: int8 = read int8",
            "_ = writeln int8, c",
            "var d: uint8 = read uint8",
            "_ = writeln uint8, d",
        ),
        stdin="true false -128 255\n",
        out="true\nfalse\n-128\n255\n",
    ),
    engine.case(
        "TC-DIFF-A64-0032",
        "§11.3 out-of-range uint8 token reports TC_RE_IO",
        root=engine.program(
            "var a: uint8 = read uint8",
            "_ = writeln uint8, a",
        ),
        stdin="256\n",
        trap="TC_RE_IO",
    ),
    engine.case(
        "TC-DIFF-A64-0033",
        "§8.2/§13 large frames with 400 locals survive two calls",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: int64 = call lib.big, 2",
                "_ = writeln int64, r",
            ),
            "lib.tc": engine.library(
                "public func big(m: int64) int64 then" +
                "".join(f"\nvar v{i}: int64 = copy int64, {i % 97}"
                        for i in range(400)) +
                "\nvar acc: int64 = copy int64, 0" +
                "".join(f"\nvar t{i}: int64 = add int64, acc, v{i}"
                        f"\nacc = copy int64, t{i}"
                        for i in range(0, 400, 50)) +
                "\nvar once: int64 = mul int64, acc, m"
                "\n_ = return once"
                "\nend",
            ),
        },
        out="472\n",
    ),
    engine.case(
        "TC-DIFF-A64-0034",
        "§8.5 nine-argument calls execute on the VM but stay outside the S7 "
        "backend subset",
        files={
            "root.tc": engine.program(
                "import lib",
                "var r: int64 = call lib.f9, 1, 2, 3, 4, 5, 6, 7, 8, 9",
                "_ = writeln int64, r",
            ),
            "lib.tc": engine.library(
                "public func f9(a: int64, b: int64, c: int64, d: int64,"
                " e: int64, f: int64, g: int64, h: int64, i: int64) int64"
                " then",
                "_ = return a",
                "end",
            ),
        },
        out="1\n",
        unsupported=True,
    ),
    engine.case(
        "TC-DIFF-A64-0035",
        "§11.3 input tokens with very long leading zero runs are valid",
        root=engine.program(
            "var a: int32 = read int32",
            "_ = writeln int32, a",
            "var b: uint64 = read uint64",
            "_ = writeln uint64, b",
            "var c: int8 = read int8",
            "_ = writeln int8, c",
        ),
        stdin="0" * 200 + "7\n" + "0" * 200 + "9\n" + "-0" + "0" * 100 +
              "5\n",
        out="7\n9\n-5\n",
    ),
    engine.case(
        "TC-DIFF-A64-0036",
        "§11.2/§11.3 write without newline mixes byte-exactly and a token "
        "may end at EOF",
        root=engine.program(
            "var a: int32 = copy int32, -1",
            "_ = write int32, a",
            "_ = write int32, 0",
            "_ = write bool, true",
            "_ = write uint8, 255",
            "_ = writeln int32, 42",
            "var b: int64 = read int64",
            "_ = write int64, b",
            "_ = writeln uint64, 18446744073709551615u",
        ),
        stdin="-9223372036854775808",
        out="-10true25542\n-922337203685477580818446744073709551615\n",
    ),
    engine.case(
        "TC-DIFF-A64-0037",
        "§3.12/§4.7 static initializer images at the signed/unsigned edges",
        files={
            "root.tc": engine.program(
                "import lib",
                "var a: uint64 = call lib.getU",
                "_ = writeln uint64, a",
                "var b: int64 = call lib.getI",
                "_ = writeln int64, b",
                "var c: uint8 = call lib.getP",
                "_ = writeln uint8, c",
                "var d: int8 = call lib.getN",
                "_ = writeln int8, d",
            ),
            "lib.tc": engine.library(
                "public static var U: uint64 = 18446744073709551615u",
                "public static var I: int64 = -9223372036854775808",
                "public static var P: uint8 = 255",
                "public static var N: int8 = -128",
                "public func getU() uint64 then",
                "_ = return Self.U",
                "end",
                "public func getI() int64 then",
                "_ = return Self.I",
                "end",
                "public func getP() uint8 then",
                "_ = return Self.P",
                "end",
                "public func getN() int8 then",
                "_ = return Self.N",
                "end",
            ),
        },
        out="18446744073709551615\n-9223372036854775808\n255\n-128\n",
    ),
    engine.case(
        "TC-DIFF-A64-0038",
        "§6.10/§7.5 switch targets may repeat and labels are reusable across "
        "switches",
        root=engine.program(
            "var k: int32 = copy int32, 2",
            "_ = switch k, a, a, b, b",
            "label a:",
            "_ = writeln int32, 10",
            "_ = br fin",
            "label b:",
            "_ = writeln int32, 20",
            "_ = br fin",
            "label fin:",
            "var m: int32 = copy int32, 4",
            "_ = switch m, a2, b2, zzz",
            "label a2:",
            "_ = writeln int32, 110",
            "_ = br done",
            "label b2:",
            "_ = writeln int32, 120",
            "_ = br done",
            "label zzz:",
            "_ = writeln int32, 999",
            "label done:",
        ),
        out="20\n999\n",
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
