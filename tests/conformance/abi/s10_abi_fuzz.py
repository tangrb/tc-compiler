#!/usr/bin/env python3
"""S10 generated ABI fuzz: random signatures against an independent C ABI.

Two directions are generated from one deterministic signature:

  * C caller -> TC callee: clang builds the caller, the TC callee is emitted
    by tc-aot;
  * TC caller -> C callee: tc-aot builds the caller, clang builds the callee.

Scalars of every integer width, bool, float32/float64, and packed structs of
1..N fields (including padding) are mixed.  All-float AArch64 HFA candidates
are generated without explicit padding, because a packed C struct needs a
non-float padding member and would no longer be an HFA.  The TC function returns an integer
checksum of its arguments; the expected checksum is computed in Python.  This
checks argument/return placement beyond the fixed cases: register exhaustion,
the one-register 2*XLEN split, stack pairs, by-reference composites and hidden
indirect returns.

Run:
    python3 tests/conformance/abi/s10_abi_fuzz.py --check
    python3 tests/conformance/abi/s10_abi_fuzz.py --check --seeds 200
    python3 tests/conformance/abi/s10_abi_fuzz.py --check --profile=arm32-baremetal
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import random
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-ABI-FUZZ-S10-0001"
DEFAULT_PROFILE = "riscv32-baremetal"
PROFILES = ("riscv32-baremetal", "arm32-baremetal", "aarch64-baremetal")


@dataclass(frozen=True)
class Scalar:
    tc: str
    c: str
    kind: str  # int, uint, bool, float32, float64


SCALARS = (
    Scalar("int8", "int8_t", "int"),
    Scalar("uint8", "uint8_t", "uint"),
    Scalar("int16", "int16_t", "int"),
    Scalar("uint16", "uint16_t", "uint"),
    Scalar("int32", "int32_t", "int"),
    Scalar("uint32", "uint32_t", "uint"),
    Scalar("int64", "int64_t", "int"),
    Scalar("uint64", "uint64_t", "uint"),
    Scalar("bool", "bool", "bool"),
    Scalar("float32", "float", "float32"),
    Scalar("float64", "double", "float64"),
)
INT_SCALARS = tuple(item for item in SCALARS
                    if item.kind in ("int", "uint", "bool"))
FLOAT_SCALARS = tuple(item for item in SCALARS
                      if item.kind in ("float32", "float64"))


def tc_literal(item: Scalar, value: int | float | bool) -> str:
    if item.kind == "bool":
        return "true" if value else "false"
    if item.kind == "float32":
        return f"{float(value):.1f}f"
    if item.kind == "float64":
        return f"{float(value):.1f}"
    if item.kind == "uint":
        return f"{int(value)}u"
    return str(int(value))


def c_literal(item: Scalar, value: int | float | bool) -> str:
    if item.kind == "bool":
        return "true" if value else "false"
    if item.kind == "float32":
        return f"{float(value):.1f}f"
    if item.kind == "float64":
        return f"{float(value):.1f}"
    if item.kind == "uint64":
        return f"UINT64_C({int(value)})"
    if item.kind == "uint32":
        return f"UINT32_C({int(value)})"
    return str(int(value))


def value_for(item: Scalar, rng: random.Random) -> int | float | bool:
    if item.kind == "bool":
        return rng.choice([False, True])
    if item.kind == "float32":
        return rng.choice([0.0, 1.0, -1.0, 2.0, 3.0, -2.0])
    if item.kind == "float64":
        return rng.choice([0.0, 1.0, -1.0, 2.0, 3.0, -2.0, 7.0])
    if item.kind == "uint":
        return rng.randint(0, 20)
    return rng.randint(-20, 20)


def scalar_checksum(item: Scalar, name: str, lines: list[str],
                    terms: list[str], prefix: str) -> None:
    if item.kind in ("float32", "float64"):
        lines.append(f"var {prefix}f: float64 = cast float64, {name}")
        lines.append(f"var {prefix}i: int64 = cast int64, {prefix}f")
        terms.append(f"{prefix}i")
    elif item.tc == "int64":
        lines.append(f"var {prefix}i: int64 = copy int64, {name}")
        terms.append(f"{prefix}i")
    else:
        lines.append(f"var {prefix}i: int64 = cast int64, {name}")
        terms.append(f"{prefix}i")


@dataclass
class Struct:
    name: str
    fields: list[tuple[str, Scalar, int]]  # field name, type, padding after


@dataclass
class Signature:
    params: list[tuple[str, Scalar | Struct]]
    values: list  # scalar value or list of field values
    structs: list[Struct]


def make_struct(rng: random.Random, index: int) -> Struct:
    fields = []
    count = rng.randint(1, 5)
    for field_index in range(count):
        if rng.random() < 0.15:
            item = rng.choice(FLOAT_SCALARS)
        else:
            item = rng.choice(INT_SCALARS)
        padding = rng.choice([0, 0, 0, 1, 2, 4, 8])
        fields.append((f"f{field_index}", item, padding))
    # An all-float TC aggregate is an AArch64 HFA; its explicit @padding bytes
    # cannot be represented by a packed C struct without adding a non-float
    # member.  Keep generated HFA candidates padding-free so the C oracle has
    # the same classification.
    kinds = {field.kind for _name, field, _padding in fields}
    if len(kinds) == 1 and next(iter(kinds)) in ("float32", "float64"):
        fields = [(name, field, 0) for name, field, _padding in fields]
    return Struct(f"S{index}", fields)


def make_signature(seed: int) -> Signature:
    rng = random.Random(seed)
    count = rng.randint(0, 18)
    structs: list[Struct] = []
    params: list[tuple[str, Scalar | Struct]] = []
    values: list = []
    for index in range(count):
        if rng.random() < 0.4:
            if not structs or rng.random() < 0.5:
                structs.append(make_struct(rng, len(structs)))
            item = rng.choice(structs)
            field_values = [value_for(field, rng)
                            for _fname, field, _pad in item.fields]
            params.append((f"p{index}", item))
            values.append(field_values)
        else:
            item = rng.choice(SCALARS)
            params.append((f"p{index}", item))
            values.append(value_for(item, rng))
    return Signature(params, values, structs)


def struct_size(item: Struct) -> int:
    total = 0
    for _name, field, padding in item.fields:
        total += {"int8": 1, "uint8": 1, "bool": 1,
                  "int16": 2, "uint16": 2,
                  "int32": 4, "uint32": 4, "float32": 4,
                  "int64": 8, "uint64": 8, "float64": 8}[field.tc]
        total += padding
    return total


def tc_struct_decls(structs: list[Struct]) -> list[str]:
    lines: list[str] = []
    for item in structs:
        lines.append(f"public struct {item.name} then")
        for field_name, field, padding in item.fields:
            if padding:
                lines.append(f"var {field_name}: {field.tc} @padding({padding})")
            else:
                lines.append(f"var {field_name}: {field.tc}")
        lines.append("end")
    return lines


def c_struct_decls(structs: list[Struct]) -> list[str]:
    lines: list[str] = []
    for item in structs:
        lines.append("typedef struct __attribute__((packed)) {")
        for field_name, field, padding in item.fields:
            lines.append(f"    {field.c} {field_name};")
            if padding:
                lines.append(f"    uint8_t pad_{field_name}[{padding}];")
        lines.append(f"}} {item.name};")
    return lines


def tc_param_list(signature: Signature) -> str:
    parts = []
    for name, item in signature.params:
        if isinstance(item, Struct):
            parts.append(f"{name}: {item.name}")
        else:
            parts.append(f"{name}: {item.tc}")
    return ", ".join(parts)


def c_param_list(signature: Signature) -> str:
    parts = []
    for _name, item in signature.params:
        parts.append(item.name if isinstance(item, Struct) else item.c)
    return ", ".join(parts) if parts else "void"


def checksum_terms_tc(signature: Signature, lines: list[str],
                      source_names: list[str]) -> list[str]:
    terms: list[str] = []
    for index, (name, item) in enumerate(signature.params):
        source = source_names[index]
        if isinstance(item, Struct):
            for field_name, field, _padding in item.fields:
                scalar_checksum(field, f"{source}.{field_name}", lines,
                                terms, f"x{index}_{field_name}")
        else:
            scalar_checksum(item, source, lines, terms, f"x{index}")
    return terms


def sum_lines(lines: list[str], terms: list[str]) -> None:
    if not terms:
        lines.append("var s: int64 = copy int64, 0")
        lines.append("_ = return s")
        return
    lines.append(f"var s0: int64 = copy int64, {terms[0]}")
    for index, term in enumerate(terms[1:], start=1):
        lines.append(f"var s{index}: int64 = add int64, s{index - 1}, {term}")
    lines.append(f"_ = return s{len(terms) - 1}")


def expected_checksum(signature: Signature) -> int:
    total = 0
    for value, (_name, item) in zip(signature.values, signature.params):
        if isinstance(item, Struct):
            total += sum(int(field_value) for field_value in value)
        else:
            total += int(value)
    return total


def build_c_to_tc(signature: Signature) -> tuple[str, str]:
    lib = ["#lib"] + tc_struct_decls(signature.structs)
    lib.append(f"public func f({tc_param_list(signature)}) int64 then")
    lines: list[str] = []
    terms = checksum_terms_tc(
        signature, lines, [name for name, _item in signature.params])
    sum_lines(lines, terms)
    lib.extend(lines)
    lib.append("end")
    harness = []
    harness.append(f"extern int64_t tc_test_call({c_param_list(signature)});")
    harness.append("void tc_main(void) {")
    arguments = []
    for index, (_name, item) in enumerate(signature.params):
        if isinstance(item, Struct):
            harness.append(f"    {item.name} a{index};")
            for field_index, (field_name, _field, _pad) in enumerate(
                    item.fields):
                value = signature.values[index][field_index]
                harness.append(
                    f"    a{index}.{field_name} = {c_literal(item.fields[field_index][1], value)};")
            arguments.append(f"a{index}")
        else:
            value = signature.values[index]
            arguments.append(c_literal(item, value))
    call = f"tc_test_call({', '.join(arguments)})"
    harness.append(f"    pi((uint64_t){call});")
    harness.append("}")
    prefix = "\n".join(c_struct_decls(signature.structs)) + "\n"
    return ("\n".join(lib) + "\n",
            prefix + "\n".join(harness) + "\n")


def build_tc_to_c(signature: Signature) -> tuple[str, str]:
    lib = ["#lib"] + tc_struct_decls(signature.structs)
    target_lines: list[str] = []
    terms = checksum_terms_tc(
        signature, target_lines, [name for name, _item in signature.params])
    sum_lines(target_lines, terms)
    lib.append(f"public func target({tc_param_list(signature)}) int64 then")
    lib.extend(target_lines)
    lib.append("end")
    # Caller constructs values and calls target.
    caller_lines: list[str] = []
    arguments: list[str] = []
    for index, (_name, item) in enumerate(signature.params):
        if isinstance(item, Struct):
            field_args = []
            for field_index, (_field_name, field, _pad) in enumerate(
                    item.fields):
                value = signature.values[index][field_index]
                field_args.append(tc_literal(field, value))
            caller_lines.append(
                f"var s{index}: {item.name} = make {item.name}, {', '.join(field_args)}")
            arguments.append(f"s{index}")
        else:
            arguments.append(tc_literal(item, signature.values[index]))
    call = f"call Self.target{', ' if arguments else ''}{', '.join(arguments)}"
    caller_lines.append(f"var r: int64 = {call}")
    caller_lines.append("_ = return r")
    lib.append("public func caller() int64 then")
    lib.extend(caller_lines)
    lib.append("end")
    # C callee checksum.
    c_terms = []
    for index, (_name, item) in enumerate(signature.params):
        if isinstance(item, Struct):
            for field_name, field, _pad in item.fields:
                if field.kind in ("float32", "float64"):
                    c_terms.append(
                        f"((int64_t)(int32_t)p{index}.{field_name})")
                else:
                    c_terms.append(f"((int64_t)p{index}.{field_name})")
        else:
            if item.kind in ("float32", "float64"):
                c_terms.append(f"((int64_t)(int32_t)p{index})")
            else:
                c_terms.append(f"((int64_t)p{index})")
    c_lines = ["#include <stdint.h>", "#include <stdbool.h>"]
    c_lines.extend(c_struct_decls(signature.structs))
    params = ", ".join(
        f"{item.name if isinstance(item, Struct) else item.c} p{index}"
        for index, (_name, item) in enumerate(signature.params)) or "void"
    expression = " + ".join(c_terms) if c_terms else "0"
    c_lines.append(f"int64_t c_callee({params}) {{ return {expression}; }}")
    c_lines.append("extern int64_t tc_function_1(void);")
    c_lines.append("void tc_main(void) { pi((uint64_t)tc_function_1()); }")
    return ("\n".join(lib) + "\n", "\n".join(c_lines) + "\n")


def load_module(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def riscv_clang() -> str | None:
    for candidate in ("/opt/homebrew/opt/llvm/bin/clang",
                      "/usr/local/opt/llvm/bin/clang"):
        if Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    return shutil.which("clang")


def run_one(seed: int, profile: str, codegen, runner, expected: int,
            keep: bool) -> tuple[bool, str]:
    signature = make_signature(seed)
    lib, harness = codegen(signature)
    directory = Path(tempfile.mkdtemp(prefix=f"tc-abifuzz-{seed}-"))
    try:
        try:
            code, stdout, diagnostic = runner(lib, harness, directory)
        except Exception as exc:  # noqa: BLE001 - report generated-case failure
            text = (f"seed {seed} profile {profile}: exception {exc!r}")
            if keep:
                target = Path(tempfile.gettempdir()) / (
                    f"tc-abifuzz-fail-{seed}-{profile.replace('/', '_')}")
                shutil.rmtree(target, ignore_errors=True)
                shutil.copytree(directory, target)
                text += f"\nkept {target}"
            return False, text
        actual = stdout.decode("utf-8", "replace")
        wanted = f"{expected}\n"
        if code != 0 or actual != wanted or "none" not in diagnostic:
            text = (f"seed {seed} profile {profile}: code={code} "
                    f"stdout={actual!r} expected={wanted!r}\n"
                    f"diagnostic={diagnostic!r}")
            if keep:
                target = Path(tempfile.gettempdir()) / (
                    f"tc-abifuzz-fail-{seed}-{profile.replace('/', '_')}")
                shutil.rmtree(target, ignore_errors=True)
                shutil.copytree(directory, target)
                text += f"\nkept {target}"
            return False, text
        return True, ""
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--seeds", type=int, default=8)
    parser.add_argument("--profile", default=None)
    parser.add_argument("--profiles", default=",".join(PROFILES))
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    profiles = ([args.profile] if args.profile is not None
                else [item for item in args.profiles.split(",") if item])
    for profile in profiles:
        if profile not in PROFILES:
            parser.error(f"unsupported profile {profile}")
    interop = load_module("s10_interop", "tests/conformance/abi/s9_arm32_interop.py")
    reverse = load_module("s10_reverse",
                          "tests/conformance/abi/reverse_interop_cases.py")
    failures: list[str] = []
    total = 0
    for profile in profiles:
        clang = (riscv_clang() if profile == "riscv32-baremetal"
                 else shutil.which("clang"))
        interop.CLANG = clang
        reverse.CLANG = clang
        for seed in range(args.seeds):
            signature = make_signature(seed)
            expected = expected_checksum(signature)
            ok, detail = run_one(
                seed, profile,
                lambda sig: build_c_to_tc(sig),
                lambda lib_text, c_text, directory: interop.build_and_run(
                    lib_text, c_text, directory, profile),
                expected, args.keep)
            total += 1
            if not ok:
                failures.append(detail)
            ok, detail = run_one(
                seed, profile,
                lambda sig: build_tc_to_c(sig),
                lambda lib_text, c_text, directory: reverse.build_and_run(
                    profile, lib_text, c_text, directory),
                expected, args.keep)
            total += 1
            if not ok:
                failures.append(detail)
    if failures:
        for item in failures[:20]:
            print(item)
        print(f"{CASE_ID}: {len(failures)} of {total} generated ABI cases failed")
        return 1
    print(f"{CASE_ID}: {total} generated ABI cases passed "
          f"({', '.join(profiles)}, {args.seeds} seeds x 2 directions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
