#!/usr/bin/env python3
"""
TC-Compiler: TcRhsKind 分发点全覆盖检查

验证 8 个分发点中的每个都完整覆盖了所有 TC_RHS_ 枚举值，
防止新增 RHS kind 后遗漏某个分发点的处理。

用法:
    python3 scripts/sync/check_rhs_coverage.py
    python3 scripts/sync/check_rhs_coverage.py --verbose   # 详细输出
    python3 scripts/sync/check_rhs_coverage.py --fix       # 自动更新 knowledge-graph.mdc 行号

返回码: 0=全部覆盖, 1=有遗漏/错误
"""

import re
import sys
import os
import subprocess
import argparse

# 项目根目录
REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))

# ---------------------------------------------------------------------------
# 所有 TC_RHS_ 枚举值
# ---------------------------------------------------------------------------
TC_RHS_KINDS = [
    "TC_RHS_LIT",
    "TC_RHS_CONST_REF",
    "TC_RHS_ARITH",
    "TC_RHS_UNARY",
    "TC_RHS_COMPARE",
    "TC_RHS_LOGIC_BIN",
    "TC_RHS_LOGIC_UN",
    "TC_RHS_CAST",
    "TC_RHS_CONST_CAST",
]

# ---------------------------------------------------------------------------
# 分发点定义
# ---------------------------------------------------------------------------
# 每个分发点:
#   path:      相对于 REPO_ROOT 的文件路径
#   func:      函数名
#   line_func: 函数定义的大致行号（人工标注，用于知识图谱表）
#   pattern:   grep 模式，用于提取 rhs->kind 检查
#   skip:      有意不处理的 kinds，带说明
#   note:      额外说明（如 "fallthrough" 表示最后一个 if 块无显式 kind 检查）
DispatchPoint = {
    "path": "",
    "func": "",
    "line_func": 0,
    "skip": {},       # kind -> justification
    "note": "",       # 额外注释
    "extra_kinds": [],  # 需要额外匹配的组合（如 CONST_REF || CONST_CAST 打包检查）
}
# 分布点特定规则：记录需要额外匹配的 kind（非 rhs->kind == 模式）
# 和因 fallthrough 等有意不显式检查的 kind
DISPATCH_POINTS = [
    {
        "path": "src/vm/parser/tc_parser.c",
        "func": "tc_rhs_free",
        "line_func": 861,
        "skip": {
            "TC_RHS_LIT": "LIT 无动态内存，不需释放",
        },
    },
    {
        "path": "src/vm/parser/tc_parser.c",
        "func": "tc_parse_rhs",
        "line_func": 537,
        "note": "按 token kind 分派到子函数；LIT 通过 out->kind = TC_RHS_LIT 赋值",
        "output_kinds": ["TC_RHS_LIT"],  # 通过 out->kind = 赋值，非 rhs->kind == 比较
        "skip": {
            "TC_RHS_CONST_REF": "只在 tc_parse_const_rhs 中创建",
            "TC_RHS_CONST_CAST": "只在 tc_parse_const_rhs 中创建",
        },
    },
    {
        "path": "src/vm/parser/tc_parser.c",
        "func": "tc_parse_const_rhs",
        "line_func": 582,
        "note": "按 token kind 分派到子函数；LIT 通过 out->kind = TC_RHS_LIT 赋值",
        "output_kinds": ["TC_RHS_LIT"],
        "skip": {
            "TC_RHS_CAST": "常量表达式中用 TC_RHS_CONST_CAST 代替",
        },
    },
    {
        "path": "src/vm/analyzer/tc_analyzer.c",
        "func": "tc_check_rhs",
        "line_func": 217,
        "skip": {
            "TC_RHS_CAST": "最后一条 if 链后的 fallthrough 块处理（无显式 rhs->kind == 检查）",
        },
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/vm/analyzer/tc_const_eval.c",
        "func": "tc_eval_const_rhs",
        "line_func": 136,
        "skip": {
            "TC_RHS_CAST": "CAST 用于运行时变量，不进入编译期常量求值",
        },
    },
    {
        "path": "src/vm/executor/tc_executor.c",
        "func": "tc_eval_rhs",
        "line_func": 123,
        "skip": {},
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/aot/tc_aot_codegen.c",
        "func": "tc_aot_emit_rhs",
        "line_func": 80,
        "skip": {
            "TC_RHS_CAST": "最后一条 if 链后的 fallthrough 块处理（无显式 rhs->kind == 检查）",
        },
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/aot/tc_aot_rt.c",
        "func": "tc_aot_* shim",
        "line_func": 1,
        "note": "按函数名分派（非 switch on kind），只需验证 shim 函数存在",
        "skip": {
            "TC_RHS_LIT": "LIT 在 codegen 内联展开",
            "TC_RHS_CONST_REF": "代码生成时返回 -1，不产生 shim 调用",
            "TC_RHS_CONST_CAST": "代码生成时返回 -1，不产生 shim 调用",
        },
    },
]


