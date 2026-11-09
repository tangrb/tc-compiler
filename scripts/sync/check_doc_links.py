#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_doc_links.py — Markdown 相对链接存在性检查（防断链）

扫描仓库内 Markdown 文件中的相对链接目标，按链接所在文件目录解析并检查
目标是否存在。跳过外部链接、纯锚点、图片链接，以及围栏代码块与行内代码
中的伪链接。

用法：python3 scripts/sync/check_doc_links.py
断链时打印位置并以非零退出。
"""

import os
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FENCE_RE = re.compile(r"```.*?```", re.S)
INLINE_CODE_RE = re.compile(r"`[^`\n]*`")
LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]*)\)")
SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")

SKIP_DIRS = {".git", "build", "node_modules", ".qoder"}


def strip_code(text):
    """移除围栏代码块与行内代码，避免把代码示例中的方括号当链接。"""
    text = FENCE_RE.sub("", text)
    return INLINE_CODE_RE.sub("", text)


def discover_markdown():
    try:
        out = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "-z", "--", "*.md"],
            stderr=subprocess.DEVNULL,
        )
        files = [p.decode("utf-8") for p in out.split(b"\0") if p]
        files = [p for p in files if os.path.exists(os.path.join(ROOT, p))]
        if files:
            return sorted(files)
    except (OSError, subprocess.CalledProcessError):
        pass

    files = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith(".md"):
                files.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
    return sorted(files)


def resolve_target(source_rel, target):
    """返回 (规范路径, 原始目标) 或 None（无需检查）。"""
    target = target.strip()
    if not target:
        return None
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1].strip()
    if not target or target.startswith("#"):
        return None
    if SCHEME_RE.match(target):
        return None

    file_part = unquote(target.split("#", 1)[0])
    if not file_part or file_part.startswith("{"):
        return None
    if file_part.startswith("/"):
        path = os.path.join(ROOT, file_part.lstrip("/"))
    else:
        path = os.path.join(ROOT, os.path.dirname(source_rel), file_part)
    return os.path.normpath(path)


def main():
    files = discover_markdown()
    failures = []
    links = 0

    for rel in files:
        path = os.path.join(ROOT, rel)
        with open(path, encoding="utf-8") as f:
            text = strip_code(f.read())
        for lineno, line in enumerate(text.split("\n"), 1):
            for match in LINK_RE.finditer(line):
                target = match.group(1).strip()
                resolved = resolve_target(rel, target)
                if resolved is None:
                    continue
                links += 1
                if not os.path.exists(resolved):
                    shown = os.path.relpath(resolved, ROOT)
                    failures.append(f"{rel}:{lineno}: {target!r} → {shown} 不存在")

    if failures:
        print("check_doc_links: 以下相对链接无法解析：")
        for item in failures:
            print(f"  - {item}")
        sys.exit(1)

    print(f"check_doc_links: {len(files)} 个 Markdown 文件，相对链接 {links} 处，断链 0 处")


if __name__ == "__main__":
    main()
