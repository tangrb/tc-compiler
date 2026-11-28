#!/usr/bin/env python3
"""T6 reproducibility: repeatable and root-independent debug artifacts."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-REPRO-0.0.47-0001"
PROFILES = ("aarch64-baremetal", "arm32-baremetal", "riscv32-baremetal")
SOURCE = """#program
import lib
var value: int32 = call lib.calc, 7
_ = writeln int32, value
"""
LIB = """#lib
public func calc(x: int32) int32 then
var r: int32 = mul int32, x, 6
_ = return r
end
"""


def write(directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "lib.tc").write_text(LIB, encoding="utf-8")
    root = directory / "root.tc"
    root.write_text(SOURCE, encoding="utf-8")
    return root


def emit(root: Path, output: Path, profile: str, kind: str) -> bytes:
    command = [engine.tc_aot_path(), f"--profile={profile}", "--debug",
               f"--emit={kind}", f"--output={output}",
               f"--include={root.parent}", f"--ld={engine.lld_path()}",
               str(root)]
    proc = subprocess.run(command, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=240)
    if proc.returncode != 0:
        raise RuntimeError(f"{profile} {kind} failed: "
                           f"{proc.stderr.decode('utf-8', 'replace').strip()}")
    return output.read_bytes()


def check_profile(profile: str, directory: Path,
                  failures: list[str]) -> None:
    first_root = write(directory / "first")
    second_root = write(directory / "second")
    for kind in ("asm", "obj", "elf", "flat"):
        first = emit(first_root, directory / f"first.{kind}", profile, kind)
        repeated = emit(first_root, directory / f"repeat.{kind}", profile, kind)
        relocated = emit(second_root, directory / f"second.{kind}", profile, kind)
        if first != repeated:
            failures.append(f"{CASE_ID}: {profile}: repeated debug {kind} differs")
        if first != relocated:
            failures.append(
                f"{CASE_ID}: {profile}: cross-root debug {kind} differs")
        if str(first_root.parent).encode() in first or \
                str(second_root.parent).encode() in relocated:
            failures.append(f"{CASE_ID}: {profile}: debug {kind} has absolute paths")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    directory = engine.temporary_directory("tc-t6-repro-")
    failures: list[str] = []
    try:
        for profile in PROFILES:
            check_profile(profile, directory / profile, failures)
    except (OSError, RuntimeError) as error:
        failures.append(f"{CASE_ID}: {error}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        for failure in failures:
            print(failure)
        return 1
    print(f"{CASE_ID}: three-profile debug asm/obj/elf/flat artifacts are "
          "repeatable and root-independent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
