#!/usr/bin/env python3
"""End-to-end acceptance checks for the private `tc --test` runner."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

PROGRAM = "#program\n_ = writeln int32, 1\n"
BAD_PROGRAM = "#program\nvar x: int32 = \n"
LIBRARY = "#lib\npublic const K: int32 = 1\n"
DIV_ZERO = """#program
_ = writeln int32, 7
_ = div int32, 1, 0
"""
FOREVER = """#program
label L:
_ = br L
"""
FAILS = 0


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def write(path: Path, data: str | bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_text(data, encoding="utf-8")
    return path


def project(root: Path, name: str = "runner-test", profile: str | None = None,
            includes: list[str] | None = None) -> Path:
    lines = ["tc-project 1", f'name = "{name}"']
    if profile is not None:
        lines.append(f'profile = "{profile}"')
    if includes is None:
        lines.append('include = "src"')
    else:
        lines.extend(f'include = "{value}"' for value in includes)
    write(root / "tc.project", "\n".join(lines) + "\n")
    return root


def test(root: Path, name: str, source: str = PROGRAM,
         stdout: str | bytes = "1\n", stdin: str | bytes | None = None,
         trap: str | bytes | None = None,
         exit_code: str | int | None = None) -> Path:
    directory = root / "tests" / name
    write(directory / "main.tc", source)
    if stdout is not None:
        write(directory / "stdout.bin", stdout)
    if stdin is not None:
        write(directory / "stdin.bin", stdin)
    if trap is not None:
        write(directory / "trap", trap)
    if exit_code is not None:
        write(directory / "exit", str(exit_code))
    return directory


def run(tool: str, root: Path, args: list[str] | None = None,
        cwd: Path | None = None, pass_fds: tuple[int, ...] = (),
        stdin=None, stdout=None, stderr=None,
        timeout: float = 30) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    command = [tool, "--test", *(args or [])]
    if stdout is None and stderr is None and not pass_fds:
        return subprocess.run(command, cwd=cwd or root, env=env,
                              capture_output=True, timeout=timeout,
                              pass_fds=pass_fds)
    return subprocess.run(command, cwd=cwd or root, env=env,
                          stdin=stdin, stdout=stdout, stderr=stderr,
                          timeout=timeout, pass_fds=pass_fds)


def check_clean(tool: str, root: Path, result: subprocess.CompletedProcess,
                expected_stdout: bytes) -> None:
    check(result.returncode == 0,
          f"clean run exits 0: {result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")
    check(result.stdout == expected_stdout,
          f"clean report bytes: {result.stdout!r} != {expected_stdout!r}; "
          f"stderr={result.stderr!r}")
    check(result.stderr == b"", f"clean run has no stderr: {result.stderr!r}")


def normal_and_filters(tool: str, base: Path) -> None:
    root = project(base / "normal")
    test(root, "zeta")
    test(root, "alpha", stdout=b"wrong")
    (root / "tests/empty").mkdir(parents=True)
    (root / "tests/not-a-test").mkdir(parents=True)

    result = run(tool, root)
    check(result.returncode == 1, "stdout mismatch is a test failure")
    check(result.stdout == (
        b'project "runner-test" (aarch64-linux-user)\n'
        b'FAIL tests/alpha: stdout mismatch\n'
        b'ok   tests/zeta\n'
        b'1 passed, 1 failed\n'), f"ordered report: {result.stdout!r}")
    check(result.stderr.startswith(
        b"detail tests/alpha: stdout mismatch\n[result]\n"
        b"trap: expected none, got none\nexit: expected 0, got 0\n"
        b"[stdout]\nfirst difference at offset 0, expected 5 bytes, got 2 bytes\n"),
        f"failure detail prefix: {result.stderr[:250]!r}")

    result = run(tool, root, ["zeta", "zeta", "--test-timeout=0"])
    check_clean(tool, root, result, (
        b'project "runner-test" (aarch64-linux-user)\n'
        b'ok   tests/zeta\n1 passed, 0 failed\n'))
    for name in ("empty", "not-a-test"):
        result = run(tool, root, [name])
        check_clean(tool, root, result, (
            b'project "runner-test" (aarch64-linux-user)\n0 tests\n'))
    for args in (["unknown"], ["zeta", "unknown"], ["file-entry"]):
        result = run(tool, root, args)
        check(result.returncode == 2 and result.stdout == b"",
              f"unknown/non-directory filter {args} precedes header")

    result = run(tool, root, ["--", "zeta"])
    check(result.returncode == 0 and b"tests/zeta" in result.stdout,
          f"double dash and interleaved timeout parse: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")


def sidecars_and_traps(tool: str, base: Path) -> None:
    root = project(base / "sidecars")
    test(root, "input", source=(
        "#program\nvar x: int32 = read int32\n_ = writeln int32, x\n"),
        stdout=b"7\n", stdin=b" \t0007\r\n")
    test(root, "trap", source=DIV_ZERO, stdout=b"7\n",
         trap=b"TC_RE_DIV_ZERO\r\n", exit_code="0001")
    test(root, "trap-default", source=DIV_ZERO, stdout=b"7\n",
         trap="TC_RE_DIV_ZERO")
    test(root, "bad-trap", source=DIV_ZERO, stdout=b"7\n",
         trap="TC_RE_DIV_ZERO")
    write(root / "tests/bad-trap/trap", b"TC_RE_DIV_ZERO ")
    test(root, "bad-exit", exit_code=256)
    test(root, "large-trap", trap="TC_RE_TRAP")
    write(root / "tests/large-trap/trap", b"TC_RE_" + b"A" * 4096)
    test(root, "after")

    result = run(tool, root, ["input", "trap", "trap-default"])
    check_clean(tool, root, result, (
        b'project "runner-test" (aarch64-linux-user)\n'
        b'ok   tests/input\nok   tests/trap\nok   tests/trap-default\n'
        b'3 passed, 0 failed\n'))
    for name in ("bad-trap", "bad-exit", "large-trap"):
        result = run(tool, root, [name])
        check(result.returncode == 2, f"{name} format error exits 2")
        check(result.stdout == b'project "runner-test" (aarch64-linux-user)\n',
              f"{name} format error occurs after header")
        check(b"0 passed" not in result.stdout and b"1 passed" not in result.stdout,
              f"{name} format error omits summary")

    test(root, "no-newline", stdout=b"1")
    result = run(tool, root, ["no-newline"])
    check(result.returncode == 1 and b"stdout mismatch" in result.stdout and
          b"got 2 bytes" in result.stderr,
          "stdout without newline compares exact bytes without short-read IO")

    root = project(base / "priority")
    test(root, "priority", source=DIV_ZERO, stdout=b"wrong",
         trap="TC_RE_IO", exit_code=9)
    test(root, "compile", source=BAD_PROGRAM, stdout=b"wrong")
    test(root, "timeout", source=FOREVER, stdout=b"ignored")
    test(root, "final")
    result = run(tool, root, ["--test-timeout=1"])
    check(result.returncode == 1, "mixed test failures exit 1")
    check(b"FAIL tests/compile: compile error\n" in result.stdout,
          "compile failure precedes runtime result")
    check(b"ok   tests/final\n" in result.stdout,
          "runner continues after failures and timeout")
    check(b"FAIL tests/timeout: timeout after 1s\n" in result.stdout,
          "timeout reason preserves argument text")
    check(b"FAIL tests/priority: trap expected TC_RE_IO, got TC_RE_DIV_ZERO\n"
          in result.stdout, "trap mismatch precedes exit/stdout")
    timeout_detail = result.stderr.split(b"detail tests/timeout:", 1)
    check(len(timeout_detail) == 2 and b"[stdout]" not in timeout_detail[1].split(
        b"detail ", 1)[0], "timeout detail discards stdout")
    check(b"1 passed, 3 failed\n" in result.stdout,
          "mixed failures summary is exact")


def discovery_and_modes(tool: str, base: Path) -> None:
    root = project(base / "missing")
    result = run(tool, root)
    check_clean(tool, root, result, (
        b'project "runner-test" (aarch64-linux-user)\n0 tests\n'))
    result = run(tool, root, ["anything"])
    check(result.returncode == 2 and result.stdout == b"",
          "filter with missing tests is unknown")

    root = project(base / "missing-main")
    (root / "tests/dangling").parent.mkdir(parents=True)
    (root / "tests/dangling").symlink_to("absent", target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "dangling test directory precedes header")
    (root / "tests/dangling").unlink()
    (root / "tests/dangling").symlink_to(str(root / "tests/other"),
                                        target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "absolute in-tree symlink is forbidden")

    root = project(base / "outside")
    outside = base / "outside-target"
    test(outside, "real")
    project(root)
    (root / "tests").mkdir()
    (root / "tests/escape").symlink_to(outside / "tests/real",
                                       target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "test directory outside tests tree is rejected")

    root = project(base / "leave-return")
    project(root)
    write(base / "leave-return-out/back", b"")
    (base / "leave-return-out/back").unlink()
    (base / "leave-return-out/back").symlink_to(
        root / "tests/real", target_is_directory=True)
    (root / "tests").mkdir()
    (root / "tests/escape").symlink_to("../leave-return-out/back",
                                       target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "intermediate escape is rejected even when target returns")

    root = project(base / "modes")
    test(root, "a-lib", source="#lib\nvar x: int32 = \n")
    test(root, "z-bad", source=BAD_PROGRAM)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "root #lib precheck precedes later lexical error and header")
    (root / "tests/a-lib/main.tc").unlink()
    test(root, "a-program")
    result = run(tool, root, ["a-program"])
    check(result.returncode == 0,
          f"program precheck allows worker execution: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")
    test(root, "duplicate-mode", source="#program\n#lib\n")
    result = run(tool, root, ["duplicate-mode"])
    check(result.returncode == 1 and b"compile error" in result.stdout,
          "later #lib is a SYN failure")


def relative_imports(tool: str, base: Path) -> None:
    root = project(base / "imports", includes=["libs"])
    directory = test(root, "nested", source=(
        "#program\nimport helper\n_ = writeln int32, helper.K\n"))
    write(directory / "helper.tc", "#lib\npublic const K: int32 = 3\n")
    (directory / "stdout.bin").write_bytes(b"3\n")
    result = run(tool, root, ["nested"])
    check(result.returncode == 0 and result.stdout.endswith(b"1 passed, 0 failed\n"),
          f"relative import anchors importer fd: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")

    directory = test(root, "include", source=(
        "#program\nimport shared\n_ = writeln int32, shared.K\n"),
        stdout=b"4\n")
    write(root / "libs/shared.tc", "#lib\npublic const K: int32 = 4\n")
    result = run(tool, root, ["include"])
    check(result.returncode == 0,
          f"manifest include fd works: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")


def hardened_boundaries(tool: str, base: Path) -> None:
    root = project(base / "tests-symlink")
    target = base / "tests-symlink-target"
    test(target, "smoke")
    (root / "tests").symlink_to(target / "tests", target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 2 and result.stdout == b"",
          "tests symlink is a layout error")

    root = project(base / "dangling-main")
    directory = test(root, "case")
    (directory / "main.tc").unlink()
    (directory / "main.tc").symlink_to("absent")
    result = run(tool, root)
    check(result.returncode == 70 and result.stdout == b"",
          "dangling main is a host input error")

    root = project(base / "referent-imports")
    directory = test(root, "main-link", stdout=b"5\n")
    write(directory / "referent/main.tc", (
        "#program\nimport helper\n_ = writeln int32, helper.K\n"))
    write(directory / "referent/helper.tc",
          "#lib\npublic const K: int32 = 5\n")
    (directory / "main.tc").unlink()
    (directory / "main.tc").symlink_to("referent/main.tc")
    result = run(tool, root)
    check(result.returncode == 0,
          f"main symlink uses referent parent for imports: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")

    directory = test(root, "main-fallback", stdout=b"7\n")
    write(directory / "referent/main.tc", (
        "#program\nimport shared\n_ = writeln int32, shared.K\n"))
    write(directory / "shared.tc", "#lib\npublic const K: int32 = 7\n")
    (directory / "main.tc").unlink()
    (directory / "main.tc").symlink_to("referent/main.tc")
    result = run(tool, root, ["main-fallback"])
    check(result.returncode == 0,
          f"main symlink falls back to canonical test include root: "
          f"exit={result.returncode}; stdout={result.stdout!r}; "
          f"stderr={result.stderr!r}")

    directory = test(root, "import-link", source=(
        "#program\nimport helper\n_ = writeln int32, helper.K\n"),
        stdout=b"6\n")
    write(directory / "modules/helper.tc", (
        "#lib\nimport nested\npublic const K: int32 = nested.K\n"))
    write(directory / "modules/nested.tc",
          "#lib\npublic const K: int32 = 6\n")
    (directory / "helper.tc").symlink_to("modules/helper.tc")
    result = run(tool, root, ["import-link"])
    check(result.returncode == 0,
          f"import symlink uses referent parent: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")
    nested = directory / "modules/nested.tc"
    before = nested.read_bytes()
    with nested.open("rb") as error_output:
        result = run(tool, root, ["import-link"], stdout=subprocess.PIPE,
                     stderr=error_output)
    check(result.returncode == 2 and result.stdout == b"" and
          nested.read_bytes() == before,
          "recursive alias scan follows imported referent parent")

    root = project(base / "ignored-sigchld")
    test(root, "case")

    def ignore_sigchld() -> None:
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)

    result = subprocess.run([tool, "--test"], cwd=root,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            preexec_fn=ignore_sigchld, timeout=30)
    check_clean(tool, root, result, (
        b'project "runner-test" (aarch64-linux-user)\n'
        b'ok   tests/case\n1 passed, 0 failed\n'))

    root = project(base / "mode-token")
    test(root, "lookalike", source="#libfoo\n_ = writeln int32, 1\n")
    result = run(tool, root)
    check(result.returncode == 1 and b"compile error" in result.stdout,
          "mode prefix lookalike is not root #lib")

    root = project(base / "sidecar-order")
    directory = test(root, "case")
    (directory / "stdout.bin").unlink()
    (directory / "stdout.bin").mkdir()
    outside = write(base / "sidecar-order-input", b"")
    (directory / "stdin.bin").symlink_to(outside)
    result = run(tool, root)
    check(result.returncode == 70 and
          b"sidecar is not a regular file" in result.stderr,
          "stdout sidecar failure precedes stdin boundary failure")
    (directory / "stdout.bin").rmdir()
    write(directory / "stdout.bin", b"1\n")
    (directory / "stdin.bin").unlink()
    write(directory / "trap", b"bad")
    write(directory / "exit", b"bad")
    result = run(tool, root)
    check(result.returncode == 2 and b"invalid trap sidecar" in result.stderr,
          "trap format failure precedes exit format failure")

    root = project(base / "full-trap")
    trap = b"TC_RE_" + b"A" * 4090
    test(root, "case", trap=trap)
    result = run(tool, root)
    check(result.returncode == 1 and b"trap expected " + trap in result.stdout,
          "legal 4 KiB unknown trap is a test failure")

    root = project(base / "long-timeout")
    test(root, "case", source=FOREVER)
    timeout = "0" * 5000 + "1"
    result = run(tool, root, [f"--test-timeout={timeout}"])
    check(result.returncode == 1 and
          b"FAIL tests/case: timeout after " + timeout.encode() + b"s\n"
          in result.stdout, "long timeout reason is not truncated")

    for name, include in (("include-file", "plain"),
                          ("include-prefix", "plain/child"),
                          ("include-loop", "loop"),
                          ("include-long", "x" * 300)):
        root = project(base / name, includes=[include])
        if name == "include-file":
            write(root / "plain", b"")
        elif name == "include-prefix":
            write(root / "plain", b"")
        elif name == "include-loop":
            (root / "loop").symlink_to("loop")
        result = run(tool, root)
        check(result.returncode == 2 and result.stdout == b"",
              f"include path error {name} exits 2")

    root = project(base / "include-missing", includes=["missing"])
    result = run(tool, root)
    check_clean(tool, root, result, (
        b'project "runner-test" (aarch64-linux-user)\n0 tests\n'))

    root = project(base / "closed-stdin")
    test(root, "case")

    def close_stdin() -> None:
        os.close(0)

    result = subprocess.run([tool, "--test"], cwd=root,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            preexec_fn=close_stdin, timeout=30)
    check(result.returncode == 70 and result.stdout == b"" and
          result.stderr == b"",
          "initially closed stdio remains a silent environment failure")


def limits_and_capture(tool: str, base: Path) -> None:
    root = project(base / "limits")
    test(root, "a-too-large")
    with (root / "tests/a-too-large/main.tc").open("wb") as stream:
        stream.write(b"#" * (4 * 1024 * 1024 + 1))
    (root / "tests/z-dangling").symlink_to("absent", target_is_directory=True)
    result = run(tool, root)
    check(result.returncode == 71 and result.stdout == b"",
          "oversize main implementation limit precedes later usage error")

    root = project(base / "capture")
    large_source = """#program
