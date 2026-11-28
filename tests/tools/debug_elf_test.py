#!/usr/bin/env python3
"""Semantic and adversarial tests for the private VM debug ELF writer."""

from __future__ import annotations

import json
import os
import re
import resource
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TMP_ROOT = ROOT / ".tmp"
CHECKS = 0
FAILS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS, FAILS
    CHECKS += 1
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def environment() -> dict[str, str]:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "TMPDIR": str(TMP_ROOT)})
    return env


def run(tool: str, args: list[str], cwd: Path, *, limit: int | None = None,
        **kwargs) -> subprocess.CompletedProcess:
    def set_limits() -> None:
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

    return subprocess.run([tool, *args], cwd=cwd, input=kwargs.pop("input", b""),
                          capture_output=True, timeout=30, env=environment(),
                          preexec_fn=set_limits if limit is not None else None, **kwargs)


def uleb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift > 70:
            raise ValueError("oversized ULEB128")


def sleb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            if byte & 0x40:
                value -= 1 << shift
            return value, offset
        if shift > 70:
            raise ValueError("oversized SLEB128")


def cstring(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(0, offset)
    return data[offset:end].decode("utf-8"), end + 1


def elf_sections(path: Path) -> tuple[dict, dict[str, bytes]]:
    data = path.read_bytes()
    check(data[:4] == b"\x7fELF", f"{path.name}: ELF magic")
    check(data[5] == 1, f"{path.name}: little endian")
    wide = data[4] == 2
    if wide:
        header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
        etype, machine, _, entry, phoff, shoff, _, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = header
        fmt = "<IIQQQQIIQQ"
    else:
        header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        etype, machine, _, entry, phoff, shoff, _, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = header
        fmt = "<IIIIIIIIII"
    check(etype == 1 and phnum == 0 and phentsize == 0,
          f"{path.name}: ET_REL without program headers")
    raw = []
    for index in range(shnum):
        values = struct.unpack_from(fmt, data, shoff + index * shentsize)
        raw.append(values)
    names_section = raw[shstrndx]
    names = data[names_section[4]:names_section[4] + names_section[5]]
    sections = {}
    for values in raw:
        name_offset, stype, flags, addr, offset, size, link, info, align, entsize = values
        name, _ = cstring(names, name_offset)
        sections[name] = data[offset:offset + size]
    return {"wide": wide, "machine": machine, "raw": raw}, sections


def decode_symbols(path: Path, wide: bool) -> list[dict]:
    _, sections = elf_sections(path)
    data = sections[".symtab"]
    strings = sections[".strtab"]
    size = 24 if wide else 16
    result = []
    for offset in range(0, len(data), size):
        if wide:
            name, info, _, shndx, value, length = struct.unpack_from("<IBBHQQ", data, offset)
        else:
            name, value, length, info, _, shndx = struct.unpack_from("<IIIBBH", data, offset)
        text, _ = cstring(strings, name)
        result.append({"name": text, "type": info & 15, "bind": info >> 4,
                       "shndx": shndx, "value": value, "size": length})
    return result


def decode_abbrev(data: bytes) -> dict[int, tuple[int, int, list[tuple[int, int]]]]:
    result = {}
    offset = 0
    while data[offset]:
        code = data[offset]
        offset += 1
        tag, offset = uleb(data, offset)
        children = data[offset]
        offset += 1
        attrs = []
        while True:
            attr, offset = uleb(data, offset)
            form, offset = uleb(data, offset)
            if attr == 0 and form == 0:
                break
            attrs.append((attr, form))
        result[code] = (tag, children, attrs)
    return result


def decode_info(path: Path, wide: bool) -> tuple[dict, list[dict]]:
    _, sections = elf_sections(path)
    abbrev = decode_abbrev(sections[".debug_abbrev"])
    data = sections[".debug_info"]
    unit_length = struct.unpack_from("<I", data)[0]
    check(struct.unpack_from("<H", data, 4)[0] == 4, f"{path.name}: DWARF version 4")
    check(struct.unpack_from("<I", data, 6)[0] == 0, f"{path.name}: abbrev offset")
    addr_size = data[10]
    check(addr_size == (8 if wide else 4), f"{path.name}: DWARF address size")
    offset = 11
    end = 4 + unit_length
    dies = []
    cu = {}
    while offset < end:
        code, offset = uleb(data, offset)
        if code == 0:
            break
        tag, children, attrs = abbrev[code]
        values = {}
        for attr, form in attrs:
            if form == 0x08:
                value, offset = cstring(data, offset)
            elif form == 0x0f:
                value, offset = uleb(data, offset)
            elif form == 0x06:
                value = struct.unpack_from("<I", data, offset)[0]
                offset += 4
            elif form == 0x17:
                value = struct.unpack_from("<I", data, offset)[0]
                offset += 4
            elif form == 0x01:
                value = int.from_bytes(data[offset:offset + addr_size], "little")
                offset += addr_size
            else:
                raise ValueError(f"unsupported DWARF form {form:#x}")
            values[attr] = value
        if tag == 0x11:
            cu = values
        elif tag == 0x2e:
            dies.append(values)
    check(offset == end, f"{path.name}: DIE unit length")
    return cu, dies


def decode_lines(path: Path, wide: bool) -> tuple[list[str], list[dict]]:
    _, sections = elf_sections(path)
    data = sections[".debug_line"]
    unit_length = struct.unpack_from("<I", data)[0]
    check(struct.unpack_from("<H", data, 4)[0] == 4, f"{path.name}: line version 4")
    header_length = struct.unpack_from("<I", data, 6)[0]
    offset = 10
    end_header = offset + header_length
    min_inst = data[offset]
    max_ops = data[offset + 1]
    default_stmt = data[offset + 2]
    line_base = struct.unpack_from("<b", data, offset + 3)[0]
    line_range = data[offset + 4]
    opcode_base = data[offset + 5]
    check(min_inst == 1 and max_ops == 1 and default_stmt == 1,
          f"{path.name}: line header defaults")
    offset += 6
    standard = list(data[offset:offset + opcode_base - 1])
    offset += opcode_base - 1
    check(standard == [0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1],
          f"{path.name}: standard opcode lengths")
    while data[offset]:
        _, offset = cstring(data, offset)
    offset += 1
    files = []
    while data[offset]:
        name, offset = cstring(data, offset)
        _, offset = uleb(data, offset)
        _, offset = uleb(data, offset)
        _, offset = uleb(data, offset)
        files.append(name)
    offset += 1
    check(offset == end_header, f"{path.name}: line header length")
    addr_size = 8 if wide else 4
    address = 0
    file_index = 1
    line = 1
    column = 0
    rows = []
    program_end = 4 + unit_length

    def emit(is_end: bool = False) -> None:
        rows.append({"address": address, "file": files[file_index - 1],
                     "line": line, "column": column, "end": is_end})

    while offset < program_end:
        opcode = data[offset]
        offset += 1
        if opcode == 0:
            length, offset = uleb(data, offset)
            extended_end = offset + length
            subopcode = data[offset]
            offset += 1
            if subopcode == 1:
                emit(True)
                address = 0
                file_index = 1
                line = 1
                column = 0
            elif subopcode == 2:
                address = int.from_bytes(data[offset:offset + addr_size], "little")
                offset += addr_size
            else:
                raise ValueError(f"unsupported extended opcode {subopcode}")
            offset = extended_end
        elif opcode == 1:
            emit()
        elif opcode == 2:
            amount, offset = uleb(data, offset)
            address += amount
        elif opcode == 3:
            delta, offset = sleb(data, offset)
            line += delta
        elif opcode == 4:
            file_index, offset = uleb(data, offset)
        elif opcode == 5:
            column, offset = uleb(data, offset)
        elif opcode == 8:
            address += min_inst * ((255 - opcode_base) // line_range)
        elif opcode == 9:
            address += struct.unpack_from("<H", data, offset)[0]
            offset += 2
        elif opcode >= opcode_base:
            adjusted = opcode - opcode_base
            address += min_inst * (adjusted // line_range)
            line += line_base + adjusted % line_range
            emit()
        else:
            raise ValueError(f"unsupported line opcode {opcode}")
    return files, rows


def decode(path: Path, machine: int, wide: bool) -> tuple[list[dict], list[dict], list[dict]]:
    header, sections = elf_sections(path)
    check(header["machine"] == machine, f"{path.name}: e_machine {header['machine']}")
    check(header["raw"][1][6] == 2 and header["raw"][1][7] == 1,
          f"{path.name}: symbol table links to string table with first global index")
    check(header["raw"][2][1] == 3, f"{path.name}: linked symbol string table type")
    check(set(sections) == {"", ".symtab", ".strtab", ".shstrtab", ".debug_abbrev",
                            ".debug_info", ".debug_line"},
          f"{path.name}: minimal section set")
    check(all(not (values[2] & 0x4) for values in header["raw"]),
          f"{path.name}: no executable sections")
    symbols = decode_symbols(path, wide)
    _, dies = decode_info(path, wide)
    _, rows = decode_lines(path, wide)
    return symbols, dies, rows


def write_project(cwd: Path) -> None:
    (cwd / "root.tc").write_text(
        "#program\nimport lib1\nimport lib2\n"
        "var x: int32 = call lib1.add7, 7, 5\n"
        "var y: int32 = call lib2.add7, x, 1\n"
        "_ = writeln int32, y\n", encoding="utf-8")
    for name in ("lib1.tc", "lib2.tc"):
        (cwd / name).write_text(
            f"#lib\npublic func add7(a: int32, b: int32) int32 then\n"
            f"var c: int32 = add int32, a, b\n_ = return c\nend\n"
            f"public func empty() void then\nend\n", encoding="utf-8")


def verify_project(vm: str, profile: str, machine: int, wide: bool,
                   cwd: Path, label: str) -> None:
    elf = cwd / f"{label}.elf"
    linemap = cwd / f"{label}.json"
    result = run(vm, [f"--profile={profile}", f"--gdb-elf-out={elf}",
                      f"--dump-linemap={linemap}", "root.tc"], cwd)
    check(result.returncode == 0, f"{label}: generation exit {result.returncode}: {result.stderr!r}")
    check(result.stdout == b"13\n", f"{label}: normal program stdout")
    if result.returncode != 0:
        return
    symbols, dies, rows = decode(elf, machine, wide)
    functions = [symbol for symbol in symbols if symbol["type"] == 2]
    check([symbol["name"] for symbol in functions] == ["add7", "add7"],
          f"{label}: source function symbol names")
    check(all(symbol["bind"] == 1 and symbol["shndx"] == 0xfff1 and symbol["size"] == 8
              for symbol in functions), f"{label}: global absolute sized function symbols")
    check(len(dies) == 2 and all(die[0x3b] == 3 for die in dies),
          f"{label}: only nonempty functions have DIEs")
    expected = json.loads(linemap.read_text(encoding="utf-8"))["entries"]
    actual = [{key: row[key] for key in ("address", "file", "line", "column")}
              for row in rows if not row["end"]]
    wanted = [{"address": entry["vm_addr"], "file": entry["file"],
               "line": entry["line"], "column": entry["col"]} for entry in expected]
    check(actual == wanted, f"{label}: DWARF rows equal shared linemap")
    check(rows[-1]["end"] and rows[-1]["address"] == wanted[-1]["address"] + 4,
          f"{label}: terminal end sequence")
    base = 0x544300000000 if wide else 0x54430000
    check(wanted[0]["address"] == base, f"{label}: synthetic code base")
    check(all(symbol["name"] not in {"", "<top>", "empty"} for symbol in functions),
          f"{label}: no synthetic or empty-function symbols")
    check(b"add7#" not in elf.read_bytes() and b"<top>" not in elf.read_bytes(),
          f"{label}: no internal identity in ELF")
    dumped = subprocess.run([DWARFDUMP, "--debug-info", "--debug-line", str(elf)],
                            capture_output=True, timeout=30, env=environment())
    check(dumped.returncode == 0 and not dumped.stderr and
          b"warning:" not in dumped.stdout.lower(), f"{label}: llvm-dwarfdump clean decode")


def cli_checks(vm: str, aot: str, cwd: Path) -> None:
    cases = [
        ("duplicate", [f"--gdb-elf-out={cwd/'a.elf'}", f"--gdb-elf-out={cwd/'b.elf'}", "root.tc"]),
        ("empty", ["--gdb-elf-out=", "root.tc"]),
        ("syntax", ["--syntax-only", f"--gdb-elf-out={cwd/'a.elf'}", "root.tc"]),
        ("sem", ["--sem-only", f"--gdb-elf-out={cwd/'a.elf'}", "root.tc"]),
        ("aot", [f"--gdb-elf-out={cwd/'a.elf'}", "root.tc"]),
    ]
    for name, args in cases:
        result = run(aot if name == "aot" else vm, args, cwd)
        check(result.returncode == 2, f"CLI {name}: exit {result.returncode}")
    source = cwd / "root.tc"
    original = source.read_bytes()
    result = run(vm, ["--static-only", f"--gdb-elf-out={source}", "root.tc"], cwd)
    check(result.returncode == 2 and source.read_bytes() == original,
          "ELF output source alias rejected without truncation")
    hardlink = cwd / "hardlink.elf"
    os.link(source, hardlink)
    result = run(vm, [f"--gdb-elf-out={hardlink}", "root.tc"], cwd)
    check(result.returncode == 2 and source.read_bytes() == original,
          "ELF output hardlink alias rejected without truncation")
    symlink = cwd / "symlink.elf"
    symlink.symlink_to(source.name)
    result = run(vm, [f"--gdb-elf-out={symlink}", "root.tc"], cwd)
    check(result.returncode == 2 and source.read_bytes() == original,
          "ELF output symlink alias rejected without truncation")
    result = run(vm, ["--profile=not-a-profile", f"--gdb-elf-out={cwd/'bad.elf'}", "root.tc"], cwd)
    check(result.returncode == 2, "invalid profile is a usage error")
    result = run(vm, [f"--gdb-elf-out={cwd}", "root.tc"], cwd)
    check(result.returncode == 2, "directory output target rejected")
    fifo = cwd / "output.fifo"
    os.mkfifo(fifo)
    result = run(vm, [f"--gdb-elf-out={fifo}", "root.tc"], cwd)
    check(result.returncode == 2, "FIFO output target rejected without blocking")
    alias = cwd / "alias.out"
    result = run(vm, [f"--gdb-elf-out={alias}", f"--dump-linemap={alias}", "root.tc"], cwd)
    check(result.returncode == 2 and not alias.exists(), "output-to-output alias rejected")
    limited = cwd / "limited.elf"
    result = run(vm, [f"--gdb-elf-out={limited}", "root.tc"], cwd, limit=8)
    check(result.returncode == 70 and not limited.exists(),
          f"ELF write failure maps to 70 and removes the partial output: {result.returncode}")
    existing_limited = cwd / "existing-limited.elf"
    existing_limited.write_bytes(b"keep")
    result = run(vm, [f"--gdb-elf-out={existing_limited}", "root.tc"], cwd, limit=8)
    check(result.returncode == 70 and result.stdout == b"" and
          existing_limited.stat().st_size <= 8,
          "existing-output ELF write failure stops before VM execution")
    static_elf = cwd / "static.elf"
    result = run(vm, ["--static-only", f"--gdb-elf-out={static_elf}", "root.tc"], cwd)
    check(result.returncode == 0 and result.stdout == b"" and static_elf.exists(),
          "static-only generates ELF without executing")
    result = run(vm, ["--profile=w32", f"--gdb-elf-out={cwd/'w32.elf'}", "root.tc"], cwd)
    check(result.returncode == 70 and not (cwd / "w32.elf").exists(),
          "w32 ELF capability failure leaves no artifact")
    existing = cwd / "existing.elf"
    existing.write_bytes(b"keep")
    result = run(vm, ["--profile=w32", f"--gdb-elf-out={existing}", "root.tc"], cwd)
    check(result.returncode == 70 and existing.read_bytes() == b"keep",
          "w32 capability failure preserves an existing output")
    (cwd / "trap.tc").write_text(
        "#program\nvar a: int32 = copy int32, 7\n_ = writeln int32, a\n"
        "var b: int32 = div int32, a, 0\n_ = writeln int32, b\n", encoding="utf-8")
    trap_elf = cwd / "trap.elf"
    result = run(vm, [f"--gdb-elf-out={trap_elf}", "trap.tc"], cwd)
    check(result.returncode == 1 and result.stdout == b"7\n" and trap_elf.exists(),
          "debug ELF remains available after a VM trap")


def gdb_check(vm: str, gdb: str, cwd: Path) -> None:
    profiles = {
        "aarch64-linux-user": (0x544300000000, "aarch64"),
        "aarch64-baremetal": (0x544300000000, "aarch64"),
        "arm32-baremetal": (0x54430000, "arm"),
        "riscv32-baremetal": (0x54430000, "riscv:rv32"),
    }
    for profile, (base, architecture) in profiles.items():
        elf = cwd / f"gdb-{profile}.elf"
        result = run(vm, ["--static-only", f"--profile={profile}",
                          f"--gdb-elf-out={elf}", "root.tc"], cwd)
        check(result.returncode == 0, f"gdb fixture ELF generation: {profile}")
        if result.returncode != 0:
            continue
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        journal = cwd / f"journal-{profile}"
        journal.mkdir()
        runtime_elf = cwd / f"runtime-{profile}.elf"
        proc = subprocess.Popen([vm, f"--profile={profile}", f"--gdb={port}",
                                 f"--gdb-stdout-journal={journal}",
                                 f"--gdb-elf-out={runtime_elf}", "root.tc"],
                                cwd=cwd, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                env=environment())
        try:
            for _ in range(100):
                if proc.poll() is not None:
                    break
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.05):
                        break
                except OSError:
                    time.sleep(0.02)
            commands = [
                gdb, "-q", "-nx", "-batch", "-ex", "set pagination off",
                "-ex", "set tcp connect-timeout 3", "-ex", f"file {elf}",
                "-ex", f"set architecture {architecture}",
            ]
            if architecture == "arm":
                commands += ["-ex", "set arm force-mode thumb"]
            commands += [
                "-ex", f"target remote 127.0.0.1:{port}",
                "-ex", f"mem {base:#x} {base + 0x100000:#x} rw",
                "-ex", "info line add7", "-ex", "break root.tc:5",
                "-ex", "info break", "-ex", "continue",
                "-ex", "printf \"TC_IR_PC=0x%lx\\n\", $tc_ir_pc",
            ]
            # RISC-V stepi uses a two-byte architectural step; IR slots are 4 bytes.
            if architecture in ("arm", "riscv:rv32"):
                commands += ["-ex", "maintenance packet s",
                             "-ex", "maintenance flush register-cache"]
            else:
                commands += ["-ex", "stepi"]
            commands += ["-ex", "printf \"STEP_TC_IR_PC=0x%lx\\n\", $tc_ir_pc"]
            if architecture != "arm":
                commands += ["-ex", "bt"]
            commands += ["-ex", "detach"]
            transcript = subprocess.run(
                commands, cwd=cwd, capture_output=True, timeout=20, env=environment())
            output = transcript.stdout + transcript.stderr
            address = f"0x{base + 4:x}".encode()
            check(transcript.returncode == 0,
                  f"real gdb exit {profile}: {transcript.returncode}: {output!r}")
            check(b"Breakpoint 1 at " + address + b": file root.tc, line 5." in output,
                  f"real gdb source breakpoint address {profile}: {output!r}")
            check(b"Breakpoint 1," in output and b"root.tc:5" in output,
                  f"real gdb stops at source breakpoint {profile}: {output!r}")
            check(b"TC_IR_PC=" + address in output,
                  f"real gdb reads 64-bit synthetic PC {profile}: {output!r}")
            step_pc = re.search(rb"STEP_TC_IR_PC=(0x[0-9a-f]+)", output)
            check(step_pc is not None and int(step_pc.group(1), 16) != int(address, 16),
                  f"real gdb single-steps synthetic IR {profile}: {output!r}")
        finally:
            try:
                stdout, stderr = proc.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                proc.terminate()
                stdout, stderr = proc.communicate(timeout=3)
            check(proc.returncode == 0,
                  f"gdb VM clean exit {profile}: {stdout!r} {stderr!r}")
            check(runtime_elf.exists() and runtime_elf.read_bytes() == elf.read_bytes(),
                  f"--gdb and --gdb-elf-out compose deterministically: {profile}")


def main() -> int:
    global DWARFDUMP
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} TC_VM TC_AOT LLVM_DWARFDUMP GDB", file=sys.stderr)
        return 2
    vm, aot, DWARFDUMP, gdb = sys.argv[1:]
    TMP_ROOT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="debug-elf-", dir=TMP_ROOT) as temporary:
        cwd = Path(temporary)
        write_project(cwd)
        verify_project(vm, "aarch64-linux-user", 183, True, cwd, "aarch64")
        verify_project(vm, "aarch64-baremetal", 183, True, cwd, "aarch64-bare")
        verify_project(vm, "arm32-baremetal", 40, False, cwd, "arm32")
        verify_project(vm, "riscv32-baremetal", 243, False, cwd, "riscv32")
        first = cwd / "aarch64.elf"
        second = cwd / "aarch64-2.elf"
        result = run(vm, ["--static-only", f"--gdb-elf-out={second}", "root.tc"], cwd)
        check(result.returncode == 0 and first.read_bytes() == second.read_bytes(),
              "debug ELF generation is deterministic")
        cli_checks(vm, aot, cwd)
        gdb_check(vm, gdb, cwd)
    print(f"debug ELF checks: {CHECKS - FAILS}/{CHECKS}")
    print("evidence-result=toolchain.debug_elf/debug-elf:four-profile-semantic-decode "
          f"assertions={CHECKS - FAILS}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    raise SystemExit(main())
