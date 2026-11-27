#!/usr/bin/env python3
"""P4a shared-line-map, AOT location and DWARF v4 regression checks."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CASE_ID = "TC-AOT-DEBUG-P4A-0001"
CHECKS = 0
SOURCE_NAME = 'src/ma"in\\lib.tc'
SOURCE = """#program
import lib
var a: int32 = copy int32, 7
var b: int32 = add int32, a, 5
var c: int32 = call lib.f, b
_ = writeln int32, c
"""
LIBRARY = """#lib
public func f(x: int32) int32 then
var y: int32 = add int32, x, 1
_ = return y
end
"""
PROFILES = {
    "aarch64-linux-user": (8, "elf64-littleaarch64"),
    "aarch64-baremetal": (8, "elf64-littleaarch64"),
    "arm32-baremetal": (4, "elf32-littlearm"),
    "riscv32-baremetal": (4, "elf32-littleriscv"),
}


def check(condition: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def project(base: Path, name: str) -> Path:
    root = base / name / "project"
    write(root / "tc.project",
          'tc-project 1\nname = "p4a-debug"\ninclude = "src"\n'
          'include = "../external"\n')
    write(root / SOURCE_NAME, SOURCE)
    write(base / name / "external/lib.tc", LIBRARY)
    return root


def run(command: list[str], *, cwd: Path, env: dict[str, str] | None = None,
        expected: int = 0, preexec_fn=None) -> subprocess.CompletedProcess:
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                            timeout=120, preexec_fn=preexec_fn)
    check(result.returncode == expected,
          f"{command!r} exited {result.returncode}, expected {expected}: "
          f"stdout={result.stdout!r} stderr={result.stderr!r}")
    return result


def fixture_run(helper: str, root: Path, output: Path, profile: str,
                emit: str, keep: int, linemap: Path | None = None,
                expected: int = 0) -> subprocess.CompletedProcess:
    temporary = root / "tmp"
    temporary.mkdir(exist_ok=True)
    for item in temporary.iterdir():
        if item.is_dir() and not item.is_symlink():
            shutil.rmtree(item)
        else:
            item.unlink()
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(temporary)})
    command = [helper, str(root / SOURCE_NAME), str(output), profile, emit,
               str(keep)]
    if linemap is not None:
        command.append(str(linemap))
    return run(command, cwd=root.parent, env=env, expected=expected)


def asm_quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_assembly(path: Path, data: dict, temporary: Path) -> None:
    text = path.read_text(encoding="utf-8")
    entries = data["entries"]
    files = list(dict.fromkeys(entry["file"] for entry in entries))
    check(data["format"] == "tc-linemap" and data["version"] == 3,
          "linemap schema identity/version")
    check(data["source"] == SOURCE_NAME, "root display path in linemap")
    check(list(data) == ["format", "version", "profile", "source", "entries"],
          "linemap top-level key order")
    for entry in entries:
        expected_keys = ["kind", "module", "func", "bb", "insn", "file",
                         "line", "col"]
        if entry.get("external") is True:
            expected_keys.append("external")
        check(list(entry) == expected_keys, "linemap entry key order")
    check(text.count("  .loc ") == len(entries),
          "one .loc per finalized IR instruction")
    check(text.count("  # source: ") == len(entries),
          "one source comment per finalized IR instruction")
    for index, name in enumerate(files, start=1):
        check(f"  .file {index} {asm_quote(name)}\n" in text,
              f"deterministic .file entry {index}")
    locations = [(files.index(entry["file"]) + 1, entry["line"], entry["col"])
                 for entry in entries]
    decoded = [(int(file), int(line), int(column)) for file, line, column in
               re.findall(r"^  \.loc (\d+) (\d+) (\d+)$", text, re.MULTILINE)]
    check(decoded == locations, "assembly locations follow final IR order")
    comments = re.findall(r"^  # source: (.*):(\d+)$", text, re.MULTILINE)
    check(comments == [(entry["file"], str(entry["line"])) for entry in entries],
          "source comments follow final IR order")
    check(any(entry.get("external") is True for entry in entries),
          "external import marked in linemap")
    check(str(path.parent.resolve()).encode() not in path.read_bytes(),
          "absolute fixture root leaked into assembly")


def check_dwarfdump(dwarfdump: str, obj: Path, profile: str) -> None:
    pointer_size, file_format = PROFILES[profile]
    run([dwarfdump, "--verify", str(obj)], cwd=ROOT)
    info = run([dwarfdump, "--debug-info", str(obj)], cwd=ROOT)
    line_table = run([dwarfdump, "--debug-line", str(obj)], cwd=ROOT)
    text = info.stdout.decode("utf-8", "replace")
    lines = line_table.stdout.decode("utf-8", "replace")
    check(file_format in text, f"{profile}: ELF format")
    check("format = DWARF32" in text and "version = 0x0004" in text,
          f"{profile}: DWARF v4 CU")
    check("format: DWARF32" in lines and re.search(r"^\s*version: 4$", lines,
                                                  re.MULTILINE) is not None,
          f"{profile}: DWARF v4 line table")
    check(f"addr_size = 0x0{pointer_size}" in text,
          f"{profile}: DWARF address size")
    check("DW_TAG_compile_unit" in text, f"{profile}: compile unit DIE")
    check("DW_AT_stmt_list" in text and "DW_AT_comp_dir" in text and
          "DW_AT_name" in text and "DW_AT_low_pc" in text and
          "DW_AT_high_pc" in text, f"{profile}: CU attributes")
    compilation_dir = next((line for line in text.splitlines()
                            if "DW_AT_comp_dir" in line), "")
    check(compilation_dir.strip() == 'DW_AT_comp_dir\t(".")',
          f"{profile}: fixed compilation dir: {compilation_dir!r}")
    check(re.search(r'DW_AT_name\s+\(' +
                    re.escape(json.dumps(SOURCE_NAME)) + r'\)', text) is not None,
          f"{profile}: root display name")
    check(lines.count(".debug_line contents:") == 1,
          f"{profile}: exactly one assembler-generated line table")
    rows = []
    for line in lines.splitlines():
        if re.match(r"^0x[0-9a-f]+\s+\d+\s+\d+\s+\d+", line):
            rows.append(int(line.split()[0], 16))
    check(len(rows) >= 2, f"{profile}: decoded source rows")
    check(all(left < right for left, right in zip(rows, rows[1:])),
          f"{profile}: source row addresses strictly increase")


def check_profile(helper: str, dwarfdump: str, root: Path,
                  profile: str) -> None:
    output = root / f"{profile}.s"
    linemap = root / f"{profile}.json"
    fixture_run(helper, root, output, profile, "asm", 1, linemap)
    data = json.loads(linemap.read_text(encoding="utf-8"))
    check(data["profile"] == profile, "linemap profile identity")
    check_assembly(output, data, root / "tmp")
    obj = root / f"{profile}.o"
    fixture_run(helper, root, obj, profile, "obj", 0)
    check_dwarfdump(dwarfdump, obj, profile)


def collect_cross_root(helper: str, dwarfdump: str, root: Path) -> dict:
    profile = "aarch64-baremetal"
    artifacts = {}
    for emit in ("asm", "obj", "elf", "flat"):
        output = root / f"cross.{emit}"
        fixture_run(helper, root, output, profile, emit, 1)
        data = output.read_bytes()
        check(str(root.resolve()).encode() not in data,
              f"cross-root {emit}: absolute root leaked")
        if emit == "flat":
            check(b".debug_info" not in data and b".debug_line" not in data,
                  "flat image has no DWARF sections")
        assemblies = list((root / "tmp").glob("tc-aot-*.s"))
        check(bool(assemblies), f"cross-root {emit}: intermediate assembly")
        if emit == "asm":
            assembly_data = data
        else:
            matches = [item for item in assemblies
                       if b".loc " in item.read_bytes()]
            check(len(matches) == 1,
                  f"cross-root {emit}: debug intermediate assembly")
            assembly_data = matches[0].read_bytes()
        artifacts[emit] = (data, assembly_data)
        if emit == "elf":
            check_dwarfdump(dwarfdump, output, profile)
    repeated = root / "repeat.asm"
    fixture_run(helper, root, repeated, profile, "asm", 0)
    check(repeated.read_bytes() == (root / "cross.asm").read_bytes(),
          "repeated debug assembly is byte-identical")
    return artifacts


def check_cross_root(helper: str, dwarfdump: str, first: Path,
                     second: Path) -> None:
    first_artifacts = collect_cross_root(helper, dwarfdump, first)
    second_artifacts = collect_cross_root(helper, dwarfdump, second)
    for emit, (first_data, first_asm) in first_artifacts.items():
        second_data, second_asm = second_artifacts[emit]
        check(first_data == second_data, f"cross-root {emit} bytes differ")
        check(first_asm == second_asm, f"cross-root {emit} assembly differs")


def check_output_types(helper: str, root: Path) -> None:
    source = root / SOURCE_NAME
    directory = root / "directory-output"
    directory.mkdir()
    fifo = root / "fifo-output"
    os.mkfifo(fifo)
    for target in (directory, fifo):
        fixture_run(helper, root, target, "aarch64-baremetal", "asm", 0,
                    expected=2)
    alias = root / "source-alias.s"
    alias.symlink_to(source)
    before = source.read_bytes()
    fixture_run(helper, root, alias, "aarch64-baremetal", "asm", 0,
                expected=2)
    check(source.read_bytes() == before, "source alias was not overwritten")
    alias.unlink()
    hardlink = root / "source-hardlink.s"
    os.link(source, hardlink)
    fixture_run(helper, root, hardlink, "aarch64-baremetal", "asm", 0,
                expected=2)
    check(source.read_bytes() == before, "source hardlink was not overwritten")
    hardlink.unlink()
    import_alias = root / "import-alias.s"
    os.link(root.parent / "external/lib.tc", import_alias)
    library_before = (root.parent / "external/lib.tc").read_bytes()
    fixture_run(helper, root, import_alias, "aarch64-baremetal", "asm", 0,
                expected=2)
    check((root.parent / "external/lib.tc").read_bytes() == library_before,
          "import hardlink was not overwritten")
    import_alias.unlink()
    manifest_alias = root / "manifest-alias.s"
    os.link(root / "tc.project", manifest_alias)
    manifest_before = (root / "tc.project").read_bytes()
    fixture_run(helper, root, manifest_alias, "aarch64-baremetal", "asm", 0,
                expected=2)
    check((root / "tc.project").read_bytes() == manifest_before,
          "manifest hardlink was not overwritten")
    manifest_alias.unlink()
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(root / "tmp")})
    command = [helper, str(source), "-", "aarch64-baremetal", "asm", "0"]
    with source.open("ab") as stream:
        result = subprocess.run(command, cwd=root.parent, env=env,
                                stdout=stream, stderr=subprocess.PIPE,
                                timeout=120)
    check(result.returncode == 2 and source.read_bytes() == before,
          "default stdout source alias was not rejected")
    command = [helper, str(source), str(root / "stdio.s"),
               "aarch64-baremetal", "asm", "0"]
    with source.open("ab") as stream:
        result = subprocess.run(command, cwd=root.parent, env=env,
                                stdout=subprocess.PIPE, stderr=stream,
                                timeout=120)
    check(result.returncode == 2 and source.read_bytes() == before and
          result.stdout == b"", "stderr source alias was not rejected")
    command = [helper, str(source), "/dev/stdout",
               "aarch64-baremetal", "asm", "0"]
    result = subprocess.run(command, cwd=root.parent, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=120)
    check(result.returncode == 0 and b"  .loc " in result.stdout,
          "explicit formal stdout pipe was rejected")


def check_cli_guards(helper: str, root: Path, temporary: Path) -> None:
    aot = str(Path(helper).parent.parent / "src" / "driver" / "tc-aot")
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(temporary)})
    base = [aot, "--profile=aarch64-baremetal", "--emit=asm"]
    source = root / SOURCE_NAME

    first = root / "plain.s"
    second = root / "plain-repeat.s"
    run(base + [f"--output={first}", str(source)], cwd=root.parent, env=env)
    run(base + [f"--output={second}", str(source)], cwd=root.parent, env=env)
    plain = first.read_bytes()
    check(plain == second.read_bytes(), "non-debug assembly is deterministic")
    check(b".loc " not in plain and b".file " not in plain and
          b".debug_info" not in plain, "non-debug assembly has no debug records")

    bad = project(temporary, "invalid")
    bad_source = bad / SOURCE_NAME
    write(bad_source, "import missing\nvar =\n")
    before = bad_source.read_bytes()
    alias = bad / "invalid-alias.s"
    os.link(bad_source, alias)
    result = run(base + [f"--output={alias}", str(bad_source)],
                 cwd=bad.parent, env=env, expected=2)
    check(result.stdout == b"" and result.stderr == b"" and
          bad_source.read_bytes() == before,
          "diagnostic plus output alias must be silent usage error")
    alias.unlink()

    with bad_source.open("ab") as stream:
        result = subprocess.run(base + [str(bad_source)], cwd=bad.parent, env=env,
                                stdout=stream, stderr=subprocess.PIPE, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before,
          "diagnostic plus stdout alias must not overwrite source")
    explicit = bad / "invalid-output.s"
    with bad_source.open("ab") as stream:
        result = subprocess.run(base + [f"--output={explicit}", str(bad_source)],
                                cwd=bad.parent, env=env, stdout=subprocess.PIPE,
                                stderr=stream, timeout=120)
    check(result.returncode == 2 and result.stdout == b"" and
          bad_source.read_bytes() == before and not explicit.exists(),
          "diagnostic plus stderr alias must be silent and leave no output")

    potential = bad / "src/missing.tc"
    run(base + [f"--output={potential}", str(bad_source)], cwd=bad.parent,
        env=env, expected=2)
    check(not potential.exists(), "potential import reservation was not removed")
    unrelated = bad / "unrelated.tc"
    run(base + [f"--output={unrelated}", str(bad_source)], cwd=bad.parent,
        env=env, expected=1)
    check(not unrelated.exists(), "unrelated .tc output was treated as an import")

    result = subprocess.run(base + [str(source)], cwd=root.parent, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120)
    check(result.returncode == 2 and result.stdout == b"",
          "merged stdout/stderr must be rejected silently")

    def close_descriptor(descriptor: int):
        def close() -> None:
            os.close(descriptor)
        return close

    closed_stdin = root / "closed-stdin.s"
    run(base + [f"--output={closed_stdin}", str(source)], cwd=root.parent,
        env=env, preexec_fn=close_descriptor(0))
    closed_stderr = root / "closed-stderr.s"
    run(base + [f"--output={closed_stderr}", str(source)], cwd=root.parent,
        env=env, preexec_fn=close_descriptor(2))
    run(base + [str(bad_source)], cwd=bad.parent, env=env, expected=70,
        preexec_fn=close_descriptor(2))
    run(base + [str(source)], cwd=root.parent, env=env, expected=70,
        preexec_fn=close_descriptor(1))

    vm = str(Path(helper).parent.parent / "src" / "driver" / "tc-vm")
    with bad_source.open("ab") as stream:
        result = subprocess.run([vm, str(bad_source)], cwd=bad.parent, env=env,
                                stdout=stream, stderr=subprocess.PIPE, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before,
          "VM diagnostic plus stdout alias must not overwrite source")
    with bad_source.open("ab") as stream:
        result = subprocess.run([vm, str(bad_source)], cwd=bad.parent, env=env,
                                stdout=subprocess.PIPE, stderr=stream, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before and
          result.stdout == b"", "VM stderr alias must be silent")
    result = subprocess.run([vm, str(source)], cwd=root.parent, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120)
    check(result.returncode == 2 and result.stdout == b"",
          "VM merged stdout/stderr must be rejected silently")


def check_early_guards(aot: str, vm: str, root: Path,
                       temporary: Path) -> None:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(temporary)})
    base = [aot, "--profile=aarch64-baremetal", "--emit=asm"]
    source = root / SOURCE_NAME

    malformed = project(temporary, "malformed")
    manifest = malformed / "tc.project"
    write(manifest, 'tc-project 1\nname = "unterminated\n')
    main = malformed / SOURCE_NAME
    write(main, SOURCE)
    alias = malformed / "stderr-alias"
    os.link(manifest, alias)
    before = manifest.read_bytes()
    with alias.open("ab") as stream:
        result = subprocess.run([vm, str(main)], cwd=malformed.parent, env=env,
                                stdout=subprocess.PIPE, stderr=stream,
                                timeout=120)
    check(result.returncode == 2 and result.stdout == b"" and
          manifest.read_bytes() == before,
          "malformed manifest stderr alias must be silent and unchanged")
    alias.unlink()

    bad = project(temporary, "early-invalid")
    bad_source = bad / SOURCE_NAME
    write(bad_source, "import missing\nvar =\n")
    before = bad_source.read_bytes()
    with bad_source.open("ab") as stream:
        result = subprocess.run(base + ["--syntax-only", str(bad_source)],
                                cwd=bad.parent, env=env, stdout=stream,
                                stderr=subprocess.PIPE, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before,
          "syntax-only stdout alias must not overwrite source")
    with bad_source.open("ab") as stream:
        result = subprocess.run(base + ["--profile=invalid", str(bad_source)],
                                cwd=bad.parent, env=env, stdout=subprocess.PIPE,
                                stderr=stream, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before and
          result.stdout == b"", "profile error stderr alias must be silent")
    with bad_source.open("ab") as stream:
        result = subprocess.run(
            base + ["--device=invalid", str(bad_source)], cwd=bad.parent,
            env=env, stdout=subprocess.PIPE, stderr=stream, timeout=120)
    check(result.returncode == 2 and bad_source.read_bytes() == before and
          result.stdout == b"", "device error stderr alias must be silent")

    unusual = bad / "main.source"
    write(unusual, "var =\n")
    unusual_alias = bad / "unusual-alias.s"
    os.link(unusual, unusual_alias)
    unusual_before = unusual.read_bytes()
    run(base + [f"--output={unusual_alias}", str(unusual)], cwd=bad.parent,
        env=env, expected=2)
    check(unusual.read_bytes() == unusual_before,
          "failed root without .tc suffix must remain protected")
    unusual_alias.unlink()

    chained = project(temporary, "chained")
    chained_source = chained / SOURCE_NAME
    write(chained_source, "#program\nimport lib\nvar =\n")
    canonical = chained.parent / "canonical"
    write(canonical / "lib.tc", "#lib\nimport missing\nvar =\n")
    (chained / "src/lib.tc").symlink_to(canonical / "lib.tc")
    potential = canonical / "missing.tc"
    run(base + [f"--output={potential}", str(chained_source)],
        cwd=chained.parent, env=env, expected=2)
    check(not potential.exists(),
          "failed symlink import must scan its canonical directory")

    continued = project(temporary, "continued")
    continued_source = continued / SOURCE_NAME
    write(continued_source, "#program\nimport \\\nmissing\nvar =\n")
    potential = continued / "src/missing.tc"
    run(base + [f"--output={potential}", str(continued_source)],
        cwd=continued.parent, env=env, expected=2)
    check(not potential.exists(), "continued import candidate must be protected")
    potential.unlink(missing_ok=True)
    write(continued_source, "#program\n/// import missing\nvar =\n")
    run(base + [f"--output={potential}", str(continued_source)],
        cwd=continued.parent, env=env, expected=1)
    check(not potential.exists(), "commented import must not reserve output")

    target = root / "dangling-target.s"
    link = root / "dangling-output.s"
    link.symlink_to(target.name)
    run(base + [f"--output={link}", str(source)], cwd=root.parent, env=env)
    check(link.is_symlink() and target.stat().st_size > 0 and
          b"  .loc " not in target.read_bytes(),
          "dangling output symlink must create its regular referent")
    target.unlink()
    first = root / "cycle-a.s"
    second = root / "cycle-b.s"
    first.symlink_to(second.name)
    second.symlink_to(first.name)
    run(base + [f"--output={first}", str(source)], cwd=root.parent, env=env,
        expected=2)
    source_alias = root / "source-output.s"
    source_alias.symlink_to(source)
    source_before = source.read_bytes()
    run(base + [f"--output={source_alias}", str(source)], cwd=root.parent,
        env=env, expected=2)
    check(source.read_bytes() == source_before,
          "output symlink to source must not overwrite source")

    result = run([aot, "--profile=aarch64-baremetal", "--emit=obj",
                  str(bad_source)], cwd=bad.parent, env=env, expected=2)
    check(result.stdout == b"" and
          b"--output is required for --emit=obj/elf/flat" in result.stderr and
          b"error[SYN]" not in result.stderr,
          "missing non-asm output must precede source diagnostics")

    for tool in (aot, vm):
        partial_output = bad / f"{Path(tool).name}-partial.s"
        result = run([tool, "--syntax-only", f"--output={partial_output}",
                      str(bad_source)], cwd=bad.parent, env=env, expected=2)
        check(not partial_output.exists() and b"error[SYN]" not in result.stderr,
              f"{Path(tool).name} partial output must be rejected before parsing")
        with bad_source.open("ab") as stream:
            result = subprocess.run([tool, "--help", str(bad_source)],
                                    cwd=bad.parent, env=env, stdout=stream,
                                    stderr=subprocess.PIPE, timeout=120)
        check(result.returncode == 2 and bad_source.read_bytes() == before and
              result.stderr == b"",
              f"{Path(tool).name} help stdout alias must be silent: "
              f"exit={result.returncode} stderr={result.stderr!r} "
              f"before={before!r} after={bad_source.read_bytes()!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("helper")
    parser.add_argument("dwarfdump")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    temporary = Path(tempfile.mkdtemp(prefix="tc-p4a-", dir=ROOT / ".tmp"))
    try:
        first = project(temporary, "first")
        second = project(temporary, "second")
        for profile in PROFILES:
            check_profile(args.helper, args.dwarfdump, first, profile)
        check_cross_root(args.helper, args.dwarfdump, first, second)
        check_output_types(args.helper, first)
        check_cli_guards(args.helper, first, temporary)
        check_early_guards(
            str(Path(args.helper).parent.parent / "src" / "driver" / "tc-aot"),
            str(Path(args.helper).parent.parent / "src" / "driver" / "tc-vm"),
            first, temporary)
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(f"{CASE_ID}: FAIL: {error}")
        if args.keep:
            print(f"kept {temporary}")
            return 1
        shutil.rmtree(temporary, ignore_errors=True)
        return 1
    shutil.rmtree(temporary, ignore_errors=True)
    print(f"{CASE_ID}: shared line map, AOT locations, DWARF v4 and "
          f"cross-root artifacts passed ({CHECKS} checks)")
    print("evidence-result=toolchain.aot_debug/aot-debug:four-profile-dwarf "
          f"assertions={CHECKS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
