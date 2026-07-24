#!/usr/bin/env python3
"""
TC-Compiler: 源文件 tc_*.h / tc_*.c 命名一致性检查

规则（与 coding-standards §源文件命名 一致）：
  1. src/ 下实现文件须为 tc_<module>.c，或与 tc_<module>.h 成对
  2. 例外：CLI 入口 main.c（可无对应头文件）
  3. 例外：仅头文件模块 tc_version.h（可无 .c）

用法:
    python3 scripts/sync/check_source_naming.py
    python3 scripts/sync/check_source_naming.py --verbose

返回码: 0=通过, 1=违规
"""

import argparse
import os
import sys

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC_ROOT = os.path.join(REPO_ROOT, "src")

ENTRY_POINT_C = frozenset({"main.c"})
HEADER_ONLY_H = frozenset({
    "tc_version.h",
    "tc_parser_internal.h",  # parser 子模块共享声明，实现在 tc_parser.c
    "tc_analyzer_internal.h",  # analyzer 子模块共享类型/声明
    "tc_executor_internal.h",  # executor / ptr / memblock 共享上下文
    "tc_stmt_index.h",  # header-only inline 实现
})


def iter_source_files(root):
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if name.endswith((".c", ".h")):
                yield os.path.join(dirpath, name)


def rel(path):
    return os.path.relpath(path, REPO_ROOT)


def check_naming(verbose=False):
    errors = []
    checked_pairs = []

    c_files = []
    h_files = []
    for path in iter_source_files(SRC_ROOT):
        name = os.path.basename(path)
        if name.endswith(".c"):
            c_files.append(path)
        else:
            h_files.append(path)

    for c_path in sorted(c_files):
        name = os.path.basename(c_path)
        directory = os.path.dirname(c_path)
        stem, _ = os.path.splitext(name)

        if name in ENTRY_POINT_C:
            if verbose:
                print(f"  [ok] 入口 {rel(c_path)}")
            continue

        if not stem.startswith("tc_"):
            errors.append(
                f"{rel(c_path)}: 实现文件须为 tc_<module>.c（入口 main.c 除外）"
            )
            continue

        h_path = os.path.join(directory, stem + ".h")
        if not os.path.isfile(h_path):
            errors.append(
                f"{rel(c_path)}: 缺少同目录配对头文件 {stem}.h"
            )
            continue

        checked_pairs.append((rel(c_path), rel(h_path)))
        if verbose:
            print(f"  [ok] {rel(c_path)} ↔ {rel(h_path)}")

    for h_path in sorted(h_files):
        name = os.path.basename(h_path)
        directory = os.path.dirname(h_path)
        stem, _ = os.path.splitext(name)

        if name in HEADER_ONLY_H:
            if verbose:
                print(f"  [ok] 仅头文件 {rel(h_path)}")
            continue

        if not stem.startswith("tc_"):
            errors.append(
                f"{rel(h_path)}: 头文件须为 tc_<module>.h"
            )
            continue

        c_path = os.path.join(directory, stem + ".c")
        if not os.path.isfile(c_path):
            errors.append(
                f"{rel(h_path)}: 缺少同目录配对实现文件 {stem}.c"
            )

    return errors, checked_pairs


def main():
    parser = argparse.ArgumentParser(description="检查 src/ 下 tc_*.h / tc_*.c 命名一致性")
    parser.add_argument("--verbose", action="store_true", help="列出已检查的成对文件")
    args = parser.parse_args()

    print("=" * 60)
    print("源文件命名一致性检查 (tc_<module>.h ↔ tc_<module>.c)")
    print("=" * 60)

    errors, pairs = check_naming(verbose=args.verbose)

    if args.verbose and pairs:
        print()
        print(f"--- 已验证 {len(pairs)} 对 ---")

    if errors:
        print()
        for msg in errors:
            print(f"❌ {msg}")
        print()
        print(f"共 {len(errors)} 处违规")
        return 1

    print()
    print(f"✅ 所有 src/ 源文件符合 tc_<module>.h ↔ tc_<module>.c 命名约定")
    return 0


if __name__ == "__main__":
    sys.exit(main())
