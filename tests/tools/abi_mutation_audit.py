#!/usr/bin/env python3
"""Mutation audit for the ABI argument planners.

This is a development audit, not a ctest gate.  It copies the source tree,
injects one classification bug into an ``emit_abi.c`` file, builds a mutant
``tc-aot`` in the copy, and checks whether an independent C-interop suite
detects it.  A survivor is either a test gap or an equivalent mutant; it must
be classified, not ignored.

The VM/AOT differential is not the primary oracle here: the VM has no ABI and
a TC-to-TC call uses the same planner on both sides, so a classification bug
can be self-consistent.  The C caller/callee suites are the kill criterion.

Run:
    python3 tests/tools/abi_mutation_audit.py --check
    python3 tests/tools/abi_mutation_audit.py --mutant a64_hfa_exhaust --keep
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLANG = shutil.which("clang")
LLD = shutil.which("ld.lld")
QEMU_A64 = shutil.which("qemu-system-aarch64")
QEMU_ARM = shutil.which("qemu-system-arm")
QEMU_RISCV32 = shutil.which("qemu-system-riscv32")

MUTANTS = [
    {
        "name": "a64_b4_threshold",
        "file": "src/backend/aarch64/emit_abi.c",
        "old": "if (size <= 16u) {\n                unsigned chunks =",
        "new": "if (size <= 24u) {\n                unsigned chunks =",
        "count": 1,
        "suites": ["tests/conformance/abi/reverse_interop_cases.py"],
        "expect": "24-byte composite is passed by value instead of by pointer",
    },
    {
        "name": "a64_hfa_exhaust",
        "file": "src/backend/aarch64/emit_abi.c",
        "old": "if (nsrn + hfa <= 8u) {",
        "new": "if (nsrn + hfa <= 9u) {",
        "count": 1,
        "suites": ["tests/conformance/abi/reverse_interop_cases.py"],
        "expect": "HFA after seven v registers stays in v7/v8 instead of stack",
    },
    {
        "name": "a64_composite_ngrn",
        "file": "src/backend/aarch64/emit_abi.c",
        "old": "ngrn += chunks;",
        "new": "ngrn += 0;",
        "count": 1,
        "suites": ["tests/conformance/abi/s8_interop_cases.py"],
        "expect": "following arguments reuse the composite's x registers",
    },
    {
        "name": "riscv_split_aggregate",
        "file": "src/backend/riscv32/emit_abi.c",
        "old": "if (*ncrn == 7u && words == 2u) {",
        "new": "if (0) {",
        "count": 1,
        "suites": ["tests/conformance/abi/s10_riscv32_interop.py"],
        "expect": "one-register 2*XLEN aggregate is not split to the stack",
    },
    {
        "name": "arm32_c5_underflow",
        "file": "src/backend/arm32/emit_abi.c",
        "old": "unsigned reg_words = 4u - *ncrn;",
        "new": "unsigned reg_words = 4u - *ncrn - 1u;",
        "count": 1,
        "suites": ["tests/conformance/abi/s9_arm32_interop.py"],
        "expect": "Rule C.5 split puts one too many composite words on the stack",
    },
]


def copy_tree(target: Path) -> None:
    shutil.copytree(
        ROOT, target,
        ignore=shutil.ignore_patterns(
            ".git", "build", "build-*", "__pycache__", "*.pyc", "*.pyo"))


def configure_and_build(source: Path, build: Path) -> tuple[bool, str]:
    configure = subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build),
         "-DTC_ENABLE_AOT_TESTS=ON"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    if configure.returncode != 0:
        return False, configure.stdout.decode("utf-8", "replace")
    built = subprocess.run(
        ["cmake", "--build", str(build), "--target", "tc-aot",
         "tc_aarch64_runtime", "tc_arm32_runtime", "tc_riscv32_runtime",
         "-j", "8"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    return built.returncode == 0, built.stdout.decode("utf-8", "replace")


def run_suite(source: Path, build: Path, suite: str) -> tuple[bool, str]:
    env = dict(os.environ)
    env["TC_AOT"] = str(build / "src/driver/tc-aot")
    env["TC_AOT_LINKER"] = LLD or env.get("TC_AOT_LINKER", "")
    env["TC_AOT_QEMU_SYSTEM"] = QEMU_A64 or env.get("TC_AOT_QEMU_SYSTEM", "")
    env["TC_AOT_QEMU_SYSTEM_ARM"] = QEMU_ARM or env.get(
        "TC_AOT_QEMU_SYSTEM_ARM", "")
    env["TC_AOT_QEMU_SYSTEM_RISCV32"] = QEMU_RISCV32 or env.get(
        "TC_AOT_QEMU_SYSTEM_RISCV32", "")
    proc = subprocess.run(
        [sys.executable, str(source / suite), "--check"],
        cwd=str(source), env=env, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=900)
    text = proc.stdout.decode("utf-8", "replace")
    return proc.returncode == 0, text


def audit_one(mutant: dict, keep: bool) -> bool:
    temp = Path(tempfile.mkdtemp(prefix=f"tc-mutant-{mutant['name']}-"))
    source = temp / "src-tree"
    build = temp / "build"
    try:
        copy_tree(source)
        path = source / mutant["file"]
        text = path.read_text(encoding="utf-8")
        if text.count(mutant["old"]) != mutant["count"]:
            print(f"{mutant['name']}: patch site count mismatch")
            return False
        path.write_text(text.replace(mutant["old"], mutant["new"],
                                     mutant["count"]), encoding="utf-8")
        print(f"{mutant['name']}: building ({mutant['file']})")
        ok, log = configure_and_build(source, build)
        if not ok:
            print(f"{mutant['name']}: build failed")
            print("\n".join(log.splitlines()[-20:]))
            return False
        for suite in mutant["suites"]:
            passed, output = run_suite(source, build, suite)
            if not passed:
                print(f"{mutant['name']}: KILLED by {suite}")
                tail = "\n".join(output.splitlines()[-8:])
                print(tail)
                return True
            print(f"{mutant['name']}: survived {suite}")
        print(f"{mutant['name']}: SURVIVED ({mutant['expect']})")
        return False
    finally:
        if keep:
            print(f"{mutant['name']}: kept {temp}")
        else:
            shutil.rmtree(temp, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--mutant", action="append", choices=[m["name"] for m in MUTANTS])
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check and not args.mutant:
        parser.error("use --check or --mutant NAME")
    if CLANG is None or LLD is None:
        print("mutation.audit: clang and ld.lld are required")
        return 1
    selected = [m for m in MUTANTS
                if args.mutant is None or m["name"] in args.mutant]
    survivors = 0
    for mutant in selected:
        if not audit_one(mutant, args.keep):
            survivors += 1
    if survivors:
        print(f"mutation.audit: {survivors} survivor(s) of {len(selected)}")
        return 1
    print(f"mutation.audit: all {len(selected)} mutant(s) killed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
