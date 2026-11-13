#!/usr/bin/env python3
"""Validate tests/traceability/codes.json against the 0.0.46 standard.

The authoritative source is docs/TC语言及编译器标准-0.0.46.md:
  * section 12.3 (the complete 65 CE + 15 RE table),
  * the section 12.3 preamble "字长相关条件" paragraph (the 12 W-sensitive
    codes), and
  * section 12.2 / the IMPORT paragraph (the five IMPORT-report codes).

All tables and code sets are parsed from the Markdown.  Nothing about the
80-code set is hardcoded here except the shapes needed to validate it.

Exit code 0 = codes.json is consistent; 1 = any mismatch or missing input.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CODES = Path(__file__).resolve().parent / "codes.json"
DEFAULT_STANDARD = REPO_ROOT / "docs" / "TC语言及编译器标准-0.0.46.md"

STATIC_STAGES = {"LEX", "SYN", "LEX/SYN", "SEM", "CT"}
CODE_RE = re.compile(r"^TC_(CE|RE)_[A-Z0-9_]+$")
ANY_CODE_RE = re.compile(r"`(TC_(?:CE|RE)_[A-Z0-9_]+)`")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)


def read_text(path: Path, label: str) -> str | None:
    if not path.is_file():
        fail(f"{label} missing: {path}")
        return None
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        fail(f"cannot read {label} {path}: {exc}")
        return None


def section_between(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise ValueError(f"marker not found: {start_marker}")
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        end = len(text)
    return text[start:end]


def strip_code(cell: str) -> str:
    cell = cell.strip()
    if cell.startswith("`") and cell.endswith("`"):
        cell = cell[1:-1]
    return cell.strip()


def parse_standard_codes(text: str):
    """Return (rows, table order, IMPORT codes, W-sensitive codes)."""
    section = section_between(text, "### 12.3 诊断码（完整集）", "### 12.4 不可达代码")

    preamble = section.split("#### 词法/语法", 1)[0]
    m = re.search(r"\*\*字长相关条件\*\*[：:](.*?)。", preamble, re.S)
    if not m:
        raise ValueError("cannot parse the 12.3 W-sensitive preamble")
    w_sensitive = set(ANY_CODE_RE.findall(m.group(1)))
    if len(w_sensitive) != 12:
        raise ValueError(
            f"expected 12 W-sensitive codes in the 12.3 preamble, found {len(w_sensitive)}"
        )

    import_match = re.search(r"\*\*IMPORT 码的阶段例外\*\*[：:](.*?)。", text, re.S)
    if not import_match:
        raise ValueError("cannot parse the IMPORT stage exception paragraph")
    import_codes = ANY_CODE_RE.findall(import_match.group(1))
    if len(import_codes) != 5:
        raise ValueError(f"expected 5 IMPORT codes, found {len(import_codes)}")

    rows: dict[str, dict[str, str]] = {}
    order: list[str] = []
    for line in section.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells:
            continue
        code = strip_code(cells[0])
        if not CODE_RE.match(code):
            continue
        if len(cells) >= 4 and cells[2] in STATIC_STAGES:
            print_name, stage, trigger = strip_code(cells[1]), cells[2], cells[3]
        elif len(cells) >= 3:
            # Runtime table: | code | print name | trigger | (no stage column).
            print_name, stage, trigger = strip_code(cells[1]), "", cells[2]
        else:
            raise ValueError(f"malformed 12.3 table row: {line}")
        if code in rows:
            raise ValueError(f"duplicate code in 12.3 table: {code}")
        rows[code] = {
            "code": code,
            "print_name": print_name,
            "stage": stage,
            "trigger": trigger,
        }
        order.append(code)

    if len(order) != 80:
        raise ValueError(f"12.3 table must contain 80 codes, parsed {len(order)}")
    return rows, order, import_codes, w_sensitive  # type: ignore[return-value]


def load_codes_json(path: Path) -> dict | None:
    text = read_text(path, "codes.json")
    if text is None:
        return None
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"codes.json is not valid JSON ({path}): {exc}")
        return None
    if not isinstance(data, dict) or not isinstance(data.get("codes"), list):
        fail("codes.json must be an object with a 'codes' array")
        return None
    return data


def nonempty_str(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def normalize_bool(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"true", "1", "yes", "y"}:
            return True
        if lowered in {"false", "0", "no", "n", ""}:
            return False
    if isinstance(value, int):
        return value != 0
    raise ValueError(f"not a boolean: {value!r}")


def check_codes(data: dict, standard_rows: dict[str, dict[str, str]],
                standard_order: list[str], import_codes: list[str],
                w_sensitive: set[str]) -> list[str]:
    errors: list[str] = []
    entries = data["codes"]
    seen: dict[str, dict] = {}

    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"codes[{index}] is not an object")
            continue
        code = entry.get("code")
        if not isinstance(code, str) or not CODE_RE.match(code):
            errors.append(f"codes[{index}] has invalid code: {code!r}")
            continue
        if code in seen:
            errors.append(f"duplicate code in codes.json: {code}")
            continue
        seen[code] = entry

    expected = set(standard_order)
    actual = set(seen)
    for code in standard_order:
        if code not in actual:
            errors.append(f"missing code: {code}")
    for code in sorted(actual - expected):
        errors.append(f"extra code not in standard 12.3: {code}")

    ce_count = sum(1 for code in actual if code.startswith("TC_CE_"))
    re_count = sum(1 for code in actual if code.startswith("TC_RE_"))
    if ce_count != 65 or re_count != 15:
        errors.append(f"code counts must be 65 TC_CE_* + 15 TC_RE_*, found {ce_count}+{re_count}")
    if len(entries) != 80:
        errors.append(f"codes.json must contain 80 entries, found {len(entries)}")

    for code in standard_order:
        entry = seen.get(code)
        if entry is None:
            continue
        ref = standard_rows[code]

        if entry.get("print_name") != ref["print_name"]:
            errors.append(
                f"{code}: print_name {entry.get('print_name')!r} != standard {ref['print_name']!r}"
            )

        std_stage = ref["stage"]
        stage = entry.get("stage")
        report_stage = entry.get("report_stage")
        if std_stage:
            if stage != std_stage:
                errors.append(f"{code}: stage {stage!r} != standard {std_stage!r}")
        else:
            # Runtime rows have no stage column.  codes.json uses RT.
            if stage not in {"RT", ""}:
                errors.append(f"{code}: runtime code must use stage 'RT' (or blank), got {stage!r}")

        if code in import_codes:
            if stage != "SEM":
                errors.append(f"{code}: IMPORT code must keep stage 'SEM', got {stage!r}")
            if report_stage != "IMPORT":
                errors.append(
                    f"{code}: IMPORT code must report at IMPORT, got {report_stage!r}"
                )
        else:
            if report_stage != stage:
                errors.append(
                    f"{code}: report_stage {report_stage!r} != stage {stage!r}"
                )

        expected_w = code in w_sensitive
        try:
            actual_w = normalize_bool(entry.get("w_sensitive"))
        except ValueError as exc:
            errors.append(f"{code}: {exc}")
            actual_w = False
        if actual_w != expected_w:
            where = "must be true" if expected_w else "must be false"
            errors.append(f"{code}: w_sensitive {where} per the 12.3 preamble")

        for field in ("trigger", "position", "first_test_stage"):
            if not nonempty_str(entry.get(field)):
                errors.append(f"{code}: field {field!r} must be a non-empty string")

        if not nonempty_str(entry.get("kind")):
            errors.append(f"{code}: field 'kind' must be 'ce' or 're'")
        else:
            want_kind = "ce" if code.startswith("TC_CE_") else "re"
            if entry["kind"] != want_kind:
                errors.append(f"{code}: kind {entry['kind']!r} != {want_kind!r}")

    return errors


def parse_c_table_dump(path: Path) -> tuple[dict[str, dict[str, str]], list[str]]:
    # A dump argument may be a TSV file or an executable that writes the TSV to
    # stdout (tests/unit/dump_codes.c).  This keeps CTest integration shell-free.
    if path.is_file() and os.access(path, os.X_OK):
        try:
            proc = subprocess.run([str(path)], capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as exc:
            raise ValueError(f"cannot run C table dump {path}: {exc}") from exc
        if proc.returncode != 0:
            raise ValueError(
                f"C table dump {path} exited {proc.returncode}: "
                f"{proc.stderr.decode('utf-8', 'replace').strip()}"
            )
        text = proc.stdout.decode("utf-8")
    else:
        text = read_text(path, "--c-table-dump")
        if text is None:
            return {}, []
    rows: dict[str, dict[str, str]] = {}
    order: list[str] = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        if not raw.strip():
            continue
        cells = raw.rstrip("\n").split("\t")
        if len(cells) != 6:
            raise ValueError(
                f"C table dump line {lineno}: expected 6 TSV columns, found {len(cells)}"
            )
        if cells[0].strip().lower() == "code":
            continue  # header
        row = {
            "code": cells[0].strip(),
            "print_name": cells[1].strip(),
            "stage": cells[2].strip(),
            "report_stage": cells[3].strip(),
            "w_sensitive": cells[4].strip(),
            "group": cells[5].strip(),
        }
        code = row["code"]
        if code in rows:
            raise ValueError(f"C table dump duplicate code: {code}")
        rows[code] = row
        order.append(code)
    return rows, order


def check_c_table(data: dict, dump: dict[str, dict[str, str]],
                  dump_order: list[str]) -> list[str]:
    errors: list[str] = []
    entries = {entry.get("code"): entry for entry in data["codes"] if isinstance(entry, dict)}
    expected_order = [entry.get("code") for entry in data["codes"]]
    if dump_order != expected_order:
        first_diff = next(
            (i for i, (a, b) in enumerate(zip(dump_order, expected_order)) if a != b),
            min(len(dump_order), len(expected_order)),
        )
        errors.append(
            "C table dump order differs from codes.json at index "
            f"{first_diff}: dump={dump_order[first_diff:first_diff + 1]!r} "
            f"json={expected_order[first_diff:first_diff + 1]!r}"
        )
    for code in sorted(set(entries) - set(dump)):
        errors.append(f"C table dump missing code: {code}")
    for code in dump_order:
        if code not in entries:
            errors.append(f"C table dump has extra code not in codes.json: {code}")
            continue
        want = entries[code]
        got = dump[code]
        for field in ("print_name", "stage", "report_stage", "group"):
            if got[field] != want.get(field):
                errors.append(
                    f"{code}: C dump {field} {got[field]!r} != codes.json {want.get(field)!r}"
                )
        try:
            dump_w = normalize_bool(got["w_sensitive"])
            json_w = normalize_bool(want.get("w_sensitive"))
        except ValueError as exc:
            errors.append(f"{code}: {exc}")
            continue
        if dump_w != json_w:
            errors.append(
                f"{code}: C dump w_sensitive {dump_w} != codes.json {json_w}"
            )
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate codes.json against standard section 12.3."
    )
    parser.add_argument("--codes", type=Path, default=DEFAULT_CODES,
                        help=f"codes.json path (default: {DEFAULT_CODES})")
    parser.add_argument("--standard", type=Path, default=DEFAULT_STANDARD,
                        help="standard Markdown path")
    parser.add_argument("--c-table-dump", type=Path, default=None,
                        help="optional TSV dump of the C code table")
    args = parser.parse_args(argv)

    standard_text = read_text(args.standard, "standard document")
    if standard_text is None:
        return 1
    try:
        standard_rows, standard_order, import_codes, w_sensitive = parse_standard_codes(
            standard_text
        )
    except ValueError as exc:
        fail(f"cannot parse standard 12.3: {exc}")
        return 1

    data = load_codes_json(args.codes)
    if data is None:
        return 1

    errors = check_codes(data, standard_rows, standard_order, import_codes, w_sensitive)

    if args.c_table_dump is not None:
        try:
            dump, dump_order = parse_c_table_dump(args.c_table_dump)
        except ValueError as exc:
            fail(str(exc))
            return 1
        errors.extend(check_c_table(data, dump, dump_order))

    if errors:
        print(f"FAIL: {len(errors)} codes.json mismatch(es)", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print(
        "OK: codes.json matches standard 12.3 "
        f"({len(standard_order)} codes: 65 TC_CE_* + 15 TC_RE_*; "
        f"{len(w_sensitive)} W-sensitive; {len(import_codes)} IMPORT-report)"
    )
    if args.c_table_dump is not None:
        print(f"OK: C table dump matches codes.json ({len(standard_order)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
