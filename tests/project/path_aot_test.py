#!/usr/bin/env python3
"""P2 AOT source-string and bare-metal trap path regression checks."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

TRAP_BODY = """var x: int32 = copy int32, 1
var p: ptr<int32> = addr int32, x
unsafe then
var q: ptr<int32> = ptradd int32, p, 1
var v: int32 = load int32, q
"""
FAILS = 0


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def run(args: list[str], cwd: Path, *, input_bytes: bytes | None = None,
        timeout: int = 120) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    return subprocess.run(args, cwd=cwd, env=env, input=input_bytes,
                          capture_output=True, timeout=timeout)


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def project(root: Path) -> Path:
    write(root / "tc.project",
          'tc-project 1\nname = "path-aot"\ninclude = "src"\n'
          'profile = "aarch64-baremetal"\n')
    return root


def compile_aot(tool: str, linker: str, root: Path, source: Path,
                output: Path, emit: str, *extra: str) -> subprocess.CompletedProcess:
    return run([tool, "--profile=aarch64-baremetal", "--checked",
                f"--emit={emit}", f"--output={output}", f"--ld={linker}",
                *extra, str(source)], root.parent)


def check_case(tool: str, linker: str, qemu: str, base: Path,
               external: bool) -> None:
    label = "external" if external else "internal"
    root = project(base / label)
    if external:
        library = write(base / "outside/lib.tc",
                        "#lib\npublic func boom() int32 then\n" + TRAP_BODY +
                        "_ = return v\nend\nend\n")
        source = write(root / "src/main.tc",
                       "#program\nimport lib\nvar x: int32 = call lib.boom\n")
        extra = (f"--include={library.parent}",)
        display = "../outside/lib.tc"
    else:
        source = write(root / "src/main.tc",
                       "#program\n" + TRAP_BODY + "_ = writeln int32, v\nend\n")
        extra = ()
        display = "src/main.tc"

    asm = root / f"{label}.s"
    result = compile_aot(tool, linker, root, source, asm, "asm", *extra)
    check(result.returncode == 0,
          f"{label}: assembly compile failed: {result.stderr!r}")
    text = asm.read_text(encoding="utf-8") if asm.exists() else ""
    check(f'.asciz "{display}"' in text,
          f"{label}: assembly source string uses display path")
    check(str(base).encode() not in text.encode(),
          f"{label}: assembly leaks an absolute source path")

    elf = root / f"{label}.elf"
    result = compile_aot(tool, linker, root, source, elf, "elf", *extra)
    check(result.returncode == 0 and elf.exists(),
          f"{label}: ELF compile failed: {result.stderr!r}")
    if not elf.exists():
        return
    record = root / f"{label}.uart1"
    frame = b"TCINTCIN" + struct.pack("<Q", 0)
    result = run([qemu, "-M", "virt", "-cpu", "cortex-a53", "-m", "256M",
                  "-display", "none", "-monitor", "none", "-serial", "stdio",
                  "-serial", f"file:{record}", "-kernel", str(elf)],
                 root.parent, input_bytes=frame)
    text = record.read_text(encoding="utf-8", errors="replace") \
        if record.exists() else ""
    check(f" at {display}:" in text,
          f"{label}: board trap record uses display path: {text!r}")
    check("TC_RE_ADDR_UNAVAILABLE" in text and "TC-EXIT 1 " in text,
          f"{label}: board trap record retains code and exit status")
    check(str(base).encode() not in text.encode(),
          f"{label}: board trap record leaks an absolute source path")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("aot", nargs="?", default="build/src/driver/tc-aot")
    parser.add_argument("linker", nargs="?", default="ld.lld")
    parser.add_argument("qemu", nargs="?", default="qemu-system-aarch64")
    args = parser.parse_args()
    tool = str(Path(args.aot).resolve())
    linker = str(Path(args.linker).absolute()) if Path(args.linker).exists() else args.linker
    qemu = str(Path(args.qemu).absolute()) if Path(args.qemu).exists() else args.qemu
    with tempfile.TemporaryDirectory(prefix="tc-p2-aot-") as temporary:
        base = Path(temporary)
        check_case(tool, linker, qemu, base, False)
        check_case(tool, linker, qemu, base, True)
    if FAILS:
        print(f"project.paths_aot: {FAILS} failure(s)")
        return 1
    print("project.paths_aot: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
