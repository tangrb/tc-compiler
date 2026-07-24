#!/usr/bin/env python3
"""为缺少模块头的 .tc 测试固件前置 #program（Phase 2 迁移辅助）。

Phase 2 起每个源文件必须以 #program 或 #lib 开头。
本脚本扫描 tests/ 下全部 .tc：若首个非空非注释行不是模块指令，
则在文件开头插入一行 `#program`，使既有固件通过严格头检查。

用法：
  python3 scripts/sync/prepend_program_header.py
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"


def has_module_header(text: str) -> bool:
    """判断源文本是否已有 #program / #lib 模块头（跳过空行与 ; 注释）。"""
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(";"):
            continue
        return (
            stripped in ("#program", "#lib")
            or stripped.startswith("#program")
            or stripped.startswith("#lib")
        )
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
