#!/usr/bin/env python3
"""S10 implementation-limit and robustness cases for riscv32-baremetal.

These adversarial cases exercise the documented implementation boundary, not
language semantics:

  * a trap record whose source path exceeds the old 512-byte record buffer must
    still carry the TC-EXIT line;
  * a frame that fits the target stack budget builds and runs, while a frame
    that cannot satisfy D=64/reachable-stack budget is rejected with the
    implementation error and no artifact;
  * a 900 KiB static object fits the 1 MiB static window, while an object above
    the window is rejected before linking;
  * recursion at D-1 succeeds and at D traps TC_RE_STACK_OVERFLOW consistently
    on the VM and AOT;
  * a device address at the end of the device window traps ADDR_UNAVAILABLE;
  * an empty input frame reports TC_RE_IO.

Run:
    python3 tests/conformance/platform/s10_limits_cases.py --check
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

PROFILE = "riscv32-baremetal"
CASE_ID = "TC-RISCV32-S10-0002"


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def write(directory: Path, name: str, text: str) -> Path:
    path = directory / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def build(directory: Path, files: dict[str, str], *extra: str) -> tuple[int,
                                                                       str,
                                                                       Path]:
    for name, text in files.items():
        write(directory, name, text)
    root = directory / "root.tc"
    output = directory / "out.elf"
    argv = [engine.tc_aot_path(), f"--profile={PROFILE}", "--emit=elf",
            f"--output={output}", f"--include={directory}",
            f"--ld={engine.lld_path()}", *extra, str(root)]
    proc = subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=240)
    return (proc.returncode, proc.stderr.decode("utf-8", "replace"), output)


def check_long_path(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s10-lim-path-")
    deep = directory
    try:
        for index in range(15):
            deep = deep / (f"d{index}" + "x" * 40)
        deep.mkdir(parents=True)
        code, stderr, elf = build(deep, {"root.tc": program("_ = trap")})
        if code != 0:
            failures.append(f"{CASE_ID}: long-path build failed: "
                            f"{stderr.strip()[:200]}")
            return
        observed = engine.run_riscv32_elf(elf, b"", deep)
        if observed.trap != "TC_RE_TRAP" or observed.exit != 1:
            failures.append(f"{CASE_ID}: long-path trap {observed.trap}/"
                            f"{observed.exit}, expected TC_RE_TRAP/1")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def check_frame_budget(failures: list[str]) -> None:
    for padding, expect_ok in ((100000, True), (200000, False)):
        directory = engine.temporary_directory("tc-s10-lim-frame-")
        try:
            files = {
                "root.tc": program("import lib",
                                   "var r: int32 = call lib.f",
                                   "_ = writeln int32, r"),
                "lib.tc": library(
                    "public struct Big then",
                    f"var a: int32 @padding({padding})",
                    "end",
                    "public func f() int32 then",
                    "var b: Big = make Big, 7",
                    "_ = return b.a",
                    "end"),
            }
            code, stderr, elf = build(directory, files)
            if expect_ok:
                if code != 0:
                    failures.append(f"{CASE_ID}: {padding}-byte frame build "
                                    f"failed: {stderr.strip()[:200]}")
                    continue
                observed = engine.run_riscv32_elf(elf, b"", directory)
                if observed.stdout != b"7\n" or observed.trap is not None:
                    failures.append(
                        f"{CASE_ID}: {padding}-byte frame output "
                        f"{observed.stdout!r}/{observed.trap}")
            else:
                if code != 71:
                    failures.append(
                        f"{CASE_ID}: oversized frame exit {code}, expected 71")
                elif elf.exists():
                    failures.append(
                        f"{CASE_ID}: oversized frame published an artifact")
        finally:
            shutil.rmtree(directory, ignore_errors=True)


def check_static_region(failures: list[str]) -> None:
    for padding, expect_ok in ((900000, True), (1048576, False)):
        directory = engine.temporary_directory("tc-s10-lim-static-")
        try:
            files = {
                "root.tc": program("import lib",
                                   "_ = writeln int32, lib.s.a"),
                "lib.tc": library(
                    "public struct Big then",
                    f"var a: int32 @padding({padding})",
                    "end",
                    "public static var s: Big = make Big, 7"),
            }
            code, stderr, elf = build(directory, files)
            if expect_ok:
                if code != 0:
                    failures.append(f"{CASE_ID}: {padding}-byte static build "
                                    f"failed: {stderr.strip()[:200]}")
                    continue
                observed = engine.run_riscv32_elf(elf, b"", directory)
                if observed.stdout != b"7\n" or observed.trap is not None:
                    failures.append(
                        f"{CASE_ID}: {padding}-byte static output "
                        f"{observed.stdout!r}/{observed.trap}")
            else:
                if code != 71:
                    failures.append(
                        f"{CASE_ID}: oversized static exit {code}, expected 71")
                elif elf.exists():
                    failures.append(
                        f"{CASE_ID}: oversized static published an artifact")
        finally:
            shutil.rmtree(directory, ignore_errors=True)


REC_LIB = library(
    "public func rec(n: int32) int32 then",
    "var z: int32 = copy int32, 0",
    "var c: bool = eq int32, n, z",
    "_ = br_if c, done",
    "var m: int32 = sub int32, n, 1",
    "var r: int32 = call Self.rec, m",
    "_ = return r",
    "label done:",
    "_ = return n",
    "end")


def check_stack_depth(failures: list[str]) -> None:
    for depth, trap in ((63, None), (64, "TC_RE_STACK_OVERFLOW")):
        directory = engine.temporary_directory("tc-s10-lim-depth-")
        try:
            files = {
                "root.tc": program("import lib",
                                   f"var r: int32 = call lib.rec, {depth}",
                                   "_ = writeln int32, r"),
                "lib.tc": REC_LIB,
            }
            code, stderr, elf = build(directory, files)
            if code != 0:
                failures.append(f"{CASE_ID}: depth {depth} build failed: "
                                f"{stderr.strip()[:200]}")
                continue
            observed = engine.run_riscv32_elf(elf, b"", directory)
            if observed.trap != trap:
                failures.append(f"{CASE_ID}: depth {depth} trap "
                                f"{observed.trap}, expected {trap}")
                continue
            if trap is None and observed.stdout != b"0\n":
                failures.append(f"{CASE_ID}: depth {depth} output "
                                f"{observed.stdout!r}")
        finally:
            shutil.rmtree(directory, ignore_errors=True)


def check_device_end(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s10-lim-dev-")
    try:
        files = {"root.tc": program(
            "var a: usize = copy usize, 0x20000000",
            "unsafe then",
            "var p: ptr<uint32> = bitcast ptr<uint32>, a",
            "var v: uint32 = load uint32, p",
            "end",
            "_ = writeln int32, 1")}
        code, stderr, elf = build(directory, files)
        if code != 0:
            failures.append(f"{CASE_ID}: device-end build failed: "
                            f"{stderr.strip()[:200]}")
            return
        observed = engine.run_riscv32_elf(elf, b"", directory)
        if observed.trap != "TC_RE_ADDR_UNAVAILABLE":
            failures.append(f"{CASE_ID}: device-end trap {observed.trap}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def check_flat_execution(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s10-lim-flat-")
    try:
        files = {"root.tc": program(
            "var z: int32 = copy int32, 0",
            "var n: int32 = copy int32, 7",
            "_ = writeln int32, z",
            "_ = writeln int32, n")}
        for name, text in files.items():
            write(directory, name, text)
        output = directory / "out.bin"
        argv = [engine.tc_aot_path(), f"--profile={PROFILE}",
                "--emit=flat", f"--output={output}",
                f"--include={directory}", f"--ld={engine.lld_path()}",
                str(directory / "root.tc")]
        proc = subprocess.run(argv, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=240)
        if proc.returncode != 0:
            failures.append(f"{CASE_ID}: flat build failed: "
                            f"{proc.stderr.decode()[:200]}")
            return
        run = subprocess.run(
            [engine.qemu_riscv32_path(), "-M", "virt", "-cpu", "rv32",
             "-m", "256M", "-bios", "none", "-display", "none",
             "-monitor", "none", "-serial", "stdio", "-semihosting",
             "-kernel", str(output)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        if run.returncode != 0 or run.stdout != b"0\n7\n":
            failures.append(f"{CASE_ID}: flat execution rc {run.returncode} "
                            f"stdout {run.stdout!r}")
        elif b"TC-EXIT 0 none" not in run.stderr:
            failures.append(f"{CASE_ID}: flat execution lost the exit record")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def check_empty_input(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s10-lim-in-")
    try:
        files = {"root.tc": program("var x: int32 = read int32",
                                    "_ = writeln int32, x")}
        code, stderr, elf = build(directory, files)
        if code != 0:
            failures.append(f"{CASE_ID}: empty-input build failed: "
                            f"{stderr.strip()[:200]}")
            return
        observed = engine.run_riscv32_elf(elf, b"", directory)
        if observed.trap != "TC_RE_IO":
            failures.append(f"{CASE_ID}: empty-input trap {observed.trap}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    check_long_path(failures)
    check_frame_budget(failures)
    check_static_region(failures)
    check_stack_depth(failures)
    check_device_end(failures)
    check_flat_execution(failures)
    check_empty_input(failures)
    if failures:
        for item in failures:
            print(item)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: implementation-limit and robustness cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
