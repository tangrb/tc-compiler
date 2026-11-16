#!/usr/bin/env python3
"""Per-opcode static-shape cases for the S6 opcode coverage rows.

The S6 runtime suite covers every opcode's normal and trap behavior.  These
companion L3 cases pin the remaining static-error rows: each opcode is
recognized, its LHS is legal, and the missing-operand shape is reported as
SEM `TC_CE_OPERAND_COUNT` at the mnemonic (standard §6.15.1/§12.5.1).  Type,
mode, safety and result-type rules are covered by the S3/S4 tables; this table
only closes the per-opcode "at least one static error" slot.

Run:
    python3 tests/conformance/static/s6_static_cases.py --write
    python3 tests/conformance/static/s6_static_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "opcode_static"

# Normative standard §6.2 opcode-table order (base and defined modes).
OPCODES = [
    "add", "add.wrap", "add.ieee", "sub", "sub.wrap", "sub.ieee", "mul",
    "mul.wrap", "mul.ieee", "div", "div.ieee", "mod", "mod.ieee", "neg",
    "neg.wrap", "abs", "shl", "shl.wrap", "shr", "and", "or", "xor", "not",
    "eq", "ne", "lt", "le", "gt", "ge", "cast", "trunc", "bitcast", "load",
    "load.volatile", "store", "store.volatile", "memmove", "addr", "ptradd",
    "ptrsub", "ptrdiff", "ptreq", "ptrne", "ptrlt", "ptrle", "ptrgt", "ptrge",
    "sizeof", "funcaddr", "call", "call_indirect", "funceq", "funcne", "make",
    "copy", "br", "br_if", "switch", "return", "trap", "read", "write",
    "writeln",
]

UNSAFE = {
    "store", "store.volatile", "load", "load.volatile", "memmove", "ptradd",
    "ptrsub", "call_indirect",
}

SPEC = ("§6.15.1 opcode 已识别但操作数数量/形状不符 -> OPERAND_COUNT；"
        "§12.5.1 助记符")


def case_id(index: int) -> str:
    return f"TC-SEM-OPC-{index:04d}"


def render(name: str, index: int):
    cid = case_id(index)
    files = {}
    if name == "return":
        files["root.tc"] = "#program\nimport lib\n"
        files["lib.tc"] = (
            "#lib\npublic func f() int32 then\n_ = return foo: 1\nend\n"
        )
        primary, line, column = "lib.tc", 3, 5
    elif name == "trap":
        files["root.tc"] = "#program\nvar x: int32 = trap\n"
        primary, line, column = "root.tc", 2, 16
    elif name in UNSAFE:
        files["root.tc"] = f"#program\nunsafe then\n_ = {name}\nend\n"
        primary, line, column = "root.tc", 3, 5
    else:
        files["root.tc"] = f"#program\n_ = {name}\n"
        primary, line, column = "root.tc", 2, 5
    yaml = (
        f"id: {cid}\n"
        "profile: common\n"
        "source: root.tc\n"
        f'spec: "{SPEC}"\n'
        "expect:\n"
        "  stage: SEM\n"
        "  code: TC_CE_OPERAND_COUNT\n"
        f"  primary: {{file: {primary}, line: {line}, column: {column}}}\n"
        "  related: []\n"
        "  diagnostics: exactly-one\n"
    )
    return cid, files, yaml


def write_all() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    expected = {}
    for index, name in enumerate(OPCODES, 1):
        cid, files, yaml = render(name, index)
        directory = OUT / cid.lower()
        directory.mkdir(parents=True, exist_ok=True)
        expected[directory] = []
        for filename, text in files.items():
            path = directory / filename
            path.write_text(text, encoding="utf-8")
            expected[directory].append(path)
        path = directory / f"{cid.lower()}.yaml"
        path.write_text(yaml, encoding="utf-8")
        expected[directory].append(path)
    for entry in sorted(OUT.iterdir()):
        if entry.is_dir() and entry not in expected:
            for child in entry.iterdir():
                child.unlink()
            entry.rmdir()
        elif entry.is_file():
            entry.unlink()
    return 0


def check_all() -> int:
    errors = 0
    for index, name in enumerate(OPCODES, 1):
        cid, files, yaml = render(name, index)
        directory = OUT / cid.lower()
        yaml_path = directory / f"{cid.lower()}.yaml"
        if not yaml_path.exists() or yaml_path.read_text(encoding="utf-8") != yaml:
            print(f"stale or missing {yaml_path}")
            errors += 1
        for filename, text in files.items():
            path = directory / filename
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                print(f"stale or missing {path}")
                errors += 1
    if errors:
        print(f"s6_static_cases: {errors} stale file(s)")
        return 1
    print(f"s6_static_cases: {len(OPCODES)} per-opcode static cases up to date")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("use exactly one of --write/--check")
    return write_all() if args.write else check_all()


if __name__ == "__main__":
    raise SystemExit(main())
