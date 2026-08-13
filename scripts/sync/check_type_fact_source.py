#!/usr/bin/env python3
"""Guard: type fact-source fields must not use bare TcTypeTag.

TcTypeTag is a projection (dispatch / width predicates). Complete type facts
must be stored as const TcType* (or owned TcType during parse).

This script scans src/ for struct/typedef field declarations typed as
TcTypeTag that look like type-fact holders. A small whitelist covers
legitimate projections (Embed host ABI, lexer tokens, etc.).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"

# path relative to src/ → set of allowed field names (or "*" for whole file)
WHITELIST: dict[str, set[str]] = {
    # Embed host ABI: scalar projection for C callers
    "vm/embed/tc_embed.h": {"type"},
    # Lexer token payload for type name tokens
    "vm/lexer/tc_lexer.h": {"*"},
    "vm/lexer/tc_lexer.c": {"*"},
    # AOT codegen scratch: return-type tag projection
    "aot/tc_aot_codegen_internal.h": {"current_return_type"},
}

# Match: TcTypeTag field_name;  inside structs (heuristic)
FIELD_RE = re.compile(
    r"^\s*TcTypeTag\s+(\w+)\s*;",
    re.MULTILINE,
)

# Discriminant / projection field names always allowed on any type
ALWAYS_OK = {"tag", "type_tag"}


def rel(path: Path) -> str:
    return str(path.relative_to(SRC)).replace("\\", "/")


def allowed(path: str, field: str) -> bool:
    if field in ALWAYS_OK:
        return True
    rules = WHITELIST.get(path)
    if not rules:
        return False
    return "*" in rules or field in rules


def main() -> int:
    bad: list[str] = []
    for path in sorted(SRC.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        r = rel(path)
        for m in FIELD_RE.finditer(text):
            field = m.group(1)
            if allowed(r, field):
                continue
            bad.append(f"{r}: field '{field}' has type TcTypeTag (use const TcType* for facts)")

    if bad:
        print("check_type_fact_source: FAIL", file=sys.stderr)
        for line in bad:
            print(f"  {line}", file=sys.stderr)
        print(
            "\nTcTypeTag may only appear as a projection (whitelist) or local variable/param.",
            file=sys.stderr,
        )
        return 1
    print("check_type_fact_source: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
