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

All rows start as "uncovered".  Only named S0 L0 contract rows in
L0_COVERED_ROWS may be marked "covered" with a real case id; missing coverage
for everything else is explicit and is never silently rendered as covered.

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

    # S0 explicitly covered contract rows.  Only named L0 cases may set a row
    # to covered; everything else remains visibly uncovered.
    for row in rows:
        case_ids = L0_COVERED_ROWS.get(row["id"])
        if case_ids:
            row["status"] = "covered"
            row["case_ids"] = list(case_ids)

    return rows, design_ops


def summarize(rows):
    categories = {}
    for category in CATEGORY_ORDER:
        category_rows = [row for row in rows if row["category"] == category]
        item_ids = {row["item_id"] for row in category_rows}
        uncovered = sum(1 for row in category_rows if row["status"] == "uncovered")
        covered = sum(1 for row in category_rows if row["status"] == "covered")
        categories[category] = {
            "label": CATEGORY_LABELS[category],
            "items": len(item_ids),
            "slots": len(category_rows),
            "covered": covered,
            "uncovered": uncovered,
        }
    total = {
        "items": len({row["item_id"] for row in rows}),
        "slots": len(rows),
        "covered": sum(1 for row in rows if row["status"] == "covered"),
        "uncovered": sum(1 for row in rows if row["status"] == "uncovered"),
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
            "rule": "Missing coverage is explicit; an item is covered only when case_ids are non-empty.",
            "l0_covered_rows": sorted(L0_COVERED_ROWS),
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
        "> Generated skeleton from the 0.0.46 standard, design and test plan.",
        "> **Every row starts as `uncovered`; a row is `covered` only when it",
        "> carries at least one `case_ids` entry.** S0 L0 rows named in",
        "> `tests/unit/README.md` are explicitly covered; conformance cases remain",
        "> uncovered until S1+. Missing coverage is explicit (test plan §3.6/§7.5).",
        "",
        f"- Language version: `{doc['language_version']}`",
        f"- Spec: {doc['spec']}",
        f"- Rows: **{summary['slots']}** total, **{summary['uncovered']}** uncovered, "
        f"**{summary['covered']}** covered",
        "",
        "## Summary by category",
        "",
        "| Category | Items | Slots | Covered | Uncovered |",
        "|---|---:|---:|---:|---:|",
    ]
    for category in CATEGORY_ORDER:
        info = doc["categories"][category]
        lines.append(
            f"| {CATEGORY_LABELS[category]} (`{category}`) | {info['items']} | "
            f"{info['slots']} | {info['covered']} | {info['uncovered']} |"
        )
    lines.append(
        f"| **Total** | **{summary['items']}** | **{summary['slots']}** | "
        f"**{summary['covered']}** | **{summary['uncovered']}** |"
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
