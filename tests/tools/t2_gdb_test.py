#!/usr/bin/env python3
"""Real gdb/qemu gdbstub smoke test for tc-aot DWARF output."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TMP_ROOT = ROOT / ".tmp"
SOURCE = """#program
var x: int32 = copy int32, 7
_ = writeln int32, x
_ = writeln int32, x
"""
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def wait_for_port(port: int, process: subprocess.Popen) -> None:
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"qemu exited before gdbstub listened: {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("qemu gdbstub did not open its TCP port")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("aot")
    parser.add_argument("qemu")
    parser.add_argument("gdb")
    args = parser.parse_args()
    if not args.gdb:
        print("SKIP: real gdb is unavailable")
        return 77
    aot = str(Path(args.aot).resolve())
    qemu = str(Path(args.qemu).resolve())
    gdb = str(Path(args.gdb).resolve())
    TMP_ROOT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="t2-gdb-", dir=TMP_ROOT) as tmp:
        root = Path(tmp)
        (root / "tc.project").write_text(
            'tc-project 1\nname = "t2-gdb"\n', encoding="utf-8")
        (root / "main.tc").write_text(SOURCE, encoding="utf-8")
        env = os.environ.copy()
        env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                    "TMPDIR": str(root)})
        build = subprocess.run(
            [aot, "--debug", "--profile=aarch64-baremetal", "--emit=elf",
             "--output=program.elf", "main.tc"],
            cwd=root, env=env, capture_output=True, timeout=120)
        check(build.returncode == 0,
              f"tc-aot debug ELF failed: {build.stderr!r}")
        check((root / "program.elf").is_file(), "debug ELF was not published")

        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        qemu_process = subprocess.Popen(
            [qemu, "-M", "virt", "-cpu", "cortex-a53", "-m", "256M",
             "-display", "none", "-monitor", "none",
             "-serial", "stdio", "-serial", "file:uart1.txt",
             "-gdb", f"tcp:127.0.0.1:{port}", "-S", "-kernel",
             "program.elf"],
            cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for_port(port, qemu_process)
            commands = root / "smoke.gdb"
            commands.write_text(
                "set pagination off\n"
                "set confirm off\n"
                "set architecture aarch64\n"
                "file program.elf\n"
                f"target remote 127.0.0.1:{port}\n"
                "info line main.tc:3\n"
                "info line main.tc:4\n"
                "break main.tc:3\n"
                "continue\n"
                "printf \"STOP_PC=0x%lx\\n\", $pc\n"
                "x/i $pc\n"
                "info line *$pc\n"
                "stepi\n"
                "printf \"STEP_PC=0x%lx\\n\", $pc\n"
                "disconnect\n"
                "quit\n",
                encoding="utf-8")
            try:
                debugged = subprocess.run(
                    [gdb, "-q", "-batch", "-x", str(commands)],
                    cwd=root, env=env, capture_output=True, timeout=60)
            except subprocess.TimeoutExpired as error:
                output = ((error.stdout or b"") + (error.stderr or b"")).decode(
                    "utf-8", "replace")
                raise AssertionError(f"real gdb script timed out: {output}") from error
            output = (debugged.stdout + debugged.stderr).decode(
                "utf-8", "replace")
            check(debugged.returncode == 0,
                  f"real gdb script failed: {output}")
            check(re.search(r"Line 3 of .*main\.tc", output) is not None,
                  f"gdb info line did not resolve line 3: {output}")
            check(re.search(r"Line 4 of .*main\.tc", output) is not None,
                  f"gdb info line did not resolve line 4: {output}")
            check("Breakpoint 1," in output,
                  f"source breakpoint was not hit: {output}")
            check(re.search(r"STOP_PC=0x[0-9a-f]+", output) is not None and
                  re.search(r"STEP_PC=0x[0-9a-f]+", output) is not None,
                  f"gdb did not report pcs around single-step: {output}")
            stop_pc = re.search(r"STOP_PC=(0x[0-9a-f]+)", output)
            step_pc = re.search(r"STEP_PC=(0x[0-9a-f]+)", output)
            check(stop_pc is not None and step_pc is not None and
                  stop_pc.group(1) != step_pc.group(1),
                  f"single-step did not advance pc: {output}")
        finally:
            if qemu_process.poll() is None:
                qemu_process.terminate()
                try:
                    qemu_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    qemu_process.kill()
                    qemu_process.wait(timeout=5)
            qemu_stdout, qemu_stderr = qemu_process.communicate()
            check(qemu_process.returncode in (0, -15),
                  f"qemu smoke failed: {qemu_process.returncode} "
                  f"{qemu_stdout!r} {qemu_stderr!r}")
    print(f"T2 real gdb smoke passed: {CHECKS} checks")
    print("evidence-result=toolchain.debug_smoke/"
          "aarch64-baremetal:qemu-gdbstub-source-breakpoint "
          f"assertions={CHECKS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
