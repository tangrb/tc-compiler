#!/usr/bin/env python3
"""Validate every *.golden.yaml in tests/golden.

The validator is deliberately independent of any compiler.  It parses the
small YAML subset used by the golden files with the Python standard library
only and checks the S0 contract:

  * id, spec, derivation;
  * review.reviewer, review.status == approved, review.date (YYYY-MM-DD);
  * for diagnostic goldens: expect.stage, expect.code, expect.primary.line /
    expect.primary.column, and expect.diagnostics == exactly_one.

Exit code 1 on any missing field, malformed value, duplicate id, or parse
error.  Run: python3 tests/golden/validate.py
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CODES_PATH = HERE.parent / "traceability" / "codes.json"

ALLOWED_TOP = {
    "id", "kind", "profile", "profiles", "spec", "source", "source_file",
    "derivation", "review", "expect", "compare", "normalize", "link", "uart",
    "device",
}
ALLOWED_REVIEW = {
    "reviewer", "reviewer_kind", "reviewer_independent", "review_kind",
    "status", "date", "commit", "evidence_version", "notes",
}
ALLOWED_EXPECT_DIAG = {
    "stage", "code", "primary", "related", "diagnostics",
}


class YamlError(ValueError):
    pass


def indent_of(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def parse_scalar(text: str):
    text = text.strip()
    if text == "":
        return ""
    if text.startswith('"') and text.endswith('"') and len(text) >= 2:
        try:
            return json.loads(text)
        except json.JSONDecodeError as exc:
            raise YamlError(f"invalid double-quoted scalar {text!r}: {exc}") from exc
    if text.startswith("'") and text.endswith("'") and len(text) >= 2:
        return text[1:-1].replace("''", "'")
    lowered = text.lower()
    if lowered in {"true", "yes"}:
        return True
    if lowered in {"false", "no"}:
        return False
    if lowered in {"null", "~"}:
        return None
    if text == "[]":
        return []
    if text == "{}":
        return {}
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    if text.startswith("[") or text.startswith("{"):
        raise YamlError(f"flow collection is not supported by validate.py: {text!r}")
    return text


class MiniYaml:
    def __init__(self, text: str):
        self.lines = text.splitlines()

    def parse(self):
        index = self._skip(0)
        if index >= len(self.lines):
            return {}, 0
        value, index = self._block(index, indent_of(self.lines[index]))
        index = self._skip(index)
        if index < len(self.lines):
            raise YamlError(f"unexpected content at line {index + 1}: {self.lines[index]!r}")
        return value

    def _skip(self, index: int) -> int:
        while index < len(self.lines):
            stripped = self.lines[index].strip()
            if not stripped or stripped.startswith("#"):
                index += 1
                continue
            return index
        return index

    def _block(self, index: int, indent: int):
        index = self._skip(index)
        if index >= len(self.lines):
            return "", index
        line = self.lines[index]
        if indent_of(line) < indent:
            return "", index
        if line[indent:].startswith("- "):
            return self._sequence(index, indent)
        return self._mapping(index, indent)

    def _mapping(self, index: int, indent: int):
        result = {}
        while True:
            index = self._skip(index)
            if index >= len(self.lines):
                break
            line = self.lines[index]
            if indent_of(line) != indent:
                break
            content = line[indent:]
            if content.startswith("- "):
                break
            if ":" not in content:
                raise YamlError(f"expected 'key: value' at line {index + 1}: {line!r}")
            key_text, rest = content.split(":", 1)
            key = parse_scalar(key_text.strip())
            if not isinstance(key, str) or not key:
                raise YamlError(f"invalid key at line {index + 1}: {key_text!r}")
            if key in result:
                raise YamlError(f"duplicate key {key!r} at line {index + 1}")
            rest = rest.strip()
            if rest in {"|", "|-", ">", ">-"}:
                value, index = self._block_scalar(index + 1, indent, fold=rest.startswith(">"))
                result[key] = value
            elif rest == "":
                next_index = self._skip(index + 1)
                if next_index < len(self.lines) and indent_of(self.lines[next_index]) > indent:
                    value, index = self._block(next_index, indent_of(self.lines[next_index]))
                else:
                    value, index = "", index + 1
                result[key] = value
            else:
                result[key] = parse_scalar(rest)
                index += 1
        return result, index

    def _sequence(self, index: int, indent: int):
        result = []
        while True:
            index = self._skip(index)
            if index >= len(self.lines):
                break
            line = self.lines[index]
            if indent_of(line) != indent or not line[indent:].startswith("- "):
                break
            rest = line[indent + 2:].strip()
            if rest == "":
                next_index = self._skip(index + 1)
                if next_index < len(self.lines) and indent_of(self.lines[next_index]) > indent:
                    value, index = self._block(next_index, indent_of(self.lines[next_index]))
                else:
                    value, index = "", index + 1
            elif ":" in rest and not rest.startswith(('"', "'")):
                # A small inline mapping item is accepted by re-reading it as a
                # nested block is intentionally not supported; reject loudly.
                raise YamlError(
                    f"sequence mapping items are not supported at line {index + 1}"
                )
            else:
                value = parse_scalar(rest)
                index += 1
            result.append(value)
        return result, index

    def _block_scalar(self, index: int, parent_indent: int, fold: bool):
        collected = []
        block_indent = None
        while index < len(self.lines):
            line = self.lines[index]
            if line.strip() == "":
                collected.append("")
                index += 1
                continue
            if indent_of(line) <= parent_indent:
                break
            if block_indent is None:
                block_indent = indent_of(line)
            collected.append(line[block_indent:] if len(line) >= block_indent else "")
            index += 1
        while collected and collected[-1] == "":
            collected.pop()
        if fold:
            value = " ".join(part.strip() for part in collected if part.strip())
        else:
            value = "\n".join(collected)
        return value + ("\n" if value else ""), index


def parse_yaml(text: str):
    return MiniYaml(text).parse()


def nonempty_string(value) -> bool:
    return isinstance(value, str) and bool(value.strip())


def load_review_record(path: Path = HERE / "review-record.md") -> dict[str, dict]:
    if not path.is_file():
        raise ValueError(f"{path}: missing review record")
    text = path.read_text(encoding="utf-8")
    match = re.search(r"```json\s*(.*?)```", text, re.S)
    if not match:
        raise ValueError(f"{path}: missing json review block")
    data = json.loads(match.group(1))
    result = {}
    for review in data.get("reviews", []):
        if isinstance(review, dict) and nonempty_string(review.get("artifact")):
            artifact = review["artifact"]
            prefix = "tests/golden/"
            if artifact.startswith(prefix):
                artifact = artifact[len(prefix):]
            result[artifact] = review
    return result


def load_code_map(path: Path = CODES_PATH) -> dict[str, dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    codes = data.get("codes")
    if not isinstance(codes, list):
        raise ValueError(f"{path}: missing codes list")
    result = {}
    for entry in codes:
        if isinstance(entry, dict) and nonempty_string(entry.get("code")):
            result[entry["code"]] = entry
    if len(result) != 80:
        raise ValueError(f"{path}: expected 80 codes, found {len(result)}")
    return result


def stage_allowed(entry: dict, stage: str) -> bool:
    report = entry.get("report_stage")
    listed = entry.get("stage")
    if report == "IMPORT":
        return stage == "IMPORT"
    if listed == "LEX/SYN":
        return stage in {"LEX", "SYN", "LEX/SYN"}
    return stage == listed


def _reject_unknown(mapping, allowed, where, errors, path):
    if not isinstance(mapping, dict):
        return
    unknown = sorted(set(mapping) - allowed)
    if unknown:
        errors.append(f"{path}: {where} has unknown field(s): {', '.join(unknown)}")


def validate_file(path: Path, root: Path, code_map: dict[str, dict] | None = None) -> list[str]:
    errors = []
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"{path}: cannot read: {exc}"]
    try:
        data = parse_yaml(text)
    except YamlError as exc:
        return [f"{path}: YAML parse error: {exc}"]
    if not isinstance(data, dict):
        return [f"{path}: top level must be a mapping"]
    _reject_unknown(data, ALLOWED_TOP, "top level", errors, path)

    for field in ("id", "kind", "profile", "spec", "source", "derivation"):
        if not nonempty_string(data.get(field)):
            errors.append(f"{path}: missing or empty '{field}'")
    profile = data.get("profile")
    if nonempty_string(profile) and profile not in {"common", "w32", "w64"}:
        errors.append(f"{path}: 'profile' must be common/w32/w64, got {profile!r}")
    try:
        rel = path.resolve().relative_to(root.resolve())
        directory = rel.parts[0] if len(rel.parts) > 1 else ""
    except ValueError:
        directory = ""
    if directory in {"common", "w32", "w64"} and profile != directory:
        errors.append(
            f"{path}: profile {profile!r} does not match directory {directory!r}"
        )
    spec = data.get("spec")
    if nonempty_string(spec) and "§" not in spec:
        errors.append(f"{path}: 'spec' must cite at least one § section")
    if "source_file" in data and not nonempty_string(data.get("source_file")):
        errors.append(f"{path}: 'source_file' must be non-empty if present")

    review = data.get("review")
    if not isinstance(review, dict):
        errors.append(f"{path}: missing 'review' mapping")
    else:
        _reject_unknown(review, ALLOWED_REVIEW, "review", errors, path)
        reviewer = review.get("reviewer")
        if not nonempty_string(reviewer):
            errors.append(f"{path}: missing or empty 'review.reviewer'")
        elif any(word in reviewer.lower() for word in ("placeholder", "rehearsal")):
            errors.append(f"{path}: 'review.reviewer' is still a placeholder")
        if review.get("status") != "approved":
            errors.append(f"{path}: 'review.status' must be approved")
        if review.get("reviewer_independent") is not True:
            errors.append(f"{path}: 'review.reviewer_independent' must be true")
        date = review.get("date")
        if not nonempty_string(date):
            errors.append(f"{path}: missing or empty 'review.date'")
        else:
            try:
                datetime.date.fromisoformat(date)
            except ValueError:
                errors.append(f"{path}: 'review.date' must be a real YYYY-MM-DD date, got {date!r}")

    kind = data.get("kind")
    expect = data.get("expect")
    diagnostic = kind == "diagnostic"
    if not diagnostic:
        errors.append(f"{path}: unsupported golden kind {kind!r}; S0 accepts diagnostic")
        return errors
    if not isinstance(expect, dict):
        errors.append(f"{path}: diagnostic golden needs an 'expect' mapping")
        return errors
    _reject_unknown(expect, ALLOWED_EXPECT_DIAG, "expect", errors, path)
    stage = expect.get("stage")
    if not nonempty_string(stage):
        errors.append(f"{path}: missing or empty 'expect.stage'")
    code = expect.get("code")
    if not nonempty_string(code):
        errors.append(f"{path}: missing or empty 'expect.code'")
    elif not re.fullmatch(r"TC_(?:CE|RE)_[A-Z0-9_]+", code):
        errors.append(f"{path}: 'expect.code' is not a TC_CE_* / TC_RE_* code: {code!r}")
    elif code_map is not None:
        entry = code_map.get(code)
        if entry is None:
            errors.append(f"{path}: 'expect.code' is not in the frozen 80-code table: {code!r}")
        elif nonempty_string(stage) and not stage_allowed(entry, stage):
            errors.append(
                f"{path}: code {code!r} cannot be reported in stage {stage!r} "
                f"(table stage={entry.get('stage')!r}, "
                f"report_stage={entry.get('report_stage')!r})"
            )
    primary = expect.get("primary")
    if not isinstance(primary, dict):
        errors.append(f"{path}: missing 'expect.primary' mapping")
    else:
        _reject_unknown(primary, {"file", "line", "column"}, "expect.primary", errors, path)
        for key in ("line", "column"):
            value = primary.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value < 1:
                errors.append(f"{path}: 'expect.primary.{key}' must be a positive integer")
        if "file" in primary and not nonempty_string(primary["file"]):
            errors.append(f"{path}: 'expect.primary.file' must be non-empty")
    related = expect.get("related", [])
    if not isinstance(related, list):
        errors.append(f"{path}: 'expect.related' must be a list")
    else:
        for index, item in enumerate(related):
            if not isinstance(item, dict):
                errors.append(f"{path}: 'expect.related[{index}]' must be a mapping")
                continue
            _reject_unknown(item, {"file", "line", "column"}, f"expect.related[{index}]", errors, path)
            for key in ("line", "column"):
                value = item.get(key)
                if not isinstance(value, int) or isinstance(value, bool) or value < 1:
                    errors.append(
                        f"{path}: 'expect.related[{index}].{key}' must be a positive integer"
                    )
    diagnostics = expect.get("diagnostics")
    if diagnostics not in {"exactly_one", "exactly-one", 1}:
        errors.append(
            f"{path}: 'expect.diagnostics' must be exactly_one, got {diagnostics!r}"
        )
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate golden YAML files.")
    parser.add_argument("root", nargs="?", type=Path, default=HERE,
                        help=f"golden root (default: {HERE})")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if not root.is_dir():
        print(f"ERROR: golden root missing: {root}", file=sys.stderr)
        return 1

    try:
        code_map = load_code_map()
    except (OSError, ValueError) as exc:
        print(f"ERROR: cannot load frozen code table: {exc}", file=sys.stderr)
        return 1
    record = None
    if root == HERE:
        try:
            record = load_review_record()
        except (OSError, ValueError) as exc:
            print(f"ERROR: cannot load golden review record: {exc}", file=sys.stderr)
            return 1
    files = sorted(root.rglob("*.golden.yaml"))
    errors = []
    ids = {}
    for path in files:
        file_errors = validate_file(path, root, code_map)
        errors.extend(file_errors)
        try:
            data = parse_yaml(path.read_text(encoding="utf-8"))
            if record is not None and isinstance(data, dict):
                rel = path.resolve().relative_to(root.resolve()).as_posix()
                entry = record.get(rel)
                if entry is None:
                    errors.append(f"{path}: no entry in review-record.md")
                else:
                    review = data.get("review") if isinstance(data.get("review"), dict) else {}
                    for key in ("reviewer", "status", "date"):
                        if entry.get(key) != review.get(key):
                            errors.append(
                                f"{path}: review.{key} {review.get(key)!r} != "
                                f"review-record {entry.get(key)!r}"
                            )
                    if entry.get("reviewer_independent") is not True:
                        errors.append(f"{path}: review-record reviewer_independent is not true")
            golden_id = data.get("id") if isinstance(data, dict) else None
            if nonempty_string(golden_id):
                if golden_id in ids:
                    errors.append(
                        f"{path}: duplicate golden id {golden_id!r} (also in {ids[golden_id]})"
                    )
                else:
                    ids[golden_id] = path
        except (OSError, YamlError):
            pass

    if errors:
        print(f"FAIL: {len(errors)} golden validation error(s)", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"OK: {len(files)} golden file(s) validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
