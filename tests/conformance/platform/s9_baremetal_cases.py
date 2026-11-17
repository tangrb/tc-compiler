#!/usr/bin/env python3
"""S9 freestanding AArch64 bare-metal platform gate (design §7/§8/§11).

The suite builds the `aarch64-baremetal` ELF and runs it on
qemu-system-aarch64 virt **without semihosting**:

  - PL011 UART0 is the program stdout and stdin channel (a framed byte
    stream: "TCINTCIN", an eight-byte little-endian length, then payload);
  - PL011 UART1 is the exit-record channel;
  - termination is PSCI SYSTEM_OFF (the platform's halt policy).

It verifies the ELF/region/symbol contract, the absence of libc/dynamic
dependencies, the UART console, traps, token input through the runtime arena,
the static initializer bytes and all four device-region policies.

Run:
    python3 tests/conformance/platform/s9_baremetal_cases.py --check
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

PROFILE = "aarch64-baremetal"
CASE_ID = "TC-PLAT-S9-0001"

EM_AARCH64 = 183
ET_REL = 1
ET_EXEC = 2


def llvm_tool(name: str) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    for hint in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin",
                 "/opt/homebrew/bin", "/usr/bin"):
        candidate = Path(hint) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def write(directory: Path, name: str, text: str) -> Path:
    path = directory / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def build(directory: Path, source: str, *extra: str,
          emit: str = "elf") -> tuple[int, str, str, Path]:
    root = write(directory, "root.tc", source)
    output = directory / f"out.{emit}"
    argv = [engine.tc_aot_path(), f"--profile={PROFILE}", f"--emit={emit}",
            f"--output={output}", f"--include={directory}",
            f"--ld={engine.lld_path()}", *extra, str(root)]
    proc = subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=180)
    return (proc.returncode, proc.stdout.decode("utf-8", "replace"),
            proc.stderr.decode("utf-8", "replace"), output)


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def elf_header(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise RuntimeError("not an ELF file")
    if data[4] != 2 or data[5] != 1:
        raise RuntimeError("not a 64-bit little-endian ELF")
    e_type, e_machine, _version, e_entry = struct.unpack_from("<HHIQ", data, 16)
    return {"type": e_type, "machine": e_machine, "entry": e_entry}


def symbols(path: Path) -> dict[str, int]:
    tool = llvm_tool("llvm-readelf") or "readelf"
    proc = subprocess.run([tool, "-s", "-W", str(path)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)
    result: dict[str, int] = {}
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].endswith(":"):
            continue
        try:
            value = int(parts[1], 16)
        except ValueError:
            continue
        result.setdefault(parts[7], value)
    return result


def undefined_symbols(path: Path) -> list[str]:
    tool = llvm_tool("llvm-nm") or "nm"
    proc = subprocess.run([tool, "-u", str(path)], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60)
    names = []
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            names.append(parts[1])
    return names


def dynamic_sections(path: Path) -> str:
    tool = llvm_tool("llvm-readelf") or "readelf"
    proc = subprocess.run([tool, "-d", "-l", "-W", str(path)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)
    return proc.stdout.decode("utf-8", "replace")


def section_bytes(path: Path, section: str) -> bytes:
    data = path.read_bytes()
    header_offset = struct.unpack_from("<Q", data, 40)[0]
    shentsize = struct.unpack_from("<H", data, 58)[0]
    shnum = struct.unpack_from("<H", data, 60)[0]
    shstrndx = struct.unpack_from("<H", data, 62)[0]
    if shnum == 0 or shstrndx >= shnum:
        raise RuntimeError("extended section numbering is not supported")

    def section_at(index: int) -> tuple[int, int, int, int, int]:
        base = header_offset + index * shentsize
        name, kind, _flags, addr, offset, length = struct.unpack_from(
            "<IIQQQQ", data, base)
        return name, kind, addr, offset, length

    _n, _k, _a, shstr_off, shstr_len = section_at(shstrndx)
    names = data[shstr_off:shstr_off + shstr_len]
    for index in range(shnum):
        name_off, _kind, _addr, offset, length = section_at(index)
        end = names.find(b"\x00", name_off)
        name = names[name_off:end].decode("ascii", "replace")
        if name == section:
            return data[offset:offset + length]
    raise RuntimeError(f"section {section} not found")


def disassembly(path: Path) -> str:
    tool = llvm_tool("llvm-objdump") or "objdump"
    proc = subprocess.run([tool, "-d", str(path)], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60)
    return proc.stdout.decode("utf-8", "replace")


def run_elf(elf: Path, stdin: bytes, directory: Path):
    return engine.run_aarch64_baremetal_elf(elf, stdin, directory)


DEVICE_LOAD = program(
    "var a: usize = copy usize, 0x09000018",
    "unsafe then",
    "var p: ptr<uint32> = bitcast ptr<uint32>, a",
    "var v: uint32 = load uint32, p",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_LOAD_VOLATILE = program(
    "var a: usize = copy usize, 0x09000018",
    "unsafe then",
    "var p: ptr<uint32> = bitcast ptr<uint32>, a",
    "var v: uint32 = load.volatile uint32, p",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_STORE = program(
    "var a: usize = copy usize, 0x09040044",
    "unsafe then",
    "var p: ptr<uint32> = bitcast ptr<uint32>, a",
    "_ = store uint32, p, 0",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_STORE_VOLATILE = program(
    "var a: usize = copy usize, 0x09040044",
    "unsafe then",
    "var p: ptr<uint32> = bitcast ptr<uint32>, a",
    "_ = store.volatile uint32, p, 0",
    "end",
    "_ = writeln int32, 1",
)

POLICY_MATRIX = [
    ("reject", "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE",
     "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE"),
    ("read", None, "TC_RE_ADDR_UNAVAILABLE", None,
     "TC_RE_ADDR_UNAVAILABLE"),
    ("readwrite", None, None, None, None),
    ("volatile", "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE", None,
     None),
]


def check_elf_contract(directory: Path, failures: list[str]) -> None:
    source = program(
        "import lib",
        "_ = writeln int32, 42",
        "_ = writeln float64, 3.75",
        "var x: int64 = mul int64, 3037000499, 3037000499",
        "_ = writeln int64, x",
    )
    write(directory, "lib.tc", library("public static var V: int32 = 5"))
    code, _out, stderr, elf = build(directory, source)
    if code != 0 or not elf.exists():
        failures.append(f"{CASE_ID}: bare-metal build failed ({code}): "
                        f"{stderr.strip()}")
        return
    header = elf_header(elf)
    if header["type"] != ET_EXEC or header["machine"] != EM_AARCH64:
        failures.append(f"{CASE_ID}: not an AArch64 ET_EXEC ELF")
    if header["entry"] == 0:
        failures.append(f"{CASE_ID}: ELF entry is zero")
    if not os.access(elf, os.X_OK):
        failures.append(f"{CASE_ID}: ELF is not executable")
    undefined = undefined_symbols(elf)
    if undefined:
        failures.append(f"{CASE_ID}: undefined symbols: {undefined[:8]}")
    info = dynamic_sections(elf)
    if "INTERP" in info or "NEEDED" in info or "DYNAMIC" in info:
        failures.append(f"{CASE_ID}: dynamic dependency present")
    for name, expected in (
        ("__tc_code_start", None),
        ("__tc_static_region", None),
        ("__tc_stack_bottom", 0x4E000000),
        ("__tc_stack_top", 0x4F000000),
        ("__tc_input_image", 0x41000000),
        ("__tc_rt_device_base", None),
        ("__tc_rt_device_policy", None),
        ("__tc_rt_checked", None),
        ("tc_main", None),
        ("_start", None),
    ):
        table = symbols(elf)
        if name not in table:
            failures.append(f"{CASE_ID}: missing symbol {name}")
            continue
        if expected is not None and table[name] != expected:
            failures.append(
                f"{CASE_ID}: {name} = 0x{table[name]:x}, expected "
                f"0x{expected:x}")
    table = symbols(elf)
    if table.get("__tc_code_start") != 0x40000000:
        failures.append(f"{CASE_ID}: code region does not start at 0x40000000")
    if table.get("__tc_static_region", 0) < 0x40000000:
        failures.append(f"{CASE_ID}: static region is not in the loaded image")
    if table.get("_start") != header["entry"]:
        failures.append(f"{CASE_ID}: ELF entry is not _start")
    if re.search(r"hlt\s*#0xf000", disassembly(elf)):
        failures.append(
            f"{CASE_ID}: bare-metal image contains a semihosting call")
    archive = engine.runtime_archive(64, baremetal=True)
    nm = llvm_tool("llvm-nm") or "nm"
    archive_symbols = subprocess.run(
        [nm, str(archive)], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=60).stdout.decode("utf-8", "replace")
    for forbidden in ("tc_module_load", "tc_module_resolve", "dlopen",
                      "fopen", "printf", "malloc"):
        if forbidden in archive_symbols:
            failures.append(
                f"{CASE_ID}: bare-metal runtime references {forbidden}")
    result = run_elf(elf, b"", directory)
    expected_out = (b"42\n3.75\n9223372030926249001\n")
    if result.exit != 0 or result.trap is not None:
        failures.append(f"{CASE_ID}: normal run trapped ({result.trap})")
    if result.stdout != expected_out:
        failures.append(f"{CASE_ID}: stdout {result.stdout!r} != "
                        f"{expected_out!r}")
    if "TC-EXIT 0 none" not in result.diagnostic:
        failures.append(f"{CASE_ID}: missing normal exit record: "
                        f"{result.diagnostic!r}")


def check_trap(directory: Path, failures: list[str]) -> None:
    source = program(
        "_ = writeln int32, 7",
        "var a: int32 = copy int32, 2147483647",
        "var b: int32 = add int32, a, 1",
    )
    code, _out, stderr, elf = build(directory, source)
    if code != 0:
        failures.append(f"TC-PLAT-S9-0002: build failed: {stderr.strip()}")
        return
    result = run_elf(elf, b"", directory)
    if result.stdout != b"7\n":
        failures.append(f"TC-PLAT-S9-0002: stdout {result.stdout!r}")
    if result.exit != 1 or result.trap != "TC_RE_INTEGER_OVERFLOW":
        failures.append(f"TC-PLAT-S9-0002: trap {result.trap} exit "
                        f"{result.exit}")
    if "TC_RE_INTEGER_OVERFLOW" not in result.diagnostic:
        failures.append("TC-PLAT-S9-0002: trap code missing from UART record")


def check_input(directory: Path, failures: list[str]) -> None:
    source = program(
        "var a: int32 = read int32",
        "var b: int64 = read int64",
        "_ = writeln int32, a",
        "_ = writeln int64, b",
    )
    code, _out, stderr, elf = build(directory, source)
    if code != 0:
        failures.append(f"TC-PLAT-S9-0003: build failed: {stderr.strip()}")
        return
    result = run_elf(elf, b"12 -9223372036854775808\n", directory)
    expected = b"12\n-9223372036854775808\n"
    if result.stdout != expected or result.exit != 0:
        failures.append(f"TC-PLAT-S9-0003: stdout {result.stdout!r} exit "
                        f"{result.exit} trap {result.trap}")
    long_source = program("var x: int32 = read int32",
                          "_ = writeln int32, x")
    code, _out, stderr, elf = build(directory, long_source)
    if code != 0:
        failures.append(f"TC-PLAT-S9-0003: arena build failed: "
                        f"{stderr.strip()}")
        return
    result = run_elf(elf, b"0" * 4000 + b"42\n", directory)
    if result.stdout != b"42\n" or result.exit != 0:
        failures.append(f"TC-PLAT-S9-0003: arena token input {result.stdout!r} "
                        f"exit {result.exit} trap {result.trap}")


def check_device_policies(directory: Path, failures: list[str]) -> None:
    programs = (
        ("nonvolatile load", DEVICE_LOAD),
        ("nonvolatile store", DEVICE_STORE),
        ("volatile load", DEVICE_LOAD_VOLATILE),
        ("volatile store", DEVICE_STORE_VOLATILE),
    )
    for policy, *expected in POLICY_MATRIX:
        for index, (label, source) in enumerate(programs):
            code, _out, stderr, elf = build(directory, source,
                                            f"--device={policy}")
            if code != 0:
                failures.append(
                    f"TC-PLAT-S9-0004: {policy}/{label} build failed: "
                    f"{stderr.strip()}")
                continue
            result = run_elf(elf, b"", directory)
            want_trap = expected[index]
            if want_trap is None:
                if result.trap is not None or result.stdout != b"1\n":
                    failures.append(
                        f"TC-PLAT-S9-0004: {policy}/{label}: expected "
                        f"success, got trap {result.trap} stdout "
                        f"{result.stdout!r}")
            elif result.trap != want_trap:
                failures.append(
                    f"TC-PLAT-S9-0004: {policy}/{label}: trap "
                    f"{result.trap} != {want_trap}")


def check_static_initializer(directory: Path, failures: list[str]) -> None:
    write(directory, "lib.tc",
          library("public static var V: int32 = 0x12345678"))
    source = program("import lib", "_ = writeln int32, lib.V")
    code, _out, stderr, elf = build(directory, source)
    if code != 0:
        failures.append(f"TC-PLAT-S9-0005: build failed: {stderr.strip()}")
        return
    data = section_bytes(elf, ".data")
    table = symbols(elf)
    start = table.get("__tc_static_region")
    if start is None:
        failures.append("TC-PLAT-S9-0005: no static region symbol")
        return
    # The section is emitted at the static region base in this image.
    offset = 0
    if len(data) < offset + 4 or data[offset:offset + 4] != bytes(
            (0x78, 0x56, 0x34, 0x12)):
        failures.append(f"TC-PLAT-S9-0005: static bytes "
                        f"{data[:4].hex()} != 78563412")
    result = run_elf(elf, b"", directory)
    if result.stdout != b"305419896\n" or result.exit != 0:
        failures.append(f"TC-PLAT-S9-0005: static read {result.stdout!r} "
                        f"exit {result.exit}")


def check_relocatable(directory: Path, failures: list[str]) -> None:
    source = program("_ = writeln int32, 1")
    code, _out, stderr, obj = build(directory, source, emit="obj")
    if code != 0 or not obj.exists():
        failures.append(f"TC-PLAT-S9-0007: --emit=obj failed: {stderr.strip()}")
        return
    header = elf_header(obj)
    if header["type"] != ET_REL or header["machine"] != EM_AARCH64:
        failures.append(f"TC-PLAT-S9-0007: object is not AArch64 ET_REL")


def check_flat(directory: Path, failures: list[str]) -> None:
    source = program("_ = writeln int32, 1")
    root = write(directory, "root.tc", source)
    for profile in ("aarch64-baremetal", "arm32-baremetal"):
        output = directory / f"flat-{profile}.bin"
        proc = subprocess.run(
            [engine.tc_aot_path(), f"--profile={profile}",
             "--emit=flat", f"--output={output}", f"--include={directory}",
             f"--ld={engine.lld_path()}", str(root)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        if proc.returncode != 0 or not output.is_file() or                 output.stat().st_size == 0:
            failures.append(
                f"TC-PLAT-S9-0008: {profile} flat artifact failed: "
                f"{proc.stderr.decode('utf-8', 'replace').strip()}")
            continue
        if profile == "arm32-baremetal":
            data = output.read_bytes()
            if len(data) < 4 or struct.unpack("<I", data[:4])[0] != 0x00400000:
                failures.append(
                    "TC-PLAT-S9-0008: ARM32 flat image does not start with "
                    "the initial stack vector")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    directory = engine.temporary_directory("tc-s9-baremetal-")
    failures: list[str] = []
    try:
        check_elf_contract(directory / "contract", failures)
        check_trap(directory / "trap", failures)
        check_input(directory / "input", failures)
        check_device_policies(directory / "device", failures)
        check_static_initializer(directory / "static", failures)
        check_relocatable(directory / "obj", failures)
        check_flat(directory / "flat", failures)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        for failure in failures:
            print(failure)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: bare-metal ELF/UART/trap/input/static/device/object/"
          f"flat checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