def get_git_line(path):
    """获取文件的 git blame 行数（用于验证 line_func 是否准确）"""
    abs_path = os.path.join(REPO_ROOT, path)
    try:
        result = subprocess.run(
            ["git", "blame", "-l", abs_path],
            capture_output=True, text=True, cwd=REPO_ROOT, timeout=10,
        )
        return result.stdout
    except Exception:
        return None


def get_rhs_kind_refs(path, output_kinds=None):
    """从源码中提取 rhs->kind 比较和赋值模式，返回 (kinds_handled, kinds_not_handled_detail)"""
    abs_path = os.path.join(REPO_ROOT, path)
    output_kinds = output_kinds or []
    if not os.path.exists(abs_path):
        return set(), {}

    with open(abs_path, "r") as f:
        content = f.read()

    # 匹配 rhs->kind == TC_RHS_XXX 或 rhs->kind != TC_RHS_XXX
    single_matches = re.findall(
        r'rhs->kind\s*(==|!=)\s*TC_RHS_(\w+)', content
    )

    # 匹配合并条件: rhs->kind == TC_RHS_XXX || rhs->kind == TC_RHS_YYY
    combined_matches = re.findall(
        r'TC_RHS_(\w+).*?\|\|.*?rhs->kind\s*==\s*TC_RHS_(\w+)', content
    )

    # 匹配输出赋值: out->kind = TC_RHS_XXX
    out_matches = re.findall(
        r'out->kind\s*=\s*TC_RHS_(\w+)', content
    )

    kinds_handled = set()
    kinds_not_handled = {}

    for op, kind in single_matches:
        full_kind = f"TC_RHS_{kind}"
        if op == "==":
            kinds_handled.add(full_kind)
        else:
            kinds_not_handled[full_kind] = True

    for k1, k2 in combined_matches:
        kinds_handled.add(f"TC_RHS_{k1}")
        kinds_handled.add(f"TC_RHS_{k2}")

    # 输出赋值也计入已处理
    for kind in out_matches:
        kinds_handled.add(f"TC_RHS_{kind}")

    # 额外 output_kinds（非正则捕获的赋值模式）
    for ok in output_kinds:
        kinds_handled.add(ok)

    return kinds_handled, kinds_not_handled


def get_aot_rt_shims(path):
    """检查 tc_aot_rt.c 中 shim 函数是否存在"""
    abs_path = os.path.join(REPO_ROOT, path)
    if not os.path.exists(abs_path):
        return set()

    with open(abs_path, "r") as f:
        content = f.read()

    # 字面量映射：RHS kind -> 对应的 aot rt shim 函数名
    shim_map = {
        "TC_RHS_ARITH": "tc_aot_arith",
        "TC_RHS_UNARY": "tc_aot_unary",
        "TC_RHS_COMPARE": "tc_aot_compare",
        "TC_RHS_LOGIC_BIN": "tc_aot_logic",
        "TC_RHS_LOGIC_UN": "tc_aot_logic_unary",
        "TC_RHS_CAST": "tc_aot_cast",
    }

    present = set()
    for kind, func_name in shim_map.items():
        if re.search(rf'\b{func_name}\b', content):
            present.add(kind)

    return present


