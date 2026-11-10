#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_keywords.py — 0.0.46 重写版关键字/opcode/EBNF 一致性检查

检查：
  - §2.3 关键字表的语法关键字均能在 §13 EBNF 引号终端中找到；
  - §13 EBNF 的关键字形态终端均登记在 §2.3；
  - §2.3 指令表与 §6.2 opcode 表一致（含 copy）；
  - 标签声明使用 `label L:`，注解文法支持 `@readonly`。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANG_STD = "docs/TC语言标准-0.0.46.md"

SYNTAX_CATEGORIES = {"模式", "声明", "安全", "类型", "字面量", "命名空间"}
TABLE_ROW_RE = re.compile(r"^\|\s*([^|]+?)\s*\|(.*)\|\s*$")
TOKEN_RE = re.compile(r"`([^`]+)`")
KEYWORD_LIKE_RE = re.compile(r"^(?:#[A-Za-z]+|[A-Za-z][A-Za-z0-9_]*)$")
CONTEXTUAL_TERMINALS = {"padding"}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def section(text, start_re, end_re):
    m = re.search(start_re + r"(.*?)(?=" + end_re + r"|\Z)", text, re.S | re.M)
    return m.group(1) if m else ""


def extract_keyword_table(text):
    body = section(text, r"^###\s+2\.3\s+", r"\n###\s+")
    syntax, ops, all_tokens = set(), set(), []
    for line in body.splitlines():
        m = TABLE_ROW_RE.match(line)
        if not m:
            continue
        category, cells = m.group(1).strip(), m.group(2)
        tokens = TOKEN_RE.findall(cells)
        if not tokens:
            continue
        all_tokens.extend(tokens)
        if category in SYNTAX_CATEGORIES:
            syntax.update(tokens)
        elif category == "指令":
            ops.update(tokens)
    return syntax, ops, all_tokens


def extract_ebnf_terminals(text):
    body = section(text, r"^##\s+13\.\s+", r"\n##\s+")
    terminals = set()
    for block in re.findall(r"```ebnf\n(.*?)```", body, re.S):
        for token in re.findall(r'"([^"]*)"', block):
            if KEYWORD_LIKE_RE.match(token) and len(token) > 1:
                terminals.add(token)
    return terminals - CONTEXTUAL_TERMINALS


def extract_opcode_bases(text):
    body = section(text, r"^###\s+6\.2\s+", r"\n###\s+6\.3\s+")
    ops = set()
    for line in body.splitlines():
        m = TABLE_ROW_RE.match(line)
        if not m:
            continue
        first_cell = m.group(1)
        for token in TOKEN_RE.findall(first_cell):
            ops.add(token.split(".", 1)[0])
    return ops


def main():
    path = os.path.join(ROOT, LANG_STD)
    if not os.path.exists(path):
        print(f"check_keywords: 语言标准缺失：{LANG_STD}")
        sys.exit(1)
    text = read(path)
    syntax, ops, all_tokens = extract_keyword_table(text)
    terminals = extract_ebnf_terminals(text)
    table_ops = extract_opcode_bases(text)

    failures = []
    duplicates = sorted({t for t in all_tokens if all_tokens.count(t) > 1})
    if duplicates:
        failures.append("§2.3 重复出现：" + ", ".join(duplicates))
    missing_in_ebnf = sorted(syntax - terminals)
    if missing_in_ebnf:
        failures.append("§2.3 有、§13 EBNF 无：" + ", ".join(missing_in_ebnf))
    missing_in_table = sorted(terminals - syntax)
    if missing_in_table:
        failures.append("§13 EBNF 有、§2.3 无：" + ", ".join(missing_in_table))
    missing_in_6_2 = sorted(ops - table_ops)
    if missing_in_6_2:
        failures.append("§2.3 指令有、§6.2 opcode 无：" + ", ".join(missing_in_6_2))
    missing_in_2_3 = sorted(table_ops - ops)
    if missing_in_2_3:
        failures.append("§6.2 opcode 有、§2.3 指令无：" + ", ".join(missing_in_2_3))
    if "copy" not in ops or "copy" not in table_ops:
        failures.append("copy 未同时出现在 §2.3 指令表与 §6.2 opcode 表")
    if not re.search(r'label_def\s*=\s*"label"', text):
        failures.append("§13 EBNF 的 label_def 未使用 \"label\" 关键字")
    if not re.search(r'annotation_name\s*=\s*IDENT\s*\|\s*"unsafe"\s*\|\s*"readonly"', text):
        failures.append("§13 EBNF 的 annotation_name 未支持 @readonly")

    if failures:
        print("check_keywords: 关键字/opcode/EBNF 不一致：")
        for item in failures:
            print(f"  - {item}")
        sys.exit(1)

    print(
        f"check_keywords: §2.3 语法关键字 {len(syntax)} 个、指令 {len(ops)} 个，"
        "与 §13 EBNF / §6.2 opcode 表一致"
    )


if __name__ == "__main__":
    main()
