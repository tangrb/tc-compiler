#!/usr/bin/env python3
"""Documentation cleanup gate.

Wrap-up must leave no process/historical narrative in the user-facing docs:
round-by-round audits, errata logs, commit/environment pins, iterative changelog
entries and similar material belong to git history, not to the branch.  This
check is deliberately small and deterministic; normative documents (the
standard, design and test plan) are not scanned.  AGENTS.md defines the policy
and therefore names the forbidden categories, so it is not scanned itself.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

SCANNED = [
    "README.md",
    "README.en.md",
    "CHANGELOG.md",
    "CHANGELOG.en.md",
    "CONTRIBUTING.md",
    "CONTRIBUTING.en.md",
    "docs/README.md",
    "docs/README.en.md",
    "docs/gates/README.md",
    "docs/gates/README.en.md",
    "src/README.md",
    "src/CONTRACTS.md",
    "tests/README.md",
    "tests/unit/README.md",
    "tests/conformance/README.md",
    "tests/traceability/README.md",
]

SCANNED += [
    str(path.relative_to(REPO))
    for path in sorted((REPO / "docs" / "gates").glob("TC-G*门禁报告-*.md"))
]

FORBIDDEN = [
    (r"首轮", "round-history wording"),
    (r"复核通过", "review-history wording"),
    (r"reviewed commit", "commit pin"),
    (r"evidence version", "evidence-version pin"),
    (r"\bB1-\d+", "audit finding id"),
    (r"A1[–-]A9", "audit finding list"),
    (r"本轮实测", "per-round measurement log"),
    (r"审计发现", "audit-finding log"),
    (r"修复后", "fix-history narrative"),
    (r"过程性文档", "process-document wording"),
]

MAX_GATE_LINES = 120


def main() -> int:
    failures = []
    for rel in SCANNED:
        path = REPO / rel
        if not path.is_file():
            failures.append(f"missing scanned document: {rel}")
            continue
        text = path.read_text(encoding="utf-8")
        for pattern, label in FORBIDDEN:
            for match in re.finditer(pattern, text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{rel}:{line}: {label}: {match.group(0)!r}")
    for gate in sorted((REPO / "docs" / "gates").glob("TC-G*门禁报告-*.md")):
        lines = len(gate.read_text(encoding="utf-8").splitlines())
        if lines > MAX_GATE_LINES:
            failures.append(
                f"{gate.relative_to(REPO)}: {lines} lines exceeds "
                f"{MAX_GATE_LINES}; keep gate records current and concise"
            )
    for changelog in ("CHANGELOG.md", "CHANGELOG.en.md"):
        path = REPO / changelog
        if not path.is_file():
            continue
        versions = re.findall(r"^## \[([^\]]+)\]", path.read_text(encoding="utf-8"),
                              re.MULTILINE)
        if len(versions) != len(set(versions)):
            failures.append(f"{changelog}: duplicate release headings {versions}")
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"docs cleanup: {len(failures)} failure(s)")
        return 1
    print("docs cleanup: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
