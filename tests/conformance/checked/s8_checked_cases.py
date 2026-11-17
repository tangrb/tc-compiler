#!/usr/bin/env python3
"""S8 checked-build conformance suite (standard §13.3, design §12 item 5).

A checked build adds detection for UB accesses only: out-of-object accesses,
one-past dereferences, dangling pointers and reads of uninitialized storage.
It must not add traps to defined programs and must not change the mandatory
traps (null pointer, read-only parameter object, code/device region, conversion
overflow, division by zero, depth D, I/O failure) or the defined behavior of
accepted programs.

Each case runs on both tools with `--checked`; stdout/trap/exit expectations
are derived from the standard.  The "legal set" section re-runs representative
release fixtures under a checked build and requires the same observable
behavior, so checked and mandatory-trap statistics stay separate.

Run:
    python3 tests/conformance/checked/s8_checked_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

P = engine.program
L = engine.library

CHECKED_CASES = [
    engine.case(
        "TC-CHK-S8-0001",
        "§13.3 uninitialized frame storage read through a pointer -> ADDR_UNAVAILABLE",
        root=P(
            "var flag: bool = read bool",
            "_ = br_if flag, Ldecl",
            "var y: int32 = copy int32, 5",
            "label Ldecl:",
            "var p: ptr<int32> = addr int32, y",
            "unsafe then",
            "var v: int32 = load int32, p",
            "_ = writeln int32, v",
            "end",
        ),
        stdin="true",
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0002",
        "§13.3 read of a defined frame object through a pointer is accepted",
        root=P(
            "var y: int32 = copy int32, 5",
            "var p: ptr<int32> = addr int32, y",
            "unsafe then",
            "var v: int32 = load int32, p",
            "_ = writeln int32, v",
            "end",
        ),
        out="5\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0003",
        "§3.12/§13.3 one-past dereference -> ADDR_UNAVAILABLE",
        root=P(
            "var a: int32 = copy int32, 7",
            "var p: ptr<int32> = addr int32, a",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 1",
            "var v: int32 = load int32, q",
            "_ = writeln int32, v",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0004",
        "§3.12/§13.3 pointer arithmetic beyond the object bounds -> ADDR_UNAVAILABLE",
        root=P(
            "var a: int32 = copy int32, 7",
            "var p: ptr<int32> = addr int32, a",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 2",
            "var v: int32 = load int32, q",
            "_ = writeln int32, v",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0005",
        "§13.3 dangling frame pointer dereference after return -> ADDR_UNAVAILABLE",
        files={
            "root.tc": P(
                "import lib",
                "var p: ptr<int32> = call lib.leak",
                "unsafe then",
                "var v: int32 = load int32, p",
                "_ = writeln int32, v",
                "end",
            ),
            "lib.tc": L(
                "public func leak() ptr<int32> then",
                "var x: int32 = copy int32, 5",
                "var p: ptr<int32> = addr int32, x",
                "_ = return p",
                "end",
            ),
        },
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0006",
        "§3.12/§6.7 memmove zero elements still rejects a one-past pointer in a checked build",
        root=P(
            "var a: int32 = copy int32, 7",
            "var p: ptr<int32> = addr int32, a",
            "var n: usize = copy usize, 0",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 1",
            "_ = memmove int32, q, p, n",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0007",
        "§3.12 memmove zero elements between live objects is a no-op",
        root=P(
            "var a: int32 = copy int32, 7",
            "var b: int32 = copy int32, 8",
            "var pa: ptr<int32> = addr int32, a",
            "var pb: ptr<int32> = addr int32, b",
            "var n: usize = copy usize, 0",
            "unsafe then",
            "_ = memmove int32, pa, pb, n",
            "end",
            "_ = writeln int32, a",
        ),
        out="7\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0008",
        "§6.7 null pointer trap is mandatory in a checked build",
        root=P(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var x: int32 = load int32, p",
            "end",
        ),
        trap="TC_RE_NULL_POINTER",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0009",
        "§6.7 read-only parameter write trap is mandatory in a checked build",
        files={
            "root.tc": P("import lib", "var x: int32 = call lib.f, 3"),
            "lib.tc": L(
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
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0010",
        "§6.7 code-region access trap is mandatory in a checked build",
        files={
            "root.tc": P(
                "import lib",
                "var fp: fnptr<int32, int32> = funcaddr lib.id",
                "unsafe then",
                "var u: usize = bitcast usize, fp",
                "var p: ptr<int32> = bitcast ptr<int32>, u",
                "var x: int32 = load int32, p",
                "end",
            ),
            "lib.tc": L(
                "public func id(x: int32) int32 then",
                "_ = return x",
                "end",
            ),
        },
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0011",
        "§6.3 integer overflow trap is mandatory in a checked build",
        root=P("var x: int32 = add int32, 2147483647, 1"),
        trap="TC_RE_INTEGER_OVERFLOW",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0012",
        "§6.9 depth D trap is mandatory in a checked build",
        files={
            "root.tc": P("import lib", "var r: int64 = call lib.down, 600"),
            "lib.tc": L(
                "public func down(n: int64) int64 then",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var result: int64 = copy int64, 0",
                "_ = br_if stop, done",
                "var m: int64 = sub int64, n, 1",
                "result = call Self.down, m",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        trap="TC_RE_STACK_OVERFLOW",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0013",
        "§11.3 input EOF maps to the mandatory TC_RE_IO trap in a checked build",
        root=P(
            "var x: int32 = read int32",
            "_ = writeln int32, x",
        ),
        stdin="",
        trap="TC_RE_IO",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0014",
        "§3.7 padding bytes of a made struct are defined; a byte load is accepted",
        root=P(
            "struct S then",
            "var a: int32 @padding(4)",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var pa: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, pa",
            "unsafe then",
            "var q: ptr<int8> = ptradd int8, p, 4",
            "var v: int8 = load int8, q",
            "_ = writeln int8, v",
            "end",
        ),
        out="0\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0015",
        "§3.12 a load spanning sibling fields is inside the containing struct",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var ps: ptr<S> = addr S, s",
            "unsafe then",
            "var p: ptr<int64> = bitcast ptr<int64>, ps",
            "var v: int64 = load int64, p",
            "_ = writeln int64, v",
            "end",
        ),
        out="8589934593\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0016",
        "§3.12 ptrsub from a field one-past returns inside the field even when an adjacent object starts there",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var p: ptr<int32> = addr int32, s.b",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 1",
            "var back: ptr<int32> = ptrsub int32, q, 1",
            "var v: int32 = load int32, back",
            "_ = writeln int32, v",
            "end",
        ),
        out="2\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0017",
        "§3.12 the last byte of a live object is readable",
        root=P(
            "struct S then",
            "var a: int64",
            "end",
            "var s: S = make S, 0x0102030405060708",
            "var pa: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, pa",
            "unsafe then",
            "var q: ptr<int8> = ptradd int8, p, 7",
            "var v: int8 = load int8, q",
            "_ = writeln int8, v",
            "end",
        ),
        out="1\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0018",
        "§3.12 an access crossing the object end is rejected in a checked build",
        root=P(
            "struct S then",
            "var a: int64",
            "end",
            "var s: S = make S, 7",
            "var pa: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, pa",
            "unsafe then",
            "var q: ptr<int8> = ptradd int8, p, 7",
            "var r: ptr<int64> = bitcast ptr<int64>, q",
            "var v: int64 = load int64, r",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0019",
        "§3.12 a committed memmove defines its destination bytes",
        root=P(
            "var a: int64 = copy int64, 0",
            "var b: int64 = copy int64, 0",
            "var aa: ptr<int64> = addr int64, a",
            "var bb: ptr<int64> = addr int64, b",
            "var pa: ptr<int8> = bitcast ptr<int8>, aa",
            "var pb: ptr<int8> = bitcast ptr<int8>, bb",
            "unsafe then",
            "_ = store int8, pa, 1",
            "_ = memmove int8, pb, pa, 1",
            "var v: int8 = load int8, pb",
            "_ = writeln int8, v",
            "end",
        ),
        out="1\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0020",
        "§4.7 static initializer bytes are defined in a checked build",
        files={
            "root.tc": P(
                "import lib",
                "var pa: ptr<int32> = addr int32, lib.V",
                "var p: ptr<int8> = bitcast ptr<int8>, pa",
                "unsafe then",
                "var v: int32 = load int32, pa",
                "_ = writeln int32, v",
                "var q: ptr<int8> = ptradd int8, p, 1",
                "var b: int8 = load int8, q",
                "_ = writeln int8, b",
                "end",
            ),
            "lib.tc": L("public static var V: int32 = 0x12345678"),
        },
        out="305419896\n86\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0021",
        "§5.3 parameter objects and their fields are defined at function entry",
        files={
            "root.tc": P(
                "import lib",
                "var s: lib.S = make lib.S, 1, 99",
                "var r: int64 = call lib.f, s",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct S then",
                "var a: int32",
                "var b: int64",
                "end",
                "public func f(s: S) int64 then",
                "var p: ptr<int64> = addr int64, s.b",
                "var v: int64 = copy int64, 0",
                "unsafe then",
                "v = load int64, p",
                "end",
                "_ = return v",
                "end",
            ),
        },
        out="99\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0022",
        "§6.14 a field write keeps previously defined sibling fields readable through pointers",
        root=P(
            "struct S then",
            "var a: int64",
            "var b: int64",
            "end",
            "var s: S = make S, 1, 2",
            "s.b = add int64, s.b, 5",
            "var p: ptr<int64> = addr int64, s.b",
            "unsafe then",
            "var v: int64 = load int64, p",
            "_ = writeln int64, v",
            "end",
        ),
        out="7\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0023",
        "§13.3 a store through a pointer defines a conditionally uninitialized object",
        root=P(
            "var flag: bool = read bool",
            "_ = br_if flag, Ldecl",
            "var y: int32 = copy int32, 5",
            "label Ldecl:",
            "var p: ptr<int32> = addr int32, y",
            "unsafe then",
            "_ = store int32, p, 9",
            "var v: int32 = load int32, p",
            "_ = writeln int32, v",
            "end",
        ),
        stdin="true",
        out="9\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0024",
        "§3.12 dangling pointers may be copied, compared and subtracted in a checked build",
        files={
            "root.tc": P(
                "import lib",
                "var p: ptr<int64> = call lib.leak",
                "var q: ptr<int64> = copy ptr<int64>, p",
                "var r: ptr<int64> = copy ptr<int64>, q",
                "unsafe then",
                "var d: isize = ptrdiff int64, r, p",
                "_ = writeln isize, d",
                "var e: bool = ptreq int64, r, p",
                "_ = writeln bool, e",
                "end",
            ),
            "lib.tc": L(
                "public func leak() ptr<int64> then",
                "var x: int64 = copy int64, 5",
                "var p: ptr<int64> = addr int64, x",
                "_ = return p",
                "end",
            ),
        },
        out="0\ntrue\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0025",
        "§3.12 ptrsub from the one-past of the last field returns into that field",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var p: ptr<int32> = addr int32, s.b",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 1",
            "var back: ptr<int32> = ptrsub int32, q, 1",
            "var v: int32 = load int32, back",
            "_ = writeln int32, v",
            "end",
        ),
        out="2\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0026",
        "§6.7 an address outside every region is unavailable in a checked build",
        root=P(
            "var u: usize = copy usize, 18446744073709551615u",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var v: int32 = load int32, p",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0027",
        "§3.7 a byte store into struct padding is accepted and visible",
        root=P(
            "struct S then",
            "var a: int32 @padding(4)",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var pa: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, pa",
            "unsafe then",
            "var q: ptr<int8> = ptradd int8, p, 4",
            "_ = store int8, q, 7",
            "var v: int8 = load int8, q",
            "_ = writeln int8, v",
            "end",
        ),
        out="7\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0028",
        "§3.7 a frame field beyond a 4095-byte offset is defined after make",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.f, 7",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int32 @padding(5000)",
                "var b: int64",
                "end",
                "public func f(x: int32) int64 then",
                "var s: B = make B, x, 99",
                "var p: ptr<int64> = addr int64, s.b",
                "var y: int64 = copy int64, 0",
                "unsafe then",
                "y = load int64, p",
                "end",
                "_ = return y",
                "end",
            ),
        },
        out="99\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0029",
        "§3.12 a store spanning sibling fields stays inside the containing struct",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var ps: ptr<S> = addr S, s",
            "var v: int64 = copy int64, 0",
            "unsafe then",
            "var p: ptr<int64> = bitcast ptr<int64>, ps",
            "_ = store int64, p, 0x0000000500000007",
            "v = load int64, p",
            "end",
            "_ = writeln int64, v",
        ),
        out="21474836487\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0030",
        "§3.12 memmove crossing the object end is rejected in a checked build",
        root=P(
            "var a: int64 = copy int64, 1",
            "var b: int64 = copy int64, 2",
            "var pa: ptr<int64> = addr int64, a",
            "var pb: ptr<int64> = addr int64, b",
            "unsafe then",
            "var q: ptr<int8> = bitcast ptr<int8>, pa",
            "var src: ptr<int8> = bitcast ptr<int8>, pb",
            "var r: ptr<int8> = ptradd int8, q, 4",
            "_ = memmove int8, r, src, 8",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0031",
        "§3.12 a store starting at the last byte but extending past it is rejected",
        root=P(
            "struct S then",
            "var a: int64",
            "end",
            "var s: S = make S, 7",
            "var pa: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, pa",
            "var v: int64 = copy int64, 1",
            "unsafe then",
            "var q: ptr<int8> = ptradd int8, p, 7",
            "var r: ptr<int64> = bitcast ptr<int64>, q",
            "_ = store int64, r, v",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0032",
        "§8.5 a by-reference composite parameter is defined in the callee frame",
        files={
            "root.tc": P(
                "import lib",
                "var b: lib.Big = make lib.Big, 7, 8, 9",
                "var r: int64 = call lib.sum, b",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct Big then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func sum(x: Big) int64 then",
                "var p: ptr<int64> = addr int64, x.c",
                "var v: int64 = copy int64, 0",
                "unsafe then",
                "v = load int64, p",
                "end",
                "var r: int64 = add int64, x.a, x.b",
                "var s: int64 = add int64, r, v",
                "_ = return s",
                "end",
            ),
        },
        out="24\n",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0033",
        "§3.12/§6.7 memmove of a positive count from an uninitialized source object "
        "reports ADDR_UNAVAILABLE in a checked build",
        root=P(
            "var flag: bool = read bool",
            "_ = br_if flag, Ldecl",
            "var src: int64 = copy int64, 1",
            "label Ldecl:",
            "var dst: int64 = copy int64, 2",
            "var p: ptr<int64> = addr int64, src",
            "var q: ptr<int64> = addr int64, dst",
            "unsafe then",
            "_ = memmove int64, q, p, 1",
            "end",
        ),
        stdin="true",
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0034",
        "§3.12 memmove with zero elements still rejects a dangling destination "
        "pointer in a checked build",
        files={
            "root.tc": P(
                "import lib",
                "var p: ptr<int64> = call lib.leak",
                "var b: int64 = copy int64, 4",
                "var pb: ptr<int64> = addr int64, b",
                "unsafe then",
                "_ = memmove int64, p, pb, 0",
                "end",
            ),
            "lib.tc": L(
                "public func leak() ptr<int64> then",
                "var x: int64 = copy int64, 5",
                "var q: ptr<int64> = addr int64, x",
                "_ = return q",
                "end",
            ),
        },
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0035",
        "§6.3 integer division by zero is a mandatory trap that also fires in a checked build",
        root=P(
            "var z: int32 = copy int32, 0",
            "var x: int32 = div int32, 1, z",
        ),
        trap="TC_RE_DIV_ZERO",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0036",
        "§6.6.1 cast overflow is a mandatory trap that also fires in a checked build",
        root=P(
            "var a: int64 = copy int64, 200",
            "var b: int8 = cast int8, a",
        ),
        trap="TC_RE_CAST_OVERFLOW",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0037",
        "§6.7/§8 device-rejected access is a mandatory trap that also fires in a checked build",
        root=P(
            "var a: usize = copy usize, 0x20000000",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, a",
            "var x: int32 = load int32, p",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0038",
        "§6.3.2 strict float underflow is a mandatory trap that also fires in a checked build",
        root=P(
            "var a: float64 = mul float64, 5.0e-324, 0.5",
        ),
        trap="TC_RE_FLOAT_UNDERFLOW",
        checked=True,
    ),
    engine.case(
        "TC-CHK-S8-0039",
        "§3.12/§13.3 a one-past pointer may be stored into a field, loaded "
        "back and ptrsub back into the object in a checked build",
        root=P(
            "struct S then",
            "var a: int64",
            "var b: int64",
            "end",
            "struct Holder then",
            "var p: ptr<int64>",
            "end",
            "var s: S = make S, 7, 9",
            "var pa: ptr<int64> = addr int64, s.a",
            "unsafe then",
            "var one: ptr<int64> = ptradd int64, pa, 2",
            "end",
            "var h: Holder = make Holder, one",
            "var back: ptr<int64> = copy ptr<int64>, h.p",
            "unsafe then",
            "var inb: ptr<int64> = ptrsub int64, back, 2",
            "var v: int64 = load int64, inb",
            "end",
            "_ = writeln int64, v",
        ),
        out="7\n",
        checked=True,
    ),
]

def w64_s6_ids() -> list[str]:
    run = ROOT / "tests" / "conformance" / "runtime" / "run"
    ids: list[str] = []
    for directory in sorted(run.iterdir()):
        if not directory.is_dir():
            continue
        yaml_path = directory / f"{directory.name}.yaml"
        if not yaml_path.exists():
            continue
        data = engine.yaml_subset.parse(yaml_path.read_text(encoding="utf-8"))
        if data.get("profile") == "w64":
            ids.append(str(data["id"]))
    return ids


# Every W=64 runtime conformance fixture is a legal program; a checked build
# must keep each one's observable behavior identical to the derived expectation.
LEGAL_REUSE = w64_s6_ids()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    release = 0
    checked = 0
    for case in CHECKED_CASES:
        if case.trap is None:
            release += 1
        else:
            checked += 1
        try:
            failures.extend(engine.run_case(case, keep=args.keep))
        except (engine.CaseError, OSError, Exception) as exc:  # noqa: BLE001
            failures.append(f"{case.case_id}: {exc}")
    if len(LEGAL_REUSE) < 60:
        print(f"s8.checked: only {len(LEGAL_REUSE)} W=64 legal fixtures found")
        return 1
    legal_failures: list[str] = []
    for case_id in LEGAL_REUSE:
        case = engine.load_s6_case(case_id)
        try:
            legal_failures.extend(engine.run_case(replace(case, checked=True)))
        except (engine.CaseError, OSError, Exception) as exc:  # noqa: BLE001
            legal_failures.append(f"{case_id} (checked): {exc}")
    if failures:
        print(f"s8.checked: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    if legal_failures:
        print(f"s8.checked: {len(legal_failures)} legal-set failure(s)")
        for item in legal_failures:
            print(f"  {item}")
        return 1
    print(
        f"s8.checked: {len(CHECKED_CASES)} cases "
        f"({checked} UB-trap, {release} accepted), "
        f"{len(LEGAL_REUSE)} legal fixtures unchanged"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
