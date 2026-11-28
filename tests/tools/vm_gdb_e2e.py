#!/usr/bin/env python3
"""Real GDB smoke test for the VM synthetic-address RSP stub."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

import vm_gdb_protocol_test as protocol
from vm_gdb_protocol_test import BASE, BASE32, ROOT, Stub, check, environment, linemap

E2E_SOURCE = (
    "#program\n"
    "var a: int32 = copy int32, 7\n"
    "var b: int32 = add int32, a, 1\n"
    "_ = writeln int32, b\n"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tc-vm")
    parser.add_argument("gdb")
    parser.add_argument("--profile", default="aarch64-linux-user",
                        choices=("aarch64-linux-user", "aarch64-baremetal",
                                 "arm32-baremetal", "riscv32-baremetal"))
    args = parser.parse_args()
    if not args.gdb:
        print("SKIP: real gdb is unavailable")
        return 77
    tool = str(Path(getattr(args, "tc-vm")).resolve())
    gdb = str(Path(args.gdb).resolve())
    if args.profile == "arm32-baremetal":
        base, architecture = BASE32, "arm"
    elif args.profile == "riscv32-baremetal":
        base, architecture = BASE32, "riscv:rv32"
    else:
        base, architecture = BASE, "aarch64"
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vm-gdb-e2e-", dir=ROOT / ".tmp") as tmp:
        root = Path(tmp)
        stub = Stub(tool, root, E2E_SOURCE, profile=args.profile)
        try:
            entries = linemap(tool, root, args.profile)["entries"]
            address = base + 4 * next(i for i, item in enumerate(entries)
                                      if item["module"] == "main.tc" and item["insn"] == 1)
            elf = root / "program.elf"
            generated = subprocess.run(
                [tool, f"--profile={args.profile}", "--static-only",
                 f"--gdb-elf-out={elf}", "main.tc"],
                cwd=root, env=environment(root), capture_output=True, timeout=30)
            check(generated.returncode == 0 and elf.is_file(),
                  f"debug ELF generation failed: {generated.stderr!r}")
            commands = root / "smoke.gdb"
            arm_mode = "set arm force-mode thumb\n" if architecture == "arm" else ""
            # GDB 17 models RISC-V stepi as a 2-byte step; IR slots are 4 bytes.
            step_command = (
                ("maintenance packet s\n"
                 "maintenance flush register-cache\n")
                if architecture in ("arm", "riscv:rv32") else "stepi\n")
            commands.write_text(
                "set pagination off\nset confirm off\n"
                f"file {elf}\n"
                f"set architecture {architecture}\n"
                f"{arm_mode}"
                f"target remote 127.0.0.1:{stub.port}\n"
                # Unmapped synthetic addresses are read-only in GDB; the stub implements Z0.
                f"mem {base:#x} {base + 4 * (len(entries) + 1):#x} rw\n"
                "printf \"INITIAL_PC=0x%lx\\n\", $pc\n"
                "printf \"INITIAL_TC_IR_PC=0x%lx\\n\", $tc_ir_pc\n"
                f"break *{address:#x}\ncontinue\n"
                "printf \"STOP_PC=0x%lx\\n\", $pc\n"
                "printf \"STOP_TC_IR_PC=0x%lx\\n\", $tc_ir_pc\n"
                f"{step_command}"
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
            initial_tc = re.search(r"INITIAL_TC_IR_PC=(0x[0-9a-f]+)", output)
            stop_tc = re.search(r"STOP_TC_IR_PC=(0x[0-9a-f]+)", output)
            check(initial is not None and int(initial.group(1), 16) == base,
                  f"initial PC mismatch: {output}")
            check(initial_tc is not None and int(initial_tc.group(1), 16) == base,
                  f"initial 64-bit TC IR PC mismatch: {output}")
            check(stop is not None and int(stop.group(1), 16) == address,
                  f"breakpoint PC mismatch: {output}")
            check(stop_tc is not None and int(stop_tc.group(1), 16) == address,
                  f"breakpoint TC IR PC mismatch: {output}")
            check(step is not None and int(step.group(1), 16) != address,
                  f"single-step did not advance: {output}")
            check("executed=" in output and "io_bytes=" in output,
                  f"monitor stats missing: {output}")
            stub.process.wait(timeout=10)
            check(stub.process.returncode == 0 and stub.stdout() == b"8\n",
                  "real gdb run changed program semantics")
        finally:
            stub.close()
    print(f"vm.gdb.e2e: {protocol.CHECKS} checks passed ({args.profile})")
    print(f"evidence-result=vm.gdb.e2e/{args.profile}:synthetic-address-breakpoint "
          f"assertions={protocol.CHECKS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
