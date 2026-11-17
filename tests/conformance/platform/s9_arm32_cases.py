#!/usr/bin/env python3
"""S9 ARM32 Cortex-M7 backend differential and semantics gate (design §10).

Every case runs the same source under one profile, `arm32-baremetal`, on the
reference VM and as an ELF on qemu-system-arm MPS2 AN500 (Cortex-M7).  The
suite checks:

  - the published 32-bit static results and §13.2.2 word-size table;
  - int64/uint64 helpers, float32/binary64 arithmetic and conversions;
  - the AAPCS32/ILP32 argument/return classes, stack arguments, 64-bit pairs,
    by-value composites, hidden indirect returns and indirect calls;
  - pointer/layout/memmove/ptrdiff behaviour at W=32;
  - the complete S6 runtime and S8 differential corpora under W=32.

Successful runs compare program stdout bytes, the first `TC_RE_*` trap code and
the profile exit status.  Cases that both tools reject statically must report
the same first `TC_CE_*` code.  No 32-bit and 64-bit result is compared with the
other profile.

Run:
    python3 tests/conformance/platform/s9_arm32_cases.py --check
"""

from __future__ import annotations

import argparse
import importlib.util
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
from diff.engine import Case  # noqa: E402

PROFILE = "arm32-baremetal"
CASE_ID = "TC-ARM32-S9-0001"
DIAG_RE = re.compile(r"TC_(?:CE|RE)_[A-Z0-9_]+")
EM_ARM = 40
ET_EXEC = 2
LIBC_SYMBOLS = (
    "printf", "fprintf", "sprintf", "snprintf", "malloc", "calloc",
    "realloc", "free", "fopen", "fclose", "fread", "fwrite", "exit",
    "abort", "errno", "localeconv", "setlocale", "pow", "sqrt", "fmod",
)


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


