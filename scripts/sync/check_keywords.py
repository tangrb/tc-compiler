#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_keywords.py — §2.7 关键字表与附录 A EBNF 终端一致性检查

从语言标准 §2.7 的关键字代码块提取保留字，从附录 A 的全部 EBNF 代码块
提取引号终端，取其中关键字形态的多字符标识符后双向比对，防止新增产生式
终端或关键字表漏同步。

`padding` 是 `@padding(N)` 属性名，不是保留字，故排除。

用法：python3 scripts/sync/check_keywords.py
不一致时打印差集并以非零退出。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANG_STD = "docs/TC语言标准-0.0.46.md"

KEYWORD_BLOCK_RE = re.compile(
    r"^###\s+2\.7\s+关键字.*?```text\n(.*?)\n```", re.S | re.M)
APPENDIX_A_RE = re.compile(r"##\s+附录\s+A.*?(?=\n##\s+附录\s+B)", re.S)
EBNF_BLOCK_RE = re.compile(r"```ebnf\n(.*?)```", re.S)
QUOTED_RE = re.compile(r'"([^"]*)"')
KEYWORD_LIKE_RE = re.compile(r"^(?:#[A-Za-z]+|[A-Za-z][A-Za-z0-9_]*)$")
CONTEXTUAL_TERMINALS = {"padding"}


def main():
    path = os.path.join(ROOT, LANG_STD)
    if not os.path.exists(path):
        print(f"check_keywords: 语言标准缺失：{LANG_STD}")
        sys.exit(1)

    with open(path, encoding="utf-8") as f:
        text = f.read()

    m = KEYWORD_BLOCK_RE.search(text)
    if not m:
        print("check_keywords: 无法定位 §2.7 关键字代码块")
        sys.exit(1)
    tokens = re.findall(r"\S+", m.group(1))
    keywords = set(tokens)

    a = APPENDIX_A_RE.search(text)
    if not a:
        print("check_keywords: 无法定位附录 A")
        sys.exit(1)
    terminals = set()
    for block in EBNF_BLOCK_RE.findall(a.group(0)):
        for token in QUOTED_RE.findall(block):
            if KEYWORD_LIKE_RE.match(token) and len(token) > 1:
                terminals.add(token)
    terminals -= CONTEXTUAL_TERMINALS

    missing_in_a = sorted(keywords - terminals)
    missing_in_27 = sorted(terminals - keywords)
    duplicates = sorted({t for t in tokens if tokens.count(t) > 1})

    failures = []
    if duplicates:
        failures.append(f"§2.7 重复出现：{', '.join(duplicates)}")
    if missing_in_a:
        failures.append(f"§2.7 有、附录 A 无：{', '.join(missing_in_a)}")
    if missing_in_27:
        failures.append(f"附录 A 有、§2.7 无：{', '.join(missing_in_27)}")

    if failures:
        print("check_keywords: 关键字表与附录 A 终端不一致：")
        for item in failures:
            print(f"  - {item}")
        sys.exit(1)

    print(f"check_keywords: §2.7 保留字 {len(keywords)} 个，与附录 A EBNF 终端一致")


if __name__ == "__main__":
    main()
