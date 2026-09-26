#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_grammar_stage.py — 语法受限类型位置 ↔ 声明诊断阶段一致性检查（防回潮）

背景：附录 A 的部分产生式把**显式类型参数槽**限定为受约束的非终结符
（`int_type` / `bool_type` / `scalar_type` / `ref_pointee_type` /
`addr_pointee_type` / `memblock_element_type` / `int_type | float_type`）。
按语言标准 §1.3「语法阶段拒绝」与附录 A.3「类型非终结符的约束力」，这类槽位上的
类型实参不合法时**不能在语法阶段通过**，因此必须按 `TC_CE_SYNTAX` 报告，不得改报
静态语义码（`TC_CE_TYPE_MISMATCH` 等）。「首个规范诊断及其错误码」是稳定接口
（§1.3、§11），因此这类漂移必须在门禁上拦住。

本脚本做三件事：
  1. 从语言标准附录 A 的 EBNF 解析「受限类型槽」的产生式清单，断言解析结果非空
     且数量合理（防止解析失效导致检查静默通过）；
  2. **覆盖检查**：对每个受限位置，要求两份草案都有一处把它明确写成语法拒绝
     （`TC_CE_SYNTAX` / 「语法拒绝」）；
  3. **回潮检查**：断言历史上出现过的错误配对措辞不再出现。

已知边界：这是**锚点级**检查，不做语义级推理——它保证「每个受限位置都有明确声明」
与「已修复的错误措辞不再回潮」，不替代人工逐条对账（新增受限位置应同步更新清单）。

两份草案缺失时跳过。用法：python3 scripts/sync/check_grammar_stage.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

LANG_STD = "docs/TC语言标准设计说明书-0.0.45.md"
COMPILER_STD = "docs/TC编译器标准设计说明书-0.0.45.md"

TYPE_TOKENS = {
    "int_type", "float_type", "bool_type", "scalar_type",
    "ref_pointee_type", "addr_pointee_type", "memblock_element_type",
    "type", "value_type", "field_type", "return_type", "addr_access_type",
}
RESTRICTED = {
    "int_type", "float_type", "bool_type", "scalar_type",
    "ref_pointee_type", "addr_pointee_type", "memblock_element_type",
    "int_type|float_type", "addr_access_type",
}
# 类型清单本身的定义式不是「指令形态」，不参与位置清单
TYPE_DEFS = {
    "type", "value_type", "field_type", "return_type", "ref_type", "addr_type",
    "ref_pointee_type", "addr_pointee_type", "memblock_element_type",
    "sized_memblock_type", "scalar_type", "addr_access_type",
}
SYNTAX_MARKERS = ("TC_CE_SYNTAX", "语法拒绝")

# 覆盖清单：(锚点文字, 说明, 适用文档)；同处还必须出现语法拒绝标记
COVERAGE = (
    ("`addr<ref<U>>` / `addr<struct>` / `addr<memblock>`", "addr 所指类型（编译器标准）", "编译器标准"),
    ("addr<ref<U>> / addr<struct> / addr<memblock> 不在本产生式中", "addr 所指类型（语言标准 附录 A）", "语言标准"),
    ("不在 `addr_pointee_type` 中 → **语法拒绝**", "addr 所指类型（语言标准 §3.11.1）", "语言标准"),
    ("`T` 为浮点时报 `TC_CE_SYNTAX`", "位运算/移位（语言标准）", "语言标准"),
    ("由附录 A 的 `addr_access_type`", "addr 访问/算术的类型参数", "语言标准"),
    ("`T` 由 `addr_access_type` 限定", "addr 访问/算术的类型参数（编译器标准）", "编译器标准"),
    ("`bits_of(void)` 属**语法拒绝**", "`bits_of` 的 `void` 类型参数", "语言标准"),
    ("或作为 `addr<T>` 的所指类型", "§2.7 的 `void` 总则豁免 `addr<void>`", "语言标准"),
    ("位运算与移位的显式类型参数只接受整数类型", "位运算/移位（编译器标准）", "编译器标准"),
    ("`eq(bool, ...)` 或带 `ieee` 的形态属语法拒绝", "比较运算（语言标准）", "语言标准"),
    ("其余类型参数与 `bool` 属**语法拒绝**", "比较运算（编译器标准）", "编译器标准"),
    ("逻辑产生式要求类型参数为 `bool`", "逻辑运算（语言标准）", "语言标准"),
    ("逻辑产生式只接受 `bool` 类型参数", "逻辑运算（编译器标准）", "编译器标准"),
    ("该形态不符合附录 A 的 `memblock_element_type`，属**语法拒绝**", "memblock 元素类型（语言标准）", "语言标准"),
    ("写入 `void` 属**语法拒绝**", "memblock 显式类型参数的 `void`", "两份"),
    ("目标类型参数不在附录 A 的 `int_type` 中", "`truncate` 目标类型（语言标准）", "语言标准"),
    ("目标类型参数非整数属**语法拒绝**", "`truncate` 目标类型（编译器标准）", "编译器标准"),
    ("写入 `void` 或 `addr<U>` 不符合附录 A 的产生式", "引用指令类型参数（语言标准）", "语言标准"),
    ("属**语法拒绝** `TC_CE_SYNTAX`（[语言标准 §3.10.1]", "引用指令类型参数（编译器标准）", "编译器标准"),
)

