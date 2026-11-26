#!/usr/bin/env python3
"""End-to-end checks for the private tc --init-project implementation."""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

FAILS = 0
PROGRAM = b"#program\n_ = writeln int32, 1\n"
TARGETS = [
    "tc.project",
    "src/main.tc",
    "tests/smoke/main.tc",
    "tests/smoke/stdout.bin",
    ".gitignore",
    ".gitattributes",
]


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def run(tool: str, args: list[str], cwd: Path, **kwargs: object) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, *args], cwd=cwd, capture_output=True, timeout=20,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "PATH": "/usr/bin:/bin"},
        **kwargs,
    )


def snapshot(root: Path) -> dict[str, tuple[str, bytes]]:
    result: dict[str, tuple[str, bytes]] = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in dirs + files:
            path = base / name
            rel = str(path.relative_to(root))
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                result[rel] = ("symlink", os.readlink(path).encode())
            elif stat.S_ISDIR(mode):
                result[rel] = ("dir", b"")
            elif stat.S_ISREG(mode):
                result[rel] = ("file", path.read_bytes())
            elif stat.S_ISFIFO(mode):
                result[rel] = ("fifo", b"")
            else:
                result[rel] = ("other", b"")
    return result


def expected_success(name: str, statuses: list[str] | None = None) -> bytes:
    statuses = statuses or ["created"] * 6
    lines = [f'initialized project "{name}"']
    lines.extend(f"{status} {path}" for status, path in zip(statuses, TARGETS))
    lines.append("next: tc --test")
    return ("\n".join(lines) + "\n").encode()


def check_full_generation(tool: str, root: Path) -> None:
    name = " demo.项目 "
    result = run(tool, ["--init-project", "--", name], root)
    check(result.returncode == 0, f"full generation exit {result.returncode}")
    check(result.stderr == b"", "full generation stderr empty")
    check(result.stdout == expected_success(name), "full generation exact report")
    expected = {
        "tc.project": ("file", (
            "tc-project 1\n"
            f'name = "{name}"\n'
            'entry = "src/main.tc"\n'
            'include = "src"\n'
            'profile = "aarch64-linux-user"\n'
        ).encode()),
        "src": ("dir", b""),
        "src/main.tc": ("file", PROGRAM),
        "tests": ("dir", b""),
        "tests/smoke": ("dir", b""),
        "tests/smoke/main.tc": ("file", PROGRAM),
        "tests/smoke/stdout.bin": ("file", b"1\n"),
        ".gitignore": ("file", b"build/\n"),
        ".gitattributes": ("file", b"*.bin -text\n"),
    }
    check(snapshot(root) == expected, "full generation exact tree and bytes")
    check(not (root / name).exists(), "project name is not a child directory")
    check(not (root / "build").exists(), "init does not create build/")


def check_existing_and_blocked_prefixes(tool: str, base: Path) -> None:
    root = base / "existing"
    (root / "src").mkdir(parents=True)
    (root / "tests/smoke").mkdir(parents=True)
    (root / "keep").write_bytes(b"keep")
    (root / "src/main.tc").write_bytes(b"old source")
    (root / "tests/smoke/stdout.bin").write_bytes(b"old\x00bytes")
    (root / ".gitignore").write_bytes(b"custom\n")
    (root / ".gitattributes").symlink_to("missing-referent")
    result = run(tool, ["--init-project", "demo"], root)
    statuses = ["created", "skipped", "created", "skipped", "skipped", "skipped"]
    check(result.returncode == 0, "existing/skipped exit 0")
    check(result.stdout == expected_success("demo", statuses),
          "existing/skipped exact fixed-order report")
    check((root / "src/main.tc").read_bytes() == b"old source", "existing source unchanged")
    check((root / "tests/smoke/stdout.bin").read_bytes() == b"old\x00bytes",
          "existing binary unchanged")
    check((root / ".gitignore").read_bytes() == b"custom\n", "existing gitignore unchanged")
    check((root / ".gitattributes").is_symlink(), "dangling symlink remains skipped")
    check((root / "tests/smoke/main.tc").read_bytes() == PROGRAM, "missing smoke source created")

    for kind in ("file", "fifo", "symlink"):
        root = base / f"src-{kind}"
        root.mkdir()
        outside = base / f"outside-{kind}"
        outside.mkdir()
        (outside / "main.tc").write_bytes(b"outside")
        if kind == "file":
            (root / "src").write_bytes(b"not a directory")
        elif kind == "fifo":
            os.mkfifo(root / "src")
        else:
            (root / "src").symlink_to(outside, target_is_directory=True)
        result = run(tool, ["--init-project", "demo"], root)
        check(result.returncode == 0, f"blocked src {kind} still initializes")
        check(b"skipped src/main.tc\n" in result.stdout, f"blocked src {kind} skips child")
        check((outside / "main.tc").read_bytes() == b"outside", f"src {kind} not followed")
        check((root / "tc.project").is_file(), f"src {kind} does not block root files")

    for kind in ("file", "fifo", "symlink"):
        root = base / f"smoke-{kind}"
        (root / "tests").mkdir(parents=True)
        if kind == "file":
            (root / "tests/smoke").write_bytes(b"not a directory")
        elif kind == "fifo":
            os.mkfifo(root / "tests/smoke")
        else:
            (root / "tests/smoke").symlink_to("missing")
        result = run(tool, ["--init-project", "demo"], root)
        check(result.returncode == 0, f"blocked smoke {kind} skips descendants")
        check(b"skipped tests/smoke/main.tc\n" in result.stdout and
              b"skipped tests/smoke/stdout.bin\n" in result.stdout,
              f"smoke {kind} exact skipped children")


