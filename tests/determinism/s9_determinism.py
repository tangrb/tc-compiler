#!/usr/bin/env python3
"""S9 determinism: ARM32/AArch64 bare-metal assembly, ELF and execution.

Checks, per profile and without comparing results across profiles:
  - repeated assembly emission is byte-identical;
  - O0 and O2 emit the same assembly (zero-pass baseline);
  - repeated ELF builds are byte-identical;
  - repeated execution observes the same stdout and exit record;
  - a checked-build assembly is itself repeatable and differs from release.

Run:
    python3 tests/determinism/s9_determinism.py --check
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-DET-S9-0001"

SOURCE = """#program
import lib
var a: int32 = call lib.calc, 7
_ = writeln int32, a
var p: lib.Pair = make lib.Pair, 10, 20
var q: lib.Pair = call lib.id, p
_ = writeln int32, q.y
var f: float64 = div float64, 1.0, 3.0
_ = writeln float64, f
var i: int64 = mul int64, 3037000499, 3037000499
_ = writeln int64, i
"""

LIB = """#lib
public struct Pair then
var x: int32
var y: int32
end
public func calc(x: int32) int32 then
var r: int32 = mul int32, x, 6
_ = return r
end
public func id(p: Pair) Pair then
_ = return p
end
"""


def write(directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "lib.tc").write_text(LIB, encoding="utf-8")
    root = directory / "root.tc"
    root.write_text(SOURCE, encoding="utf-8")
    return root


def emit(root: Path, output: Path, profile: str, *extra: str) -> bytes:
    proc = subprocess.run(
        [engine.tc_aot_path(), f"--profile={profile}", "--emit=asm",
         f"--output={output}", f"--include={root.parent}", *extra,
         str(root)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if proc.returncode != 0:
        raise SystemExit(f"assembly emission failed: "
                         f"{proc.stderr.decode('utf-8', 'replace')}")
    return output.read_bytes()


def link(root: Path, output: Path, profile: str, *extra: str) -> bytes:
    proc = subprocess.run(
        [engine.tc_aot_path(), f"--profile={profile}", "--emit=elf",
         f"--output={output}", f"--include={root.parent}",
         f"--ld={engine.lld_path()}", *extra, str(root)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=240)
    if proc.returncode != 0:
        raise SystemExit(f"ELF link failed: "
                         f"{proc.stderr.decode('utf-8', 'replace')}")
    return output.read_bytes()


def execute(profile: str, elf: Path, stdin: bytes, work: Path):
    work.mkdir(parents=True, exist_ok=True)
    if profile == "arm32-baremetal":
        return engine.run_arm32_elf(elf, stdin, work)
    return engine.run_aarch64_baremetal_elf(elf, stdin, work)


def check_profile(profile: str, failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s9-det-")
    try:
        root = write(directory)
        first = emit(root, directory / "a1.s", profile)
        second = emit(root, directory / "a2.s", profile)
        if first != second:
            failures.append(f"{CASE_ID}: {profile}: repeated .s differs")
        for opt in ("O0", "O2"):
            other = emit(root, directory / f"{opt}.s", profile,
                         f"--opt={opt}")
            if other != first:
                failures.append(
                    f"{CASE_ID}: {profile}: {opt} .s differs from zero-pass")
        elf_a = link(root, directory / "a.elf", profile)
        elf_b = link(root, directory / "b.elf", profile)
        if elf_a != elf_b:
            failures.append(f"{CASE_ID}: {profile}: repeated ELF differs")
        elf = directory / "a.elf"
        observations = []
        for index in range(3):
            obs = execute(profile, elf, b"", directory / f"run{index}")
            observations.append((obs.stdout, obs.trap, obs.exit))
        if len(set(observations)) != 1:
            failures.append(
                f"{CASE_ID}: {profile}: repeated execution differs")
        if observations[0][2] != 0:
            failures.append(
                f"{CASE_ID}: {profile}: determinism program exited "
                f"{observations[0][2]} trap {observations[0][1]}")
        if profile == "arm32-baremetal":
            checked = emit(root, directory / "checked1.s", profile,
                           "--checked")
            checked2 = emit(root, directory / "checked2.s", profile,
                            "--checked")
            if checked != checked2:
                failures.append(
                    f"{CASE_ID}: {profile}: repeated checked .s differs")
            if checked == first:
                failures.append(
                    f"{CASE_ID}: {profile}: checked .s equals release .s")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    check_profile("arm32-baremetal", failures)
    check_profile("aarch64-baremetal", failures)
    if failures:
        for failure in failures:
            print(failure)
        return 1
    digest = hashlib.sha256(SOURCE.encode()).hexdigest()[:12]
    print(f"{CASE_ID}: ARM32/AArch64 assembly, ELF and execution are "
          f"repeatable (corpus {digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
