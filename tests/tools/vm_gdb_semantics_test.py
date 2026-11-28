#!/usr/bin/env python3
"""Compare VM behavior with and without GDB pauses."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from vm_gdb_protocol_test import BASE, BASE32, ROOT, SOURCE, Stub, check, environment, linemap, reg_value

PROFILES = {
    "aarch64-linux-user": (BASE, 0x22),
    "aarch64-baremetal": (BASE, 0x22),
    "arm32-baremetal": (BASE32, 0x11),
    "riscv32-baremetal": (BASE32, 0x21),
}

TRAP_SOURCE = """#program
var a: int32 = copy int32, 7
_ = writeln int32, a
var b: int32 = div int32, a, 0
_ = writeln int32, b
"""


def normal(tool: str, root: Path, source: str, profile: str) -> subprocess.CompletedProcess:
    (root / "main.tc").write_text(source, encoding="utf-8")
    (root / "tc.project").write_text('tc-project 1\nname = "vm-gdb"\n', encoding="utf-8")
    if source == SOURCE:
        from vm_gdb_protocol_test import LIBRARY
        (root / "lib.tc").write_text(LIBRARY, encoding="utf-8")
    return subprocess.run([tool, f"--profile={profile}", "--trace=trap", "main.tc"], cwd=root,
                          env=environment(root), capture_output=True, timeout=30)


def compare(tool: str, base: Path, source: str, paused: bool, profile: str) -> None:
    name = profile + "-" + ("trap" if source == TRAP_SOURCE else "normal") + \
        ("-paused" if paused else "-continue")
    root = base / name
    root.mkdir()
    expected = normal(tool, root, source, profile)
    debug_root = base / (name + "-gdb")
    stub = Stub(tool, debug_root, source, profile=profile)
    try:
        client = stub.client()
        count = len(linemap(tool, debug_root, profile)["entries"])
        replies = []
        for _ in range(count + 4):
            reply = client.send_packet(b"s" if paused else b"c")
            replies.append(reply)
            if reply.startswith(b"W") or reply == b"T0bthread:1;":
                break
        check(replies[-1].startswith(b"W") or replies[-1] == b"T0bthread:1;",
              f"{name}: execution did not terminate")
        check(stub.stdout() == expected.stdout,
              f"{name}: logical stdout differs: {stub.stdout()!r} != {expected.stdout!r}")
        check(client.send_packet(b"?") == f"W{expected.returncode:02x}".encode(),
              f"{name}: RSP exit status differs")
        if expected.returncode != 0:
            trap = client.monitor("monitor trap")
            check("TC_RE_DIV_ZERO" in trap, f"{name}: first trap diagnostic differs")
        else:
            check(reg_value(client, PROFILES[profile][1]) != 0,
                  f"{name}: missing termination PC")
        check(client.send_packet(b"D") == b"OK", f"{name}: terminated detach")
        client.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == expected.returncode,
              f"{name}: process exit differs")
    finally:
        stub.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tool")
    tool = str(Path(parser.parse_args().tool).resolve())
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vm-gdb-semantics-", dir=ROOT / ".tmp") as tmp:
        base = Path(tmp)
        for profile in PROFILES:
            for source in (SOURCE, TRAP_SOURCE):
                compare(tool, base, source, False, profile)
                compare(tool, base, source, True, profile)
    print("vm.gdb.semantics: stdout, first trap and exit status agree across four profiles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
