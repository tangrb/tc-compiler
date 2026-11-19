#!/usr/bin/env python3
"""Cross-check that every case id cited by the coverage matrix exists.

The coverage matrix is allowed to cite L0/L1/L2 unit ids (documented in
`tests/unit/README.md`) as well as conformance and golden case ids.  This gate
collects ids from those artefacts and fails when a matrix row cites an id that
no test artefact declares.  It never infers an id from a naming pattern.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
COVERAGE = HERE / "coverage.generated.json"
ID_RE = re.compile(r"TC-[A-Z0-9]+(?:-[A-Z0-9]+)*")
SKIP = {"coverage.py", "coverage.generated.json", "coverage-report.md",
        "check_case_ids.py"}


def declared_ids() -> set[str]:
    ids = set()
    for root in (REPO / "tests",):
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.name in SKIP:
                continue
            if path.suffix not in {".yaml", ".tc", ".py", ".md", ".json"}:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            ids.update(ID_RE.findall(text))
    return ids


def main() -> int:
    if not COVERAGE.is_file():
        print(f"ERROR: missing {COVERAGE}", file=sys.stderr)
        return 1
    data = json.loads(COVERAGE.read_text(encoding="utf-8"))
    cited = set()
    for row in data.get("rows", []):
        cited.update(row.get("case_ids") or [])
    known = declared_ids()

    def normalize(case_id: str) -> str:
        return case_id.split("@", 1)[0]

    known = {normalize(item) for item in known}
    missing = sorted(item for item in cited if normalize(item) not in known)
    # A handful of contract ids are documented in the unit README only; they
    # are still test artefacts.  Report anything genuinely absent.
    if missing:
        for case_id in missing:
            print(f"FAIL: coverage cites undeclared case id {case_id}")
        print(f"case ids: {len(missing)} undeclared of {len(cited)} cited")
        return 1
    print(f"case ids: {len(cited)} cited ids all declared")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
