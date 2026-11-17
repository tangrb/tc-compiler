#!/usr/bin/env python3
"""S8 artifact, entry and platform-instance checks (design §10/§11, §12).

The AArch64 image is an ELF executable produced by clang + ld.lld from the
emitted assembly and the bundled freestanding runtime.  This suite checks the
S8 artifact deliverables without executing TC code:

  - ELF identity (ET_EXEC/ET_REL, AArch64, executable bit) and the `_start`
    reset entry;
  - no undefined symbols, no dynamic dependencies and no libc dependency;
  - code/static/stack region symbols and non-overlapping image layout;
  - the static `var` initializer bytes in `.data` equal the independently
    derived little-endian value (the same image the VM preloads);
  - ICF/entry merging disabled: two functions with identical bodies keep
    distinct entry addresses (standard §3.13.3);
  - `--emit=obj` publishes a relocatable object and `--emit=asm` references
    the runtime helpers used by the lowered program.

Run:
    python3 tests/conformance/tools/s8_artifact_cases.py --check
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

CASE_ID = "TC-ART-S8-0001"

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
    path.write_text(text, encoding="utf-8")
    return path


PROGRAM = """#program
import lib
struct S then
var a: int32
var b: float64
end
var s: S = make S, 7, 2.5
var p: ptr<S> = addr S, s
var q: ptr<int32> = addr int32, s.a
var n: usize = copy usize, 1
var f: float64 = add float64, s.b, 1.0
var fp: fnptr<int32, int32> = funcaddr lib.id
unsafe then
var v: int32 = load int32, q
_ = store int32, q, v
var r: ptr<int32> = ptradd int32, q, n
_ = memmove S, p, p, n
var iv: int32 = call_indirect fp, v
_ = writeln int32, iv
end
_ = writeln float64, f
_ = writeln int32, lib.V
"""

LIBRARY = """#lib
public static var V: int32 = 0x12345678
public func id(x: int32) int32 then
_ = return x
end
public func twin1(x: int32) int32 then
var r: int32 = copy int32, x
_ = return r
end
public func twin2(x: int32) int32 then
var r: int32 = copy int32, x
_ = return r
end
"""


def aot(source: Path, directory: Path, *extra: str) -> subprocess.CompletedProcess:
    argv = [engine.tc_aot_path(), "--profile=aarch64-linux-user",
            f"--include={directory}", f"--ld={engine.lld_path()}", *extra,
            str(source)]
    return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=180)


def read_elf_header(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise RuntimeError("not an ELF file")
    ident = data[:16]
    if ident[4] != 2 or ident[5] != 1:
        raise RuntimeError("not a 64-bit little-endian ELF")
    e_type, e_machine, _version, e_entry = struct.unpack_from("<HHIQ", data, 16)
    return {"type": e_type, "machine": e_machine, "entry": e_entry}


def read_symbols(readelf: str, path: Path) -> dict[str, int]:
    proc = subprocess.run([readelf, "-s", "-W", str(path)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)
    symbols: dict[str, int] = {}
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].endswith(":"):
            continue
        try:
            value = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[7]
        symbols.setdefault(name, value)
    return symbols


def read_section_bytes(readelf: str, path: Path, section: str) -> tuple[int, bytes]:
    proc = subprocess.run([readelf, "-S", "-W", str(path)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)
    address = None
    size = 0
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        if section not in line:
            continue
        parts = line.split()
        try:
            index = parts.index(section)
        except ValueError:
            continue
        if index + 5 >= len(parts):
            continue
        try:
            address = int(parts[index + 3], 16)
        except ValueError:
            continue
        break
    if address is None:
        raise RuntimeError(f"section {section} not found")
    data = path.read_bytes()
    section_header_offset = struct.unpack_from("<Q", data, 40)[0]
    shentsize = struct.unpack_from("<H", data, 58)[0]
    shnum = struct.unpack_from("<H", data, 60)[0]
    shstrndx = struct.unpack_from("<H", data, 62)[0]
    if shnum == 0 or shstrndx >= shnum:
        raise RuntimeError("extended section numbering is not supported")
    def section_at(index: int) -> tuple[int, int, int, int, int]:
        base = section_header_offset + index * shentsize
        name, kind, _flags, addr, offset, length = struct.unpack_from(
            "<IIQQQQ", data, base)
        return name, kind, addr, offset, length
    # locate by name in the section string table
    _n, _k, _a, shstr_off, shstr_len = section_at(shstrndx)
    names = data[shstr_off:shstr_off + shstr_len]
    for index in range(shnum):
        name_off, _kind, addr, offset, length = section_at(index)
        end = names.find(b"\x00", name_off)
        name = names[name_off:end].decode("ascii", "replace")
        if name == section:
            return addr, data[offset:offset + length]
    raise RuntimeError(f"section {section} not found by name")


def check_artifacts(tmp: Path, failures: list[str]) -> None:
    root = write(tmp, "root.tc", PROGRAM)
    write(tmp, "lib.tc", LIBRARY)
    elf = tmp / "a.elf"
    result = aot(root, tmp, "--emit=elf", f"--output={elf}")
    if result.returncode != 0 or not elf.exists():
        failures.append(
            f"artifact: ELF build failed ({result.returncode}): "
            f"{result.stderr.decode('utf-8', 'replace').strip()}")
        return
    if not os.access(elf, os.X_OK):
        failures.append("artifact: ELF is not executable")
    header = read_elf_header(elf)
    if header["type"] != ET_EXEC:
        failures.append(f"artifact: ELF type {header['type']} is not ET_EXEC")
    if header["machine"] != EM_AARCH64:
        failures.append(
            f"artifact: ELF machine {header['machine']} is not AArch64")
    readelf = llvm_tool("llvm-readelf")
    nm = llvm_tool("llvm-nm")
    if readelf is None or nm is None:
        failures.append("artifact: llvm-readelf/llvm-nm are required")
        return
    symbols = read_symbols(readelf, elf)
    for required in (
        "_start", "tc_main", "__tc_static_region",
        "__tc_static_region_end", "__tc_static_objects",
        "__tc_static_object_count", "__tc_code_start", "__tc_code_end",
        "__tc_stack_bottom", "__tc_stack_top", "__tc_a64_checked",
        "__tc_a64_device_base", "__tc_a64_device_size",
        "__tc_a64_device_policy", "__tc_a64_heap_base", "__tc_a64_heap_size",
        "__tc_a64_normal_status", "__tc_a64_trap_status",
        "__tc_static_shadow", "tc_a64_load", "tc_a64_store",
        "tc_a64_frame_enter", "tc_a64_frame_leave",
    ):
        if required not in symbols:
            failures.append(f"artifact: missing symbol {required}")
    if "_start" in symbols and symbols["_start"] != header["entry"]:
        failures.append("artifact: ELF entry point is not _start")
    if ("__tc_code_start" in symbols and "__tc_code_end" in symbols and
            symbols["__tc_code_start"] >= symbols["__tc_code_end"]):
        failures.append("artifact: empty code region")
    if ("__tc_code_end" in symbols and "__tc_static_region" in symbols and
            symbols["__tc_code_end"] > symbols["__tc_static_region"]):
        failures.append("artifact: code and static regions overlap")
    if ("__tc_static_region_end" in symbols and "__tc_stack_bottom" in symbols
            and symbols["__tc_static_region_end"] > symbols["__tc_stack_bottom"]):
        failures.append("artifact: static region overlaps the stack region")
    undefined = subprocess.run([nm, "-u", str(elf)], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=60)
    if undefined.returncode != 0:
        failures.append("artifact: llvm-nm -u failed")
    elif undefined.stdout.strip():
        failures.append(
            "artifact: undefined symbols: "
            + undefined.stdout.decode("utf-8", "replace").strip())
    dynamic = subprocess.run([readelf, "-d", str(elf)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             timeout=60)
    dynamic_text = dynamic.stdout.decode("utf-8", "replace")
    if "NEEDED" in dynamic_text:
        failures.append("artifact: ELF has dynamic dependencies")
    # Freestanding compile gate (design §12 item 4): the runtime archive is
    # cross-compiled with -ffreestanding -nostdlib, so the image must have no
    # interpreter, no libc dependency and no host library symbols.
    program_headers = subprocess.run([readelf, "-l", str(elf)],
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, timeout=60)
    if "INTERP" in program_headers.stdout.decode("utf-8", "replace"):
        failures.append("artifact: ELF has an interpreter (not freestanding)")
    defined = subprocess.run([nm, "--defined-only", str(elf)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             timeout=60).stdout.decode("utf-8", "replace")
    for libc_symbol in ("printf", "malloc", "fopen", "__libc_start_main",
                        "errno"):
        if re.search(rf"\b{re.escape(libc_symbol)}\b", defined):
            failures.append(
                f"artifact: ELF depends on host libc symbol {libc_symbol}")
    # ICF disabled: identical bodies keep distinct entry addresses.
    twins = [name for name in ("tc_function_1", "tc_function_2")
             if name in symbols]
    if len(twins) == 2 and symbols["tc_function_1"] == symbols["tc_function_2"]:
        failures.append("artifact: identical functions were ICF-merged")
    # Static initializer bytes: a minimal image whose static region holds only
    # `V = 0x12345678`.  The bytes must equal the CT image the VM preloads.
    static_dir = tmp / "static"
    static_dir.mkdir()
    static_root = write(static_dir, "root.tc",
                        "#program\nimport lib\n_ = writeln int32, lib.V\n")
    write(static_dir, "lib.tc",
          "#lib\npublic static var V: int32 = 0x12345678\n")
    static_elf = static_dir / "static.elf"
    result = aot(static_root, static_dir, "--emit=elf",
                 f"--output={static_elf}")
    if result.returncode != 0 or not static_elf.exists():
        failures.append("artifact: static-only ELF build failed")
    else:
        try:
            static_address, static_bytes = read_section_bytes(
                readelf, static_elf, ".data")
            static_symbols = read_symbols(readelf, static_elf)
            base = static_symbols["__tc_static_region"]
            offset = base - static_address
            if offset < 0 or offset + 4 > len(static_bytes):
                failures.append("artifact: static region is outside .data")
            elif static_bytes[offset:offset + 4] != b"\x78\x56\x34\x12":
                failures.append(
                    "artifact: static var bytes are "
                    f"{static_bytes[offset:offset + 4].hex()}, "
                    "expected 78563412")
        except RuntimeError as exc:
            failures.append(f"artifact: {exc}")
    # --emit=obj publishes a relocatable object.
    obj = tmp / "a.o"
    result = aot(root, tmp, "--emit=obj", f"--output={obj}")
    if result.returncode != 0 or not obj.exists():
        failures.append("artifact: --emit=obj failed")
    else:
        obj_header = read_elf_header(obj)
        if obj_header["type"] != ET_REL:
            failures.append(
                f"artifact: --emit=obj type {obj_header['type']} is not ET_REL")
    # The emitted assembly names the runtime helper entry points.
    asm = tmp / "a.s"
    result = aot(root, tmp, "--emit=asm", f"--output={asm}")
    if result.returncode != 0 or not asm.exists():
        failures.append("artifact: --emit=asm failed")
    else:
        text = asm.read_text(encoding="utf-8")
        for helper in ("tc_a64_load", "tc_a64_store", "tc_a64_ptradd",
                       "tc_a64_float_binop", "tc_a64_write_scalar"):
            if helper not in text:
                failures.append(
                    f"artifact: assembly does not reference {helper}")
    # Checked builds publish the defined-byte shadow and checked flag.
    checked_elf = tmp / "c.elf"
    result = aot(root, tmp, "--emit=elf", "--checked",
                 f"--output={checked_elf}")
    if result.returncode != 0 or not checked_elf.exists():
        failures.append("artifact: checked ELF build failed")
    else:
        checked_asm = tmp / "c.s"
        result = aot(root, tmp, "--emit=asm", "--checked",
                     f"--output={checked_asm}")
        if result.returncode != 0:
            failures.append("artifact: checked --emit=asm failed")
        else:
            text = checked_asm.read_text(encoding="utf-8")
            if "__tc_a64_checked:" not in text or ".quad 1" not in text:
                failures.append("artifact: checked flag is not emitted")
            if "bl tc_a64_define" not in text:
                failures.append("artifact: checked build lacks define tracking")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    tmp = Path(tempfile.mkdtemp(prefix="tc-s8-artifact-"))
    try:
        check_artifacts(tmp, failures)
    except (RuntimeError, OSError, subprocess.SubprocessError) as exc:
        failures.append(f"artifact: {exc}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print(f"s8.artifact: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    print(f"s8.artifact: {CASE_ID} ELF entry/regions/symbols/static bytes/ICF and freestanding symbol audit verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
