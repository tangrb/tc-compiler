#!/usr/bin/env python3
"""S8 full VM/AOT differential conformance cases.

This suite closes the design §12 item 1 differential requirement for every
runtime case of the W=64 profile: all W=64 S6 fixtures are executed on the
reference VM and on the AArch64 ELF produced by `tc-aot`, and stdout bytes,
the first `TC_RE_*` code and the exit status are compared against independently
derived expectations.  Additional cases exercise the S8 delivery: structures,
HFA/large composites, the full AAPCS64 argument/return surface, indirect
calls, pointers/objects/regions, static initializer bytes, deterministic I/O,
non-aligned access and the `@fast_math` boundary.

Addresses are environment-defined (standard §10.5), so cases observe address
values only through defined relationships (`ptrdiff` inside one object,
`funceq`/`funcne`, region insertion), never as absolute output.  Expectations
follow the standard clauses named in `spec`; none is copied from tool output.

Run:
    python3 tests/conformance/diff/s8_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RUN = ROOT / "tests" / "conformance" / "runtime" / "run"
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402


def w64_s6_ids() -> list[str]:
    ids: list[str] = []
    for directory in sorted(RUN.iterdir()):
        if not directory.is_dir():
            continue
        yaml_path = directory / f"{directory.name}.yaml"
        if not yaml_path.exists():
            continue
        data = engine.yaml_subset.parse(yaml_path.read_text(encoding="utf-8"))
        if data.get("profile") == "w64":
            ids.append(str(data["id"]))
    return ids


S6_IDS = w64_s6_ids()
CASES = [engine.load_s6_case(case_id) for case_id in S6_IDS]


P = engine.program
L = engine.library

EXTRA = [
    engine.case(
        "TC-DIFF-S8-0001",
        "§3.7/§6.12/§6.14 struct value, copy, field assignment and whole-value store",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int8",
            "var c: int64",
            "end",
            "var s: S = make S, 7, 3, 100",
            "var t: S = copy S, s",
            "t.a = add int32, t.a, 1",
            "_ = writeln int32, s.a",
            "_ = writeln int8, t.b",
            "_ = writeln int64, t.c",
            "var p: ptr<S> = addr S, s",
            "unsafe then",
            "_ = store S, p, t",
            "end",
            "_ = writeln int32, s.a",
        ),
        out="7\n3\n100\n8\n",
    ),
    engine.case(
        "TC-DIFF-S8-0002",
        "§3.7 @padding bytes are zero and participate in copy/store",
        root=P(
            "struct S then",
            "var a: int32 @padding(3)",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var t: S = copy S, s",
            "var ps: ptr<S> = addr S, s",
            "var p: ptr<int8> = bitcast ptr<int8>, ps",
            "unsafe then",
            "var q4: ptr<int8> = ptradd int8, p, 4",
            "var v4: int8 = load int8, q4",
            "_ = writeln int8, v4",
            "var q7: ptr<int8> = ptradd int8, p, 7",
            "var v7: int8 = load int8, q7",
            "_ = writeln int8, v7",
            "end",
            "_ = writeln int32, t.b",
        ),
        out="0\n2\n2\n",
    ),
    engine.case(
        "TC-DIFF-S8-0003",
        "§8.2/§8.5 composite arguments and returns of 3/9/12/16 bytes",
        files={
            "root.tc": P(
                "import lib",
                "var t1: lib.T1 = make lib.T1, 1, 2",
                "var r1: lib.T1 = call lib.idT1, t1",
                "_ = writeln int8, r1.a",
                "_ = writeln int16, r1.b",
                "var t2: lib.T2 = make lib.T2, 3, 4",
                "var r2: lib.T2 = call lib.idT2, t2",
                "_ = writeln int8, r2.a",
                "_ = writeln int32, r2.b",
                "var t3: lib.T3 = make lib.T3, 5, 6",
                "var r3: lib.T3 = call lib.idT3, t3",
                "_ = writeln int32, r3.a",
                "_ = writeln int64, r3.b",
                "var t4: lib.T4 = make lib.T4, 7, 8",
                "var r4: lib.T4 = call lib.idT4, t4",
                "_ = writeln int64, r4.a",
                "_ = writeln int64, r4.b",
            ),
            "lib.tc": L(
                "public struct T1 then",
                "var a: int8",
                "var b: int16",
                "end",
                "public struct T2 then",
                "var a: int8",
                "var b: int32",
                "end",
                "public struct T3 then",
                "var a: int32",
                "var b: int64",
                "end",
                "public struct T4 then",
                "var a: int64",
                "var b: int64",
                "end",
                "public func idT1(x: T1) T1 then",
                "_ = return x",
                "end",
                "public func idT2(x: T2) T2 then",
                "_ = return x",
                "end",
                "public func idT3(x: T3) T3 then",
                "_ = return x",
                "end",
                "public func idT4(x: T4) T4 then",
                "_ = return x",
                "end",
            ),
        },
        out="1\n2\n3\n4\n5\n6\n7\n8\n",
    ),
    engine.case(
        "TC-DIFF-S8-0004",
        "§8.5 HFA float32 aggregates use v registers for parameters and returns",
        files={
            "root.tc": P(
                "import lib",
                "var q: lib.Q = call lib.mk, 1.5f, 2.5f, 3.5f",
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
        out="7.5\n",
    ),
    engine.case(
        "TC-DIFF-S8-0005",
        "§8.5 four-member float64 HFA across v0-v3",
        files={
            "root.tc": P(
                "import lib",
                "var h: lib.H4 = call lib.mk, 1.0, 2.0, 3.0, 4.0",
                "var r: float64 = call lib.sum, h",
                "_ = writeln float64, r",
            ),
            "lib.tc": L(
                "public struct H4 then",
                "var a: float64",
                "var b: float64",
                "var c: float64",
                "var d: float64",
                "end",
                "public func mk(a: float64, b: float64, c: float64, d: float64) H4 then",
                "var h: H4 = make H4, a, b, c, d",
                "_ = return h",
                "end",
                "public func sum(h: H4) float64 then",
                "var t1: float64 = add float64, h.a, h.b",
                "var t2: float64 = add float64, h.c, h.d",
                "var r: float64 = add float64, t1, t2",
                "_ = return r",
                "end",
            ),
        },
        out="10\n",
    ),
    engine.case(
        "TC-DIFF-S8-0006",
        "§8.5 nested homogeneous float aggregates flatten to one HFA",
        files={
            "root.tc": P(
                "import lib",
                "var n: lib.N = call lib.mk, 1.0f, 2.0f, 3.0f, 4.0f",
                "var r: float32 = call lib.sum, n",
                "_ = writeln float32, r",
            ),
            "lib.tc": L(
                "public struct P2 then",
                "var a: float32",
                "var b: float32",
                "end",
                "public struct N then",
                "var x: P2",
                "var y: P2",
                "end",
                "public func mk(a: float32, b: float32, c: float32, d: float32) N then",
                "var p: P2 = make P2, a, b",
                "var q: P2 = make P2, c, d",
                "var n: N = make N, p, q",
                "_ = return n",
                "end",
                "public func sum(n: N) float32 then",
                "var t1: float32 = add float32, n.x.a, n.x.b",
                "var t2: float32 = add float32, n.y.a, n.y.b",
                "var r: float32 = add float32, t1, t2",
                "_ = return r",
                "end",
            ),
        },
        out="10\n",
    ),
    engine.case(
        "TC-DIFF-S8-0007",
        "§8.2/§8.5 aggregates larger than 16 bytes pass on the stack and return indirectly",
        files={
            "root.tc": P(
                "import lib",
                "var b: lib.B = call lib.mk, 7",
                "var c: lib.B = call lib.idB, b",
                "var r: int64 = call lib.sum, c",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func mk(x: int64) B then",
                "var b: B = make B, x, 2, 3",
                "_ = return b",
                "end",
                "public func idB(x: B) B then",
                "_ = return x",
                "end",
                "public func sum(b: B) int64 then",
                "var t: int64 = add int64, b.a, b.b",
                "var r: int64 = add int64, t, b.c",
                "_ = return r",
                "end",
            ),
        },
        out="12\n",
    ),
    engine.case(
        "TC-DIFF-S8-0008",
        "§8.2/§8.5 17-byte composite (stack argument, indirect return)",
        files={
            "root.tc": P(
                "import lib",
                "var b: lib.B = call lib.mk, 10",
                "var c: lib.B = call lib.idB, b",
                "_ = writeln int64, c.a",
                "_ = writeln int64, c.b",
                "_ = writeln int8, c.c",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int64",
                "var b: int64",
                "var c: int8",
                "end",
                "public func mk(x: int64) B then",
                "var b: B = make B, x, 11, 12",
                "_ = return b",
                "end",
                "public func idB(x: B) B then",
                "_ = return x",
                "end",
            ),
        },
        out="10\n11\n12\n",
    ),
    engine.case(
        "TC-DIFF-S8-0009",
        "§8.5 nine- and ten-argument integer calls spill to the stack",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.sum, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public func sum(a: int64, b: int64, c: int64, d: int64,"
                " e: int64, f: int64, g: int64, h: int64, i: int64,"
                " j: int64) int64 then",
                "var s1: int64 = add int64, a, b",
                "var s2: int64 = add int64, s1, c",
                "var s3: int64 = add int64, s2, d",
                "var s4: int64 = add int64, s3, e",
                "var s5: int64 = add int64, s4, f",
                "var s6: int64 = add int64, s5, g",
                "var s7: int64 = add int64, s6, h",
                "var s8: int64 = add int64, s7, i",
                "var s9: int64 = add int64, s8, j",
                "_ = return s9",
                "end",
            ),
        },
        out="55\n",
    ),
    engine.case(
        "TC-DIFF-S8-0010",
        "§8.5 ten float64 arguments use v0-v7 and the stack",
        files={
            "root.tc": P(
                "import lib",
                "var r: float64 = call lib.sum, 1.0, 2.0, 3.0, 4.0, 5.0,"
                " 6.0, 7.0, 8.0, 9.0, 10.0",
                "_ = writeln float64, r",
            ),
            "lib.tc": L(
                "public func sum(a: float64, b: float64, c: float64,"
                " d: float64, e: float64, f: float64, g: float64,"
                " h: float64, i: float64, j: float64) float64 then",
                "var s1: float64 = add float64, a, b",
                "var s2: float64 = add float64, s1, c",
                "var s3: float64 = add float64, s2, d",
                "var s4: float64 = add float64, s3, e",
                "var s5: float64 = add float64, s4, f",
                "var s6: float64 = add float64, s5, g",
                "var s7: float64 = add float64, s6, h",
                "var s8: float64 = add float64, s7, i",
                "var s9: float64 = add float64, s8, j",
                "_ = return s9",
                "end",
            ),
        },
        out="55\n",
    ),
    engine.case(
        "TC-DIFF-S8-0011",
        "§8.5 a composite needing two x registers after seven integer arguments goes to the stack",
        files={
            "root.tc": P(
                "import lib",
                "var s: lib.T = make lib.T, 10, 20",
                "var r: int32 = call lib.f, 1, 2, 3, 4, 5, 6, 7, s, 8",
                "_ = writeln int32, r",
            ),
            "lib.tc": L(
                "public struct T then",
                "var a: int32",
                "var b: int64",
                "end",
                "public func f(a: int32, b: int32, c: int32, d: int32,"
                " e: int32, g: int32, h: int32, s: T, i: int32) int32 then",
                "var t1: int32 = add int32, a, b",
                "var t2: int32 = add int32, t1, c",
                "var t3: int32 = add int32, t2, d",
                "var t4: int32 = add int32, t3, e",
                "var t5: int32 = add int32, t4, g",
                "var t6: int32 = add int32, t5, h",
                "var t7: int32 = add int32, t6, i",
                "var t8: int32 = add int32, t7, s.a",
                "_ = return t8",
                "end",
            ),
        },
        out="46\n",
    ),
    engine.case(
        "TC-DIFF-S8-0012",
        "§8.5 an HFA after seven float arguments goes to the stack",
        files={
            "root.tc": P(
                "import lib",
                "var h: lib.H = make lib.H, 10.0, 20.0",
                "var r: float64 = call lib.f, 1.0, 2.0, 3.0, 4.0, 5.0,"
                " 6.0, 7.0, h, 8.0",
                "_ = writeln float64, r",
            ),
            "lib.tc": L(
                "public struct H then",
                "var a: float64",
                "var b: float64",
                "end",
                "public func f(a: float64, b: float64, c: float64,"
                " d: float64, e: float64, g: float64, h: float64, s: H,"
                " i: float64) float64 then",
                "var t1: float64 = add float64, a, b",
                "var t2: float64 = add float64, t1, c",
                "var t3: float64 = add float64, t2, d",
                "var t4: float64 = add float64, t3, e",
                "var t5: float64 = add float64, t4, g",
                "var t6: float64 = add float64, t5, h",
                "var t7: float64 = add float64, t6, s.a",
                "var t8: float64 = add float64, t7, s.b",
                "var t9: float64 = add float64, t8, i",
                "_ = return t9",
                "end",
            ),
        },
        out="66\n",
    ),
    engine.case(
        "TC-DIFF-S8-0013",
        "§6.9/§8.2 call_indirect across integer, float, struct and HFA signatures",
        files={
            "root.tc": P(
                "import lib",
                "var fi: fnptr<int64, int32, int64> = funcaddr lib.fint",
                "var ff: fnptr<float64, float64> = funcaddr lib.ffloat",
                "var ft: fnptr<lib.T, lib.T> = funcaddr lib.fstruct",
                "var fh: fnptr<lib.H, lib.H> = funcaddr lib.fhfa",
                "unsafe then",
                "var a: int64 = call_indirect fi, 3, 9",
                "_ = writeln int64, a",
                "var b: float64 = call_indirect ff, 2.5",
                "_ = writeln float64, b",
                "var t: lib.T = make lib.T, 4, 5",
                "var u: lib.T = call_indirect ft, t",
                "_ = writeln int32, u.a",
                "_ = writeln int64, u.b",
                "var h: lib.H = make lib.H, 1.5, 2.5",
                "var g: lib.H = call_indirect fh, h",
                "var c: float64 = add float64, g.a, g.b",
                "_ = writeln float64, c",
                "end",
            ),
            "lib.tc": L(
                "public struct T then",
                "var a: int32",
                "var b: int64",
                "end",
                "public struct H then",
                "var a: float64",
                "var b: float64",
                "end",
                "public func fint(a: int32, b: int64) int64 then",
                "var x: int64 = cast int64, a",
                "var r: int64 = add int64, x, b",
                "_ = return r",
                "end",
                "public func ffloat(x: float64) float64 then",
                "var r: float64 = mul float64, x, 2.0",
                "_ = return r",
                "end",
                "public func fstruct(x: T) T then",
                "_ = return x",
                "end",
                "public func fhfa(x: H) H then",
                "_ = return x",
                "end",
            ),
        },
        out="12\n5\n4\n5\n4\n",
    ),
    engine.case(
        "TC-DIFF-S8-0014",
        "§3.13.3 distinct function entry addresses (ICF disabled); funcaddr/funceq/funcne",
        files={
            "root.tc": P(
                "import lib",
                "var fa: fnptr<int32, int32> = funcaddr lib.f1",
                "var fb: fnptr<int32, int32> = funcaddr lib.f2",
                "var z: fnptr<int32, int32> = copy fnptr<int32, int32>, nullptr",
                "var s1: bool = funceq fnptr<int32, int32>, fa, fb",
                "_ = writeln bool, s1",
                "var s2: bool = funcne fnptr<int32, int32>, fa, fb",
                "_ = writeln bool, s2",
                "var s3: bool = funceq fnptr<int32, int32>, fa, fa",
                "_ = writeln bool, s3",
                "var s4: bool = funceq fnptr<int32, int32>, fa, z",
                "_ = writeln bool, s4",
                "var s5: bool = funceq fnptr<int32, int32>, z, z",
                "_ = writeln bool, s5",
            ),
            "lib.tc": L(
                "public func f1(x: int32) int32 then",
                "var r: int32 = copy int32, x",
                "_ = return r",
                "end",
                "public func f2(x: int32) int32 then",
                "var r: int32 = copy int32, x",
                "_ = return r",
                "end",
            ),
        },
        out="false\ntrue\ntrue\nfalse\ntrue\n",
    ),
    engine.case(
        "TC-DIFF-S8-0015",
        "§6.8/§13.2 in-region object offsets: static objects and field sub-objects",
        root=P(
            "struct S then",
            "var x: int32",
            "var y: int32",
            "var z: int64",
            "end",
            "var a: int64 = copy int64, 1",
            "var b: int64 = copy int64, 2",
            "var pa: ptr<int64> = addr int64, a",
            "var pb: ptr<int64> = addr int64, b",
            "unsafe then",
            "var d: isize = ptrdiff int64, pb, pa",
            "_ = writeln isize, d",
            "end",
            "var s: S = make S, 3, 4, 5",
            "var px: ptr<int32> = addr int32, s.x",
            "var py: ptr<int32> = addr int32, s.y",
            "unsafe then",
            "var d2: isize = ptrdiff int32, py, px",
            "_ = writeln isize, d2",
            "end",
            "var pz: ptr<int64> = addr int64, s.z",
            "var pz32: ptr<int32> = bitcast ptr<int32>, pz",
            "unsafe then",
            "var d3: isize = ptrdiff int32, pz32, px",
            "_ = writeln isize, d3",
            "end",
        ),
        out="8\n4\n8\n",
    ),
    engine.case(
        "TC-DIFF-S8-0016",
        "§3.12/§6.8 one-past ptradd offset 0, ptrsub back into the object and ptrdiff",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 1, 2",
            "var ps: ptr<S> = addr S, s",
            "unsafe then",
            "var q: ptr<S> = ptradd S, ps, 1",
            "var d: isize = ptrdiff S, q, ps",
            "_ = writeln isize, d",
            "var z: ptr<S> = ptradd S, q, 0",
            "var same: bool = ptreq S, z, q",
            "_ = writeln bool, same",
            "var back: ptr<S> = ptrsub S, q, 1",
            "var v: S = load S, back",
            "_ = writeln int32, v.b",
            "end",
        ),
        out="8\ntrue\n2\n",
    ),
    engine.case(
        "TC-DIFF-S8-0017",
        "§6.7/§3.12 memmove overlapping byte ranges uses a temporary copy",
        root=P(
            "var a: int32 = copy int32, 0",
            "var pa: ptr<int32> = addr int32, a",
            "var p: ptr<uint8> = bitcast ptr<uint8>, pa",
            "unsafe then",
            "_ = store uint8, p, 1",
            "var p1: ptr<uint8> = ptradd uint8, p, 1",
            "_ = store uint8, p1, 2",
            "var p2: ptr<uint8> = ptradd uint8, p1, 1",
            "_ = store uint8, p2, 3",
            "var p3: ptr<uint8> = ptradd uint8, p2, 1",
            "_ = store uint8, p3, 4",
            "_ = memmove uint8, p1, p, 3",
            "var v0: uint8 = load uint8, p",
            "_ = writeln uint8, v0",
            "var v1: uint8 = load uint8, p1",
            "_ = writeln uint8, v1",
            "var v2: uint8 = load uint8, p2",
            "_ = writeln uint8, v2",
            "var v3: uint8 = load uint8, p3",
            "_ = writeln uint8, v3",
            "end",
        ),
        out="1\n1\n2\n3\n",
    ),
    engine.case(
        "TC-DIFF-S8-0018",
        "§6.7 non-aligned 64-bit field load/store through an object pointer",
        root=P(
            "struct U then",
            "var x: int8",
            "var y: int64",
            "end",
            "var u: U = make U, 1, 123456789",
            "var p: ptr<int64> = addr int64, u.y",
            "unsafe then",
            "var v: int64 = load int64, p",
            "_ = writeln int64, v",
            "_ = store int64, p, 42",
            "var w: int64 = load int64, p",
            "_ = writeln int64, w",
            "end",
            "_ = writeln int64, u.y",
        ),
        out="123456789\n42\n42\n",
    ),
    engine.case(
        "TC-DIFF-S8-0028",
        "§13.2/§6.7 host (heap) region has no core objects: access -> ADDR_UNAVAILABLE",
        root=P(
            "var u: usize = copy usize, 0x80000000",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var x: int32 = load int32, p",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
    engine.case(
        "TC-DIFF-S8-0029",
        "§10.3 @fast_math covers float memory access and control flow in its lexical body",
        files={
            "root.tc": P(
                "import lib",
                "unsafe then",
                "var a: float64 = call lib.calc, 5.0",
                "_ = writeln float64, a",
                "var b: float64 = call lib.calc, -5.0",
                "_ = writeln float64, b",
                "end",
            ),
            "lib.tc": L(
                "public struct F then",
                "var x: float64",
                "end",
                "public @fast @fast_math func calc(a: float64) float64 then",
                "var f: F = make F, a",
                "var p: ptr<F> = addr F, f",
                "var g: F = make F, 0.0",
                "unsafe then",
                "g = load F, p",
                "end",
                "var zero: float64 = copy float64, 0.0",
                "var negative: bool = lt float64, g.x, zero",
                "var out: float64 = copy float64, 0.0",
                "_ = br_if negative, done",
                "out = div float64, g.x, 2.0",
                "label done:",
                "_ = return out",
                "end",
            ),
        },
        out="2.5\n0\n",
    ),
    engine.case(
        "TC-DIFF-S8-0019",
        "§3.4/§6.7 bool read/byte-store normalization through a uint8 alias",
        root=P(
            "var b: bool = copy bool, false",
            "var pb: ptr<bool> = addr bool, b",
            "var pu: ptr<uint8> = bitcast ptr<uint8>, pb",
            "var v: uint8 = copy uint8, 2",
            "unsafe then",
            "_ = store uint8, pu, v",
            "end",
            "var x: bool = copy bool, b",
            "_ = writeln bool, x",
            "var pb2: ptr<bool> = bitcast ptr<bool>, pu",
            "var y: uint8 = copy uint8, 0",
            "unsafe then",
            "_ = store bool, pb2, true",
            "y = load uint8, pu",
            "end",
            "_ = writeln uint8, y",
        ),
        out="true\n1\n",
    ),
    engine.case(
        "TC-DIFF-S8-0020",
        "§4.7/§6.7 static var initializer image bytes are identical in VM preload and AOT .data",
        files={
            "root.tc": P(
                "import lib",
                "var pv: ptr<int32> = addr int32, lib.V",
                "var p: ptr<int8> = bitcast ptr<int8>, pv",
                "unsafe then",
                "var v: int32 = load int32, pv",
                "_ = writeln int32, v",
                "var p0: ptr<int8> = copy ptr<int8>, p",
                "var b0: int8 = load int8, p0",
                "_ = writeln int8, b0",
                "var p1: ptr<int8> = ptradd int8, p, 1",
                "var b1: int8 = load int8, p1",
                "_ = writeln int8, b1",
                "end",
            ),
            "lib.tc": L("public static var V: int32 = 0x12345678"),
        },
        out="305419896\n120\n86\n",
    ),
    engine.case(
        "TC-DIFF-S8-0021",
        "§3.10/§4.10 scalar and aggregate const values are inlined at the use site",
        files={
            "root.tc": P(
                "import lib",
                "var x: int32 = add int32, lib.K, 1",
                "_ = writeln int32, x",
                "var s: lib.S = copy lib.S, lib.C",
                "_ = writeln int32, s.a",
                "_ = writeln int64, s.b",
            ),
            "lib.tc": L(
                "public struct S then",
                "var a: int32",
                "var b: int64",
                "end",
                "public const K: int32 = 42",
                "public const C: S = make S, 7, 8",
            ),
        },
        out="43\n7\n8\n",
    ),
    engine.case(
        "TC-DIFF-S8-0022",
        "§10.6 @fast_math relaxes only the lexical body; direct and indirect calls agree",
        files={
            "root.tc": P(
                "import lib",
                "var fp: fnptr<float64, float64, float64> = funcaddr lib.fm",
                "unsafe then",
                "var a: float64 = call lib.fm, 7.0, 2.0",
                "_ = writeln float64, a",
                "var b: float64 = call_indirect fp, 7.0, 2.0",
                "_ = writeln float64, b",
                "end",
            ),
            "lib.tc": L(
                "public @fast @fast_math func fm(x: float64, y: float64) float64 then",
                "var a: float64 = add float64, x, y",
                "var b: float64 = div float64, a, y",
                "_ = return b",
                "end",
            ),
        },
        out="4.5\n4.5\n",
    ),
    engine.case(
        "TC-DIFF-S8-0023",
        "§7.5 switch on a negative int64 selector takes the default target",
        root=P(
            "var x: int64 = copy int64, -3",
            "_ = switch x, L0, L1, L2",
            "label L0:",
            "_ = writeln int32, 0",
            "_ = br Lend",
            "label L1:",
            "_ = writeln int32, 1",
            "_ = br Lend",
            "label L2:",
            "_ = writeln int32, 2",
            "label Lend:",
            "",
        ),
        out="2\n",
    ),
    engine.case(
        "TC-DIFF-S8-0024",
        "§6.2.9/§6.14 read into a struct field and load into a struct field",
        root=P(
            "struct S then",
            "var a: int32",
            "var b: int32",
            "end",
            "var s: S = make S, 0, 0",
            "s.a = read int32",
            "var t: int32 = copy int32, 9",
            "var p: ptr<int32> = addr int32, t",
            "unsafe then",
            "s.b = load int32, p",
            "end",
            "_ = writeln int32, s.a",
            "_ = writeln int32, s.b",
        ),
        stdin="42\n",
        out="42\n9\n",
    ),
    engine.case(
        "TC-DIFF-S8-0025",
        "§6.9 struct return stored into a field and passed as a field operand",
        files={
            "root.tc": P(
                "import lib",
                "var z: lib.T = make lib.T, 0, 0",
                "var o: lib.O = make lib.O, z",
                "var v: lib.T = make lib.T, 7, 8",
                "o.t = call lib.idT, v",
                "var r: int32 = call lib.use, o.t",
                "_ = writeln int32, r",
                "_ = writeln int64, o.t.b",
            ),
            "lib.tc": L(
                "public struct T then",
                "var a: int32",
                "var b: int64",
                "end",
                "public struct O then",
                "var t: T",
                "end",
                "public func idT(x: T) T then",
                "_ = return x",
                "end",
                "public func use(t: T) int32 then",
                "var r: int32 = copy int32, t.a",
                "_ = return r",
                "end",
            ),
        },
        out="7\n8\n",
    ),
    engine.case(
        "TC-DIFF-S8-0026",
        "§6.2.7 named make fields and named call arguments in declaration order",
        files={
            "root.tc": P(
                "import lib",
                "var s: lib.S = make lib.S, a: 7, b: 9",
                "var r: int64 = call lib.f, x: 3, y: s",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct S then",
                "var a: int32 @padding(2)",
                "var b: int64",
                "end",
                "public func f(x: int32, y: S) int64 then",
                "var a64: int64 = cast int64, y.a",
                "var t: int64 = add int64, y.b, a64",
                "var c: int64 = cast int64, x",
                "var r: int64 = add int64, t, c",
                "_ = return r",
                "end",
            ),
        },
        out="19\n",
    ),
    engine.case(
        "TC-DIFF-S8-0027",
        "§6.8/§6.9 a struct pointer field and fnptr field survive by-value copies",
        files={
            "root.tc": P(
                "import lib",
                "var a: int32 = copy int32, 5",
                "var p: ptr<int32> = addr int32, a",
                "var fp: fnptr<int32, int32> = funcaddr lib.id",
                "var s: lib.S = make lib.S, p, fp",
                "var t: lib.S = copy lib.S, s",
                "var r: int32 = call lib.use, t",
                "_ = writeln int32, r",
                "var same: bool = funceq fnptr<int32, int32>, t.f, fp",
                "_ = writeln bool, same",
                "var samep: bool = ptreq int32, t.p, p",
                "_ = writeln bool, samep",
            ),
            "lib.tc": L(
                "public struct S then",
                "var p: ptr<int32>",
                "var f: fnptr<int32, int32>",
                "end",
                "public func id(x: int32) int32 then",
                "_ = return x",
                "end",
                "public func use(s: S) int32 then",
                "var result: int32 = copy int32, 0",
                "unsafe then",
                "var v: int32 = load int32, s.p",
                "var r: int32 = call_indirect s.f, v",
                "result = copy int32, r",
                "end",
                "_ = return result",
                "end",
            ),
        },
        out="5\ntrue\ntrue\n",
    ),
]


_EDGE_CASES = [
    engine.case(
        "TC-DIFF-S8-0030",
        "§6.6.1 a same-type cast preserves the full NaN payload",
        root=P(
            "var a: float64 = bitcast float64, 0x7ff8000000000123u",
            "var b: float64 = cast float64, a",
            "var u: uint64 = bitcast uint64, b",
            "_ = writeln uint64, u",
            "var c: float32 = bitcast float32, 0x7fc00123u",
            "var d: float32 = cast float32, c",
            "var v: uint32 = bitcast uint32, d",
            "_ = writeln uint32, v",
        ),
        out="9221120237041090851\n2143289635\n",
    ),
    engine.case(
        "TC-DIFF-S8-0031",
        "§6.6.1 a cross-type float cast produces a canonical quiet NaN",
        root=P(
            "var a: float64 = bitcast float64, 0xfff8000000000123u",
            "var b: float32 = cast float32, a",
            "var u: uint32 = bitcast uint32, b",
            "_ = writeln uint32, u",
            "var c: float32 = bitcast float32, 0xffc00123u",
            "var d: float64 = cast float64, c",
            "var v: uint64 = bitcast uint64, d",
            "_ = writeln uint64, v",
        ),
        out="2143289344\n9221120237041090560\n",
    ),
    engine.case(
        "TC-DIFF-S8-0032",
        "§6.8 ptrdiff whose mathematical difference exceeds the W-bit isize traps",
        root=P(
            "var p: ptr<int8> = copy ptr<int8>, nullptr",
            "var b: usize = copy usize, 18446744073709551615u",
            "unsafe then",
            "var q: ptr<int8> = bitcast ptr<int8>, b",
            "var d: isize = ptrdiff int8, p, q",
            "end",
        ),
        trap="TC_RE_PTRDIFF_OVERFLOW",
    ),
    engine.case(
        "TC-DIFF-S8-0033",
        "§3.12 pointer arithmetic whose byte delta exceeds INT64_MAX traps",
        root=P(
            "var a: int64 = copy int64, 1",
            "var p: ptr<int64> = addr int64, a",
            "var n: usize = copy usize, 18446744073709551615u",
            "unsafe then",
            "var q: ptr<int64> = ptradd int64, p, n",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
    engine.case(
        "TC-DIFF-S8-0034",
        "§6.7 an address outside every region reports ADDR_UNAVAILABLE",
        root=P(
            "var u: usize = copy usize, 18446744073709551615u",
            "unsafe then",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var v: int32 = load int32, p",
            "end",
        ),
        trap="TC_RE_ADDR_UNAVAILABLE",
    ),
]


def _scale_cases():
    cases = []
    names = [f"p{i}" for i in range(100)]
    signature = ", ".join(f"{name}: int64" for name in names)
    body = []
    for index in range(100):
        if index == 0:
            body.append(f"var s0: int64 = copy int64, {names[0]}")
        else:
            body.append(
                f"var s{index}: int64 = add int64, s{index - 1}, {names[index]}")
    body.append("_ = return s99")
    cases.append(engine.case(
        "TC-DIFF-S8-0035",
        "§8.5 one hundred integer arguments cover the stack argument area",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.many, "
                + ", ".join(str(i + 1) for i in range(100)),
                "_ = writeln int64, r",
            ),
            "lib.tc": L(f"public func many({signature}) int64 then", *body,
                        "end"),
        },
        out="5050\n",
    ))
    functions = []
    for index in range(60):
        functions += [
            f"public func f{index}(x: int64) int64 then",
            f"var r: int64 = add int64, x, {index}",
            "_ = return r",
            "end",
        ]
    lines = ["import lib"]
    for index in range(60):
        lines.append(
            f"var a{index}: fnptr<int64, int64> = funcaddr lib.f{index}")
    for index in range(59):
        lines.append(
            f"var e{index}: bool = funceq fnptr<int64, int64>, a{index}, "
            f"a{index + 1}")
        lines.append(f"_ = writeln bool, e{index}")
    cases.append(engine.case(
        "TC-DIFF-S8-0036",
        "§3.13.3 identical function bodies keep distinct entry addresses",
        files={"root.tc": P(*lines), "lib.tc": L(*functions)},
        out="false\n" * 59,
    ))
    lines = []
    for index in range(300):
        lines.append(f"var v{index}: int64 = copy int64, {index}")
    lines.append("_ = writeln int64, v299")
    cases.append(engine.case(
        "TC-DIFF-S8-0037",
        "§4.7 a large static object table keeps program-var layout deterministic",
        root=P(*lines),
        out="299\n",
    ))
    labels = [f"l{index}" for index in range(200)]
    lines = ["var x: int64 = copy int64, 199",
             "_ = switch x, " + ", ".join(labels)]
    for index, label in enumerate(labels):
        lines += [f"label {label}:", f"_ = writeln int32, {index}",
                  "_ = br fin"]
    lines.append("label fin:")
    cases.append(engine.case(
        "TC-DIFF-S8-0038",
        "§7.5 a 200-target switch selects the last case",
        root=P(*lines),
        out="199\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0039",
        "§3.7/§10.3 frame fields beyond a 4095-byte displacement",
        files={
            "root.tc": P(
                "import lib",
                "var x: int32 = copy int32, 7",
                "var r: int64 = call lib.f, x",
                "_ = writeln int64, r",
                "var z: usize = sizeof lib.B",
                "_ = writeln usize, z",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int32 @padding(20000)",
                "var b: int64",
                "end",
                "public func f(x: int32) int64 then",
                "var s: B = make B, x, 99",
                "var p: ptr<int64> = addr int64, s.b",
                "var y: int64 = copy int64, 0",
                "unsafe then",
                "y = load int64, p",
                "end",
                "var r: int64 = add int64, y, 1",
                "_ = return r",
                "end",
            ),
        },
        out="100\n20012\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0040",
        "§4.7 a 900000-byte static object is emitted at its in-region offsets",
        files={
            "root.tc": P(
                "import lib",
                "var s: lib.Big = make lib.Big, 3, 123456",
                "_ = writeln int32, s.b",
                "var z: usize = sizeof lib.Big",
                "_ = writeln usize, z",
            ),
            "lib.tc": L(
                "public struct Big then",
                "var a: int8 @padding(900000)",
                "var b: int32",
                "end",
            ),
        },
        out="123456\n900005\n",
    ))
    types = ["public struct N0 then", "var v: int64", "end"]
    lines = ["import lib", "var x0: lib.N0 = make lib.N0, 5"]
    for level in range(1, 12):
        types += [f"public struct N{level} then", f"var inner: N{level - 1}",
                  "var extra: int64", "end"]
        lines.append(
            f"var x{level}: lib.N{level} = make lib.N{level}, "
            f"x{level - 1}, {level}")
    field = "x11" + ".inner" * 11 + ".v"
    lines.append(f"_ = writeln int64, {field}")
    cases.append(engine.case(
        "TC-DIFF-S8-0041",
        "§3.7 twelve nested struct levels keep compact field offsets",
        files={"root.tc": P(*lines), "lib.tc": L(*types)},
        out="5\n",
    ))
    return cases


EXTRA = EXTRA + _EDGE_CASES + _scale_cases()


def _advanced_cases():
    cases = []
    cases.append(engine.case(
        "TC-DIFF-S8-0042",
        "§8.5 recursive calls returning a 24-byte composite use the x8 indirect buffer",
        files={
            "root.tc": P(
                "import lib",
                "var r: lib.B = call lib.rec, 10",
                "_ = writeln int64, r.a",
                "_ = writeln int64, r.b",
                "_ = writeln int64, r.c",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func rec(n: int64) B then",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var result: B = make B, 0, 0, 0",
                "_ = br_if stop, done",
                "var m: int64 = sub int64, n, 1",
                "var d: B = call Self.rec, m",
                "var a1: int64 = add int64, d.a, 1",
                "var b1: int64 = add int64, d.b, 1",
                "var c1: int64 = add int64, d.c, 1",
                "result = make B, a1, b1, c1",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        out="10\n10\n10\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0043",
        "§8.5 recursive HFA returns keep the v-register return class",
        files={
            "root.tc": P(
                "import lib",
                "var r: lib.H = call lib.hrec, 4",
                "_ = writeln float64, r.x",
                "_ = writeln float64, r.y",
            ),
            "lib.tc": L(
                "public struct H then",
                "var x: float64",
                "var y: float64",
                "end",
                "public func hrec(n: int64) H then",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var result: H = make H, 10.5, 20.5",
                "_ = br_if stop, done",
                "var m: int64 = sub int64, n, 1",
                "var d: H = call Self.hrec, m",
                "var x1: float64 = add float64, d.x, 0.5",
                "var y1: float64 = add float64, d.y, 0.5",
                "result = make H, x1, y1",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        out="12.5\n22.5\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0044",
        "§6.9 depth D still traps for recursive indirect-return calls",
        files={
            "root.tc": P(
                "import lib",
                "var r: lib.B = call lib.rec, 600",
                "_ = writeln int64, r.a",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func rec(n: int64) B then",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var result: B = make B, 0, 0, 0",
                "_ = br_if stop, done",
                "var m: int64 = sub int64, n, 1",
                "var d: B = call Self.rec, m",
                "var a1: int64 = add int64, d.a, 1",
                "result = make B, a1, d.b, d.c",
                "label done:",
                "_ = return result",
                "end",
            ),
        },
        trap="TC_RE_STACK_OVERFLOW",
    ))
    # 100-field struct: make, aggregate sum, identity and sizeof.
    fields = [f"var f{i}: int32" for i in range(100)]
    body = []
    previous = None
    for index in range(100):
        if previous is None:
            body.append(f"var s0: int64 = cast int64, m.f0")
        else:
            body.append(f"var c{index}: int64 = cast int64, m.f{index}")
            body.append(
                f"var s{index}: int64 = add int64, s{previous}, c{index}")
        previous = index
    body.append("_ = return s99")
    lib_text = "\n".join(
        ["#lib", "public struct M then"] + fields + ["end",
         "public func sum(m: M) int64 then"] + body + ["end",
         "public func id(m: M) M then", "_ = return m", "end"]) + "\n"
    make = ", ".join(str(i + 1) for i in range(100))
    cases.append(engine.case(
        "TC-DIFF-S8-0045",
        "§3.7/§8.5 a 100-field struct passes by value and returns indirectly",
        files={
            "root.tc": P(
                "import lib",
                f"var m: lib.M = make lib.M, {make}",
                "var r: int64 = call lib.sum, m",
                "_ = writeln int64, r",
                "var n: lib.M = call lib.id, m",
                "_ = writeln int32, n.f99",
                "var z: usize = sizeof lib.M",
                "_ = writeln usize, z",
            ),
            "lib.tc": lib_text,
        },
        out="5050\n100\n400\n",
    ))
    # 150 locals in one frame.
    body = ["public func f(x: int64) int64 then"]
    for index in range(150):
        body.append(f"var v{index}: int64 = add int64, x, {index}")
    body.append("var sum: int64 = copy int64, 0")
    for index in range(150):
        body.append(f"sum = add int64, sum, v{index}")
    body += ["_ = return sum", "end"]
    cases.append(engine.case(
        "TC-DIFF-S8-0046",
        "§10.3 150 frame locals keep distinct slots and offsets",
        files={
            "root.tc": P(
                "import lib",
                "var r: int64 = call lib.f, 2",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(*body),
        },
        out=f"{2 * 150 + sum(range(150))}\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0047",
        "§7.6 multiple returns deliver scalar and composite values on every path",
        files={
            "root.tc": P(
                "import lib",
                "var a: lib.T = call lib.choose, true",
                "var b: lib.T = call lib.choose, false",
                "_ = writeln int64, a.a",
                "_ = writeln int64, b.a",
                "var c: int64 = call lib.pick, true",
                "_ = writeln int64, c",
            ),
            "lib.tc": L(
                "public struct T then",
                "var a: int64",
                "var b: int64",
                "end",
                "public func choose(x: bool) T then",
                "var t1: T = make T, 1, 2",
                "var t2: T = make T, 3, 4",
                "_ = br_if x, first",
                "_ = return t2",
                "label first:",
                "_ = return t1",
                "end",
                "public func pick(x: bool) int64 then",
                "var a: int64 = copy int64, 10",
                "var b: int64 = copy int64, 20",
                "_ = br_if x, yes",
                "_ = return b",
                "label yes:",
                "_ = return a",
                "end",
            ),
        },
        out="1\n3\n10\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0048",
        "§6.9 indirect recursion mixes direct returns with call_indirect frames",
        files={
            "root.tc": P(
                "import lib",
                "var r: lib.B = call lib.rec, 5",
                "_ = writeln int64, r.a",
            ),
            "lib.tc": L(
                "public struct B then",
                "var a: int64",
                "end",
                "public func rec(n: int64) B then",
                "var zero: int64 = copy int64, 0",
                "var stop: bool = eq int64, n, zero",
                "var r: B = make B, 0",
                "_ = br_if stop, done",
                "var fp: fnptr<B, int64> = funcaddr Self.rec",
                "var m: int64 = sub int64, n, 1",
                "var d: B = make B, 0",
                "unsafe then",
                "d = call_indirect fp, m",
                "end",
                "var a1: int64 = add int64, d.a, 1",
                "r = make B, a1",
                "label done:",
                "_ = return r",
                "end",
            ),
        },
        out="5\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0049",
        "§3.7/§6.12 struct field copy, call result and load beyond 4095-byte offsets",
        files={
            "root.tc": P(
                "import lib",
                "var i0: lib.Inner = make lib.Inner, 0, 0",
                "var o: lib.Outer = make lib.Outer, 1, i0, 3",
                "var n: lib.Inner = make lib.Inner, 11, 22",
                "o.inner = copy lib.Inner, n",
                "var r: lib.Inner = call lib.mk",
                "o.inner = call lib.id, r",
                "var p: ptr<lib.Inner> = addr lib.Inner, o.inner",
                "unsafe then",
                "var t: lib.Inner = load lib.Inner, p",
                "end",
                "_ = writeln int64, o.inner.a",
                "_ = writeln int64, o.inner.b",
                "_ = writeln int32, o.tail",
            ),
            "lib.tc": L(
                "public struct Inner then",
                "var a: int64",
                "var b: int64",
                "end",
                "public struct Outer then",
                "var pad: int8 @padding(5000)",
                "var inner: Inner",
                "var tail: int32",
                "end",
                "public func id(x: Inner) Inner then",
                "_ = return x",
                "end",
                "public func mk() Inner then",
                "var x: Inner = make Inner, 11, 22",
                "_ = return x",
                "end",
            ),
        },
        out="11\n22\n3\n",
    ))
    # 300 composite arguments: the outgoing argument area exceeds 4 KiB.
    count = 300
    signature = ", ".join(f"p{i}: S" for i in range(count))
    body = []
    previous = None
    for index in range(count):
        body.append(
            f"var c{index}: int64 = add int64, p{index}.a, p{index}.c")
        if previous is None:
            body.append(f"var s0: int64 = copy int64, c0")
        else:
            body.append(
                f"var s{index}: int64 = add int64, s{previous}, c{index}")
        previous = index
    body.append(f"_ = return s{previous}")
    lib_text = "\n".join(
        ["#lib", "public struct S then", "var a: int64", "var b: int64",
         "var c: int64", "end",
         f"public func many({signature}) int64 then"] + body + ["end"]) + "\n"
    lines = ["import lib"]
    for index in range(count):
        lines.append(f"var v{index}: lib.S = make lib.S, 1, 2, 3")
    lines.append(
        "var r: int64 = call lib.many, " + ", ".join(
            f"v{i}" for i in range(count)))
    lines.append("_ = writeln int64, r")
    cases.append(engine.case(
        "TC-DIFF-S8-0050",
        "§8.5 300 composite arguments place a multi-kilobyte outgoing area",
        files={"root.tc": P(*lines), "lib.tc": lib_text},
        out=f"{4 * count}\n",
    ))
    cases.append(engine.case(
        "TC-DIFF-S8-0051",
        "§8.5 a 6008-byte composite argument and return uses the stack and x8",
        files={
            "root.tc": P(
                "import lib",
                "var b: lib.Big = make lib.Big, 7, 9",
                "var c: lib.Big = call lib.id, b",
                "var r: int64 = call lib.get, c",
                "_ = writeln int64, r",
                "_ = writeln int64, c.a",
            ),
            "lib.tc": L(
                "public struct Big then",
                "var a: int64 @padding(6000)",
                "var b: int64",
                "end",
                "public func id(x: Big) Big then",
                "_ = return x",
                "end",
                "public func get(x: Big) int64 then",
                "_ = return x.b",
                "end",
            ),
        },
        out="9\n7\n",
    ))
    return cases


EXTRA = EXTRA + _advanced_cases()


def _byref_composition_case():
    return engine.case(
        "TC-DIFF-S8-0052",
        "§8.5 a by-reference parameter can be forwarded to another by-reference call",
        files={
            "root.tc": P(
                "import lib",
                "var b: lib.Big = make lib.Big, 4, 5, 6",
                "var r: int64 = call lib.outer, b",
                "_ = writeln int64, r",
            ),
            "lib.tc": L(
                "public struct Big then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public func inner(x: Big) int64 then",
                "var r: int64 = add int64, x.a, x.b",
                "var s: int64 = add int64, r, x.c",
                "_ = return s",
                "end",
                "public func outer(x: Big) int64 then",
                "var r: int64 = call Self.inner, x",
                "var s: int64 = add int64, r, 1",
                "_ = return s",
                "end",
            ),
        },
        out="16\n",
    )


EXTRA = EXTRA + [_byref_composition_case()]


def _byref_field_indirect_case():
    return engine.case(
        "TC-DIFF-S8-0053",
        "§8.5 by-reference arguments copy from a field operand and through call_indirect",
        files={
            "root.tc": P(
                "import lib",
                "var i: lib.Big = make lib.Big, 2, 3, 4",
                "var o: lib.Outer = make lib.Outer, 1, i, 9",
                "var r: int64 = call lib.take, o.big",
                "_ = writeln int64, r",
                "var fp: fnptr<int64, lib.Big> = funcaddr lib.take",
                "unsafe then",
                "var s: int64 = call_indirect fp, o.big",
                "_ = writeln int64, s",
                "end",
            ),
            "lib.tc": L(
                "public struct Big then",
                "var a: int64",
                "var b: int64",
                "var c: int64",
                "end",
                "public struct Outer then",
                "var tag: int8",
                "var big: Big",
                "var tail: int32",
                "end",
                "public func take(x: Big) int64 then",
                "var r: int64 = add int64, x.a, x.b",
                "var s: int64 = add int64, r, x.c",
                "_ = return s",
                "end",
            ),
        },
        out="9\n9\n",
    )


EXTRA = EXTRA + [_byref_field_indirect_case()]

CASES = CASES + EXTRA


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if len(S6_IDS) < 60:
        print(f"s8.diff: only {len(S6_IDS)} W=64 S6 fixtures found")
        return 1
    if len(EXTRA) < 27:
        print(f"s8.diff: only {len(EXTRA)} S8-specific cases present")
        return 1
    return engine.run_cases(CASES, "s8.diff", keep=args.keep)


if __name__ == "__main__":
    raise SystemExit(main())