# 回潮清单：修复前的错误配对措辞（原文），不得再次出现
REGRESSIONS = (
    "浮点类型不得参与位运算 → `TC_CE_TYPE_MISMATCH`",
    "`ref<T>` / `addr<T>` 不得参与位运算 → `TC_CE_TYPE_MISMATCH`",
    "`addr<ref<U>>` / `addr<struct>` / `addr<memblock>` → `TC_CE_TYPE_MISMATCH`",
    "由 §3.11.1 静态语义拒绝",
    "`ref<U>` / `struct` / `memblock` 报 `TC_CE_TYPE_MISMATCH`",
    "不接受非整数类型",
)


def read(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_productions(gram):
    gram = re.sub(r"/\*.*?\*/", "", gram, flags=re.S)
    prods, lines, i = {}, gram.split("\n"), 0
    while i < len(lines):
        m = re.fullmatch(r"\s*([a-z_][a-z0-9_]*)\s*", lines[i])
        if m and i + 1 < len(lines) and lines[i + 1].lstrip().startswith("="):
            name, buf, j = m.group(1), [], i + 1
            while j < len(lines):
                buf.append(lines[j])
                if ";" in lines[j]:
                    break
                j += 1
            prods[name] = " ".join(buf).split("=", 1)[1]
            i = j + 1
            continue
        m2 = re.match(r"\s*([a-z_][a-z0-9_]*)\s*=(.*)", lines[i])
        if m2:
            name, buf, j = m2.group(1), [m2.group(2)], i
            while ";" not in buf[-1] and j + 1 < len(lines):
                j += 1
                buf.append(lines[j])
            prods[name] = " ".join(buf)
            i = j + 1
            continue
        i += 1
    return prods


def restricted_positions(prods):
    """→ [(production, slot)]，仅指令形态（排除类型清单定义式）。"""
    out = []
    for name, rhs in prods.items():
        if name in TYPE_DEFS:
            continue
        if re.search(r"\(\s*int_type\s*\|\s*float_type\s*\)", rhs):
            out.append((name, "int_type|float_type"))
            continue
        for tok in re.findall(r"[a-z_][a-z0-9_]*", rhs):
            if tok in TYPE_TOKENS:
                if tok in RESTRICTED:
                    out.append((name, tok))
                break
    return sorted(out)


def main():
    lang = read(LANG_STD)
    comp = read(COMPILER_STD)
    if lang is None or comp is None:
        print("check_grammar_stage: 0.0.45 草案缺失，跳过")
        return

    blocks = re.findall(r"```ebnf\n(.*?)```", lang, re.S)
    prods = parse_productions("\n".join(blocks))
    positions = restricted_positions(prods)
    if len(blocks) < 4 or len(prods) < 150 or len(positions) < 8:
        print(f"check_grammar_stage: 附录 A 解析异常"
              f"（ebnf 块 {len(blocks)}、产生式 {len(prods)}、受限槽 {len(positions)}）")
        sys.exit(1)

    failures = []
    docs = {"语言标准": lang, "编译器标准": comp}
    for anchor, desc, scope in COVERAGE:
        targets = list(docs) if scope == "两份" else [scope]
        for doc_name in targets:
            hit = [ln for ln in docs[doc_name].split("\n")
                   if anchor in ln and any(m in ln for m in SYNTAX_MARKERS)]
            if not hit:
                failures.append(f"{doc_name}：{desc} 缺少「{anchor}」+ 语法拒绝声明")
    for bad in REGRESSIONS:
        for doc_name, doc in (("语言标准", lang), ("编译器标准", comp)):
            if bad in doc:
                failures.append(f"{doc_name}：错误配对措辞回潮 → {bad}")

    if failures:
        print("check_grammar_stage: 受限类型位置的诊断阶段不一致：")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(f"check_grammar_stage: 附录 A 受限类型槽 {len(positions)} 处；"
          f"覆盖锚点 {len(COVERAGE)} 项、回潮模式 {len(REGRESSIONS)} 项全部通过")


if __name__ == "__main__":
    main()
