#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_doc_counts.py — 文档统计数字与事实源一致性检查（防回潮）

从事实源提取计数并与文档声称值比对：
  - 错误码种类：tests/unit/runtime/test_types.c 的 `error_kind_count == N` 断言
                vs .cursor/skills/tc-architecture/types.md「错误种类：**N**」
  - TcRhsKind 枚举数：src/vm/runtime/tc_types.h 的 TcRhsKind 枚举成员数
                vs docs/TC-0.0.39-开发计划.md「RHS 分发覆盖 | N」
  - VM 用例规模：scripts/vm/run_tests.sh 的测试调用行数（不含 helper 定义）
                vs .cursor/skills/tc-architecture/test-map.md「N VM」
  - AOT 用例规模：scripts/aot/run_tests.sh 的 run_diff_test/run_check_ok/
                run_check_fail/run_aot_cli_golden 注册数
                vs test-map.md「~N AOT（注册）」

unit「约 N check()」为约值（跨多个 cmake target 动态编译），不在此校验。

用法：python3 scripts/sync/check_doc_counts.py
不一致时打印差异并以非零退出；配合 CI 或 run_tests.sh 使用。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

VM_CALL_RE = (
    r"^\s*(?:run_expect_\w+|run_cli_golden|run_with_stdin|"
    r"run_include_search|run_include_path_limit|run_ambiguous_import)\s+"
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


def count_vm_test_calls(vm_sh):
    """统计会调用 pass() 的测试注册行（排除 run_expect_* 等 helper 定义）。"""
    return len(re.findall(VM_CALL_RE, vm_sh, re.M))


def main():
    failures = []

    # ---- 1. 错误码种类 -------------------------------------------------
    test_types = read("tests/unit/runtime/test_types.c")
    m = re.search(r"error_kind_count == (\d+)U", test_types)
    err_actual = int(m.group(1)) if m else None

    types_md = read(".cursor/skills/tc-architecture/types.md")
    m = re.search(r"错误种类：\*\*(\d+)\*\*", types_md) if types_md else None
    err_doc = int(m.group(1)) if m else None
    if err_actual and err_doc and err_actual != err_doc:
        failures.append(
            f"错误码种类：test_types.c 断言 {err_actual}，types.md 写 {err_doc}")
    elif not err_actual or not err_doc:
        failures.append("错误码种类：无法从 test_types.c / types.md 提取计数")

    # ---- 2. TcRhsKind 枚举数 -------------------------------------------
    tc_types_h = read("src/vm/runtime/tc_types.h")
    rhs_actual = count_tc_rhs_kinds(tc_types_h) if tc_types_h else None

    dev_plan = read("docs/TC-0.0.39-开发计划.md")
    m = re.search(r"RHS 分发覆盖 \| (\d+)", dev_plan)
    rhs_doc = int(m.group(1)) if m else None
    if rhs_actual and rhs_doc and rhs_actual != rhs_doc:
        failures.append(
            f"RHS 分发覆盖：TcRhsKind 枚举 {rhs_actual} 个，开发计划写 {rhs_doc}")
    elif not rhs_actual or not rhs_doc:
        failures.append("RHS 分发覆盖：无法从 tc_types.h / 开发计划提取计数")

    # ---- 3. VM 用例规模 -------------------------------------------------
    vm_sh = read("scripts/vm/run_tests.sh")
    vm_actual = count_vm_test_calls(vm_sh) if vm_sh else None
    test_map = read(".cursor/skills/tc-architecture/test-map.md")
    m = re.search(r"约 (\d+) VM", test_map) if test_map else None
    vm_doc = int(m.group(1)) if m else None
    if vm_actual is not None and vm_doc is not None and vm_actual != vm_doc:
        failures.append(f"VM 用例规模：run_tests.sh 注册 {vm_actual}，test-map 写 {vm_doc}")

    # ---- 4. AOT 用例规模（注册行）--------------------------------------
    aot_sh = read("scripts/aot/run_tests.sh")
    aot_actual = (len(re.findall(r"run_diff_test\b", aot_sh))
                  + len(re.findall(r"run_check_ok\b", aot_sh))
                  + len(re.findall(r"run_check_fail\b", aot_sh))
                  + len(re.findall(r"run_aot_cli_golden\b", aot_sh)))
    m = re.search(r"~(\d+) AOT（注册）", test_map) if test_map else None
    aot_doc = int(m.group(1)) if m else None
    if aot_doc is not None and aot_actual != aot_doc:
        failures.append(
            f"AOT 用例规模（注册）：run_tests.sh 注册 {aot_actual}，test-map 写 {aot_doc}")

    # ---- 汇总 ------------------------------------------------------------
    if failures:
        print("check_doc_counts: 以下文档数字与事实源不一致：")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print("check_doc_counts: 错误码 / RHS / VM / AOT 文档数字均与事实源一致")


if __name__ == "__main__":
    main()
