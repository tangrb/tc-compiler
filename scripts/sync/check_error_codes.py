#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_error_codes.py — 0.0.45 草案错误码表一致性检查（防双轨）

事实源：docs/TC语言标准设计说明书-0.0.45.md 附录 B（唯一权威码表）。
被检对象：docs/TC编译器标准设计说明书-0.0.45.md §11.4 各表（镜像附录 B）。

断言：
  1. 附录 B 唯一码 86 = 73 TC_CE_* + 13 TC_RE_*，且与正文声称值一致；
  2. 编译器 §11.4 各表语言码集合与附录 B 完全相等
     （实现资源码 TC_ERR_OUT_OF_MEMORY 单独登记，不计入语言码）；
  3. 码不重复（§11.4.6 对空值三码的交叉列出除外，且内容须一致）；
     打印名非空、全局唯一 —— 码 ↔ 打印名 ↔ 类别一一对应；
  4. 各小节「本节码数」与 §11.4 概览表声称值一致；
  5. 前缀与阶段自洽：TC_RE_* 标运行时，TC_CE_* 不含运行时。

两份 0.0.45 草案缺失时跳过（允许不携带草案的检出）。
用法：python3 scripts/sync/check_error_codes.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

LANG_STD = "docs/TC语言标准设计说明书-0.0.45.md"
COMPILER_STD = "docs/TC编译器标准设计说明书-0.0.45.md"

OOM_CODE = "TC_ERR_OUT_OF_MEMORY"
CODE_PAT = r"(?:TC_(?:CE|RE)_[A-Z_]+|" + OOM_CODE + r")"
ROW_RE = re.compile(r"^\|\s*`?(" + CODE_PAT + r")`?\s*\|(.*)$")
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")


