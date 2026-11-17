#!/usr/bin/env python3
"""S8 determinism suite (standard §1.9, design §10.1/§12 item 15).

The zero-pass baseline must be deterministic: the emitted assembly, the linked
ELF, and the observable behavior of repeated executions are byte-identical.
`O0` and `O2` run zero IR transformations and therefore produce identical
assembly.  A checked build is deterministic on its own but legitimately
differs from the release assembly (it carries the defined-byte shadow and
define calls); release and checked executions of a legal program still agree.

Run:
    python3 tests/determinism/s8_determinism.py --check
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

# Structural case id for the traceability matrix.
CASE_ID = "TC-DET-S8-0001"

PROGRAMS = {
    "ints": engine.program(
        "var a: int32 = copy int32, 7",
        "var b: int32 = add int32, a, 5",
        "_ = writeln int32, b",
        "var c: int32 = div int32, b, 2",
        "var d: int32 = mod int32, b, 2",
        "_ = writeln int32, c",
        "_ = writeln int32, d",
    ),
    "structs": engine.program(
        "struct S then",
        "var a: int32",
        "var b: float64",
        "end",
        "var s: S = make S, 3, 1.5",
        "var p: ptr<S> = addr S, s",
        "unsafe then",
        "var t: S = load S, p",
        "end",
        "var f: float64 = add float64, s.b, 2.5",
        "_ = writeln float64, f",
        "_ = writeln int32, s.a",
    ),
    "io": engine.program(
        "var a: int32 = read int32",
        "_ = writeln int32, a",
        "_ = writeln float64, 0.5",
    ),
}
STDIN = b"42\n"


def run(argv: list[str], stdin: bytes = b"") -> subprocess.CompletedProcess:
    return subprocess.run(argv, input=stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=180)


def aot(source: Path, work: Path, emit: str, output: Path, *extra: str) -> subprocess.CompletedProcess:
    argv = [engine.tc_aot_path(), f"--emit={emit}", f"--output={output}",
            "--profile=aarch64-linux-user", f"--include={work}",
            f"--ld={engine.lld_path()}", *extra, str(source)]
    return run(argv)


def run_elf(elf: Path, work: Path) -> tuple[bytes, str | None, int]:
    stdin = work / "stdin.img"
    stdin.write_bytes(len(STDIN).to_bytes(8, "little") + STDIN)
    diag = work / "diag.txt"
    if diag.exists():
        diag.unlink()
    proc = run([
        engine.qemu_path(), "-M", "virt", "-cpu", "cortex-a53", "-m", "256M",
        "-display", "none", "-monitor", "none",
        "-semihosting-config", "enable=on,target=native,chardev=semio",
        "-chardev", "stdio,id=semio", "-serial", f"file:{diag}",
        "-device", f"loader,file={stdin},addr=0x41000000,force-raw=on",
        "-kernel", str(elf),
    ])
    text = diag.read_text(encoding="utf-8", errors="replace") if diag.exists() else ""
    status = 0
    trap = None
    for line in text.splitlines():
        if line.startswith("TC-EXIT "):
            parts = line.split()
            status = int(parts[1])
            trap = None if parts[2] == "none" else parts[2]
    return proc.stdout, trap, status


def run_vm(source: Path, work: Path, checked: bool) -> tuple[bytes, int]:
    argv = [engine.tc_vm_path(), "--profile=aarch64-linux-user",
            f"--include={work}"]
    if checked:
        argv.append("--checked")
    argv.append(str(source))
    proc = run(argv, STDIN)
    return proc.stdout, proc.returncode


def check_program(name: str, source_text: str, failures: list[str]) -> None:
    base = Path(tempfile.mkdtemp(prefix=f"tc-s8-det-{name}-"))
    try:
        source = base / "root.tc"
        source.write_text(source_text, encoding="utf-8")
        asm_a = base / "a.s"
        asm_b = base / "b.s"
        asm_o2 = base / "o2.s"
        for path, extra in ((asm_a, ()), (asm_b, ()), (asm_o2, ("--opt=O2",))):
            proc = aot(source, base, "asm", path, *extra)
            if proc.returncode != 0:
                failures.append(
                    f"determinism {name}: assembly failed: "
                    f"{proc.stderr.decode('utf-8', 'replace').strip()}")
                return
        if asm_a.read_bytes() != asm_b.read_bytes():
            failures.append(f"determinism {name}: repeated assembly differs")
        if asm_a.read_bytes() != asm_o2.read_bytes():
            failures.append(f"determinism {name}: O0/O2 assembly differs")
        elf_a = base / "a.elf"
        elf_b = base / "b.elf"
        for path in (elf_a, elf_b):
            proc = aot(source, base, "elf", path)
            if proc.returncode != 0:
                failures.append(
                    f"determinism {name}: ELF build failed: "
                    f"{proc.stderr.decode('utf-8', 'replace').strip()}")
                return
        if elf_a.read_bytes() != elf_b.read_bytes():
            failures.append(f"determinism {name}: repeated ELF build differs")
        first = run_elf(elf_a, base)
        second = run_elf(elf_a, base)
        if first != second:
            failures.append(f"determinism {name}: repeated image execution differs")
        vm_first = run_vm(source, base, False)
        vm_second = run_vm(source, base, False)
        if vm_first != vm_second:
            failures.append(f"determinism {name}: repeated VM execution differs")
        if (first[0], first[1], first[2]) != (vm_first[0], None, vm_first[1]):
            failures.append(
                f"determinism {name}: VM/AOT observable behavior differs")
        # Checked build: deterministic on its own and a legal program keeps
        # the same observable behavior.
        checked_a = base / "ca.s"
        checked_b = base / "cb.s"
        for path in (checked_a, checked_b):
            proc = aot(source, base, "asm", path, "--checked")
            if proc.returncode != 0:
                failures.append(
                    f"determinism {name}: checked assembly failed: "
                    f"{proc.stderr.decode('utf-8', 'replace').strip()}")
                return
        if checked_a.read_bytes() != checked_b.read_bytes():
            failures.append(
                f"determinism {name}: repeated checked assembly differs")
        if checked_a.read_bytes() == asm_a.read_bytes():
            failures.append(
                f"determinism {name}: checked and release assembly are identical")
        checked_elf = base / "c.elf"
        proc = aot(source, base, "elf", checked_elf, "--checked")
        if proc.returncode != 0:
            failures.append(f"determinism {name}: checked ELF build failed")
        else:
            checked_run = run_elf(checked_elf, base)
            if checked_run != first:
                failures.append(
                    f"determinism {name}: checked execution of a legal program "
                    "differs from release")
    finally:
        shutil.rmtree(base, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    for name, text in PROGRAMS.items():
        check_program(name, text, failures)
    if failures:
        print(f"s8.determinism: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    print(
        f"s8.determinism: {len(PROGRAMS)} programs reproducible "
        "(O0/O2, repeat .s/ELF/execution, checked separate)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
