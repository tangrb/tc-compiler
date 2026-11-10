#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_doc_counts.py — 文档统计数字与事实源一致性检查（防回潮）

从事实源提取计数并与文档声称值比对：
  - 错误码种类：src/vm/runtime/tc_types.h 的 TcErrorKind 枚举成员数；
                交叉核对 tests/unit/runtime/test_types.c 的 `error_kind_count` 断言
                vs .cursor/skills/tc-architecture/types.md「错误种类：**语言标准附录 B 共 N 码** … 断言 M」
  - 附录 B（0.0.44 基线）：语言标准附录 B 唯一 TC_CE_*/TC_RE_* 码数（86）
            vs 正文「共 N 码」vs 实现枚举 N+1（+ `TC_ERR_OUT_OF_MEMORY`）
  - 附录 B（0.0.46 目标）：附录 B 91 个错误码 = 77 CE + 14 RE，另有
            1 个 CW 编译警告码，且每个码在附录 B 之外至少出现一次；
            语法阶段专用码恰为 6 个且阶段为 SYN
  - TcRhsKind 枚举数：src/vm/runtime/tc_types.h 的 TcRhsKind 枚举成员数
                vs .cursor/skills/tc-architecture/types.md「RHS 分发覆盖：**N**」
  - VM 用例规模：scripts/vm/run_tests.sh 的测试调用行数（不含 helper 定义）
                vs .cursor/skills/tc-architecture/test-map.md「N VM」
  - AOT 用例规模：scripts/aot/run_tests.sh 的 run_diff_test/run_check_ok/
                run_check_fail/run_aot_cli_golden 注册数
                vs test-map.md「~N AOT（注册）」；执行项另含 runtime_fail /
                codegen / embed codegen / CLI 相对路径与 -I 上限
                vs 「N AOT（执行）」

unit「约 N check()」为约值（跨多个 cmake target 动态编译），不在此校验。

用法：python3 scripts/sync/check_doc_counts.py
不一致时打印差异并以非零退出；配合 CI 或 run_tests.sh 使用。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

LANG_STD_BASELINE = "docs/TC语言标准设计说明书-0.0.44.md"
LANG_STD_TARGET = "docs/TC语言标准-0.0.46.md"

SYNTAX_SPECIAL_CODES = frozenset({
    "TC_CE_MISSING_END",
    "TC_CE_OPERAND_COUNT",
    "TC_CE_MODULE_LAYER",
    "TC_CE_MISSING_VISIBILITY",
    "TC_CE_PROGRAM_MODE_MISUSE",
    "TC_CE_VAR_MISSING_INIT",
})

VM_CALL_RE = (
    r"^\s*(?:run_expect_\w+|run_cli_\w+|run_with_stdin|"
    r"run_include_search|run_include_path_limit|run_ambiguous_import)\s+"
)

AOT_REGISTER_HELPERS = (
    "run_diff_test",
    "run_check_ok",
    "run_check_fail",
    "run_aot_cli_golden",
)
AOT_EXEC_HELPERS = AOT_REGISTER_HELPERS + (
    "run_runtime_fail",
    "run_codegen_contains",
    "run_codegen_not_contains",
    "run_codegen_embed_contains",
    "run_codegen_embed_not_contains",
    "run_aot_relative_run",
)


def read(rel):
    """读取文件；缺失（如 CI 检出无 .cursor）时返回 None，对应项跳过。"""
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def count_tc_rhs_kinds(tc_types_h):
    """仅统计 TcRhsKind 枚举体内的成员行（排除注释中的 TC_RHS_* 误匹配）。"""
    m = re.search(
        r"/\*\* 右值表达式种类 \*/\s*typedef enum \{(.*?)\} TcRhsKind;",
        tc_types_h,
        re.S,
    )
    if not m:
        return None
    return sum(1 for ln in m.group(1).split("\n") if re.match(r"\s*TC_RHS_", ln))


