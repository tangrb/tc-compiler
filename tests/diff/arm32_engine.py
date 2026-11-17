#!/usr/bin/env python3
"""S9 execution engine: reference VM and 32/64-bit freestanding AOT images.

The engine runs one TC program on two same-profile implementations:

  * the reference VM (`tc-vm --profile=arm32-baremetal` or
    `--profile=aarch64-baremetal`), and
  * the freestanding ELF emitted by `tc-aot --emit=elf` for that profile.

ARM32 images run on the QEMU MPS2 AN500 (Cortex-M7) board with semihosting as
the program stdout channel and CMSDK UART0 as the exit-record channel.  AArch64
bare-metal images run on the QEMU virt machine with PL011 UART0 for program
stdout, PL011 UART1 for the exit record and PSCI SYSTEM_OFF termination; no
semihosting call is made.  Both use an eight-byte little-endian length plus the
stdin bytes placed by the QEMU loader, so the input path is a freestanding
memory image rather than a host file descriptor.

This module is test infrastructure only; it is not part of the compiler.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from diff.engine import Case, Observation, CaseError, first_difference  # noqa: E402,F401


def _tool(name: str, env: str, default: str | None = None) -> str:
    value = os.environ.get(env)
    if value:
        return value
    if default is not None:
        local = ROOT / default
        if local.is_file() and os.access(local, os.X_OK):
            return str(local)
    local = ROOT / "build" / "src" / "driver" / name
    if local.is_file() and os.access(local, os.X_OK):
        return str(local)
    found = shutil.which(name)
    if not found:
        raise CaseError(f"required tool '{name}' not found (set {env})")
    return found


def tc_vm_path() -> str:
    return _tool("tc-vm", "TC_VM")


def tc_aot_path() -> str:
    return _tool("tc-aot", "TC_AOT")


def lld_path() -> str:
    return _tool("ld.lld", "TC_AOT_LINKER")


def qemu_arm_path() -> str:
    return _tool("qemu-system-arm", "TC_AOT_QEMU_SYSTEM_ARM")


def qemu_aarch64_path() -> str:
    return _tool("qemu-system-aarch64", "TC_AOT_QEMU_SYSTEM")


def runtime_archive(width: int, baremetal: bool = False) -> Path:
    driver = Path(tc_aot_path()).resolve()
    build_src = driver.parent.parent
    if width == 32:
        return build_src / "arm32-runtime" / "libtc_arm32_runtime.a"
    name = ("libtc_aarch64_baremetal_runtime.a" if baremetal
            else "libtc_aarch64_runtime.a")
    return build_src / "aarch64-runtime" / name


@dataclass
class AotObservation(Observation):
    pass


def _write_files(directory: Path, files: dict[str, str]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def _parse_vm_trap(stderr: str) -> str | None:
    marker = "error[RT]:"
    for line in stderr.splitlines():
        pos = line.find(marker)
        if pos < 0:
            continue
        rest = line[pos + len(marker):].strip()
        code = rest.split(":", 1)[0].strip()
        if code.startswith("TC_RE_"):
            return code
    return None


def run_vm(case: Case, work: Path, profile: str) -> Observation:
    source = work / "root.tc"
    argv = [tc_vm_path(), f"--profile={profile}", f"--include={work}"]
    if case.checked:
        argv.append("--checked")
    if case.opt:
        argv.append(f"--opt={case.opt}")
    argv.append(str(source))
    proc = subprocess.run(argv, input=case.stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=120)
    return Observation(
        stdout=proc.stdout,
        trap=_parse_vm_trap(proc.stderr.decode("utf-8", "replace")),
        exit=proc.returncode,
        stderr=proc.stderr.decode("utf-8", "replace"),
        command=argv,
    )


def compile_aot(case: Case, work: Path, profile: str,
                emit: str = "elf") -> tuple[int, str, str, Path]:
    source = work / "root.tc"
    output = work / f"{case.case_id}.{emit}"
    argv = [tc_aot_path(), f"--emit={emit}", f"--output={output}",
            f"--profile={profile}", f"--include={work}",
            f"--ld={lld_path()}"]
    if case.checked:
        argv.append("--checked")
    if case.opt:
        argv.append(f"--opt={case.opt}")
    argv.append(str(source))
    proc = subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=180)
    return (proc.returncode, proc.stdout.decode("utf-8", "replace"),
            proc.stderr.decode("utf-8", "replace"), output)


def _parse_exit_record(text: str) -> tuple[int, str | None]:
    status = None
    trap = None
    for line in text.splitlines():
        if not line.startswith("TC-EXIT "):
            continue
        parts = line.split()
        if len(parts) < 3:
            raise CaseError(f"malformed AOT exit record: {line!r}")
        status = int(parts[1])
        trap = None if parts[2] == "none" else parts[2]
    if status is None:
        raise CaseError("AOT image did not report a TC-EXIT record")
    return status, trap


def run_arm32_elf(elf: Path, stdin: bytes, work: Path) -> Observation:
    diag = work / "diag.txt"
    if diag.exists():
        diag.unlink()
    image = work / "stdin.img"
    image.write_bytes(struct.pack("<Q", len(stdin)) + stdin)
    argv = [
        qemu_arm_path(),
        "-M", "mps2-an500",
        "-cpu", "cortex-m7",
        "-m", "16M",
        "-display", "none",
        "-monitor", "none",
        "-semihosting-config", "enable=on,target=native,chardev=semio",
        "-chardev", "stdio,id=semio",
        "-serial", f"file:{diag}",
        "-device", f"loader,file={image},addr=0x20000000,force-raw=on",
        "-kernel", str(elf),
    ]
    try:
        proc = subprocess.run(argv, input=stdin, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=120)
    except subprocess.TimeoutExpired as exc:
        raise CaseError(
            f"qemu-system-arm timed out for {elf}; "
            f"stdout={exc.stdout!r}") from exc
    text = diag.read_text(encoding="utf-8", errors="replace") \
        if diag.exists() else ""
    status, trap = _parse_exit_record(text)
    return Observation(
        stdout=proc.stdout,
        trap=trap,
        exit=status,
        stderr=proc.stderr.decode("utf-8", "replace"),
        diagnostic=text,
        command=argv,
    )


def run_aarch64_baremetal_elf(elf: Path, stdin: bytes,
                              work: Path) -> Observation:
    stdout_file = work / "uart0.txt"
    diag = work / "uart1.txt"
    for path in (stdout_file, diag):
        if path.exists():
            path.unlink()
    image = work / "stdin.img"
    image.write_bytes(struct.pack("<Q", len(stdin)) + stdin)
    argv = [
        qemu_aarch64_path(),
        "-M", "virt",
        "-cpu", "cortex-a53",
        "-m", "256M",
        "-display", "none",
        "-monitor", "none",
        "-serial", f"file:{stdout_file}",
        "-serial", f"file:{diag}",
        "-device", f"loader,file={image},addr=0x41000000,force-raw=on",
        "-kernel", str(elf),
    ]
    proc = subprocess.run(argv, input=stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=120)
    text = diag.read_text(encoding="utf-8", errors="replace") \
        if diag.exists() else ""
    status, trap = _parse_exit_record(text)
    console = stdout_file.read_text(encoding="utf-8", errors="replace") \
        if stdout_file.exists() else ""
    return Observation(
        stdout=console.encode("utf-8"),
        trap=trap,
        exit=status,
        stderr=proc.stderr.decode("utf-8", "replace"),
        diagnostic=text,
        command=argv,
    )


def run_case(case: Case, profile: str, work: Path) -> tuple[Observation,
                                                           Observation,
                                                           Observation]:
    """Run one case on the VM and on an AOT image.

    Returns (vm, aot, build): `aot` is None when compilation was rejected; the
    caller decides whether the rejection is the expected same-profile static
    result.
    """
    _write_files(work, case.files)
    vm = run_vm(case, work, profile)
    code, _stdout, stderr, output = compile_aot(case, work, profile)
    build = Observation(stdout=b"", trap=None, exit=code, stderr=stderr,
                        command=[])
    if code != 0:
        return vm, None, build
    if profile == "arm32-baremetal":
        aot = run_arm32_elf(output, case.stdin, work)
    elif profile == "aarch64-baremetal":
        aot = run_aarch64_baremetal_elf(output, case.stdin, work)
    else:
        raise CaseError(f"unsupported S9 profile {profile}")
    return vm, aot, build


def normalize_tool_text(text: str) -> str:
    return text.replace("tc-aot:", "tc-vm:").replace("tc-aot ", "tc-vm ")


def observation_diff(expected: Observation, actual: Observation) -> str:
    if expected.stdout != actual.stdout:
        return first_difference(expected.stdout, actual.stdout)
    if expected.trap != actual.trap:
        return f"trap {actual.trap!r} != {expected.trap!r}"
    if expected.exit != actual.exit:
        return f"exit {actual.exit} != {expected.exit}"
    return ""


def make_case(case_id: str, files: dict[str, str], stdout: str,
              trap: str | None = None, stdin: bytes = b"",
              checked: bool = False, opt: str | None = None,
              spec: str = "") -> Case:
    return Case(
        case_id=case_id,
        profile="w32",
        files=files,
        stdout=stdout,
        trap=trap,
        exit=1 if trap else 0,
        stdin=stdin,
        checked=checked,
        opt=opt,
        spec=spec,
        category="s9",
    )


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def temporary_directory(prefix: str) -> Path:
    return Path(tempfile.mkdtemp(prefix=prefix))
