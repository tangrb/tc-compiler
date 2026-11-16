#!/usr/bin/env python3
"""S7 differential property test over generated integer programs.

A fixed seed generates single-operation programs across the S7 integer widths,
signedness, strict/wrap modes, shifts and comparisons.  The reference VM is the
oracle (its semantics are pinned by the S6 golden/conformance suites); each
program is then compiled to AArch64 and executed on qemu.  The test fails if
the AOT image differs in stdout, first trap or exit status, or if a generated
program that the backend should support is rejected.

Run:
    python3 tests/conformance/diff/s7_fuzz.py --check
"""

from __future__ import annotations

import argparse
import random
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

TYPES = {
    "int8": (-128, 127),
    "int16": (-32768, 32767),
    "int32": (-(2 ** 31), 2 ** 31 - 1),
    "int64": (-(2 ** 63), 2 ** 63 - 1),
    "uint8": (0, 255),
    "uint16": (0, 65535),
    "uint32": (0, 2 ** 32 - 1),
    "uint64": (0, 2 ** 64 - 1),
    "isize": (-(2 ** 63), 2 ** 63 - 1),
    "usize": (0, 2 ** 64 - 1),
}
SIGNED = {"int8", "int16", "int32", "int64", "isize"}
BINOPS = ["add", "sub", "mul", "div", "mod", "shl", "shr", "and", "or", "xor"]
CMPOPS = ["eq", "ne", "lt", "le", "gt", "ge"]
UNOPS = ["neg", "abs", "not"]
WRAPPABLE = {"add", "sub", "mul", "shl"}
COUNT = 40


def literal(type_name: str, value: int) -> str:
    if type_name == "uint64" and value > 2 ** 63 - 1:
        return f"{value}u"
    return str(value)


def generate(rng: random.Random) -> tuple[str, str]:
    type_name = rng.choice(list(TYPES))
    low, high = TYPES[type_name]
    kind = rng.choice(["bin", "cmp", "un"])
    if kind == "bin":
        op = rng.choice(BINOPS)
        a = rng.randint(low, high)
        b = rng.randint(low, high)
        if op in ("div", "mod") and b == 0:
            b = 1
        if op in ("shl", "shr"):
            b = rng.choice([-1, 0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 65])
        mode = ""
        if type_name in SIGNED and op in WRAPPABLE and rng.random() < 0.3:
            mode = ".wrap"
        source = (f"var x: {type_name} = {op}{mode} {type_name}, "
                  f"{literal(type_name, a)}, {literal(type_name, b)}\n"
                  f"_ = writeln {type_name}, x\n")
        return type_name, source
    if kind == "cmp":
        op = rng.choice(CMPOPS)
        a = rng.randint(low, high)
        b = rng.randint(low, high)
        return type_name, (f"var x: bool = {op} {type_name}, "
                           f"{literal(type_name, a)}, {literal(type_name, b)}\n"
                           f"_ = writeln bool, x\n")
    op = rng.choice(UNOPS)
    a = rng.randint(low, high)
    mode = ".wrap" if type_name in SIGNED and op == "neg" and \
        rng.random() < 0.3 else ""
    return type_name, (f"var x: {type_name} = {op}{mode} {type_name}, "
                       f"{literal(type_name, a)}\n"
                       f"_ = writeln {type_name}, x\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    rng = random.Random(0x5A17)
    failures: list[str] = []
    ran = 0
    attempts = 0
    while ran < COUNT and attempts < COUNT * 8:
        attempts += 1
        type_name, body = generate(rng)
        text = "#program\n" + body
        work = Path(tempfile.mkdtemp(prefix="tc-s7-fuzz-"))
        try:
            (work / "root.tc").write_text(text, encoding="utf-8")
            case = engine.Case(
                case_id=f"TC-DIFF-FUZZ-{ran:04d}",
                profile="aarch64-linux-user",
                files={"root.tc": text},
                stdout="", trap=None, exit=0,
            )
            vm = engine.run_vm(case, work)
            if vm.exit not in (0, 1) or (
                    vm.exit == 1 and vm.trap is None):
                # Not a valid generated program for the S7 source grammar;
                # try another seed draw.
                continue
            case.stdout = vm.stdout.decode("utf-8")
            case.trap = vm.trap
            case.exit = vm.exit
            code, _out, err = engine.compile_aot(case, work, emit="elf")
            if code != 0:
                failures.append(
                    f"{type_name}: AOT rejected supported program ({code}): "
                    f"{err.strip()}\n{body}")
                ran += 1
                continue
            elf = work / f"{case.case_id}.elf"
            aot = engine.run_elf(elf, b"", work / "diag", work)
            if (aot.stdout, aot.trap, aot.exit) != (
                    vm.stdout, vm.trap, vm.exit):
                failures.append(
                    f"{type_name}: VM/AOT differ\n{body}"
                    f"  VM={vm.stdout!r},{vm.trap},{vm.exit}\n"
                    f"  AOT={aot.stdout!r},{aot.trap},{aot.exit}")
            ran += 1
        finally:
            shutil.rmtree(work, ignore_errors=True)
    if ran < COUNT:
        failures.append(f"generated only {ran} valid programs")
    if failures:
        print(f"s7.fuzz: {len(failures)} failure(s)")
        for item in failures[:10]:
            print(f"  {item}")
        return 1
    print(f"s7.fuzz: {ran} generated integer programs VM/AOT identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
