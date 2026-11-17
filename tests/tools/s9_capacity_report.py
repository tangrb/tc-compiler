#!/usr/bin/env python3
"""Measure the static-region and image capacity of the bare-metal profiles.

This is a development report, not a ctest gate.  It builds the 900000-byte
static-object fixture for both bare-metal targets and reports the emitted
static span, ELF section sizes, image end and remaining room.  The input image
window and runtime arena are fixed platform facts printed alongside the
measurement.

Run:
    python3 tests/tools/s9_capacity_report.py --check
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

CASE_ID = "TC-CAP-S9-0001"
STATIC_OBJECT = 900005

ROOT_TC = """#program
import lib
var s: lib.Big = make lib.Big, 3, 123456
_ = writeln int32, s.b
var z: usize = sizeof lib.Big
_ = writeln usize, z
"""

LIB_TC = """#lib
public struct Big then
var a: int8 @padding(900000)
var b: int32
end
"""


def nm_symbols(elf: Path) -> dict[str, int]:
    tool = shutil.which("llvm-nm") or "nm"
    proc = subprocess.run([tool, "--defined-only", str(elf)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)
    values: dict[str, int] = {}
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                values[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return values


def section_sizes(elf: Path) -> dict[str, int]:
    data = elf.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        return {}
    is64 = data[4] == 2
    endian = "<" if data[5] == 1 else ">"
    if is64:
        shoff = struct.unpack_from(endian + "Q", data, 0x28)[0]
        shentsize = struct.unpack_from(endian + "H", data, 0x3A)[0]
        shnum = struct.unpack_from(endian + "H", data, 0x3C)[0]
        shstrndx = struct.unpack_from(endian + "H", data, 0x3E)[0]
        header = endian + "IIQQQQIIQQ"
    else:
        shoff = struct.unpack_from(endian + "I", data, 0x20)[0]
        shentsize = struct.unpack_from(endian + "H", data, 0x2E)[0]
        shnum = struct.unpack_from(endian + "H", data, 0x30)[0]
        shstrndx = struct.unpack_from(endian + "H", data, 0x32)[0]
        header = endian + "IIIIIIIIII"

    def section(index: int) -> tuple[int, int, int]:
        fields = struct.unpack_from(header, data, shoff + index * shentsize)
        name, _, _, _, offset, size = fields[:6]
        return name, offset, size

    _, string_offset, string_size = section(shstrndx)
    strings = data[string_offset:string_offset + string_size]
    sizes: dict[str, int] = {}
    for index in range(shnum):
        name, _, size = section(index)
        end = strings.find(b"\0", name)
        if end < 0:
            continue
        label = strings[name:end].decode("ascii", "replace")
        if label in (".text", ".data", ".bss"):
            sizes[label] = size
    return sizes


def build(profile: str, directory: Path) -> Path:
    (directory / "root.tc").write_text(ROOT_TC, encoding="utf-8")
    (directory / "lib.tc").write_text(LIB_TC, encoding="utf-8")
    elf = directory / "case.elf"
    proc = subprocess.run(
        [engine.tc_aot_path(), "--emit=elf", f"--profile={profile}",
         f"--include={directory}", f"--output={elf}",
         str(directory / "root.tc")],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace"))
    return elf


def report(profile: str) -> dict[str, int | str]:
    directory = Path(tempfile.mkdtemp(prefix=f"tc-capacity-{profile}-"))
    try:
        elf = build(profile, directory)
        symbols = nm_symbols(elf)
        sections = section_sizes(elf)
        static_start = symbols["__tc_static_region"]
        static_end = symbols["__tc_static_region_end"]
        image_end = symbols.get("__tc_image_end")
        if image_end is None:
            return {
                "profile": profile,
                "static_start": static_start,
                "static_end": static_end,
                "static_span": static_end - static_start,
                "text": sections.get(".text", 0),
                "data": sections.get(".data", 0),
                "bss": sections.get(".bss", 0),
                "elf_bytes": elf.stat().st_size,
            }
        return {
            "profile": profile,
            "static_start": static_start,
            "static_end": static_end,
            "static_span": static_end - static_start,
            "image_end": image_end,
            "text": sections.get(".text", 0),
            "data": sections.get(".data", 0),
            "bss": sections.get(".bss", 0),
            "elf_bytes": elf.stat().st_size,
        }
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    rows = [report("aarch64-baremetal"), report("arm32-baremetal")]
    failures = 0
    print(f"{CASE_ID}: 900005-byte static object capacity")
    for row in rows:
        span = int(row["static_span"])
        print(f"  {row['profile']}: static_span={span} "
              f"text={row['text']} data={row['data']} "
              f"bss={row['bss']} elf={row['elf_bytes']}")
        if "image_end" in row:
            print(f"    image_end=0x{int(row['image_end']):x}")
        if span < STATIC_OBJECT:
            failures += 1
            print(f"  {row['profile']}: static span is below the object size")
        if span > 1024 * 1024:
            failures += 1
            print(f"  {row['profile']}: static span exceeds the 1 MiB profile")
    print("  input image window: 1 MiB; token limit: 16 KiB; "
          "runtime arena: AArch64 8 MiB / ARM32 128 KiB")
    if failures:
        print(f"{CASE_ID}: {failures} capacity failure(s)")
        return 1
    print(f"{CASE_ID}: measured capacity is within the documented profiles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
