#!/usr/bin/env python3
"""
TC-Compiler: TcRhsKind 分发点全覆盖检查

验证 8 个分发点中的每个都完整覆盖了所有 TC_RHS_ 枚举值，
防止新增 RHS kind 后遗漏某个分发点的处理。

用法:
    python3 scripts/sync/check_rhs_coverage.py
    python3 scripts/sync/check_rhs_coverage.py --verbose   # 详细输出
    python3 scripts/sync/check_rhs_coverage.py --fix       # 校验函数名并同步 knowledge-graph.mdc 文件路径

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
    "TC_RHS_BITWISE_BIN",
    "TC_RHS_BITWISE_UN",
    "TC_RHS_SHIFT",
    "TC_RHS_CAST",
    "TC_RHS_CONST_CAST",
    "TC_RHS_FLOAT_ARITH",
    "TC_RHS_FLOAT_UNARY",
    "TC_RHS_FLOAT_COMPARE",
    "TC_RHS_BITCAST",
    # 0.0.35 复合/调用 RHS（解析 + 运行时已落地）
    "TC_RHS_MEMBLOCK_LOAD",
    "TC_RHS_MEMBLOCK_CONSTRUCTOR",
    "TC_RHS_MEMBLOCK_COUNT",
    "TC_RHS_STRUCT_CONSTRUCTOR",
    "TC_RHS_FIELD_READ",
    "TC_RHS_PTR_LOAD",
    "TC_RHS_PTR_ADDRESS",
    "TC_RHS_PTR_ADD",
    "TC_RHS_PTR_SUB",
    "TC_RHS_PTR_EQ",
    "TC_RHS_PTR_NE",
    "TC_RHS_PTR_LT",
    "TC_RHS_PTR_LE",
    "TC_RHS_PTR_GT",
    "TC_RHS_PTR_GE",
    "TC_RHS_PTR_SIZE",
    "TC_RHS_FUNCALL_EXPR",
    "TC_RHS_SELF_MEMBER",
]

# 全局 reserved（空）：复合/调用 RHS 已落地；per-point skip 见 DISPATCH_POINTS
TC_RHS_PHASE1_RESERVED = {}

# ---------------------------------------------------------------------------
# 分发点定义
# ---------------------------------------------------------------------------
# 每个分发点:
#   path:      相对于 REPO_ROOT 的文件路径
#   func:      函数名
#   line_func: 已废弃（P4-04）；保留字段兼容旧配置，--fix 不再写回行号
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
        "path": "src/vm/parser/tc_parser_free.c",
        "func": "tc_rhs_free",
        "line_func": 0,
        "skip": {
            "TC_RHS_LIT": "LIT 无动态内存，不需释放",
        },
    },
    {
        "path": "src/vm/parser/tc_parser_rhs.c",
        "func": "tc_parse_rhs",
        "line_func": 0,
        "note": "按 token kind 分派到子函数；LIT 通过 out->kind = TC_RHS_LIT 赋值",
        "output_kinds": ["TC_RHS_LIT"],  # 通过 out->kind = 赋值，非 rhs->kind == 比较
        "skip": {
            "TC_RHS_CONST_CAST": "只在 tc_parse_const_rhs 中创建",
            "TC_RHS_FUNCALL_EXPR": "funcall 表达式在 tc_parser.c 赋值 out->kind",
        },
    },
    {
        "path": "src/vm/parser/tc_parser_rhs.c",
        "func": "tc_parse_const_rhs",
        "line_func": 0,
        "note": "按 token kind 分派到子函数；LIT 通过 out->kind = TC_RHS_LIT 赋值",
        "output_kinds": ["TC_RHS_LIT"],
        "skip": {
            "TC_RHS_CAST": "常量表达式中用 TC_RHS_CONST_CAST 代替",
            "TC_RHS_FUNCALL_EXPR": "let 禁函数调用表达式",
        },
    },
    {
        "path": "src/vm/analyzer/tc_analyzer_pass2.c",
        "func": "tc_check_rhs",
        "line_func": 0,
        "skip": {
            "TC_RHS_CAST": "最后一条 if 链后的 fallthrough 块处理（无显式 rhs->kind == 检查）",
            "TC_RHS_FUNCALL_EXPR": "switch case；经 tc_func_check_funcall",
            "TC_RHS_PTR_LOAD": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_ADDRESS": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_ADD": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_SUB": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_EQ": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_NE": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_LT": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_LE": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_GT": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_GE": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_PTR_SIZE": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_MEMBLOCK_LOAD": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_MEMBLOCK_CONSTRUCTOR": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_MEMBLOCK_COUNT": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_STRUCT_CONSTRUCTOR": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_FIELD_READ": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
            "TC_RHS_SELF_MEMBER": "tc_check_rhs 标量路径；复合类型经 tc_type_check_rhs",
        },
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/vm/analyzer/tc_const_eval.c",
        "func": "tc_eval_const_rhs",
        "line_func": 0,
        "skip": {
            "TC_RHS_CAST": "CAST 用于运行时变量，不进入编译期常量求值",
            "TC_RHS_FUNCALL_EXPR": "let 禁函数调用；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_MEMBLOCK_LOAD": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_MEMBLOCK_CONSTRUCTOR": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_MEMBLOCK_COUNT": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_FIELD_READ": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_LOAD": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_ADDRESS": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_ADD": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_SUB": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_EQ": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_NE": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_LT": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_LE": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_GT": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_GE": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_PTR_SIZE": "let 禁；运行时由 tc_eval_rhs 覆盖",
            "TC_RHS_SELF_MEMBER": "let 禁；运行时由 tc_eval_rhs 覆盖",
        },
    },
    {
        "path": "src/vm/executor/tc_executor.c",
        "func": "tc_eval_rhs",
        "line_func": 0,
        "skip": {},
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/aot/tc_aot_codegen.c",
        "func": "tc_aot_emit_rhs",
        "line_func": 0,
        "skip": {
            "TC_RHS_CAST": "最后一条 if 链后的 fallthrough 块处理（无显式 rhs->kind == 检查）",
        },
        "extra_kinds": ["TC_RHS_CONST_REF, TC_RHS_CONST_CAST"],
    },
    {
        "path": "src/aot/tc_aot_rt.c",
        "func": "tc_aot_* shim",
        "line_func": 0,
        "note": "按函数名分派（非 switch on kind），只需验证 shim 函数存在",
        "skip": {
            "TC_RHS_LIT": "LIT 在 codegen 内联展开",
            "TC_RHS_CONST_REF": "codegen 直接发射 let 位模式或 var slot，不产生 shim 调用",
            "TC_RHS_CONST_CAST": "代码生成时返回 -1，不产生 shim 调用",
            "TC_RHS_FUNCALL_EXPR": "codegen 内联发射函数调用，无独立 rt shim",
            "TC_RHS_SELF_MEMBER": "codegen 直接读 static/slot，无独立 rt shim",
            "TC_RHS_MEMBLOCK_LOAD": "tc_aot_memblock_* shim 族",
            "TC_RHS_MEMBLOCK_CONSTRUCTOR": "tc_aot_memblock_* shim 族",
            "TC_RHS_MEMBLOCK_COUNT": "tc_aot_memblock_* shim 族",
            "TC_RHS_STRUCT_CONSTRUCTOR": "tc_aot_struct_* shim 族",
            "TC_RHS_FIELD_READ": "tc_aot_struct_* shim 族",
            "TC_RHS_PTR_LOAD": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_ADDRESS": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_ADD": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_SUB": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_EQ": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_NE": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_LT": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_LE": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_GT": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_GE": "tc_aot_ptr_* shim 族",
            "TC_RHS_PTR_SIZE": "tc_aot_ptr_* shim 族",
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
        "TC_RHS_BITWISE_BIN": "tc_aot_bitwise_binary",
        "TC_RHS_BITWISE_UN": "tc_aot_bitwise_unary",
        "TC_RHS_SHIFT": "tc_aot_shift",
        "TC_RHS_CAST": "tc_aot_cast",
        "TC_RHS_FLOAT_ARITH": "tc_aot_fp_arith",
        "TC_RHS_FLOAT_UNARY": "tc_aot_fp_unary",
        "TC_RHS_FLOAT_COMPARE": "tc_aot_fp_compare",
        "TC_RHS_BITCAST": "tc_aot_bitcast",
    }

    present = set()
    for kind, func_name in shim_map.items():
        if re.search(rf'\b{func_name}\b', content):
            present.add(kind)

    return present


def check_dispatch_point(dp):
    """检查单个分发点是否完整覆盖所有 TC_RHS_KINDS"""
    path = dp["path"]
    skip = dict(TC_RHS_PHASE1_RESERVED)
    skip.update(dp["skip"])
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


KNOWLEDGE_GRAPH_PATH = os.path.join(REPO_ROOT, ".cursor/rules/knowledge-graph.mdc")


def find_function_def(path, func_name):
    """在文件中确认函数定义存在（跳过前向声明）；返回 1-based 行号，未找到返回 0。"""
    abs_path = os.path.join(REPO_ROOT, path)
    if not os.path.exists(abs_path):
        return 0
    sig_re = re.compile(
        rf'^(static\s+)?(int|void|TcRhsKind|const\s+char\s*\*)\s+{re.escape(func_name)}\s*\('
    )
    with open(abs_path, "r") as f:
        lines = f.readlines()
    i = 0
    while i < len(lines):
        if not sig_re.match(lines[i]):
            i += 1
            continue
        start = i
        for j in range(i, min(i + 20, len(lines))):
            if "{" in lines[j]:
                return start + 1
            if ";" in lines[j]:
                i = j + 1
                break
        else:
            i += 1
    return 0


def dispatch_rel_path(dp):
    return (
        os.path.basename(os.path.dirname(dp["path"]))
        + "/"
        + os.path.basename(dp["path"])
    )


def update_knowledge_graph_table():
    """将 DISPATCH_POINTS 的「函数→文件」写回 knowledge-graph.mdc（不含行号）。"""
    if not os.path.exists(KNOWLEDGE_GRAPH_PATH):
        print(f"warning: {KNOWLEDGE_GRAPH_PATH} not found", file=sys.stderr)
        return False
    with open(KNOWLEDGE_GRAPH_PATH, "r") as f:
        content = f.read()
    changed = False
    for dp in DISPATCH_POINTS:
        func = dp["func"]
        if func == "tc_aot_* shim":
            # 表格里写作 tc_aot_*
            kg_func = "tc_aot_*"
        else:
            kg_func = func
        rel_path = dispatch_rel_path(dp)
        # | `func` | old/path | 说明 |  → 更新文件列
        pattern = rf'(\| `{re.escape(kg_func)}` \| )[^|]+( \|)'
        new_content, count = re.subn(
            pattern, rf"\g<1>{rel_path}\2", content, count=1
        )
        if count and new_content != content:
            content = new_content
            changed = True
    if changed:
        with open(KNOWLEDGE_GRAPH_PATH, "w") as f:
            f.write(content)
    return changed


def verify_function_bindings():
    """按函数名校验各分发点实现仍存在于声明的文件中。"""
    missing = []
    ok = []
    for dp in DISPATCH_POINTS:
        func = dp["func"]
        path = dp["path"]
        if func == "tc_aot_* shim":
            ok.append((func, path, "shim-by-name"))
            continue
        line = find_function_def(path, func)
        if line:
            ok.append((func, path, f"@{line}"))
        else:
            missing.append((func, path))
    return ok, missing


def format_dispatch_table():
    """生成 Markdown 表格供 knowledge-graph.mdc 使用（函数名绑定，无行号）。"""
    lines = [
        "| 函数 | 文件 | 说明 |",
        "|------|------|------|",
    ]
    for dp in DISPATCH_POINTS:
        func = dp["func"]
        if func == "tc_aot_* shim":
            func = "tc_aot_*"
        path = dispatch_rel_path(dp)
        note = dp.get("note", "")
        skip_info = dp.get("skip", {})
        if skip_info:
            skipped = ", ".join(
                [f"{k}({v})" for k, v in skip_info.items()]
            )
            if note:
                note += "; "
            note += f"跳过: {skipped}"
        # 精简默认说明：无 note 时用空
        if not note:
            note = "—"
        lines.append(f"| `{func}` | {path} | {note} |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="TcRhsKind 分发点全覆盖检查"
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="详细输出")
    parser.add_argument(
        "--fix",
        action="store_true",
        help="按函数名校验绑定，并同步 knowledge-graph.mdc 文件路径（不再写行号）",
    )
    parser.add_argument("--table", action="store_true", help="仅输出知识图谱 Markdown 表格")
    args = parser.parse_args()

    all_errors = []
    all_warnings = []
    all_info = []

    if args.table:
        print(format_dispatch_table())
        return 0

    # 函数名绑定校验（替代旧行号同步）
    ok_binds, missing_binds = verify_function_bindings()
    if args.fix:
        if missing_binds:
            print("函数绑定缺失:")
            for func, path in missing_binds:
                print(f"  {func} not found in {path}")
        else:
            print("函数名绑定均有效:")
            for func, path, where in ok_binds:
                print(f"  {func} → {path} ({where})")
        if update_knowledge_graph_table():
            print(f"已写入 {KNOWLEDGE_GRAPH_PATH}")
        else:
            print("knowledge-graph.mdc 分发点表无路径变更")
    elif missing_binds:
        for func, path in missing_binds:
            all_errors.append(
                f"  [{func}] 在 {path} 中未找到函数定义（请检查 DISPATCH_POINTS 路径）"
            )

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
