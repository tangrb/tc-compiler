#!/usr/bin/env python3
"""Adversarial contract tests for T1 observation outputs."""

from __future__ import annotations

import json
import os
import re
import resource
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TMP_ROOT = ROOT / ".tmp"
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

FAILS = 0
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global FAILS, CHECKS
    CHECKS += 1
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def environment() -> dict[str, str]:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(TMP_ROOT)})
    return env


def run(tool: str, args: list[str], cwd: Path, *, stdin: bytes = b"",
        limit: int | None = None, stdout=None) -> subprocess.CompletedProcess:
    def set_limits() -> None:
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

    return subprocess.run(
        [tool, *args], cwd=cwd, input=stdin, stdout=stdout,
        capture_output=stdout is None, timeout=30, env=environment(),
        preexec_fn=set_limits if limit is not None else None,
    )


def run_merged(tool: str, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["/bin/sh", "-c", 'exec "$@" 2>&1', "sh", tool, *args],
        cwd=cwd, capture_output=True, timeout=30, env=environment(),
    )


def write(cwd: Path, name: str, text: str) -> Path:
    path = cwd / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def records(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def data_records(items: list[dict]) -> list[dict]:
    return [item for item in items if item.get("type") not in
            {"header", "truncated", "footer"}]


def event_records(items: list[dict], key: str) -> list[dict]:
    return [item for item in data_records(items) if key in item.get("detail", {})]


def check_json_keys(item: dict, keys: list[str], label: str) -> None:
    check(list(item) == keys, f"{label} JSON key order {list(item)!r}")


def elf_text_size(path: Path) -> int:
    data = path.read_bytes()
    check(data[:4] == b"\x7fELF", f"ELF identity {path.name}")
    if data[:4] != b"\x7fELF":
        return -1
    wide = data[4] == 2
    endian = "<" if data[5] == 1 else ">"
    if wide:
        section_offset = struct.unpack_from(endian + "Q", data, 40)[0]
        entry_size, count, names_index = struct.unpack_from(
            endian + "HHH", data, 58)
    else:
        section_offset = struct.unpack_from(endian + "I", data, 32)[0]
        entry_size, count, names_index = struct.unpack_from(
            endian + "HHH", data, 46)
    names_header = section_offset + names_index * entry_size
    if wide:
        names_offset, names_size = struct.unpack_from(endian + "QQ", data,
                                                      names_header + 24)
    else:
        names_offset, names_size = struct.unpack_from(endian + "II", data,
                                                      names_header + 16)
    names = data[names_offset:names_offset + names_size]
    for index in range(count):
        offset = section_offset + index * entry_size
        name_offset = struct.unpack_from(endian + "I", data, offset)[0]
        end = names.find(b"\0", name_offset)
        if names[name_offset:end] == b".text":
            if wide:
                return struct.unpack_from(endian + "Q", data, offset + 32)[0]
            return struct.unpack_from(endian + "I", data, offset + 20)[0]
    return -1


def assembly_demand(path: Path, riscv: bool) -> int:
    total = 0
    in_text = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line == ".text" or line.startswith(".text."):
            in_text = True
        elif line.startswith(".section"):
            in_text = line.startswith((".section .text", '.section ".text"'))
        elif line in {".data", ".bss", ".rodata"}:
            in_text = False
        elif line.startswith(".p2align") and in_text:
            alignment = 1 << int(line.split()[1])
            total += (-total) % alignment
        elif in_text and not line.startswith((".", "#", "@", "/")) and \
                not line.endswith(":"):
            total += 8 if riscv and line.split()[0] in {"call", "tail"} else 4
    return total


def simple_program() -> str:
    return engine.program(
        "var x: int32 = copy int32, 7",
        "_ = writeln int32, x",
    )


def check_vm(vm: str, cwd: Path) -> None:
    source = write(cwd, "main.tc", simple_program())
    trace = cwd / "trace.jsonl"
    stats = cwd / "stats.txt"
    dump = cwd / "ir.txt"
    proc = run(vm, [f"--dump-ir={dump}", f"--trace-out={trace}",
                    "--trace=ir,io", "--trace-format=jsonl",
                    f"--stats-out={stats}", "--stats", str(source)], cwd)
    check(proc.returncode == 0, f"VM observation exit {proc.returncode}: {proc.stderr!r}")
    check(proc.stdout == b"7\n", "VM observation keeps program stdout")
    check(proc.stderr == b"", f"explicit outputs do not interleave: {proc.stderr!r}")
    check("funcs:\n" in dump.read_text(encoding="utf-8"), "IR dump has finalized IR")
    items = records(trace)
    check(bool(items) and items[0]["type"] == "header" and
          items[-1]["type"] == "footer", "trace header/footer")
    check_json_keys(items[0], ["type", "format", "version", "tool", "profile",
                               "source"], "header")
    check(items[0]["format"] == "tc-trace" and items[0]["version"] == 3 and
          items[0]["tool"] == "tc-vm" and items[0]["source"] == "main.tc",
          "trace header identity")
    data = data_records(items)
    check([item["seq"] for item in data] == list(range(1, len(data) + 1)),
          "trace sequence starts at one and is contiguous")
    check(all(item["kind"] == "unit" and item["func"] == "<top>" and
              item["module"] == "main.tc" and item["depth"] == 0 for item in data),
          "top-level trace identity")
    check(all(list(item)[:8] == ["seq", "depth", "kind", "module", "func", "bb",
                                "insn", "op"] for item in data),
          "trace deterministic field prefix")
    io = event_records(items, "bytes")
    check(len(io) == 1 and io[0]["detail"] ==
          {"bytes": 2, "result": "ok"},
          f"writeln I/O accounting {io!r}")
    footer = items[-1]
    check_json_keys(footer, ["type", "records", "dropped", "status"], "footer")
    check(footer["records"] == len(data) and footer["dropped"] == 0 and
          footer["status"] == "ok", "trace footer accounting")
    text = stats.read_text(encoding="utf-8").splitlines()
    prefix = ["executed=2", "depth_max=0", "region_peak.frame=",
              "region_peak.static=", "region_peak.device=0",
              "region_peak.heap=0", "region_peak.code=16", "allocations=0",
              "io_bytes=2"]
    check(text[0].startswith("# tc-stats 1 tc-vm ") and
          all(line.startswith(value) for line, value in zip(text[1:], prefix)),
          f"VM stats fixed field order {text!r}")
    ops = [line.split("=", 1)[0][3:] for line in text[10:]]
    check(ops == sorted(ops) and "copy" in ops and "writeln" in ops,
          f"VM opcode stats full-name order {ops!r}")
    check(str(cwd) not in trace.read_text(encoding="utf-8"),
          "trace has no temporary absolute path")

    device_source = write(cwd, "device.tc", engine.program(
        "var address: usize = copy usize, 0x09001000",
        "unsafe then",
        "var p: ptr<uint8> = bitcast ptr<uint8>, address",
        "_ = store.volatile uint8, p, 1",
        "end",
    ))
    device_stats = cwd / "device.stats"
    proc = run(vm, ["--profile=aarch64-baremetal", f"--stats-out={device_stats}",
                    "--stats", str(device_source)], cwd)
    check(proc.returncode == 0 and
          "region_peak.device=4097\n" in device_stats.read_text(encoding="utf-8"),
          f"device peak counts successful byte high-water mark {proc.stderr!r}")

    move_source = write(cwd, "device-move.tc", engine.program(
        "var source: usize = copy usize, 0x09002000",
        "var destination: usize = copy usize, 0x09001000",
        "unsafe then",
        "var src: ptr<uint8> = bitcast ptr<uint8>, source",
        "var dst: ptr<uint8> = bitcast ptr<uint8>, destination",
        "_ = memmove uint8, dst, src, 1",
        "end",
    ))
    move_stats = cwd / "device-move.stats"
    proc = run(vm, ["--profile=aarch64-baremetal", "--device=readwrite",
                    f"--stats-out={move_stats}", "--stats",
                    str(move_source)], cwd)
    check(proc.returncode == 0 and
          "region_peak.device=8193\n" in move_stats.read_text(encoding="utf-8"),
          f"device memmove source contributes to high-water mark {proc.stderr!r}")

    write(cwd, "empty.tc", engine.library(
        "public func empty() void then",
        "end",
    ))
    empty_source = write(cwd, "empty-call.tc", engine.program(
        "import empty",
        "_ = call empty.empty",
        "_ = writeln int32, 1",
    ))
    empty_stats = cwd / "empty-call.stats"
    proc = run(vm, [f"--stats-out={empty_stats}", "--stats",
                    str(empty_source)], cwd)
    check(proc.returncode == 0 and proc.stdout == b"1\n" and
          "depth_max=1\n" in empty_stats.read_text(encoding="utf-8"),
          f"empty callee contributes to maximum depth {proc.stderr!r}")

    second = cwd / "trace-second.jsonl"
    proc = run(vm, [f"--trace-out={second}", "--trace=ir,io",
                    "--trace-format=jsonl", str(source)], cwd)
    check(proc.returncode == 0 and trace.read_bytes() == second.read_bytes(),
          "trace bytes deterministic across output paths")

    limited = cwd / "limited.jsonl"
    proc = run(vm, [f"--trace-out={limited}", "--trace=ir", "--trace-limit=1",
                    "--trace-format=jsonl", str(source)], cwd)
    check(proc.returncode == 0 and proc.stdout == b"7\n",
          "trace truncation does not alter execution")
    items = records(limited)
    check(len(data_records(items)) == 1 and
          sum(item.get("type") == "truncated" for item in items) == 1,
          "trace limit emits one data record and one marker")
    check(items[-1]["records"] == 1 and items[-1]["dropped"] == 1 and
          items[-1]["status"] == "ok", "trace drop accounting")

    text_trace = cwd / "trace.txt"
    proc = run(vm, [f"--trace-out={text_trace}", "--trace=ir,io", str(source)], cwd)
    lines = text_trace.read_text(encoding="utf-8").splitlines()
    check(proc.returncode == 0 and lines[0] ==
          "# tc-trace 3 tc-vm aarch64-linux-user main.tc" and
          lines[-1] == "# tc-trace end 3 ok", f"text trace controls {lines!r}")
    check(lines[1].startswith("[0001] ") and lines[2].startswith("[0002] "),
          "text trace zero-padded sequence")

    proc = run(vm, ["--dump-ir", "--trace=ir", "--stats", str(source)], cwd)
    stderr = proc.stderr.decode("utf-8")
    check(proc.returncode == 0 and stderr.find("funcs:\n") <
          stderr.find("# tc-trace 3") < stderr.find("# tc-trace end") <
          stderr.find("executed="), "stderr dump/trace/stats ordering")


def check_calls(vm: str, cwd: Path) -> None:
    source = write(cwd, "calls.tc", engine.program(
        "import lib",
        "var y: int32 = copy int32, 4",
        "var z: int32 = call lib.twice, y",
        "_ = writeln int32, z",
    ))
    write(cwd, "lib.tc", engine.library(
        "public func twice(x: int32) int32 then",
        "var r: int32 = add.wrap int32, x, x",
        "_ = return r",
        "end",
    ))
    trace = cwd / "calls.jsonl"
    proc = run(vm, [f"--trace-out={trace}", "--trace=call",
                    "--trace-format=jsonl", str(source)], cwd)
    check(proc.returncode == 0 and proc.stdout == b"8\n",
          f"call trace execution: {proc.returncode} {proc.stderr!r}")
    data = event_records(records(trace), "direction")
    check([item["detail"]["direction"] for item in data] == ["enter", "return"],
          f"call enter/return order {data!r}")
    if len(data) == 2:
        check(data[0]["seq"] + 1 == data[1]["seq"] and
              data[0]["bb"] == data[1]["bb"] and
              data[0]["insn"] == data[1]["insn"] and
              data[0]["depth"] == data[1]["depth"] == 0 and
              data[0]["func"] == "<top>", "call events anchored at caller")


def check_traps(vm: str, cwd: Path) -> None:
    source = write(cwd, "trap.tc", engine.program(
        "unsafe then", "_ = trap", "end",
    ))
    trace = cwd / "trap.jsonl"
    stats = cwd / "trap.stats"
    proc = run(vm, [f"--trace-out={trace}", "--trace=trap",
                    "--trace-format=jsonl", f"--stats-out={stats}", "--stats",
                    str(source)], cwd)
    check(proc.returncode == 1 and proc.stdout == b"",
          f"explicit trap status: {proc.returncode} {proc.stderr!r}")
    stderr = proc.stderr.decode("utf-8")
    check(re.search(r"error\[RT\]: TC_RE_[A-Z0-9_]+:.*"
                    r"\[func=<top> bb=[0-9]+ insn=[0-9]+ op=trap depth=0 "
                    r"kind=unit module=trap.tc\]$", stderr, re.MULTILINE) is not None,
          f"trap report context {stderr!r}")
    items = records(trace)
    data = data_records(items)
    check(items[-1]["status"] == "trap" and len(data) == 1 and
          data[0]["detail"].get("code", "").startswith("TC_RE_"),
          "trap trace footer and record")
    check("first_trap=TC_RE_" in stats.read_text(encoding="utf-8"),
          "trap stats first_trap")

    root = write(cwd, "root.tc", engine.program(
        "import lib", "var x: int32 = call lib.f, 3",
    ))
    write(cwd, "lib.tc", engine.library(
        "public func f(x: int32) int32 then",
        "var p: ptr<int32> = addr int32, x",
        "unsafe then", "_ = store int32, p, 5", "end",
        "_ = return x", "end",
    ))
    mem_trace = cwd / "mem-trap.jsonl"
    proc = run(vm, [f"--trace-out={mem_trace}", "--trace=mem,trap",
                    "--trace-format=jsonl", str(root)], cwd)
    check(proc.returncode == 1 and "TC_RE_WRITE_PROTECTED" in
          proc.stderr.decode("utf-8"), "read-only parameter trap")
    data = data_records(records(mem_trace))
    check("region" in data[0].get("detail", {}) and
          "code" in data[1].get("detail", {}),
          f"mem precedes permission trap {data!r}")
    if len(data) == 2:
        check(data[0]["insn"] == data[1]["insn"] and
              data[1]["func"] == "f" and data[1]["kind"] == "function" and
              data[1]["module"] == "lib.tc" and data[1]["depth"] == 1,
              "function trap identity")


def check_input(vm: str, cwd: Path) -> None:
    source = write(cwd, "input.tc", engine.program(
        "var a: int32 = read int32",
        "var b: int32 = read int32",
        "var sum: int32 = add.wrap int32, a, b",
        "_ = writeln int32, sum",
    ))
    trace = cwd / "input.jsonl"
    stats = cwd / "input.stats"
    proc = run(vm, [f"--trace-out={trace}", "--trace=io",
                    "--trace-format=jsonl", f"--stats-out={stats}", "--stats",
                    str(source)], cwd, stdin=b" -42 7")
    check(proc.returncode == 0 and proc.stdout == b"-35\n",
          f"chunked input execution: {proc.returncode} {proc.stderr!r}")
    io = [item for item in data_records(records(trace)) if item["op"] == "read"]
    check([item["detail"]["bytes"] for item in io] == [5, 1] and
          all(item["detail"]["result"] == "ok" for item in io),
          f"logical input bytes include separators {io!r}")
    check("io_bytes=10\n" in stats.read_text(encoding="utf-8"),
          "input plus output byte statistics")

    eof_trace = cwd / "eof.jsonl"
    proc = run(vm, [f"--trace-out={eof_trace}", "--trace=io,trap",
                    "--trace-format=jsonl", str(source)], cwd)
    data = data_records(records(eof_trace))
    check(proc.returncode == 1 and data[0]["detail"] ==
          {"bytes": 0, "result": "eof"} and
          "code" in data[-1].get("detail", {}), f"EOF I/O event {data!r}")

    bad_trace = cwd / "bad-input.jsonl"
    proc = run(vm, [f"--trace-out={bad_trace}", "--trace=io",
                    "--trace-format=jsonl", str(source)], cwd, stdin=b"x")
    data = data_records(records(bad_trace))
    check(proc.returncode == 1 and data[0]["detail"] ==
          {"bytes": 0, "result": "error"},
          f"invalid input I/O event {data!r}")


def check_cli(vm: str, aot: str, cwd: Path) -> None:
    source = write(cwd, "main.tc", simple_program())
    invalid_vm = [
        ["--trace="], ["--trace=ir,,io"], ["--trace=IR"],
        ["--trace-limit=-1", "--trace=ir"],
        ["--trace-limit=18446744073709551616", "--trace=ir"],
        ["--trace-out=x"], ["--stats-out=x"],
        ["--dump-ir=a", "--dump-ir=b"],
        ["--trace=ir", "--trace=io"],
        ["--trace=ir", "--trace-out=a", "--trace-out=b"],
        ["--trace=ir", "--trace-format=text", "--trace-format=jsonl"],
        ["--trace=ir", "--trace-limit=1", "--trace-limit=2"],
        ["--stats", "--stats-out=a", "--stats-out=b"],
        ["--diag-format=text", "--diag-format=json"],
        ["--syntax-only", "--stats"], ["--sem-only", "--trace=ir"],
        ["--static-only", "--stats"], ["--static-only", "--trace=ir"],
        ["--emit=obj", "--output=vm.out"], ["--output=vm.out"],
    ]
    for options in invalid_vm:
        proc = run(vm, [*options, str(source)], cwd)
        check(proc.returncode == 2 and proc.stdout == b"",
              f"VM invalid options {options!r}: {proc.returncode} {proc.stderr!r}")
    proc = run(aot, ["--trace=ir", str(source)], cwd)
    check(proc.returncode == 2 and proc.stdout == b"", "AOT rejects trace")
    proc = run(aot, ["--static-only", "--debug", str(source)], cwd)
    check(proc.returncode == 2 and proc.stdout == b"", "AOT static-only rejects debug")
    merged_vm = run_merged(vm, ["--stats", str(source)], cwd)
    check(merged_vm.returncode == 2 and merged_vm.stdout == b"",
          "VM rejects fd1/fd2 identity mixing")
    merged_aot = run_merged(aot, [str(source)], cwd)
    check(merged_aot.returncode == 2 and merged_aot.stdout == b"",
          "AOT fd1 result rejects fd1/fd2 identity mixing")
    proc = run(vm, ["--include=one", "--include=two", "--stats", "--stats",
                    "--trace-limit=18446744073709551615", "--trace=ir",
                    str(source)], cwd)
    check(proc.returncode == 0, "repeatable include and idempotent stats remain valid")


def check_output_safety(vm: str, aot: str, cwd: Path) -> None:
    source = write(cwd, "safe.tc", simple_program())
    original = source.read_bytes()
    alias = cwd / "source-alias.tc"
    alias.hardlink_to(source)
    directory = cwd / "directory"
    directory.mkdir()
    symlink = cwd / "symlink.tc"
    symlink.symlink_to(source.name)
    cases = [
        ["--dump-ir=safe.tc"], [f"--dump-ir={alias}"],
        ["--dump-ir=directory"], ["--dump-ir=symlink.tc"],
        ["--dump-ir=/dev/null"], ["--dump-ir=/dev/stdout"],
        ["--dump-ir=/dev/stderr"],
        ["--stats", "--stats-out=safe.tc"],
        ["--dump-ir=same", "--stats", "--stats-out=same"],
    ]
    for options in cases:
        proc = run(vm, [*options, str(source)], cwd)
        check(proc.returncode == 2 and proc.stdout == b"",
              f"unsafe output {options!r}: {proc.returncode} {proc.stderr!r}")
        check(source.read_bytes() == original, "source remains unchanged")
    check(not (cwd / "same").exists(), "aliasing outputs are not published")

    bad = write(cwd, "bad.tc", "#program\nvar x: int32 = missing\n")
    bad_original = bad.read_bytes()
    proc = run(vm, [f"--dump-ir={bad}", str(bad)], cwd)
    check(proc.returncode == 2 and proc.stderr == b"" and
          bad.read_bytes() == bad_original,
          "output preflight precedes first diagnostic")
    reserved = cwd / "reserved.ir"
    proc = run(vm, [f"--dump-ir={reserved}", str(bad)], cwd)
    check(proc.returncode == 1 and b"error[" in proc.stderr and
          not reserved.exists(), "reserved output cleaned after diagnostic")

    trace = cwd / "limited-io.jsonl"
    proc = run(vm, [f"--trace-out={trace}", "--trace=ir", "--trace-format=jsonl",
                    str(source)], cwd, limit=96)
    check(proc.returncode == 70 and proc.stdout == b"7\n",
          "trace sink failure preserves execution and maps to tool I/O: "
          f"{proc.returncode} {proc.stdout!r} {proc.stderr!r}")
    check(b'"type":"footer"' not in trace.read_bytes(),
          "failed trace sink has no footer")

    explicit = cwd / "explicit.s"
    proc = run_merged(aot, ["--emit=asm", f"--output={explicit}",
                            str(source)], cwd)
    check(proc.returncode == 0 and proc.stdout == b"" and explicit.is_file() and
          ".text" in explicit.read_text(encoding="utf-8"),
          f"explicit AOT output tolerates merged stderr: {proc.returncode} {proc.stdout!r}")
    proc = run_merged(aot, ["--emit=asm", "--output=/dev/stdout",
                            str(source)], cwd)
    check(proc.returncode == 2 and proc.stdout == b"",
          "AOT /dev/stdout result still rejects fd1/fd2 mixing")

    includes = cwd / "includes"
    includes.mkdir()
    importing = write(cwd, "importing.tc",
                      "#program\nimport absent\n_ = writeln int32, 1\n")
    dangling = cwd / "dangling.s"
    dangling.symlink_to(includes / "absent.tc")
    proc = run(aot, ["--emit=asm", f"--include={includes}",
                     f"--output={dangling}", str(importing)], cwd)
    check(proc.returncode == 2 and proc.stdout == b"",
          f"dangling potential-import alias is usage: {proc.returncode} {proc.stderr!r}")

    cycle = cwd / "cycle"
    cycle.symlink_to(cycle.name)
    proc = run(aot, ["--emit=asm", f"--output={cycle / 'out.s'}",
                     str(source)], cwd)
    check(proc.returncode == 2 and proc.stdout == b"",
          f"intermediate symlink cycle is usage: {proc.returncode} {proc.stderr!r}")


def check_aot(aot: str, cwd: Path) -> None:
    source = write(cwd, "main.tc", simple_program())
    stats = cwd / "aot.stats"
    dump = cwd / "aot.ir"
    proc = run(aot, ["--stats", f"--stats-out={stats}", f"--dump-ir={dump}",
                     str(source)], cwd)
    check(proc.returncode == 0 and b".text" in proc.stdout,
          f"AOT observation emission {proc.returncode}: {proc.stderr!r}")
    text = stats.read_text(encoding="utf-8")
    check(text.startswith("# tc-stats 1 tc-aot aarch64-linux-user\nfuncs=0\nir_instrs=") and
          re.search(r"^frame_max=[0-9]+\ncode_bytes=[1-9][0-9]*\n", text,
                    re.MULTILINE) is not None, f"AOT target stats {text!r}")
    check("units:\n" in dump.read_text(encoding="utf-8"), "AOT IR dump")

    static_stats = cwd / "static.stats"
    proc = run(aot, ["--static-only", "--stats", f"--stats-out={static_stats}",
                     str(source)], cwd)
    text = static_stats.read_text(encoding="utf-8")
    check(proc.returncode == 0 and proc.stdout == b"" and
          text.startswith("# tc-stats 1 tc-aot aarch64-linux-user\nfuncs=0\nir_instrs=") and
          "frame_max=" not in text and "code_bytes=" not in text,
          "AOT static-only emits only IR-derived stats")

    target_source = write(cwd, "target.tc", engine.program(
        "var x: int32 = copy int32, 0x12345800",
        "var y: int32 = add.wrap int32, x, 1",
        "_ = writeln int32, y",
    ))
    for profile in ("aarch64-linux-user", "aarch64-baremetal",
                    "arm32-baremetal", "riscv32-baremetal"):
        target_stats = cwd / f"{profile}.stats"
        target_object = cwd / f"{profile}.o"
        proc = run(aot, [f"--profile={profile}", "--emit=obj",
                         f"--output={target_object}", "--stats",
                         f"--stats-out={target_stats}", str(target_source)], cwd)
        check(proc.returncode == 0,
              f"AOT target object {profile}: {proc.returncode} {proc.stderr!r}")
        if proc.returncode != 0:
            continue
        match = re.search(r"^code_bytes=([0-9]+)$",
                          target_stats.read_text(encoding="utf-8"), re.MULTILINE)
        target_assembly = cwd / f"{profile}.s"
        proc = run(aot, [f"--profile={profile}", "--emit=asm",
                         f"--output={target_assembly}", str(target_source)], cwd)
        check(proc.returncode == 0,
              f"AOT target assembly {profile}: {proc.returncode} {proc.stderr!r}")
        if proc.returncode != 0 or match is None:
            continue
        demand = assembly_demand(target_assembly, profile.startswith("riscv32"))
        actual = elf_text_size(target_object)
        check(int(match.group(1)) == demand,
              f"code_bytes equals emitted code demand for {profile}: "
              f"stats={match.group(1)} assembly={demand}")
        check(0 < actual <= demand,
              f"assembled .text fits code demand for {profile}: "
              f"elf={actual} demand={demand}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: t1_observe_test.py <tc-vm> <tc-aot>", file=sys.stderr)
        return 2
    vm, aot = sys.argv[1:]
    TMP_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="t1-observe-", dir=TMP_ROOT) as tmp:
        cwd = Path(tmp)
        check_vm(vm, cwd)
        check_calls(vm, cwd)
        check_traps(vm, cwd)
        check_input(vm, cwd)
        check_cli(vm, aot, cwd)
        check_output_safety(vm, aot, cwd)
        check_aot(aot, cwd)
    if FAILS:
        print(f"t1.observe: {FAILS} failure(s) in {CHECKS} checks")
        return 1
    print(f"t1.observe: {CHECKS} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
