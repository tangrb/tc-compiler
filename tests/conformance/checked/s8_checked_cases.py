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