def count_appendix_b_codes(lang_std):
    """统计语言标准附录 B 表中唯一的 TC_CE_* / TC_RE_* 码。

    终止边界取「下一个二级标题」或文末：0.0.42 之后是 `## 变更记录`，
    0.0.44 已把修订历史移出规范正文，附录 B 直接到文末。
    """
    m = re.search(r"## 附录 B：(?:错误码|诊断码)速查表(.*?)(?=\n## |\Z)", lang_std, re.S)
    if not m:
        return None
    return len(set(re.findall(r"`(TC_(?:CE|RE)_[A-Z0-9_]+)`", m.group(1))))


def split_appendix_b(text):
    """返回 (附录 B 之前的正文, 附录 B 及之后)，便于检查正文覆盖。"""
    m = re.search(r"## 附录 B：(?:错误码|诊断码)速查表", text)
    if not m:
        return None, None
    return text[:m.start()], text[m.start():]


def check_target_standard(lang_std, failures):
    """校验 0.0.46 语言标准附录 B 的码数、正文覆盖与语法专用码。"""
    body, appendix = split_appendix_b(lang_std)
    if appendix is None:
        failures.append("0.0.46 附录 B：无法定位附录 B 标题")
        return

    codes = set(re.findall(r"`(TC_(?:CE|RE|CW)_[A-Z0-9_]+)`", appendix))
    ce = {c for c in codes if c.startswith("TC_CE_")}
    rt = {c for c in codes if c.startswith("TC_RE_")}
    cw = {c for c in codes if c.startswith("TC_CW_")}
    if (len(codes), len(ce), len(rt), len(cw)) != (92, 77, 14, 1):
        failures.append(
            f"0.0.46 附录 B：码数 {len(codes)} = {len(ce)} CE + {len(rt)} RE "
            f"+ {len(cw)} CW，应为 92 = 77 CE + 14 RE + 1 CW")
    else:
        m = re.search(
            r"共 (\d+) 个错误码\*\* = (\d+) `TC_CE_\*` \+ (\d+) `TC_RE_\*`；"
            r"另有 \*\*(\d+) 个编译警告码\*\*",
            appendix,
        )
        if not m:
            failures.append(
                "0.0.46 附录 B：无法提取「共 N 个错误码 = N CE + N RE；"
                "另有 N 个编译警告码」声明")
        else:
            claimed = tuple(int(x) for x in m.groups())
            if claimed != (91, 77, 14, 1):
                failures.append(
                    f"0.0.46 附录 B：声明 {claimed[0]} 个错误码 = {claimed[1]} CE + "
                    f"{claimed[2]} RE，另有 {claimed[3]} 个警告码，"
                    "应为 91 = 77 CE + 14 RE，另有 1 个警告码")

    missing = sorted(c for c in codes if f"`{c}`" not in body)
    if missing:
        failures.append(
            "0.0.46 附录 B：以下码在附录 B 之外无正文出现：" + ", ".join(missing))

    intro_m = re.search(r"###\s+1\.3\s+.*?(?=\n##\s+2\.)", body, re.S)
    intro = intro_m.group(0) if intro_m else ""
    if not intro:
        failures.append("0.0.46 §1.3：无法定位规范约定小节")
    else:
        m = re.search(r"语法阶段专用码\*\*共 (\d+) 个\*\*", intro)
        if not m or int(m.group(1)) != len(SYNTAX_SPECIAL_CODES):
            failures.append(
                "0.0.46 §1.3：语法阶段专用码声明应为 "
                f"{len(SYNTAX_SPECIAL_CODES)} 个")
        for code in sorted(SYNTAX_SPECIAL_CODES):
            if f"`{code}`" not in intro:
                failures.append(f"0.0.46 §1.3：缺少语法阶段专用码 {code}")

    m = re.search(r"语法阶段专用码共 (\d+) 个", appendix)
    if not m or int(m.group(1)) != len(SYNTAX_SPECIAL_CODES):
        failures.append(
            "0.0.46 附录 B：语法阶段专用码说明应为 "
            f"{len(SYNTAX_SPECIAL_CODES)} 个")

    stages = dict(re.findall(
        r"^\|\s*`(TC_(?:CE|RE)_[A-Z0-9_]+)`\s*\|\s*([^|]+?)\s*\|",
        appendix, re.M))
    for code in sorted(SYNTAX_SPECIAL_CODES):
        if code not in codes:
            failures.append(f"0.0.46 附录 B：语法阶段专用码 {code} 未收录")
        elif "SYN" not in stages.get(code, ""):
            failures.append(
                f"0.0.46 附录 B：语法阶段专用码 {code} 阶段不是 SYN")