def check_preflight_errors(tool: str, base: Path) -> None:
    for kind in ("file", "dir", "fifo", "symlink", "dangling"):
        root = base / f"manifest-{kind}"
        root.mkdir()
        path = root / "tc.project"
        if kind == "file":
            path.write_bytes(b"existing")
        elif kind == "dir":
            path.mkdir()
        elif kind == "fifo":
            os.mkfifo(path)
        elif kind == "symlink":
            (base / "manifest-referent").write_bytes(b"referent")
            path.symlink_to(base / "manifest-referent")
        else:
            path.symlink_to("missing")
        before = snapshot(root)
        result = run(tool, ["--init-project", "demo"], root)
        check(result.returncode == 2, f"existing tc.project {kind} exit 2")
        check(result.stdout == b"", f"existing tc.project {kind} stdout empty")
        check(snapshot(root) == before, f"existing tc.project {kind} unchanged")

    root = base / "priority"
    root.mkdir()
    (root / "tc.project").write_bytes(b"existing")
    (root / "tests").write_bytes(b"not a directory")
    result = run(tool, ["--init-project", "demo"], root)
    check(result.returncode == 2, "manifest/tests double error exit 2")
    check(b"tc.project" in result.stderr and b"tests must" not in result.stderr,
          "tc.project precheck has priority over tests")

    for kind in ("file", "fifo", "symlink"):
        root = base / f"tests-{kind}"
        root.mkdir()
        if kind == "file":
            (root / "tests").write_bytes(b"not a directory")
        elif kind == "fifo":
            os.mkfifo(root / "tests")
        else:
            (root / "tests").symlink_to(base / "outside-file", target_is_directory=True)
        before = snapshot(root)
        result = run(tool, ["--init-project", "demo"], root)
        check(result.returncode == 2, f"invalid tests {kind} exit 2")
        check(result.stdout == b"", f"invalid tests {kind} stdout empty")
        check(snapshot(root) == before, f"invalid tests {kind} writes nothing")

    for name in (".", "..", "a/b", 'a"b', "a\\b", "\u2028", ""):
        root = base / ("invalid-" + str(len(list(base.iterdir()))))
        root.mkdir()
        before = snapshot(root)
        result = run(tool, ["--init-project", name], root)
        check(result.returncode == 2, f"invalid name {name!r} exit 2")
        check(result.stdout == b"" and snapshot(root) == before,
              f"invalid name {name!r} has no effects")
    if os.geteuid() != 0:
        root = base / "tests-priority"
        (root / "src").mkdir(parents=True)
        (root / "tests").write_bytes(b"not a directory")
        (root / "src").chmod(0)
        try:
            result = run(tool, ["--init-project", "demo"], root)
        finally:
            (root / "src").chmod(0o700)
        check(result.returncode == 2 and result.stderr == b"",
              "tests precheck precedes inaccessible src with unsafe stderr")

        root = base / "manifest-referent-priority"
        root.mkdir()
        locked = base / "locked-referent"
        locked.mkdir()
        (root / "tc.project").symlink_to(locked / "project")
        locked.chmod(0)
        try:
            result = run(tool, ["--init-project", "demo"], root)
        finally:
            locked.chmod(0o700)
        check(result.returncode == 2 and result.stderr == b"",
              "existing manifest entry precedes inaccessible referent safely")

    root = base / "invalid-utf8"
    root.mkdir()
    result = subprocess.run([tool.encode(), b"--init-project", b"bad\xff"], cwd=root,
                            capture_output=True, timeout=20)
    check(result.returncode == 2 and result.stdout == b"" and snapshot(root) == {},
          "invalid UTF-8 name has no effects")


