#!/usr/bin/env python3
"""S10 long-term regression corpus and cross-target sample matrix.

The corpus is deterministic and independent of the implementation's output:

  * standard-clause / boundary corpus: a fixed sample of every conformance
    static `.tc` source is checked twice under `w32` and `w64`; the first
    diagnostic, stdout and stderr must be byte-identical between repetitions;
  * differential corpus: a fixed sample of the S8 case table is executed on
    every AOT target and compared VM vs AOT within the same profile;
  * fuzz regression corpus: fixed seeds of the ABI/layout/memory/float
    generators are replayed on every AOT target;
  * platform corpus: one common program is built and executed on all three
    freestanding targets.

Results are never compared across 32/64 profiles.  The expected W=64 values
are checked on the 64-bit target only; on W=32 targets the VM is the same
profile's reference and only VM/AOT agreement is required.

Run:
    python3 tests/conformance/longterm/s10_longterm.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-LT-S10-0001"
PROFILES = ("aarch64-baremetal", "arm32-baremetal", "riscv32-baremetal")
W32_PROFILES = {"arm32-baremetal", "riscv32-baremetal"}


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def check_static_corpus(failures: list[str]) -> int:
    sources = sorted((ROOT / "tests/conformance/static").rglob("*.tc"))
    sample = sources[::7]
    for profile in ("w32", "w64"):
        for source in sample:
            argv = [engine.tc_vm_path(), "--static-only",
                    f"--profile={profile}",
                    f"--include={source.parent}", str(source)]
            first = subprocess.run(argv, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, timeout=60)
            second = subprocess.run(argv, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=60)
            if (first.returncode, first.stdout, first.stderr) != (
                    second.returncode, second.stdout, second.stderr):
                failures.append(f"{CASE_ID}: static corpus {source} ({profile}) "
                                "is not repeatable")
    return len(sample)


def run_diff_case(case, profile: str, failures: list[str],
                  check_expected: bool) -> None:
    directory = engine.temporary_directory("tc-s10-lt-diff-")
    try:
        vm, aot, build = engine.run_case(case, profile, directory)
        if aot is None:
            failures.append(f"{CASE_ID}: {case.case_id} ({profile}): AOT "
                            f"rejected {build.exit} {build.stderr[:160]}")
            return
        if (vm.stdout, vm.trap, vm.exit) != (aot.stdout, aot.trap, aot.exit):
            failures.append(
                f"{CASE_ID}: {case.case_id} ({profile}): VM "
                f"{vm.stdout!r}/{vm.trap}/{vm.exit} != AOT "
                f"{aot.stdout!r}/{aot.trap}/{aot.exit}")
        if check_expected and (vm.stdout != case.stdout.encode() or
                               vm.trap != case.trap or vm.exit != case.exit):
            failures.append(
                f"{CASE_ID}: {case.case_id} ({profile}): VM "
                f"{vm.stdout!r}/{vm.trap}/{vm.exit} != expected "
                f"{case.stdout.encode()!r}/{case.trap}/{case.exit}")
    except Exception as exc:  # noqa: BLE001 - report and continue
        failures.append(f"{CASE_ID}: {case.case_id} ({profile}): {exc}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def check_differential(failures: list[str]) -> int:
    module = load("lt_s8_cases", "tests/conformance/diff/s8_cases.py")
    sample = list(module.CASES)[::11]
    for profile in PROFILES:
        for case in sample:
            run_diff_case(case, profile, failures,
                          check_expected=profile == "aarch64-baremetal")
    return len(sample)


def make_case(case_id: str, files: dict[str, str], expected: str):
    return engine.make_case(case_id, files, expected,
                            trap=None, stdin=b"", checked=False,
                            spec="generated regression corpus")


def check_fuzz(failures: list[str]) -> int:
    abi = load("lt_abi_fuzz", "tests/conformance/diff/s8_abi_fuzz.py")
    fp = load("lt_fp_fuzz", "tests/conformance/diff/s8_fp_fuzz.py")
    layout = load("lt_layout_fuzz",
                  "tests/conformance/diff/s8_layout_fuzz.py")
    mem = load("lt_mem_fuzz", "tests/conformance/diff/s8_mem_fuzz.py")
    generated = 0
    for profile in PROFILES:
        for seed in (0, 1):
            files, expected = abi.build(seed)
            case = make_case(f"TC-FUZZ-ABI-{seed}",
                             {"root.tc": files[0], "lib.tc": files[1]},
                             expected)
            run_diff_case(case, profile, failures,
                          check_expected=profile == "aarch64-baremetal")
            generated += 1
        for seed in (0,):
            root, expected = fp.gen(seed)
            case = make_case(f"TC-FUZZ-FP-{seed}", {"root.tc": root},
                             expected)
            run_diff_case(case, profile, failures,
                          check_expected=profile == "aarch64-baremetal")
            generated += 1
            root, lib, expected = layout.gen(seed)
            case = make_case(f"TC-FUZZ-LAYOUT-{seed}",
                             {"root.tc": root, "lib.tc": lib}, expected)
            run_diff_case(case, profile, failures,
                          check_expected=profile == "aarch64-baremetal")
            generated += 1
            root, expected = mem.gen(seed)
            case = make_case(f"TC-FUZZ-MEM-{seed}", {"root.tc": root},
                             expected)
            run_diff_case(case, profile, failures,
                          check_expected=profile == "aarch64-baremetal")
            generated += 1
    return generated


PLATFORM_SOURCE = """#program
import lib
var x: lib.Pair = call lib.build_pair, 7
var y: int32 = call lib.sum, x
_ = writeln int32, y
var f: float64 = div float64, 1.0, 3.0
_ = writeln float64, f
var i: int64 = mul int64, 3037000499, 3037000499
_ = writeln int64, i
"""

PLATFORM_LIB = """#lib
public struct Pair then
var x: int32
var y: int32
end
public func build_pair(x: int32) Pair then
var y: int32 = add int32, x, 1
var p: Pair = make Pair, x, y
_ = return p
end
public func sum(p: Pair) int32 then
var r: int32 = add int32, p.x, p.y
_ = return r
end
"""


def check_platform(failures: list[str]) -> int:
    expected = "15\n0.333333\n9223372030926249001\n"
    case = engine.Case(
        case_id="TC-LT-PLAT", profile="aarch64-baremetal",
        files={"root.tc": PLATFORM_SOURCE, "lib.tc": PLATFORM_LIB},
        stdout=expected, trap=None, exit=0, stdin=b"",
        spec="long-term platform corpus")
    for profile in PROFILES:
        run_diff_case(engine.Case(
            case_id="TC-LT-PLAT", profile=profile, files=case.files,
            stdout=expected, trap=None, exit=0, stdin=b"",
            spec="long-term platform corpus"), profile, failures,
            check_expected=True)
    return len(PROFILES)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    static_count = check_static_corpus(failures)
    diff_count = check_differential(failures)
    fuzz_count = check_fuzz(failures)
    platform_count = check_platform(failures)
    if failures:
        for item in failures:
            print(item)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: long-term corpus passed "
          f"({static_count} static sources x2 profiles, "
          f"{diff_count} differential cases x{len(PROFILES)} targets, "
          f"{fuzz_count} fuzz cases, {platform_count} platform targets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
