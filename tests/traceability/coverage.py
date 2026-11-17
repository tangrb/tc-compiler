#!/usr/bin/env python3
"""Build the TC-Compiler 0.0.46 structural coverage matrix skeleton.

Rows are derived from the standard, the design and the test plan, never from
an implementation:

  * every standard chapter (§0-§14) with positive/negative slots,
  * all 80 diagnostic codes with trigger/no-false-positive/position/stage slots,
  * every opcode in the normative §6.2 tables (cross-checked against the
    TcOpcode enum in design §3.2) with normal/static-error/trap slots,
  * the 15 design §12 verification items with suite-mapping/gate slots,
  * the ten first-diagnostic ordering rules in standard §12.2,
  * explicit same-position code priorities (SAFE_VIOLATION over UNSAFE_*/FAST_CALL,
    call argument order, make field order, call safety first),
  * the 32/64 flip table in standard §13.2.2,
  * every annotation, the four TcDevicePolicy values, the four TcIoSrc states
    and the normal/trap exit branches.

All rows start as "uncovered".  Only named stage rows in L0_COVERED_ROWS,
S1_COVERED_ROWS, S2_COVERED_ROWS, S3_COVERED_ROWS, S4_COVERED_ROWS,
S5_COVERED_ROWS, S6_COVERED_ROWS, S7_COVERED_ROWS and S8_COVERED_ROWS may
be marked
"covered" with a real case
id; missing coverage for everything else is explicit and is never silently
rendered as covered.

Usage:
    python3 tests/traceability/coverage.py            # regenerate committed files
    python3 tests/traceability/coverage.py --check    # fail if committed files are stale
    python3 tests/traceability/coverage.py --out-dir /tmp/tc-coverage
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
DEFAULT_CODES = HERE / "codes.json"
DEFAULT_OUT = HERE
DEFAULT_STANDARD = REPO_ROOT / "docs" / "TC语言及编译器标准-0.0.46.md"
DEFAULT_DESIGN = REPO_ROOT / "docs" / "TC原生编译器设计说明书-0.0.46.md"
DEFAULT_TEST_PLAN = REPO_ROOT / "docs" / "TC开发测试计划-0.0.46.md"

JSON_NAME = "coverage.generated.json"
MARKDOWN_NAME = "coverage-report.md"

CATEGORY_ORDER = [
    "chapter",
    "code",
    "opcode",
    "design12",
    "first_diag_rule",
    "same_site_priority",
    "flip32_64",
    "annotation",
    "device_policy",
    "io_source_state",
    "exit_strategy",
]

CATEGORY_LABELS = {
    "chapter": "Standard chapter",
    "code": "Diagnostic code",
    "opcode": "Opcode",
    "design12": "Design §12 item",
    "first_diag_rule": "First-diagnostic rule (§12.2)",
    "same_site_priority": "Same-position code priority",
    "flip32_64": "32/64 flip",
    "annotation": "Annotation",
    "device_policy": "Device policy",
    "io_source_state": "I/O source state",
    "exit_strategy": "Exit strategy",
}


# S0 L0 cases that pin a contract-level row.  These are not conformance
# .tc cases; they are named in tests/unit/README.md.
L0_COVERED_ROWS = {
    "same_site_priority:SAFE_VIOLATION_OVER_UNSAFE:priority": ["TC-L0-DIAG-0002"],
    "same_site_priority:CALL_ARGUMENT_ORDER:priority": ["TC-L0-DIAG-0003"],
    "same_site_priority:MAKE_FIELD_ORDER:priority": ["TC-L0-DIAG-0004"],
    "same_site_priority:CALL_SAFETY_FIRST:priority": ["TC-L0-DIAG-0005"],
    "same_site_priority:NEGATIVE_SHIFT_BEFORE_OVERFLOW:priority": ["TC-L0-DIAG-0006"],
    # §12.2 rules 01-05 and 09-10 are pinned by the L0 comparator tests.
    "first_diag_rule:01:combination_case": ["TC-L0-DIAG-0009"],
    "first_diag_rule:02:combination_case": ["TC-L0-DIAG-0009"],
    "first_diag_rule:03:combination_case": ["TC-L0-DIAG-0009"],
    "first_diag_rule:04:combination_case": ["TC-L0-DIAG-0009"],
    "first_diag_rule:05:combination_case": ["TC-L0-DIAG-0006"],
    "first_diag_rule:09:combination_case": ["TC-L0-DIAG-0001"],
    "first_diag_rule:10:combination_case": [
        "TC-L0-DIAG-0003", "TC-L0-DIAG-0004", "TC-L0-DIAG-0007",
    ],
}

# Rules 06-08 close when SEM layout, deterministic module traversal and the
# word-size matrix are implemented.
SCOPE_NA_ROWS = {}

# S1 LEX/SYN rows.  Case ids are real artefacts: conformance cases live under
# tests/conformance/static/{lex,syn,accept} and the L1 suite is
# tests/unit/frontend_test.c.  Only the seven LEX/SYN codes and the lexical /
# EBNF chapter rows are marked; every SEM/CT/VM/backend row stays uncovered
# until its stage.
_S1_SYNTAX = ["TC-LEX-SYN-0001", "TC-SYN-OP-0001"]
S1_COVERED_ROWS = {
    # §2 词法 and §14 EBNF: positive acceptance and negative diagnostics.
    "chapter:§2:positive": ["TC-TOOL-SYN-0004", "TC-TOOL-SYN-0005"],
    "chapter:§2:negative": ["TC-LEX-SYN-0001", "TC-LEX-SYN-0025"],
    "chapter:§14:positive": ["TC-TOOL-SYN-0001", "TC-TOOL-SYN-0009"],
    "chapter:§14:negative": ["TC-SYN-GRAM-0001", "TC-SYN-GRAM-0004"],
    # TC_CE_SYNTAX
    "code:TC_CE_SYNTAX:trigger": ["TC-LEX-SYN-0001"],
    "code:TC_CE_SYNTAX:no_false_positive": ["TC-TOOL-SYN-0001"],
    "code:TC_CE_SYNTAX:position": [
        "TC-LEX-SYN-0001", "TC-SYN-END-0006", "TC-SYN-OP-0010",
        "TC-LEX-SYN-0031", "TC-LEX-SYN-0032",
    ],
    "code:TC_CE_SYNTAX:stage": ["TC-LEX-SYN-0001", "TC-SYN-OP-0001"],
    # TC_CE_MISSING_END
    "code:TC_CE_MISSING_END:trigger": ["TC-SYN-END-0001"],
    "code:TC_CE_MISSING_END:no_false_positive": ["TC-TOOL-SYN-0001"],
    "code:TC_CE_MISSING_END:position": ["TC-SYN-END-0004", "TC-SYN-END-0008"],
    "code:TC_CE_MISSING_END:stage": ["TC-SYN-END-0001"],
    # TC_CE_MODULE_LAYER
    "code:TC_CE_MODULE_LAYER:trigger": ["TC-SYN-LAYER-0001"],
    "code:TC_CE_MODULE_LAYER:no_false_positive": ["TC-TOOL-SYN-0001"],
    "code:TC_CE_MODULE_LAYER:position": ["TC-SYN-LAYER-0003"],
    "code:TC_CE_MODULE_LAYER:stage": ["TC-SYN-LAYER-0001"],
    # TC_CE_MISSING_VISIBILITY
    "code:TC_CE_MISSING_VISIBILITY:trigger": ["TC-SYN-VIS-0001"],
    "code:TC_CE_MISSING_VISIBILITY:no_false_positive": ["TC-TOOL-SYN-0001"],
    "code:TC_CE_MISSING_VISIBILITY:position": ["TC-SYN-VIS-0005"],
    "code:TC_CE_MISSING_VISIBILITY:stage": ["TC-SYN-VIS-0002"],
    # TC_CE_PROGRAM_MODE_MISUSE
    "code:TC_CE_PROGRAM_MODE_MISUSE:trigger": ["TC-SYN-MODE-0001"],
    "code:TC_CE_PROGRAM_MODE_MISUSE:no_false_positive": ["TC-TOOL-SYN-0002"],
    "code:TC_CE_PROGRAM_MODE_MISUSE:position": ["TC-SYN-MODE-0005", "TC-SYN-SELF-0001"],
    "code:TC_CE_PROGRAM_MODE_MISUSE:stage": ["TC-SYN-MODE-0008"],
    # TC_CE_VAR_MISSING_INIT
    "code:TC_CE_VAR_MISSING_INIT:trigger": ["TC-SYN-VAR-0001"],
    "code:TC_CE_VAR_MISSING_INIT:no_false_positive": ["TC-TOOL-SYN-0002"],
    "code:TC_CE_VAR_MISSING_INIT:position": ["TC-SYN-VAR-0003"],
    "code:TC_CE_VAR_MISSING_INIT:stage": ["TC-SYN-VAR-0002"],
    # TC_CE_LITERAL_LEX_RANGE
    "code:TC_CE_LITERAL_LEX_RANGE:trigger": ["TC-LEX-SYN-0015"],
    "code:TC_CE_LITERAL_LEX_RANGE:no_false_positive": ["TC-TOOL-SYN-0004"],
    "code:TC_CE_LITERAL_LEX_RANGE:position": ["TC-LEX-SYN-0016"],
    "code:TC_CE_LITERAL_LEX_RANGE:stage": ["TC-LEX-SYN-0017"],
    # §12.2 #1 stage priority and #3 source order, exercised by real sources.
    "first_diag_rule:01:combination_case": ["TC-UNIT-ORDER-0001"],
    "first_diag_rule:03:combination_case": [
        "TC-UNIT-ORDER-0002", "TC-UNIT-ORDER-0003",
    ],
    # §12.2 #4 same-position specialized code beats generic SYNTAX:
    # MISSING_END (unpaired opener) and VAR_MISSING_INIT (declaration name).
    "first_diag_rule:04:combination_case": [
        "TC-SYN-END-0001", "TC-SYN-VAR-0001",
    ],
}

CALL_ARGUMENT_ORDER = [
    "TC_CE_DUPLICATE_ARGUMENT",
    "TC_CE_UNKNOWN_ARGUMENT",
    "TC_CE_MISSING_ARGUMENT",
    "TC_CE_ARGUMENT_COUNT",
    "TC_CE_ARGUMENT_ORDER",
    "TC_CE_TYPE_MISMATCH",
]
MAKE_FIELD_ORDER = [
    "TC_CE_DUPLICATE_FIELD",
    "TC_CE_UNKNOWN_FIELD",
    "TC_CE_MISSING_FIELD",
    "TC_CE_OPERAND_COUNT",
    "TC_CE_FIELD_ORDER",
    "TC_CE_TYPE_MISMATCH",
]


class CoverageError(Exception):
    pass


def read_text(path: Path, label: str) -> str:
    if not path.is_file():
        raise CoverageError(f"{label} missing: {path}")
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CoverageError(f"cannot read {label} {path}: {exc}") from exc


def section_between(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise CoverageError(f"marker not found: {start_marker}")
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        end = len(text)
    return text[start:end]


def strip_code(cell: str) -> str:
    cell = cell.strip()
    if len(cell) >= 2 and cell.startswith("`") and cell.endswith("`"):
        cell = cell[1:-1]
    return cell.strip()


def table_rows(section: str):
    for line in section.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells:
            continue
        first = strip_code(cells[0])
        if not first or set(first) <= set("-: "):
            continue
        yield cells


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_chapters(test_plan: str, standard: str):
    section = section_between(test_plan, "### 8.1 标准章节 → 测试套件", "### 8.2")
    items = []
    for cells in table_rows(section):
        chapter = strip_code(cells[0])
        if not re.fullmatch(r"§\d+", chapter):
            continue
        if len(cells) < 4:
            raise CoverageError(f"malformed chapter row: {cells}")
        items.append({
            "item_id": f"chapter:{chapter}",
            "chapter": chapter,
            "title": strip_code(cells[1]),
            "primary_stage": strip_code(cells[2]),
            "suites": [part.strip() for part in strip_code(cells[3]).split("、") if part.strip()],
        })
    expected = [f"§{n}" for n in range(0, 15)]
    found = [item["chapter"] for item in items]
    if found != expected:
        raise CoverageError(
            f"test plan §8.1 chapters must be {expected}, found {found}"
        )
    # Cross-check that the standard really has §0-§14.
    std_chapters = {f"§{m}" for m in re.findall(r"^## (\d+)\.", standard, re.M)}
    missing = [chapter for chapter in expected if chapter not in std_chapters]
    if missing:
        raise CoverageError(f"standard is missing chapters: {missing}")
    return items


def parse_opcodes(standard: str, design: str):
    section = section_between(standard, "### 6.2 opcode 表（规范性）", "### 6.3 算术语义")
    group = ""
    opcodes = []
    seen = set()
    for line in section.splitlines():
        stripped = line.strip()
        heading = re.match(r"^####\s+6\.2\.\d+\s+(.+)$", stripped)
        if heading:
            group = heading.group(1).strip()
            continue
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 3:
            continue
        op_cell = cells[0]
        if strip_code(op_cell).lower() == "op" or set(strip_code(op_cell)) <= set("-: "):
            continue
        for piece in op_cell.split("/"):
            name = strip_code(piece.strip())
            if not re.fullmatch(r"[a-z_]+(?:\.[a-z]+)?", name):
                continue
            if name in seen:
                raise CoverageError(f"duplicate opcode in standard §6.2: {name}")
            seen.add(name)
            base, _, mode = name.partition(".")
            opcodes.append({
                "opcode": name,
                "base_opcode": base,
                "mode": mode or "strict",
                "group": group,
                "trap_text": strip_code(cells[-2]),
                "safety": strip_code(cells[-1]),
                "trap_capable": not strip_code(cells[-2]).startswith("无"),
            })
    if len(opcodes) != 63:
        raise CoverageError(f"expected 63 opcodes in standard §6.2, found {len(opcodes)}")

    # Cross-check base names against design §3.2 TcOpcode (parsed, not hardcoded).
    design_section = section_between(design, "### 3.2 指令", "### 3.3")
    enum_match = re.search(r"typedef enum\s*\{(.*?)\}\s*TcOpcode\s*;", design_section, re.S)
    if not enum_match:
        raise CoverageError("cannot parse the TcOpcode enum in design §3.2")
    design_ops = re.findall(r"\bTC_[A-Z0-9_]+\b", enum_match.group(1))
    design_ops = [name for name in design_ops if name != "TC_OP_DISCARD"]
    design_set = set(design_ops)
    standard_base = {item["base_opcode"] for item in opcodes}
    expected_design = {"TC_" + base.upper() for base in standard_base}
    if design_set != expected_design:
        raise CoverageError(
            "TcOpcode cross-check failed: only in design "
            f"{sorted(design_set - expected_design)}; only in standard "
            f"{sorted(expected_design - design_set)}"
        )
    for item in opcodes:
        item["tc_opcode"] = "TC_" + item["base_opcode"].upper()
    return opcodes, design_ops


def parse_design12(design: str, test_plan: str):
    section = section_between(design, "## 12. 验证策略", "## 13. 分期路线")
    items = []
    current = None
    for line in section.splitlines():
        match = re.match(r"^(\d+)\.\s+\*\*(.+?)\*\*(.*)$", line)
        if match:
            if current is not None:
                items.append(current)
            rest = match.group(3).strip()
            if rest.startswith("：") or rest.startswith(":"):
                rest = rest[1:].strip()
            current = {
                "number": int(match.group(1)),
                "title": match.group(2).strip(),
                "detail": rest,
            }
            continue
        if current is not None:
            stripped = line.strip()
            if stripped and not stripped.startswith("---"):
                current["detail"] = (current["detail"] + " " + stripped).strip()
    if current is not None:
        items.append(current)
    if [item["number"] for item in items] != list(range(1, 16)):
        raise CoverageError("design §12 must contain numbered items 1..15")

    plan_section = section_between(test_plan, "### 8.2 设计 §12 十五项 → 套件", "### 8.3")
    plan_rows = {}
    for cells in table_rows(plan_section):
        first = strip_code(cells[0])
        if not first.isdigit() or len(cells) < 4:
            continue
        plan_rows[int(first)] = {
            "plan_name": strip_code(cells[1]),
            "plan_stage": strip_code(cells[2]),
            "plan_suite": strip_code(cells[3]),
        }
    if sorted(plan_rows) != list(range(1, 16)):
        raise CoverageError("test plan §8.2 must map design items 1..15")
    for item in items:
        item.update(plan_rows[item["number"]])
    return items


def parse_first_diag_rules(standard: str):
    section = section_between(standard, "### 12.2 首个诊断顺序", "### 12.3")
    items = []
    current = None
    for line in section.splitlines():
        match = re.match(r"^(\d+)\.\s+\*\*(.+?)\*\*(.*)$", line.strip())
        if match:
            if current is not None:
                items.append(current)
            rest = match.group(3).strip()
            if rest.startswith("：") or rest.startswith(":"):
                rest = rest[1:].strip()
            current = {
                "number": int(match.group(1)),
                "title": match.group(2).strip(),
                "detail": rest,
            }
            continue
        if current is not None:
            stripped = line.strip()
            if stripped and not stripped.startswith("---"):
                current["detail"] = (current["detail"] + " " + stripped).strip()
    if current is not None:
        items.append(current)
    if [item["number"] for item in items] != list(range(1, 11)):
        raise CoverageError("standard §12.2 must contain numbered rules 1..10")
    return items


def parse_same_site_priorities(codes_data: dict, standard: str):
    """Derive explicit same-position code priorities from the frozen table.

    Sources: standard §12.3 (SAFE_VIOLATION trigger), §12.2 #10 (call/make
    order), §8.8.2/§9 (call safety).  Ranks are read from codes.json and
    cross-checked; this function never invents an implementation result.
    """
    entries = {
        entry.get("code"): entry
        for entry in codes_data.get("codes", [])
        if isinstance(entry, dict)
    }

    def require(code: str) -> dict:
        entry = entries.get(code)
        if entry is None:
            raise CoverageError(f"same-site priority: codes.json missing {code}")
        return entry

    def require_descending(sequence: list[str], label: str) -> None:
        ranks = [require(code).get("same_site_rank") for code in sequence]
        if not all(isinstance(rank, int) for rank in ranks):
            raise CoverageError(f"{label}: non-integer same_site_rank")
        if ranks != sorted(ranks, reverse=True) or len(set(ranks)) != len(ranks):
            raise CoverageError(f"{label}: ranks are not strictly descending: {ranks}")

    safe = require("TC_CE_SAFE_VIOLATION")
    trigger = safe.get("trigger", "")
    if "优先于" not in trigger or "UNSAFE" not in trigger or "FAST_CALL" not in trigger:
        raise CoverageError(
            "SAFE_VIOLATION trigger no longer says it precedes UNSAFE_*/FAST_CALL"
        )
    safe_rank = safe.get("same_site_rank")
    for code in ("TC_CE_UNSAFE_OPERATION", "TC_CE_UNSAFE_CALL", "TC_CE_FAST_CALL"):
        if require(code).get("same_site_rank") >= safe_rank:
            raise CoverageError(f"same-site priority not encoded: {code} >= SAFE_VIOLATION")

    require_descending(CALL_ARGUMENT_ORDER, "§12.2 #10 call argument order")
    require_descending(MAKE_FIELD_ORDER, "§12.2 #10 make field order")
    unsafe_rank = require("TC_CE_UNSAFE_CALL").get("same_site_rank")
    if unsafe_rank <= require("TC_CE_DUPLICATE_ARGUMENT").get("same_site_rank"):
        raise CoverageError("call safety rank does not precede argument checks")

    neg_rank = require("TC_RE_NEGATIVE_SHIFT").get("clause_rank")
    overflow_rank = require("TC_RE_INTEGER_OVERFLOW").get("clause_rank")
    if not isinstance(neg_rank, int) or not isinstance(overflow_rank, int):
        raise CoverageError("negative-shift/overflow clause ranks are not integers")
    if neg_rank >= overflow_rank:
        raise CoverageError("negative shift clause rank does not precede overflow")

    return [
        {
            "item_id": "same_site_priority:SAFE_VIOLATION_OVER_UNSAFE",
            "item": "SAFE_VIOLATION over UNSAFE_*/FAST_CALL",
            "slot": "priority",
            "spec": "standard §12.3 SAFE_VIOLATION trigger; §9.1/§9.3; test plan §3.6/§6.1",
            "codes": ["TC_CE_SAFE_VIOLATION", "TC_CE_UNSAFE_OPERATION",
                      "TC_CE_UNSAFE_CALL", "TC_CE_FAST_CALL"],
        },
        {
            "item_id": "same_site_priority:CALL_ARGUMENT_ORDER",
            "item": "call argument order",
            "slot": "priority",
            "spec": "standard §12.2 #10; test plan §3.6/§6.1",
            "codes": CALL_ARGUMENT_ORDER,
        },
        {
            "item_id": "same_site_priority:MAKE_FIELD_ORDER",
            "item": "make field order",
            "slot": "priority",
            "spec": "standard §12.2 #10; test plan §3.6/§6.1",
            "codes": MAKE_FIELD_ORDER,
        },
        {
            "item_id": "same_site_priority:CALL_SAFETY_FIRST",
            "item": "call safety before arguments",
            "slot": "priority",
            "spec": "standard §8.8.2/§12.2 #9 exception; test plan §3.6/§6.1",
            "codes": ["TC_CE_UNSAFE_CALL", "TC_CE_FAST_CALL",
                      "TC_CE_SAFE_VIOLATION", "TC_CE_DUPLICATE_ARGUMENT"],
        },
        {
            "item_id": "same_site_priority:NEGATIVE_SHIFT_BEFORE_OVERFLOW",
            "item": "negative shift before shl overflow",
            "slot": "priority",
            "spec": "standard §12.2 #5; test plan §3.6/§6.1",
            "codes": ["TC_RE_NEGATIVE_SHIFT", "TC_RE_INTEGER_OVERFLOW"],
        },
    ]


def parse_flips(standard: str):
    section = section_between(
        standard, "#### 13.2.2 字长相关静态结果", "#### 13.2.3 可移植写法（建议性）"
    )
    items = []
    for cells in table_rows(section):
        first = strip_code(cells[0])
        if first == "项" or len(cells) < 3:
            continue
        items.append({
            "item_id": f"flip32_64:{len(items) + 1:02d}",
            "item": first,
            "expected_w32": strip_code(cells[1]),
            "expected_w64": strip_code(cells[2]),
        })
    if len(items) != 17:
        raise CoverageError(f"expected 17 rows in standard §13.2.2, found {len(items)}")
    return items


def parse_annotations(standard: str):
    keyword_line = ""
    for line in standard.splitlines():
        if line.strip().startswith("| 注解 |"):
            keyword_line = line
            break
    if not keyword_line:
        raise CoverageError("cannot find the annotation row in standard §2.3")
    annotations = []
    for name in re.findall(r"@[a-z_]+", keyword_line):
        if name not in annotations:
            annotations.append(name)
    if len(annotations) != 16:
        raise CoverageError(f"expected 16 annotations in standard §2.3, found {len(annotations)}")

    section = section_between(standard, "### 8.6 注解与效果", "### 8.7 不支持的函数形态")
    details = {}
    for cells in table_rows(section):
        first = cells[0]
        if "注解" in first or len(cells) < 3:
            continue
        names = re.findall(r"@[a-z_]+", first)
        if not names:
            continue
        for name in names:
            details[name] = {
                "location": strip_code(cells[1]),
                "meaning": strip_code(cells[2]),
            }
    items = []
    for name in annotations:
        info = details.get(name)
        items.append({
            "item_id": f"annotation:{name}",
            "annotation": name,
            "location": info["location"] if info else "",
            "meaning": info["meaning"] if info else "",
            "spec": "§2.6, §8.6" if info else "§2.3, §2.6",
        })
    return items


def parse_device_policies(design: str):
    section = section_between(design, "## 8. 平台档案", "## 9. VM 解释器")
    enum_match = re.search(r"typedef enum\s*\{(.*?)\}\s*TcDevicePolicy\s*;", section, re.S)
    if not enum_match:
        raise CoverageError("cannot parse TcDevicePolicy in design §8")
    body = enum_match.group(1)
    names = re.findall(r"\bTC_DEV_[A-Z0-9_]+\b", body)
    if len(names) != 4:
        raise CoverageError(f"expected 4 TcDevicePolicy values, found {names}")
    descriptions = {}
    for line in body.splitlines():
        for name in re.findall(r"\bTC_DEV_[A-Z0-9_]+\b", line):
            comment = re.search(r"/\*(.*?)\*/", line)
            descriptions[name] = comment.group(1).strip() if comment else ""
    expected = {
        "TC_DEV_REJECT": "all device accesses -> TC_RE_ADDR_UNAVAILABLE",
        "TC_DEV_READ": "reads allowed; writes -> TC_RE_ADDR_UNAVAILABLE",
        "TC_DEV_READWRITE": "reads and writes allowed",
        "TC_DEV_VOLATILE": "only load.volatile/store.volatile; others -> TC_RE_ADDR_UNAVAILABLE",
    }
    return [
        {
            "item_id": f"device_policy:{name}",
            "policy": name,
            "description": descriptions.get(name, ""),
            "expected": expected[name],
            "spec": "design §8 (TcDevicePolicy), test plan §3.6/§7.5",
        }
        for name in names
    ]


def parse_io_states(design: str):
    section = section_between(design, "### 7.2 平台接口（四条缝）", "## 8. 平台档案")
    enum_match = re.search(r"typedef enum\s*\{([^}]*)\}\s*TcIoStatus\s*;", section)
    if not enum_match:
        raise CoverageError("cannot parse TcIoStatus in design §7.2")
    statuses = re.findall(r"\bTC_IO_[A-Z0-9_]+\b", enum_match.group(1))
    if statuses != ["TC_IO_OK", "TC_IO_EOF", "TC_IO_ERROR"]:
        raise CoverageError(f"unexpected TcIoStatus values: {statuses}")
    if "允许短读" not in section:
        raise CoverageError("design §7.2 no longer documents short reads")
    items = [
        {
            "item_id": "io_source_state:TC_IO_OK",
            "state": "TC_IO_OK",
            "description": "full read (n == cap)",
            "expected": "read/IO instruction consumes complete token; no trap",
        },
        {
            "item_id": "io_source_state:TC_IO_EOF",
            "state": "TC_IO_EOF",
            "description": "EOF before a complete token",
            "expected": "TC_RE_IO; no partial token and no target commit",
        },
        {
            "item_id": "io_source_state:TC_IO_ERROR",
            "state": "TC_IO_ERROR",
            "description": "source error",
            "expected": "TC_RE_IO; no partial token and no target commit",
        },
        {
            "item_id": "io_source_state:SHORT_READ",
            "state": "TC_IO_OK (short read)",
            "description": "n < cap with TC_IO_OK",
            "expected": "keep reading until the token is complete; never emit a partial token",
        },
    ]
    for item in items:
        item["spec"] = "design §7.2 (TcIoSrc), test plan §3.6/§7.5"
    return items


def parse_exit_strategies(design: str):
    section = section_between(design, "## 8. 平台档案", "## 9. VM 解释器")
    struct_match = re.search(r"typedef struct\s*\{(.*?)\}\s*TcExitPolicy\s*;", section, re.S)
    if not struct_match:
        raise CoverageError("cannot parse TcExitPolicy in design §8")
    fields = re.findall(r"\b(normal_status|trap_status)\b", struct_match.group(1))
    if fields != ["normal_status", "trap_status"]:
        raise CoverageError(f"unexpected TcExitPolicy fields: {fields}")
    descriptions = {
        "normal_status": ("normal_exit", "program terminates without trap"),
        "trap_status": ("trap_exit", "first runtime trap terminates the program"),
    }
    items = []
    for field in fields:
        branch, description = descriptions[field]
        items.append({
            "item_id": f"exit_strategy:{branch}",
            "branch": branch,
            "field": field,
            "description": description,
            "spec": "design §8 (TcExitPolicy), standard §13.1, test plan §3.6",
        })
    return items


def parse_standard_codes(standard: str):
    section = section_between(standard, "### 12.3 诊断码（完整集）", "### 12.4 不可达代码")
    order = []
    for line in section.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        code = strip_code(cells[0]) if cells else ""
        if re.fullmatch(r"TC_(?:CE|RE)_[A-Z0-9_]+", code):
            if code in order:
                raise CoverageError(f"duplicate code in standard §12.3: {code}")
            order.append(code)
    if len(order) != 80:
        raise CoverageError(f"expected 80 codes in standard §12.3, found {len(order)}")
    return order


# ---------------------------------------------------------------------------
# Row construction
# ---------------------------------------------------------------------------

def slot_row(category: str, item_id: str, item: str, slot: str, spec: str, **extra):
    row = {
        "id": f"{category}:{item_id.split(':', 1)[-1]}:{slot}",
        "category": category,
        "item_id": item_id,
        "item": item,
        "slot": slot,
        "spec": spec,
        "status": "uncovered",
        "case_ids": [],
    }
    row.update(extra)
    return row


# S2 module/IMPORT rows.
S2_COVERED_ROWS = {
    "code:TC_CE_IMPORT_NOT_FOUND:trigger": ["TC-IMP-0001"],
    "code:TC_CE_IMPORT_NOT_FOUND:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_IMPORT_NOT_FOUND:position": ["TC-IMP-0001"],
    "code:TC_CE_IMPORT_NOT_FOUND:stage": ["TC-IMP-0001"],
    "code:TC_CE_IMPORT_NOT_LIB:trigger": ["TC-IMP-0002"],
    "code:TC_CE_IMPORT_NOT_LIB:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_IMPORT_NOT_LIB:position": ["TC-IMP-0002"],
    "code:TC_CE_IMPORT_NOT_LIB:stage": ["TC-IMP-0002"],
    "code:TC_CE_DUPLICATE_IMPORT:trigger": ["TC-IMP-0003"],
    "code:TC_CE_DUPLICATE_IMPORT:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_DUPLICATE_IMPORT:position": ["TC-IMP-0003"],
    "code:TC_CE_DUPLICATE_IMPORT:stage": ["TC-IMP-0003"],
    "code:TC_CE_CIRCULAR_IMPORT:trigger": ["TC-IMP-0004"],
    "code:TC_CE_CIRCULAR_IMPORT:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_CIRCULAR_IMPORT:position": ["TC-IMP-0004"],
    "code:TC_CE_CIRCULAR_IMPORT:stage": ["TC-IMP-0004"],
    "code:TC_CE_IMPORT_NAME_CONFLICT:trigger": ["TC-IMP-0005"],
    "code:TC_CE_IMPORT_NAME_CONFLICT:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_IMPORT_NAME_CONFLICT:position": ["TC-IMP-0005"],
    "code:TC_CE_IMPORT_NAME_CONFLICT:stage": ["TC-IMP-0005"],
    "chapter:§4:positive": ["TC-TOOL-SEM-0006", "TC-TOOL-SEM-0007"],
    "chapter:§4:negative": ["TC-IMP-0001", "TC-IMP-0003"],
    "first_diag_rule:07:combination_case": [
        "TC-UNIT-MODULE-0001", "TC-TOOL-SEM-0006",
    ],
    "design12:13:suite_mapping": ["TC-UNIT-MODULE-0001"],
    "design12:13:gate_case": ["TC-TOOL-SEM-0006"],
}

# S3 name/type/conversion/layout rows.
S3_COVERED_ROWS = {
    "chapter:§0:positive": ["TC-TOOL-SEM-0009", "TC-TOOL-SEM-0010"],
    "chapter:§0:negative": ["TC-SEM-CONV-0003", "TC-SEM-LAYOUT-0001"],
    "chapter:§3:positive": ["TC-TOOL-SEM-0001", "TC-TOOL-SEM-0002"],
    "chapter:§3:negative": ["TC-SEM-TYPE-0001", "TC-SEM-TYPE-0004"],
    "chapter:§5:positive": ["TC-TOOL-SEM-0001", "TC-TOOL-SEM-0008"],
    "chapter:§5:negative": ["TC-SEM-NAME-0001", "TC-SEM-NAME-0003"],
    "chapter:§6:positive": ["TC-TOOL-SEM-0002", "TC-TOOL-SEM-0004"],
    "chapter:§6:negative": [
        "TC-SEM-OP-0001", "TC-SEM-OP-0004", "TC-SEM-MODE-0001",
    ],
    "chapter:§13:positive": ["TC-TOOL-SEM-0009", "TC-TOOL-SEM-0010"],
    "chapter:§13:negative": ["TC-SEM-LAYOUT-0001", "TC-SEM-CONV-0003"],
    "code:TC_CE_UNDEFINED_NAME:trigger": ["TC-SEM-NAME-0001"],
    "code:TC_CE_UNDEFINED_NAME:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_UNDEFINED_NAME:position": [
        "TC-SEM-NAME-0007", "TC-SEM-NAME-0112",
    ],
    "code:TC_CE_UNDEFINED_NAME:stage": ["TC-SEM-NAME-0001"],
    "code:TC_CE_DUPLICATE_NAME:trigger": ["TC-SEM-NAME-0002"],
    "code:TC_CE_DUPLICATE_NAME:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_DUPLICATE_NAME:position": ["TC-SEM-NAME-0002"],
    "code:TC_CE_DUPLICATE_NAME:stage": ["TC-SEM-NAME-0002"],
    "code:TC_CE_DUPLICATE_MEMBER:trigger": ["TC-SEM-NAME-0102"],
    "code:TC_CE_DUPLICATE_MEMBER:no_false_positive": ["TC-TOOL-SEM-0008"],
    "code:TC_CE_DUPLICATE_MEMBER:position": ["TC-SEM-NAME-0102"],
    "code:TC_CE_DUPLICATE_MEMBER:stage": ["TC-SEM-NAME-0102"],
    "code:TC_CE_DUPLICATE_PARAMETER:trigger": ["TC-SEM-NAME-0103"],
    "code:TC_CE_DUPLICATE_PARAMETER:no_false_positive": ["TC-TOOL-SEM-0005"],
    "code:TC_CE_DUPLICATE_PARAMETER:position": ["TC-SEM-NAME-0103"],
    "code:TC_CE_DUPLICATE_PARAMETER:stage": ["TC-SEM-NAME-0103"],
    "code:TC_CE_FUNCTION_SCOPE_ACCESS:trigger": [
        "TC-SEM-NAME-0101", "TC-SEM-NAME-0110",
    ],
    "code:TC_CE_FUNCTION_SCOPE_ACCESS:no_false_positive": ["TC-TOOL-SEM-0006"],
    "code:TC_CE_FUNCTION_SCOPE_ACCESS:position": [
        "TC-SEM-NAME-0101", "TC-SEM-NAME-0110",
    ],
    "code:TC_CE_FUNCTION_SCOPE_ACCESS:stage": ["TC-SEM-NAME-0101"],
    "code:TC_CE_UNDEFINED_FUNCTION:trigger": ["TC-SEM-FUNC-0101"],
    "code:TC_CE_UNDEFINED_FUNCTION:no_false_positive": ["TC-TOOL-SEM-0005"],
    "code:TC_CE_UNDEFINED_FUNCTION:position": ["TC-SEM-FUNC-0101"],
    "code:TC_CE_UNDEFINED_FUNCTION:stage": ["TC-SEM-FUNC-0101"],
    "code:TC_CE_PRIVATE_ACCESS:trigger": [
        "TC-SEM-NAME-0104", "TC-SEM-NAME-0109", "TC-SEM-NAME-0113",
    ],
    "code:TC_CE_PRIVATE_ACCESS:no_false_positive": ["TC-TOOL-SEM-0008"],
    "code:TC_CE_PRIVATE_ACCESS:position": [
        "TC-SEM-NAME-0104", "TC-SEM-FUNC-0102", "TC-SEM-NAME-0113",
    ],
    "code:TC_CE_PRIVATE_ACCESS:stage": ["TC-SEM-NAME-0105"],
    "code:TC_CE_PRIVATE_TYPE:trigger": ["TC-SEM-NAME-0106"],
    "code:TC_CE_PRIVATE_TYPE:no_false_positive": ["TC-TOOL-SEM-0008"],
    "code:TC_CE_PRIVATE_TYPE:position": ["TC-SEM-NAME-0107"],
    "code:TC_CE_PRIVATE_TYPE:stage": ["TC-SEM-NAME-0106"],
    "code:TC_CE_TYPE_MISMATCH:trigger": ["TC-SEM-TYPE-0001"],
    "code:TC_CE_TYPE_MISMATCH:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_TYPE_MISMATCH:position": ["TC-SEM-TYPE-0002", "TC-SEM-FIELD-0002"],
    "code:TC_CE_TYPE_MISMATCH:stage": ["TC-SEM-TYPE-0001"],
    "code:TC_CE_LITERAL_TYPE:trigger": ["TC-SEM-LIT-0001"],
    "code:TC_CE_LITERAL_TYPE:no_false_positive": ["TC-TOOL-SEM-0003"],
    "code:TC_CE_LITERAL_TYPE:position": ["TC-SEM-LIT-0002"],
    "code:TC_CE_LITERAL_TYPE:stage": ["TC-SEM-LIT-0001"],
    "code:TC_CE_LITERAL_RANGE:trigger": ["TC-SEM-LIT-0007"],
    "code:TC_CE_LITERAL_RANGE:no_false_positive": ["TC-TOOL-SEM-0003"],
    "code:TC_CE_LITERAL_RANGE:position": ["TC-SEM-LIT-0008"],
    "code:TC_CE_LITERAL_RANGE:stage": ["TC-SEM-LIT-0007"],
    "code:TC_CE_LITERAL_RANGE:w32": ["TC-SEM-LIT-0100", "TC-SEM-LIT-0101"],
    "code:TC_CE_LITERAL_RANGE:w64": ["TC-TOOL-SEM-0016"],
    "code:TC_CE_MODE:trigger": ["TC-SEM-MODE-0001"],
    "code:TC_CE_MODE:no_false_positive": ["TC-TOOL-SEM-0004"],
    "code:TC_CE_MODE:position": ["TC-SEM-MODE-0002"],
    "code:TC_CE_MODE:stage": ["TC-SEM-MODE-0001"],
    "code:TC_CE_ANNOTATION:trigger": ["TC-SEM-MODE-0003", "TC-SEM-ANN-0001"],
    "code:TC_CE_ANNOTATION:no_false_positive": ["TC-TOOL-SEM-0004", "TC-TOOL-SEM-0017"],
    "code:TC_CE_ANNOTATION:position": ["TC-SEM-MODE-0003", "TC-SEM-ANN-0001"],
    "code:TC_CE_ANNOTATION:stage": ["TC-SEM-MODE-0003"],
    "code:TC_CE_ANNOTATION:w32": ["TC-SEM-ANN-0001"],
    "code:TC_CE_ANNOTATION:w64": ["TC-TOOL-SEM-0017"],
    "code:TC_CE_OPERAND_COUNT:trigger": [
        "TC-SEM-OP-0001", "TC-SEM-OP-0004", "TC-SEM-OP-0005",
        "TC-SEM-OP-0006", "TC-SEM-OP-0007", "TC-SEM-OP-0008",
        "TC-SEM-OP-0009", "TC-SEM-OP-0010", "TC-SEM-OP-0011",
        "TC-SEM-OP-0012", "TC-SEM-OP-0013", "TC-SEM-OP-0014",
        "TC-SEM-OP-0015", "TC-SEM-OP-0016", "TC-SEM-OP-0017",
        "TC-SEM-OP-0018", "TC-SEM-OP-0019", "TC-SEM-OP-0020",
        "TC-SEM-OP-0021", "TC-SEM-OP-0022", "TC-SEM-OP-0023",
        "TC-SEM-OP-0024", "TC-SEM-OP-0028", "TC-SEM-OP-0101",
        "TC-SEM-OP-0108", "TC-SEM-OP-0109", "TC-SEM-OP-0111",
        "TC-SEM-NAME-0111",
    ],
    "code:TC_CE_OPERAND_COUNT:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_OPERAND_COUNT:position": [
        "TC-SEM-OP-0002", "TC-SEM-OP-0004", "TC-SEM-OP-0013",
        "TC-SEM-OP-0018", "TC-SEM-OP-0019", "TC-SEM-FIELD-0009",
    ],
    "code:TC_CE_OPERAND_COUNT:stage": [
        "TC-SEM-OP-0001", "TC-SEM-OP-0004", "TC-SEM-OP-0013",
    ],
    "code:TC_CE_UNKNOWN_FIELD:trigger": [
        "TC-SEM-FIELD-0001", "TC-SEM-OP-0026",
    ],
    "code:TC_CE_UNKNOWN_FIELD:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_UNKNOWN_FIELD:position": ["TC-SEM-FIELD-0001"],
    "code:TC_CE_UNKNOWN_FIELD:stage": ["TC-SEM-FIELD-0001"],
    "code:TC_CE_MISSING_FIELD:trigger": [
        "TC-SEM-FIELD-0004", "TC-SEM-OP-0025",
    ],
    "code:TC_CE_MISSING_FIELD:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_MISSING_FIELD:position": ["TC-SEM-FIELD-0004"],
    "code:TC_CE_MISSING_FIELD:stage": ["TC-SEM-FIELD-0004"],
    "code:TC_CE_DUPLICATE_FIELD:trigger": [
        "TC-SEM-FIELD-0005", "TC-SEM-OP-0027",
    ],
    "code:TC_CE_DUPLICATE_FIELD:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_DUPLICATE_FIELD:position": ["TC-SEM-FIELD-0010"],
    "code:TC_CE_DUPLICATE_FIELD:stage": ["TC-SEM-FIELD-0010"],
    "code:TC_CE_STRUCT_SELF_VALUE:trigger": ["TC-SEM-NAME-0005"],
    "code:TC_CE_STRUCT_SELF_VALUE:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_STRUCT_SELF_VALUE:position": ["TC-SEM-NAME-0005"],
    "code:TC_CE_STRUCT_SELF_VALUE:stage": ["TC-SEM-NAME-0005"],
    "code:TC_CE_EMPTY_STRUCT:trigger": ["TC-SEM-NAME-0006"],
    "code:TC_CE_EMPTY_STRUCT:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_EMPTY_STRUCT:position": ["TC-SEM-NAME-0006"],
    "code:TC_CE_EMPTY_STRUCT:stage": ["TC-SEM-NAME-0006"],
    "code:TC_CE_LAYOUT_OVERFLOW:trigger": ["TC-SEM-LAYOUT-0001"],
    "code:TC_CE_LAYOUT_OVERFLOW:no_false_positive": ["TC-TOOL-SEM-0009"],
    "code:TC_CE_LAYOUT_OVERFLOW:position": ["TC-SEM-LAYOUT-0002"],
    "code:TC_CE_LAYOUT_OVERFLOW:stage": ["TC-SEM-LAYOUT-0001"],
    "code:TC_CE_LAYOUT_OVERFLOW:w32": ["TC-SEM-LAYOUT-0001"],
    "code:TC_CE_LAYOUT_OVERFLOW:w64": ["TC-SEM-LAYOUT-0002"],
    "code:TC_CE_FIELD_ORDER:trigger": ["TC-SEM-FIELD-0007"],
    "code:TC_CE_FIELD_ORDER:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_FIELD_ORDER:position": ["TC-SEM-FIELD-0008"],
    "code:TC_CE_FIELD_ORDER:stage": ["TC-SEM-FIELD-0007"],
    "code:TC_CE_IMMUTABLE_FIELD:trigger": ["TC-SEM-FIELD-0003"],
    "code:TC_CE_IMMUTABLE_FIELD:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_IMMUTABLE_FIELD:position": ["TC-SEM-FIELD-0003"],
    "code:TC_CE_IMMUTABLE_FIELD:stage": ["TC-SEM-FIELD-0003"],
    "code:TC_CE_CONST_ASSIGNMENT:trigger": ["TC-SEM-ASSIGN-0001"],
    "code:TC_CE_CONST_ASSIGNMENT:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_CONST_ASSIGNMENT:position": ["TC-SEM-ASSIGN-0102"],
    "code:TC_CE_CONST_ASSIGNMENT:stage": ["TC-SEM-ASSIGN-0001"],
    "code:TC_CE_PARAMETER_ASSIGNMENT:trigger": ["TC-SEM-ASSIGN-0101"],
    "code:TC_CE_PARAMETER_ASSIGNMENT:no_false_positive": ["TC-TOOL-SEM-0005"],
    "code:TC_CE_PARAMETER_ASSIGNMENT:position": ["TC-SEM-ASSIGN-0101"],
    "code:TC_CE_PARAMETER_ASSIGNMENT:stage": ["TC-SEM-ASSIGN-0101"],
    "code:TC_CE_ADDR_OF_CONST:trigger": ["TC-SEM-ASSIGN-0002"],
    "code:TC_CE_ADDR_OF_CONST:no_false_positive": ["TC-TOOL-SEM-0001"],
    "code:TC_CE_ADDR_OF_CONST:position": ["TC-SEM-ASSIGN-0002"],
    "code:TC_CE_ADDR_OF_CONST:stage": ["TC-SEM-ASSIGN-0002"],
    "code:TC_CE_BITCAST_WIDTH:trigger": ["TC-SEM-CONV-0001"],
    "code:TC_CE_BITCAST_WIDTH:no_false_positive": ["TC-TOOL-SEM-0002", "TC-TOOL-SEM-0015"],
    "code:TC_CE_BITCAST_WIDTH:position": ["TC-SEM-CONV-0001"],
    "code:TC_CE_BITCAST_WIDTH:stage": ["TC-SEM-CONV-0001"],
    "code:TC_CE_BITCAST_WIDTH:w32": ["TC-SEM-CONV-0003", "TC-SEM-CONV-0005"],
    "code:TC_CE_BITCAST_WIDTH:w64": ["TC-SEM-CONV-0001", "TC-SEM-CONV-0004"],
    "code:TC_CE_TRUNC_WIDTH:trigger": ["TC-SEM-CONV-0002"],
    "code:TC_CE_TRUNC_WIDTH:no_false_positive": ["TC-TOOL-SEM-0002"],
    "code:TC_CE_TRUNC_WIDTH:position": ["TC-SEM-CONV-0002"],
    "code:TC_CE_TRUNC_WIDTH:stage": ["TC-SEM-CONV-0002"],
    "code:TC_CE_TRUNC_WIDTH:w32": ["TC-SEM-CONV-0007"],
    "code:TC_CE_TRUNC_WIDTH:w64": ["TC-SEM-CONV-0006"],
    "code:TC_CE_VOID:trigger": ["TC-SEM-VOID-0001"],
    "code:TC_CE_VOID:no_false_positive": ["TC-TOOL-SEM-0005"],
    "code:TC_CE_VOID:position": ["TC-SEM-VOID-0002", "TC-SEM-VOID-0003"],
    "code:TC_CE_VOID:stage": ["TC-SEM-VOID-0001"],
    "code:TC_CE_CONST_FORM:trigger": [
        "TC-SEM-CONST-0001", "TC-SEM-CONST-0004", "TC-SEM-CONST-0005",
        "TC-SEM-CONST-0006", "TC-SEM-CONST-0007", "TC-SEM-CONST-0008",
        "TC-SEM-CONST-0009", "TC-SEM-CONST-0010", "TC-SEM-CONST-0011",
        "TC-SEM-CONST-0012", "TC-SEM-CONST-0013", "TC-SEM-CONST-0014",
        "TC-SEM-CONST-0015", "TC-SEM-CONST-0016", "TC-SEM-CONST-0017",
        "TC-SEM-CONST-0018", "TC-SEM-CONST-0019", "TC-SEM-CONST-0020",
        "TC-SEM-CONST-0021", "TC-SEM-CONST-0022", "TC-SEM-CONST-0023",
        "TC-SEM-CONST-0024", "TC-SEM-CONST-0025", "TC-SEM-CONST-0026",
    ],
    "code:TC_CE_CONST_FORM:no_false_positive": [
        "TC-TOOL-SEM-0001", "TC-TOOL-SEM-0142", "TC-TOOL-SEM-0143",
        "TC-TOOL-SEM-0144", "TC-TOOL-SEM-0145",
    ],
    "code:TC_CE_CONST_FORM:position": [
        "TC-SEM-CONST-0002", "TC-SEM-CONST-0003", "TC-SEM-CONST-0004",
        "TC-SEM-CONST-0006", "TC-SEM-CONST-0007", "TC-SEM-CONST-0008",
        "TC-SEM-CONST-0009", "TC-SEM-CONST-0010", "TC-SEM-CONST-0011",
        "TC-SEM-CONST-0012", "TC-SEM-CONST-0013", "TC-SEM-CONST-0014",
        "TC-SEM-CONST-0015", "TC-SEM-CONST-0016", "TC-SEM-CONST-0017",
        "TC-SEM-CONST-0018", "TC-SEM-CONST-0019", "TC-SEM-CONST-0020",
        "TC-SEM-CONST-0021", "TC-SEM-CONST-0022", "TC-SEM-CONST-0023",
        "TC-SEM-CONST-0024", "TC-SEM-CONST-0025", "TC-SEM-CONST-0026",
    ],
    "code:TC_CE_CONST_FORM:stage": [
        "TC-SEM-CONST-0001", "TC-SEM-CONST-0004",
    ],
    "first_diag_rule:06:combination_case": ["TC-SEM-LAYOUT-0001"],
    "first_diag_rule:08:combination_case": ["TC-SEM-CONV-0010", "TC-SEM-CONV-0011"],
    "design12:06:suite_mapping": ["TC-SEM-CONV-0010", "TC-TOOL-SEM-0009"],
    "design12:06:gate_case": ["TC-SEM-CONV-0001", "TC-SEM-LAYOUT-0001"],
    "design12:09:suite_mapping": ["TC-SEM-TYPE-0001"],
    "design12:09:gate_case": ["TC-IMP-0001", "TC-SEM-MODE-0001"],
    "flip32_64:01:w32": ["TC-UNIT-LAYOUT-0001@w32"],
    "flip32_64:01:w64": ["TC-UNIT-LAYOUT-0001@w64"],
    "flip32_64:02:w32": ["TC-UNIT-LAYOUT-0001@w32"],
    "flip32_64:02:w64": ["TC-UNIT-LAYOUT-0001@w64"],
    "flip32_64:03:w32": ["TC-UNIT-LAYOUT-0001@w32"],
    "flip32_64:03:w64": ["TC-UNIT-LAYOUT-0001@w64"],
    "flip32_64:04:w32": ["TC-TOOL-SEM-0013"],
    "flip32_64:04:w64": ["TC-TOOL-SEM-0014"],
    "flip32_64:05:w32": ["TC-TOOL-SEM-0015"],
    "flip32_64:05:w64": ["TC-TOOL-SEM-0002"],
    "flip32_64:06:w32": ["TC-SEM-CONV-0010"],
    "flip32_64:06:w64": ["TC-SEM-CONV-0001"],
    "flip32_64:07:w32": ["TC-SEM-CONV-0003"],
    "flip32_64:07:w64": ["TC-SEM-CONV-0011"],
    "flip32_64:08:w32": ["TC-SEM-CONV-0012"],
    "flip32_64:08:w64": ["TC-SEM-CONV-0004"],
    "flip32_64:09:w32": ["TC-SEM-CONV-0005"],
    "flip32_64:09:w64": ["TC-SEM-CONV-0013"],
    "flip32_64:10:w32": ["TC-SEM-CONV-0014"],
    "flip32_64:10:w64": ["TC-SEM-CONV-0006"],
    "flip32_64:11:w32": ["TC-SEM-CONV-0007"],
    "flip32_64:11:w64": ["TC-SEM-CONV-0015"],
    "flip32_64:12:w32": ["TC-SEM-CONV-0016"],
    "flip32_64:12:w64": ["TC-SEM-CONV-0008"],
    "flip32_64:13:w32": ["TC-SEM-CONV-0009"],
    "flip32_64:13:w64": ["TC-SEM-CONV-0017"],
    "flip32_64:14:w32": ["TC-SEM-LIT-0100", "TC-SEM-LIT-0101"],
    "flip32_64:14:w64": ["TC-TOOL-SEM-0016"],
    "flip32_64:15:w32": ["TC-SEM-LAYOUT-0001"],
    "flip32_64:15:w64": ["TC-SEM-LAYOUT-0002"],
    "flip32_64:16:w32": [
        "TC-SEM-ANN-0001", "TC-SEM-ANN-0002", "TC-TOOL-SEM-0120",
    ],
    "flip32_64:16:w64": [
        "TC-TOOL-SEM-0017", "TC-TOOL-SEM-0018", "TC-TOOL-SEM-0121",
    ],
    "flip32_64:17:w32": ["TC-TOOL-SEM-0011"],
    "flip32_64:17:w64": ["TC-TOOL-SEM-0012"],
}

# S4 CFG/DI/call/safety/annotation rows.
S4_COVERED_ROWS = {
    "chapter:§5:positive": ["TC-TOOL-SEM-0101", "TC-TOOL-SEM-0102"],
    "chapter:§5:negative": [
        "TC-SEM-DI-0001", "TC-SEM-DI-0002", "TC-SEM-DI-0003",
    ],
    "chapter:§6:positive": ["TC-TOOL-SEM-0106", "TC-TOOL-SEM-0112"],
    "chapter:§6:negative": ["TC-SEM-CFG-0010", "TC-SEM-CALL-0112"],
    "chapter:§7:positive": ["TC-TOOL-SEM-0104", "TC-TOOL-SEM-0105"],
    "chapter:§7:negative": [
        "TC-SEM-CFG-0001", "TC-SEM-CFG-0005", "TC-SEM-CFG-0006",
        "TC-SEM-CFG-0008", "TC-SEM-CFG-0009", "TC-SEM-CFG-0022",
        "TC-SEM-CFG-0023", "TC-SEM-CFG-0024", "TC-SEM-CFG-0025",
        "TC-SEM-CFG-0026", "TC-SEM-CFG-0027", "TC-SEM-CFG-0028",
        "TC-SEM-CFG-0029",
    ],
    "chapter:§8:positive": ["TC-TOOL-SEM-0106", "TC-TOOL-SEM-0109"],
    "chapter:§8:negative": [
        "TC-SEM-CFG-0012", "TC-SEM-CFG-0013", "TC-SEM-CALL-0101",
        "TC-SEM-CALL-0105", "TC-SEM-CALL-0108", "TC-SEM-CALL-0113",
        "TC-SEM-CALL-0120", "TC-SEM-CALL-0125", "TC-SEM-ANN-0201",
        "TC-SEM-ANN-0234", "TC-SEM-ANN-0235", "TC-SEM-ANN-0236",
        "TC-SEM-ANN-0237", "TC-SEM-ANN-0238", "TC-SEM-ANN-0239",
        "TC-SEM-ANN-0240", "TC-SEM-ANN-0241",
    ],
    "chapter:§9:positive": ["TC-TOOL-SEM-0107", "TC-TOOL-SEM-0119"],
    "chapter:§9:negative": [
        "TC-SEM-SAFE-0101", "TC-SEM-SAFE-0109", "TC-SEM-SAFE-0110",
        "TC-SEM-SAFE-0115", "TC-SEM-SAFE-0116", "TC-SEM-CALL-0108",
        "TC-SEM-CALL-0109", "TC-SEM-CALL-0114", "TC-SEM-CALL-0121",
    ],
    "chapter:§10:positive": ["TC-TOOL-SEM-0109", "TC-TOOL-SEM-0110"],
    "chapter:§10:negative": ["TC-SEM-ANN-0206", "TC-SEM-ANN-0224"],
    "chapter:§12:negative": [
        "TC-SEM-CFG-0016", "TC-SEM-CFG-0017",
    ],
    # §5.4/§5.7.1 definite initialization.
    "code:TC_CE_UNINITIALIZED:trigger": [
        "TC-SEM-DI-0001", "TC-SEM-DI-0004", "TC-SEM-DI-0005",
        "TC-SEM-DI-0006", "TC-SEM-DI-0009", "TC-SEM-CFG-0024",
        "TC-SEM-CFG-0029",
    ],
    "code:TC_CE_UNINITIALIZED:no_false_positive": [
        "TC-TOOL-SEM-0101", "TC-TOOL-SEM-0102", "TC-TOOL-SEM-0103",
        "TC-TOOL-SEM-0115", "TC-TOOL-SEM-0117", "TC-TOOL-SEM-0123",
    ],
    "code:TC_CE_UNINITIALIZED:position": [
        "TC-SEM-DI-0001", "TC-SEM-DI-0007", "TC-SEM-DI-0008",
        "TC-SEM-CFG-0024", "TC-SEM-CFG-0029",
    ],
    "code:TC_CE_UNINITIALIZED:stage": ["TC-SEM-DI-0001"],
    # §7.2 labels and control.
    "code:TC_CE_DUPLICATE_LABEL:trigger": ["TC-SEM-CFG-0005", "TC-SEM-CFG-0025"],
    "code:TC_CE_DUPLICATE_LABEL:no_false_positive": ["TC-TOOL-SEM-0105"],
    "code:TC_CE_DUPLICATE_LABEL:position": ["TC-SEM-CFG-0005", "TC-SEM-CFG-0025"],
    "code:TC_CE_DUPLICATE_LABEL:stage": ["TC-SEM-CFG-0005"],
    "code:TC_CE_LABEL_NOT_FOUND:trigger": [
        "TC-SEM-CFG-0006", "TC-SEM-CFG-0007", "TC-SEM-CFG-0020",
        "TC-SEM-CFG-0028",
    ],
    "code:TC_CE_LABEL_NOT_FOUND:no_false_positive": [
        "TC-TOOL-SEM-0105", "TC-TOOL-SEM-0118",
    ],
    "code:TC_CE_LABEL_NOT_FOUND:position": [
        "TC-SEM-CFG-0006", "TC-SEM-CFG-0028",
    ],
    "code:TC_CE_LABEL_NOT_FOUND:stage": ["TC-SEM-CFG-0006"],
    "code:TC_CE_CONDITION_TYPE:trigger": ["TC-SEM-CFG-0008"],
    "code:TC_CE_CONDITION_TYPE:no_false_positive": [
        "TC-TOOL-SEM-0112", "TC-TOOL-SEM-0101",
    ],
    "code:TC_CE_CONDITION_TYPE:position": ["TC-SEM-CFG-0008"],
    "code:TC_CE_CONDITION_TYPE:stage": ["TC-SEM-CFG-0008"],
    "code:TC_CE_SWITCH_TYPE:trigger": ["TC-SEM-CFG-0009"],
    "code:TC_CE_SWITCH_TYPE:no_false_positive": ["TC-TOOL-SEM-0112"],
    "code:TC_CE_SWITCH_TYPE:position": ["TC-SEM-CFG-0009"],
    "code:TC_CE_SWITCH_TYPE:stage": ["TC-SEM-CFG-0009"],
    # §7.7.3/§8.3 returns.
    "code:TC_CE_RETURN_OUTSIDE_FUNCTION:trigger": ["TC-SEM-CFG-0011"],
    "code:TC_CE_RETURN_OUTSIDE_FUNCTION:no_false_positive": [
        "TC-TOOL-SEM-0106", "TC-TOOL-SEM-0104",
    ],
    "code:TC_CE_RETURN_OUTSIDE_FUNCTION:position": ["TC-SEM-CFG-0011"],
    "code:TC_CE_RETURN_OUTSIDE_FUNCTION:stage": ["TC-SEM-CFG-0011"],
    "code:TC_CE_RETURN_TYPE:trigger": ["TC-SEM-CFG-0012"],
    "code:TC_CE_RETURN_TYPE:no_false_positive": [
        "TC-TOOL-SEM-0106", "TC-TOOL-SEM-0104",
    ],
    "code:TC_CE_RETURN_TYPE:position": ["TC-SEM-CFG-0012"],
    "code:TC_CE_RETURN_TYPE:stage": ["TC-SEM-CFG-0012"],
    "code:TC_CE_RETURN_FORM:trigger": [
        "TC-SEM-CFG-0013", "TC-SEM-CFG-0014",
    ],
    "code:TC_CE_RETURN_FORM:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_RETURN_FORM:position": [
        "TC-SEM-CFG-0013", "TC-SEM-CFG-0014",
    ],
    "code:TC_CE_RETURN_FORM:stage": ["TC-SEM-CFG-0013"],
    "code:TC_CE_MISSING_RETURN:trigger": [
        "TC-SEM-CFG-0001", "TC-SEM-CFG-0002", "TC-SEM-CFG-0003",
        "TC-SEM-CFG-0004", "TC-SEM-CFG-0019", "TC-SEM-CFG-0022",
        "TC-SEM-CFG-0023", "TC-SEM-CFG-0027",
    ],
    "code:TC_CE_MISSING_RETURN:no_false_positive": ["TC-TOOL-SEM-0104"],
    "code:TC_CE_MISSING_RETURN:position": [
        "TC-SEM-CFG-0001", "TC-SEM-CFG-0002", "TC-SEM-CFG-0022",
        "TC-SEM-CFG-0023", "TC-SEM-CFG-0027",
    ],
    "code:TC_CE_MISSING_RETURN:stage": ["TC-SEM-CFG-0001"],
    "code:TC_CE_OPERAND_COUNT:trigger": [
        "TC-SEM-CFG-0010", "TC-SEM-CFG-0018", "TC-SEM-CFG-0026",
    ],
    "code:TC_CE_OPERAND_COUNT:no_false_positive": ["TC-TOOL-SEM-0112"],
    "code:TC_CE_OPERAND_COUNT:position": [
        "TC-SEM-CFG-0010", "TC-SEM-CFG-0018", "TC-SEM-CFG-0026",
    ],
    "code:TC_CE_OPERAND_COUNT:stage": ["TC-SEM-CFG-0010"],
    "code:TC_CE_TYPE_MISMATCH:trigger": [
        "TC-SEM-CALL-0117", "TC-SEM-CALL-0120", "TC-SEM-CALL-0122",
        "TC-SEM-CFG-0015",
    ],
    "code:TC_CE_LITERAL_TYPE:trigger": ["TC-SEM-CALL-0127"],
    "code:TC_CE_LITERAL_RANGE:trigger": ["TC-SEM-CFG-0021"],
    "code:TC_CE_LITERAL_RANGE:no_false_positive": ["TC-TOOL-SEM-0122"],
    "code:TC_CE_LITERAL_RANGE:position": ["TC-SEM-CFG-0021"],
    "code:TC_CE_LITERAL_RANGE:stage": ["TC-SEM-CFG-0021"],
    "code:TC_CE_CONST_ASSIGNMENT:trigger": ["TC-SEM-CALL-0125"],
    "code:TC_CE_CONST_ASSIGNMENT:position": ["TC-SEM-CALL-0125"],
    "code:TC_CE_PRIVATE_ACCESS:trigger": ["TC-SEM-ANN-0233"],
    "code:TC_CE_PRIVATE_ACCESS:position": ["TC-SEM-ANN-0233"],
    "code:TC_CE_MODE:trigger": [
        "TC-SEM-MODE-0104", "TC-SEM-MODE-0105",
    ],
    "code:TC_CE_MODE:position": [
        "TC-SEM-MODE-0104", "TC-SEM-MODE-0105",
    ],
    "code:TC_CE_MODE:stage": ["TC-SEM-MODE-0104"],
    # §8.8.2/§12.2 #10 call checks.
    "code:TC_CE_ARGUMENT_COUNT:trigger": ["TC-SEM-CALL-0104"],
    "code:TC_CE_ARGUMENT_COUNT:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_ARGUMENT_COUNT:position": ["TC-SEM-CALL-0104"],
    "code:TC_CE_ARGUMENT_COUNT:stage": ["TC-SEM-CALL-0104"],
    "code:TC_CE_ARGUMENT_ORDER:trigger": [
        "TC-SEM-CALL-0105", "TC-SEM-CALL-0106",
    ],
    "code:TC_CE_ARGUMENT_ORDER:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_ARGUMENT_ORDER:position": [
        "TC-SEM-CALL-0105", "TC-SEM-CALL-0106",
    ],
    "code:TC_CE_ARGUMENT_ORDER:stage": ["TC-SEM-CALL-0105"],
    "code:TC_CE_UNKNOWN_ARGUMENT:trigger": [
        "TC-SEM-CALL-0102", "TC-SEM-CALL-0115",
    ],
    "code:TC_CE_UNKNOWN_ARGUMENT:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_UNKNOWN_ARGUMENT:position": ["TC-SEM-CALL-0102"],
    "code:TC_CE_UNKNOWN_ARGUMENT:stage": ["TC-SEM-CALL-0102"],
    "code:TC_CE_DUPLICATE_ARGUMENT:trigger": ["TC-SEM-CALL-0101"],
    "code:TC_CE_DUPLICATE_ARGUMENT:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_DUPLICATE_ARGUMENT:position": ["TC-SEM-CALL-0101"],
    "code:TC_CE_DUPLICATE_ARGUMENT:stage": ["TC-SEM-CALL-0101"],
    "code:TC_CE_MISSING_ARGUMENT:trigger": ["TC-SEM-CALL-0103"],
    "code:TC_CE_MISSING_ARGUMENT:no_false_positive": ["TC-TOOL-SEM-0106"],
    "code:TC_CE_MISSING_ARGUMENT:position": ["TC-SEM-CALL-0103"],
    "code:TC_CE_MISSING_ARGUMENT:stage": ["TC-SEM-CALL-0103"],
    "code:TC_CE_VOID:trigger": ["TC-SEM-CALL-0112", "TC-SEM-CALL-0123"],
    "code:TC_CE_VOID:position": ["TC-SEM-CALL-0112", "TC-SEM-CALL-0123"],
    "code:TC_CE_VOID:stage": ["TC-SEM-CALL-0112", "TC-SEM-CALL-0123"],
    # §9 safety.
    "code:TC_CE_UNSAFE_OPERATION:trigger": [
        "TC-SEM-SAFE-0101", "TC-SEM-SAFE-0102", "TC-SEM-SAFE-0103",
        "TC-SEM-SAFE-0104", "TC-SEM-SAFE-0105", "TC-SEM-SAFE-0106",
        "TC-SEM-SAFE-0107", "TC-SEM-SAFE-0108", "TC-SEM-SAFE-0112",
        "TC-SEM-SAFE-0113", "TC-SEM-SAFE-0115", "TC-SEM-SAFE-0117",
        "TC-SEM-CALL-0116",
    ],
    "code:TC_CE_UNSAFE_OPERATION:no_false_positive": [
        "TC-TOOL-SEM-0107", "TC-TOOL-SEM-0108", "TC-TOOL-SEM-0113",
        "TC-TOOL-SEM-0119",
    ],
    "code:TC_CE_UNSAFE_OPERATION:position": ["TC-SEM-SAFE-0101"],
    "code:TC_CE_UNSAFE_OPERATION:stage": ["TC-SEM-SAFE-0101"],
    "code:TC_CE_UNSAFE_CALL:trigger": ["TC-SEM-CALL-0108"],
    "code:TC_CE_UNSAFE_CALL:no_false_positive": ["TC-TOOL-SEM-0107"],
    "code:TC_CE_UNSAFE_CALL:position": ["TC-SEM-CALL-0108"],
    "code:TC_CE_UNSAFE_CALL:stage": ["TC-SEM-CALL-0108"],
    "code:TC_CE_FAST_CALL:trigger": [
        "TC-SEM-CALL-0109", "TC-SEM-CALL-0110", "TC-SEM-CALL-0107",
        "TC-SEM-CALL-0114", "TC-SEM-CALL-0124", "TC-SEM-CALL-0126",
    ],
    "code:TC_CE_FAST_CALL:no_false_positive": ["TC-TOOL-SEM-0107"],
    "code:TC_CE_FAST_CALL:position": ["TC-SEM-CALL-0109"],
    "code:TC_CE_FAST_CALL:stage": ["TC-SEM-CALL-0109"],
    "code:TC_CE_SAFE_VIOLATION:trigger": [
        "TC-SEM-SAFE-0109", "TC-SEM-SAFE-0110", "TC-SEM-SAFE-0111",
        "TC-SEM-CALL-0111", "TC-SEM-SAFE-0114", "TC-SEM-SAFE-0116",
        "TC-SEM-CALL-0121",
    ],
    "code:TC_CE_SAFE_VIOLATION:no_false_positive": [
        "TC-TOOL-SEM-0109", "TC-TOOL-SEM-0114",
    ],
    "code:TC_CE_SAFE_VIOLATION:position": [
        "TC-SEM-SAFE-0109", "TC-SEM-SAFE-0110",
    ],
    "code:TC_CE_SAFE_VIOLATION:stage": ["TC-SEM-SAFE-0109"],
    # §2.6/§8.6 annotations.
    "code:TC_CE_ANNOTATION:trigger": [
        "TC-SEM-ANN-0201", "TC-SEM-ANN-0202", "TC-SEM-ANN-0204",
        "TC-SEM-ANN-0205", "TC-SEM-ANN-0208", "TC-SEM-ANN-0211",
        "TC-SEM-ANN-0214", "TC-SEM-ANN-0222", "TC-SEM-ANN-0223",
        "TC-SEM-ANN-0229", "TC-SEM-ANN-0234", "TC-SEM-ANN-0235",
        "TC-SEM-ANN-0236", "TC-SEM-ANN-0237", "TC-SEM-ANN-0238",
        "TC-SEM-ANN-0239", "TC-SEM-ANN-0240", "TC-SEM-ANN-0241",
        "TC-SEM-ANN-0242", "TC-SEM-ANN-0243", "TC-SEM-ANN-0244",
        "TC-SEM-ANN-0245", "TC-SEM-ANN-0246", "TC-SEM-ANN-0247",
        "TC-SEM-ANN-0248", "TC-SEM-ANN-0249", "TC-SEM-ANN-0250",
        "TC-SEM-ANN-0251", "TC-SEM-ANN-0252", "TC-SEM-ANN-0253",
        "TC-SEM-ANN-0254", "TC-SEM-ANN-0255", "TC-SEM-ANN-0256",
        "TC-SEM-ANN-0257", "TC-SEM-ANN-0258",
    ],
    "code:TC_CE_ANNOTATION:no_false_positive": [
        "TC-TOOL-SEM-0109", "TC-TOOL-SEM-0111", "TC-TOOL-SEM-0124",
        "TC-TOOL-SEM-0126", "TC-TOOL-SEM-0129", "TC-TOOL-SEM-0130",
        "TC-TOOL-SEM-0131", "TC-TOOL-SEM-0132", "TC-TOOL-SEM-0133",
        "TC-TOOL-SEM-0134", "TC-TOOL-SEM-0135", "TC-TOOL-SEM-0137",
        "TC-TOOL-SEM-0138", "TC-TOOL-SEM-0139", "TC-TOOL-SEM-0141",
    ],
    "code:TC_CE_ANNOTATION:position": [
        "TC-SEM-ANN-0201", "TC-SEM-ANN-0203", "TC-SEM-ANN-0211",
        "TC-SEM-ANN-0214", "TC-SEM-ANN-0222", "TC-SEM-ANN-0234",
        "TC-SEM-ANN-0235", "TC-SEM-ANN-0236", "TC-SEM-ANN-0237",
        "TC-SEM-ANN-0238", "TC-SEM-ANN-0239", "TC-SEM-ANN-0240",
        "TC-SEM-ANN-0241", "TC-SEM-ANN-0242", "TC-SEM-ANN-0243",
        "TC-SEM-ANN-0244", "TC-SEM-ANN-0245", "TC-SEM-ANN-0246",
        "TC-SEM-ANN-0247", "TC-SEM-ANN-0248", "TC-SEM-ANN-0249",
        "TC-SEM-ANN-0250", "TC-SEM-ANN-0251",
    ],
    "code:TC_CE_ANNOTATION:stage": ["TC-SEM-ANN-0201"],
    # §12.2 ordering rules exercised by S4 combinations.
    "first_diag_rule:02:combination_case": ["TC-SEM-CFG-0004"],
    "first_diag_rule:03:combination_case": [
        "TC-SEM-CFG-0003", "TC-SEM-CFG-0022", "TC-SEM-CFG-0023",
        "TC-SEM-CFG-0024", "TC-SEM-CFG-0029",
    ],
    "first_diag_rule:07:combination_case": [
        "TC-SEM-CFG-0003", "TC-SEM-DI-0002", "TC-SEM-CFG-0024",
    ],
    "first_diag_rule:09:combination_case": [
        "TC-SEM-CFG-0016", "TC-SEM-CFG-0017",
    ],
    "first_diag_rule:10:combination_case": [
        "TC-SEM-CALL-0101", "TC-SEM-CALL-0105", "TC-SEM-CALL-0106",
        "TC-SEM-CALL-0107",
    ],
    "same_site_priority:SAFE_VIOLATION_OVER_UNSAFE:priority": [
        "TC-SEM-SAFE-0114", "TC-SEM-CALL-0111",
    ],
    "same_site_priority:CALL_ARGUMENT_ORDER:priority": [
        "TC-SEM-CALL-0101", "TC-SEM-CALL-0105",
    ],
    "same_site_priority:CALL_SAFETY_FIRST:priority": [
        "TC-SEM-CALL-0107", "TC-SEM-CALL-0124", "TC-SEM-CALL-0126",
    ],
    # §8.6 annotation matrix slots.
    "annotation:@unsafe:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0231", "TC-SEM-ANN-0204"],
    "annotation:@safe:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0230", "TC-SEM-ANN-0204"],
    "annotation:@fast:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0204", "TC-TOOL-SEM-0110"],
    "annotation:@fast_math:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0205"],
    "annotation:@pure:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0206", "TC-TOOL-SEM-0110"],
    "annotation:@readonly:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0224", "TC-TOOL-SEM-0110"],
    "annotation:@nounwind:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0232", "TC-TOOL-SEM-0110"],
    "annotation:@willreturn:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0226"],
    "annotation:@no_overflow:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0227", "TC-TOOL-SEM-0110"],
    "annotation:@inline:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0207"],
    "annotation:@noinline:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0207"],
    "annotation:@nonnull:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0208", "TC-SEM-ANN-0209", "TC-TOOL-SEM-0110"],
    "annotation:@no_alias:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0228", "TC-TOOL-SEM-0110"],
    "annotation:@align:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0210", "TC-SEM-ANN-0211", "TC-SEM-ANN-0212", "TC-SEM-ANN-0213", "TC-SEM-ANN-0225", "TC-TOOL-SEM-0110", "TC-TOOL-SEM-0116", "TC-TOOL-SEM-0120", "TC-TOOL-SEM-0121", "TC-SEM-ANN-0234", "TC-SEM-ANN-0235", "TC-SEM-ANN-0236", "TC-SEM-ANN-0238", "TC-SEM-ANN-0239", "TC-SEM-ANN-0240", "TC-SEM-ANN-0242", "TC-SEM-ANN-0243", "TC-SEM-ANN-0244", "TC-SEM-ANN-0245", "TC-SEM-ANN-0246", "TC-SEM-ANN-0248", "TC-TOOL-SEM-0124", "TC-TOOL-SEM-0125", "TC-TOOL-SEM-0127", "TC-TOOL-SEM-0128", "TC-TOOL-SEM-0130", "TC-TOOL-SEM-0131", "TC-TOOL-SEM-0132", "TC-TOOL-SEM-0133", "TC-SEM-ANN-0249", "TC-SEM-ANN-0250", "TC-TOOL-SEM-0134", "TC-TOOL-SEM-0135", "TC-TOOL-SEM-0137", "TC-TOOL-SEM-0138", "TC-TOOL-SEM-0139", "TC-SEM-ANN-0252", "TC-SEM-ANN-0254", "TC-SEM-ANN-0255", "TC-SEM-ANN-0256", "TC-SEM-ANN-0257", "TC-SEM-ANN-0258", "TC-TOOL-SEM-0140", "TC-TOOL-SEM-0141"],
    "annotation:@range:coverage": ["TC-TOOL-SEM-0109", "TC-SEM-ANN-0215", "TC-SEM-ANN-0214", "TC-SEM-ANN-0216", "TC-SEM-ANN-0217", "TC-SEM-ANN-0218", "TC-SEM-ANN-0219", "TC-SEM-ANN-0220", "TC-SEM-ANN-0221", "TC-TOOL-SEM-0110", "TC-TOOL-SEM-0116", "TC-SEM-ANN-0233", "TC-SEM-ANN-0237", "TC-SEM-ANN-0241", "TC-SEM-ANN-0247", "TC-SEM-ANN-0248", "TC-TOOL-SEM-0124", "TC-TOOL-SEM-0125", "TC-TOOL-SEM-0127", "TC-TOOL-SEM-0128", "TC-TOOL-SEM-0133"],
    "annotation:@align:const_path_value": ["TC-SEM-ANN-0234", "TC-SEM-ANN-0235", "TC-SEM-ANN-0236", "TC-SEM-ANN-0238", "TC-SEM-ANN-0239", "TC-SEM-ANN-0240", "TC-SEM-ANN-0242", "TC-SEM-ANN-0243", "TC-SEM-ANN-0244", "TC-SEM-ANN-0245", "TC-SEM-ANN-0246", "TC-SEM-ANN-0248", "TC-TOOL-SEM-0116", "TC-TOOL-SEM-0124", "TC-TOOL-SEM-0125", "TC-TOOL-SEM-0126", "TC-TOOL-SEM-0127", "TC-TOOL-SEM-0128", "TC-TOOL-SEM-0129", "TC-TOOL-SEM-0130", "TC-TOOL-SEM-0131", "TC-TOOL-SEM-0132", "TC-TOOL-SEM-0133", "TC-UNIT-ANN-0001", "TC-SEM-ANN-0249", "TC-SEM-ANN-0250", "TC-TOOL-SEM-0134", "TC-TOOL-SEM-0135", "TC-TOOL-SEM-0137", "TC-TOOL-SEM-0138", "TC-TOOL-SEM-0139", "TC-SEM-ANN-0252", "TC-SEM-ANN-0253", "TC-SEM-ANN-0254", "TC-SEM-ANN-0255", "TC-SEM-ANN-0256", "TC-SEM-ANN-0257", "TC-SEM-ANN-0258", "TC-TOOL-SEM-0140", "TC-TOOL-SEM-0141"],
    "annotation:@range:const_path_value": ["TC-SEM-ANN-0237", "TC-SEM-ANN-0241", "TC-SEM-ANN-0247", "TC-SEM-ANN-0248", "TC-TOOL-SEM-0116", "TC-TOOL-SEM-0124", "TC-TOOL-SEM-0125", "TC-TOOL-SEM-0127", "TC-TOOL-SEM-0128", "TC-TOOL-SEM-0133", "TC-UNIT-ANN-0001"],
    "annotation:@padding:coverage": ["TC-TOOL-SEM-0019", "TC-SEM-ANN-0229", "TC-SEM-CONST-0001", "TC-SEM-CONST-0002", "TC-SEM-CONST-0003"],
    "design12:09:suite_mapping": [
        "TC-SEM-CALL-0101", "TC-SEM-CFG-0016",
    ],
    "design12:09:gate_case": [
        "TC-SEM-CALL-0107", "TC-SEM-CFG-0004",
    ],
}

# S5 CT / IR finalization rows.  Case ids are real artefacts: S5 conformance
# cases live under tests/conformance/static/{ct,accept_s5} and the L1 suites
# are tests/unit/ct_test.c and tests/unit/ir_test.c (case ids documented in
# tests/unit/README.md).
S5_COVERED_ROWS = {
    # §3.10 const_expr / §4.10 cross-module demand order / §6.3-§6.6 values.
    "chapter:§3:positive": [
        "TC-TOOL-CT-CONV-0001", "TC-TOOL-CT-LIT-0001",
        "TC-TOOL-CT-OPS-0001", "TC-TOOL-CT-XMOD-0001",
    ],
    "chapter:§3:negative": [
        "TC-CT-CAST-0001", "TC-CT-FORM-0009", "TC-CT-WIDTH-0003",
    ],
    "chapter:§4:positive": [
        "TC-TOOL-CT-REF-0001", "TC-TOOL-CT-CHAIN-0001",
        "TC-TOOL-CT-STATIC-0001",
    ],
    "chapter:§4:negative": [
        "TC-CT-ORDER-0003", "TC-CT-ORDER-0006", "TC-CT-ORDER-0007",
        "TC-CT-STATIC-0001",
    ],
    "chapter:§12:negative": [
        "TC-CT-ORDER-0001", "TC-CT-ORDER-0002", "TC-CT-STAGE-0001",
    ],
    # Six CT codes: trigger, no false positive, position, stage, word size.
    "code:TC_CE_CONST_OVERFLOW:trigger": [
        "TC-CT-OVERFLOW-0001", "TC-CT-OVERFLOW-0002",
        "TC-CT-OVERFLOW-0003", "TC-CT-OVERFLOW-0004",
        "TC-CT-ORDER-0005", "TC-CT-STATIC-0001",
    ],
    "code:TC_CE_CONST_OVERFLOW:no_false_positive": [
        "TC-TOOL-CT-NOFP-0001", "TC-TOOL-CT-WCAST-0001",
    ],
    "code:TC_CE_CONST_OVERFLOW:position": [
        "TC-CT-OVERFLOW-0001", "TC-CT-OVERFLOW-0004",
        "TC-CT-STATIC-0001",
    ],
    "code:TC_CE_CONST_OVERFLOW:stage": ["TC-CT-OVERFLOW-0001"],
    "code:TC_CE_CONST_OVERFLOW:w32": ["TC-CT-OVERFLOW-0004"],
    "code:TC_CE_CONST_OVERFLOW:w64": ["TC-CT-ORDER-0005"],
    "code:TC_CE_CONST_DIV_ZERO:trigger": [
        "TC-CT-DIVZERO-0001", "TC-CT-DIVZERO-0002",
        "TC-CT-DIVZERO-0003", "TC-CT-ORDER-0003",
        "TC-CT-ORDER-0004", "TC-CT-ORDER-0006", "TC-CT-ORDER-0007",
    ],
    "code:TC_CE_CONST_DIV_ZERO:no_false_positive": ["TC-TOOL-CT-NOFP-0002"],
    "code:TC_CE_CONST_DIV_ZERO:position": [
        "TC-CT-DIVZERO-0003", "TC-CT-ORDER-0006",
    ],
    "code:TC_CE_CONST_DIV_ZERO:stage": ["TC-CT-DIVZERO-0001"],
    "code:TC_CE_CONST_SHIFT:trigger": [
        "TC-CT-SHIFT-0001", "TC-CT-SHIFT-0002",
    ],
    "code:TC_CE_CONST_SHIFT:no_false_positive": ["TC-TOOL-CT-NOFP-0003"],
    "code:TC_CE_CONST_SHIFT:position": ["TC-CT-SHIFT-0002"],
    "code:TC_CE_CONST_SHIFT:stage": ["TC-CT-SHIFT-0001"],
    "code:TC_CE_CONST_FLOAT_INVALID:trigger": [
        "TC-CT-FLOATINVALID-0001", "TC-CT-FLOATINVALID-0002",
    ],
    "code:TC_CE_CONST_FLOAT_INVALID:no_false_positive": [
        "TC-TOOL-CT-NOFP-0004",
    ],
    "code:TC_CE_CONST_FLOAT_INVALID:position": ["TC-CT-FLOATINVALID-0001"],
    "code:TC_CE_CONST_FLOAT_INVALID:stage": ["TC-CT-FLOATINVALID-0001"],
    "code:TC_CE_CONST_FLOAT_UNDERFLOW:trigger": [
        "TC-CT-FLOATUNDERFLOW-0001", "TC-CT-FLOATUNDERFLOW-0002",
    ],
    "code:TC_CE_CONST_FLOAT_UNDERFLOW:no_false_positive": [
        "TC-TOOL-CT-NOFP-0005",
    ],
    "code:TC_CE_CONST_FLOAT_UNDERFLOW:position": [
        "TC-CT-FLOATUNDERFLOW-0002",
    ],
    "code:TC_CE_CONST_FLOAT_UNDERFLOW:stage": ["TC-CT-FLOATUNDERFLOW-0001"],
    "code:TC_CE_CONST_CAST:trigger": [
        "TC-CT-CAST-0001", "TC-CT-CAST-0002", "TC-CT-CAST-0003",
        "TC-CT-CAST-0004", "TC-CT-CAST-0005",
    ],
    "code:TC_CE_CONST_CAST:no_false_positive": [
        "TC-TOOL-CT-NOFP-0006", "TC-TOOL-CT-WCAST-0001",
        "TC-TOOL-CT-WCASTVICE-0001",
    ],
    "code:TC_CE_CONST_CAST:position": ["TC-CT-CAST-0002"],
    "code:TC_CE_CONST_CAST:stage": ["TC-CT-CAST-0001"],
    "code:TC_CE_CONST_CAST:w32": ["TC-CT-CAST-0004"],
    "code:TC_CE_CONST_CAST:w64": ["TC-CT-CAST-0005"],
    # Design §12 items owned by IR finalization.
    "design12:11:suite_mapping": [
        "TC-UNIT-IR-0001", "TC-UNIT-IR-0002",
        "TC-UNIT-IR-0003", "TC-UNIT-IR-0004",
        "TC-UNIT-IR-0009", "TC-UNIT-IR-0010",
    ],
    "design12:11:gate_case": ["TC-UNIT-IR-0001", "TC-UNIT-IR-0005"],
    "design12:12:suite_mapping": [
        "TC-UNIT-CT-0001", "TC-CT-STATIC-0001",
        "TC-TOOL-CT-MAKE-0001", "TC-TOOL-CT-STATIC-0001",
    ],
    "design12:12:gate_case": [
        "TC-CT-STATIC-0001", "TC-UNIT-IR-0003", "TC-UNIT-CT-0001",
    ],
    "design12:13:suite_mapping": [
        "TC-UNIT-IR-0001", "TC-UNIT-IR-0004", "TC-UNIT-IR-0008",
        "TC-UNIT-MODULE-0001",
    ],
    "design12:13:gate_case": [
        "TC-UNIT-IR-0001", "TC-TOOL-CT-CHAIN-0001",
    ],
}

S6_COVERED_ROWS = {
    # §1 observable behavior, §11 I/O and §12 runtime diagnostics.
    "chapter:§1:positive": ["TC-RT-INT-0001", "TC-RT-INT-0003", "TC-RT-MEM-0001"],
    "chapter:§1:negative": ["TC-RT-INT-0004", "TC-RT-CTRL-0002"],
    "chapter:§11:positive": ["TC-RT-IO-0001", "TC-RT-IO-0002", "TC-RT-IO-0006"],
    "chapter:§11:negative": ["TC-RT-IO-0003", "TC-RT-IO-0004", "TC-RT-IO-0005"],
    "chapter:§12:positive": ["TC-RT-CTRL-0002", "TC-RT-MEM-0005", "TC-RT-INT-0001"],
    # Design §12 items owned by the reference VM and runtime.
    "design12:03:suite_mapping": [
        "TC-RT-GOLD-0001", "TC-RT-GOLD-0002", "TC-RT-GOLD-0101",
        "TC-RT-GOLD-0102",
    ],
    "design12:03:gate_case": ["TC-RT-GOLD-0001", "TC-RT-GOLD-0102"],
    "design12:05:suite_mapping": ["TC-UNIT-VM-0006"],
    "design12:05:gate_case": ["TC-UNIT-VM-0006"],
    "design12:08:suite_mapping": [
        "TC-RT-MEM-0001", "TC-RT-MEM-0002", "TC-RT-MEM-0004",
    ],
    "design12:08:gate_case": ["TC-RT-MEM-0004", "TC-RT-MEM-0011"],
    "design12:14:suite_mapping": [
        "TC-RT-DEV-0001", "TC-RT-IO-0002", "TC-UNIT-VM-0006",
    ],
    "design12:14:gate_case": [
        "TC-RT-DEV-0001", "TC-RT-IO-0003", "TC-RT-IO-0004",
    ],
    "design12:15:suite_mapping": ["TC-RT-OPT-0001"],
    "design12:15:gate_case": ["TC-RT-OPT-0001"],
    # Device policy matrix (design §8).
    "device_policy:TC_DEV_REJECT:trap_mapping": ["TC-RT-DEV-0001"],
    "device_policy:TC_DEV_READ:trap_mapping": ["TC-UNIT-VM-0006"],
    "device_policy:TC_DEV_READWRITE:trap_mapping": ["TC-UNIT-VM-0006"],
    "device_policy:TC_DEV_VOLATILE:trap_mapping": ["TC-UNIT-VM-0006"],
    # TcIoSrc states and short reads (design §7.2).
    "io_source_state:TC_IO_OK:path": ["TC-RT-IO-0002"],
    "io_source_state:TC_IO_EOF:path": ["TC-RT-IO-0004"],
    "io_source_state:TC_IO_ERROR:path": ["TC-UNIT-VM-0006"],
    "io_source_state:SHORT_READ:path": ["TC-UNIT-VM-0006"],
    # Exit policy branches (design §8, standard §13.1).
    "exit_strategy:normal_exit:branch": ["TC-RT-INT-0001"],
    "exit_strategy:trap_exit:branch": ["TC-RT-CTRL-0002"],
}

S7_COVERED_ROWS = {
    # Design §12 item 1 (VM/AOT differential) and item 7 (ABI/calls) are owned
    # by the S7 AArch64 subset.  Item 15 (zero pass) adds the S7 determinism
    # evidence to the VM-level case.
    "design12:01:suite_mapping": [
        "TC-DIFF-A64-0001", "TC-DIFF-A64-0002", "TC-DIFF-A64-0003",
        "TC-DIFF-A64-0004", "TC-DIFF-A64-0005", "TC-DIFF-A64-0006",
        "TC-DIFF-A64-0007", "TC-DIFF-A64-0008", "TC-DIFF-A64-0009",
        "TC-DIFF-A64-0010", "TC-DIFF-A64-0011", "TC-DIFF-A64-0012",
        "TC-DIFF-A64-0013", "TC-DIFF-A64-0014", "TC-DIFF-A64-0015",
        "TC-DIFF-A64-0016", "TC-DIFF-A64-0017", "TC-DIFF-A64-0018",
        "TC-DIFF-A64-0019", "TC-DIFF-A64-0020", "TC-DIFF-A64-0021",
        "TC-DIFF-A64-0022", "TC-DIFF-A64-0023", "TC-DIFF-A64-0024",
        "TC-DIFF-A64-0025", "TC-DIFF-A64-0026", "TC-DIFF-A64-0027",
        "TC-DIFF-A64-0028", "TC-DIFF-A64-0029", "TC-DIFF-A64-0030",
        "TC-DIFF-A64-0031", "TC-DIFF-A64-0032", "TC-DIFF-A64-0033",
        "TC-DIFF-A64-0035", "TC-DIFF-A64-0036", "TC-DIFF-A64-0037",
        "TC-DIFF-A64-0038", "TC-DIFF-A64-0039", "TC-DIFF-A64-0040",
        "TC-DIFF-A64-0041", "TC-DIFF-A64-0042", "TC-DIFF-A64-0043",
        "TC-DIFF-A64-0044", "TC-DIFF-A64-0045", "TC-DIFF-A64-0046",
        "TC-DIFF-A64-0047", "TC-DIFF-A64-0048", "TC-DIFF-A64-0049",
        "TC-DIFF-A64-0050", "TC-DIFF-A64-0051", "TC-DIFF-A64-0052",
        "TC-DIFF-A64-0053", "TC-DIFF-A64-0054", "TC-DIFF-A64-0055",
        "TC-DIFF-A64-0056", "TC-DIFF-A64-0057", "TC-DIFF-A64-0058",
    ],
    "design12:01:gate_case": ["TC-DIFF-A64-0001"],
    "design12:07:suite_mapping": [
        "TC-AOT-ABI-0001", "TC-AOT-ABI-0002", "TC-AOT-ABI-0003",
        "TC-AOT-ABI-0004", "TC-AOT-ABI-0005", "TC-AOT-ABI-0006",
        "TC-AOT-ABI-0007", "TC-AOT-ABI-0008", "TC-AOT-ABI-0009",
    ],
    "design12:07:gate_case": ["TC-AOT-ABI-0001"],
    "design12:15:suite_mapping": ["TC-DET-S7-0001"],
    "design12:15:gate_case": ["TC-DET-S7-0001"],
}

S8_COVERED_ROWS = {
    # Full-runtime differential, the §13.4 eight-item consistency suite, the
    # checked-build separation, the full ABI surface and the @fast_math
    # boundary.  Design §12 item 4 is the freestanding compile/symbol audit;
    # the full bare-metal platform instance remains S9 work.
    "design12:01:suite_mapping": ["TC-DIFF-S8-0001", "TC-DIFF-S8-0021"],
    "design12:01:gate_case": ["TC-DIFF-S8-0001"],
    "design12:04:suite_mapping": ["TC-ART-S8-0001"],
    "design12:04:gate_case": ["TC-ART-S8-0001"],
    "design12:02:suite_mapping": [
        "TC-CON-S8-0001", "TC-CON-S8-0002", "TC-CON-S8-0003",
        "TC-CON-S8-0004", "TC-CON-S8-0005", "TC-CON-S8-0006",
        "TC-CON-S8-0007", "TC-CON-S8-0008",
    ],
    "design12:02:gate_case": ["TC-CON-S8-0001", "TC-CON-S8-0005"],
    "design12:05:suite_mapping": [
        "TC-CHK-S8-0001", "TC-CHK-S8-0003", "TC-CHK-S8-0005",
        "TC-CHK-S8-0006", "TC-CHK-S8-0008", "TC-CHK-S8-0011",
    ],
    "design12:05:gate_case": ["TC-CHK-S8-0001", "TC-CHK-S8-0012"],
    "design12:07:suite_mapping": [
        "TC-ABI-S8-0001", "TC-ABI-S8-0002", "TC-ABI-S8-0004",
        "TC-ABI-S8-0005", "TC-ABI-S8-0006", "TC-ABI-S8-0008",
        "TC-ABI-S8-0010",
    ],
    "design12:07:gate_case": [
        "TC-ABI-S8-0001", "TC-ABI-S8-0006", "TC-ABI-S8-0010",
    ],
    "design12:08:suite_mapping": [
        "TC-DIFF-S8-0015", "TC-DIFF-S8-0016", "TC-DIFF-S8-0017",
        "TC-CHK-S8-0003",
    ],
    "design12:08:gate_case": ["TC-DIFF-S8-0015", "TC-DIFF-S8-0016"],
    "design12:10:suite_mapping": ["TC-DIFF-S8-0022"],
    "design12:10:gate_case": ["TC-DIFF-S8-0022"],
    "design12:12:suite_mapping": ["TC-DIFF-S8-0020", "TC-DIFF-S8-0021"],
    "design12:12:gate_case": ["TC-DIFF-S8-0020"],
    "design12:14:suite_mapping": ["TC-CHK-S8-0008", "TC-CHK-S8-0013"],
    "design12:14:gate_case": ["TC-CHK-S8-0008", "TC-CHK-S8-0013"],
    "design12:15:suite_mapping": ["TC-DET-S8-0001"],
    "design12:15:gate_case": ["TC-DET-S8-0001"],
}

_S6_OPCODE_CASES = {
    "abs": ("TC-RT-INT-0001", "TC-RT-INT-0008"),
    "add": ("TC-RT-INT-0001", "TC-RT-INT-0004"),
    "add.ieee": ("TC-RT-FP-0006", None),
    "add.wrap": ("TC-RT-INT-0002", None),
    "addr": ("TC-RT-MEM-0001", None),
    "and": ("TC-RT-INT-0001", None),
    "bitcast": ("TC-RT-CONV-0001", None),
    "br": ("TC-RT-CTRL-0001", None),
    "br_if": ("TC-RT-CTRL-0001", None),
    "call": ("TC-RT-CALL-0001", "TC-RT-CALL-0002"),
    "call_indirect": ("TC-RT-CALL-0003", "TC-RT-CALL-0004"),
    "cast": ("TC-RT-CONV-0001", "TC-RT-CONV-0002"),
    "copy": ("TC-RT-MEM-0002", None),
    "div": ("TC-RT-INT-0001", "TC-RT-INT-0005"),
    "div.ieee": ("TC-RT-FP-0006", None),
    "eq": ("TC-RT-INT-0001", None),
    "funcaddr": ("TC-RT-CALL-0003", None),
    "funceq": ("TC-RT-CALL-0003", None),
    "funcne": ("TC-RT-CALL-0003", None),
    "ge": ("TC-RT-MEM-0001", None),
    "gt": ("TC-RT-MEM-0001", None),
    "le": ("TC-RT-MEM-0001", None),
    "load": ("TC-RT-MEM-0001", "TC-RT-MEM-0005"),
    "load.volatile": ("TC-RT-MEM-0010", "TC-RT-MEM-0016"),
    "lt": ("TC-RT-INT-0001", None),
    "make": ("TC-RT-MEM-0002", None),
    "memmove": ("TC-RT-MEM-0004", "TC-RT-MEM-0007"),
    "mod": ("TC-RT-INT-0001", "TC-RT-INT-0011"),
    "mod.ieee": ("TC-RT-FP-0006", None),
    "mul": ("TC-RT-INT-0001", "TC-RT-INT-0012"),
    "mul.ieee": ("TC-RT-FP-0006", "TC-RT-FP-0006"),
    "mul.wrap": ("TC-RT-INT-0002", "TC-RT-INT-0002"),
    "ne": ("TC-RT-FP-0001", None),
    "neg": ("TC-RT-INT-0001", "TC-RT-INT-0007"),
    "neg.wrap": ("TC-RT-INT-0002", None),
    "not": ("TC-RT-INT-0001", None),
    "or": ("TC-RT-INT-0001", None),
    "ptradd": ("TC-RT-MEM-0001", "TC-RT-MEM-0012"),
    "ptrdiff": ("TC-RT-MEM-0001", "TC-RT-MEM-0011"),
    "ptreq": ("TC-RT-MEM-0001", None),
    "ptrge": ("TC-RT-MEM-0001", None),
    "ptrgt": ("TC-RT-MEM-0001", None),
    "ptrle": ("TC-RT-MEM-0001", None),
    "ptrlt": ("TC-RT-MEM-0001", None),
    "ptrne": ("TC-RT-MEM-0001", None),
    "ptrsub": ("TC-RT-MEM-0001", "TC-RT-MEM-0017"),
    "read": ("TC-RT-IO-0002", "TC-RT-IO-0003"),
    "return": ("TC-RT-CALL-0001", None),
    "shl": ("TC-RT-INT-0001", "TC-RT-INT-0009"),
    "shl.wrap": ("TC-RT-INT-0002", "TC-RT-INT-0013"),
    "shr": ("TC-RT-INT-0010", "TC-RT-INT-0014"),
    "sizeof": ("TC-RT-SIZEOF-0001", None),
    "store": ("TC-RT-MEM-0001", "TC-RT-MEM-0006"),
    "store.volatile": ("TC-RT-MEM-0010", "TC-RT-MEM-0015"),
    "sub": ("TC-RT-INT-0001", "TC-RT-INT-0015"),
    "sub.ieee": ("TC-RT-FP-0006", "TC-RT-FP-0006"),
    "sub.wrap": ("TC-RT-INT-0002", "TC-RT-INT-0002"),
    "switch": ("TC-RT-CTRL-0001", None),
    "trap": ("TC-RT-CTRL-0002", "TC-RT-CTRL-0002"),
    "trunc": ("TC-RT-CONV-0001", None),
    "write": ("TC-RT-IO-0001", "TC-UNIT-VM-0006"),
    "writeln": ("TC-RT-IO-0001", "TC-UNIT-VM-0006"),
    "xor": ("TC-RT-INT-0001", None),
}
for _opcode, (_normal, _trap) in _S6_OPCODE_CASES.items():
    S6_COVERED_ROWS[f"opcode:{_opcode}:normal"] = [_normal]
    if _trap is not None:
        S6_COVERED_ROWS[f"opcode:{_opcode}:trap"] = [_trap]

_S6_STATIC_OPCODES = [
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
for _index, _opcode in enumerate(_S6_STATIC_OPCODES, 1):
    S6_COVERED_ROWS[f"opcode:{_opcode}:static_error"] = [
        f"TC-SEM-OPC-{_index:04d}"
    ]

_S6_CODE_CASES = {
    "TC_RE_INTEGER_OVERFLOW": {
        "trigger": ["TC-RT-INT-0004", "TC-RT-INT-0006", "TC-RT-INT-0007",
                    "TC-RT-INT-0008", "TC-RT-INT-0012", "TC-RT-INT-0015",
                    "TC-RT-W-0001"],
        "no_false_positive": ["TC-RT-INT-0001", "TC-RT-INT-0002",
                              "TC-RT-W-0002"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-INT-0004"],
        "w32": ["TC-RT-W-0001"],
        "w64": ["TC-RT-INT-0004"],
    },
    "TC_RE_DIV_ZERO": {
        "trigger": ["TC-RT-INT-0005", "TC-RT-INT-0011", "TC-RT-FP-0003"],
        "no_false_positive": ["TC-RT-INT-0001", "TC-RT-FP-0001"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-INT-0005"],
    },
    "TC_RE_NEGATIVE_SHIFT": {
        "trigger": ["TC-RT-INT-0009", "TC-RT-INT-0013", "TC-RT-INT-0014"],
        "no_false_positive": ["TC-RT-INT-0010"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-INT-0009"],
    },
    "TC_RE_FLOAT_OVERFLOW": {
        "trigger": ["TC-RT-FP-0004"],
        "no_false_positive": ["TC-RT-FP-0001", "TC-RT-FP-0006"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-FP-0004"],
    },
    "TC_RE_FLOAT_UNDERFLOW": {
        "trigger": ["TC-RT-FP-0005"],
        "no_false_positive": ["TC-RT-FP-0007"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-FP-0005"],
    },
    "TC_RE_FLOAT_INVALID": {
        "trigger": ["TC-RT-FP-0002"],
        "no_false_positive": ["TC-RT-FP-0001", "TC-RT-FP-0006"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-FP-0002"],
    },
    "TC_RE_CAST_OVERFLOW": {
        "trigger": ["TC-RT-CONV-0002", "TC-RT-CONV-0004"],
        "no_false_positive": ["TC-RT-CONV-0001"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-CONV-0002"],
        "w32": ["TC-RT-CONV-0004"],
        "w64": ["TC-RT-CONV-0002"],
    },
    "TC_RE_NULL_POINTER": {
        "trigger": ["TC-RT-MEM-0005", "TC-RT-MEM-0006", "TC-RT-MEM-0007",
                    "TC-RT-MEM-0012", "TC-RT-MEM-0015", "TC-RT-MEM-0016",
                    "TC-RT-MEM-0017"],
        "no_false_positive": ["TC-RT-MEM-0001", "TC-RT-MEM-0010"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-MEM-0005"],
    },
    "TC_RE_PTRDIFF_OVERFLOW": {
        "trigger": ["TC-RT-MEM-0011", "TC-RT-MEM-0013"],
        "no_false_positive": ["TC-RT-MEM-0001"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-MEM-0011"],
        "w32": ["TC-RT-MEM-0013"],
        "w64": ["TC-RT-MEM-0011"],
    },
    "TC_RE_WRITE_PROTECTED": {
        "trigger": ["TC-RT-MEM-0009"],
        "no_false_positive": ["TC-RT-MEM-0001", "TC-RT-MEM-0010"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-MEM-0009"],
    },
    "TC_RE_ADDR_UNAVAILABLE": {
        "trigger": ["TC-RT-MEM-0008", "TC-RT-MEM-0014", "TC-RT-DEV-0001"],
        "no_false_positive": ["TC-RT-MEM-0001", "TC-RT-MEM-0010"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-MEM-0008"],
        "w32": ["TC-RT-MEM-0014"],
        "w64": ["TC-RT-MEM-0008"],
    },
    "TC_RE_STACK_OVERFLOW": {
        "trigger": ["TC-RT-CALL-0002"],
        "no_false_positive": ["TC-RT-CALL-0001"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-CALL-0002"],
    },
    "TC_RE_NULL_FUNCTION_POINTER": {
        "trigger": ["TC-RT-CALL-0004"],
        "no_false_positive": ["TC-RT-CALL-0003"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-CALL-0004"],
    },
    "TC_RE_IO": {
        "trigger": ["TC-RT-IO-0003", "TC-RT-IO-0004", "TC-RT-IO-0005"],
        "no_false_positive": ["TC-RT-IO-0001", "TC-RT-IO-0002",
                              "TC-RT-IO-0006"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-IO-0003"],
        "w32": ["TC-RT-IO-0005"],
        "w64": ["TC-RT-IO-0003"],
    },
    "TC_RE_TRAP": {
        "trigger": ["TC-RT-CTRL-0002"],
        "no_false_positive": ["TC-RT-INT-0001"],
        "position": ["TC-UNIT-VM-0006"],
        "stage": ["TC-RT-CTRL-0002"],
    },
}
for _code, _slots in _S6_CODE_CASES.items():
    for _slot, _cases in _slots.items():
        S6_COVERED_ROWS[f"code:{_code}:{_slot}"] = _cases


def build_rows(codes_data: dict, standard: str, design: str, test_plan: str):
    rows = []

    chapters = parse_chapters(test_plan, standard)
    for item in chapters:
        for slot in ("positive", "negative"):
            rows.append(slot_row(
                "chapter", item["item_id"], f"{item['chapter']} {item['title']}", slot,
                "test plan §3.6/§8.1",
                chapter=item["chapter"], title=item["title"],
                primary_stage=item["primary_stage"], suites=item["suites"],
            ))

    standard_codes = parse_standard_codes(standard)
    by_code = {}
    for entry in codes_data["codes"]:
        code = entry.get("code")
        if not isinstance(code, str):
            raise CoverageError("codes.json entry without a string code")
        if code in by_code:
            raise CoverageError(f"duplicate code in codes.json: {code}")
        by_code[code] = entry
    missing = [code for code in standard_codes if code not in by_code]
    extra = [code for code in by_code if code not in set(standard_codes)]
    if missing:
        raise CoverageError("codes.json missing standard codes: " + ", ".join(missing))
    if extra:
        raise CoverageError("codes.json has non-standard codes: " + ", ".join(extra))

    for code in standard_codes:
        entry = by_code[code]
        for slot in ("trigger", "no_false_positive", "position", "stage"):
            rows.append(slot_row(
                "code", f"code:{code}", code, slot,
                "§12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6",
                code=code, print_name=entry.get("print_name"),
                stage=entry.get("stage"), report_stage=entry.get("report_stage"),
                group=entry.get("group"), kind=entry.get("kind"),
                w_sensitive=entry.get("w_sensitive"),
                first_test_stage=entry.get("first_test_stage"),
            ))
        if entry.get("w_sensitive"):
            for slot in ("w32", "w64"):
                rows.append(slot_row(
                    "code", f"code:{code}", code, slot,
                    "standard §13.2.2 word-size instantiation; test plan §3.6",
                    code=code, w_sensitive=True,
                    first_test_stage=entry.get("first_test_stage"),
                ))

    opcodes, design_ops = parse_opcodes(standard, design)
    for item in opcodes:
        for slot in ("normal", "static_error"):
            rows.append(slot_row(
                "opcode", f"opcode:{item['opcode']}", item["opcode"], slot,
                "standard §6.2, design §3.2; test plan §3.6",
                opcode=item["opcode"], base_opcode=item["base_opcode"],
                mode=item["mode"], tc_opcode=item["tc_opcode"],
                group=item["group"], safety=item["safety"],
                trap_capable=item["trap_capable"], trap=item["trap_text"],
            ))
        if item["trap_capable"]:
            rows.append(slot_row(
                "opcode", f"opcode:{item['opcode']}", item["opcode"], "trap",
                "standard §6.2 trap column; test plan §3.6",
                opcode=item["opcode"], base_opcode=item["base_opcode"],
                mode=item["mode"], tc_opcode=item["tc_opcode"],
                group=item["group"], trap=item["trap_text"],
            ))

    design_items = parse_design12(design, test_plan)
    for item in design_items:
        for slot in ("suite_mapping", "gate_case"):
            rows.append(slot_row(
                "design12", f"design12:{item['number']:02d}",
                f"{item['number']}. {item['title']}", slot,
                "design §12, test plan §3.6/§8.2",
                number=item["number"], title=item["title"], detail=item["detail"],
                plan_name=item["plan_name"], plan_stage=item["plan_stage"],
                plan_suite=item["plan_suite"],
            ))

    for item in parse_first_diag_rules(standard):
        rows.append(slot_row(
            "first_diag_rule", f"first_diag_rule:{item['number']:02d}",
            f"{item['number']}. {item['title']}", "combination_case",
            "standard §12.2, test plan §3.6/§6.1",
            number=item["number"], title=item["title"], detail=item["detail"],
        ))

    for item in parse_same_site_priorities(codes_data, standard):
        rows.append(slot_row(
            "same_site_priority", item["item_id"], item["item"], item["slot"],
            item["spec"], codes=item["codes"],
        ))

    for item in parse_flips(standard):
        for slot in ("w32", "w64"):
            rows.append(slot_row(
                "flip32_64", item["item_id"], item["item"], slot,
                "standard §13.2.2, test plan §3.6/§8.2",
                expected_w32=item["expected_w32"],
                expected_w64=item["expected_w64"],
            ))

    for item in parse_annotations(standard):
        rows.append(slot_row(
            "annotation", item["item_id"], item["annotation"], "coverage",
            item["spec"] + "; test plan §3.6",
            annotation=item["annotation"], location=item["location"],
            meaning=item["meaning"],
            required_slots=[
                "position_legal", "position_illegal", "mutual_exclusion",
                "parameter_range", "promise_violation_non_static",
            ],
        ))
        if item["annotation"] in ("@align", "@range"):
            rows.append(slot_row(
                "annotation", item["item_id"], item["annotation"],
                "const_path_value",
                "§8.6 constant-path endpoint value: power-of-two/lo<=hi/W",
                annotation=item["annotation"], location=item["location"],
                meaning=item["meaning"],
            ))

    for item in parse_device_policies(design):
        rows.append(slot_row(
            "device_policy", item["item_id"], item["policy"], "trap_mapping",
            item["spec"], policy=item["policy"],
            description=item["description"], expected=item["expected"],
        ))

    for item in parse_io_states(design):
        rows.append(slot_row(
            "io_source_state", item["item_id"], item["state"], "path",
            item["spec"], state=item["state"], description=item["description"],
            expected=item["expected"],
        ))

    for item in parse_exit_strategies(design):
        rows.append(slot_row(
            "exit_strategy", item["item_id"], item["branch"], "branch",
            item["spec"], branch=item["branch"], field=item["field"],
            description=item["description"],
        ))

    # S0/S1 explicitly covered rows.  Only named case ids may set a row to
    # covered; everything else remains visibly uncovered.
    for row in rows:
        case_ids = L0_COVERED_ROWS.get(row["id"])
        if case_ids:
            row["status"] = "covered"
            row["case_ids"] = list(case_ids)
        for covered in (S1_COVERED_ROWS, S2_COVERED_ROWS, S3_COVERED_ROWS,
                        S4_COVERED_ROWS, S5_COVERED_ROWS, S6_COVERED_ROWS,
                        S7_COVERED_ROWS, S8_COVERED_ROWS):
            case_ids = covered.get(row["id"])
            if case_ids:
                row["status"] = "covered"
                merged = list(row["case_ids"])
                for case_id in case_ids:
                    if case_id not in merged:
                        merged.append(case_id)
                row["case_ids"] = merged
        scope_na = SCOPE_NA_ROWS.get(row["id"])
        if scope_na:
            row["status"] = "suspended"
            row["case_ids"] = []
            row["reason"] = scope_na[0]
            row["closure_stage"] = scope_na[1]

    return rows, design_ops


def summarize(rows):
    categories = {}
    for category in CATEGORY_ORDER:
        category_rows = [row for row in rows if row["category"] == category]
        item_ids = {row["item_id"] for row in category_rows}
        uncovered = sum(1 for row in category_rows if row["status"] == "uncovered")
        covered = sum(1 for row in category_rows if row["status"] == "covered")
        suspended = sum(1 for row in category_rows if row["status"] == "suspended")
        categories[category] = {
            "label": CATEGORY_LABELS[category],
            "items": len(item_ids),
            "slots": len(category_rows),
            "covered": covered,
            "uncovered": uncovered,
            "suspended": suspended,
        }
    total = {
        "items": len({row["item_id"] for row in rows}),
        "slots": len(rows),
        "covered": sum(1 for row in rows if row["status"] == "covered"),
        "uncovered": sum(1 for row in rows if row["status"] == "uncovered"),
        "suspended": sum(1 for row in rows if row["status"] == "suspended"),
    }
    return categories, total


def build_document(codes_data, standard_text, design_text, test_plan_text,
                   standard_path, design_path, test_plan_path, codes_path):
    rows, design_ops = build_rows(codes_data, standard_text, design_text, test_plan_text)
    categories, total = summarize(rows)
    return {
        "schema": "tc-traceability-coverage/1",
        "language_version": codes_data.get("language_version", "0.0.46"),
        "spec": codes_data.get("spec", ""),
        "generated_from": {
            "codes_json": str(codes_path.relative_to(REPO_ROOT)) if codes_path.is_relative_to(REPO_ROOT) else str(codes_path),
            "standard": str(standard_path.relative_to(REPO_ROOT)) if standard_path.is_relative_to(REPO_ROOT) else str(standard_path),
            "design": str(design_path.relative_to(REPO_ROOT)) if design_path.is_relative_to(REPO_ROOT) else str(design_path),
            "test_plan": str(test_plan_path.relative_to(REPO_ROOT)) if test_plan_path.is_relative_to(REPO_ROOT) else str(test_plan_path),
            "sha256": {
                "codes_json": sha256_text(json.dumps(codes_data, ensure_ascii=False, sort_keys=True, indent=2)),
                "standard": sha256_text(standard_text),
                "design": sha256_text(design_text),
                "test_plan": sha256_text(test_plan_text),
            },
        },
        "policy": {
            "initial_status": "uncovered",
            "statuses": ["uncovered", "covered", "suspended"],
            "rule": "Missing coverage is explicit; an item is covered only when case_ids are non-empty (named L0/S1/S2-S3/S4 stage sets); suspended rows have a reason and closure stage.",
            "l0_covered_rows": sorted(L0_COVERED_ROWS),
            "s1_covered_rows": sorted(S1_COVERED_ROWS),
            "s2_covered_rows": sorted(S2_COVERED_ROWS),
            "s3_covered_rows": sorted(S3_COVERED_ROWS),
            "s4_covered_rows": sorted(S4_COVERED_ROWS),
            "s5_covered_rows": sorted(S5_COVERED_ROWS),
            "s6_covered_rows": sorted(S6_COVERED_ROWS),
            "s7_covered_rows": sorted(S7_COVERED_ROWS),
            "s8_covered_rows": sorted(S8_COVERED_ROWS),
            "scope_na_rows": sorted(SCOPE_NA_ROWS),
        },
        "cross_checks": {
            "standard_chapters": "§0-§14",
            "diagnostic_codes": len(parse_standard_codes(standard_text)),
            "standard_opcodes": len({row["item_id"] for row in rows if row["category"] == "opcode"}),
            "tc_opcode_enum": len(design_ops),
        },
        "summary": total,
        "categories": categories,
        "rows": rows,
    }


def render_markdown(doc) -> str:
    summary = doc["summary"]
    lines = [
        "# TC-Compiler 0.0.46 Structural Coverage Matrix",
        "",
        "> Generated from the 0.0.46 standard, design and test plan.",
        "> **Every row starts as `uncovered`; a row is `covered` only when it",
        "> carries at least one `case_ids` entry.** Named L0/S1/S2-S3/S4/S5/S6",
        "> rows from the unit, conformance and golden tables are explicitly",
        "> covered; every later-stage row remains visibly uncovered. Missing",
        "> coverage is explicit (test plan §3.6/§7.5).",
        "",
        f"- Language version: `{doc['language_version']}`",
        f"- Spec: {doc['spec']}",
        f"- Rows: **{summary['slots']}** total, **{summary['uncovered']}** uncovered, "
        f"**{summary['covered']}** covered, **{summary.get('suspended', 0)}** N/A",
        "",
        "## Summary by category",
        "",
        "| Category | Items | Slots | Covered | Uncovered | N/A |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for category in CATEGORY_ORDER:
        info = doc["categories"][category]
        lines.append(
            f"| {CATEGORY_LABELS[category]} (`{category}`) | {info['items']} | "
            f"{info['slots']} | {info['covered']} | {info['uncovered']} | "
            f"{info.get('suspended', 0)} |"
        )
    lines.append(
        f"| **Total** | **{summary['items']}** | **{summary['slots']}** | "
        f"**{summary['covered']}** | **{summary['uncovered']}** | "
        f"**{summary.get('suspended', 0)}** |"
    )
    lines.extend(["", "## Coverage rows", ""])

    for category in CATEGORY_ORDER:
        category_rows = [row for row in doc["rows"] if row["category"] == category]
        if not category_rows:
            continue
        lines.extend([
            f"### {CATEGORY_LABELS[category]} (`{category}`)",
            "",
            "| Row ID | Item | Slot | Status | Case IDs | Spec |",
            "|---|---|---|---|---|---|",
        ])
        for row in category_rows:
            case_ids = ", ".join(row["case_ids"]) if row["case_ids"] else "—"
            lines.append(
                f"| `{row['id']}` | {markdown_cell(row['item'])} | "
                f"{markdown_cell(row['slot'])} | **{row['status']}** | "
                f"{markdown_cell(case_ids)} | {markdown_cell(row['spec'])} |"
            )
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def markdown_cell(value) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ").strip()


def write_outputs(doc, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    json_text = json.dumps(doc, ensure_ascii=False, indent=2) + "\n"
    md_text = render_markdown(doc)
    (out_dir / JSON_NAME).write_text(json_text, encoding="utf-8")
    (out_dir / MARKDOWN_NAME).write_text(md_text, encoding="utf-8")
    return json_text, md_text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate/check the structural coverage matrix.")
    parser.add_argument("--check", action="store_true",
                        help="fail if coverage.generated.json or coverage-report.md is stale")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT,
                        help=f"output directory (default: {DEFAULT_OUT})")
    parser.add_argument("--codes", type=Path, default=DEFAULT_CODES,
                        help=f"codes.json path (default: {DEFAULT_CODES})")
    parser.add_argument("--standard", type=Path, default=DEFAULT_STANDARD)
    parser.add_argument("--design", type=Path, default=DEFAULT_DESIGN)
    parser.add_argument("--test-plan", type=Path, default=DEFAULT_TEST_PLAN)
    args = parser.parse_args(argv)

    codes_path = args.codes.resolve()
    standard_path = args.standard.resolve()
    design_path = args.design.resolve()
    test_plan_path = args.test_plan.resolve()
    try:
        codes_text = read_text(codes_path, "codes.json")
        try:
            codes_data = json.loads(codes_text)
        except json.JSONDecodeError as exc:
            raise CoverageError(f"codes.json is not valid JSON ({codes_path}): {exc}") from exc
        if not isinstance(codes_data, dict) or not isinstance(codes_data.get("codes"), list):
            raise CoverageError("codes.json must be an object with a 'codes' array")
        standard_text = read_text(standard_path, "standard document")
        design_text = read_text(design_path, "design document")
        test_plan_text = read_text(test_plan_path, "test plan")

        doc = build_document(
            codes_data, standard_text, design_text, test_plan_text,
            standard_path, design_path, test_plan_path, codes_path,
        )
        summary = doc["summary"]
        if args.check:
            json_path = args.out_dir / JSON_NAME
            md_path = args.out_dir / MARKDOWN_NAME
            expected_json = json.dumps(doc, ensure_ascii=False, indent=2) + "\n"
            expected_md = render_markdown(doc)
            stale = []
            if not json_path.is_file():
                stale.append(f"missing {json_path}")
            elif json_path.read_text(encoding="utf-8") != expected_json:
                stale.append(f"stale {json_path}")
            if not md_path.is_file():
                stale.append(f"missing {md_path}")
            elif md_path.read_text(encoding="utf-8") != expected_md:
                stale.append(f"stale {md_path}")
            if stale:
                print(f"FAIL: {'; '.join(stale)}", file=sys.stderr)
                print(
                    "  regenerate with: python3 tests/traceability/coverage.py",
                    file=sys.stderr,
                )
                print(
                    f"uncovered: {summary['uncovered']}/{summary['slots']} slots",
                    file=sys.stderr,
                )
                return 1
            print(
                f"OK: coverage matrix is current; uncovered "
                f"{summary['uncovered']}/{summary['slots']} slots "
                f"({summary['items']} items)"
            )
            return 0

        write_outputs(doc, args.out_dir)
    except CoverageError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {args.out_dir / JSON_NAME}")
    print(f"wrote {args.out_dir / MARKDOWN_NAME}")
    for category in CATEGORY_ORDER:
        info = doc["categories"][category]
        print(
            f"  {category}: {info['items']} items, {info['slots']} slots, "
            f"{info['uncovered']} uncovered"
        )
    print(
        f"uncovered: {summary['uncovered']}/{summary['slots']} slots "
        f"({summary['items']} items, {summary['covered']} covered)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
