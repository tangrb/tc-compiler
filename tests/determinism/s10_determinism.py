#!/usr/bin/env python3
"""S10 RISC-V determinism: assembly, ELF and execution repeatability.

Reuses the S9 determinism corpus under `riscv32-baremetal`:
  - repeated assembly emission is byte-identical;
  - O0 and O2 emit the same assembly (zero-pass baseline);
  - repeated ELF builds are byte-identical;
  - repeated execution observes the same stdout and exit record;
  - checked-build assembly is repeatable and differs from release.

Run:
    python3 tests/determinism/s10_determinism.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

CASE_ID = "TC-DET-S10-0001"


def load_s9():
    path = ROOT / "tests/determinism/s9_determinism.py"
    spec = importlib.util.spec_from_file_location("s10_determinism_model",
                                                  path)
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
    module.CASE_ID = CASE_ID
    failures: list[str] = []
    module.check_profile("riscv32-baremetal", failures)
    if failures:
        for failure in failures:
            print(failure)
        return 1
    print(f"{CASE_ID}: RISC-V 32 assembly, ELF and execution are repeatable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
