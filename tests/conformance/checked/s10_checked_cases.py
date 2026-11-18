#!/usr/bin/env python3
"""S10 checked-build UB cases on riscv32-baremetal.

Reuses the W=64 checked fixtures (uninitialized storage, one-past, dangling,
out-of-object and accepted controls) and runs each under the RISC-V 32 profile
with ``--checked``.  Both tools must agree on stdout/trap/exit.  The W=64 expected trap code is
not asserted: the checked shadow is address-range based and the W=32 frame
layout can place a one-past byte in padding or an adjacent live object, which
the checked model permits (the same limitation is recorded in
``src/CONTRACTS.md``).  W=64-only programs that become static rejections under
W=32 are compared as rejections.

Run:
    python3 tests/conformance/checked/s10_checked_cases.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402
from diff.engine import Case  # noqa: E402

PROFILE = "riscv32-baremetal"
CASE_ID = "TC-CHK-S10-0001"


def load_cases():
    spec = importlib.util.spec_from_file_location(
        "s8_checked", HERE / "s8_checked_cases.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules["s8_checked"] = module
    spec.loader.exec_module(module)
    return module.CHECKED_CASES


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    total = 0
    traps = 0
    for case in load_cases():
        directory = Path(tempfile.mkdtemp(prefix=f"tc-s10-chk-{case.case_id}-"))
        checked = Case(case_id=case.case_id, profile="w32",
                       files=case.files, stdout=case.stdout, trap=case.trap,
                       exit=case.exit, stdin=case.stdin, checked=True,
                       opt=case.opt, spec=case.spec)
        try:
            vm, aot, build = engine.run_case(checked, PROFILE, directory)
            total += 1
            if aot is None:
                if vm.exit != build.exit:
                    failures.append(
                        f"{case.case_id}: static rejection mismatch VM "
                        f"{vm.exit} vs AOT {build.exit}")
                continue
            if (vm.stdout, vm.trap, vm.exit) != (aot.stdout, aot.trap,
                                                 aot.exit):
                failures.append(
                    f"{case.case_id}: VM {vm.stdout!r}/{vm.trap}/{vm.exit} "
                    f"!= AOT {aot.stdout!r}/{aot.trap}/{aot.exit}")
                continue
            if case.trap is not None:
                traps += 1
        except Exception as exc:  # noqa: BLE001 - report fixture failure
            failures.append(f"{case.case_id}: exception {exc!r}")
        finally:
            shutil.rmtree(directory, ignore_errors=True)
    if failures:
        for item in failures:
            print(item)
        print(f"{CASE_ID}: {len(failures)} of {total} cases failed")
        return 1
    print(f"{CASE_ID}: {total} checked cases passed "
          f"({traps} UB expected, remainder accepted/static)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