def run_with_fd(tool: str, args: list[str], cwd: Path, stdout_fd: int | None = None,
                stderr_fd: int | None = None,
                **kwargs: object) -> tuple[subprocess.CompletedProcess, os.PathLike | None]:
    stdout_target = stdout_fd if stdout_fd is not None else subprocess.PIPE
    stderr_target = stderr_fd if stderr_fd is not None else subprocess.PIPE
    result = subprocess.run([tool, *args], cwd=cwd, stdout=stdout_target,
                            stderr=stderr_target, timeout=20,
                            env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                                 "PATH": "/usr/bin:/bin"}, **kwargs)
    return result, stdout_target if isinstance(stdout_target, int) else None


def check_aliases(tool: str, base: Path) -> None:
    root = base / "alias-stdout"
    root.mkdir()
    (root / ".gitignore").write_bytes(b"do not modify")
    fd = os.open(root / ".gitignore", os.O_RDONLY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root, stdout_fd=fd)
    finally:
        os.close(fd)
    check(result.returncode == 2, "stdout aliases target exit 2")
    check(result.stdout is None and result.stderr, "stdout alias diagnostic uses safe stderr")
    check((root / ".gitignore").read_bytes() == b"do not modify", "stdout alias input unchanged")
    check(set(snapshot(root)) == {".gitignore"}, "stdout alias creates nothing")

    root = base / "alias-stderr"
    root.mkdir()
    victim = base / "stderr-victim"
    victim.write_bytes(b"do not modify")
    (root / ".gitignore").symlink_to(victim)
    fd = os.open(victim, os.O_RDONLY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "."], root, stderr_fd=fd)
    finally:
        os.close(fd)
    check(result.returncode == 2, "stderr aliases skipped referent with cached error exit 2")
    check(result.stdout == b"", "stderr alias stdout empty")
    check(victim.read_bytes() == b"do not modify", "cached diagnostic not written to alias input")

    root = base / "alias-referent"
    root.mkdir()
    victim = base / "stdout-victim"
    victim.write_bytes(b"do not modify")
    (root / ".gitattributes").symlink_to(victim)
    fd = os.open(victim, os.O_RDONLY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root, stdout_fd=fd)
    finally:
        os.close(fd)
    check(result.returncode == 2, "stdout aliases skipped symlink referent exit 2")
    check(victim.read_bytes() == b"do not modify", "skipped referent not modified")

    root = base / "alias-prefix-referent"
    root.mkdir()
    outside = base / "prefix-referent"
    outside.mkdir()
    victim = outside / "main.tc"
    victim.write_bytes(b"do not modify")
    (root / "src").symlink_to(outside, target_is_directory=True)
    fd = os.open(victim, os.O_RDONLY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root,
                                stdout_fd=fd)
    finally:
        os.close(fd)
    check(result.returncode == 2,
          "stdout aliases referent beneath skipped prefix exit 2")
    check(victim.read_bytes() == b"do not modify",
          "skipped-prefix referent not modified")
    check(set(snapshot(root)) == {"src"},
          "skipped-prefix alias creates nothing")

    root = base / "alias-hardlink"
    root.mkdir()
    (root / ".gitignore").write_bytes(b"one")
    os.link(root / ".gitignore", root / ".gitattributes")
    before = snapshot(root)
    result = run(tool, ["--init-project", "demo"], root)
    check(result.returncode == 2 and result.stdout == b"", "hardlinked targets exit 2")
    check(snapshot(root) == before, "hardlinked targets unchanged")

    root = base / "alias-streams"
    root.mkdir()
    fd1 = os.open("/dev/null", os.O_WRONLY)
    fd2 = os.open("/dev/null", os.O_WRONLY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root,
                                stdout_fd=fd1, stderr_fd=fd2)
    finally:
        os.close(fd1)
        os.close(fd2)
    check(result.returncode == 2 and snapshot(root) == {}, "fd1/fd2 identity collision exit 2")

    if os.geteuid() != 0:
        for mode, label in ((os.O_RDWR, "writable"), (os.O_RDONLY, "readonly")):
            root = base / f"unresolved-stderr-{label}"
            root.mkdir()
            locked = base / f"unresolved-locked-{label}"
            locked.mkdir()
            victim = locked / "project"
            victim.write_bytes(b"do not modify")
            (root / "tc.project").symlink_to(victim)
            fd = os.open(victim, mode)
            locked.chmod(0)
            try:
                result, _ = run_with_fd(tool, ["--init-project", "demo"], root,
                                        stderr_fd=fd)
            finally:
                os.close(fd)
                locked.chmod(0o700)
            check(result.returncode == 2 and result.stdout == b"",
                  f"unresolved referent with {label} stderr exit 2")
            check(victim.read_bytes() == b"do not modify",
                  f"unresolved {label} stderr referent unchanged")
            check(set(snapshot(root)) == {"tc.project"},
                  f"unresolved {label} stderr creates nothing")

    root = base / "alias-fifo"
    root.mkdir()
    os.mkfifo(root / ".gitignore")
    fd = os.open(root / ".gitignore", os.O_RDONLY | os.O_NONBLOCK)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root, stdout_fd=fd)
    finally:
        os.close(fd)
    check(result.returncode == 2, "stdout aliases skipped FIFO target exit 2")


