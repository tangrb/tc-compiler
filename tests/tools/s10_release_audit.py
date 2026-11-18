#!/usr/bin/env python3
"""S10 release audit (test plan S10 / design §12/§14/§15).

Checks the release surface of the 0.0.46 branch:

  * closed diagnostic set (65 TC_CE_* + 15 TC_RE_*), no extra codes;
  * private tc_core static library, no install/export rule, hidden symbols;
  * exactly two tool entry points (tc-vm, tc-aot), no C host public library or
    embedding API;
  * documented implementation limits use the internal-error channel, not
    TC_ERR_OUT_OF_MEMORY;
  * the freestanding RISC-V image has no undefined/libc symbols;
  * version and documentation consistency (current capabilities, gate state,
    changelog, links, target list).

Run:
    python3 tests/tools/s10_release_audit.py --check
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
ROOT = HERE.parents[1]

CASE_ID = "TC-REL-S10-0001"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def tool(name: str, env: str) -> str:
    value = os.environ.get(env)
    if value:
        return value
    local = ROOT / "build" / "src" / "driver" / name
    if local.is_file() and os.access(local, os.X_OK):
        return str(local)
    found = shutil.which(name)
    if not found:
        raise SystemExit(f"required tool '{name}' not found (set {env})")
    return found


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


def check_codes(failures: list[str]) -> None:
    codes = read("src/diagnostic/codes.inc")
    ce = re.findall(r"\bTC_CE_[A-Z0-9_]+\b", codes)
    re_codes = re.findall(r"\bTC_RE_[A-Z0-9_]+\b", codes)
    if len(set(ce)) != 65:
        failures.append(f"{CASE_ID}: {len(set(ce))} TC_CE_* codes, expected 65")
    if len(set(re_codes)) != 15:
        failures.append(
            f"{CASE_ID}: {len(set(re_codes))} TC_RE_* codes, expected 15")
    json_codes = re.findall(r'"TC_[A-Z0-9_]+"', read("tests/traceability/codes.json"))
    if len(set(json_codes)) != 80:
        failures.append(
            f"{CASE_ID}: traceability code table has {len(set(json_codes))} "
            "entries, expected 80")
    if "TC_ERR_OUT_OF_MEMORY" in codes:
        failures.append(f"{CASE_ID}: TC_ERR_OUT_OF_MEMORY entered the language set")


def check_release_surface(failures: list[str]) -> None:
    cmake = read("src/CMakeLists.txt")
    if "add_library(tc_core STATIC" not in cmake:
        failures.append(f"{CASE_ID}: tc_core is not a static private library")
    if "C_VISIBILITY_PRESET hidden" not in cmake:
        failures.append(f"{CASE_ID}: tc_core symbols are not hidden")
    for relative in ("CMakeLists.txt", "src/CMakeLists.txt",
                     "src/driver/CMakeLists.txt", "tests/CMakeLists.txt"):
        text = read(relative)
        if re.search(r"(?m)^\s*install\s*\(", text):
            failures.append(f"{CASE_ID}: {relative} contains an install rule")
    host = read("src/CMakeLists.txt") + read("CMakeLists.txt")
    if re.search(r"add_library\([^)]*SHARED", host):
        failures.append(f"{CASE_ID}: a shared host library is built")
    driver = read("src/driver/CMakeLists.txt")
    if driver.count("add_executable(") != 2:
        failures.append(
            f"{CASE_ID}: src/driver builds {driver.count('add_executable(')} "
            "executables, expected 2")
    if "add_executable(tc-vm" not in driver or \
            "add_executable(tc-aot" not in driver:
        failures.append(f"{CASE_ID}: the two CLI entry points changed")
    sources = list((ROOT / "src").rglob("*.c"))
    mains = []
    for path in sources:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r"\bint\s+main\s*\(", text):
            mains.append(str(path.relative_to(ROOT)))
    if sorted(mains) != ["src/driver/tc-aot.c", "src/driver/tc-vm.c"]:
        failures.append(f"{CASE_ID}: unexpected main() definitions: {mains}")
    if (ROOT / "src/include").exists() or (ROOT / "include").exists():
        failures.append(f"{CASE_ID}: a public include directory exists")
    for header in (ROOT / "src").rglob("*.h"):
        text = header.read_text(encoding="utf-8", errors="ignore")
        if 'visibility("default")' in text or "TC_PUBLIC_API" in text:
            failures.append(
                f"{CASE_ID}: {header.relative_to(ROOT)} exports a public API")
    if "TC_ERR_OUT_OF_MEMORY" in read("src/frontend/input.h"):
        failures.append(f"{CASE_ID}: TC_ERR_OUT_OF_MEMORY used as a limit")
    limits = read("src/frontend/input.h")
    for needle in ("4u * 1024u * 1024u", "16384u", "256u"):
        if needle not in limits:
            failures.append(f"{CASE_ID}: documented limit {needle} is missing")
    if "tc_source_limit_violation" not in read("src/frontend/input.c"):
        failures.append(f"{CASE_ID}: source limit check is missing")
    driver_text = read("src/driver/tc-vm.c") + read("src/driver/tc-aot.c")
    if "tc_error_internal" not in driver_text:
        failures.append(f"{CASE_ID}: limits do not use tc_error_internal")


def check_artifacts(failures: list[str]) -> None:
    sys.path.insert(0, str(ROOT / "tests"))
    from diff import arm32_engine as engine  # noqa: E402

    directory = engine.temporary_directory("tc-s10-audit-")
    try:
        (directory / "root.tc").write_text(
            "#program\nvar x: int64 = mul int64, 3037000499, 3037000499\n"
            "_ = writeln int64, x\n", encoding="utf-8")
        elf = directory / "out.elf"
        proc = subprocess.run(
            [tool("tc-aot", "TC_AOT"), "--profile=riscv32-baremetal",
             "--emit=elf", f"--output={elf}", f"--include={directory}",
             f"--ld={tool('ld.lld', 'TC_AOT_LINKER')}",
             str(directory / "root.tc")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        if proc.returncode != 0:
            failures.append(
                f"{CASE_ID}: RISC-V release image build failed: "
                f"{proc.stderr.decode('utf-8', 'replace').strip()}")
            return
        data = elf.read_bytes()
        if data[:4] != b"\x7fELF" or data[4] != 1:
            failures.append(f"{CASE_ID}: release image is not ELF32")
        nm = llvm_tool("llvm-nm") or "nm"
        proc = subprocess.run([nm, "-u", str(elf)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60)
        undefined = [line.split()[-1] for line in
                     proc.stdout.decode("utf-8", "replace").splitlines()
                     if line.split()[:1] == ["U"]]
        if undefined:
            failures.append(
                f"{CASE_ID}: freestanding image has undefined symbols: "
                f"{undefined[:8]}")
        forbidden = {"printf", "malloc", "free", "strlen", "memcmp",
                     "abort", "exit", "__libc_start_main"}
        proc = subprocess.run([nm, str(elf)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60)
        defined = {line.split()[-1] for line in
                   proc.stdout.decode("utf-8", "replace").splitlines()
                   if len(line.split()) >= 3}
        leaked = forbidden & defined
        if leaked:
            failures.append(
                f"{CASE_ID}: libc symbols in freestanding image: "
                f"{sorted(leaked)}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    for name in ("tc-vm", "tc-aot"):
        proc = subprocess.run([tool(name, "TC_VM" if name == "tc-vm"
                                    else "TC_AOT"), "--version"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=30)
        text = proc.stdout.decode("utf-8", "replace")
        if "0.0.46" not in text:
            failures.append(f"{CASE_ID}: {name} version is not 0.0.46")


def check_docs(failures: list[str]) -> None:
    required = {
        "README.md": ["riscv32-baremetal", "RISC-V"],
        "README.en.md": ["riscv32", "RISC-V"],
        "AGENTS.md": ["riscv32", "S10"],
        "src/README.md": ["riscv32"],
        "tests/README.md": ["S10", "riscv32"],
        "docs/README.md": ["RISC-V", "riscv32"],
        "docs/gates/README.md": ["G10", "PASS"],
        "src/CONTRACTS.md": ["riscv32"],
    }
    for relative, needles in required.items():
        text = read(relative)
        for needle in needles:
            if needle not in text:
                failures.append(f"{CASE_ID}: {relative} lacks {needle!r}")
    stale = {
        "README.md": ["未实现**：RISC-V", "RISC-V 等后续目标"],
        "README.en.md": ["Not implemented: later targets such as RISC-V"],
        "AGENTS.md": ["未实现：RISC-V"],
        "src/README.md": ["riscv32 not implemented", "remains a future target"],
    }
    for relative, needles in stale.items():
        text = read(relative)
        for needle in needles:
            if needle in text:
                failures.append(
                    f"{CASE_ID}: {relative} still says {needle!r}")
    gate = ROOT / "docs/gates/TC-G10门禁报告-0.0.46.md"
    if not gate.is_file():
        failures.append(f"{CASE_ID}: missing G10 gate record")
    version_h = read("src/base/version.h")
    if '"0.0.46"' not in version_h:
        failures.append(f"{CASE_ID}: language version is not 0.0.46")
    if "VERSION 0.0.46" not in read("CMakeLists.txt"):
        failures.append(f"{CASE_ID}: CMake project version is not 0.0.46")
    for changelog in ("CHANGELOG.md", "CHANGELOG.en.md"):
        text = read(changelog)
        if "0.0.46" not in text:
            failures.append(f"{CASE_ID}: {changelog} lacks 0.0.46")
        headings = re.findall(r"(?m)^#{1,3} .*?(\d+\.\d+\.\d+)", text)
        duplicates = {item for item in headings if headings.count(item) > 1}
        if duplicates:
            failures.append(
                f"{CASE_ID}: {changelog} repeats versions {sorted(duplicates)}")
    for relative in ("README.md", "README.en.md", "docs/README.md",
                     "docs/gates/README.md", "AGENTS.md", "src/README.md"):
        text = read(relative)
        for target in re.findall(r"\]\(([^)#]+)\)", text):
            if target.startswith(("http://", "https://", "mailto:")):
                continue
            if not (ROOT / relative).parent.joinpath(target).exists():
                failures.append(
                    f"{CASE_ID}: {relative} has a broken link {target!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    check_codes(failures)
    check_release_surface(failures)
    check_artifacts(failures)
    check_docs(failures)
    if failures:
        for item in failures:
            print(item)
        print(f"{CASE_ID}: {len(failures)} release-audit failure(s)")
        return 1
    print(f"{CASE_ID}: release audit passed (codes, private core, two CLIs, "
          "freestanding symbols, docs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
