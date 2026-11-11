#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_section_refs.py — 规范文本内部 § 交叉引用检查（防重构失联）

用法：
    python3 scripts/sync/check_section_refs.py [文档路径]

默认检查 docs/TC语言及编译器标准-0.0.46.md：

  1. 收集锚点：`## 1.` / `### 3.10` / `#### 6.8.1` 形式的标题编号，
     以及 `**3.10.3.2 标题**` 形式的粗体子节编号，附录 `A.2` / `B.6`；
  2. 收集正文中所有 `§x.y[.z]` 引用；
  3. 报告没有对应锚点的引用。

不检查语义，只检查「引用的编号是否存在」。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_DOC = "docs/TC语言及编译器标准-0.0.46.md"

HEADING_RE = re.compile(r"^#{2,4}\s+(\d+(?:\.\d+)*)[.、\s]")
BOLD_RE = re.compile(r"^\*\*(\d+(?:\.\d+)+)\s")
APPENDIX_RE = re.compile(r"^##\s+附录\s+([AB])")
APPENDIX_SUB_RE = re.compile(r"^###\s+([AB]\.\d+)\s")
REF_RE = re.compile(r"§\s?([0-9]+(?:\.[0-9]+)*|[AB]\.\d+)")
LANG_REF_RE = re.compile(r"\[语言标准 §\s?([0-9]+(?:\.[0-9]+)*)\]")


def collect_anchors(lines):
    anchors = set()
    for line in lines:
        for regex in (HEADING_RE, BOLD_RE, APPENDIX_SUB_RE):
            m = regex.match(line.strip())
            if m:
                anchors.add(m.group(1))
        m = APPENDIX_RE.match(line.strip())
        if m:
            anchors.add(m.group(1))
    return anchors


def main():
    rel = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DOC
    path = rel if os.path.isabs(rel) else os.path.join(ROOT, rel)
    if not os.path.exists(path):
        print(f"check_section_refs: 文档缺失：{rel}")
        sys.exit(1)

    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")

    anchors = collect_anchors(lines)

    # 跨文档：[语言标准 §x] 旧式引用按标准语言篇锚点校验
    lang_anchors = None
    if os.path.abspath(path) != os.path.abspath(os.path.join(ROOT, DEFAULT_DOC)):
        lang_path = os.path.join(ROOT, DEFAULT_DOC)
        if os.path.exists(lang_path):
            with open(lang_path, encoding="utf-8") as f:
                lang_anchors = collect_anchors(f.read().split("\n"))

    failures = []
    refs = 0
    for i, line in enumerate(lines, 1):
        for m in REF_RE.finditer(line):
            refs += 1
            if m.group(1) not in anchors and not (
                lang_anchors is not None and m.group(1) in lang_anchors
            ):
                failures.append((i, m.group(1), line.strip()[:120]))
        if lang_anchors is not None:
            for m in LANG_REF_RE.finditer(line):
                refs += 1
                if m.group(1) not in lang_anchors:
                    failures.append((i, "语言篇 §"+m.group(1), line.strip()[:120]))

    if failures:
        print(f"check_section_refs: {len(failures)} 处 § 引用无对应锚点：")
        for i, ref, line in failures:
            print(f"  - {rel}:{i}: §{ref}")

    print(f"check_section_refs: 锚点 {len(anchors)} 个，引用 {refs} 处，失效 {len(failures)} 处")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
