#!/usr/bin/env python3
"""S7 VM/AOT differential execution engine.

One case is a complete TC program plus independently derived expectations.  The
engine runs the same sources on `tc-vm` and on the AArch64 ELF produced by
`tc-aot --emit=elf`, then checks three things in order:

1. the VM matches the derived stdout/trap/exit expectation;
2. the AOT image matches the same expectation;
3. VM and AOT observable behavior is identical.

Program stdout is the program-visible console stream.  The freestanding runtime
writes its final exit record (and only its exit record) to the QEMU PL011 side
channel, so a trap code can be compared without contaminating stdout.  The
subset has no address-valued output, so no address normalization is required;
cases that would observe an address are outside the S7 subset and are rejected
by the backend instead.

This module is test infrastructure only; it is not part of the compiler.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
S6_RUN = ROOT / "tests" / "conformance" / "runtime" / "run"

sys.path.insert(0, str(ROOT / "tests"))
from harness import yaml_subset  # noqa: E402


class CaseError(RuntimeError):
    pass


@dataclass
class Case:
    case_id: str
    profile: str
    files: dict[str, str]
    stdout: str
    trap: str | None
    exit: int
    stdin: bytes = b""
    opt: str | None = None
    spec: str = ""
    category: str = "diff"
    # When true the source is expected to be outside the subset and the AOT
    # compile is expected to be rejected explicitly.
    unsupported: bool = False
    # Checked-build execution (UB access checks; mandatory traps unchanged).
    checked: bool = False


@dataclass
class Observation:
    stdout: bytes
    trap: str | None
    exit: int
    stderr: str = ""
    diagnostic: str = ""
    command: list[str] = field(default_factory=list)


def _tool(name: str, env: str) -> str:
    value = os.environ.get(env)
    if value:
        return value
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


def qemu_path() -> str:
    return _tool("qemu-system-aarch64", "TC_AOT_QEMU_SYSTEM")


def lld_path() -> str:
    return _tool("ld.lld", "TC_AOT_LINKER")


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


def run_vm(case: Case, work: Path) -> Observation:
    source = work / "root.tc"
    argv = [tc_vm_path(), "--profile=aarch64-linux-user",
            f"--include={work}"]
    if case.checked:
        argv.append("--checked")
    if case.opt:
        argv.append(f"--opt={case.opt}")
    argv.append(str(source))
    proc = subprocess.run(argv, input=case.stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60)
    return Observation(
        stdout=proc.stdout,
        trap=_parse_vm_trap(proc.stderr.decode("utf-8", "replace")),
        exit=proc.returncode,
        stderr=proc.stderr.decode("utf-8", "replace"),
        command=argv,
    )


def compile_aot(case: Case, work: Path, asm_out: Path | None = None,
                emit: str = "elf") -> tuple[int, str, str]:
    source = work / "root.tc"
    if asm_out is not None:
        output = asm_out
    else:
        output = work / f"{case.case_id}.{emit}"
    argv = [tc_aot_path(), f"--emit={emit}", f"--output={output}",
            "--profile=aarch64-linux-user", f"--include={work}",
            f"--ld={lld_path()}"]
    if case.checked:
        argv.append("--checked")
    if case.opt:
        argv.append(f"--opt={case.opt}")
    argv.append(str(source))
    proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=120)
    return proc.returncode, proc.stdout.decode("utf-8", "replace"), \
        proc.stderr.decode("utf-8", "replace")


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


def run_elf(elf: Path, stdin: bytes, diag: Path,
            work: Path) -> Observation:
    if diag.exists():
        diag.unlink()
    image = work / "stdin.img"
    image.write_bytes(struct.pack("<Q", len(stdin)) + stdin)
    argv = [
        qemu_path(),
        "-M", "virt",
        "-cpu", "cortex-a53",
        "-m", "256M",
        "-display", "none",
        "-monitor", "none",
        "-semihosting-config", "enable=on,target=native,chardev=semio",
        "-chardev", "stdio,id=semio",
        "-serial", f"file:{diag}",
        "-device", f"loader,file={image},addr=0x41000000,force-raw=on",
        "-kernel", str(elf),
    ]
    try:
        proc = subprocess.run(argv, input=stdin, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=120)
    except subprocess.TimeoutExpired as exc:
        raise CaseError(
            f"qemu timed out for {elf}; stdout={exc.stdout!r}") from exc
    text = diag.read_text(encoding="utf-8", errors="replace") \
        if diag.exists() else ""
    status, trap = _parse_exit_record(text)
    if proc.returncode != 0 and not text:
        raise CaseError(
            f"qemu failed for {elf}: exit {proc.returncode}: "
            f"{proc.stderr.decode('utf-8', 'replace')}")
    return Observation(
        stdout=proc.stdout,
        trap=trap,
        exit=status,
        stderr=proc.stderr.decode("utf-8", "replace"),
        diagnostic=text,
        command=argv,
    )


def expected_bytes(case: Case) -> bytes:
    return case.stdout.encode("utf-8")


def first_difference(expected: bytes, actual: bytes) -> str:
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            lo = max(0, index - 8)
            hi_e = min(len(expected), index + 9)
            hi_a = min(len(actual), index + 9)
            return (f"first difference at byte {index}: expected "
                    f"{expected[lo:hi_e]!r}, actual {actual[lo:hi_a]!r}")
    if len(expected) != len(actual):
        return (f"length differs at byte {limit}: expected {len(expected)} "
                f"bytes, actual {len(actual)} bytes")
    return "no byte difference"


def compare_observation(case: Case, who: str,
                        observation: Observation,
                        expected: bytes) -> list[str]:
    errors: list[str] = []
    if observation.stdout != expected:
        errors.append(
            f"{case.case_id}: {who} stdout {observation.stdout!r} != "
            f"expected "
            f"{expected!r} "
            f"({first_difference(expected, observation.stdout)})")
    if observation.trap != case.trap:
        errors.append(
            f"{case.case_id}: {who} trap {observation.trap!r} != expected "
            f"{case.trap!r}")
    if observation.exit != case.exit:
        errors.append(
            f"{case.case_id}: {who} exit {observation.exit} != "
            f"expected {case.exit}")
    return errors


def run_case(case: Case, keep: bool = False) -> list[str]:
    errors: list[str] = []
    expected = expected_bytes(case)
    base = Path(tempfile.mkdtemp(prefix=f"tc-s7-{case.case_id.lower()}-"))
    try:
        _write_files(base, case.files)
        vm_observation = run_vm(case, base)
        errors.extend(compare_observation(case, "VM", vm_observation,
                                          expected))
        if case.unsupported:
            code, _stdout, error_output = compile_aot(case, base,
                                                       emit="asm")
            if code != 70:
                errors.append(
                    f"{case.case_id}: unsupported AOT compile exit {code}, "
                    f"expected 70: {error_output.strip()}")
            return errors
        asm_path = base / f"{case.case_id}.s"
        code, _stdout, error_output = compile_aot(case, base,
                                                   asm_out=asm_path,
                                                   emit="asm")
        if code != 0:
            errors.append(
                f"{case.case_id}: AOT assembly failed ({code}): "
                f"{error_output.strip()}")
            return errors
        elf = base / f"{case.case_id}.elf"
        code, _stdout, error_output = compile_aot(case, base,
                                                   asm_out=None, emit="elf")
        # compile_aot names the ELF from the case id.
        produced = base / f"{case.case_id}.elf"
        if produced.exists():
            elf = produced
        if code != 0 or not elf.exists():
            errors.append(
                f"{case.case_id}: AOT ELF build failed ({code}): "
                f"{error_output.strip()}")
            return errors
        diag = base / f"{case.case_id}.diag"
        aot_observation = run_elf(elf, case.stdin, diag, base)
        errors.extend(compare_observation(case, "AOT", aot_observation,
                                          expected))
        if vm_observation.stdout != aot_observation.stdout or \
                vm_observation.trap != aot_observation.trap or \
                vm_observation.exit != aot_observation.exit:
            errors.append(
                f"{case.case_id}: VM/AOT differ: "
                f"VM(stdout={vm_observation.stdout!r}, "
                f"trap={vm_observation.trap!r}, "
                f"exit={vm_observation.exit}) "
                f"AOT(stdout={aot_observation.stdout!r}, "
                f"trap={aot_observation.trap!r}, "
                f"exit={aot_observation.exit})")
        if errors and aot_observation.diagnostic:
            errors.append(f"{case.case_id}: AOT diagnostic: "
                          f"{aot_observation.diagnostic}")
        return errors
    finally:
        if not keep:
            shutil.rmtree(base, ignore_errors=True)


def run_cases(cases: list[Case], title: str, keep: bool = False) -> int:
    failures: list[str] = []
    for case in cases:
        try:
            failures.extend(run_case(case, keep=keep))
        except (CaseError, OSError, subprocess.SubprocessError) as exc:
            failures.append(f"{case.case_id}: {exc}")
    if failures:
        print(f"{title}: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    expected_rejections = sum(1 for case in cases if case.unsupported)
    differential = len(cases) - expected_rejections
    if expected_rejections:
        print(f"{title}: {differential} case(s) VM/AOT identical, "
              f"{expected_rejections} expected rejection(s)")
    else:
        print(f"{title}: {len(cases)} case(s) VM/AOT identical")
    return 0


def load_s6_case(case_id: str, category: str = "diff") -> Case:
    directory = S6_RUN / case_id.lower()
    yaml_path = directory / f"{case_id.lower()}.yaml"
    if not directory.is_dir() or not yaml_path.exists():
        raise CaseError(f"missing S6 fixture for {case_id}")
    data = yaml_subset.parse(yaml_path.read_text(encoding="utf-8"))
    if data.get("profile") != "w64":
        raise CaseError(f"{case_id} is not a W=64 case")
    files: dict[str, str] = {}
    for path in sorted(directory.glob("*.tc")):
        files[path.name] = path.read_text(encoding="utf-8")
    expect = data.get("expect", {})
    run = data.get("run", {})
    stdin_hex = run.get("stdin_hex", "")
    stdout_hex = expect.get("stdout_hex", "")
    trap = expect.get("trap")
    return Case(
        case_id=case_id,
        profile="aarch64-linux-user",
        files=files,
        stdout=bytes.fromhex(stdout_hex).decode("utf-8"),
        trap=trap,
        exit=int(expect.get("exit", 0)),
        stdin=bytes.fromhex(stdin_hex),
        opt=data.get("opt"),
        spec=str(data.get("spec", "")),
        category=category,
        checked=bool(data.get("checked", False)),
    )


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def case(case_id: str, spec: str, *, root: str | None = None,
         files: dict[str, str] | None = None, out: str = "",
         trap: str | None = None, stdin: str = "", opt: str | None = None,
         category: str = "diff", unsupported: bool = False,
         checked: bool = False) -> Case:
    if files is None:
        files = {"root.tc": root if root is not None else program()}
    return Case(
        case_id=case_id,
        profile="aarch64-linux-user",
        files=files,
        stdout=out,
        trap=trap,
        exit=1 if trap is not None else 0,
        stdin=stdin.encode("utf-8"),
        opt=opt,
        spec=spec,
        category=category,
        unsupported=unsupported,
        checked=checked,
    )
