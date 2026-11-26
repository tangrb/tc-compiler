#!/usr/bin/env python3
"""P2 project-root-relative path and source-boundary regression checks."""

from __future__ import annotations

import argparse
import ctypes
import errno
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PROGRAM = "#program\n_ = writeln int32, 1\n"
LIBRARY = "#lib\npublic const K: int32 = 1\n"
BAD_PROGRAM = "#program\nvar x: int32 = \n"
TRAP_PROGRAM = """#program
var x: int32 = copy int32, 1
var p: ptr<int32> = addr int32, x
unsafe then
var q: ptr<int32> = ptradd int32, p, 1
var v: int32 = load int32, q
_ = writeln int32, v
end
"""
FAILS = 0


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def run(tool: str, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    return subprocess.run([tool, *args], cwd=cwd, env=env,
                          capture_output=True, timeout=30)


def write(path: Path, text: str | bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(text, bytes):
        path.write_bytes(text)
    else:
        path.write_text(text, encoding="utf-8")
    return path


def project(root: Path, includes: list[str] | None = None) -> Path:
    lines = ["tc-project 1", 'name = "path-test"']
    if includes is None:
        lines.append('include = "src"')
    else:
        lines.extend(f'include = "{value}"' for value in includes)
    write(root / "tc.project", "\n".join(lines) + "\n")
    return root


def no_absolute(result: subprocess.CompletedProcess, root: Path, label: str) -> None:
    check(str(root).encode() not in result.stdout + result.stderr,
          f"{label}: absolute temporary path leaked")


def check_discovery_and_display(tool: str, base: Path) -> None:
    root = project(base / "project")
    source = write(root / "src/main.tc", BAD_PROGRAM)
    elsewhere = base / "elsewhere"
    elsewhere.mkdir()
    result = run(tool, ["--syntax-only", str(source)], elsewhere)
    check(result.returncode != 0, "syntax-only rejects invalid root")
    check(result.stderr.startswith(b"src/main.tc:"),
          f"cross-cwd diagnostic is root-relative: {result.stderr!r}")
    no_absolute(result, base, "cross-cwd diagnostic")

    alias = write(elsewhere / "alias.tc", PROGRAM)
    alias.unlink()
    alias.symlink_to(source)
    result = run(tool, ["--syntax-only", str(alias)], elsewhere)
    check(result.returncode != 0 and result.stderr.startswith(b"src/main.tc:"),
          "source symlink displays canonical project-relative target")
    no_absolute(result, base, "source symlink")

    nested = project(root / "nested")
    write(nested / "src/main.tc", BAD_PROGRAM)
    result = run(tool, ["--syntax-only", str(nested / "src/main.tc")], elsewhere)
    check(result.stderr.startswith(b"src/main.tc:"),
          "nearest nested manifest remains the display anchor")


def check_include_order(tool: str, base: Path) -> None:
    root = project(base / "project", ["first", "missing"])
    source = write(root / "src/main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
    write(root / "first/lib.tc", BAD_PROGRAM.replace("#program", "#lib"))
    cli = base / "cli"
    write(cli / "lib.tc", BAD_PROGRAM.replace("#program", "#lib"))
    result = run(tool, ["--sem-only", f"--include={cli}", str(source)], base)
    check(result.returncode != 0 and b"first/lib.tc:" in result.stderr,
          "manifest include order precedes CLI include")
    check(b"../cli/lib.tc:" not in result.stderr,
          "later include root is not parsed after first match")
    no_absolute(result, base, "manifest include")

    (root / "first/lib.tc").unlink()
    result = run(tool, ["--sem-only", f"--include={cli}", str(source)], base)
    check(result.returncode != 0 and b"../cli/lib.tc:" in result.stderr,
          "external CLI include uses ../ display path")
    no_absolute(result, base, "external include")

    default = project(base / "default")
    source = write(default / "src/main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
    write(default / "src/lib.tc", LIBRARY)
    result = run(tool, [str(source)], base)
    check(result.returncode == 0 and result.stdout == b"1\n",
          "manifest without include supplies default src root")

    bare = base / "bare"
    source = write(bare / "main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
    write(bare / "src/lib.tc", LIBRARY)
    result = run(tool, [str(source)], base)
    check(result.returncode != 0 and b"TC_CE_IMPORT_NOT_FOUND" in result.stderr,
          "no-manifest compilation does not add default src root")


def check_root_limit(tool: str, base: Path) -> None:
    root = project(base / "project", [f"missing{i}" for i in range(31)])
    source = write(root / "src/main.tc", PROGRAM)
    result = run(tool, [f"--include={base / 'extra'}", str(source)], base)
    check(result.returncode == 0, "31 manifest roots plus one CLI root accepted")
    result = run(tool, [f"--include={base / 'extra1'}",
                        f"--include={base / 'extra2'}", str(source)], base)
    check(result.returncode == 2, "33 total include roots rejected before access")

    root = project(base / "default-count")
    source = write(root / "src/main.tc", PROGRAM)
    args = [f"--include={base / f'absent{i}'}" for i in range(32)]
    result = run(tool, [*args, str(source)], base)
    check(result.returncode == 2, "default src plus 32 CLI roots rejected")

    bare = base / "bare-count"
    source = write(bare / "main.tc", PROGRAM)
    args = [f"--include={base / f'none{i}'}" for i in range(32)]
    result = run(tool, [*args, str(source)], base)
    check(result.returncode == 0, "32 CLI roots accepted without manifest")


def check_include_errors(tool: str, base: Path) -> None:
    for kind in ("file", "fifo", "dangling", "loop", "enotdir", "too-long",
                 "dangling-child"):
        root = project(base / kind, [kind])
        source = write(root / "src/main.tc", PROGRAM)
        candidate = root / kind
        if kind == "file":
            write(candidate, b"not a directory")
        elif kind == "fifo":
            os.mkfifo(candidate)
        elif kind == "dangling":
            candidate.symlink_to("missing")
        elif kind == "dangling-child":
            candidate.symlink_to("missing")
            (root / "tc.project").write_text(
                'tc-project 1\nname = "path-test"\ninclude = "dangling-child/child"\n',
                encoding="utf-8")
        elif kind == "loop":
            candidate.symlink_to(kind)
        elif kind == "enotdir":
            write(candidate, b"file")
            (root / "tc.project").write_text(
                f'tc-project 1\nname = "path-test"\ninclude = "{kind}/child"\n',
                encoding="utf-8")
        else:
            (root / "tc.project").write_text(
                'tc-project 1\nname = "path-test"\ninclude = "' +
                "x" * 300 + '"\n', encoding="utf-8")
        result = run(tool, [str(source)], base)
        check(result.returncode == 2, f"invalid include root {kind} exits 2")

    root = project(base / "absent", ["absent"])
    source = write(root / "src/main.tc", "#program\nimport lib\n")
    result = run(tool, [str(source)], base)
    check(result.returncode != 0 and b"TC_CE_IMPORT_NOT_FOUND" in result.stderr,
          "nonexistent include root is an empty search candidate")

    root = project(base / "denied", ["denied/child"])
    source = write(root / "src/main.tc", PROGRAM)
    denied = root / "denied"
    (denied / "child").mkdir(parents=True)
    denied.chmod(0)
    try:
        result = run(tool, [str(source)], base)
        check(result.returncode == 70, "inaccessible include root exits 70")
    finally:
        denied.chmod(0o700)

    root = project(base / "invalid-text")
    source = write(root / "src/main.tc", PROGRAM)
    invalid = base / os.fsdecode(b"invalid-\xff")
    libc = ctypes.CDLL(None, use_errno=True)
    try:
        if libc.mkdir(ctypes.c_char_p(os.fsencode(invalid)), 0o700) != 0:
            raise OSError(ctypes.get_errno(), "cannot create invalid-UTF-8 directory")
    except OSError as error:
        if error.errno != errno.EILSEQ:
            raise
    else:
        link = root / "linked"
        link.symlink_to(invalid, target_is_directory=True)
        (root / "tc.project").write_text(
            'tc-project 1\nname = "path-test"\ninclude = "linked"\n', encoding="utf-8")
        result = run(tool, [str(source)], base)
        check(result.returncode == 70,
              "filesystem-discovered invalid canonical path text exits 70")

        write(invalid / "main.tc", PROGRAM)
        source_link = root / "source-link.tc"
        source_link.symlink_to(invalid / "main.tc")
        result = run(tool, ["--syntax-only", str(source_link)], base)
        check(result.returncode == 70 and b"TC_ERR_OUT_OF_MEMORY" not in result.stderr,
              "invalid canonical source text is I/O rather than OOM")
        no_absolute(result, base, "invalid canonical source")

    result = run(tool, [f"--include={base / 'bad\nname'}", str(source)], base)
    check(result.returncode == 2, "CLI include control character exits 2")


def check_source_precedence(tool: str, base: Path) -> None:
    root = base / "project"
    source = write(root / "src/main.tc", PROGRAM)
    write(root / "tc.project", b"corrupt")
    result = run(tool, [str(source)], base)
    check(result.returncode == 2, "existing regular source reports manifest error first")

    missing_root = base / "missing-project"
    missing = missing_root / "src/missing.tc"
    write(missing_root / "tc.project", b"corrupt")
    result = run(tool, [str(missing)], base)
    check(result.returncode == 70, "missing source I/O error precedes corrupt manifest")
    check(b"src/missing.tc" in result.stderr,
          "missing source error detail uses root-relative display")
    no_absolute(result, base, "missing source")

    directory_root = base / "directory-project"
    (directory_root / "src/main.tc").mkdir(parents=True)
    write(directory_root / "tc.project", b"corrupt")
    result = run(tool, [str(directory_root / "src/main.tc")], base)
    check(result.returncode == 70, "non-regular source precedes corrupt manifest")

    fifo_root = base / "fifo-project"
    fifo_root.mkdir()
    os.mkfifo(fifo_root / "main.tc")
    result = run(tool, [str(fifo_root / "main.tc")], base)
    check(result.returncode == 70, "FIFO source is rejected without blocking")

    loop_root = base / "loop-project"
    loop_root.mkdir()
    (loop_root / "main.tc").symlink_to("main.tc")
    result = run(tool, [str(loop_root / "main.tc")], base)
    check(result.returncode == 70, "source symlink loop exits 70")

    dangling_root = base / "dangling-source-project"
    (dangling_root / "src").mkdir(parents=True)
    (dangling_root / "src/link").symlink_to("missing")
    result = run(tool, [str(dangling_root / "src/link/main.tc")], base)
    check(result.returncode == 70,
          "dangling intermediate source symlink is an I/O error")
    no_absolute(result, base, "dangling intermediate source")


def check_import_boundaries(tool: str, base: Path) -> None:
    for kind in ("fifo", "directory", "dangling"):
        root = project(base / kind)
        source = write(root / "src/main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
        candidate = root / "src/lib.tc"
        if kind == "fifo":
            os.mkfifo(candidate)
        elif kind == "directory":
            candidate.mkdir()
        else:
            candidate.symlink_to("missing")
        later = base / f"later-{kind}"
        write(later / "lib.tc", LIBRARY)
        result = run(tool, [f"--include={later}", str(source)], base)
        check(result.returncode == 70,
              f"illegal first import candidate {kind} stops search without blocking")
        check(b"src/lib.tc" in result.stderr,
              f"illegal import candidate {kind} reports its display path")
        check(b"TC_CE_IMPORT_NOT_FOUND" not in result.stderr,
              f"illegal import candidate {kind} is not treated as absent")
        no_absolute(result, base, f"illegal import {kind}")

    root = project(base / "oversize")
    source = write(root / "src/main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
    write(root / "src/lib.tc", b"#" + b" " * (4 * 1024 * 1024))
    later = base / "oversize-later"
    write(later / "lib.tc", LIBRARY)
    result = run(tool, [f"--include={later}", str(source)], base)
    check(result.returncode == 71, "oversized first import candidate is not skipped")
    check(b"src/lib.tc" in result.stderr,
          "oversized import reports the failing candidate display path")

    root = project(base / "symlink")
    source = write(root / "src/main.tc", "#program\nimport lib\n_ = writeln int32, 1\n")
    target = write(root / "canonical/lib.tc", LIBRARY)
    (root / "src/lib.tc").symlink_to(target)
    result = run(tool, [str(source)], base)
    check(result.returncode == 0 and result.stdout == b"1\n",
          "import symlink to regular file is accepted")


def check_size_and_traps(tool: str, base: Path) -> None:
    root = project(base / "size")
    source = write(root / "src/main.tc", PROGRAM)
    exact = PROGRAM.encode() + b" " * (4 * 1024 * 1024 - len(PROGRAM))
    source.write_bytes(exact)
    result = run(tool, ["--syntax-only", str(source)], base)
    check(result.returncode == 0, "exactly 4 MiB source is accepted")
    source.write_bytes(exact + b" ")
    result = run(tool, ["--syntax-only", str(source)], base)
    check(result.returncode == 71, "4 MiB plus one source byte exits 71")
    no_absolute(result, base, "source size limit")

    root = project(base / "trap")
    source = write(root / "src/main.tc", TRAP_PROGRAM)
    for option in ("--syntax-only", "--sem-only", "--static-only"):
        result = run(tool, [option, str(source)], base)
        check(result.returncode == 0, f"partial mode {option} accepts trap program")
    result = run(tool, ["--checked", str(source)], base)
    check(result.returncode != 0 and result.stderr.startswith(b"src/main.tc:"),
          "VM trap uses project-relative display path")
    check(b"TC_RE_ADDR_UNAVAILABLE" in result.stderr, "checked VM trap is reported")
    no_absolute(result, base, "VM trap")

    outside = base / "outside"
    write(outside / "lib.tc", """#lib
public func boom() int32 then
var x: int32 = copy int32, 1
var p: ptr<int32> = addr int32, x
unsafe then
var q: ptr<int32> = ptradd int32, p, 1
var v: int32 = load int32, q
_ = return v
end
end
""")
    source = write(root / "src/main.tc", "#program\nimport lib\nvar x: int32 = call lib.boom\n")
    result = run(tool, ["--checked", f"--include={outside}", str(source)], base)
    check(result.returncode != 0 and result.stderr.startswith(b"../outside/lib.tc:"),
          f"external imported VM trap uses ../ display path: {result.stderr!r}")
    no_absolute(result, base, "external VM trap")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("vm", nargs="?", default="build/src/driver/tc-vm")
    args = parser.parse_args()
    vm = str(Path(args.vm).resolve())
    with tempfile.TemporaryDirectory(prefix="tc-p2-paths-") as temporary:
        base = Path(temporary)
        check_discovery_and_display(vm, base)
        check_include_order(vm, base)
        check_root_limit(vm, base)
        check_include_errors(vm, base)
        check_source_precedence(vm, base)
        check_import_boundaries(vm, base)
        check_size_and_traps(vm, base)
    if FAILS:
        print(f"project.paths: {FAILS} failure(s)")
        return 1
    print("project.paths: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
