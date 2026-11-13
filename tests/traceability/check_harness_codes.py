#!/usr/bin/env python3
"""Cross-check the harness's frozen diagnostic table against codes.json.

The harness deliberately stays standalone (no documentation dependency), so it
keeps a copied 80-code table.  This check prevents that copy from drifting from
the machine-readable table used by the C table generator and coverage matrix.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tests" / "harness"
CODES = ROOT / "tests" / "traceability" / "codes.json"


def main() -> int:
    sys.path.insert(0, str(HARNESS))
    try:
        import diag_codes  # type: ignore
    except Exception as exc:  # pragma: no cover - import failure is the failure
        print(f"ERROR: cannot import harness diag_codes: {exc}", file=sys.stderr)
        return 1
    try:
        data = json.loads(CODES.read_text(encoding="utf-8"))
    except OSError as exc:
        print(f"ERROR: cannot read {CODES}: {exc}", file=sys.stderr)
        return 1
    expected = {
        c["code"]: (c["print_name"], c["stage"]) for c in data["codes"]
    }
    actual = {code: tuple(value) for code, value in diag_codes.DIAGNOSTIC_CODES.items()}
    errors: list[str] = []
    for code in sorted(set(expected) - set(actual)):
        errors.append(f"harness table missing {code}")
    for code in sorted(set(actual) - set(expected)):
        errors.append(f"harness table has extra {code}")
    for code in sorted(set(expected) & set(actual)):
        if expected[code] != actual[code]:
            errors.append(
                f"{code}: harness {actual[code]!r} != codes.json {expected[code]!r}"
            )
    if len(actual) != 80:
        errors.append(f"harness table has {len(actual)} codes, expected 80")
    for code in sorted(diag_codes.IMPORT_CODES):
        if not diag_codes.expected_stage_matches(code, "IMPORT"):
            errors.append(f"{code}: harness does not accept IMPORT as its report stage")
        if diag_codes.expected_stage_matches(code, "SEM"):
            errors.append(f"{code}: harness incorrectly accepts SEM as its report stage")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("OK: harness diagnostic table matches codes.json (80 codes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
