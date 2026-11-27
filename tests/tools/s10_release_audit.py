#!/usr/bin/env python3
"""S10 release audit (test plan S10 / design §12/§14/§15).

Checks the release surface of the 0.0.47 toolchain with frozen 0.0.46
language contracts:

  * closed diagnostic set (65 TC_CE_* + 15 TC_RE_*), no extra codes;
  * private tc_core static library, no install/export rule, hidden symbols;
  * exactly three private tool entry points (tc, tc-vm, tc-aot), no C host
    public library or embedding API;
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
import json
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


def run_nm(command: list[str], failures: list[str],
           label: str) -> bytes | None:
    proc = subprocess.run(command, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60)
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        failures.append(f"{CASE_ID}: {label} failed: {detail}")
        return None
    return proc.stdout


def check_codes(failures: list[str]) -> None:
    sys.path.insert(0, str(ROOT / "tests" / "traceability"))
    from gen_codes import parse_standard_codes  # noqa: E402

    standard_codes = {
        entry["code"]
        for entry in parse_standard_codes(read("docs/TC语言及编译器标准-0.0.46.md"))
    }
    codes = read("src/diagnostic/codes.inc")
    ce = re.findall(r"\bTC_CE_[A-Z0-9_]+\b", codes)
    re_codes = re.findall(r"\bTC_RE_[A-Z0-9_]+\b", codes)
    implementation_codes = set(ce) | set(re_codes)
    if len(set(ce)) != 65:
        failures.append(f"{CASE_ID}: {len(set(ce))} TC_CE_* codes, expected 65")
    if len(set(re_codes)) != 15:
        failures.append(
            f"{CASE_ID}: {len(set(re_codes))} TC_RE_* codes, expected 15")
    if implementation_codes != standard_codes:
        failures.append(
            f"{CASE_ID}: implementation diagnostic names differ from the standard: "
            f"missing={sorted(standard_codes - implementation_codes)}, "
            f"extra={sorted(implementation_codes - standard_codes)}")
    payload = json.loads(read("tests/traceability/codes.json"))
    json_codes = {entry["code"] for entry in payload["codes"]}
    if json_codes != standard_codes:
        failures.append(
            f"{CASE_ID}: traceability diagnostic names differ from the standard: "
            f"missing={sorted(standard_codes - json_codes)}, "
            f"extra={sorted(json_codes - standard_codes)}")
    if "TC_ERR_OUT_OF_MEMORY" in implementation_codes:
        failures.append(f"{CASE_ID}: TC_ERR_OUT_OF_MEMORY entered the language set")


def check_release_surface(failures: list[str]) -> None:
    cmake = read("src/CMakeLists.txt")
    if "add_library(tc_core STATIC" not in cmake:
        failures.append(f"{CASE_ID}: tc_core is not a static private library")
    if "C_VISIBILITY_PRESET hidden" not in cmake:
        failures.append(f"{CASE_ID}: tc_core symbols are not hidden")
    if re.search(
            r"target_include_directories\s*\(\s*tc_core\s+[^)]*\b(?:PUBLIC|INTERFACE)\b",
            cmake, re.S):
        failures.append(f"{CASE_ID}: tc_core exposes internal include requirements")
    for relative in ("CMakeLists.txt", "src/CMakeLists.txt",
                     "src/driver/CMakeLists.txt", "tests/CMakeLists.txt"):
        text = read(relative)
        if re.search(r"(?m)^\s*install\s*\(", text):
            failures.append(f"{CASE_ID}: {relative} contains an install rule")
    host = read("src/CMakeLists.txt") + read("CMakeLists.txt")
    if re.search(r"add_library\([^)]*SHARED", host):
        failures.append(f"{CASE_ID}: a shared host library is built")
    driver = read("src/driver/CMakeLists.txt")
    if driver.count("add_executable(") != 3:
        failures.append(
            f"{CASE_ID}: src/driver builds {driver.count('add_executable(')} "
            "executables, expected 3")
    for name in ("tc", "tc-vm", "tc-aot"):
        if f"add_executable({name} " not in driver:
            failures.append(f"{CASE_ID}: missing CLI entry point {name}")
    sources = list((ROOT / "src").rglob("*.c"))
    mains = []
    for path in sources:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r"\bint\s+main\s*\(", text):
            mains.append(str(path.relative_to(ROOT)))
    if sorted(mains) != ["src/driver/tc-aot.c", "src/driver/tc-vm.c",
                         "src/driver/tc.c"]:
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
        output = run_nm([nm, "-u", str(elf)], failures,
                        "undefined-symbol audit")
        if output is None:
            return
        undefined = [line.split()[-1] for line in
                     output.decode("utf-8", "replace").splitlines()
                     if line.split()[:1] == ["U"]]
        if undefined:
            failures.append(
                f"{CASE_ID}: freestanding image has undefined symbols: "
                f"{undefined[:8]}")
        forbidden = {"printf", "malloc", "free", "strlen", "memcmp",
                     "abort", "exit", "__libc_start_main"}
        output = run_nm([nm, str(elf)], failures, "defined-symbol audit")
        if output is None:
            return
        defined = {line.split()[-1] for line in
                   output.decode("utf-8", "replace").splitlines()
                   if len(line.split()) >= 3}
        leaked = forbidden & defined
        if leaked:
            failures.append(
                f"{CASE_ID}: libc symbols in freestanding image: "
                f"{sorted(leaked)}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    versions = {
        "tc": ("TC_PROJECT", "tc 0.0.47 (language 0.0.46; project tool)\n"),
        "tc-vm": ("TC_VM", "tc-vm 0.0.47 (language 0.0.46; reference VM)\n"),
        "tc-aot": ("TC_AOT", "tc-aot 0.0.47 (language 0.0.46; native AOT)\n"),
    }
    for name, (env, expected) in versions.items():
        proc = subprocess.run([tool(name, env), "--version"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=30)
        if proc.returncode != 0 or proc.stderr or \
                proc.stdout.decode("utf-8", "replace") != expected:
            failures.append(f"{CASE_ID}: {name} has an incorrect dual-version line")


def check_docs(failures: list[str]) -> None:
    required = {
        "README.md": ["riscv32-baremetal", "RISC-V", "t2.debug", "t2.gdb",
                      "--dump-linemap", "--gdb-stdout-journal", "vm.gdb.protocol"],
        "README.en.md": ["riscv32", "RISC-V", "t2.debug", "t2.gdb",
                         "--dump-linemap", "--gdb-stdout-journal", "vm.gdb.protocol"],
        "AGENTS.md": ["riscv32", "S10", "t2.debug", "t2.gdb",
                      "--gdb-stdout-journal", "vm.gdb.protocol"],
        "src/README.md": ["riscv32", "--dump-linemap", "DWARF v4",
                          "--gdb-stdout-journal", "gdb_rsp"],
        "tests/README.md": ["S10", "riscv32", "t2.debug", "t2.gdb",
                            "gdb_journal.unit", "vm.gdb.protocol",
                            "vm.gdb.semantics", "vm.gdb.e2e"],
        "docs/README.md": ["RISC-V", "riscv32", "t2.debug", "t2.gdb",
                           "vm.gdb.protocol"],
        "docs/gates/README.md": ["G10", "PASS", "t2.debug", "t2.gdb",
                                 "vm.gdb.protocol"],
        "src/CONTRACTS.md": ["riscv32", "TC_DEBUG_CODE_BASE",
                             "--dump-linemap"],
    }
    for relative, needles in required.items():
        text = read(relative)
        for needle in needles:
            if needle not in text:
                failures.append(f"{CASE_ID}: {relative} lacks {needle!r}")
    if "TC开发调试工具链-0.0.47.md" not in read("docs/README.md"):
        failures.append(f"{CASE_ID}: docs/README.md lacks the toolchain design link")
    stale = {
        "README.md": ["未实现**：RISC-V", "RISC-V 等后续目标",
                      "P4b、P5 与 T3–T6 尚未开始",
                      "`--dump-linemap` 尚未开始"],
        "README.en.md": ["Not implemented: later targets such as RISC-V",
                         "P4b, P5 and T3–T6 have not started",
                         "`--dump-linemap` have not started"],
        "AGENTS.md": ["未实现：RISC-V", "P4b、P5 与 T3–T6 尚未开始"],
        "src/README.md": ["riscv32 not implemented", "remains a future target",
                          "Public `tc-aot --debug`/`--dump-linemap`"],
        "tests/README.md": ["0.0.47 CTest 注册总数 83"],
        "docs/README.md": ["T3–T6 尚未开始"],
        "CONTRIBUTING.md": ["P4b、P5 与 T3–T6 尚未开始"],
        "CONTRIBUTING.en.md": ["P4b, P5 and T3–T6 have not started"],
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
    if '#define TC_LANGUAGE_VERSION "0.0.46"' not in version_h:
        failures.append(f"{CASE_ID}: language version is not 0.0.46")
    if '#define TC_TOOLCHAIN_VERSION "0.0.47"' not in version_h:
        failures.append(f"{CASE_ID}: toolchain version is not 0.0.47")
    if not re.search(r"(?m)^\s*project\s*\([^)]*\bVERSION\s+0\.0\.47(?:\s|\))",
                     read("CMakeLists.txt")):
        failures.append(f"{CASE_ID}: CMake project version is not 0.0.47")
    for changelog in ("CHANGELOG.md", "CHANGELOG.en.md"):
        text = read(changelog)
        if "0.0.46" not in text:
            failures.append(f"{CASE_ID}: {changelog} lacks the language version")
        if len(re.findall(r"(?m)^## \[0\.0\.47\](?:\s|$)", text)) != 1:
            failures.append(
                f"{CASE_ID}: {changelog} lacks one exact 0.0.47 heading")
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


def check_g11_evidence(failures: list[str], artifact_dir: Path,
                       repo: str) -> None:
    sys.path.insert(0, str(HERE))
    from g11_evidence import signer_workflow, verify_evidence  # noqa: E402

    try:
        proc = subprocess.run(["git", "rev-parse", "HEAD"],
                              cwd=ROOT, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=30)
    except (OSError, subprocess.SubprocessError) as error:
        failures.append(f"{CASE_ID}: cannot determine audited commit: {error}")
        return
    commit = proc.stdout.decode("ascii", "replace").strip()
    if proc.returncode != 0 or re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        failures.append(f"{CASE_ID}: cannot determine audited commit")
        return
    try:
        workflow = signer_workflow(repo)
    except ValueError as error:
        failures.append(f"{CASE_ID}: {error}")
        return
    failures.extend(verify_evidence(artifact_dir, repo, workflow, commit))


def selftest() -> int:
    original_read = read

    def run_with_read(target: str, replacement, checker) -> list[str]:
        failures: list[str] = []

        def mutated(relative: str) -> str:
            text = original_read(relative)
            return replacement(text) if relative == target else text

        globals()["read"] = mutated
        try:
            checker(failures)
        finally:
            globals()["read"] = original_read
        return failures

    for checker in (check_codes, check_release_surface, check_docs):
        failures: list[str] = []
        checker(failures)
        if failures:
            for failure in failures:
                print(failure)
            return 1

    mutations = (
        ("src/diagnostic/codes.inc",
         lambda text: re.sub(r"\bTC_CE_[A-Z0-9_]+\b", "TC_CE_MUTATED",
                             text, count=1), check_codes),
        ("src/CMakeLists.txt",
         lambda text: text.replace("target_include_directories(tc_core PRIVATE",
                                   "target_include_directories(tc_core PUBLIC"),
         check_release_surface),
        ("CMakeLists.txt",
         lambda text: text.replace("VERSION 0.0.47", "VERSION 0.0.470"),
         check_docs),
        ("CHANGELOG.md",
         lambda text: text.replace("## [0.0.47]", "### [0.0.47]"),
         check_docs),
        ("CHANGELOG.en.md",
         lambda text: text.replace("## [0.0.47]", "### [0.0.47]"),
         check_docs),
        ("docs/README.md",
         lambda text: text.replace("TC开发调试工具链-0.0.47.md", "missing.md"),
         check_docs),
        ("README.md",
         lambda text: text.replace("t2.gdb", "missing-t2-gdb"),
         check_docs),
        ("README.md",
         lambda text: text.replace("vm.gdb.protocol", "missing-vm-gdb-protocol"),
         check_docs),
        ("tests/README.md",
         lambda text: text.replace("t2.debug", "missing-t2-debug"),
         check_docs),
    )
    for target, replacement, checker in mutations:
        failures = run_with_read(target, replacement, checker)
        if not failures:
            print(f"release audit selftest: mutation was not rejected: {target}")
            return 1

    original_run = subprocess.run
    failures = []
    try:
        subprocess.run = lambda *args, **kwargs: subprocess.CompletedProcess(
            args[0], 1, stdout=b"", stderr=b"nm failed")
        output = run_nm(["nm"], failures, "undefined-symbol audit")
    finally:
        subprocess.run = original_run
    if output is not None or not failures:
        print("release audit selftest: nm failure was not rejected")
        return 1
    print("release audit selftest: all mutations rejected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--selftest", action="store_true")
    parser.add_argument("--require-g11-evidence", type=Path)
    parser.add_argument("--g11-repo", default=os.environ.get("GITHUB_REPOSITORY"))
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    failures: list[str] = []
    check_codes(failures)
    check_release_surface(failures)
    check_artifacts(failures)
    check_docs(failures)
    if args.require_g11_evidence is not None:
        if not args.g11_repo:
            failures.append(
                f"{CASE_ID}: --g11-repo or GITHUB_REPOSITORY is required with "
                "--require-g11-evidence")
        else:
            check_g11_evidence(failures, args.require_g11_evidence.resolve(),
                               args.g11_repo)
    if failures:
        for item in failures:
            print(item)
        print(f"{CASE_ID}: {len(failures)} release-audit failure(s)")
        return 1
    print(f"{CASE_ID}: release audit passed (codes, private core, three CLIs, "
          "freestanding symbols, docs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
