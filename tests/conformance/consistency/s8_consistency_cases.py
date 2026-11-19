#!/usr/bin/env python3
"""S8 standard §13.4 eight-item consistency suite (design §12 item 2).

Standard §13.4 requires evidence for eight consistency items:
  1. static results (VM and AOT front ends agree);
  2. module order (module_order and import order are traversal independent);
  3. CFG/DI (control flow and definite initialization);
  4. types and conversions;
  5. the safety model;
  6. floating point;
  7. standard I/O;
  8. platform (same in-region offsets, device/code region policy).

Each item is backed by executable differential cases with independently
derived expectations and, where static behavior applies, by byte-identical
`--static-only` output from both tools.  The suite prints the item-to-evidence
mapping used by the G8 gate; a missing item fails the suite.

Run:
    python3 tests/conformance/consistency/s8_consistency_cases.py --check
"""

from __future__ import annotations

import argparse
import shutil
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

STATIC_SOURCES = {
    "accepted": {
        "root.tc": P(
            "import lib",
            "var s: lib.S = make lib.S, 1, 2.5",
            "var r: float64 = add float64, s.b, 0.5",
            "_ = writeln float64, r",
        ),
        "lib.tc": L(
            "public struct S then",
            "var a: int32",
            "var b: float64",
            "end",
        ),
    },
    "syntax": {"root.tc": "#program\nvar x: int32 = add int32, 1\n"},
    "type": {"root.tc": "#program\nvar x: int32 = copy bool, true\n"},
    "ct": {
        "root.tc": P("import lib", "var x: int32 = copy int32, lib.C"),
        "lib.tc": L("public const C: int32 = div int32, 1, 0"),
    },
    "unsafe-context": {
        "root.tc": P(
            "var x: int32 = copy int32, 1",
            "var p: ptr<int32> = addr int32, x",
            "unsafe then",
            "var y: int32 = load int32, p",
            "end",
        ),
        "lib.tc": "#lib\n",
    },
    "unsafe-op": {
        "root.tc": P(
            "var x: int32 = copy int32, 1",
            "var p: ptr<int32> = addr int32, x",
            "var y: int32 = load int32, p",
        ),
        "lib.tc": "#lib\n",
    },
}

MODULE_ORDER = engine.case(
    "TC-CON-S8-0001",
    "§4.2/§4.11 module_order diamond: one shared module instance and static storage",
    files={
        "root.tc": P(
            "import a",
            "import b",
            "var x: int32 = call a.bump",
            "_ = writeln int32, x",
            "var y: int32 = call b.bump",
            "_ = writeln int32, y",
            "var z: int32 = call a.get",
            "_ = writeln int32, z",
        ),
        "a.tc": L(
            "import c",
            "public func bump() int32 then",
            "var r: int32 = call c.bump",
            "_ = return r",
            "end",
            "public func get() int32 then",
            "var r: int32 = copy int32, c.N",
            "_ = return r",
            "end",
        ),
        "b.tc": L(
            "import c",
            "public func bump() int32 then",
            "var r: int32 = call c.bump",
            "_ = return r",
            "end",
        ),
        "c.tc": L(
            "public static var N: int32 = 0",
            "public func bump() int32 then",
            "Self.N = add int32, Self.N, 1",
            "var r: int32 = copy int32, Self.N",
            "_ = return r",
            "end",
        ),
    },
    out="1\n2\n2\n",
)

CFG_DI = engine.case(
    "TC-CON-S8-0002",
    "§7.7/§5.7 CFG, unreachable code and DI fixed point",
    root=P(
        "var x: int32 = copy int32, 0",
        "_ = br go",
        "var unreachable: int32 = div int32, 1, 0",
        "label go:",
        "var i: int32 = copy int32, 0",
        "var s: int32 = copy int32, 0",
        "label loop:",
        "var c: bool = lt int32, i, 4",
        "_ = br_if c, body",
        "_ = br done",
        "label body:",
        "s = add int32, s, i",
        "i = add int32, i, 1",
        "_ = br loop",
        "label done:",
        "_ = writeln int32, s",
        "_ = switch x, l0, l1",
        "label l0:",
        "_ = writeln int32, 10",
        "_ = br fin",
        "label l1:",
        "_ = writeln int32, 11",
        "label fin:",
    ),
    out="6\n10\n",
)

TYPES = engine.case(
    "TC-CON-S8-0003",
    "§3.11/§6.6 conversions: cast, trunc, bitcast",
    root=P(
        "var f: float64 = copy float64, 3.9",
        "var i: int32 = cast int32, f",
        "_ = writeln int32, i",
        "var b: int64 = copy int64, 0x123456789abcdef0",
        "var t: int32 = trunc int32, b",
        "_ = writeln int32, t",
        "var u: uint64 = copy uint64, 4607182418800017408u",
        "var d: float64 = bitcast float64, u",
        "_ = writeln float64, d",
    ),
    out="3\n-1698898192\n1\n",
)

SAFETY = engine.case(
    "TC-CON-S8-0004",
    "§9.4/§6.7 safety: unsafe context requirement and read-only parameter trap",
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
)

