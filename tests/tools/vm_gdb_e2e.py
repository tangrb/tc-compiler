#!/usr/bin/env python3
"""Real GDB smoke test for the VM synthetic-address RSP stub."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from vm_gdb_protocol_test import BASE, ROOT, SOURCE, Stub, check, environment, linemap


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tc-vm")
    parser.add_argument("gdb")
    parser.add_argument("--profile", default="aarch64-linux-user")
    args = parser.parse_args()
    if not args.gdb:
        print("SKIP: real gdb is unavailable")
        return 77
    tool = str(Path(getattr(args, "tc-vm")).resolve())
    gdb = str(Path(args.gdb).resolve())
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vm-gdb-e2e-", dir=ROOT / ".tmp") as tmp:
        root = Path(tmp)
        stub = Stub(tool, root, SOURCE)
        try:
            entries = linemap(tool, root, args.profile)["entries"]
            address = BASE + 4 * next(i for i, item in enumerate(entries)
                                      if item["module"] == "main.tc" and item["insn"] == 1)
            commands = root / "smoke.gdb"
            commands.write_text(
                "set pagination off\nset confirm off\n"
                "set architecture aarch64\n"
                f"target remote 127.0.0.1:{stub.port}\n"
                "printf \"INITIAL_PC=0x%lx\\n\", $pc\n"
                f"break *{address:#x}\ncontinue\n"
                "printf \"STOP_PC=0x%lx\\n\", $pc\n"
                "x/i $pc\nstepi\n"
                "printf \"STEP_PC=0x%lx\\n\", $pc\n"
                "monitor stats\ncontinue\n"
                "quit\n", encoding="utf-8")
            result = subprocess.run([gdb, "-q", "-batch", "-x", str(commands)],
                                    cwd=root, env=environment(root),
                                    capture_output=True, timeout=60)
            output = (result.stdout + result.stderr).decode("utf-8", "replace")
            check(result.returncode == 0, f"real gdb failed: {output}")
            check("Breakpoint 1," in output, f"breakpoint not hit: {output}")
            initial = re.search(r"INITIAL_PC=(0x[0-9a-f]+)", output)
            stop = re.search(r"STOP_PC=(0x[0-9a-f]+)", output)
            step = re.search(r"STEP_PC=(0x[0-9a-f]+)", output)
            check(initial is not None and int(initial.group(1), 16) == BASE,
                  f"initial PC mismatch: {output}")
            check(stop is not None and int(stop.group(1), 16) == address,
                  f"breakpoint PC mismatch: {output}")
            check(step is not None and int(step.group(1), 16) != address,
                  f"single-step did not advance: {output}")
            check("executed=" in output and "io_bytes=" in output,
                  f"monitor stats missing: {output}")
            stub.process.wait(timeout=10)
            check(stub.process.returncode == 0 and stub.stdout() == b"8\n",
                  "real gdb run changed program semantics")
        finally:
            stub.close()
    print(f"vm.gdb.e2e: real gdb passed ({args.profile})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
