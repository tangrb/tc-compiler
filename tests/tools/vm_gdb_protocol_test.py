#!/usr/bin/env python3
"""Adversarial VM GDB RSP protocol and debugger I/O tests."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import resource
import selectors
import signal
import socket
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from vm_gdb_client import GdbClient, GdbClientError, _encode_frame  # noqa: E402
from vm_gdb_stdout_adapter import read_stream  # noqa: E402

BASE = 0x544300000000
BASE32 = 0x54430000
SOURCE = """#program
import lib
var a: int32 = copy int32, 7
var b: int32 = call lib.f, a
_ = writeln int32, b
"""
LIBRARY = """#lib
public func f(x: int32) int32 then
var y: int32 = add int32, x, 1
_ = return y
end
"""
INPUT_SOURCE = """#program
var a: int32 = read int32
_ = writeln int32, a
var b: int32 = read int32
_ = writeln int32, b
"""
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def environment(tmp: Path) -> dict[str, str]:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "TMPDIR": str(tmp)})
    return env


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class Stub:
    def __init__(self, tool: str, root: Path, source: str = SOURCE,
                 *, profile: str = "aarch64-linux-user",
                 allow_write: bool = False, limit: int | None = None,
                 ephemeral: bool = False, stdin=subprocess.DEVNULL):
        self.root = root
        root.mkdir(parents=True, exist_ok=True)
        self.journal = root / "journal"
        self.journal.mkdir()
        write(root / "tc.project", 'tc-project 1\nname = "vm-gdb"\n')
        write(root / "main.tc", source)
        if source == SOURCE:
            write(root / "lib.tc", LIBRARY)
        if ephemeral:
            self.port = 0
        else:
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                self.port = reservation.getsockname()[1]
        args = [tool, f"--profile={profile}",
                "--gdb" if ephemeral else f"--gdb={self.port}",
                f"--gdb-stdout-journal={self.journal}"]
        if allow_write:
            args.append("--gdb-allow-write")
        args.append("main.tc")

        def limits() -> None:
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

        self.process = subprocess.Popen(
            args, cwd=root, env=environment(root), stdin=stdin,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            preexec_fn=limits if limit is not None else None)
        selector = selectors.DefaultSelector()
        selector.register(self.process.stderr, selectors.EVENT_READ)
        deadline = time.monotonic() + 15
        banner = bytearray()
        while b"\n" not in banner:
            if self.process.poll() is not None:
                raise AssertionError(
                    f"stub exited before listening: {self.process.communicate()!r}")
            check(time.monotonic() < deadline, "stub did not report its listener")
            for key, _ in selector.select(0.2):
                banner.extend(os.read(key.fd, 4096))
        selector.close()
        match = re.search(rb"gdbstub on 127\.0\.0\.1:([0-9]+)\n", banner)
        check(match is not None, f"invalid stub banner: {banner!r}")
        if ephemeral:
            check(int(match.group(1)) > 0, "ephemeral listener was not assigned a port")
            self.port = int(match.group(1))
        else:
            check(int(match.group(1)) == self.port, "stub banner port mismatch")
        self.banner_extra = bytes(banner[match.end():])

    def client(self) -> GdbClient:
        client = GdbClient(self.port)
        client.connect()
        return client

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)
        out, err = self.process.communicate()
        check(out == b"", f"debugger mode leaked logical stdout: {out!r}")

    def stdout(self) -> bytes:
        return read_stream(self.journal)


def linemap(tool: str, root: Path, profile: str = "aarch64-linux-user") -> dict:
    output = root / "map.json"
    result = subprocess.run(
        [tool, f"--profile={profile}", "--static-only",
         f"--dump-linemap={output}", "main.tc"], cwd=root,
        env=environment(root), capture_output=True, timeout=30)
    check(result.returncode == 0, f"linemap generation failed: {result.stderr!r}")
    return json.loads(output.read_text())


def reg_value(client: GdbClient, number: int) -> int:
    reply = client.send_packet(f"p{number:x}".encode())
    check(re.fullmatch(rb"[0-9a-f]{16}", reply) is not None,
          f"invalid TC register encoding: {reply!r}")
    return int.from_bytes(bytes.fromhex(reply.decode()), "little")


def protocol(tool: str, base: Path) -> None:
    root = base / "protocol"
    stub = Stub(tool, root, allow_write=True)
    try:
        entries = linemap(tool, root)["entries"]
        check(len(entries) >= 5, "fixture did not finalize expected IR")
        call_index = next(i for i, item in enumerate(entries)
                          if item["module"] == "main.tc" and item["insn"] == 1)
        call_address = BASE + 4 * call_index
        c = stub.client()
        check(c.send_packet(b"qSupported") ==
              b"PacketSize=4000;vContSupported+;QStartNoAckMode+;"
              b"qXfer:features:read+;qXfer:memory-map:read+", "qSupported contract")
        check(c.send_packet(b"?") == b"T05thread:1;", "initial stop reason")
        check(c.send_packet(b"R00") == b"E01", "restart requires extended mode")
        xml = c.q_xfer("features:read:target.xml", length=97)
        tree = ET.fromstring(xml)
        check(tree.findtext("architecture") == "aarch64", "target architecture")
        regs = tree.findall("./feature/reg")
        check(len(regs) == 38, "AArch64 core plus TC register count")
        check([int(reg.attrib["regnum"]) for reg in regs] == list(range(38)),
              "target register numbering")
        memory_xml = ET.fromstring(c.q_xfer("memory-map:read", length=113))
        memory_nodes = memory_xml.findall("memory")
        check([node.attrib["type"] for node in memory_nodes] ==
              ["ram", "ram", "rom", "ram", "rom"], "memory-map order and types")
        static_address = int(memory_nodes[1].attrib["start"], 16)
        device_address = int(memory_nodes[2].attrib["start"], 16)
        heap_address = int(memory_nodes[3].attrib["start"], 16)
        code_address = int(memory_nodes[4].attrib["start"], 16)
        static_before = c.send_packet(f"m{static_address:x},4".encode())
        check(c.send_packet(f"M{static_address:x},4:78563412".encode()) == b"OK",
              "static memory write enabled")
        check(c.send_packet(f"m{static_address:x},4".encode()) == b"78563412",
              "static memory read reflects write")
        check(c.send_packet(f"M{static_address:x},4:{static_before.decode()}".encode()) == b"OK",
              "static memory write restored")
        heap_reply = c.send_packet(f"M{heap_address:x},4:01020304".encode())
        check(heap_reply == b"OK",
              f"heap patch write enabled: {heap_reply!r} at {heap_address:#x}")
        check(c.send_packet(f"m{heap_address:x},4".encode()) == b"01020304",
              "heap patch read reflects write")
        for address, name in ((device_address, "device"), (code_address, "code")):
            check(c.send_packet(f"M{address:x},4:00000000".encode()) == b"E01",
                  f"{name} memory write stays forbidden")
        check(c.send_packet(f"M{BASE:x},4:00000000".encode()) == b"E01",
              "synthetic code memory write stays forbidden")
        g = c.send_packet(b"g")
        check(len(g) == 600 and g[:64] == b"x" * 64, "core register unavailable encoding")
        check(reg_value(c, 0x22) == BASE, "initial synthetic PC")
        check(reg_value(c, 0x23) == 0, "initial IR depth")
        check(c.send_packet(b"p26") == b"E01", "out-of-range register")
        check(c.send_packet(b"P20=0000000000000000") == b"E01", "register write rejected")
        check(c.send_packet(b"G" + b"0" * 600) == b"E01", "bulk register write rejected")
        check(c.send_packet(b"X1,0:") == b"E01", "binary memory write rejected")
        check(c.send_packet(f"m{BASE:x},4".encode()) == b"00000000",
              "synthetic code memory reads as zero")
        check(c.send_packet(f"Z0,{BASE:x},4".encode()) == b"OK", "entry breakpoint")
        check(c.send_packet(f"Z0,{BASE:x}".encode()) == b"OK", "duplicate breakpoint")
        entry_reply = c.send_packet(b"c")
        check(entry_reply == b"T05thread:1;", f"entry breakpoint stop: {entry_reply!r}")
        check(reg_value(c, 0x22) == BASE, "entry breakpoint did not execute instruction")
        check(c.send_packet(b"s") == b"T05thread:1;", "single-step stop")
        check(reg_value(c, 0x22) == BASE + 4, "single-step advanced exactly one IR instruction")
        check(c.send_packet(b"s") == b"T05thread:1;", "call single-step")
        check(reg_value(c, 0x23) == 1, "call single-step entered callee")
        check("func=f" in c.monitor("monitor frames"), "callee frame view")
        c.monitor("monitor ir")
        c.monitor("monitor vreg 0")
        check(b"org.tc.ir" in xml and b"tc_ir_pc" in xml, "TC target feature")
        check(c.send_packet(b"qRcmd,zz") == b"E01", "odd monitor hex rejected")
        check(c.send_packet(b"qRcmd,zzzz") == b"E01", "unknown monitor command")
        check(c.send_packet(b"vCont?") == b"vCont;s;c", "vCont query")
        check(c.send_packet(b"vCont;s:1") == b"T05thread:1;", "vCont single-step")
        check(c.send_packet(b"vCont;s:2") == b"E01", "unknown vCont thread")
        check(c.send_packet(b"vCont;s:c") == b"", "multi-action vCont rejected")
        check(c.send_packet(b"Hg0") == b"OK" and c.send_packet(b"Hc-1") == b"OK",
              "valid thread selectors")
        check(c.send_packet(b"Hg2") == b"E01", "invalid thread selector")
        check(c.send_packet(b"qC") == b"QC1" and
              c.send_packet(b"qfThreadInfo") == b"m1" and
              c.send_packet(b"qfThreadInfo") == b"l", "thread enumeration state")
        check(c.send_packet(b"qAttached") == b"0", "created process attachment state")
        c.enable_no_ack()
        check(c.send_packet(b"qSupported").startswith(b"PacketSize="),
              "no-ack successful frame")
        c.close()

        c = stub.client()
        check(c.send_packet(b"?") == b"T05thread:1;", "reconnect defaults to ack mode")
        check(reg_value(c, 0x22) != BASE, "reconnect preserved stopped PC")
        check(c.send_packet(b"!") == b"OK", "extended mode")
        c.send_packet(b"R00", expect_reply=False)
        check(c.send_packet(b"?") == b"T05thread:1;", "restart stopped state")
        check(reg_value(c, 0x22) == BASE, "restart reset PC")
        check(c.send_packet(f"m{heap_address:x},4".encode()) == b"00000000",
              "restart cleared heap patches")
        check(c.send_packet(f"Z0,{BASE + 2:x}".encode()) == b"E01",
              "unaligned breakpoint rejected")
        check(c.send_packet(f"Z0,{BASE + 4 * len(entries):x}".encode()) == b"E01",
              "termination sentinel breakpoint rejected")
        check(c.send_packet(f"z0,{BASE + 2:x}".encode()) == b"E01",
              "unaligned breakpoint removal rejected")
        check(c.send_packet(f"z0,{BASE:x}".encode()) == b"OK", "restart preserved breakpoint")
        check(c.send_packet(f"P20={(BASE + 4).to_bytes(8, 'little').hex()}".encode()) == b"OK", "architectural PC write")
        check(reg_value(c, 0x22) == BASE + 4, "PC write changed next IR instruction")
        check(c.send_packet(f"P20={(BASE + 2).to_bytes(8, 'little').hex()}".encode()) == b"E01", "unaligned PC rejected")
        check(c.send_packet(b"D") == b"E01", "extended detach rejected")
        c.send_packet(b"R00", expect_reply=False)
        check(c.send_packet(f"Z0,{call_address:x}".encode()) == b"OK", "call breakpoint")
        check(c.send_packet(b"c") == b"T05thread:1;", "call breakpoint hit")
        check(reg_value(c, 0x22) == call_address, "call breakpoint PC")
        next_address = BASE + 4 * (call_index + 1)
        check(c.send_packet(f"Z0,{next_address:x}".encode()) == b"OK",
              "caller return breakpoint")
        check(c.send_packet(b"c") == b"T05thread:1;", "caller return breakpoint stop")
        check(reg_value(c, 0x22) == next_address, "caller return breakpoint PC")
        check(reg_value(c, 0x23) == 0, "caller return breakpoint depth")
        c.send_packet(b"R00", expect_reply=False)
        check(c.send_packet(b"s") == b"T05thread:1;", "finish setup call step")
        check(c.send_packet(b"s") == b"T05thread:1;", "finish setup callee step")
        check(reg_value(c, 0x23) == 1, "finish setup entered callee")
        c.monitor("monitor finish")
        check(c.send_packet(b"?") == b"T05thread:1;", "finish stopped in caller")
        check(reg_value(c, 0x23) == 0, "finish returned to caller")
        c.send_packet(b"R00", expect_reply=False)
        check(c.send_packet(f"Z0,{call_address:x}".encode()) == b"OK", "next breakpoint")
        check(c.send_packet(b"c") == b"T05thread:1;", "next setup stop")
        c.monitor("monitor next")
        check(c.send_packet(b"?") == b"T05thread:1;", "next completed")
        check(reg_value(c, 0x23) == 0, "next crossed the call")
        check(reg_value(c, 0x22) == next_address, "next stopped at caller return")
        check(c.send_packet(b"c") == b"W00", "natural termination")
        check(c.send_packet(b"?") == b"W00", "terminated query")
        check(c.send_packet(b"c") == b"W00", "terminated continue")
        check(c.send_packet(b"vKill") == b"OK", "extended terminated kill")
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 0, "extended stub exit status")
        check(stub.stdout() == b"8\n", "journal preserved logical output")
    finally:
        stub.close()


def detached(tool: str, base: Path) -> None:
    root = base / "detached"
    stub = Stub(tool, root)
    try:
        c = stub.client()
        check(c.send_packet(b"s") == b"T05thread:1;", "detach setup step")
        check(c.send_packet(b"D") == b"OK", "ordinary detach")
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 0 and stub.stdout() == b"8\n",
              "detach continued freely and exited")
        with socket.socket() as attempt:
            attempt.settimeout(0.2)
            check(attempt.connect_ex(("127.0.0.1", stub.port)) != 0,
                  "detached stub kept listening")
    finally:
        stub.close()


def input_restart(tool: str, base: Path) -> None:
    root = base / "input"
    stub = Stub(tool, root, INPUT_SOURCE, stdin=subprocess.PIPE)
    try:
        assert stub.process.stdin is not None
        c = stub.client()
        check(c.send_packet(b"!") == b"OK", "input extended mode")
        entries = linemap(tool, root)["entries"]
        second_read = BASE + 4 * next(i for i, item in enumerate(entries)
                                      if item["module"] == "main.tc" and item["insn"] == 2)
        check(c.send_packet(f"Z0,{second_read:x}".encode()) == b"OK",
              "input second-read breakpoint")
        c.send_packet(b"c", expect_reply=False)
        c._socket.settimeout(0.15)
        try:
            unexpected = c._socket.recv(1)
            raise AssertionError(f"blocked input replied early: {unexpected!r}")
        except socket.timeout:
            pass
        finally:
            c._socket.settimeout(5)
        c.send_packet_nowait(b"m0,1")
        c.send_interrupt()
        check(c.recv_packet() == b"T02thread:1;", "blocked input interrupt")
        check(c.send_packet(b"?") == b"T02thread:1;", "interrupt reason preserved")
        stub.process.stdin.write(b"1")
        stub.process.stdin.flush()
        c.send_packet(b"c", expect_reply=False)
        c._socket.settimeout(0.15)
        try:
            unexpected = c._socket.recv(1)
            raise AssertionError(f"partial token replied early: {unexpected!r}")
        except socket.timeout:
            pass
        finally:
            c._socket.settimeout(5)
        c.send_interrupt()
        check(c.recv_packet() == b"T02thread:1;", "partial token interrupt")
        stub.process.stdin.write(b"2\n3\n4\n")
        stub.process.stdin.flush()
        check(c.send_packet(b"c") == b"T05thread:1;", "prefetched second input stop")
        check(stub.stdout() == b"12\n", "first input token output")
        check(c.send_packet(f"z0,{second_read:x}".encode()) == b"OK",
              "remove input breakpoint before restart")
        c.close()
        c = stub.client()
        check(c.send_packet(b"!") == b"OK", "input reconnect extended mode")
        c.send_packet(b"R00", expect_reply=False)
        check(c.send_packet(b"c") == b"W00", "restart consumed preserved prefetched input")
        check(stub.stdout() == b"12\n3\n4\n", "restart did not replay committed input")
        c.send_packet(b"k", expect_reply=False)
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 0, "input stub exit status")
    finally:
        stub.close()


def malformed(tool: str, base: Path) -> None:
    root = base / "malformed"
    stub = Stub(tool, root)
    try:
        c = stub.client()
        c._send_all(b"$qSupported#00")
        check(c._read_byte() == ord("-"), "bad checksum requests retransmission")
        check(c.send_packet(b"qSupported").startswith(b"PacketSize="),
              "valid frame resynchronizes after bad checksum")
        fragmented = _encode_frame(b"qC")
        for value in fragmented:
            c._send_all(bytes((value,)))
        check(c.recv_packet() == b"QC1", "fragmented frame decoded")
        check(c.send_packet(b"qRcmd," + b"ir 0x0#}".hex().encode()) == b"E01",
              "escaped monitor payload decoded")
        for _ in range(3):
            c._send_all(b"$qC#00")
            check(c._read_byte() == ord("-"), "repeated bad checksum requests retransmission")
        c.close()
        c = stub.client()
        check(c.send_packet(b"qC") == b"QC1", "reconnect after repeated checksum failures")
        c._send_all(b"garbage$qC#b4")
        check(c.recv_packet() == b"QC1", "garbage bytes resynchronize")
        c._send_all(b"$" + b"a" * 16385 + b"#00")
        check(c.send_packet(b"qC") == b"QC1", "oversized frame resynchronization")
        c._ack = False
        c._send_all(b"$qC#00")
        check(c.send_packet(b"qC") == b"QC1", "no-ack bad checksum silently discarded")
        c = stub.client()
        regions = c.monitor("regions")
        static_base = int(re.search(r"static base=(0x[0-9a-f]+)", regions).group(1), 16)
        check(c.send_packet(f"M{static_base:x},4:00000000".encode()) == b"E01",
              "memory writes disabled by default")
        check(c.send_packet(b"D") == b"OK", "no-ack detach")
        c.close()
        stub.process.wait(timeout=10)
    finally:
        stub.close()


def journal_failure(tool: str, base: Path) -> None:
    root = base / "journal-failure"
    stub = Stub(tool, root)
    blocked_record = stub.journal / "record-00000000000000000001.bin"
    blocked_record.mkdir()
    try:
        c = stub.client()
        check(c.send_packet(b"c") == b"T0bthread:1;", "journal IO failure traps")
        trap = c.monitor("monitor trap")
        check("TC_RE_IO" in trap, "journal failure reports TC_RE_IO")
        check(c.send_packet(b"?") == b"W01", "trap termination status")
        check(c.send_packet(b"D") == b"OK", "terminated ordinary detach")
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 1, "journal trap process exit")
        blocked_record.rmdir()
        check(stub.stdout() == b"", "failed output record was not committed")
    finally:
        stub.close()
    root = base / "journal-failure-extended"
    stub = Stub(tool, root)
    blocked_record = stub.journal / "record-00000000000000000001.bin"
    blocked_record.mkdir()
    try:
        c = stub.client()
        check(c.send_packet(b"!") == b"OK", "journal failure extended mode")
        check(c.send_packet(b"c") == b"T0bthread:1;", "extended journal IO failure traps")
        check(c.send_packet(b"R00") == b"E01", "permanent journal failure blocks restart")
        check(c.send_packet(b"?") == b"W01", "failed restart preserved trap state")
        check(c.send_packet(b"vKill") == b"OK", "extended journal failure kill")
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 1, "extended journal trap process exit")
        blocked_record.rmdir()
        check(stub.stdout() == b"", "extended failed output record was not committed")
    finally:
        stub.close()


def profile_views(tool: str, base: Path) -> None:
    profiles = (
        ("w32", "aarch64", 38, 0x20, 8),
        ("arm32-baremetal", "arm", 21, 0x0F, 4),
        ("riscv32-baremetal", "riscv:rv32", 37, 0x20, 4),
    )
    for profile, architecture, register_count, pc_reg, pc_size in profiles:
        root = base / f"profile-{profile}"
        stub = Stub(tool, root, profile=profile, allow_write=True)
        try:
            entries = linemap(tool, root, profile)["entries"]
            call_index = next(i for i, item in enumerate(entries)
                              if item["module"] == "main.tc" and item["insn"] == 1)
            call_address = BASE32 + 4 * call_index
            c = stub.client()
            tree = ET.fromstring(c.q_xfer("features:read:target.xml", length=97))
            check(tree.findtext("architecture") == architecture,
                  f"{profile} target architecture")
            regs = tree.findall("./feature/reg")
            check(len(regs) == register_count,
                  f"{profile} core plus TC register count")
            check([int(reg.attrib["regnum"]) for reg in regs] ==
                  list(range(register_count)), f"{profile} register numbering")
            pc_reply = c.send_packet(f"p{pc_reg:x}".encode())
            check(pc_reply == BASE32.to_bytes(pc_size, "little").hex().encode(),
                  f"{profile} architectural PC width and synthetic base: {pc_reply!r}")
            tc_pc = c.send_packet(f"p{register_count - 4:x}".encode())
            check(tc_pc == BASE32.to_bytes(8, "little").hex().encode(),
                  f"{profile} 64-bit TC IR PC")
            memory_xml = ET.fromstring(c.q_xfer("memory-map:read", length=113))
            nodes = memory_xml.findall("memory")
            check(len(nodes) == 5 and
                  all(int(node.attrib["start"], 16) <= 0xFFFFFFFF and
                      int(node.attrib["length"], 16) > 0 for node in nodes),
                  f"{profile} five-region 32-bit memory map")
            check(c.send_packet(f"m{BASE32:x},4".encode()) == b"00000000",
                  f"{profile} synthetic code reads as zero")
            check(c.send_packet(f"Z0,{call_address:x},4".encode()) == b"OK",
                  f"{profile} instruction breakpoint")
            check(c.send_packet(b"c") == b"T05thread:1;",
                  f"{profile} breakpoint stop")
            check(c.send_packet(f"z0,{call_address:x},4".encode()) == b"OK",
                  f"{profile} breakpoint removal")
            check(c.send_packet(b"D") == b"OK", f"{profile} detach")
            c.close()
            stub.process.wait(timeout=10)
            check(stub.process.returncode == 0, f"{profile} normal exit")
            check(stub.stdout() == b"8\n", f"{profile} stdout parity")
        finally:
            stub.close()


def cli_errors(tool: str, base: Path) -> None:
    root = base / "cli"
    write(root / "tc.project", 'tc-project 1\nname = "vm-gdb"\n')
    write(root / "main.tc", SOURCE)
    write(root / "lib.tc", LIBRARY)
    cases = [
        (["--gdb=", "main.tc"], 2),
        (["--gdb=65536", "main.tc"], 2),
        (["--gdb=12x", "main.tc"], 2),
        (["--gdb-stdout-journal=journal", "main.tc"], 2),
        (["--gdb-allow-write", "main.tc"], 2),
        (["--gdb", "main.tc"], 70),
        (["--gdb", "--syntax-only", "--gdb-stdout-journal=journal", "main.tc"], 2),
    ]
    for args, expected in cases:
        result = subprocess.run([tool, *args], cwd=root, env=environment(root),
                                capture_output=True, timeout=30)
        check(result.returncode == expected,
              f"CLI {args!r}: {result.returncode}, expected {expected}")
    stub = Stub(tool, base / "cli-ephemeral", ephemeral=True)
    try:
        c = stub.client()
        check(c.send_packet(b"D") == b"OK", "default port session detach")
        c.close()
        stub.process.wait(timeout=10)
        check(stub.process.returncode == 0, "default port stub exit status")
    finally:
        stub.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tool")
    args = parser.parse_args()
    tool = str(Path(args.tool).resolve())
    from vm_gdb_stdout_adapter import selftest as adapter_selftest
    adapter_selftest()
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vm-gdb-protocol-", dir=ROOT / ".tmp") as tmp:
        base = Path(tmp)
        protocol(tool, base)
        profile_views(tool, base)
        detached(tool, base)
        input_restart(tool, base)
        malformed(tool, base)
        journal_failure(tool, base)
        cli_errors(tool, base)
    print(f"vm.gdb.protocol: {CHECKS} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