def count_error_kinds(tc_types_h):
    """仅统计 TcErrorKind 枚举体内的成员行（排除注释中的 TC_*_ 误匹配）。"""
    m = re.search(r"typedef enum \{(.*?)\} TcErrorKind;", tc_types_h, re.S)
    if not m:
        return None
    return sum(1 for ln in m.group(1).split("\n")
               if re.match(r"\s*TC_(?:CE|RE|ERR)_", ln))


def count_vm_test_calls(vm_sh):
    """统计会调用 pass() 的测试注册行（排除 run_expect_* 等 helper 定义）。"""
    return len(re.findall(VM_CALL_RE, vm_sh, re.M))


def count_aot_helper_calls(aot_sh, names):
    """统计 AOT helper 调用行（排除 `name() {` 定义）。"""
    total = 0
    for name in names:
        total += len(re.findall(
            r"^\s*" + re.escape(name) + r"(?:\s+|$)", aot_sh, re.M))
    return total


def main():
    failures = []

    # ---- 1. 错误码种类 -------------------------------------------------
    # 事实源：TcErrorKind 枚举成员数（= 语言码 + TC_ERR_OUT_OF_MEMORY）。
    tc_types_h = read("src/vm/runtime/tc_types.h")
    err_impl = count_error_kinds(tc_types_h) if tc_types_h else None

    # 交叉核对 test_types.c：允许 `== NU` 显式断言或 `(size_t)TC_ERR_OUT_OF_MEMORY + 1U` 派生写法。
    test_types = read("tests/unit/runtime/test_types.c")
    err_actual = None
    if test_types:
        m = re.search(r"error_kind_count\s*==\s*(\d+)U", test_types)
        if m:
            err_actual = int(m.group(1))
        elif re.search(r"error_kind_count\s*=\s*\(size_t\)\s*TC_ERR_OUT_OF_MEMORY\s*\+\s*1U",
                       test_types):
            err_actual = err_impl  # 派生断言：等价于枚举成员数

    types_md = read(".cursor/skills/tc-architecture/types.md")
    err_doc = None       # 实现枚举/断言口径
    err_doc_lang = None  # types.md 声称的语言码数
    if types_md:
        m = re.search(r"错误种类：\*\*语言标准附录 B 共 (\d+) 码\*\*[^\n]*?断言 (\d+)",
                      types_md)
        if m:
            err_doc_lang, err_doc = int(m.group(1)), int(m.group(2))
        else:
            m = re.search(r"错误种类：\*\*(\d+)\*\*", types_md)
            if m:
                err_doc = int(m.group(1))

    if err_actual is None:
        failures.append("错误码种类：无法从 test_types.c 提取计数断言")
    elif err_doc is None:
        failures.append("错误码种类：无法从 types.md 提取计数")
    elif err_actual != err_doc:
        failures.append(
            f"错误码种类：test_types.c 断言 {err_actual}，types.md 写 {err_doc}")
    if err_impl is not None and err_actual is not None and err_impl != err_actual:
        failures.append(
            f"错误码种类：tc_types.h 枚举 {err_impl}，test_types.c 断言 {err_actual}")

    # ---- 1b. 语言标准附录 B 码数 vs 实现枚举（0.0.44 基线）-------------
    lang_std_baseline = read(LANG_STD_BASELINE)
    appendix_actual = (
        count_appendix_b_codes(lang_std_baseline) if lang_std_baseline else None
    )
    appendix_claimed = None
    if lang_std_baseline:
        m = re.search(r"本附录汇总正文及附录 A 中出现过的全部诊断码（\*\*共 (\d+) 码\*\*",
                      lang_std_baseline)
        appendix_claimed = int(m.group(1)) if m else None
    if appendix_actual is None or appendix_claimed is None:
        failures.append("附录 B（0.0.44 基线）：无法从语言标准提取码数")
    else:
        if appendix_actual != appendix_claimed:
            failures.append(
                f"附录 B（0.0.44 基线）：表内唯一码 {appendix_actual}，"
                f"正文写共 {appendix_claimed} 码")
        if err_impl is not None and appendix_actual + 1 != err_impl:
            failures.append(
                f"附录 B（0.0.44 基线）：语言码 {appendix_actual} + OOM 应为 "
                f"{appendix_actual + 1}，tc_types.h 枚举 {err_impl}")
        if err_doc_lang is not None and appendix_actual != err_doc_lang:
            failures.append(
                f"附录 B（0.0.44 基线）：types.md 写语言码 {err_doc_lang}，"
                f"附录 B 实际 {appendix_actual}")

    # ---- 1c. 0.0.46 语言标准（设计目标）--------------------------------
    lang_std_target = read(LANG_STD_TARGET)
    if lang_std_target is None:
        failures.append(f"0.0.46 语言标准：文件缺失（{LANG_STD_TARGET}）")
    else:
        check_target_standard(lang_std_target, failures)

    # ---- 2. TcRhsKind 枚举数 -------------------------------------------
    rhs_actual = count_tc_rhs_kinds(tc_types_h) if tc_types_h else None

    m = re.search(r"RHS 分发覆盖：\*\*(\d+)\*\*", types_md) if types_md else None
    rhs_doc = int(m.group(1)) if m else None
    if rhs_actual and rhs_doc and rhs_actual != rhs_doc:
        failures.append(
            f"RHS 分发覆盖：TcRhsKind 枚举 {rhs_actual} 个，types.md 写 {rhs_doc}")
    elif not rhs_actual or not rhs_doc:
        failures.append("RHS 分发覆盖：无法从 tc_types.h / types.md 提取计数")

    # ---- 3. VM 用例规模 -------------------------------------------------
    vm_sh = read("scripts/vm/run_tests.sh")
    vm_actual = count_vm_test_calls(vm_sh) if vm_sh else None
    test_map = read(".cursor/skills/tc-architecture/test-map.md")
    # 兼容 test-map.md 两种写法：「约 N VM」与「**N VM**」
    m = (re.search(r"约 (\d+) VM", test_map) or
         re.search(r"\*\*(\d+) VM\*\*", test_map)) if test_map else None
    vm_doc = int(m.group(1)) if m else None
    if vm_actual is not None and vm_doc is not None and vm_actual != vm_doc:
        failures.append(f"VM 用例规模：run_tests.sh 注册 {vm_actual}，test-map 写 {vm_doc}")

    # ---- 4. AOT 用例规模（注册行 + 执行项）------------------------------
    aot_sh = read("scripts/aot/run_tests.sh")
    aot_actual = count_aot_helper_calls(aot_sh, AOT_REGISTER_HELPERS) if aot_sh else None
    m = re.search(r"~(\d+) AOT（注册）", test_map) if test_map else None
    aot_doc = int(m.group(1)) if m else None
    if aot_actual is not None and aot_doc is not None and aot_actual != aot_doc:
        failures.append(
            f"AOT 用例规模（注册）：run_tests.sh 注册 {aot_actual}，test-map 写 {aot_doc}")

    aot_exec_actual = None
    if aot_sh:
        aot_exec_actual = count_aot_helper_calls(aot_sh, AOT_EXEC_HELPERS)
        if "CLI aot -I path limit" in aot_sh:
            aot_exec_actual += 1
    m = re.search(r"(\d+) AOT（执行）", test_map) if test_map else None
    aot_exec_doc = int(m.group(1)) if m else None
    if (aot_exec_actual is not None and aot_exec_doc is not None
            and aot_exec_actual != aot_exec_doc):
        failures.append(
            f"AOT 用例规模（执行）：run_tests.sh 执行 {aot_exec_actual}，"
            f"test-map 写 {aot_exec_doc}")

    # ---- 汇总 ------------------------------------------------------------
    if failures:
        print("check_doc_counts: 以下文档数字与事实源不一致：")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(
        "check_doc_counts: 错误码 / 附录 B（0.0.44 基线、0.0.46 目标）/ "
        "RHS / VM / AOT 文档数字均与事实源一致"
    )


if __name__ == "__main__":
    main()
