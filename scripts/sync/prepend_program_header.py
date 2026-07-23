#!/usr/bin/env python3
"""Prepend #program to .tc fixtures that lack a module header (Phase 2)."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"


def has_module_header(text: str) -> bool:
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(";"):
            continue
        return stripped in ("#program", "#lib") or stripped.startswith("#program") or stripped.startswith("#lib")
    return False


def main() -> int:
    changed = 0
    for path in sorted(TESTS.rglob("*.tc")):
        text = path.read_text(encoding="utf-8")
        if has_module_header(text):
            continue
        path.write_text("#program\n" + text, encoding="utf-8")
        changed += 1
        print(f"updated {path.relative_to(ROOT)}")
    print(f"done: {changed} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