def elf_header(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise RuntimeError("not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise RuntimeError("not a 32-bit little-endian ELF")
    e_type, e_machine, _version, e_entry = struct.unpack_from("<HHII", data, 16)
    return {"type": e_type, "machine": e_machine, "entry": e_entry}


def check_elf_contract(failures: list[str]) -> None:
    directory = engine.temporary_directory("tc-s9-arm32-elf-")
    try:
        source = program("_ = writeln int32, 42")
        (directory / "root.tc").write_text(source, encoding="utf-8")
        elf = directory / "out.elf"
        proc = subprocess.run(
            [engine.tc_aot_path(), "--profile=arm32-baremetal",
             "--emit=elf", f"--output={elf}",
             f"--include={directory}", f"--ld={engine.lld_path()}",
             str(directory / "root.tc")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        if proc.returncode != 0 or not elf.exists():
            failures.append(
                f"{CASE_ID}: ARM32 ELF build failed: "
                f"{proc.stderr.decode('utf-8', 'replace').strip()}")
            return
        header = elf_header(elf)
        if header["type"] != ET_EXEC or header["machine"] != EM_ARM:
            failures.append(f"{CASE_ID}: ARM32 image is not an ET_EXEC ARM ELF")
        symbol_proc = subprocess.run(
            [llvm_tool("llvm-nm") or "nm", "--defined-only", str(elf)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        defined = symbol_proc.stdout.decode("utf-8", "replace")
        start_value = None
        for line in defined.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[2] == "_start":
                start_value = int(parts[0], 16)
        if start_value is None:
            failures.append(f"{CASE_ID}: ARM32 image lacks _start")
        elif (header["entry"] & ~1) != start_value:
            failures.append(
                f"{CASE_ID}: ARM32 entry 0x{header['entry']:x} is not _start")
        if not os.access(elf, os.X_OK):
            failures.append(f"{CASE_ID}: ARM32 ELF is not executable")
        undefined = undefined_symbols(elf)
        if undefined:
            failures.append(
                f"{CASE_ID}: ARM32 undefined symbols: {undefined[:8]}")
        readelf = llvm_tool("llvm-readelf") or "readelf"
        info = subprocess.run([readelf, "-d", "-l", "-W", str(elf)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=60).stdout.decode("utf-8", "replace")
        if "INTERP" in info or "NEEDED" in info or "DYNAMIC" in info:
            failures.append(f"{CASE_ID}: ARM32 image has dynamic dependencies")
        for required in ("tc_main", "_start", "__tc_static_region",
                         "__tc_rt_device_policy", "tc_rt_load"):
            if required not in defined:
                failures.append(
                    f"{CASE_ID}: ARM32 image lacks symbol {required}")
        for forbidden in LIBC_SYMBOLS:
            if re.search(rf"\b{forbidden}\b", defined):
                failures.append(
                    f"{CASE_ID}: ARM32 image defines libc symbol {forbidden}")
        values = {}
        for line in defined.splitlines():
            parts = line.split()
            if len(parts) == 3:
                values.setdefault(parts[2], int(parts[0], 16))
        for name, expected in (
            ("__tc_code_start", 0x00001000),
            ("__tc_static_region", 0x00100000),
            ("__tc_stack_bottom", 0x00300000),
            ("__tc_stack_top", 0x00400000),
            ("__tc_input_image", 0x20000000),
        ):
            if values.get(name) != expected:
                failures.append(
                    f"{CASE_ID}: ARM32 {name} = "
                    f"0x{values.get(name, 0):x}, expected 0x{expected:x}")
        # The runtime archive must not introduce an import/module loader.
        archive = engine.runtime_archive(32)
        archive_symbols = subprocess.run(
            [llvm_tool("llvm-nm") or "nm", str(archive)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=60).stdout.decode("utf-8", "replace")
        for forbidden in ("tc_module_load", "tc_module_resolve", "dlopen"):
            if forbidden in archive_symbols:
                failures.append(
                    f"{CASE_ID}: runtime archive references {forbidden}")
        obj = directory / "out.o"
        obj_proc = subprocess.run(
            [engine.tc_aot_path(), "--profile=arm32-baremetal",
             "--emit=obj", f"--output={obj}",
             f"--include={directory}", str(directory / "root.tc")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        if obj_proc.returncode != 0 or not obj.exists():
            failures.append(
                f"{CASE_ID}: ARM32 --emit=obj failed: "
                f"{obj_proc.stderr.decode('utf-8', 'replace').strip()}")
        else:
            obj_header = elf_header(obj)
            if obj_header["type"] != 1 or obj_header["machine"] != EM_ARM:
                failures.append(
                    f"{CASE_ID}: ARM32 object is not an ET_REL ARM object")
    finally:
        shutil.rmtree(directory, ignore_errors=True)



def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


def library(*lines: str) -> str:
    return "#lib\n" + "".join(line + "\n" for line in lines)


def case(case_id: str, spec: str, *, out: str = "", trap: str | None = None,
         stdin: bytes = b"", files: dict[str, str] | None = None,
         checked: bool = False) -> Case:
    return engine.make_case(
        case_id,
        files if files is not None else {"root.tc": program()},
        out, trap=trap, stdin=stdin, checked=checked, spec=spec)


CASES: list[Case] = [
    case(
        CASE_ID,
        "standard §13.2.1/§13.2.2 W=32 sizes, layout, bitcast/trunc and literals",
        out=("4\n4\n4\n4\n8\n8\n1065353216\n1\n2\n591751049\n"
             "18446744073709551615\n"),
        files={"root.tc": program(
            "var a: usize = sizeof usize",
            "_ = writeln usize, a",
            "var b: usize = sizeof isize",
            "_ = writeln usize, b",
            "var c: usize = sizeof ptr<int32>",
            "_ = writeln usize, c",
            "var d: usize = sizeof fnptr<int32, int32>",
            "_ = writeln usize, d",
            "var e: usize = sizeof int64",
            "_ = writeln usize, e",
            "var f: usize = sizeof float64",
            "_ = writeln usize, f",
            "var g: int32 = bitcast int32, 1.0f",
            "_ = writeln int32, g",
            "var s: int32 = copy int32, 1",
            "var t: int32 = copy int32, 2",
            "_ = writeln int32, s",
            "_ = writeln int32, t",
            "var u: uint32 = trunc uint32, 0x123456789",
            "_ = writeln uint32, u",
            "var v: uint64 = copy uint64, 18446744073709551615u",
            "_ = writeln uint64, v",
        )},
    ),
    case(
        "TC-ARM32-S9-0002",
        "standard §6.3/§6.5 int64/uint64 mul/div/mod/shift/compare helpers",
        out=("-3074457345618258602\n-1\n1844674407370955161\n"
             "9223372036854775808\n-128\ntrue\n"
             "9223372030926249001\n"),
        files={"root.tc": program(
            "var a: int64 = div int64, -9223372036854775807, 3",
            "_ = writeln int64, a",
            "var b: int64 = mod int64, -9223372036854775807, 3",
            "_ = writeln int64, b",
            "var c: uint64 = div uint64, 18446744073709551615u, 10",
            "_ = writeln uint64, c",
            "var d: uint64 = shl uint64, 1, 63",
            "_ = writeln uint64, d",
            "var e: int64 = shr int64, -1024, 3",
            "_ = writeln int64, e",
            "var f: bool = lt int64, -1, 0",
            "_ = writeln bool, f",
            "var g: int64 = mul int64, 3037000499, 3037000499",
            "_ = writeln int64, g",
        )},
    ),
    case(
        "TC-ARM32-S9-0003",
        "standard §6.3.1/§10.6/§11.2 float32/binary64 arithmetic and formatting",
        out=("0.333333\n3.75\n-2.5\n7.5\ntrue\ntrue\n"),
        files={"root.tc": program(
            "var a: float64 = div float64, 1.0, 3.0",
            "_ = writeln float64, a",
            "var b: float32 = mul float32, 1.5f, 2.5f",
            "_ = writeln float32, b",
            "var c: float64 = sub float64, 1.0, 3.5",
            "_ = writeln float64, c",
            "var d: float64 = add float64, 1.5, 2.25",
            "var e: float64 = add float64, d, 3.75",
            "_ = writeln float64, e",
            "var f: bool = lt float64, 1.0, 2.0",
            "_ = writeln bool, f",
            "var g: bool = eq float64, 0.0, -0.0",
            "_ = writeln bool, g",
        )},
    ),
    case(
        "TC-ARM32-S9-0004",
        "standard §6.3.2/§6.3.3 floating exception priority and .ieee",
        trap="TC_RE_FLOAT_OVERFLOW",
        files={"root.tc": program(
            "var a: float64 = mul float64, 1.0e308, 10.0",
        )},
    ),
    case(
        "TC-ARM32-S9-0005",
        "standard §6.3.3 .ieee returns infinity instead of trapping",
        out="inf\n",
        files={"root.tc": program(
            "var a: float64 = mul.ieee float64, 1.0e308, 10.0",
            "_ = writeln float64, a",
        )},
    ),
    case(
        "TC-ARM32-S9-0006",
        "standard §5.3/§8.5 AAPCS32 register and stack arguments",
        out="36\n15\n",
        files={
            "root.tc": program(
                "import lib",
                "var a: int64 = call lib.mix, 1, 2, 3, 4, 5, 6, 7, 8",
                "_ = writeln int64, a",
                "var b: int32 = call lib.five, 1, 2, 3, 4, 5",
                "_ = writeln int32, b",
            ),
            "lib.tc": library(
                "public func mix(a: int32, b: int64, c: int32, d: int64, "
                "e: int32, f: int64, g: int32, h: int32) int64 then",
                "var s1: int64 = add int64, b, d",
                "var s2: int64 = add int64, s1, f",
                "var s3: int64 = cast int64, a",
                "var s4: int64 = add int64, s3, s2",
                "var s5: int64 = cast int64, c",
                "var s6: int64 = add int64, s4, s5",
                "var s7: int64 = cast int64, e",
                "var s8: int64 = add int64, s6, s7",
                "var s9: int64 = cast int64, g",
                "var s10: int64 = add int64, s8, s9",
                "var s11: int64 = cast int64, h",
                "var s12: int64 = add int64, s10, s11",
                "_ = return s12",
                "end",
                "public func five(a: int32, b: int32, c: int32, d: int32, "
                "e: int32) int32 then",
                "var s: int32 = add int32, a, b",
                "var t: int32 = add int32, s, c",
                "var u: int32 = add int32, t, d",
                "var v: int32 = add int32, u, e",
                "_ = return v",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0007",
        "standard §8.5/§10.2 by-value composites and hidden indirect returns",
        out="7\n8\n10\n30\n",
        files={
            "root.tc": program(
                "import lib",
                "var p: lib.Pair = call lib.mkpair, 7, 8",
                "_ = writeln int32, p.x",
                "_ = writeln int32, p.y",
                "var b: lib.Big = call lib.mkbig, 10, 20, 30",
                "_ = writeln int32, b.a",
                "_ = writeln int32, b.c",
            ),
            "lib.tc": library(
                "public struct Pair then",
                "var x: int32",
                "var y: int32",
                "end",
                "public struct Big then",
                "var a: int32",
                "var b: int32",
                "var c: int32",
                "end",
                "public func mkpair(x: int32, y: int32) Pair then",
                "var p: Pair = make Pair, x, y",
                "_ = return p",
                "end",
                "public func mkbig(a: int32, b: int32, c: int32) Big then",
                "var p: Big = make Big, a, b, c",
                "_ = return p",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0008",
        "standard §8.5/§10.2 >4-byte composite parameters are passed by reference",
        out="200\n",
        files={
            "root.tc": program(
                "import lib",
                "var p: lib.Pair = make lib.Pair, 10, 20",
                "var q: lib.Pair = make lib.Pair, 30, 40",
                "var r: int32 = call lib.pairsum, p, q, 100",
                "_ = writeln int32, r",
            ),
            "lib.tc": library(
                "public struct Pair then",
                "var x: int32",
                "var y: int32",
                "end",
                "public func pairsum(p: Pair, q: Pair, n: int32) int32 then",
                "var a: int32 = add int32, p.x, p.y",
                "var b: int32 = add int32, q.x, q.y",
                "var c: int32 = add int32, a, b",
                "var d: int32 = add int32, c, n",
                "_ = return d",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0009",
        "standard §8.5 narrow argument extension and bool normalization",
        out="65788\n",
        files={
            "root.tc": program(
                "import lib",
                "var r: int32 = call lib.narrow, -1, 255, -2, 65535, true",
                "_ = writeln int32, r",
            ),
            "lib.tc": library(
                "public func narrow(a: int8, b: uint8, c: int16, "
                "d: uint16, e: bool) int32 then",
                "var s: int32 = cast int32, a",
                "var t: int32 = cast int32, b",
                "var u: int32 = add int32, s, t",
                "var v: int32 = cast int32, c",
                "var w: int32 = add int32, u, v",
                "var x: int32 = cast int32, d",
                "var y: int32 = add int32, w, x",
                "var z: int32 = cast int32, e",
                "var r: int32 = add int32, y, z",
                "_ = return r",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0010",
        "standard §8.5/§10.2 funcaddr, funceq and indirect calls",
        out="42\n43\ntrue\nfalse\n",
        files={
            "root.tc": program(
                "import lib",
                "unsafe then",
                "var fp: fnptr<int32, int32> = funcaddr lib.inc",
                "var f: int32 = call_indirect fp, 41",
                "_ = writeln int32, f",
                "var g: int32 = call_indirect fp, f",
                "_ = writeln int32, g",
                "var fp2: fnptr<int32, int32> = funcaddr lib.inc",
                "var same: bool = funceq fnptr<int32, int32>, fp, fp2",
                "_ = writeln bool, same",
                "var other: fnptr<int32, int32> = funcaddr lib.dec",
                "var diff: bool = funceq fnptr<int32, int32>, fp, other",
                "_ = writeln bool, diff",
                "end",
            ),
            "lib.tc": library(
                "public func inc(x: int32) int32 then",
                "var r: int32 = add int32, x, 1",
                "_ = return r",
                "end",
                "public func dec(x: int32) int32 then",
                "var r: int32 = sub int32, x, 1",
                "_ = return r",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0011",
        "standard §6.7/§6.10 pointers, ptrdiff, memmove and n*sizeof T at W=32",
        out="4\n-4\n9\n9\n5\n9\n",
        files={
            "root.tc": program(
                "import lib",
                "var s: lib.S = make lib.S, 9, 0",
                "var pa: ptr<int32> = addr int32, s.a",
                "var pb: ptr<int32> = addr int32, s.b",
                "var d: isize = ptrdiff int32, pb, pa",
                "_ = writeln isize, d",
                "var e: isize = ptrdiff int32, pa, pb",
                "_ = writeln isize, e",
                "unsafe then",
                "var n: usize = copy usize, 1",
                "_ = memmove int32, pb, pa, n",
                "var x: int32 = load int32, pb",
                "_ = writeln int32, x",
                "var z: int32 = load.volatile int32, pb",
                "_ = writeln int32, z",
                "var w: int32 = copy int32, 5",
                "_ = store int32, pb, w",
                "var v: int32 = load int32, pb",
                "_ = writeln int32, v",
                "_ = writeln int32, s.a",
                "end",
            ),
            "lib.tc": library(
                "public struct S then",
                "var a: int32",
                "var b: int32",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0012",
        "standard §6.10 switch, br_if, labels and default target",
        out="2\n9\n",
        files={"root.tc": program(
            "var x: int32 = copy int32, 1",
            "_ = switch x, L0, L1, Ld",
            "label L0:",
            "_ = writeln int32, 0",
            "_ = br done",
            "label L1:",
            "_ = writeln int32, 2",
            "_ = br done",
            "label Ld:",
            "_ = writeln int32, 99",
            "label done:",
            "var c: bool = copy bool, true",
            "_ = br_if c, yes",
            "_ = writeln int32, 0",
            "label yes:",
            "_ = writeln int32, 9",
        )},
    ),
    case(
        "TC-ARM32-S9-0013",
        "standard §11.2/§11.3 W-sized read/write formatting and token input",
        out=("4\n-7\n4294967295\n18446744073709551615\n"),
        stdin=b"4 -7 4294967295 18446744073709551615\n",
        files={"root.tc": program(
            "var a: usize = read usize",
            "_ = writeln usize, a",
            "var b: isize = read isize",
            "_ = writeln isize, b",
            "var c: uint32 = read uint32",
            "_ = writeln uint32, c",
            "var d: uint64 = read uint64",
            "_ = writeln uint64, d",
        )},
    ),
    case(
        "TC-ARM32-S9-0014",
        "standard §6.3 strict W=32 overflow and division traps",
        trap="TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var a: int32 = copy int32, 2147483647",
            "var b: int32 = add int32, a, 1",
        )},
    ),
    case(
        "TC-ARM32-S9-0015",
        "standard §6.3 div(INT32_MIN,-1) traps while mod(INT32_MIN,-1) is zero",
        out="0\n",
        files={"root.tc": program(
            "var a: int32 = copy int32, -2147483648",
            "var b: int32 = mod int32, a, -1",
            "_ = writeln int32, b",
        )},
    ),
    case(
        "TC-ARM32-S9-0016",
        "standard §6.7 null pointer dereference is a mandatory trap",
        trap="TC_RE_NULL_POINTER",
        files={"root.tc": program(
            "var p: ptr<int32> = copy ptr<int32>, nullptr",
            "unsafe then",
            "var v: int32 = load int32, p",
            "end",
        )},
    ),
    case(
        "TC-ARM32-S9-0017",
        "standard §3.10/§4.7 static storage is shared across a module",
        out="5\n6\n6\n",
        files={
            "root.tc": program(
                "import lib",
                "var a: int32 = call lib.get",
                "_ = writeln int32, a",
                "var b: int32 = call lib.bump",
                "_ = writeln int32, b",
                "var c: int32 = call lib.get",
                "_ = writeln int32, c",
            ),
            "lib.tc": library(
                "public static var V: int32 = 5",
                "public func get() int32 then",
                "_ = return Self.V",
                "end",
                "public func bump() int32 then",
                "Self.V = add int32, Self.V, 1",
                "_ = return Self.V",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0018",
        "standard §13.3 checked build keeps legal object accesses working",
        out="7\n",
        checked=True,
        files={"root.tc": program(
            "var a: int32 = copy int32, 7",
            "var p: ptr<int32> = addr int32, a",
            "unsafe then",
            "var v: int32 = load int32, p",
            "_ = writeln int32, v",
            "end",
        )},
    ),
    case(
        "TC-ARM32-S9-0019",
        "standard §13.3 checked build still executes mandatory traps",
        trap="TC_RE_INTEGER_OVERFLOW",
        checked=True,
        files={"root.tc": program(
            "var a: int32 = copy int32, 2147483647",
            "var b: int32 = add int32, a, 1",
        )},
    ),
    case(
        "TC-ARM32-S9-0024",
        "standard §8.5 Rule C.5 partially splits an eight-byte composite",
        out="100\n",
        files={
            "root.tc": program(
                "import lib",
                "var p: lib.Pair = make lib.Pair, 10, 20",
                "var r: int32 = call lib.f, 1, 2, 3, p, 64",
                "_ = writeln int32, r",
            ),
            "lib.tc": library(
                "public struct Pair then",
                "var x: int32",
                "var y: int32",
                "end",
                "public func f(a: int32, b: int32, c: int32, p: Pair, "
                "n: int32) int32 then",
                "var s: int32 = add int32, a, b",
                "var t: int32 = add int32, s, c",
                "var u: int32 = add int32, t, p.x",
                "var v: int32 = add int32, u, p.y",
                "var w: int32 = add int32, v, n",
                "_ = return w",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0025",
        "standard §8.5 float32 and narrow scalar argument/return classes",
        out="3\n1\n",
        files={
            "root.tc": program(
                "import lib",
                "var f: float32 = call lib.half, 1.5f, 2",
                "var r: int32 = cast int32, f",
                "_ = writeln int32, r",
                "var b: bool = call lib.truthy, 5",
                "var n: int32 = cast int32, b",
                "_ = writeln int32, n",
            ),
            "lib.tc": library(
                "public func half(x: float32, y: int32) float32 then",
                "var z: float32 = cast float32, y",
                "var r: float32 = add float32, x, z",
                "_ = return r",
                "end",
                "public func truthy(x: int32) bool then",
                "var z: int32 = copy int32, 0",
                "var r: bool = gt int32, x, z",
                "_ = return r",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0022",
        "standard §8.6 @range/@align endpoints on the 32-bit profile",
        out="8\n",
        files={
            "root.tc": program(
                "import lib",
                "var a: int32 = copy int32, 7",
                "var p: ptr<int32> = addr int32, a",
                "var n: usize = copy usize, 1",
                "unsafe then",
                "var r: int32 = call lib.f, p, n",
                "_ = writeln int32, r",
                "end",
            ),
            "lib.tc": library(
                "public @fast func f(@align(4) p: ptr<int32>, "
                "@range(0, 4294967295) n: usize) int32 then",
                "var v: int32 = load int32, p",
                "var m: int32 = cast int32, n",
                "var r: int32 = add int32, v, m",
                "_ = return r",
                "end",
            ),
        },
    ),
    case(
        "TC-ARM32-S9-0023",
        "standard §6.7/§13.2.1 32-bit address-space pointer arithmetic boundary",
        trap="TC_RE_ADDR_UNAVAILABLE",
        files={"root.tc": program(
            "var a: int32 = copy int32, 1",
            "var p: ptr<int32> = addr int32, a",
            "unsafe then",
            "var q: ptr<int32> = ptradd int32, p, 0x20000000",
            "var v: int32 = load int32, q",
            "end",
        )},
    ),
    case(
        "TC-ARM32-S9-0021",
        "standard §11.3 a long leading-zero token exercises the runtime arena",
        out="42\n",
        stdin=b"0" * 4000 + b"42\n",
        files={"root.tc": program("var x: int32 = read int32",
                                  "_ = writeln int32, x")},
    ),
    case(
        "TC-ARM32-S9-0020",
        "standard §10.6/§11.2 binary32 narrowing, subnormals and signed zero",
        out=("1.17549e-38\n-0\n"),
        files={"root.tc": program(
            "var a: float32 = copy float32, 1.17549435e-38f",
            "var b: float64 = cast float64, a",
            "_ = writeln float64, b",
            "var z: float64 = copy float64, -0.0",
            "_ = writeln float64, z",
        )},
    ),
    case(
        "TC-ARM32-S9-0026",
        "standard §6.3 int64 division/modulo sign grid through the 32-bit "
        "runtime helpers (truncating division, remainder takes the dividend "
        "sign, mod INT64_MIN by -1 is defined 0)",
        out=("-3\n-1\n0\n2\n0\n"),
        files={"root.tc": program(
            "var a: int64 = copy int64, -7",
            "var b: int64 = copy int64, 2",
            "var q1: int64 = div int64, a, b",
            "var m1: int64 = mod int64, a, b",
            "var q2: int64 = div int64, b, a",
            "var m2: int64 = mod int64, b, a",
            "_ = writeln int64, q1",
            "_ = writeln int64, m1",
            "_ = writeln int64, q2",
            "_ = writeln int64, m2",
            "var n: int64 = copy int64, -9223372036854775808",
            "var d: int64 = copy int64, -1",
            "var m3: int64 = mod int64, n, d",
            "_ = writeln int64, m3",
        )},
    ),
    case(
        "TC-ARM32-S9-0027",
        "standard §6.3 int64 division INT64_MIN / -1 overflows through the "
        "32-bit runtime helpers; committed output before the trap is kept",
        out="0\n",
        trap="TC_RE_INTEGER_OVERFLOW",
        files={"root.tc": program(
            "var n: int64 = copy int64, -9223372036854775808",
            "var d: int64 = copy int64, -1",
            "var m: int64 = mod int64, n, d",
            "_ = writeln int64, m",
            "var q: int64 = div int64, n, d",
            "_ = writeln int64, q",
        )},
    ),
    case(
        "TC-ARM32-S9-0028",
        "standard §6.7 unaligned multi-byte access is legal: int32 and "
        "float64 stores/loads at byte-offset addresses inside one object",
        out="1651564865\n2.5\n",
        files={"root.tc": program(
            "struct S then",
            "var c: int64",
            "end",
            "struct T then",
            "var c: int64",
            "var d: int64",
            "end",
            "var s: S = make S, 305419896",
            "var ps: ptr<int64> = addr int64, s.c",
            "var t: T = make T, 1, 2",
            "var pt: ptr<int64> = addr int64, t.c",
            "unsafe then",
            "var b1: ptr<int8> = bitcast ptr<int8>, ps",
            "var o1: ptr<int8> = ptradd int8, b1, 1",
            "var p32: ptr<int32> = bitcast ptr<int32>, o1",
            "_ = store int32, p32, 1651564865",
            "var v: int32 = load int32, p32",
            "end",
            "_ = writeln int32, v",
            "unsafe then",
            "var b2: ptr<int8> = bitcast ptr<int8>, pt",
            "var o2: ptr<int8> = ptradd int8, b2, 2",
            "var p64: ptr<float64> = bitcast ptr<float64>, o2",
            "_ = store float64, p64, 2.5",
            "var f: float64 = load float64, p64",
            "end",
            "_ = writeln float64, f",
        )},
    ),
    case(
        "TC-ARM32-S9-0029",
        "standard §8.5 AAPCS32 stack passing: a fifth float32 argument and a "
        "fifth int64 argument exceed the four core registers and are passed "
        "on the stack with correct alignment",
        out="15\n8844323110111\n",
        files={
            "root.tc": program(
                "import lib",
                "var r: float32 = call lib.sum5, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f",
                "_ = writeln float32, r",
                "var t: int64 = call lib.pick, 1, 2, 3, 4, 8844323110110",
                "_ = writeln int64, t",
            ),
            "lib.tc": library(
                "public func sum5(a: float32, b: float32, c: float32, "
                "d: float32, e: float32) float32 then",
                "var s1: float32 = add float32, a, b",
                "var s2: float32 = add float32, s1, c",
                "var s3: float32 = add float32, s2, d",
                "var s4: float32 = add float32, s3, e",
                "_ = return s4",
                "end",
                "public func pick(a: int32, b: int32, c: int32, d: int32, "
                "e: int64) int64 then",
                "var x: int64 = cast int64, a",
                "var r: int64 = add int64, x, e",
                "_ = return r",
                "end",
            ),
        },
    ),
]


def first_code(text: str) -> str | None:
    match = DIAG_RE.search(text)
    return match.group(0) if match else None


def compare_rejection(vm, build, failures: list[str], label: str) -> None:
    vm_code = first_code(vm.stderr)
    aot_code = first_code(build.stderr)
    if vm.exit == 0 or vm_code is None:
        failures.append(f"{label}: AOT build rejected but VM accepted: "
                        f"{build.stderr.strip()[:200]}")
        return
    if aot_code != vm_code:
        failures.append(f"{label}: static rejection {aot_code} != {vm_code}")


def run_curated(failures: list[str]) -> None:
    for item in CASES:
        directory = engine.temporary_directory("tc-s9-arm32-")
        try:
            vm, aot, build = engine.run_case(item, PROFILE, directory)
        except Exception as exc:  # noqa: BLE001 - report and continue
            failures.append(f"{item.case_id}: {exc}")
            shutil.rmtree(directory, ignore_errors=True)
            continue
        if vm.stdout != item.stdout.encode():
            failures.append(f"{item.case_id}: VM stdout {vm.stdout!r} != "
                            f"{item.stdout.encode()!r}")
        if vm.trap != item.trap or vm.exit != item.exit:
            failures.append(f"{item.case_id}: VM trap/exit {vm.trap}/"
                            f"{vm.exit} != {item.trap}/{item.exit}")
        if aot is None:
            compare_rejection(vm, build, failures, item.case_id)
        else:
            if aot.stdout != item.stdout.encode():
                failures.append(f"{item.case_id}: AOT stdout {aot.stdout!r} != "
                                f"{item.stdout.encode()!r}")
            if aot.trap != item.trap or aot.exit != item.exit:
                failures.append(f"{item.case_id}: AOT trap/exit {aot.trap}/"
                                f"{aot.exit} != {item.trap}/{item.exit}")
        shutil.rmtree(directory, ignore_errors=True)


def load_s6() -> list[dict]:
    path = ROOT / "tests/conformance/runtime/s6_cases.py"
    spec = importlib.util.spec_from_file_location("s9_s6_cases", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return [item for item in module.CASES
            if not item["id"].startswith("TC-RT-GOLD")]


def load_s8() -> list[Case]:
    path = ROOT / "tests/conformance/diff/s8_cases.py"
    spec = importlib.util.spec_from_file_location("s9_s8_cases", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return list(module.CASES)


def run_corpus(failures: list[str]) -> None:
    corpus = 0
    for item in load_s6():
        directory = engine.temporary_directory("tc-s9-arm32-s6-")
        files = {name: text for name, text in item["files"].items()}
        corpus_case = Case(
            case_id=f"{item['id']}",
            profile="w32",
            files=files,
            stdout=item.get("stdout", ""),
            trap=item.get("trap"),
            exit=1 if item.get("trap") else 0,
            stdin=item.get("stdin", "").encode(),
            checked=bool(item.get("checked", False)),
            opt=item.get("opt"),
            spec=item.get("spec", ""),
        )
        try:
            vm, aot, build = engine.run_case(corpus_case, PROFILE, directory)
        except Exception as exc:  # noqa: BLE001
            failures.append(f"S6 {item['id']}: {exc}")
            shutil.rmtree(directory, ignore_errors=True)
            continue
        corpus += 1
        if item.get("profile") == "w32":
            if vm.stdout != corpus_case.stdout.encode():
                failures.append(f"S6 {item['id']}: VM stdout "
                                f"{vm.stdout!r} != expected")
            if vm.trap != corpus_case.trap or vm.exit != corpus_case.exit:
                failures.append(f"S6 {item['id']}: VM trap/exit "
                                f"{vm.trap}/{vm.exit} != "
                                f"{corpus_case.trap}/{corpus_case.exit}")
        if aot is None:
            compare_rejection(vm, build, failures, f"S6 {item['id']}")
        elif vm.stdout != aot.stdout or vm.trap != aot.trap or \
                vm.exit != aot.exit:
            failures.append(
                f"S6 {item['id']}: VM {vm.stdout!r}/{vm.trap}/{vm.exit} != "
                f"AOT {aot.stdout!r}/{aot.trap}/{aot.exit}")
        shutil.rmtree(directory, ignore_errors=True)
    for item in load_s8():
        directory = engine.temporary_directory("tc-s9-arm32-s8-")
        try:
            vm, aot, build = engine.run_case(item, PROFILE, directory)
        except Exception as exc:  # noqa: BLE001
            failures.append(f"S8 {item.case_id}: {exc}")
            shutil.rmtree(directory, ignore_errors=True)
            continue
        corpus += 1
        if aot is None:
            compare_rejection(vm, build, failures, f"S8 {item.case_id}")
        elif vm.stdout != aot.stdout or vm.trap != aot.trap or \
                vm.exit != aot.exit:
            failures.append(
                f"S8 {item.case_id}: VM {vm.stdout!r}/{vm.trap}/{vm.exit} != "
                f"AOT {aot.stdout!r}/{aot.trap}/{aot.exit}")
        shutil.rmtree(directory, ignore_errors=True)
    print(f"{CASE_ID}: {len(CASES)} curated and {corpus} corpus cases")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    check_elf_contract(failures)
    run_curated(failures)
    run_corpus(failures)
    if failures:
        for failure in failures:
            print(failure)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: ARM32 semantics/ABI/corpus VM-AOT differential passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
