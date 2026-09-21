#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_doc_layering.py — 设计文档 ↔ 过程文档分层引用检查（防回潮）

规则（规范性）：
  **设计性质文档不得引用过程性文档。**

  - 设计性质文档（本脚本检查的对象，共 7 份）：
      语言标准、编译器标准、VM 详设、VM 命令行参考、AOT 详设、Embed 详设、libtc 设计说明书
    它们只允许引用同为设计/规范的文档，以及仓库内的源码与测试文件。
  - 过程性文档（设计文档中禁止出现）：
      符合性审计报告、实现同步台账、修订记录与兼容性说明、CHANGELOG、release-checklist、
      发布标签 / `git show` 追溯指引、文档地图与导航（docs/README）、Agent 工具文档
      （AGENTS.md、.cursor/**、kg-*.md、test-map.md 等）。
  - 反向允许：过程性文档可以引用设计文档（本脚本不检查过程性文档）。

检查两类出现形式：
  1. 指向过程性文档的 markdown 链接目标；
  2. 正文中按名指名过程性文档（含反引号包裹的文件名）。

用法：python3 scripts/sync/check_doc_layering.py
不一致时打印差异并以非零退出。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ---- 被检查的设计性质文档 -------------------------------------------------
DESIGN_DOCS = [
    "docs/TC语言标准设计说明书-0.0.44.md",
    "docs/TC编译器标准设计说明书-0.0.44.md",
    "docs/TC-VM详细设计说明书-0.0.44.md",
    "docs/TC-VM命令行参考-0.0.44.md",
    "docs/TC-AOT详细设计说明书-0.0.44.md",
    "docs/TC-Embed详细设计说明书-0.0.44.md",
    "docs/libtc设计说明书-0.0.44.md",
]

# ---- 允许被设计文档引用的目标（同为设计/规范的文档，按 basename 前缀） ----
ALLOWED_DOC_PREFIXES = (
    "TC语言标准设计说明书",
    "TC编译器标准设计说明书",
    "TC-VM详细设计说明书",
    "TC-VM命令行参考",
    "TC-AOT详细设计说明书",
    "TC-Embed详细设计说明书",
    "libtc设计说明书",
)

# ---- 允许被设计文档引用的非文档路径前缀（源码 / 测试 / 示例） -------------
ALLOWED_PATH_PREFIXES = ("../src/", "../tests/", "../examples/", "src/", "tests/", "examples/")

# ---- 过程性文档的具名标记（正文指名即违规） ------------------------------
PROCESS_MARKERS = (
    "语言标准符合性审计报告",
    "符合性审计报告",
    "符合性检查分析报告",
    "检查分析报告",
    "一致性勘误清单",
    "勘误清单",
    "符合性整改进度台账",
    "整改进度台账",
    "实现同步台账",
    "符合性差异",
    "修订记录与兼容性说明",
    "CHANGELOG",
    "release-checklist",
    "遗留问题清零计划",
    "清零计划",
    "修复计划",
    "过程文档",
    "过程性文档",
    "发布标签",
    "git show",
    "AGENTS.md",
    ".cursor/",
    "test-map.md",
    "kg-embed.md",
)

# ---- 已废弃的带版本号文档名（正文指名即违规） ----------------------------
# 现行设计文档名统一带 `-0.0.44` 后缀，属正常；此处只拦截**旧版本**的档名
# （如 `libtc-api-0.0.42.md`），即版本号不等于现行 0.0.44 者。
STALE_DOCNAME_RE = re.compile(
    r"[A-Za-z\u4e00-\u9fff_\-]*-(?:v?0\.0\.(?!44\b)[0-9]+)\.md"
)

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def read(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def is_allowed_target(target):
    """链接目标是否属于允许范围（设计/规范文档，或源码/测试文件，或站外/邮件）。"""
    t = target.split("#")[0].strip()
    if not t:
        return True
    if t.startswith(("http://", "https://", "mailto:")):
        return True
    if t.startswith(ALLOWED_PATH_PREFIXES):
        return True
    base = os.path.basename(t)
    if base and base.startswith(ALLOWED_DOC_PREFIXES):
        return True
    return False


def main():
    failures = []
    checked = 0

    for rel in DESIGN_DOCS:
        text = read(rel)
        if text is None:
            failures.append(f"{rel}: 文件缺失（设计文档清单与仓库不一致）")
            continue
        checked += 1
        lines = text.split("\n")

        # 1) markdown 链接目标
        for i, line in enumerate(lines, 1):
            for m in LINK_RE.finditer(line):
                target = m.group(1)
                if not is_allowed_target(target):
                    failures.append(
                        f"{rel}:{i}: 设计文档链接到非设计文档 {target!r}"
                    )

        # 2) 正文具名指名过程性文档
        for i, line in enumerate(lines, 1):
            for marker in PROCESS_MARKERS:
                if marker in line:
                    failures.append(
                        f"{rel}:{i}: 设计文档指名过程性文档标记 {marker!r}"
                    )
            for m in STALE_DOCNAME_RE.finditer(line):
                failures.append(
                    f"{rel}:{i}: 设计文档指名已废弃的带版本号文档名 {m.group(0)!r}"
                )

    if failures:
        print("check_doc_layering: 设计文档引用了过程性文档：")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)

    print(f"check_doc_layering: {checked} 份设计文档均未引用过程性文档")


if __name__ == "__main__":
    main()
