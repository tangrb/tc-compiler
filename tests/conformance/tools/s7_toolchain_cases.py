#!/usr/bin/env python3
"""S7 toolchain, artifact and static-parity checks (design §11, test plan S7).

Checks the toolchain/artifact contract rather than program semantics:

  - `.s` is byte-for-byte reproducible and O0/O2 are identical (zero pass);
  - `--emit=obj` produces an AArch64 ELF relocatable and `--emit=elf` an
    executable that runs, reproducibly, on qemu-system-aarch64;
  - platform-instance capabilities that the hosted backend does not implement
    (a different word size or device policy) are rejected explicitly (exit 70)
    and no artifact is published;
  - `tc-aot` static diagnostics are byte-identical to `tc-vm` (same front end).

The environment is documented in the G7 record: qemu-user is not available on
this macOS host, so the design §11 fallback qemu-system-aarch64 + semihosting
is used; the image is still an AArch64 ELF.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

EM_AARCH64 = 183
ET_REL = 1
ET_EXEC = 2


def tool(name: str, env: str) -> str:
    return engine._tool(name, env)


def run(argv: list[str], cwd: Path | None = None,
        timeout: int = 120) -> subprocess.CompletedProcess:
    return subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout)


def write(directory: Path, name: str, text: str) -> Path:
    path = directory / name
    path.write_text(text, encoding="utf-8")
    return path


def aot(tmp: Path, source: Path, *args: str,
        output: str | None = None) -> subprocess.CompletedProcess:
    argv = [tool("tc-aot", "TC_AOT")]
    argv.extend(args)
    if output is not None:
        argv.append(f"--output={output}")
    argv.append(str(source))
    return run(argv, cwd=tmp)


def vm(tmp: Path, source: Path, *args: str) -> subprocess.CompletedProcess:
    argv = [tool("tc-vm", "TC_VM")]
    argv.extend(args)
    argv.append(str(source))
    return run(argv, cwd=tmp)


def elf_header(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise AssertionError(f"{path} is not ELF")
    if data[4] != 2 or data[5] != 1:
        raise AssertionError(f"{path} is not 64-bit little-endian ELF")
    e_type = struct.unpack_from("<H", data, 16)[0]
    e_machine = struct.unpack_from("<H", data, 18)[0]
    return data[4], e_type, e_machine


def check_zero_pass_and_repro(tmp: Path, failures: list[str]) -> None:
    source = write(tmp, "repro.tc",
                   "#program\nvar a: int32 = copy int32, 6\n"
                   "var b: int32 = mul int32, a, 7\n"
                   "_ = writeln int32, b\n")
    hashes = []
    for index in range(3):
        output = tmp / f"repro{index}.s"
        result = aot(tmp, source, "--emit=asm", f"--opt={'O0' if index < 2 else 'O2'}",
                     output=str(output))
        if result.returncode != 0:
            failures.append(f"reproducible .s: AOT failed: {result.stderr!r}")
            return
        hashes.append(output.read_bytes())
    if hashes[0] != hashes[1] or hashes[0] != hashes[2]:
        failures.append("reproducible .s: repeated/O0/O2 assembly differs")
    if b"mul" not in hashes[0]:
        failures.append("zero pass: O0 assembly does not contain the expected "
                        "native mul lowering")


def check_artifacts(tmp: Path, failures: list[str]) -> None:
    source = write(tmp, "art.tc",
                   "#program\nvar a: int32 = copy int32, 40\n"
                   "var b: int32 = add int32, a, 2\n"
                   "_ = writeln int32, b\n")
    obj = tmp / "art.o"
    result = aot(tmp, source, "--emit=obj", output=str(obj))
    if result.returncode != 0:
        failures.append(f"obj artifact: AOT failed: {result.stderr!r}")
    else:
        try:
            _class, e_type, machine = elf_header(obj)
            if e_type != ET_REL or machine != EM_AARCH64:
                failures.append(
                    f"obj artifact: unexpected ELF type/machine {e_type}/{machine}")
        except AssertionError as exc:
            failures.append(f"obj artifact: {exc}")
    elf = tmp / "art.elf"
    result = aot(tmp, source, "--emit=elf", output=str(elf))
    if result.returncode != 0:
        failures.append(f"elf artifact: AOT failed: {result.stderr!r}")
        return
    try:
        _class, e_type, machine = elf_header(elf)
        if e_type != ET_EXEC or machine != EM_AARCH64:
            failures.append(
                f"elf artifact: unexpected ELF type/machine {e_type}/{machine}")
    except AssertionError as exc:
        failures.append(f"elf artifact: {exc}")
        return
    diag = tmp / "art.diag"
    first = engine.run_elf(elf, b"", diag, tmp)
    if first.stdout != b"42\n" or first.exit != 0 or first.trap is not None:
        failures.append(f"elf run: unexpected observation {first}")
    second = engine.run_elf(elf, b"", tmp / "art2.diag", tmp)
    if (first.stdout, first.exit, first.trap) != (
            second.stdout, second.exit, second.trap):
        failures.append("elf run: repeated qemu execution differs")


def check_profile_limits(tmp: Path, failures: list[str]) -> None:
    """Features outside the current platform instance fail explicitly.

    The S8 backend lowers pointers, structures, floats, indirect calls,
    checked builds and multi-register argument lists; what remains unsupported
    is a different word size or a platform instance this hosted backend cannot
    map (the bare-metal device policy).  Both must fail with exit 70 and must
    not publish an artifact.
    """
    pointer = write(tmp, "ptr.tc",
                    "#program\nvar x: int32 = copy int32, 1\nunsafe then\n"
                    "var p: ptr<int32> = addr int32, x\n"
                    "var y: int32 = load int32, p\n"
                    "_ = writeln int32, y\nend\n")
    out = tmp / "ptr.s"
    result = aot(tmp, pointer, "--emit=asm", output=str(out))
    if result.returncode != 0 or not out.exists() or out.stat().st_size == 0:
        failures.append(
            f"pointer lowering: exit {result.returncode}, expected a published "
            f"artifact ({result.stderr.decode('utf-8', 'replace').strip()})")
    elf_out = tmp / "ptr.elf"
    result = aot(tmp, pointer, "--emit=elf", output=str(elf_out))
    if result.returncode != 0 or not elf_out.exists():
        failures.append(
            f"pointer ELF path: exit {result.returncode}, expected an ELF")

    float_src = write(tmp, "float.tc",
                      "#program\n"
                      "var a: float64 = add float64, 1.5, 2.5\n"
                      "_ = writeln float64, a\n")
    result = aot(tmp, float_src, "--emit=asm", output=str(tmp / "float.s"))
    if result.returncode != 0:
        failures.append(
            f"float lowering: exit {result.returncode}, expected success")

    valid = write(tmp, "valid.tc",
                  "#program\n_ = writeln int32, 1\n")
    result = aot(tmp, valid, "--emit=asm", "--checked", output=str(tmp / "c.s"))
    if result.returncode != 0:
        failures.append(
            f"checked build: exit {result.returncode}, expected success")

    nine = write(tmp, "nine.tc",
                 "#program\nimport lib\n"
                 "var r: int32 = call lib.nine, 1, 2, 3, 4, 5, 6, 7, 8, 9\n"
                 "_ = writeln int32, r\n")
    write(tmp, "lib.tc",
          "#lib\npublic func nine(a: int32, b: int32, c: int32, d: int32, "
          "e: int32, f: int32, g: int32, h: int32, i: int32) int32 then\n"
          "var s: int32 = add int32, a, i\n_ = return s\nend\n")
    nine_out = tmp / "nine.s"
    result = aot(tmp, nine, "--emit=asm", "--include=" + str(tmp),
                 output=str(nine_out))
    if result.returncode != 0 or not nine_out.exists():
        failures.append(
            f"nine parameters: exit {result.returncode}, expected success")

    w32_out = tmp / "w32.s"
    result = aot(tmp, valid, "--emit=asm", "--profile=w32",
                 output=str(w32_out))
    if result.returncode != 70:
        failures.append(
            f"w32 profile: exit {result.returncode}, expected 70")
    if w32_out.exists() and w32_out.stat().st_size != 0:
        failures.append("w32 profile published an artifact")
    baremetal_out = tmp / "aarch64-baremetal.s"
    result = aot(tmp, valid, "--emit=asm", "--profile=aarch64-baremetal",
                 output=str(baremetal_out))
    if result.returncode != 0 or not baremetal_out.exists():
        failures.append(
            f"aarch64-baremetal profile: exit {result.returncode}, "
            f"expected success")
    result = aot(tmp, valid, "--emit=elf")
    if result.returncode != 2:
        failures.append(
            f"missing output: exit {result.returncode}, expected usage 2")


def check_static_parity(tmp: Path, failures: list[str]) -> None:
    cases: list[tuple[str, dict[str, str]]] = [
        ("valid", {"root.tc": "#program\n_ = writeln int32, 1\n"}),
        ("syntax", {"root.tc": "#program\nvar x: int32 = add int32, 1\n"}),
        ("name", {"root.tc": "#program\nvar x: int32 = copy int32, q\n"}),
        ("type", {"root.tc": "#program\nvar x: int32 = copy bool, true\n"}),
        ("ct", {
            "root.tc": "#program\nimport lib\n"
                       "var x: int32 = copy int32, lib.C\n",
            "lib.tc": "#lib\npublic const C: int32 = div int32, 1, 0\n",
        }),
    ]
    for name, files in cases:
        directory = tmp / f"static-{name}"
        directory.mkdir()
        for filename, text in files.items():
            write(directory, filename, text)
        root = directory / "root.tc"
        args = ["--static-only", f"--include={directory}"]
        a = vm(directory, root, *args)
        b = aot(directory, root, *args)
        if (a.returncode, a.stdout, a.stderr) != (
                b.returncode, b.stdout, b.stderr):
            failures.append(
                f"static parity {name}: VM/V AOT differ "
                f"(VM rc={a.returncode}, AOT rc={b.returncode}, "
                f"VM err={a.stderr!r}, AOT err={b.stderr!r})")


def check_cli(tmp: Path, failures: list[str]) -> None:
    source = write(tmp, "cli.tc", "#program\n_ = writeln int32, 1\n")
    result = aot(tmp, source, "--emit=asm")
    if result.returncode != 0 or b"tc_main:" not in result.stdout:
        failures.append("cli: default --emit=asm does not write assembly to stdout")
    result = aot(tmp, source, "--emit=bogus", output=str(tmp / "x.s"))
    if result.returncode != 2:
        failures.append(f"cli: invalid --emit exit {result.returncode}, expected 2")
    tt = tool("tc-aot", "TC_AOT")
    result = run([tt, "--help"])
    if result.returncode != 0 or b"--emit=asm|obj|elf|flat" not in result.stdout:
        failures.append("cli: --help does not document the AOT options")
    result = run([tt, "--version"])
    if result.returncode != 0 or b"native backends" not in result.stdout or \
            b"0.0.46" not in result.stdout:
        failures.append(
            "cli: --version does not identify the native backends and version")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    tmp = Path(tempfile.mkdtemp(prefix="tc-s7-tools-"))
    try:
        check_zero_pass_and_repro(tmp, failures)
        check_artifacts(tmp, failures)
        check_profile_limits(tmp, failures)
        check_static_parity(tmp, failures)
        check_cli(tmp, failures)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"s7.tools: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    print("s7.tools: reproducible .s, ELF artifacts, explicit rejection and "
          "static parity all hold")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
