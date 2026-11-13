#!/usr/bin/env python3
"""Generate the 0.0.46 machine-readable diagnostic-code table.

Single normative source: docs/TC语言及编译器标准-0.0.46.md §12.3.
Position rules: same document §12.5.1.
First-test stage: docs/TC开发测试计划-0.0.46.md 附录 A.

The generated JSON is committed.  `check_codes.py` re-derives the normative
facts from the standard and verifies the committed JSON (and, optionally, the
C table dump emitted by tc_dump_codes), so the JSON can never drift silently.

Usage:
    python3 tests/traceability/gen_codes.py [--check]

`--check` does not write; it fails if the committed JSON differs from what
would be generated.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STANDARD = ROOT / "docs" / "TC语言及编译器标准-0.0.46.md"
PLAN = ROOT / "docs" / "TC开发测试计划-0.0.46.md"
OUT = Path(__file__).resolve().parent / "codes.json"

CODE_RE = re.compile(r"`(TC_(?:CE|RE)_[A-Z0-9_]+)`")

GROUPS = [
    ("词法/语法", "lex_syn"),
    ("名称/模块", "name_module"),
    ("类型/声明/控制", "type_decl_control"),
    ("安全/常量", "safety_const"),
    ("运行时", "runtime"),
]


def _cells(line: str) -> list[str]:
    return [c.strip() for c in line.strip().strip("|").split("|")]


def _plain(cell: str) -> str:
    return cell.replace("`", "").strip()


def parse_standard_codes(text: str) -> list[dict]:
    """Parse the five §12.3 tables in document order."""
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("### 12.3 "))
    except StopIteration:
        raise SystemExit("standard: §12.3 not found")
    end = next(i for i, l in enumerate(lines[start:], start) if l.startswith("### 12.4 "))
    section = lines[start:end]

    group = None
    out: list[dict] = []
    for line in section:
        stripped = line.strip()
        if stripped.startswith("#### "):
            title = stripped[5:].strip()
            group = dict(GROUPS).get(title)
            continue
        if not stripped.startswith("| `TC_"):
            continue
        cells = _cells(stripped)
        if len(cells) == 4:
            code, print_name, stage, trigger = cells
        elif len(cells) == 3:
            code, print_name, trigger = cells
            stage = ""
        else:
            raise SystemExit(f"unexpected §12.3 row: {line!r}")
        code = _plain(code)
        if not CODE_RE.fullmatch(f"`{code}`"):
            raise SystemExit(f"bad code name: {code!r}")
        if group is None:
            raise SystemExit(f"code outside group: {code}")
        out.append(
            {
                "code": code,
                "print_name": _plain(print_name),
                "kind": "ce" if code.startswith("TC_CE_") else "re",
                "stage": _plain(stage) if stage else "RT",
                "trigger": _plain(trigger),
                "group": group,
            }
        )
    return out


def parse_positions(text: str) -> dict[str, str]:
    """Parse every code mentioned in the §12.5.1 position table."""
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("#### 12.5.1 "))
    except StopIteration:
        raise SystemExit("standard: §12.5.1 not found")
    end = next(i for i, l in enumerate(lines[start:], start) if l.startswith("#### 12.5.2 "))
    positions: dict[str, str] = {}
    for line in lines[start:end]:
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = _cells(stripped)
        if len(cells) != 2:
            continue
        codes = CODE_RE.findall(cells[0])
        if not codes:
            continue
        rule = _plain(cells[1])
        for code in codes:
            if code in positions:
                raise SystemExit(f"duplicate position rule for {code}")
            positions[code] = rule
    return positions


def parse_wsensitive(text: str) -> set[str]:
    marker = "**字长相关条件**："
    idx = text.find(marker)
    if idx < 0:
        raise SystemExit("standard: word-size condition paragraph not found")
    end = text.find("\n", idx)
    para = text[idx:end]
    # The sentence after the semicolon explicitly says STACK_OVERFLOW is
    # determined by D/stack size, not by W; it is not a word-size condition.
    para = para.split("。`TC_RE_STACK_OVERFLOW`")[0]
    return set(CODE_RE.findall(para))


def parse_first_test(text: str) -> dict[str, str]:
    """Parse 附录 A tables (A.1-A.5)."""
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("## 附录 A "))
    except StopIteration:
        raise SystemExit("plan: 附录 A not found")
    end = next(i for i, l in enumerate(lines) if l.startswith("## 附录 B "))
    result: dict[str, str] = {}
    for line in lines[start:end]:
        stripped = line.strip()
        if not stripped.startswith("| `TC_"):
            continue
        cells = _cells(stripped)
        code = _plain(cells[0])
        if len(cells) >= 4:
            # A.1-A.4: code | stage | first test | notes
            first = _plain(cells[2])
        elif len(cells) == 3:
            # A.5: code | first test | notes
            first = _plain(cells[1])
        else:
            raise SystemExit(f"unexpected 附录 A row: {line!r}")
        result[code] = first
    return result


# §12.2 第 4 项: a more specific code wins at the identical source position.
# This is a contract-level default; site-specific rules (§12.2 #9/#10) are
# carried by the diagnostic record's origin/site tags, not by this column.
SAME_SITE_RANK = {
    "TC_CE_SYNTAX": 0,
    "TC_CE_MISSING_END": 10,
    "TC_CE_LITERAL_TYPE": 11,
    "TC_CE_LITERAL_RANGE": 10,
    "TC_CE_TYPE_MISMATCH": 1,
    "TC_CE_CONDITION_TYPE": 0,
    "TC_CE_RETURN_TYPE": 5,
    "TC_CE_RETURN_FORM": 5,
    "TC_CE_SWITCH_TYPE": 5,
    "TC_CE_OPERAND_COUNT": 6,
}

# §6.3.1 fixed strict-float exception priority: INVALID → DIV_ZERO →
# OVERFLOW → UNDERFLOW (lower value is reported first at one site).
CLAUSE_RANK = {
    "TC_RE_FLOAT_INVALID": 0,
    "TC_RE_DIV_ZERO": 1,
    "TC_RE_FLOAT_OVERFLOW": 2,
    "TC_RE_FLOAT_UNDERFLOW": 3,
    "TC_RE_NEGATIVE_SHIFT": 0,
}

IMPORT_CODES = {
    "TC_CE_IMPORT_NOT_FOUND",
    "TC_CE_IMPORT_NOT_LIB",
    "TC_CE_DUPLICATE_IMPORT",
    "TC_CE_CIRCULAR_IMPORT",
    "TC_CE_IMPORT_NAME_CONFLICT",
}


def build() -> dict:
    text = STANDARD.read_text(encoding="utf-8")
    plan = PLAN.read_text(encoding="utf-8")
    codes = parse_standard_codes(text)
    positions = parse_positions(text)
    w_sensitive = parse_wsensitive(text)
    first_test = parse_first_test(plan)

    missing = [c["code"] for c in codes if c["code"] not in positions]
    if missing:
        raise SystemExit(f"no §12.5.1 position rule for: {', '.join(missing)}")
    missing = [c["code"] for c in codes if c["code"] not in first_test]
    if missing:
        raise SystemExit(f"no 附录 A first-test stage for: {', '.join(missing)}")

    for c in codes:
        code = c["code"]
        c["report_stage"] = "IMPORT" if code in IMPORT_CODES else c["stage"]
        c["position"] = positions[code]
        c["w_sensitive"] = code in w_sensitive
        c["first_test_stage"] = first_test[code]
        c["same_site_rank"] = SAME_SITE_RANK.get(code, 0)
        c["clause_rank"] = CLAUSE_RANK.get(code, 0)

    ordered = [
        "code",
        "print_name",
        "kind",
        "stage",
        "report_stage",
        "trigger",
        "position",
        "w_sensitive",
        "first_test_stage",
        "group",
        "same_site_rank",
        "clause_rank",
    ]
    return {
        "spec": "docs/TC语言及编译器标准-0.0.46.md §12.3; §12.5.1; docs/TC开发测试计划-0.0.46.md 附录 A",
        "language_version": "0.0.46",
        "codes": [{k: c[k] for k in ordered} for c in codes],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    payload = build()
    rendered = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not OUT.exists():
            print(f"FAIL: {OUT} missing; run gen_codes.py", file=sys.stderr)
            return 1
        if OUT.read_text(encoding="utf-8") != rendered:
            print(
                "FAIL: codes.json is stale; regenerate with tests/traceability/gen_codes.py",
                file=sys.stderr,
            )
            return 1
        ce = sum(1 for c in payload["codes"] if c["kind"] == "ce")
        re_ = sum(1 for c in payload["codes"] if c["kind"] == "re")
        print(f"OK: codes.json matches standard ({ce} CE + {re_} RE = {len(payload['codes'])})")
        return 0
    OUT.write_text(rendered, encoding="utf-8")
    ce = sum(1 for c in payload["codes"] if c["kind"] == "ce")
    re_ = sum(1 for c in payload["codes"] if c["kind"] == "re")
    print(f"wrote {OUT} ({ce} CE + {re_} RE = {len(payload['codes'])})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