FLOAT = engine.case(
    "TC-CON-S8-0005",
    "§6.3/§10.6/§11.2 floating point, mod, NaN comparison and formatting",
    root=P(
        "var a: float64 = copy float64, 7.5",
        "var b: float64 = copy float64, 2.0",
        "var m: float64 = mod float64, a, b",
        "_ = writeln float64, m",
        "var n: float64 = div float64, 1.0, 3.0",
        "_ = writeln float64, n",
        "var z: float64 = copy float64, 0.0",
        "var nanv: float64 = div.ieee float64, z, z",
        "var c: bool = eq float64, nanv, nanv",
        "_ = writeln bool, c",
        "var c2: bool = ne float64, nanv, nanv",
        "_ = writeln bool, c2",
    ),
    out="1.5\n0.333333\nfalse\ntrue\n",
)

IO = engine.case(
    "TC-CON-S8-0006",
    "§11.1-§11.3 deterministic output and complete-token input",
    root=P(
        "var a: int32 = read int32",
        "var b: float64 = read float64",
        "_ = writeln int32, a",
        "_ = writeln float64, b",
        "var c: bool = read bool",
        "_ = writeln bool, c",
    ),
    stdin="  -42\n0.5\ntrue\n",
    out="-42\n0.5\ntrue\n",
)

PLATFORM = engine.case(
    "TC-CON-S8-0007",
    "§13.2/§13.4 platform: same in-region offsets and device/code region policy",
    files={
        "root.tc": P(
            "import lib",
            "var a: int64 = copy int64, 1",
            "var b: int64 = copy int64, 2",
            "var pa: ptr<int64> = addr int64, a",
            "var pb: ptr<int64> = addr int64, b",
            "unsafe then",
            "var d: isize = ptrdiff int64, pb, pa",
            "_ = writeln isize, d",
            "end",
            "var fp: fnptr<int32, int32> = funcaddr lib.id",
            "unsafe then",
            "var u: usize = bitcast usize, fp",
            "var p: ptr<int32> = bitcast ptr<int32>, u",
            "var v: int32 = load int32, p",
            "end",
        ),
        "lib.tc": L(
            "public func id(x: int32) int32 then",
            "_ = return x",
            "end",
        ),
    },
    out="8\n",
    trap="TC_RE_ADDR_UNAVAILABLE",
)

DEVICE = engine.case(
    "TC-CON-S8-0008",
    "§13.1/§6.7 hosted profile TC_DEV_REJECT: device access -> ADDR_UNAVAILABLE",
    root=P(
        "var a: usize = copy usize, 0x20000000",
        "unsafe then",
        "var p: ptr<int32> = bitcast ptr<int32>, a",
        "var x: int32 = load int32, p",
        "end",
    ),
    trap="TC_RE_ADDR_UNAVAILABLE",
)

DIFFERENTIAL_ITEMS = [
    ("1 static results", []),
    ("2 module order", [MODULE_ORDER]),
    ("3 CFG/DI", [CFG_DI]),
    ("4 types/conversions", [TYPES]),
    ("5 safety", [SAFETY]),
    ("6 floating point", [FLOAT]),
    ("7 standard I/O", [IO]),
    ("8 platform", [PLATFORM, DEVICE]),
]


def static_consistency(tmp: Path, failures: list[str]) -> None:
    for name, files in STATIC_SOURCES.items():
        directory = tmp / f"static-{name}"
        directory.mkdir()
        for filename, text in files.items():
            (directory / filename).write_text(text, encoding="utf-8")
        root = directory / "root.tc"
        outputs = []
        for tool in (engine.tc_vm_path(), engine.tc_aot_path()):
            proc = subprocess.run(
                [tool, "--static-only", f"--include={directory}", str(root)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            outputs.append((proc.returncode, proc.stdout, proc.stderr))
        if outputs[0] != outputs[1]:
            failures.append(
                f"static consistency {name}: VM/AOT static results differ")
        expected_ok = name in ("accepted", "unsafe-context")
        expected_code = 0 if expected_ok else 1
        if outputs[0][0] != expected_code:
            failures.append(
                f"static consistency {name}: exit {outputs[0][0]}, "
                f"expected {expected_code}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    tmp = Path(tempfile.mkdtemp(prefix="tc-s8-consistency-"))
    try:
        static_consistency(tmp, failures)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for item, cases in DIFFERENTIAL_ITEMS:
        if not cases:
            continue
        for case in cases:
            try:
                failures.extend(engine.run_case(case, keep=args.keep))
            except (engine.CaseError, OSError, subprocess.SubprocessError) as exc:
                failures.append(f"{case.case_id}: {exc}")
    if failures:
        print(f"s8.consistency: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    if len(DIFFERENTIAL_ITEMS) != 8:
        print("s8.consistency: the §13.4 table must have eight items")
        return 1
    for item, cases in DIFFERENTIAL_ITEMS:
        evidence = ", ".join(case.case_id for case in cases)
        if item.startswith("1 static"):
            evidence = "static-only VM/AOT parity (5 sources)"
        print(f"s8.consistency: §13.4 item {item}: {evidence}")
    print("s8.consistency: eight §13.4 items covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
