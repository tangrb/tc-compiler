#!/usr/bin/env python3
"""S10 front-end scale case: a legal source larger than one token-growth step.

The lexer grows its token array geometrically.  A switch with many labels is a
compact way to exceed 32768 tokens while staying far below the documented
source-size and token-size limits.  The case exercises the arena and parser at
that boundary and confirms that the front end, IR finalization and assembly
emission all accept the program.  It is profile-independent; the default
profile is a 32-bit AOT target so the complete pipeline is covered.

Run:
    python3 tests/conformance/tools/s10_frontend_scale.py --check
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-FSCALE-S10-0001"
PROFILE = "riscv32-baremetal"
CASES = 800


def make_source() -> str:
    lines = ["#program", "var x: int32 = copy int32, 1"]
    lines.append("_ = switch x, " +
                 ", ".join(f"L{i}" for i in range(CASES)) + ", Ld")
    lines.append("label Ld:")
    lines.append("_ = writeln int32, 999999")
    lines.append("_ = br done")
    for index in range(CASES):
        lines.append(f"label L{index}:")
        lines.append(f"_ = writeln int32, {index}")
        lines.append("_ = br done")
    lines.append("label done:")
    return "\n".join(lines) + "\n"


def run(argv: list[str]) -> tuple[int, str]:
    proc = subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=120)
    return proc.returncode, proc.stderr.decode("utf-8", "replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="tc-fscale-") as name:
        directory = Path(name)
        source = directory / "root.tc"
        source.write_text(make_source(), encoding="utf-8")
        common = [f"--profile={PROFILE}", f"--include={directory}"]
        for tool, extra in (
                (engine.tc_vm_path(), ["--syntax-only"]),
                (engine.tc_vm_path(), ["--sem-only"]),
                (engine.tc_vm_path(), ["--static-only"]),
                (engine.tc_aot_path(), ["--emit=asm",
                                        f"--output={directory}/out.s"])):
            code, stderr = run([tool, *extra, *common, str(source)])
            if code != 0:
                failures.append(f"{CASE_ID}: {tool} {extra[-1]} exit {code}: "
                                f"{stderr.strip()[:200]}")
        if not failures:
            text = (directory / "out.s").read_text(encoding="utf-8")
            if "tc_function" not in text and "tc_main" not in text:
                failures.append(f"{CASE_ID}: emitted assembly has no entry")
    if failures:
        for item in failures:
            print(item)
        return 1
    print(f"{CASE_ID}: {CASES}-case switch passed syntax/sem/static/assembly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
