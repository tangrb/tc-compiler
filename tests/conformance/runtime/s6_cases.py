#!/usr/bin/env python3
"""S6 runtime conformance case table for TC 0.0.46.

Every case executes a complete `.tc` source on the reference VM (`tc-vm`) and
asserts the observable behavior of standard §1.3: stdout bytes, the first
`TC_RE_*` trap, and the exit status.  Expectations are derived from the
standard clause cited in `spec`; integer and bool output follows §11.2 and
floating output is computed with Python's independent exact decimal renderer
(`format(value, '.6g')`, which implements the same six-significant-digit
roundTiesToEven rule).  No expectation is copied from VM output.

Run:
    python3 tests/conformance/runtime/s6_cases.py --write
    python3 tests/conformance/runtime/s6_cases.py --check
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUN = HERE / "run"


def f64(value: float) -> str:
    """Standard §11.2 six-significant-digit rendering of a binary64 value."""
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "-inf" if value < 0 else "inf"
    return format(value, ".6g")


def f32(value: float) -> float:
    """Round a value to binary32 (roundTiesToEven)."""
    return struct.unpack(">f", struct.pack(">f", value))[0]


def f32s(value: float) -> str:
    return f64(f32(value))


def hexout(text: str) -> str:
    return text.encode("utf-8").hex()


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def run_case(cid, profile, spec, out="", trap=None, stdin="", checked=False,
             opt=None, files=None, include_roots=None):
    return {
        "id": cid,
        "profile": profile,
        "spec": spec,
        "files": files if files is not None else {"root.tc": program()},
        "stdout": out,
        "trap": trap,
        "stdin": stdin,
        "checked": checked,
        "opt": opt,
        "include_roots": list(include_roots or []),
    }


CASES = [
    # ------------------------------------------------------------------
    # Integer arithmetic, bit/logical operations and comparisons (§6.3/§6.4/§6.5).
    run_case(
        "TC-RT-INT-0001", "w64",
        "§6.3 integer arithmetic; §6.4 bit/logical; §6.5 comparison; §11.2 output",
        files={"root.tc": program(
            "var a: int32 = copy int32, 7",
            "var b: int32 = copy int32, 5",
            "var s: int32 = add int32, a, b",
            "_ = writeln int32, s",
            "var d: int32 = sub int32, a, b",
            "_ = writeln int32, d",
            "var m: int32 = mul int32, a, b",
            "_ = writeln int32, m",
            "var q: int32 = div int32, -7, 2",
            "_ = writeln int32, q",
            "var r: int32 = mod int32, -7, 2",
            "_ = writeln int32, r",
            "var n: int32 = neg int32, a",
            "_ = writeln int32, n",
            "var ab: int32 = abs int32, n",
            "_ = writeln int32, ab",
            "var sh: int32 = shl int32, 1, 4",
            "_ = writeln int32, sh",
            "var sr: int32 = shr int32, -8, 1",
            "_ = writeln int32, sr",
            "var an: int32 = and int32, 12, 10",
            "_ = writeln int32, an",
            "var o: int32 = or int32, 12, 10",
            "_ = writeln int32, o",
            "var x: int32 = xor int32, 12, 10",
            "_ = writeln int32, x",
            "var no: int32 = not int32, 0",
            "_ = writeln int32, no",
            "var r_lt: bool = lt int32, -1, 0",
            "_ = writeln bool, r_lt",
            "var r_eq: bool = eq int32, 3, 3",
            "_ = writeln bool, r_eq",
            "var r_ba: bool = and bool, true, false",
            "_ = writeln bool, r_ba",
        )},
        out="12\n2\n35\n-3\n-1\n-7\n7\n16\n-4\n8\n14\n6\n-1\ntrue\ntrue\nfalse\n",
    ),
    run_case(
        "TC-RT-INT-0002", "w64",
        "§6.3 unsigned wrap, .wrap mode, k>=n shift rules; §11.2 output",
        files={"root.tc": program(
            "var a: uint8 = add uint8, 255, 1",
            "_ = writeln uint8, a",
            "var b: uint8 = sub uint8, 0, 1",
            "_ = writeln uint8, b",
            "var c: int8 = add.wrap int8, 127, 1",
            "_ = writeln int8, c",
            "var d: int8 = shl.wrap int8, 1, 8",
            "_ = writeln int8, d",
            "var e: int8 = neg.wrap int8, -128",
            "_ = writeln int8, e",
            "var f: uint32 = add uint32, 4294967295, 1",
            "_ = writeln uint32, f",
            "var g: int8 = shl.wrap int8, 1, 7",
            "_ = writeln int8, g",
            "var h: int8 = sub.wrap int8, -128, 1",
            "_ = writeln int8, h",
            "var j: int8 = mul.wrap int8, 100, 3",
            "_ = writeln int8, j",
        )},
        out="0\n255\n-128\n0\n-128\n0\n-128\n127\n44\n",
    ),
    run_case(
        "TC-RT-INT-0003", "w64",
        "§6.3 INT_MIN/INT_MAX edges; mod(INT_MIN,-1)=0; §11.2 output",
        files={"root.tc": program(
            "var min: int32 = copy int32, -2147483648",
            "var max: int32 = copy int32, 2147483647",
            "_ = writeln int32, min",
            "_ = writeln int32, max",
            "var r: int32 = mod int32, min, -1",
            "_ = writeln int32, r",
            "var imin: int64 = copy int64, -9223372036854775808",
            "var imax: int64 = copy int64, 9223372036854775807",
            "_ = writeln int64, imin",
            "_ = writeln int64, imax",
        )},
        out="-2147483648\n2147483647\n0\n-9223372036854775808\n9223372036854775807\n",
    ),
    run_case(
        "TC-RT-INT-0004", "w64",
        "§6.3 signed strict overflow -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int32 = copy int32, 2147483647",
            "var b: int32 = add int32, a, 1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0005", "w64",
        "§6.3 integer division by zero -> TC_RE_DIV_ZERO",
        files={"root.tc": program(
            "var z: int32 = copy int32, 0",
            "var x: int32 = div int32, 1, z",
        )},
        trap="TC_RE_DIV_ZERO",
    ),
    run_case(
        "TC-RT-INT-0006", "w64",
        "§6.3 div(INT_MIN,-1) -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var x: int32 = div int32, -2147483648, -1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0007", "w64",
        "§6.3 neg(INT_MIN) -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var x: int32 = neg int32, -2147483648",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0008", "w64",
        "§6.3 abs(INT_MIN) -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var x: int32 = abs int32, -2147483648",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0009", "w64",
        "§6.3 negative shift count -> TC_RE_NEGATIVE_SHIFT (before overflow)",
        files={"root.tc": program(
            "var k: int32 = copy int32, -1",
            "var x: int32 = shl int32, 1, k",
        )},
        trap="TC_RE_NEGATIVE_SHIFT",
    ),
    run_case(
        "TC-RT-INT-0010", "w64",
        "§6.3 strict shl k>=n and a=0; .wrap truncation; §11.2 output",
        files={"root.tc": program(
            "var z: int8 = shl int8, 0, 8",
            "_ = writeln int8, z",
            "var w: int8 = shl.wrap int8, 1, 8",
            "_ = writeln int8, w",
            "var s: int8 = shr int8, -128, 8",
            "_ = writeln int8, s",
            "var u: uint8 = shr uint8, 255, 4",
            "_ = writeln uint8, u",
        )},
        out="0\n0\n0\n15\n",
    ),
    # ------------------------------------------------------------------
    # Floating arithmetic and exception priority (§6.3.1-§6.3.3, §10.6).
    run_case(
        "TC-RT-FP-0001", "w64",
        "§6.3 floating arithmetic/mod/abs/neg; §6.5 NaN comparisons; §11.2",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.5",
            "var b: float64 = copy float64, 2.5",
            "var s: float64 = add float64, a, b",
            "_ = writeln float64, s",
            "var d: float64 = sub float64, a, b",
            "_ = writeln float64, d",
            "var m: float64 = mul float64, a, b",
            "_ = writeln float64, m",
            "var q: float64 = div float64, a, b",
            "_ = writeln float64, q",
            "var r: float64 = mod float64, a, b",
            "_ = writeln float64, r",
            "var n: float64 = neg float64, a",
            "_ = writeln float64, n",
            "var ab: float64 = abs float64, n",
            "_ = writeln float64, ab",
            "var nanv: float64 = copy float64, nan",
            "var r_eq: bool = eq float64, nanv, nanv",
            "_ = writeln bool, r_eq",
            "var r_ne: bool = ne float64, nanv, nanv",
            "_ = writeln bool, r_ne",
            "var r_lt: bool = lt float64, 1.0, 2.0",
            "_ = writeln bool, r_lt",
        )},
        out=f"{f64(4.0)}\n{f64(-1.0)}\n{f64(3.75)}\n{f64(0.6)}\n"
            f"{f64(1.5)}\n{f64(-1.5)}\n{f64(1.5)}\nfalse\ntrue\ntrue\n",
    ),
    run_case(
        "TC-RT-FP-0002", "w64",
        "§6.3.1 NaN input is invalid before any other classification",
        files={"root.tc": program(
            "var n: float64 = copy float64, nan",
            "var x: float64 = div float64, n, 1.0",
        )},
        trap="TC_RE_FLOAT_INVALID",
    ),
    run_case(
        "TC-RT-FP-0003", "w64",
        "§6.3 strict floating division by zero -> TC_RE_DIV_ZERO",
        files={"root.tc": program(
            "var z: float64 = copy float64, 0.0",
            "var x: float64 = div float64, 1.0, z",
        )},
        trap="TC_RE_DIV_ZERO",
    ),
    run_case(
        "TC-RT-FP-0004", "w64",
        "§6.3 strict finite overflow -> TC_RE_FLOAT_OVERFLOW",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e308",
            "var x: float64 = add float64, a, a",
        )},
        trap="TC_RE_FLOAT_OVERFLOW",
    ),
    run_case(
        "TC-RT-FP-0005", "w64",
        "§6.3.2 tininess after rounding and inexact -> TC_RE_FLOAT_UNDERFLOW",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e-300",
            "var x: float64 = mul float64, a, a",
        )},
        trap="TC_RE_FLOAT_UNDERFLOW",
    ),
    run_case(
        "TC-RT-FP-0006", "w64",
        "§6.3 .ieee returns inf/NaN and never traps; §11.2 output",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e308",
            "var x: float64 = add.ieee float64, a, a",
            "_ = writeln float64, x",
            "var z: float64 = copy float64, 0.0",
            "var y: float64 = div.ieee float64, z, z",
            "_ = writeln float64, y",
            "var w: float64 = div.ieee float64, 1.0, z",
            "_ = writeln float64, w",
            "var u: float64 = mod.ieee float64, 1.0, z",
            "_ = writeln float64, u",
            "var m: float64 = mul.ieee float64, a, a",
            "_ = writeln float64, m",
            "var n: float64 = copy float64, -1.0e308",
            "var sb: float64 = sub.ieee float64, a, n",
            "_ = writeln float64, sb",
        )},
        out="inf\nnan\ninf\nnan\ninf\ninf\n",
    ),
    run_case(
        "TC-RT-FP-0007", "w64",
        "§6.3.2 exact subnormal results do not underflow; §11.2 output",
        files={"root.tc": program(
            "var m: float64 = copy float64, 4.9406564584124654e-324",
            "var one: float64 = copy float64, 1.0",
            "var a: float64 = mul float64, m, one",
            "_ = writeln float64, a",
            "var b: float64 = div float64, m, one",
            "_ = writeln float64, b",
            "var c: float64 = add float64, m, 0.0",
            "_ = writeln float64, c",
        )},
        out="4.94066e-324\n4.94066e-324\n4.94066e-324\n",
    ),
    run_case(
        "TC-RT-FP-0008", "w64",
        "§6.3.3 mod exact rational quotient, sign of dividend, b=inf",
        files={"root.tc": program(
            "var a: float64 = copy float64, 5.5",
            "var b: float64 = copy float64, 2.0",
            "var r: float64 = mod float64, a, b",
            "_ = writeln float64, r",
            "var n: float64 = copy float64, -5.5",
            "var s: float64 = mod float64, n, b",
            "_ = writeln float64, s",
            "var infv: float64 = copy float64, inf",
            "var t: float64 = mod float64, a, infv",
            "_ = writeln float64, t",
        )},
        out="1.5\n-1.5\n5.5\n",
    ),
    run_case(
        "TC-RT-FP-0009", "w64",
        "§11.2 binary32 rendering: 6 significant digits, min subnormal",
        files={"root.tc": program(
            "var a: float32 = copy float32, 0.1f",
            "_ = writeln float32, a",
            "var b: float32 = copy float32, 1.0e-45f",
            "_ = writeln float32, b",
            "var c: float32 = copy float32, 3.4028235e38f",
            "_ = writeln float32, c",
            "var d: float32 = div float32, 1.0f, 3.0f",
            "_ = writeln float32, d",
        )},
        out=f"{f32s(0.1)}\n{f32s(1.0e-45)}\n{f32s(3.4028235e38)}\n"
            f"{f32s(1.0 / 3.0)}\n",
    ),
    # ------------------------------------------------------------------
    # Conversions (§3.11, §6.6).
    run_case(
        "TC-RT-CONV-0001", "w64",
        "§6.6 cast/trunc/bitcast; §11.2 output",
        files={"root.tc": program(
            "var a: int32 = copy int32, -1",
            "var b: uint32 = bitcast uint32, a",
            "_ = writeln uint32, b",
            "var c: uint8 = trunc uint8, a",
            "_ = writeln uint8, c",
            "var d: float64 = cast float64, a",
            "_ = writeln float64, d",
            "var e: int8 = cast int8, d",
            "_ = writeln int8, e",
            "var f: float32 = cast float32, 0.1",
            "_ = writeln float32, f",
            "var g: bool = cast bool, 2.5",
            "_ = writeln bool, g",
            "var h: int32 = cast int32, true",
            "_ = writeln int32, h",
        )},
        out=f"4294967295\n255\n-1\n-1\n{f32s(0.1)}\ntrue\n1\n",
    ),
    run_case(
        "TC-RT-CONV-0002", "w64",
        "§6.6.1 non-representable cast -> TC_RE_CAST_OVERFLOW",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e300",
            "var b: int64 = cast int64, a",
        )},
        trap="TC_RE_CAST_OVERFLOW",
    ),
    run_case(
        "TC-RT-CONV-0003", "w64",
        "§6.6.1 float->int truncation toward zero and NaN/inf trap",
        files={"root.tc": program(
            "var a: float64 = copy float64, -0.5",
            "var b: int32 = cast int32, a",
            "_ = writeln int32, b",
            "var c: float64 = copy float64, nan",
            "var d: int32 = cast int32, c",
        )},
        out="0\n",
        trap="TC_RE_CAST_OVERFLOW",
    ),
    # ------------------------------------------------------------------
    # Memory, objects and pointers (§3.12, §6.7, §6.8, §9.4).
    run_case(
        "TC-RT-MEM-0001", "w64",
        "§6.7 load/store; §6.8 addr/ptradd/ptrsub/ptrdiff; §3.12 pointer states",
        files={"root.tc": program(
            "var a: int32 = copy int32, 11",
            "var b: int32 = copy int32, 22",
            "var pa: ptr<int32> = addr int32, a",
            "var q: ptr<int32> = addr int32, a",
            "var e: ptr<int32> = copy ptr<int32>, pa",
            "var f: ptr<int32> = copy ptr<int32>, pa",
            "unsafe then",
            "e = ptradd int32, pa, 1",
            "f = ptrsub int32, e, 1",
            "_ = store int32, pa, 33",
            "var x: int32 = load int32, pa",
            "_ = writeln int32, x",
            "var y: int32 = load int32, f",
            "_ = writeln int32, y",
            "end",
            "var d: isize = ptrdiff int32, e, pa",
            "_ = writeln isize, d",
            "var r_eq: bool = ptreq int32, pa, q",
            "_ = writeln bool, r_eq",
            "var r_ne: bool = ptrne int32, pa, e",
            "_ = writeln bool, r_ne",
            "var r_lt: bool = ptrlt int32, pa, e",
            "_ = writeln bool, r_lt",
            "var r_le: bool = ptrle int32, pa, e",
            "_ = writeln bool, r_le",
            "var r_gt: bool = ptrgt int32, e, pa",
            "_ = writeln bool, r_gt",
            "var r_ge: bool = ptrge int32, e, pa",
            "_ = writeln bool, r_ge",
        )},
        out="33\n33\n4\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n",
    ),
    run_case(
        "TC-RT-MEM-0002", "w64",
        "§3.7/§6.2.7 make/copy/field; §6.7 load/store struct by value",
        files={"root.tc": program(
            "struct S then",
            "    var a: int32",
            "    var b: int32",
            "end",
            "var s: S = make S, 3, 4",
            "var t: S = copy S, s",
            "t.a = add int32, t.a, 10",
            "_ = writeln int32, s.a",
            "_ = writeln int32, t.a",
            "_ = writeln int32, t.b",
            "var p: ptr<S> = addr S, s",
            "unsafe then",
            "_ = store S, p, t",
            "end",
            "_ = writeln int32, s.a",
            "_ = writeln int32, s.b",
        )},
        out="3\n13\n4\n13\n4\n",
    ),
    run_case(
        "TC-RT-MEM-0003", "w64",
        "§3.4 bool normalization on every read; §6.7 store bool writes 0x00/0x01",
        files={"root.tc": program(
            "var b: bool = copy bool, true",
            "var p: ptr<bool> = addr bool, b",
            "var pu: ptr<uint8> = bitcast ptr<uint8>, p",
            "var u: uint8 = copy uint8, 2",
            "unsafe then",
            "_ = store uint8, pu, u",
            "end",
            "var x: bool = copy bool, b",
            "_ = writeln bool, x",
            "var z: bool = copy bool, false",
            "var pz: ptr<bool> = addr bool, z",
            "unsafe then",
            "_ = store bool, pz, true",
            "end",
            "var pzu: ptr<uint8> = bitcast ptr<uint8>, pz",
            "unsafe then",
            "var byte: uint8 = load uint8, pzu",
            "_ = writeln uint8, byte",
            "end",
        )},
        out="true\n1\n",
    ),
    run_case(
        "TC-RT-MEM-0004", "w64",
        "§3.12/§6.7 memmove of one struct element, including a self-copy",
        files={"root.tc": program(
            "struct S then",
            "    var a: int32",
            "    var b: int32",
            "    var c: int32",
            "end",
            "var s: S = make S, 1, 2, 3",
            "var t: S = make S, 4, 5, 6",
            "var ps: ptr<S> = addr S, s",
            "var pt: ptr<S> = addr S, t",
            "unsafe then",
            "_ = memmove S, ps, pt, 1",
            "_ = memmove S, ps, ps, 1",
            "end",
            "_ = writeln int32, s.a",
            "_ = writeln int32, s.b",
            "_ = writeln int32, s.c",
        )},
        out="4\n5\n6\n",
    ),
    run_case(
        "TC-RT-MEM-0005", "w64",
        "§6.7 load null -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var x: int32 = load int32, p",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    run_case(
        "TC-RT-MEM-0006", "w64",
        "§6.7 store null -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "var v: int32 = copy int32, 1",
            "unsafe then",
            "_ = store int32, p, v",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    run_case(
        "TC-RT-MEM-0007", "w64",
        "§3.12 memmove n=0 still checks null pointers -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var a: int32 = copy int32, 1",
            "var pa: ptr<int32> = addr int32, a",
            "var pn: ptr<int32> = copy ptr<int32>, nullptr",
            "var n: usize = copy usize, 0",
            "unsafe then",
            "_ = memmove int32, pn, pa, n",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    run_case(
        "TC-RT-MEM-0008", "w64",
        "§6.7 code region access -> TC_RE_ADDR_UNAVAILABLE",
        files={
            "root.tc": program(
                "import lib",
                "var fp: fnptr<int32, int32> = funcaddr lib.add1",
                "unsafe then",
                "var a: usize = bitcast usize, fp",
                "var p: ptr<int32> = bitcast ptr<int32>, a",
                "var x: int32 = load int32, p",
                "end",
            ),
            "lib.tc": library(
                "public func add1(x: int32) int32 then",
                "var r: int32 = add int32, x, 1",
                "_ = return r",
                "end",
            ),
        },
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
    run_case(
        "TC-RT-MEM-0009", "w64",
        "§6.7 write through a readonly parameter object -> TC_RE_WRITE_PROTECTED",
        files={
            "root.tc": program(
                "import lib",
                "var x: int32 = call lib.f, 3",
            ),
            "lib.tc": library(
                "public func f(x: int32) int32 then",
                "var p: ptr<int32> = addr int32, x",
                "unsafe then",
                "_ = store int32, p, 5",
                "end",
                "_ = return x",
                "end",
            ),
        },
        trap="TC_RE_WRITE_PROTECTED",
    ),
    run_case(
        "TC-RT-MEM-0010", "w64",
        "§6.2.5 load.volatile/store.volatile execute like normal accesses on writable memory",
        files={"root.tc": program(
            "var a: int32 = copy int32, 1",
            "var p: ptr<int32> = addr int32, a",
            "unsafe then",
            "_ = store.volatile int32, p, 9",
            "var x: int32 = load.volatile int32, p",
            "_ = writeln int32, x",
            "end",
        )},
        out="9\n",
    ),
    run_case(
        "TC-RT-MEM-0011", "w64",
        "§6.8 ptrdiff result not representable in isize -> TC_RE_PTRDIFF_OVERFLOW",
        files={"root.tc": program(
            "var u: usize = copy usize, 18446744073709551615u",
            "var z: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var d: isize = ptrdiff int32, p, z",
            "end",
        )},
        trap="TC_RE_PTRDIFF_OVERFLOW",
    ),
    run_case(
        "TC-RT-MEM-0012", "w64",
        "§6.8 ptradd null pointer -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "var n: usize = copy usize, 1",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, n",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    # ------------------------------------------------------------------
    # Calls, function pointers and D (§6.9, §8.5).
    run_case(
        "TC-RT-CALL-0001", "w64",
        "§6.9 direct calls, recursion, by-value parameters and return",
        files={
            "root.tc": program(
                "import lib",
                "var r: int32 = call lib.fact, 5",
                "_ = writeln int32, r",
            ),
            "lib.tc": library(
                "public func fact(n: int32) int32 then",
                "var c: bool = le int32, n, 1",
                "_ = br_if c, base",
                "var m: int32 = sub int32, n, 1",
                "var r: int32 = call Self.fact, m",
                "var out: int32 = mul int32, n, r",
                "_ = return out",
                "label base:",
                "_ = return n",
                "end",
            ),
        },
        out="120\n",
    ),
    run_case(
        "TC-RT-CALL-0002", "w64",
        "§1.2 depth D: the 513th active call traps -> TC_RE_STACK_OVERFLOW",
        files={
            "root.tc": program(
                "import lib",
                "var r: int32 = call lib.down, 600",
            ),
            "lib.tc": library(
                "public func down(n: int32) int32 then",
                "var c: bool = eq int32, n, 0",
                "_ = br_if c, fin",
                "var m: int32 = sub int32, n, 1",
                "var r: int32 = call Self.down, m",
                "_ = return r",
                "label fin:",
                "_ = return n",
                "end",
            ),
        },
        trap="TC_RE_STACK_OVERFLOW",
    ),
    run_case(
        "TC-RT-CALL-0003", "w64",
        "§6.2.6 funcaddr/funceq/funcne and §6.9 call_indirect",
        files={
            "root.tc": program(
                "import lib",
                "var fp: fnptr<int32, int32> = funcaddr lib.add1",
                "var z: fnptr<int32, int32> = copy fnptr<int32, int32>, nullptr",
                "var a: bool = funceq fnptr<int32, int32>, fp, z",
                "_ = writeln bool, a",
                "var b: bool = funceq fnptr<int32, int32>, fp, fp",
                "_ = writeln bool, b",
                "var c: bool = funcne fnptr<int32, int32>, fp, z",
                "_ = writeln bool, c",
                "unsafe then",
                "var x: int32 = call_indirect fp, 41",
                "_ = writeln int32, x",
                "end",
            ),
            "lib.tc": library(
                "public func add1(x: int32) int32 then",
                "var r: int32 = add int32, x, 1",
                "_ = return r",
                "end",
            ),
        },
        out="false\ntrue\ntrue\n42\n",
    ),
    run_case(
        "TC-RT-CALL-0004", "w64",
        "§6.9 call_indirect nullptr -> TC_RE_NULL_FUNCTION_POINTER",
        files={"root.tc": program(
            "var fp: fnptr<int32, int32> = copy fnptr<int32, int32>, nullptr",
            "unsafe then",
            "var x: int32 = call_indirect fp, 1",
            "end",
        )},
        trap="TC_RE_NULL_FUNCTION_POINTER",
    ),
    run_case(
        "TC-RT-CALL-0005", "w64",
        "§3.7/§6.9 struct parameters and struct return by value",
        files={
            "root.tc": program(
                "import lib",
                "var s: lib.S = call lib.mk, 7",
                "var x: int32 = call lib.sum, s",
                "_ = writeln int32, x",
                "_ = writeln int32, s.a",
                "_ = writeln int64, s.b",
            ),
            "lib.tc": library(
                "public struct S then",
                "    var a: int32",
                "    var b: int64",
                "end",
                "public func mk(x: int32) S then",
                "var s: S = make S, x, 100",
                "_ = return s",
                "end",
                "public func sum(s: S) int32 then",
                "var b: int32 = cast int32, s.b",
                "var r: int32 = add int32, s.a, b",
                "_ = return r",
                "end",
            ),
        },
        out="107\n7\n100\n",
    ),
    run_case(
        "TC-RT-CALL-0006", "w64",
        "§10.3 the @fast_math permission stays inside the callee lexical body: direct "
        "and fnptr indirect calls keep the callee semantics and grant the caller no "
        "permission; §10.4 zero-pass baseline; §6.3 strict rounding; §11.2 output",
        files={
            "root.tc": program(
                "import lib",
                "var fp: fnptr<float64, float64> = funcaddr lib.fm",
                "unsafe then",
                "var direct: float64 = call lib.fm, 1.0",
                "_ = writeln float64, direct",
                "var indirect: float64 = call_indirect fp, 1.0",
                "_ = writeln float64, indirect",
                "var own: float64 = add float64, 1.0, 1.0e16",
                "var own2: float64 = sub float64, own, 1.0e16",
                "_ = writeln float64, own2",
                "var w: float64 = call lib.wrap, 2.0",
                "_ = writeln float64, w",
                "end",
            ),
            "lib.tc": library(
                "public @fast @fast_math func fm(x: float64) float64 then",
                "var t: float64 = add float64, x, 1.0e16",
                "var r: float64 = sub float64, t, 1.0e16",
                "_ = return r",
                "end",
                "public @fast func wrap(x: float64) float64 then",
                "var fp: fnptr<float64, float64> = funcaddr Self.fm",
                "var via: float64 = call_indirect fp, x",
                "var own: float64 = add float64, x, 1.0e16",
                "var own2: float64 = sub float64, own, 1.0e16",
                "_ = writeln float64, via",
                "_ = return own2",
                "end",
            ),
        },
        out="0\n0\n0\n2\n2\n",
    ),
    # ------------------------------------------------------------------
    # Control flow (§6.10, §7).
    run_case(
        "TC-RT-CTRL-0001", "w64",
        "§6.10 br/br_if/switch: loop accumulation and default target",
        files={"root.tc": program(
            "var i: int32 = copy int32, 0",
            "var s: int32 = copy int32, 0",
            "label loop:",
            "var c: bool = lt int32, i, 5",
            "_ = br_if c, body",
            "_ = br done",
            "label body:",
            "s = add int32, s, i",
            "i = add int32, i, 1",
            "_ = br loop",
            "label done:",
            "_ = writeln int32, s",
            "var k: int32 = copy int32, -3",
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
        )},
        out="10\n2\n",
    ),
    run_case(
        "TC-RT-CTRL-0002", "w64",
        "§6.2.8 trap -> TC_RE_TRAP; committed output is not rolled back",
        files={"root.tc": program(
            "_ = writeln int32, 7",
            "_ = trap",
            "_ = writeln int32, 8",
        )},
        out="7\n",
        trap="TC_RE_TRAP",
    ),
    # ------------------------------------------------------------------
    # Standard I/O (§11).
    run_case(
        "TC-RT-IO-0001", "w64",
        "§11.2 deterministic write/writeln formats for integers, bool and floats",
        files={"root.tc": program(
            "var i: int8 = copy int8, -128",
            "_ = writeln int8, i",
            "var u: uint64 = copy uint64, 18446744073709551615u",
            "_ = writeln uint64, u",
            "var b: bool = copy bool, true",
            "_ = writeln bool, b",
            "var z: float64 = copy float64, -0.0",
            "_ = writeln float64, z",
            "var n: float64 = copy float64, nan",
            "_ = writeln float64, n",
            "var infv: float64 = copy float64, -inf",
            "_ = writeln float64, infv",
            "var e: float64 = copy float64, 1000000.0",
            "_ = writeln float64, e",
        )},
        out="-128\n18446744073709551615\ntrue\n-0\nnan\n-inf\n1e+06\n",
    ),
    run_case(
        "TC-RT-IO-0002", "w64",
        "§11.3 read: leading whitespace, complete tokens, bool/float spellings",
        files={"root.tc": program(
            "var x: int32 = read int32",
            "var y: float64 = read float64",
            "var b: bool = read bool",
            "_ = writeln int32, x",
            "_ = writeln float64, y",
            "_ = writeln bool, b",
        )},
        stdin="  -42 0.5 true ",
        out="-42\n0.5\ntrue\n",
    ),
    run_case(
        "TC-RT-IO-0003", "w64",
        "§11.3 invalid/incomplete input token -> TC_RE_IO",
        files={"root.tc": program(
            "var x: int32 = read int32",
        )},
        stdin="12abc",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0004", "w64",
        "§11.3 EOF before a complete token -> TC_RE_IO",
        files={"root.tc": program(
            "var x: int32 = read int32",
            "var y: int32 = read int32",
        )},
        stdin="42",
        out="",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0005", "w32",
        "§11.3/§13.2 `read isize` range is the 32-bit range",
        files={"root.tc": program(
            "var x: isize = read isize",
            "_ = writeln isize, x",
        )},
        stdin="4294967295",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0006", "w64",
        "§11.3 leading zeros are accepted; `-0` keeps a negative zero float",
        files={"root.tc": program(
            "var x: int32 = read int32",
            "_ = writeln int32, x",
            "var y: float64 = read float64",
            "_ = writeln float64, y",
        )},
        stdin="0007 -0",
        out="7\n-0\n",
    ),
    # ------------------------------------------------------------------
    # sizeof and 32/64 word size (§6.13, §13.2).
    run_case(
        "TC-RT-SIZEOF-0001", "w64",
        "§6.13 sizeof under W=64; §13.2.1 pointer/fnptr/isize/usize are 8 bytes",
        files={"root.tc": program(
            "var a: usize = sizeof ptr<int32>",
            "_ = writeln usize, a",
            "var b: usize = sizeof isize",
            "_ = writeln usize, b",
            "var c: usize = sizeof fnptr<int32, int32>",
            "_ = writeln usize, c",
            "var d: usize = sizeof int64",
            "_ = writeln usize, d",
        )},
        out="8\n8\n8\n8\n",
    ),
    run_case(
        "TC-RT-SIZEOF-0002", "w32",
        "§6.13 sizeof under W=32; §13.2.1 pointer/fnptr/isize/usize are 4 bytes",
        files={"root.tc": program(
            "var a: usize = sizeof ptr<int32>",
            "_ = writeln usize, a",
            "var b: usize = sizeof isize",
            "_ = writeln usize, b",
            "var c: usize = sizeof int64",
            "_ = writeln usize, c",
        )},
        out="4\n4\n8\n",
    ),
    run_case(
        "TC-RT-W-0001", "w32",
        "§13.2.2 isize overflow on the 32-bit profile -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: isize = copy isize, 2147483647",
            "var b: isize = add isize, a, 1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-W-0002", "w64",
        "§13.2.2 the same isize addition is in range on the 64-bit profile",
        files={"root.tc": program(
            "var a: isize = copy isize, 2147483647",
            "var b: isize = add isize, a, 1",
            "_ = writeln isize, b",
        )},
        out="2147483648\n",
    ),
    run_case(
        "TC-RT-W-0003", "w32",
        "§13.2.2 usize/pointer width on the 32-bit profile",
        files={"root.tc": program(
            "var u: usize = copy usize, 4294967295",
            "_ = writeln usize, u",
            "var a: int32 = copy int32, 0",
            "var p: ptr<int32> = addr int32, a",
            "var nz: bool = ptrne int32, p, nullptr",
            "_ = writeln bool, nz",
        )},
        out="4294967295\ntrue\n",
    ),
    # ------------------------------------------------------------------
    # Static initialization (§4.7, §3.10) and zero-pass (§10.4).
    run_case(
        "TC-RT-CONST-0001", "w64",
        "§4.7 const/static var initializers are materialized before execution",
        files={
            "root.tc": program(
                "import lib",
                "var x: int32 = call lib.get",
                "_ = writeln int32, x",
                "_ = writeln int32, lib.K",
                "_ = writeln int32, lib.Z.a",
                "_ = writeln bool, lib.Z.b",
            ),
            "lib.tc": library(
                "public struct S then",
                "    var a: int32",
                "    readonly b: bool",
                "end",
                "public const K: int32 = 7",
                "public static var V: int32 = add int32, K, 1",
                "public const S0: S = make S, 5, true",
                "public static var Z: S = make S, S0.a, S0.b",
                "public func get() int32 then",
                "_ = return Self.V",
                "end",
            ),
        },
        out="8\n7\n5\ntrue\n",
    ),
    run_case(
        "TC-RT-OPT-0001", "w64",
        "§10.4 zero-pass baseline: O2 produces the same observable output as O0",
        opt="O2",
        files={"root.tc": program(
            "var a: int32 = copy int32, 6",
            "var b: int32 = mul int32, a, 7",
            "_ = writeln int32, b",
        )},
        out="42\n",
    ),
    # ------------------------------------------------------------------
    # Device policy of the hosted profile: REJECT (§8).
    run_case(
        "TC-RT-DEV-0001", "w64",
        "§6.7 device access; §13.1 environment profile; design §8 TC_DEV_REJECT -> TC_RE_ADDR_UNAVAILABLE",
        files={"root.tc": program(
            "var a: usize = copy usize, 0x20000000",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, a",
            "var x: int32 = load int32, p",
            "end",
        )},
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
    run_case(
        "TC-RT-INT-0011", "w64",
        "§6.3 integer modulo by zero -> TC_RE_DIV_ZERO",
        files={"root.tc": program(
            "var z: int32 = copy int32, 0",
            "var x: int32 = mod int32, 1, z",
        )},
        trap="TC_RE_DIV_ZERO",
    ),
    run_case(
        "TC-RT-INT-0012", "w64",
        "§6.3 signed strict multiplication overflow -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var x: int32 = mul int32, 100000, 100000",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0013", "w64",
        "§6.3 negative shift count traps even with .wrap",
        files={"root.tc": program(
            "var k: int32 = copy int32, -1",
            "var x: int32 = shl.wrap int32, 1, k",
        )},
        trap="TC_RE_NEGATIVE_SHIFT",
    ),
    run_case(
        "TC-RT-INT-0014", "w64",
        "§6.3 shr with a negative count -> TC_RE_NEGATIVE_SHIFT",
        files={"root.tc": program(
            "var k: int32 = copy int32, -1",
            "var x: int32 = shr int32, 1, k",
        )},
        trap="TC_RE_NEGATIVE_SHIFT",
    ),
    run_case(
        "TC-RT-MEM-0013", "w32",
        "§6.8 ptrdiff overflow under the 32-bit isize range",
        files={"root.tc": program(
            "var u: usize = copy usize, 4294967295",
            "var z: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var d: isize = ptrdiff int32, p, z",
            "end",
        )},
        trap="TC_RE_PTRDIFF_OVERFLOW",
    ),
    run_case(
        "TC-RT-MEM-0014", "w32",
        "§6.7 code-region access traps under the 32-bit profile",
        files={
            "root.tc": program(
                "import lib",
                "var fp: fnptr<int32, int32> = funcaddr lib.add1",
                "unsafe then",
                "var a: usize = bitcast usize, fp",
                "var p: ptr<int32> = bitcast ptr<int32>, a",
                "var x: int32 = load int32, p",
                "end",
            ),
            "lib.tc": library(
                "public func add1(x: int32) int32 then",
                "var r: int32 = add int32, x, 1",
                "_ = return r",
                "end",
            ),
        },
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
    run_case(
        "TC-RT-MEM-0015", "w64",
        "§6.2.5 store.volatile null pointer -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "var v: int32 = copy int32, 1",
            "unsafe then",
            "_ = store.volatile int32, p, v",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    run_case(
        "TC-RT-MEM-0016", "w64",
        "§6.2.5 load.volatile null pointer -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var x: int32 = load.volatile int32, p",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    run_case(
        "TC-RT-CONV-0004", "w32",
        "§6.6.1/§13.2.2 float->isize range under W=32 -> TC_RE_CAST_OVERFLOW",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e300",
            "var b: isize = cast isize, a",
        )},
        trap="TC_RE_CAST_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0015", "w64",
        "§6.3 signed strict subtraction overflow -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var x: int32 = sub int32, -2147483648, 1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-MEM-0017", "w64",
        "§6.8 ptrsub null pointer -> TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "var n: usize = copy usize, 1",
            "unsafe then",
            "var q: ptr<int32> = ptrsub int32, p, n",
            "end",
        )},
        trap="TC_RE_NULL_POINTER",
    ),
    # ------------------------------------------------------------------
    # Integer edge adversarial cases (§6.3 strict/wrap, W-flip §13.2.2).
    run_case(
        "TC-RT-INT-0020", "w64",
        "§6.3 int64 edges: mod(INT64_MIN,-1) is 0; div(INT64_MAX,1) is exact",
        files={"root.tc": program(
            "var a: int64 = mod int64, -9223372036854775808, -1",
            "_ = writeln int64, a",
            "var b: int64 = div int64, 9223372036854775807, 1",
            "_ = writeln int64, b",
        )},
        out="0\n9223372036854775807\n",
    ),
    run_case(
        "TC-RT-INT-0021", "w64",
        "§6.3 int64 div(INT64_MIN,-1) overflows -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int64 = div int64, -9223372036854775808, -1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0022", "w64",
        "§6.3 int64 neg(INT64_MIN) overflows -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int64 = neg int64, -9223372036854775808",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0023", "w64",
        "§6.3 int64 wrap and wide-shift paths: neg.wrap(INT64_MIN), shl.wrap k>=n, "
        "shr k>=n returns 0 without sign extension, shr k=n-1 is arithmetic",
        files={"root.tc": program(
            "var a: int64 = neg.wrap int64, -9223372036854775808",
            "_ = writeln int64, a",
            "var b: int64 = shl.wrap int64, 1, 64",
            "_ = writeln int64, b",
            "var c: int64 = shr int64, -1, 64",
            "_ = writeln int64, c",
            "var d: int64 = shr int64, -1, 63",
            "_ = writeln int64, d",
        )},
        out="-9223372036854775808\n0\n0\n-1\n",
    ),
    run_case(
        "TC-RT-INT-0024", "w64",
        "§6.3 k>=n: strict shl of a=0 is defined 0, nonzero a traps -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int64 = shl int64, 0, 64",
            "_ = writeln int64, a",
            "var b: int64 = shl int64, 1, 64",
        )},
        out="0\n",
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0025", "w64",
        "§6.3 unsigned strict shl k>=n with nonzero a overflows -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: usize = shl usize, 1, 64",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0026", "w64",
        "§6.3 unsigned multiplication wraps silently at both widths",
        files={"root.tc": program(
            "var a: uint32 = mul uint32, 4294967295, 3",
            "_ = writeln uint32, a",
            "var b: uint64 = mul uint64, 18446744073709551615, 2",
            "_ = writeln uint64, b",
        )},
        out="4294967293\n18446744073709551614\n",
    ),
    run_case(
        "TC-RT-INT-0027", "w64",
        "§6.3 mode matrix: unsigned shl defaults to strict; k=n-1 with a fitting "
        "result is exact and shl.wrap on unsigned shl is a defined combination",
        files={"root.tc": program(
            "var a: uint32 = shl uint32, 1, 31",
            "_ = writeln uint32, a",
            "var b: uint32 = shl.wrap uint32, 3, 31",
            "_ = writeln uint32, b",
        )},
        out="2147483648\n2147483648\n",
    ),
    run_case(
        "TC-RT-INT-0034", "w64",
        "§6.3 unsigned strict shl whose result does not fit T traps -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: uint32 = shl uint32, 3, 31",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0028", "w64",
        "§6.3 trunc-division sign grid for all divisor-sign combinations",
        files={"root.tc": program(
            "var a: int32 = div int32, -7, -2",
            "_ = writeln int32, a",
            "var b: int32 = mod int32, -7, -2",
            "_ = writeln int32, b",
            "var c: int32 = div int32, 7, -2",
            "_ = writeln int32, c",
            "var d: int32 = mod int32, 7, -2",
            "_ = writeln int32, d",
            "var e: int32 = div int32, -7, 2",
            "_ = writeln int32, e",
            "var f: int32 = mod int32, -7, 2",
            "_ = writeln int32, f",
        )},
        out="3\n-1\n-3\n1\n-3\n-1\n",
    ),
    run_case(
        "TC-RT-INT-0029", "w32",
        "§6.3/§13.2.2 W=32 isize INT_MIN edges: mod by -1 is 0; div by 1 exact",
        files={"root.tc": program(
            "var a: isize = mod isize, -2147483648, -1",
            "_ = writeln isize, a",
            "var b: isize = div isize, 2147483647, 1",
            "_ = writeln isize, b",
        )},
        out="0\n2147483647\n",
    ),
    run_case(
        "TC-RT-INT-0030", "w32",
        "§6.3/§13.2.2 W=32 isize div(INT_MIN,-1) overflows -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: isize = div isize, -2147483648, -1",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0031", "w32",
        "§6.3/§13.2.2 W=32 isize k>=n: shl of 0 is 0, shl of 1 traps -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: isize = shl isize, 0, 32",
            "_ = writeln isize, a",
            "var b: isize = shl isize, 1, 32",
        )},
        out="0\n",
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-INT-0032", "w32",
        "§6.3/§13.2.2 int64 arithmetic under W=32 stays 64-bit exact",
        files={"root.tc": program(
            "var a: int64 = mul int64, 3037000499, 3037000499",
            "_ = writeln int64, a",
            "var b: int64 = div int64, 9223372036854775807, 2",
            "_ = writeln int64, b",
            "var c: int64 = shl int64, 1, 62",
            "_ = writeln int64, c",
        )},
        out="9223372030926249001\n4611686018427387903\n4611686018427387904\n",
    ),
    run_case(
        "TC-RT-INT-0033", "w32",
        "§6.3 int64 strict multiplication overflow under W=32 -> TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int64 = mul int64, 3037000500, 3037000500",
        )},
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    # ------------------------------------------------------------------
    # Floating exception priority and mod edge adversarial cases (§6.3.1-§6.3.3).
    run_case(
        "TC-RT-FP-0010", "w64",
        "§6.3.1 0/0 is an invalid operation, not division by zero -> TC_RE_FLOAT_INVALID",
        files={"root.tc": program(
            "var z: float64 = copy float64, 0.0",
            "var x: float64 = div float64, z, z",
        )},
        trap="TC_RE_FLOAT_INVALID",
    ),
    run_case(
        "TC-RT-FP-0011", "w64",
        "§6.3.1 NaN operand beats division-by-zero classification -> TC_RE_FLOAT_INVALID",
        files={"root.tc": program(
            "var n: float64 = copy float64, nan",
            "var z: float64 = copy float64, 0.0",
            "var x: float64 = div float64, n, z",
        )},
        trap="TC_RE_FLOAT_INVALID",
    ),
    run_case(
        "TC-RT-FP-0012", "w64",
        "§6.5 unordered comparisons: eq/lt/le/gt/ge are false, ne is true for NaN",
        files={"root.tc": program(
            "var n: float64 = copy float64, nan",
            "var a: bool = eq float64, n, n",
            "_ = writeln bool, a",
            "var b: bool = ne float64, n, n",
            "_ = writeln bool, b",
            "var c: bool = lt float64, n, n",
            "_ = writeln bool, c",
            "var d: bool = le float64, n, n",
            "_ = writeln bool, d",
            "var e: bool = gt float64, n, n",
            "_ = writeln bool, e",
            "var f: bool = ge float64, n, n",
            "_ = writeln bool, f",
        )},
        out="false\ntrue\nfalse\nfalse\nfalse\nfalse\n",
    ),
    run_case(
        "TC-RT-FP-0013", "w64",
        "§6.3.3 exact zero remainder keeps the dividend sign in mod",
        files={"root.tc": program(
            "var a: float64 = mod float64, 5.0, 2.5",
            "_ = writeln float64, a",
            "var b: float64 = mod float64, -5.0, 2.5",
            "_ = writeln float64, b",
        )},
        out="0\n-0\n",
    ),
    run_case(
        "TC-RT-FP-0014", "w64",
        "§6.3.3 mod with b=-inf returns the dividend bit-exactly",
        files={"root.tc": program(
            "var n: float64 = copy float64, -inf",
            "var r: float64 = mod float64, 3.5, n",
            "_ = writeln float64, r",
        )},
        out="3.5\n",
    ),
    run_case(
        "TC-RT-FP-0015", "w64",
        "§6.3.3 mod exact rational remainder for a power-of-two dividend: 2^60 mod 3 = 1",
        files={"root.tc": program(
            "var a: float64 = mod float64, 1152921504606846976.0, 3.0",
            "_ = writeln float64, a",
            "var b: float64 = mod float64, -1152921504606846976.0, 3.0",
            "_ = writeln float64, b",
        )},
        out="1\n-1\n",
    ),
    run_case(
        "TC-RT-FP-0016", "w64",
        "§6.3 neg/abs flip only the sign bit: zeros stay zeros, NaN stays NaN",
        files={"root.tc": program(
            "var a: float64 = neg float64, -0.0",
            "_ = writeln float64, a",
            "var b: float64 = abs float64, -0.0",
            "_ = writeln float64, b",
            "var c: float64 = neg float64, inf",
            "_ = writeln float64, c",
            "var d: float64 = abs float64, -inf",
            "_ = writeln float64, d",
            "var e: float64 = neg float64, nan",
            "_ = writeln float64, e",
            "var f: float64 = abs float64, nan",
            "_ = writeln float64, f",
        )},
        out="0\n0\n-inf\ninf\nnan\nnan\n",
    ),
    run_case(
        "TC-RT-FP-0017", "w64",
        "§6.3.2 .ieee subnormal boundary: half of min subnormal rounds to zero (RT2E tie), "
        "three times min subnormal is an exact subnormal",
        files={"root.tc": program(
            "var a: float64 = mul.ieee float64, 5.0e-324, 0.5",
            "_ = writeln float64, a",
            "var b: float64 = mul.ieee float64, 5.0e-324, 3.0",
            "_ = writeln float64, b",
        )},
        out="0\n1.4822e-323\n",
    ),
    run_case(
        "TC-RT-FP-0018", "w64",
        "§6.3.2 strict product below min subnormal is inexact and tiny -> TC_RE_FLOAT_UNDERFLOW",
        files={"root.tc": program(
            "var a: float64 = mul float64, 5.0e-324, 0.5",
        )},
        trap="TC_RE_FLOAT_UNDERFLOW",
    ),
    run_case(
        "TC-RT-FP-0019", "w64",
        "§11.2 %g round-to-even ties at the 7th significant digit and rounding just above",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1234555.0",
            "_ = writeln float64, a",
            "var b: float64 = copy float64, 1234545.0",
            "_ = writeln float64, b",
            "var c: float64 = copy float64, 1234560.0",
            "_ = writeln float64, c",
            "var d: float64 = copy float64, 1234567.0",
            "_ = writeln float64, d",
        )},
        out="1.23456e+06\n1.23454e+06\n1.23456e+06\n1.23457e+06\n",
    ),
    run_case(
        "TC-RT-FP-0020", "w64",
        "§11.2 %g exponent boundary: e=-4 stays fixed, e=-5 goes scientific; 7th digit rounds",
        files={"root.tc": program(
            "var a: float64 = copy float64, 0.000123456",
            "_ = writeln float64, a",
            "var b: float64 = copy float64, 0.0000123456",
            "_ = writeln float64, b",
            "var c: float64 = copy float64, 0.0001234567",
            "_ = writeln float64, c",
        )},
        out="0.000123456\n1.23456e-05\n0.000123457\n",
    ),
    # ------------------------------------------------------------------
    # Conversion edge adversarial cases (§6.6, §6.6.1).
    run_case(
        "TC-RT-CONV-0010", "w64",
        "§6.6.1 int-to-int cast narrowing overflow -> TC_RE_CAST_OVERFLOW",
        files={"root.tc": program(
            "var a: int64 = copy int64, 200",
            "var b: int8 = cast int8, a",
        )},
        trap="TC_RE_CAST_OVERFLOW",
    ),
    run_case(
        "TC-RT-CONV-0011", "w64",
        "§6.6.3 int-to-float cast rounds-to-even at the 2^53 and 2^52 boundaries",
        files={"root.tc": program(
            "var a: float64 = cast float64, 9007199254740993",
            "_ = writeln float64, a",
            "var b: float64 = cast float64, 4503599627370497",
            "_ = writeln float64, b",
        )},
        out="9.0072e+15\n4.5036e+15\n",
    ),
    run_case(
        "TC-RT-CONV-0012", "w64",
        "§6.6.1 f64-to-f32 cast of a finite value beyond binary32 range -> TC_RE_CAST_OVERFLOW",
        files={"root.tc": program(
            "var a: float64 = copy float64, 1.0e300",
            "var b: float32 = cast float32, a",
        )},
        trap="TC_RE_CAST_OVERFLOW",
    ),
    run_case(
        "TC-RT-CONV-0013", "w64",
        "§6.6 f32-to-f64 widening preserves the binary32 value exactly",
        files={"root.tc": program(
            "var a: float32 = copy float32, 0.1f",
            "var b: float64 = cast float64, a",
            "_ = writeln float64, b",
        )},
        out="0.1\n",
    ),
    # ------------------------------------------------------------------
    # Input edge adversarial cases (§11.3).
    run_case(
        "TC-RT-IO-0007", "w64",
        "§11.3 float input accepts inf, -0.0 and exponent tokens; -0 stays negative",
        files={"root.tc": program(
            "var a: float64 = read float64",
            "_ = writeln float64, a",
            "var b: float64 = read float64",
            "_ = writeln float64, b",
            "var c: float64 = read float64",
            "_ = writeln float64, c",
        )},
        stdin="inf -0.0 1e5",
        out="inf\n-0\n100000\n",
    ),
    run_case(
        "TC-RT-IO-0008", "w64",
        "§11.3 '+' is rejected even in the exponent of a float token -> TC_RE_IO",
        files={"root.tc": program(
            "var a: float64 = read float64",
        )},
        stdin="1e+5",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0009", "w64",
        "§11.3 'f' suffix is not part of an input float token -> TC_RE_IO",
        files={"root.tc": program(
            "var a: float64 = read float64",
        )},
        stdin="1.5f",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0010", "w64",
        "§11.3 input token that rounds to infinity is out of range -> TC_RE_IO",
        files={"root.tc": program(
            "var a: float64 = read float64",
        )},
        stdin="1e400",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0011", "w64",
        "§11.3 nonzero input token that rounds to zero is out of range -> TC_RE_IO",
        files={"root.tc": program(
            "var a: float64 = read float64",
        )},
        stdin="1e-400",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0012", "w64",
        "§11.3 input decimal 2^52+0.5 rounds to even at the binary64 boundary",
        files={"root.tc": program(
            "var a: float64 = read float64",
            "_ = writeln float64, a",
        )},
        stdin="4503599627370496.5",
        out="4.5036e+15\n",
    ),
    run_case(
        "TC-RT-IO-0013", "w64",
        "§11.3 read at EOF fails even when the result is discarded with `_` -> TC_RE_IO",
        files={"root.tc": program(
            "_ = read float64",
        )},
        stdin="",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0014", "w32",
        "§11.3/§13.2.2 W=32 isize input beyond 2^31-1 is out of range -> TC_RE_IO",
        files={"root.tc": program(
            "var a: isize = read isize",
        )},
        stdin="2147483648",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0015", "w64",
        "§11.3 isize input beyond 2^63-1 is out of range -> TC_RE_IO",
        files={"root.tc": program(
            "var a: isize = read isize",
        )},
        stdin="9223372036854775808",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0016", "w64",
        "§11.3 binary32 input keeps a negative zero through read and print",
        files={"root.tc": program(
            "var a: float32 = read float32",
            "_ = writeln float32, a",
        )},
        stdin="-0",
        out="-0\n",
    ),
    # ------------------------------------------------------------------
    # Call depth boundary adversarial cases (§1.2, §8.8.3).
    run_case(
        "TC-RT-CALL-0007", "w64",
        "§1.2 depth D=512: exactly 512 active calls succeed and return",
        files={
            "root.tc": program(
                "import lib",
                "var r: int32 = call lib.down, 511",
                "_ = writeln int32, r",
            ),
            "lib.tc": library(
                "public func down(n: int32) int32 then",
                "var c: bool = eq int32, n, 0",
                "_ = br_if c, fin",
                "var m: int32 = sub int32, n, 1",
                "var r: int32 = call Self.down, m",
                "var s: int32 = add int32, r, 1",
                "_ = return s",
                "label fin:",
                "_ = return n",
                "end",
            ),
        },
        out="511\n",
    ),
    run_case(
        "TC-RT-CALL-0008", "w64",
        "§1.2 depth D=512: the call that would create the 513th frame traps -> TC_RE_STACK_OVERFLOW",
        files={
            "root.tc": program(
                "import lib",
                "var r: int32 = call lib.down, 512",
            ),
            "lib.tc": library(
                "public func down(n: int32) int32 then",
                "var c: bool = eq int32, n, 0",
                "_ = br_if c, fin",
                "var m: int32 = sub int32, n, 1",
                "var r: int32 = call Self.down, m",
                "var s: int32 = add int32, r, 1",
                "_ = return s",
                "label fin:",
                "_ = return n",
                "end",
            ),
        },
        trap="TC_RE_STACK_OVERFLOW",
    ),
    # ------------------------------------------------------------------
    # Field sub-object one-past adversarial case (§3.12, §6.8).
    run_case(
        "TC-RT-MEM-0018", "w64",
        "§3.12/§6.8 field one-past: ptradd n=0, ptrsub back are defined and "
        "ptrdiff is the exact byte difference (8 bytes for one int64 element)",
        files={"root.tc": program(
            "struct S then",
            "var a: int64",
            "var b: int64",
            "end",
            "var s: S = make S, 7, 9",
            "var p: ptr<int64> = addr int64, s.a",
            "unsafe then",
            "var q: ptr<int64> = ptradd int64, p, 1",
            "var r: ptr<int64> = ptradd int64, q, 0",
            "var t: ptr<int64> = ptrsub int64, r, 1",
            "var v: int64 = load int64, t",
            "var d: isize = ptrdiff int64, q, p",
            "end",
            "_ = writeln int64, v",
            "_ = writeln isize, d",
        )},
        out="7\n8\n",
    ),
    # ------------------------------------------------------------------
    # Bool representation smuggling through byte copies (§3.4, §3.12, §6.7).
    run_case(
        "TC-RT-MEM-0019", "w64",
        "§3.4/§3.12/§6.7 struct copy/memmove moves raw bytes: a non-canonical "
        "bool byte (0x02) read back normalizes to true, and bool stores write "
        "canonical 0x00/0x01 observable through a byte-level copy; ptr<->ptr "
        "bitcast is a safe operation",
        files={"root.tc": program(
            "struct Raw then",
            "var b: int8",
            "end",
            "struct Flag then",
            "var f: bool",
            "end",
            "var r: Raw = make Raw, 2",
            "var g: Flag = make Flag, false",
            "var pr: ptr<Raw> = addr Raw, r",
            "var pg: ptr<Flag> = addr Flag, g",
            "var pf: ptr<Flag> = bitcast ptr<Flag>, pr",
            "unsafe then",
            "_ = memmove Flag, pg, pf, 1",
            "end",
            "_ = writeln bool, g.f",
            "var r2: Raw = make Raw, 0",
            "var g2: Flag = make Flag, true",
            "var pr2: ptr<Raw> = addr Raw, r2",
            "var pg2: ptr<Flag> = addr Flag, g2",
            "var pf2: ptr<Flag> = bitcast ptr<Flag>, pg2",
            "var pr2r: ptr<Raw> = bitcast ptr<Raw>, pr2",
            "var dst2: ptr<Flag> = bitcast ptr<Flag>, pr2r",
            "unsafe then",
            "_ = memmove Flag, dst2, pf2, 1",
            "end",
            "_ = writeln int8, r2.b",
            "var g3: Flag = make Flag, false",
            "var pg3: ptr<Flag> = addr Flag, g3",
            "var pf3: ptr<Flag> = bitcast ptr<Flag>, pg3",
            "unsafe then",
            "_ = memmove Flag, dst2, pf3, 1",
            "end",
            "_ = writeln int8, r2.b",
        )},
        out="true\n1\n0\n",
    ),
    # ------------------------------------------------------------------
    # ptrdiff between two null pointers is the address-0 difference (§3.12).
    run_case(
        "TC-RT-MEM-0020", "w64",
        "§3.12/§6.8 null participates in ptrdiff as address 0: the difference "
        "of two null pointers is 0 for scalar and struct element types and "
        "does not trap; nullptr equality comparison is defined",
        files={"root.tc": program(
            "struct S then",
            "var x: int32",
            "end",
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "var q: ptr<int32> = copy ptr<int32>, nullptr",
            "var d: isize = ptrdiff int32, p, q",
            "_ = writeln isize, d",
            "var ps: ptr<S> = copy ptr<S>, nullptr",
            "var qs: ptr<S> = copy ptr<S>, nullptr",
            "var ds: isize = ptrdiff S, ps, qs",
            "_ = writeln isize, ds",
            "var e: bool = ptreq int32, p, q",
            "_ = writeln bool, e",
        )},
        out="0\n0\ntrue\n",
    ),
    # ------------------------------------------------------------------
    # Dangling pointers keep their original address relations (§3.12, §6.8).
    run_case(
        "TC-RT-MEM-0021", "w64",
        "§3.12/§6.8 dangling pointers may be copied, returned, compared and "
        "subtracted: ptrdiff of two dangling pointers into the same original "
        "object is the original byte difference and the comparison uses the "
        "original addresses",
        files={
            "root.tc": program(
                "import lib",
                "var r: lib.P = call lib.grab",
                "var d: isize = ptrdiff int64, r.p, r.q",
                "_ = writeln isize, d",
                "var less: bool = ptrlt int64, r.p, r.q",
                "_ = writeln bool, less",
                "var same: bool = ptreq int64, r.p, r.q",
                "_ = writeln bool, same",
            ),
            "lib.tc": library(
                "public struct P then",
                "var p: ptr<int64>",
                "var q: ptr<int64>",
                "end",
                "public struct S then",
                "var a: int64",
                "var b: int64",
                "end",
                "public func grab() P then",
                "var s: S = make S, 7, 9",
                "var pa: ptr<int64> = addr int64, s.a",
                "var pb: ptr<int64> = addr int64, s.b",
                "var r: P = make P, pa, pb",
                "_ = return r",
                "end",
            ),
        },
        out="-8\ntrue\nfalse\n",
    ),
    # ------------------------------------------------------------------
    # One module file shared by several importers is one instance (§4.5).
    run_case(
        "TC-RT-IMP-0001", "w64",
        "§4.5/§4.7 importers that resolve to the same module file share one "
        "module instance and one static storage: the root module and two "
        "library modules all importing shared.tc bump the same counter",
        files={
            "root.tc": program(
                "import shared",
                "import a",
                "import b",
                "var r0: int32 = call shared.bump",
                "_ = writeln int32, r0",
                "var r1: int32 = call a.fa",
                "_ = writeln int32, r1",
                "var r2: int32 = call b.fb",
                "_ = writeln int32, r2",
                "var r3: int32 = call shared.bump",
                "_ = writeln int32, r3",
            ),
            "a.tc": library(
                "import shared",
                "public func fa() int32 then",
                "var t: int32 = call shared.bump",
                "_ = return t",
                "end",
            ),
            "b.tc": library(
                "import shared",
                "public func fb() int32 then",
                "var t: int32 = call shared.bump",
                "_ = return t",
                "end",
            ),
            "shared.tc": library(
                "public static var C: int32 = 0",
                "public func bump() int32 then",
                "Self.C = add int32, Self.C, 1",
                "_ = return Self.C",
                "end",
            ),
        },
        out="1\n2\n3\n4\n",
    ),
    # ------------------------------------------------------------------
    # Same-name modules at different paths are separate instances (§4.5).
    run_case(
        "TC-RT-IMP-0002", "w64",
        "§4.5 module identity is the resolved file path: left/shared.tc and "
        "right/shared.tc are two module instances with independent static "
        "storage, so a.tc bumps only the left counter and b.tc only the "
        "right one",
        include_roots=["left", "right"],
        files={
            "root.tc": program(
                "import a",
                "import b",
                "var r1: int32 = call a.fa",
                "_ = writeln int32, r1",
                "var r2: int32 = call a.fa",
                "_ = writeln int32, r2",
                "var s1: int32 = call b.fb",
                "_ = writeln int32, s1",
                "var s2: int32 = call b.fb",
                "_ = writeln int32, s2",
            ),
            "left/a.tc": library(
                "import shared",
                "public func fa() int32 then",
                "var t: int32 = call shared.bump",
                "_ = return t",
                "end",
            ),
            "right/b.tc": library(
                "import shared",
                "public func fb() int32 then",
                "var t: int32 = call shared.bump",
                "_ = return t",
                "end",
            ),
            "left/shared.tc": library(
                "public static var C: int32 = 0",
                "public func bump() int32 then",
                "Self.C = add int32, Self.C, 1",
                "_ = return Self.C",
                "end",
            ),
            "right/shared.tc": library(
                "public static var C: int32 = 0",
                "public func bump() int32 then",
                "Self.C = add int32, Self.C, 1",
                "_ = return Self.C",
                "end",
            ),
        },
        out="1\n2\n1\n2\n",
    ),
    run_case(
        "TC-RT-IO-0017", "w64",
        "§11.1 committed stdout bytes are retained when a later instruction "
        "traps inside a called frame -> TC_RE_DIV_ZERO",
        files={
            "root.tc": program(
                "import lib",
                "_ = writeln int32, 111",
                "_ = call lib.crash",
            ),
            "lib.tc": library(
                "public func crash() int32 then",
                "var z: int32 = copy int32, 0",
                "var r: int32 = div int32, 1, z",
                "_ = return r",
                "end",
            ),
        },
        out="111\n",
        trap="TC_RE_DIV_ZERO",
    ),
    run_case(
        "TC-RT-MEM-0022", "w64",
        "§6.14 readonly matrix: addr of a readonly field + store writes "
        "through in unsafe, the sibling var field stays writable, the whole "
        "value can be replaced by make, the pointee of a readonly ptr field "
        "is unaffected by the field modifier, a copied local object is "
        "writable again, and a store through a parameter object field -> "
        "TC_RE_WRITE_PROTECTED",
        files={
            "root.tc": program(
                "import lib",
                "struct Holder then",
                "readonly p: ptr<int32>",
                "end",
                "var s: lib.Pair = make lib.Pair, 5, 6",
                "var pro: ptr<int32> = addr int32, s.ro",
                "unsafe then",
                "_ = store int32, pro, 50",
                "end",
                "var prw: ptr<int32> = addr int32, s.rw",
                "unsafe then",
                "_ = store int32, prw, 60",
                "end",
                "_ = writeln int32, s.ro",
                "_ = writeln int32, s.rw",
                "s = make lib.Pair, 7, 8",
                "_ = writeln int32, s.ro",
                "var v: int32 = copy int32, 1",
                "var pv: ptr<int32> = addr int32, v",
                "var h: Holder = make Holder, pv",
                "unsafe then",
                "_ = store int32, h.p, 42",
                "end",
                "_ = writeln int32, v",
                "var q: lib.Pair = copy lib.Pair, s",
                "var pq: ptr<int32> = addr int32, q.ro",
                "unsafe then",
                "_ = store int32, pq, 99",
                "end",
                "_ = writeln int32, q.ro",
                "var t: int32 = call lib.touch, s",
                "_ = writeln int32, t",
            ),
            "lib.tc": library(
                "public struct Pair then",
                "readonly ro: int32",
                "var rw: int32",
                "end",
                "public func touch(p: Pair) int32 then",
                "var pf: ptr<int32> = addr int32, p.rw",
                "unsafe then",
                "_ = store int32, pf, 77",
                "end",
                "var r: int32 = copy int32, p.rw",
                "_ = return r",
                "end",
            ),
        },
        out="50\n60\n7\n42\n99\n",
        trap="TC_RE_WRITE_PROTECTED",
    ),
    run_case(
        "TC-RT-MEM-0023", "w64",
        "§6.7 unaligned multi-byte access at byte-offset addresses; "
        "little-endian per the documented profile contract (§13.2): an "
        "int32 store at +1 inside an int64 object and the resulting byte "
        "smear, a float64 store/load at +2",
        files={"root.tc": program(
            "struct S then",
            "var c: int64",
            "end",
            "struct T then",
            "var c: int64",
            "var d: int64",
            "end",
            "var s: S = make S, 11259375",
            "var ps: ptr<int64> = addr int64, s.c",
            "unsafe then",
            "var b: ptr<int8> = bitcast ptr<int8>, ps",
            "var o: ptr<int8> = ptradd int8, b, 1",
            "var p32: ptr<int32> = bitcast ptr<int32>, o",
            "_ = store int32, p32, 287454020",
            "var v: int32 = load int32, p32",
            "var w: int64 = load int64, ps",
            "end",
            "_ = writeln int32, v",
            "_ = writeln int64, w",
            "var t: T = make T, 1, 2",
            "var pt: ptr<int64> = addr int64, t.c",
            "unsafe then",
            "var b2: ptr<int8> = bitcast ptr<int8>, pt",
            "var o2: ptr<int8> = ptradd int8, b2, 2",
            "var p64: ptr<float64> = bitcast ptr<float64>, o2",
            "_ = store float64, p64, 2.5",
            "var f: float64 = load float64, p64",
            "end",
            "_ = writeln float64, f",
        )},
        out="287454020\n73588229359\n2.5\n",
    ),
    run_case(
        "TC-RT-FP-0021", "w64",
        "§6.3.3 mod keeps the sign of a when r=0 (including -0) and "
        "b=±inf yields the bit-equal a",
        files={"root.tc": program(
            "var a: float64 = copy float64, -4.0",
            "var b: float64 = copy float64, 2.0",
            "var r: float64 = mod float64, a, b",
            "_ = writeln float64, r",
            "var c: float64 = copy float64, 4.0",
            "var d: float64 = copy float64, -2.0",
            "var s: float64 = mod float64, c, d",
            "_ = writeln float64, s",
            "var p: float64 = copy float64, 5.5",
            "var infv: float64 = copy float64, inf",
            "var t: float64 = mod float64, p, infv",
            "_ = writeln float64, t",
            "var n: float64 = copy float64, -5.5",
            "var ninf: float64 = copy float64, -inf",
            "var u: float64 = mod float64, n, ninf",
            "_ = writeln float64, u",
        )},
        out="-0\n0\n5.5\n-5.5\n",
    ),
    run_case(
        "TC-RT-FP-0022", "w64",
        "§6.3.3 strict float mod with finite nonzero a and b=0 -> "
        "TC_RE_DIV_ZERO after committed output",
        files={"root.tc": program(
            "_ = writeln float64, 1.5",
            "var z: float64 = copy float64, 0.0",
            "var r: float64 = mod float64, 1.0, z",
            "_ = writeln float64, r",
        )},
        out="1.5\n",
        trap="TC_RE_DIV_ZERO",
    ),
    run_case(
        "TC-RT-FP-0023", "w64",
        "§6.3.3 strict float mod with a=inf -> TC_RE_FLOAT_INVALID after "
        "committed output",
        files={"root.tc": program(
            "_ = writeln float64, 2.5",
            "var infv: float64 = copy float64, inf",
            "var r: float64 = mod float64, infv, 2.0",
            "_ = writeln float64, r",
        )},
        out="2.5\n",
        trap="TC_RE_FLOAT_INVALID",
    ),
    run_case(
        "TC-RT-CONV-0014", "w64",
        "§6.6.1 int->float cast applies roundTiesToEven (float32 2^24+1); "
        "§3.13 equal-width int->float bitcast exposes the minimal binary32 "
        "subnormal bit pattern",
        files={"root.tc": program(
            "var i: int32 = copy int32, 16777217",
            "var f: float32 = cast float32, i",
            "_ = writeln float32, f",
            "var b: int32 = copy int32, 1",
            "var g: float32 = bitcast float32, b",
            "_ = writeln float32, g",
        )},
        out=f"{f32s(16777217)}\n"
            f"{f32s(struct.unpack('>f', struct.pack('>I', 1))[0])}\n",
    ),
    run_case(
        "TC-RT-CALL-0009", "w64",
        "§6.2.6 funceq/funcne on nullptr operands: nullptr equals nullptr",
        files={"root.tc": program(
            "var z: fnptr<int32, int32> = copy fnptr<int32, int32>, nullptr",
            "var a: bool = funceq fnptr<int32, int32>, z, z",
            "_ = writeln bool, a",
            "var b: bool = funcne fnptr<int32, int32>, z, z",
            "_ = writeln bool, b",
        )},
        out="true\nfalse\n",
    ),
    run_case(
        "TC-RT-INT-0035", "w64",
        "§6.3 k>=n: strict shl of a=0 yields 0 and shr yields 0 without "
        "sign extension",
        files={"root.tc": program(
            "var z: uint32 = shl uint32, 0, 64",
            "_ = writeln uint32, z",
            "var n: int32 = copy int32, -1",
            "var s: int32 = shr int32, n, 33",
            "_ = writeln int32, s",
        )},
        out="0\n0\n",
    ),
    run_case(
        "TC-RT-INT-0036", "w64",
        "§6.3 k>=n with nonzero a in strict shl -> TC_RE_INTEGER_OVERFLOW "
        "after committed output",
        files={"root.tc": program(
            "_ = writeln int32, 9",
            "var x: int32 = shl int32, 1, 32",
        )},
        out="9\n",
        trap="TC_RE_INTEGER_OVERFLOW",
    ),
    run_case(
        "TC-RT-IO-0018", "w64",
        "§11.3 EOF at the very start of the input stream -> TC_RE_IO",
        files={"root.tc": program(
            "var x: int32 = read int32",
        )},
        stdin="",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-IO-0019", "w64",
        "§11.1/§11.3 committed stdout bytes are retained when a discarded "
        "`_ = read` hits EOF -> TC_RE_IO",
        files={"root.tc": program(
            "_ = writeln int32, 42",
            "_ = read int32",
        )},
        stdin="",
        out="42\n",
        trap="TC_RE_IO",
    ),
    run_case(
        "TC-RT-CTRL-0003", "w64",
        "§7.7.2 unreachable code (a no-predecessor block) does not "
        "participate in DI: reading a variable never initialized on any "
        "real path is legal there while the reachable region still runs",
        files={"root.tc": program(
            "var c: bool = copy bool, false",
            "_ = br_if c, A",
            "_ = br M",
            "label A:",
            "var x: int32 = copy int32, 1",
            "_ = br M",
            "label M:",
            "_ = writeln int32, 7",
            "_ = br E",
            "label DEAD:",
            "_ = write int32, x",
            "_ = writeln int32, 8",
            "label E:",
            "_ = writeln int32, 9",
        )},
        out="7\n9\n",
    ),
]


def _yaml_scalar(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render(case):
    cid = case["id"]
    directory = RUN / cid.lower()
    files = {}
    for name, text in case["files"].items():
        files[name] = text
    lines = [
        f"id: {cid}",
        f"profile: {case['profile']}",
        "source: root.tc",
        f"spec: {_yaml_scalar(case['spec'])}",
        "kind: run",
    ]
    if case["stdin"]:
        lines.append("run:")
        lines.append(f"  stdin_hex: {_yaml_scalar(hexout(case['stdin']))}")
    if case["checked"]:
        lines.append("checked: true")
    if case["opt"] is not None:
        lines.append(f"opt: {case['opt']}")
    if case["include_roots"]:
        roots = ", ".join(_yaml_scalar(root) for root in case["include_roots"])
        lines.append(f"include_roots: [{roots}]")
    lines.append("expect:")
    lines.append(f"  stdout_hex: {_yaml_scalar(hexout(case['stdout']))}")
    lines.append(f"  trap: {case['trap'] if case['trap'] is not None else 'null'}")
    lines.append(f"  exit: {0 if case['trap'] is None else 1}")
    lines.append(
        f"  termination: {'normal' if case['trap'] is None else 'trap'}"
    )
    yaml = "\n".join(lines) + "\n"
    return directory, files, yaml


def write_all():
    RUN.mkdir(parents=True, exist_ok=True)
    for case in CASES:
        directory, files, yaml = render(case)
        directory.mkdir(parents=True, exist_ok=True)
        for name, text in files.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        (directory / f"{case['id'].lower()}.yaml").write_text(
            yaml, encoding="utf-8"
        )
    # Remove stale generated directories not in the table.
    known = {case["id"].lower() for case in CASES}
    if RUN.exists():
        for entry in sorted(RUN.iterdir()):
            if entry.is_dir() and entry.name not in known:
                for child in entry.iterdir():
                    child.unlink()
                entry.rmdir()
            elif entry.is_file() and entry.suffix == ".yaml":
                entry.unlink()


def check_all() -> int:
    errors = 0
    for case in CASES:
        directory, files, yaml = render(case)
        yaml_path = directory / f"{case['id'].lower()}.yaml"
        if not yaml_path.exists() or yaml_path.read_text(encoding="utf-8") != yaml:
            print(f"stale or missing {yaml_path}")
            errors += 1
        for name, text in files.items():
            path = directory / name
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                print(f"stale or missing {path}")
                errors += 1
    if errors:
        print(f"s6_cases: {errors} stale file(s)")
        return 1
    print(f"s6_cases: {len(CASES)} runtime cases up to date")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("use exactly one of --write/--check")
    if args.write:
        write_all()
        return 0
    return check_all()


if __name__ == "__main__":
    raise SystemExit(main())
