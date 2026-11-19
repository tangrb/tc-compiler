#!/usr/bin/env python3
"""S10 static parity: tc-vm and tc-aot agree under riscv32-baremetal.

The static front end is shared by both tools.  This suite runs
`--static-only` on every conformance `.tc` source under the new
`riscv32-baremetal` profile and compares exit status, stdout and normalized
stderr.  Results are never compared across profiles.

Run:
    python3 tests/conformance/tools/s10_static_parity.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

PROFILE = "riscv32-baremetal"
CASE_ID = "TC-PAR-S10-0001"


def load_s9():
    path = ROOT / "tests/conformance/tools/s9_static_parity.py"
    spec = importlib.util.spec_from_file_location("s10_static_model", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    module = load_s9()
    vm = module.tool("tc-vm", "TC_VM")
    aot = module.tool("tc-aot", "TC_AOT")
    sources = sorted(module.STATIC.rglob("*.tc"))
    failures: list[str] = []
    for source in sources:
        include = f"--include={source.parent}"
        try:
            first = module.run(vm, PROFILE, [include, str(source)])
            second = module.run(aot, PROFILE, [include, str(source)])
        except subprocess.TimeoutExpired:
            failures.append(f"{source}: timed out")
            continue
        first_err = first.stderr.replace(b"tc-vm:", b"tc:")
        second_err = second.stderr.replace(b"tc-aot:", b"tc:")
        if (first.returncode, first.stdout, first_err) != (
                second.returncode, second.stdout, second_err):
            failures.append(
                f"{source}: VM rc={first.returncode} "
                f"AOT rc={second.returncode}")
    if failures:
        print(f"{CASE_ID}: {len(failures)} mismatch(es)")
        for item in failures[:20]:
            print(f"  {item}")
        return 1
    print(f"{CASE_ID}: {len(sources)} sources identical under {PROFILE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
