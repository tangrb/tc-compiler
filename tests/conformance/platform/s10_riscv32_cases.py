#!/usr/bin/env python3
"""S10 RISC-V 32 target admission gate (design §10/§11/§14).

The new target `riscv32-baremetal` runs the same reference VM and freestanding
AOT image under one profile.  This suite checks:

  - the RISC-V ELF/object contract (ELF32, EM_RISCV, entry, no dynamic
    dependencies, no libc imports, profile region symbols);
  - the curated W=32 semantics/ABI cases and the full S6/S8 corpus under the
    new profile (VM vs AOT on qemu-system-riscv32 virt);
  - the four device policies;
  - checked-build UB detection without changing mandatory traps;
  - UART stdin framing and the short-read path;
  - flat-image emission.

Run:
    python3 tests/conformance/platform/s10_riscv32_cases.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import struct
import subprocess
import sys
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

PROFILE = "riscv32-baremetal"
CASE_ID = "TC-RISCV32-S10-0001"

EM_RISCV = 243
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


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


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


def elf_header(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise RuntimeError("not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise RuntimeError("not a 32-bit little-endian ELF")
    e_type, e_machine, _version, e_entry = struct.unpack_from("<HHII", data,
                                                              16)
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
    header_offset = struct.unpack_from("<I", data, 32)[0]
    shentsize = struct.unpack_from("<H", data, 46)[0]
    shnum = struct.unpack_from("<H", data, 48)[0]
    shstrndx = struct.unpack_from("<H", data, 50)[0]
    if shnum == 0 or shstrndx >= shnum:
        raise RuntimeError("extended section numbering is not supported")

    def section_at(index: int) -> tuple[int, int, int, int, int]:
        base = header_offset + index * shentsize
        name, kind, _flags, addr, offset, length = struct.unpack_from(
            "<IIIIII", data, base)
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


def read_u64_at(path: Path, address: int) -> int:
    data = path.read_bytes()
    phoff = struct.unpack_from("<I", data, 28)[0]
    phentsize = struct.unpack_from("<H", data, 42)[0]
    phnum = struct.unpack_from("<H", data, 44)[0]
    for index in range(phnum):
        base = phoff + index * phentsize
        _type, offset, vaddr, _paddr, filesz, _memsz, _flags, _align = \
            struct.unpack_from("<IIIIIIII", data, base)
        if vaddr <= address < vaddr + filesz:
            file_offset = offset + (address - vaddr)
            return struct.unpack_from("<Q", data, file_offset)[0]
    raise RuntimeError(f"address 0x{address:x} is not in a file segment")


def load_arm32_suite():
    path = ROOT / "tests/conformance/platform/s9_arm32_cases.py"
    spec = importlib.util.spec_from_file_location("s10_arm32_cases", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def check_elf_contract(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s10-riscv-elf-")
    try:
        source = program(
            "import lib",
            "_ = writeln int32, 42",
            "_ = writeln float64, 3.75",
            "var x: int64 = mul int64, 3037000499, 3037000499",
            "_ = writeln int64, x",
        )
        write(directory, "lib.tc",
              library("public static var V: int32 = 5"))
        code, _out, stderr, elf = build(directory, source)
        if code != 0 or not elf.exists():
            failures.append(f"{CASE_ID}: RISC-V ELF build failed ({code}): "
                            f"{stderr.strip()}")
            return
        header = elf_header(elf)
        if header["type"] != ET_EXEC or header["machine"] != EM_RISCV:
            failures.append(f"{CASE_ID}: not a RISC-V ET_EXEC ELF")
        if header["entry"] == 0:
            failures.append(f"{CASE_ID}: ELF entry is zero")
        if not os.access(elf, os.X_OK):
            failures.append(f"{CASE_ID}: ELF is not executable")
        table = symbols(elf)
        if table.get("_start") != header["entry"]:
            failures.append(f"{CASE_ID}: ELF entry is not _start")
        if table.get("__tc_code_start") != 0x80000000:
            failures.append(f"{CASE_ID}: __tc_code_start is not 0x80000000")
        for name, expected in (
            ("__tc_static_region", 0x81000000),
            ("__tc_stack_bottom", 0x87800000),
            ("__tc_stack_top", 0x88000000),
        ):
            if name not in table:
                failures.append(f"{CASE_ID}: missing symbol {name}")
            elif table[name] != expected:
                failures.append(
                    f"{CASE_ID}: {name} = 0x{table[name]:x}, expected "
                    f"0x{expected:x}")
        for name, expected in (
            ("__tc_rt_device_base", 0x10000000),
            ("__tc_rt_device_size", 0x10000000),
            ("__tc_rt_device_policy", 3),  # TC_DEV_VOLATILE
            ("__tc_rt_checked", 0),
            ("__tc_rt_normal_status", 0),
            ("__tc_rt_trap_status", 1),
        ):
            if name not in table:
                failures.append(f"{CASE_ID}: missing symbol {name}")
                continue
            value = read_u64_at(elf, table[name])
            if value != expected:
                failures.append(
                    f"{CASE_ID}: {name} value = 0x{value:x}, expected "
                    f"0x{expected:x}")
        if undefined_symbols(elf):
            failures.append(
                f"{CASE_ID}: undefined symbols: {undefined_symbols(elf)[:8]}")
        info = dynamic_sections(elf)
        if "INTERP" in info or "NEEDED" in info or "DYNAMIC" in info:
            failures.append(f"{CASE_ID}: dynamic dependency present")
        data = section_bytes(elf, ".data")
        if b"\x05\x00\x00\x00" not in data:
            failures.append(f"{CASE_ID}: static initializer bytes missing")
        code, _out, stderr, obj = build(directory, source, emit="obj")
        if code != 0 or not obj.exists():
            failures.append(f"{CASE_ID}: --emit=obj failed ({code}): "
                            f"{stderr.strip()}")
        else:
            obj_header = elf_header(obj)
            if obj_header["type"] != ET_REL or obj_header["machine"] != EM_RISCV:
                failures.append(f"{CASE_ID}: --emit=obj is not a RISC-V "
                                "relocatable object")
        code, _out, stderr, flat = build(directory, source, emit="flat")
        if code != 0 or not flat.exists() or flat.stat().st_size == 0:
            failures.append(f"{CASE_ID}: --emit=flat failed ({code}): "
                            f"{stderr.strip()}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


POLICY_MATRIX = [
    ("reject", "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE",
     "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE"),
    ("read", None, "TC_RE_ADDR_UNAVAILABLE", None,
     "TC_RE_ADDR_UNAVAILABLE"),
    ("readwrite", None, None, None, None),
    ("volatile", "TC_RE_ADDR_UNAVAILABLE", "TC_RE_ADDR_UNAVAILABLE", None,
     None),
]

DEVICE_LOAD = program(
    "var a: usize = copy usize, 0x10000005",
    "unsafe then",
    "var p: ptr<uint8> = bitcast ptr<uint8>, a",
    "var v: uint8 = load uint8, p",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_LOAD_VOLATILE = program(
    "var a: usize = copy usize, 0x10000005",
    "unsafe then",
    "var p: ptr<uint8> = bitcast ptr<uint8>, a",
    "var v: uint8 = load.volatile uint8, p",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_STORE = program(
    "var a: usize = copy usize, 0x10000000",
    "unsafe then",
    "var p: ptr<uint8> = bitcast ptr<uint8>, a",
    "_ = store uint8, p, 0",
    "end",
    "_ = writeln int32, 1",
)
DEVICE_STORE_VOLATILE = program(
    "var a: usize = copy usize, 0x10000000",
    "unsafe then",
    "var p: ptr<uint8> = bitcast ptr<uint8>, a",
    "_ = store.volatile uint8, p, 0",
    "end",
    "_ = writeln int32, 1",
)


def check_device_policies(failures: list[str]) -> None:
    sources = (("normal load", DEVICE_LOAD), ("normal store", DEVICE_STORE),
               ("volatile load", DEVICE_LOAD_VOLATILE),
               ("volatile store", DEVICE_STORE_VOLATILE))
    for policy, *expected in POLICY_MATRIX:
        for (label, source), want in zip(sources, expected):
            directory = engine.temporary_directory("tc-s10-riscv-dev-")
            try:
                code, _out, stderr, elf = build(
                    directory, source, f"--device={policy}")
                if code != 0:
                    failures.append(
                        f"{CASE_ID}: device {policy}/{label} build failed "
                        f"({code}): {stderr.strip()}")
                    continue
                observed = engine.run_riscv32_elf(elf, b"", directory)
                if observed.trap != want:
                    failures.append(
                        f"{CASE_ID}: device {policy}/{label}: trap "
                        f"{observed.trap} != {want}")
            finally:
                shutil.rmtree(directory, ignore_errors=True)


def check_checked_build(failures: list[str]) -> None:
    # A read of uninitialized frame storage is UB: a checked build detects it,
    # while the release build keeps the program's defined behavior unchanged.
    uninitialized = program(
        "var flag: bool = read bool",
        "_ = br_if flag, Ldecl",
        "var y: int32 = copy int32, 5",
        "label Ldecl:",
        "var p: ptr<int32> = addr int32, y",
        "unsafe then",
        "var v: int32 = load int32, p",
        "_ = writeln int32, v",
        "end",
    )
    for checked, want_trap in ((False, None),
                               (True, "TC_RE_ADDR_UNAVAILABLE")):
        directory = engine.temporary_directory("tc-s10-riscv-chk-")
        try:
            extra = ("--checked",) if checked else ()
            code, _out, stderr, elf = build(directory, uninitialized, *extra)
            if code != 0:
                failures.append(
                    f"{CASE_ID}: checked={checked} build failed ({code}): "
                    f"{stderr.strip()}")
                continue
            observed = engine.run_riscv32_elf(elf, b"true\n", directory)
            if observed.trap != want_trap:
                failures.append(
                    f"{CASE_ID}: checked={checked} uninitialized trap "
                    f"{observed.trap} != {want_trap}")
        finally:
            shutil.rmtree(directory, ignore_errors=True)
    # A mandatory trap must execute in both builds.
    null_case = program(
        "var p: ptr<int32> = copy ptr<int32>, nullptr",
        "unsafe then",
        "var v: int32 = load int32, p",
        "end",
        "_ = writeln int32, 1",
    )
    for checked in (False, True):
        directory = engine.temporary_directory("tc-s10-riscv-null-")
        try:
            extra = ("--checked",) if checked else ()
            code, _out, stderr, elf = build(directory, null_case, *extra)
            if code != 0:
                failures.append(
                    f"{CASE_ID}: mandatory-trap build failed ({code}): "
                    f"{stderr.strip()}")
                continue
            observed = engine.run_riscv32_elf(elf, b"", directory)
            if observed.trap != "TC_RE_NULL_POINTER":
                failures.append(
                    f"{CASE_ID}: checked={checked} null trap "
                    f"{observed.trap}")
        finally:
            shutil.rmtree(directory, ignore_errors=True)


def check_stdin(failures: list[str]) -> None:
    source = program(
        "var x: int64 = read int64",
        "_ = writeln int64, x",
    )
    directory = engine.temporary_directory("tc-s10-riscv-stdin-")
    try:
        code, _out, stderr, elf = build(directory, source)
        if code != 0:
            failures.append(f"{CASE_ID}: stdin build failed ({code}): "
                            f"{stderr.strip()}")
            return
        observed = engine.run_riscv32_elf(elf, b"0" * 4000 + b"42\n",
                                          directory)
        if observed.stdout != b"42\n" or observed.trap is not None:
            failures.append(f"{CASE_ID}: UART stdin output {observed.stdout!r}"
                            f"/{observed.trap}, expected b'42\\n'")
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    arm32 = load_arm32_suite()
    arm32.CASE_ID = CASE_ID
    cases = [
        replace(item, case_id=item.case_id.replace("TC-ARM32-S9-",
                                                   "TC-RISCV32-S10-1"))
        for item in arm32.CASES
    ]
    check_elf_contract(failures)
    arm32.run_curated(failures, PROFILE, cases)
    arm32.run_corpus(failures, PROFILE)
    check_device_policies(failures)
    check_checked_build(failures)
    check_stdin(failures)
    if failures:
        for failure in failures:
            print(failure)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: RISC-V 32 target admission gate passed "
          f"({len(cases)} curated + S6/S8 corpus)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