def read(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def cells_of(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_appendix_b(text):
    """附录 B 各表的 (code, stage) 序列。"""
    m = re.search(r"## 附录 B：错误码速查表(.*?)(?=\n## |\Z)", text, re.S)
    if not m:
        return None
    rows = []
    for line in m.group(1).split("\n"):
        mm = ROW_RE.match(line)
        if mm:
            cells = cells_of(line)
            rows.append((mm.group(1), cells[1] if len(cells) > 1 else ""))
    return rows


def parse_compiler_sections(text):
    """§11.4 各小节的 (heading, [(code, name, stage), ...]) 与概览表文本。"""
    m = re.search(r"### 11\.4 标准错误码对照表(.*?)(?=\n## |\Z)", text, re.S)
    if not m:
        return None, None
    body = m.group(1)
    parts = re.split(r"(?m)^(### 11\.4\.\d[^\n]*)$", body)
    sections = []
    for i in range(1, len(parts), 2):
        head, sec = parts[i].strip(), parts[i + 1]
        rows = []
        for line in sec.split("\n"):
            mm = ROW_RE.match(line)
            if not mm:
                continue
            cells = cells_of(line)
            name = cells[1].strip("`") if len(cells) > 1 else ""
            stage = cells[3] if len(cells) > 3 else ""
            rows.append((mm.group(1), name, stage))
        sections.append((head, rows))
    return sections, body


def parse_section_claims(body):
    """概览表：§11.4.x → 声称的本节码数。"""
    claims = {}
    for line in body.split("\n"):
        m = re.match(r"^\|\s*§11\.4\.(\d)\s*(?:专用)?\s*\|\s*(\d+)", line)
        if m:
            claims[f"11.4.{m.group(1)}"] = int(m.group(2))
    return claims


def main():
    lang = read(LANG_STD)
    comp = read(COMPILER_STD)
    if lang is None or comp is None:
        print("check_error_codes: 0.0.45 草案缺失，跳过")
        return

    failures = []

    # ---- 1. 语言标准附录 B ------------------------------------------------
    ls_rows = parse_appendix_b(lang)
    if not ls_rows:
        failures.append("附录 B：未找到错误码速查表")
        ls_rows = []
    ls_codes = [c for c, _ in ls_rows]
    ls_set = set(ls_codes)
    ls_ce = {c for c in ls_set if c.startswith("TC_CE_")}
    ls_re = {c for c in ls_set if c.startswith("TC_RE_")}
    if len(ls_set) != len(ls_codes):
        dup = sorted({c for c in ls_codes if ls_codes.count(c) > 1})
        failures.append(f"附录 B：同一码在表内重复出现 {dup}")
    if (len(ls_set), len(ls_ce), len(ls_re)) != (87, 73, 14):
        failures.append(
            f"附录 B：实际 {len(ls_set)} 码（{len(ls_ce)} TC_CE_* + {len(ls_re)} TC_RE_*），"
            "应为 87（73 + 14）")
    m = re.search(r"共 (\d+) 码\*\* = (\d+) `TC_CE_\*` \+ (\d+) `TC_RE_\*`", lang)
    if not m:
        failures.append("附录 B：未找到「共 N 码 = N TC_CE_* + N TC_RE_*」声称值")
    elif (int(m.group(1)), int(m.group(2)), int(m.group(3))) != (len(ls_set), len(ls_ce), len(ls_re)):
        failures.append(
            f"附录 B：正文声称 {m.group(1)} = {m.group(2)} + {m.group(3)}，"
            f"表内实际 {len(ls_set)} = {len(ls_ce)} + {len(ls_re)}")

    # ---- 2. 编译器 §11.4 各表 --------------------------------------------
    sections, body = parse_compiler_sections(comp)
    if not sections:
        failures.append("编译器 §11.4：未找到错误码对照表")
        sections, body = [], ""

    comp_rows = [(c, n, s, h) for h, rows in sections for (c, n, s) in rows]
    comp_lang = [(c, n, s, h) for (c, n, s, h) in comp_rows if c != OOM_CODE]
    comp_set = {c for c, _, _, _ in comp_lang}
    if comp_set != ls_set:
        missing = sorted(ls_set - comp_set)
        extra = sorted(comp_set - ls_set)
        if missing:
            failures.append(f"编译器 §11.4 缺少附录 B 码 {missing}")
        if extra:
            failures.append(f"编译器 §11.4 出现附录 B 之外的语言码 {extra}")

    # ---- 3. 码 / 打印名一一对应 ------------------------------------------
    seen = {}
    spots = {}
    for code, name, stage, head in comp_lang:
        if not name or not NAME_RE.match(name):
            failures.append(f"编译器 §11.4：{code} 的打印名非法（{name!r}）")
        if code in seen:
            if seen[code] != name:
                failures.append(
                    f"编译器 §11.4：{code} 在两处打印名不一致（{seen[code]} / {name}）")
        seen[code] = name
        spots.setdefault(code, []).append((name, stage, head.split()[1]))
    for code, occ in spots.items():
        if len(occ) > 2:
            failures.append(f"编译器 §11.4：{code} 出现 {len(occ)} 次（最多 2 次）")
        elif len(occ) == 2:
            if occ[0][0] != occ[1][0] or occ[0][1] != occ[1][1]:
                failures.append(f"编译器 §11.4：{code} 交叉列出处内容不一致 {occ}")
            if "11.4.1" not in (occ[0][2], occ[1][2]):
                failures.append(f"编译器 §11.4：{code} 的交叉列出未包含 §11.4.1 {occ}")

    # 打印名：同一条件的 TC_CE_X / TC_RE_X 可共用，其余必须唯一
    by_name = {}
    for code, name in seen.items():
        by_name.setdefault(name, set()).add(code)
    for name, codes in sorted(by_name.items()):
        if len(codes) == 1:
            continue
        suffixes = set()
        ok = len(codes) == 2
        for c in codes:
            mt = re.match(r"^TC_(CE|RE)_(.+)$", c)
            if not mt:
                ok = False
                break
            suffixes.add(mt.group(2))
        if not ok or len(suffixes) != 1:
            failures.append(f"编译器 §11.4：打印名 {name} 被多个无关码共用 {sorted(codes)}")

    # ---- 4. 分节计数 ------------------------------------------------------
    claims = parse_section_claims(body)
    if not claims:
        failures.append("编译器 §11.4：未找到概览表分节计数")
    acc = set()
    for head, rows in sections:
        m = re.match(r"### (11\.4\.\d)", head)
        if not m:
            continue
        key = m.group(1)
        new = [c for c, _, _ in rows if c != OOM_CODE and c not in acc]
        acc.update(c for c, _, _ in rows if c != OOM_CODE)
        claimed = claims.get(key)
        if claimed is None:
            failures.append(f"编译器 {key}：概览表缺少本节的码数")
        elif claimed != len(new):
            failures.append(
                f"编译器 {key}（{head.split(' ', 2)[-1]}）：概览表写 {claimed}，表内新增 {len(new)}")
    if len(acc) != len(ls_set):
        failures.append(f"编译器 §11.4：去重后共 {len(acc)} 码，附录 B 为 {len(ls_set)}")

    # ---- 5. 实现资源码 ----------------------------------------------------
    oom = [r for r in comp_rows if r[0] == OOM_CODE]
    if len(oom) != 1:
        failures.append(f"编译器 §11.4：{OOM_CODE} 应恰好出现 1 次，实际 {len(oom)}")
    elif "实现" not in oom[0][2]:
        failures.append(f"编译器 §11.4：{OOM_CODE} 的阶段未标为「实现」（{oom[0][2]!r}）")
    m = re.search(r"实现枚举 \| \*\*(\d+)\*\* = (\d+) 语言码 \+ 1 `TC_ERR_OUT_OF_MEMORY`", comp)
    if not m:
        failures.append("编译器 §11.4：未找到「实现枚举 N = N 语言码 + 1」声称值")
    elif (int(m.group(1)), int(m.group(2))) != (len(ls_set) + 1, len(ls_set)):
        failures.append(
            f"编译器 §11.4：正文声称实现枚举 {m.group(1)} = {m.group(2)} + 1，"
            f"按附录 B 应为 {len(ls_set) + 1} = {len(ls_set)} + 1")

    # ---- 6. 前缀与阶段自洽 ------------------------------------------------
    for code, _, stage, head in comp_lang:
        if code.startswith("TC_RE_") and "运行时" not in stage:
            failures.append(f"编译器 §11.4：{code} 为运行时码，阶段却写 {stage!r}")
        if code.startswith("TC_CE_") and "运行时" in stage:
            failures.append(f"编译器 §11.4：{code} 为编译期码，阶段却含「运行时」（{stage!r}）")
    for code, stage in ls_rows:
        if code.startswith("TC_RE_") and stage != "RT":
            failures.append(f"附录 B：{code} 阶段应写 RT，实际 {stage!r}")
        if code.startswith("TC_CE_") and "RT" in stage:
            failures.append(f"附录 B：{code} 阶段不应含 RT（{stage!r}）")

    # ---- 汇总 ------------------------------------------------------------
    if failures:
        print("check_error_codes: 0.0.45 草案错误码表不一致：")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(
        f"check_error_codes: 附录 B 与编译器 §11.4 一致（{len(ls_set)} 语言码 = "
        f"{len(ls_ce)} TC_CE_* + {len(ls_re)} TC_RE_*，另 +1 {OOM_CODE}）")


if __name__ == "__main__":
    main()