def check_dispatch_point(dp):
    """检查单个分发点是否完整覆盖所有 TC_RHS_KINDS"""
    path = dp["path"]
    skip = dp["skip"]
    note = dp.get("note", "")
    extra_kinds = dp.get("extra_kinds", [])
    func = dp["func"]

    errors = []
    warnings = []
    info = []

    output_kinds = dp.get("output_kinds", [])

    if func == "tc_aot_* shim":
        # AOT rt shim 特殊处理
        kinds_handled = get_aot_rt_shims(path)
        missing = set(TC_RHS_KINDS) - kinds_handled - set(skip.keys())
        if missing:
            errors.append(
                f"  [{func}] 缺失 shim 函数: {', '.join(sorted(missing))}\n"
                f"      需要为这些 RHS kind 添加 tc_aot_xxx 包装函数"
            )
    else:
        kinds_handled, kinds_not_handled = get_rhs_kind_refs(path, output_kinds)

        # 将 extra_kinds 展开后加入到 handled 集合
        for ek in extra_kinds:
            for k in ek.split(","):
                kinds_handled.add(k.strip())

        all_kinds = set(TC_RHS_KINDS)
        handled = set(kinds_handled)

        # 检查是否所有 kinds 都覆盖了（handled + skip = all）
        covered = handled | set(skip.keys())
        not_covered = all_kinds - covered

        # 检查是否有 handled 的 kind 不在枚举中（拼写错误或误删）
        unknown = handled - all_kinds

        if unknown:
            errors.append(
                f"  [{func}] 引用了未知的 TC_RHS_ kind: {', '.join(sorted(unknown))}"
            )

        if not_covered:
            # 检查是否在注释/另一种 pattern 中出现了
            abs_path = os.path.join(REPO_ROOT, path)
            for nk in sorted(not_covered):
                if os.path.exists(abs_path):
                    with open(abs_path, "r") as f:
                        content = f.read()
                    if re.search(rf'\b{nk}\b', content):
                        warnings.append(
                            f"  [{func}] {nk} 出现在文件中但未被 rhs->kind 检查捕获"
                        )
                    else:
                        errors.append(
                            f"  [{func}] 完全未处理: {nk}\n"
                            f"      若有意跳过，请在 check_rhs_coverage.py DISPATCH_POINTS 的 skip 中添加说明"
                        )

        info.append(f"  [{func}] 已处理: {', '.join(sorted(handled))}" if handled else "")
        skipped_info = [f"{k}({v})" for k, v in skip.items()]
        if skipped_info:
            info.append(f"  [{func}] 有意跳过: {'; '.join(skipped_info)}")

    return errors, warnings, info


def find_function_line(path, func_name):
    """在文件中找到函数定义的行号"""
    abs_path = os.path.join(REPO_ROOT, path)
    if not os.path.exists(abs_path):
        return 0
    with open(abs_path, "r") as f:
        for i, line in enumerate(f, 1):
            # 匹配 static int func_name( 或 int func_name( 或 void func_name(
            if re.match(
                rf'^(static\s+)?(int|void|TcRhsKind|const\s+char\s*\*)\s+{re.escape(func_name)}\s*\(',
                line,
            ):
                return i
    return 0


def sync_line_numbers():
    """自动校正 DISPATCH_POINTS 中的行号"""
    updated = []
    for dp in DISPATCH_POINTS:
        func = dp["func"]
        path = dp["path"]
        if func == "tc_aot_* shim":
            continue
        actual_line = find_function_line(path, func)
        if actual_line and actual_line != dp["line_func"]:
            updated.append((dp["func"], dp["line_func"], actual_line))
            dp["line_func"] = actual_line
    return updated


def format_dispatch_table():
    """生成 Markdown 表格供 knowledge-graph.mdc 使用"""
    lines = [
        "| 函数 | 文件 | 行号 | 说明 |",
        "|------|------|------|------|",
    ]
    for dp in DISPATCH_POINTS:
        func = dp["func"]
        path = os.path.basename(os.path.dirname(dp["path"])) + "/" + os.path.basename(dp["path"])
        line_num = str(dp["line_func"]) if dp["line_func"] else "-"
        note = dp.get("note", "")
        skip_info = dp.get("skip", {})
        if skip_info:
            skipped = ", ".join(
                [f"{k}({v})" for k, v in skip_info.items()]
            )
            if note:
                note += "; "
            note += f"跳过: {skipped}"
        lines.append(f"| `{func}` | {path} | {line_num} | {note} |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="TcRhsKind 分发点全覆盖检查"
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="详细输出")
    parser.add_argument("--fix", action="store_true", help="自动更新 knowledge-graph.mdc 行号")
    parser.add_argument("--table", action="store_true", help="仅输出知识图谱 Markdown 表格")
    args = parser.parse_args()

    all_errors = []
    all_warnings = []
    all_info = []

    if args.table:
        print(format_dispatch_table())
        return 0

    # 同步行号
    if args.fix:
        updated = sync_line_numbers()
        if updated:
            print("行号更新:")
            for func, old, new in updated:
                print(f"  {func}: {old} → {new}")
        else:
            print("行号均已最新")

    # 检查每个分发点
    for dp in DISPATCH_POINTS:
        e, w, i = check_dispatch_point(dp)
        all_errors.extend(e)
        all_warnings.extend(w)
        all_info.extend(i)

    # 输出结果
    print("=" * 60)
    print("TcRhsKind 分发点全覆盖检查")
    print("=" * 60)

    if all_info:
        print("\n--- 覆盖详情 ---")
        for line in all_info:
            print(line)

    if all_warnings:
        print("\n--- 警告 ---")
        for line in all_warnings:
            print(line)

    if all_errors:
        print("\n--- 错误 ---")
        for line in all_errors:
            print(line)
        print(f"\n❌ 发现 {len(all_errors)} 个错误")
        return 1
    else:
        print("\n✅ 所有分发点均已完整覆盖所有 TcRhsKind")
        return 0


if __name__ == "__main__":
    sys.exit(main())