var i: int32 = copy int32, 0
label loop:
_ = write int32, 0
i = add int32, i, 1
var more: bool = lt int32, i, 20000
_ = br_if more, loop
"""
    test(root, "large", source=large_source,
        stdout=b"0" * 20000)
    result = run(tool, root, ["large"], timeout=180)
    check(result.returncode == 0,
          f"large streaming stdout passes: exit={result.returncode}; "
          f"stdout={result.stdout!r}; stderr={result.stderr!r}")
    write(root / "tests/large/stdout.bin", b"0" * 19999 + b"1" + b"0")
    result = run(tool, root, ["large"], timeout=180)
    check(result.returncode == 1 and b"stdout mismatch" in result.stdout,
          "large stdout mismatch fails")
    check(b"first difference at offset 19999, expected 20001 bytes, got 20000 bytes\n"
          in result.stderr, "large stdout first difference and lengths exact")
    check(result.stderr.count(b"0") < 41000 and len(result.stderr) < 41000,
          "stdout failure context is bounded")


def output_aliases(tool: str, base: Path) -> None:
    root = project(base / "aliases")
    directory = test(root, "smoke")
    with (root / "tests/smoke/stdout.bin").open("rb") as output:
        result = run(tool, root, stdout=output,
                     stderr=subprocess.PIPE)
    check(result.returncode == 2 and result.stderr == b"",
          "fd1 alias to stdout sidecar is silent usage error")
    check((directory / "stdout.bin").read_bytes() == b"1\n",
          "output alias attempt does not modify sidecar")

    root = project(base / "stderr-alias")
    test(root, "smoke")
    with (root / "tc.project").open("rb") as error_output:
        result = run(tool, root, stdout=subprocess.PIPE, stderr=error_output)
    check(result.returncode == 2 and result.stdout == b"",
          "fd2 alias to manifest is silent usage error")

    root = project(base / "stdio-alias")
    test(root, "smoke")
    before = (root / "tc.project").read_bytes()
    with (root / "tc.project").open("rb") as shared:
        result = run(tool, root, stdout=subprocess.PIPE, stdin=shared,
                     stderr=shared)
    check(result.returncode == 2 and result.stdout == b"" and
          (root / "tc.project").read_bytes() == before,
          "fd2 alias to fd0 is a silent usage error")

    root = project(base / "continued-import-alias")
    directory = test(root, "case", source=(
        "#program\nimport \\\nhelper\n_ = writeln int32, helper.K\n"))
    helper = directory / "helper.tc"
    write(helper, "#lib\npublic const K: int32 =\n")
    result = run(tool, root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(result.returncode == 1 and b"compile error" in result.stdout,
          "continued import reaches the invalid imported module")
    before = helper.read_bytes()
    with helper.open("r+b") as error_output:
        result = run(tool, root, stdout=subprocess.PIPE, stderr=error_output)
    check(result.returncode == 2 and result.stdout == b"" and
          helper.read_bytes() == before,
          "continued import alias is rejected before header or diagnostics")

    root = project(base / "malformed-before-import-alias")
    directory = test(root, "case", source=(
        "#program\n/\nimport helper\n_ = writeln int32, helper.K\n"))
    helper = directory / "helper.tc"
    write(helper, "#lib\npublic const K: int32 =\n")
    before = helper.read_bytes()
    with helper.open("r+b") as error_output:
        result = run(tool, root, stdout=subprocess.PIPE, stderr=error_output)
    check(result.returncode == 2 and result.stdout == b"" and
          helper.read_bytes() == before,
          "tolerant import scan continues after an earlier lexical error")

    root = project(base / "oversized-import-alias")
    directory = test(root, "case", source=(
        "#program\nimport helper\n_ = writeln int32, helper.K\n"))
    helper = directory / "helper.tc"
    write(helper, "#lib\npublic const K: int32 = 1\n")
    with helper.open("r+b") as stream:
        stream.truncate(4 * 1024 * 1024 + 1)
    before = helper.read_bytes()
    with helper.open("r+b") as error_output:
        result = run(tool, root, stdout=subprocess.PIPE, stderr=error_output)
    check(result.returncode == 2 and result.stdout == b"" and
          helper.read_bytes() == before,
          "oversized import identity is protected before source-size failure")


def report_write_failure(tool: str, base: Path) -> None:
    root = project(base / "report-write-failure")
    test(root, "case", source=FOREVER, stdout=b"")
    read_fd, write_fd = os.pipe()
    process = subprocess.Popen(
        [tool, "--test", "--test-timeout=1"], cwd=root,
        stdin=subprocess.DEVNULL, stdout=write_fd, stderr=subprocess.PIPE)
    os.close(write_fd)
    header = b'project "runner-test" (aarch64-linux-user)\n'
    received = b""
    try:
        while len(received) < len(header):
            chunk = os.read(read_fd, len(header) - len(received))
            if not chunk:
                break
            received += chunk
    finally:
        os.close(read_fd)
    _, error_output = process.communicate(timeout=10)
    check(received == header and process.returncode == 70 and
          b"cannot write test report" in error_output,
          f"report write failure exits 70 after timeout worker cleanup: "
          f"exit={process.returncode}; received={received!r}; stderr={error_output!r}")


def invalid_utf8_and_cli(tool: str, base: Path) -> None:
    root = project(base / "cli")
    test(root, "smoke")
    cases = [
        (["--help"],), (["--test-timeout=1", "--test"],),
        (["--test-timeout=-1"],), (["--test-timeout=18446744073709551616"],),
        (["--test-timeout="],), (["--test-timeout=1", "--test-timeout=2"],),
    ]
    for args in cases:
        result = run(tool, root, list(args[0]))
        check(result.returncode == 2 and b'project "' not in result.stdout,
              f"invalid test CLI args rejected: {args}")

    import ctypes
    invalid = Path(os.fsdecode(b"bad-\xff"))
    try:
        os.mkdir(os.fsencode(root / "tests" / invalid), 0o700)
    except FileExistsError:
        pass
    except OSError as error:
        check(error.errno == 92, f"host rejects invalid UTF-8 names: {error.errno}")
    else:
        write(root / "tests" / invalid / "main.tc", PROGRAM)
        result = run(tool, root)
        check(result.returncode == 2 and result.stdout == b"",
              "invalid UTF-8 entry name precedes header")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("tc")
    args = parser.parse_args()
    tool = os.path.abspath(args.tc)
    with tempfile.TemporaryDirectory(prefix="tc-project-test-") as temporary:
        base = Path(temporary)
        normal_and_filters(tool, base)
        sidecars_and_traps(tool, base)
        discovery_and_modes(tool, base)
        relative_imports(tool, base)
        hardened_boundaries(tool, base)
        limits_and_capture(tool, base)
        output_aliases(tool, base)
        report_write_failure(tool, base)
        invalid_utf8_and_cli(tool, base)
    if FAILS:
        print(f"project.test_runner: {FAILS} failure(s)")
        return 1
    print("project.test_runner: acceptance checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