def check_cwd_and_report_failures(tool: str, base: Path) -> None:
    actual = base / "actual"
    actual.mkdir()
    link = base / "cwd-link"
    link.symlink_to(actual, target_is_directory=True)
    result = run(tool, ["--init-project", "demo"], link)
    check(result.returncode == 0 and (actual / "tc.project").is_file(),
          "symlink cwd canonicalizes to real directory")
    check(not (link / "tc.project").is_symlink(), "cwd symlink itself is not replaced")

    root = base / "report-readonly"
    root.mkdir()
    readonly = base / "report-readonly.bin"
    readonly.write_bytes(b"not writable")
    fd = os.open(readonly, os.O_RDONLY)
    try:
        result = subprocess.run([tool, "--init-project", "demo"], cwd=root,
                                stdout=fd, stderr=subprocess.PIPE, timeout=20)
    finally:
        os.close(fd)
    check(result.returncode == 70, "read-only stdout report exit 70")
    check((root / "tc.project").is_file() and (root / ".gitattributes").is_file(),
          "report failure keeps committed files")
    check(b"initialized project" not in result.stderr, "report failure has no success summary")

    root = base / "report-closed"
    root.mkdir()
    result = subprocess.run([tool, "--init-project", "demo"], cwd=root,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
                            preexec_fn=lambda: os.close(1))
    check(result.returncode == 70 and result.stdout == b"", "closed stdout report exit 70")
    check((root / "tc.project").is_file(), "closed stdout keeps committed files")

    root = base / "report-closed-reused-fd"
    root.mkdir()
    directory_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
    try:
        result, _ = run_with_fd(tool, ["--init-project", "demo"], root,
                                stderr_fd=directory_fd,
                                preexec_fn=lambda: os.close(1),
                                pass_fds=(directory_fd,))
    finally:
        os.close(directory_fd)
    check(result.returncode == 70 and result.stdout == b"",
          "closed stdout is not confused with reused internal fd")
    check((root / "tc.project").is_file() and (root / ".gitattributes").is_file(),
          "internal fd relocation keeps committed files")

    root = base / "report-pipe"
    root.mkdir()
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        result = subprocess.run([tool, "--init-project", "demo"], cwd=root,
                                stdout=write_fd, stderr=subprocess.PIPE, timeout=20,
                                pass_fds=(write_fd,))
    finally:
        os.close(write_fd)
    check(result.returncode == 70, "broken stdout pipe exits 70 rather than SIGPIPE")
    check((root / "tc.project").is_file(), "broken pipe keeps committed files")

    root = base / "closed-stderr-ok"
    root.mkdir()
    result = subprocess.run([tool, "--init-project", "demo"], cwd=root,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
                            preexec_fn=lambda: os.close(2))
    check(result.returncode == 0, "closed stderr does not break successful report")
    root = base / "closed-stderr-error"
    root.mkdir()
    before = snapshot(root)
    result = subprocess.run([tool, "--init-project", "."], cwd=root,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
                            preexec_fn=lambda: os.close(2))
    check(result.returncode == 70 and result.stdout == b"" and snapshot(root) == before,
          "closed stderr turns cached usage diagnostic into 70")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: init_project_test.py <tc-or-init-driver>", file=sys.stderr)
        return 2
    tool = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="tc-init-project-") as tmp:
        base = Path(tmp)
        full = base / "full"
        full.mkdir()
        check_full_generation(tool, full)
        check_existing_and_blocked_prefixes(tool, base)
        check_preflight_errors(tool, base)
        check_aliases(tool, base)
        check_cwd_and_report_failures(tool, base)
    if FAILS:
        print(f"{FAILS} init-project check(s) failed")
        return 1
    print("init-project checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
