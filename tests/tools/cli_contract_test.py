#!/usr/bin/env python3
"""CLI contract test for tc / tc-vm / tc-aot.

The static front end is implemented.  `--syntax-only` (LEX/SYN), `--sem-only`
(through SEM) and `--static-only` (through CT and IR finalization) are partial
checks with exit 0, no output.  `tc-vm` executes a normal invocation on the
reference VM and reports the first `TC_RE_*` trap on stderr with
`error[RT]`; `tc-aot` emits target assembly on a normal invocation.
Capabilities outside this hosted platform instance (a different word size or
the bare-metal device policy) still fail explicitly with the
implementation-level "unavailable" status (70). Diagnostics exit 1 with the
stable diagnostic line. File/resource failures and unavailable capabilities
exit 70; internal contract violations and documented limits exit 71.
Implementation errors are never language diagnostics.
"""

from __future__ import annotations

import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

FAILS = 0
DIAG_PREFIX = ": error["


def check(cond: bool, msg: str) -> None:
    global FAILS
    if not cond:
        print(f"FAIL: {msg}")
        FAILS += 1


def run(tool: str, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, *args],
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=20,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "PATH": "/usr/bin:/bin"},
    )


def run_closed(tool: str, args: list[str], cwd: Path,
               descriptor: int) -> subprocess.CompletedProcess:
    command = "exec " + shlex.join([tool, *args]) + f" {descriptor}>&-"
    return subprocess.run(
        ["bash", "-c", command],
        cwd=cwd,
        capture_output=True,
        timeout=20,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "PATH": "/usr/bin:/bin"},
    )


def tree_snapshot(cwd: Path) -> dict[str, bytes]:
    return {
        str(path.relative_to(cwd)): path.read_bytes()
        for path in cwd.rglob("*") if path.is_file()
    }


def check_project_cli(tool: str, cwd: Path,
                      before: dict[str, bytes]) -> None:
    valid_unavailable = [
        ["--test"],
        ["--test", "--test-timeout=0"],
        ["--test", "alpha", "--test-timeout=18446744073709551615", "beta"],
        ["--test", "--", "-alpha", "--", "--test"],
        ["--test", "-"],
        ["--test", "--test-timeout=000", "--", "--test-timeout=-1"],
    ]
    valid_init = [
        ["--init-project", "demo"],
        ["--init-project", "demo.name"],
        ["--init-project", " demo "],
        ["--init-project", "项目"],
        ["--init-project", "--", "-demo"],
        ["--init-project", "--", "--help"],
    ]
    invalid_usage = [
        [],
        ["--"],
        ["--", "--test"],
        ["--help", "extra"],
        ["--version", "extra"],
        ["--init-project"],
        ["--init-project", ""],
        ["--init-project", "."],
        ["--init-project", ".."],
        ["--init-project", "a/b"],
        ["--init-project", "a\\b"],
        ["--init-project", 'a"b'],
        ["--init-project", "\x01"],
        ["--init-project", "\x7f"],
        ["--init-project", "\u0085"],
        ["--init-project", "\u00ad"],
        ["--init-project", "\u200b"],
        ["--init-project", "\u2028"],
        ["--init-project", "\u2029"],
        ["--init-project", "\u2066"],
        ["--init-project", "\ufeff"],
        ["--init-project", "\U000e0001"],
        ["--init-project", "\U000e0020"],
        ["--init-project", "one", "two"],
        ["--init-project", "demo", "--test"],
        ["--test", "--init-project"],
        ["--test", "--help"],
        ["--test", "--version"],
        ["--test", "--bogus"],
        ["--test", "--test-timeout"],
        ["--test", "--test-timeout="],
        ["--test", "--test-timeout=-1"],
        ["--test", "--test-timeout=+1"],
        ["--test", "--test-timeout=1.0"],
        ["--test", "--test-timeout= 1"],
        ["--test", "--test-timeout=١"],
        ["--test", "--test-timeout=18446744073709551616"],
        ["--test", "--test-timeout=1", "--test-timeout=2"],
        ["--init-project", "demo", "--test-timeout=1"],
    ]
    for args in valid_unavailable:
        result = run(tool, args, cwd)
        check(result.returncode == 70, f"tc {args!r} unavailable exit {result.returncode}")
        check(result.stdout == "", f"tc {args!r} unavailable stdout")
        check(
            result.stderr.startswith("tc: tc_error_unavailable: "),
            f"tc {args!r} unavailable error channel",
        )
        check(tree_snapshot(cwd) == before,
              f"tc {args!r} must not change project files")
    for args in valid_init:
        with tempfile.TemporaryDirectory(prefix="tc-init-cli-") as init_tmp:
            init_cwd = Path(init_tmp)
            result = run(tool, args, init_cwd)
            check(result.returncode == 0,
                  f"tc {args!r} init exit {result.returncode}")
            check(result.stderr == "", f"tc {args!r} init stderr")
            check(result.stdout.startswith('initialized project "') and
                  result.stdout.endswith("next: tc --test\n"),
                  f"tc {args!r} init report")
            check((init_cwd / "tc.project").is_file() and
                  (init_cwd / "src/main.tc").is_file() and
                  (init_cwd / "tests/smoke/stdout.bin").read_bytes() == b"1\n",
                  f"tc {args!r} init files")
    for args in invalid_usage:
        result = run(tool, args, cwd)
        check(result.returncode == 2, f"tc {args!r} usage exit {result.returncode}")
        check(result.stdout == "", f"tc {args!r} usage stdout")
        expected_error = ("tc: invalid project name"
                          if len(args) == 2 and args[0] == "--init-project"
                          else "usage:")
        check(expected_error in result.stderr,
              f"tc {args!r} usage stderr")
        check(tree_snapshot(cwd) == before,
              f"tc {args!r} must not change project files")
    invalid_utf8 = [
        b"\x80",
        b"\xc0\x80",
        b"\xe0\x80\x80",
        b"\xed\xa0\x80",
        b"\xf0\x80\x80\x80",
        b"\xf4\x90\x80\x80",
        b"\xf5\x80\x80\x80",
        b"ok\xc3",
    ]
    for name in invalid_utf8:
        result = subprocess.run(
            [tool.encode(), b"--init-project", name],
            cwd=cwd,
            capture_output=True,
            timeout=20,
        )
        check(result.returncode == 2,
              f"tc invalid UTF-8 name {name!r} usage exit {result.returncode}")
        check(result.stdout == b"",
              f"tc invalid UTF-8 name {name!r} usage stdout")
        check(b"tc: invalid project name" in result.stderr,
              f"tc invalid UTF-8 name {name!r} usage stderr")
        check(tree_snapshot(cwd) == before,
              f"tc invalid UTF-8 name {name!r} must not change project files")


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: cli_contract_test.py <tc-vm> <tc-aot> <tc>", file=sys.stderr)
        return 2
    tools = sys.argv[1:3]
    project_tool = sys.argv[3]
    with tempfile.TemporaryDirectory(prefix="tc-cli-") as tmp:
        cwd = Path(tmp)
        source = cwd / "main.tc"
        source.write_text("#program\nvar x: int32 = copy int32, 1\n", encoding="utf-8")
        bad = cwd / "bad.tc"
        bad.write_text("#program\nvar x: int32 = 1 ;\n", encoding="utf-8")
        run_source = cwd / "run_main.tc"
        run_source.write_text(
            "#program\n_ = writeln int32, 42\n", encoding="utf-8"
        )
        trap_source = cwd / "trap_main.tc"
        trap_source.write_text(
            "#program\n"
            "var z: int32 = copy int32, 0\n"
            "var x: int32 = div int32, 1, z\n",
            encoding="utf-8",
        )
        ct_source = cwd / "ct_main.tc"
        ct_source.write_text(
            "#lib\n"
            "public const BAD: int32 = div int32, 1, 0\n",
            encoding="utf-8",
        )
        ct_root = cwd / "ct_root.tc"
        ct_root.write_text("#program\nimport ct_main\n", encoding="utf-8")
        missing = cwd / "missing.tc"
        invalid_utf8 = cwd / "invalid_utf8.tc"
        invalid_utf8.write_bytes(b"#program\n/// ab \xc3\xa9 \xff\n")
        malformed_utf8 = [
            (b"#program\n/// ab \x80\n", 16),
            (b"#program\n/// ab \xe0\x80\x80\n", 17),
            (b"#program\n/// ab \xc3\x28\n", 17),
            (b"#program\n/// ab \xe2\x82", 18),
        ]
        imported_utf8 = cwd / "imported_utf8"
        imported_utf8.mkdir()
        (imported_utf8 / "root.tc").write_text(
            "#program\nimport lib\n", encoding="utf-8"
        )
        (imported_utf8 / "lib.tc").write_bytes(b"#lib\n/// ab \xff\n")
        pipe_source = cwd / "pipe_main.tc"
        pipe_source.write_text(
            "#program\n"
            + "_ = writeln int64, 123456789012345\n" * 5000,
            encoding="utf-8",
        )

        project_cwd = cwd / "project"
        project_cwd.mkdir()
        (project_cwd / "keep.bin").write_bytes(b"unchanged\x00payload")
        project_before = tree_snapshot(project_cwd)
        check_project_cli(project_tool, project_cwd, project_before)
        result = run(project_tool, ["--help"], project_cwd)
        check(result.returncode == 0, "tc --help exit 0")
        check("--init-project" in result.stdout and "--test" in result.stdout,
              "tc --help documents project operations")
        check(result.stderr == "", "tc --help stderr empty")
        result = run(project_tool, ["--version"], project_cwd)
        check(result.returncode == 0, "tc --version exit 0")
        check(result.stdout == "tc 0.0.47 (language 0.0.46; project tool)\n",
              "tc exact dual-version output")
        check(result.stderr == "", "tc --version stderr empty")
        for args in (["--help"], ["--version"]):
            result = run_closed(project_tool, args, project_cwd, 1)
            check(result.returncode == 70,
                  f"tc {args!r} closed stdout exit {result.returncode}")
            check(result.stderr == b"",
                  f"tc {args!r} closed stdout must not write stderr")
        for args in ([], ["--test"]):
            result = run_closed(project_tool, args, project_cwd, 2)
            check(result.returncode == 70,
                  f"tc {args!r} closed stderr exit {result.returncode}")
            check(result.stdout == b"",
                  f"tc {args!r} closed stderr must not write stdout")
        with tempfile.TemporaryDirectory(prefix="tc-init-closed-stderr-") as init_tmp:
            init_cwd = Path(init_tmp)
            result = run_closed(project_tool, ["--init-project", "demo"],
                                init_cwd, 2)
            check(result.returncode == 0 and
                  result.stdout.startswith(b'initialized project "demo"'),
                  "tc init with closed stderr succeeds")
            check((init_cwd / "tc.project").is_file(),
                  "tc init with closed stderr commits files")
        with tempfile.TemporaryDirectory(prefix="tc-init-closed-stdout-") as init_tmp:
            init_cwd = Path(init_tmp)
            result = run_closed(project_tool, ["--init-project", "demo"],
                                init_cwd, 1)
            check(result.returncode == 70,
                  "tc init with closed stdout exits 70")
            check((init_cwd / "tc.project").is_file(),
                  "tc init report failure keeps committed files")
        check(tree_snapshot(project_cwd) == project_before,
              "closed-descriptor non-init project commands must not change files")

        versions = {
            "tc-vm": "tc-vm 0.0.47 (language 0.0.46; reference VM)\n",
            "tc-aot": "tc-aot 0.0.47 (language 0.0.46; native AOT)\n",
        }
        for tool in tools:
            name = Path(tool).name
            r = run(tool, ["--help"], cwd)
            check(r.returncode == 0, f"{name} --help exit {r.returncode}")
            check("usage:" in r.stdout, f"{name} --help stdout")
            check("--syntax-only" in r.stdout, f"{name} --help documents --syntax-only")
            check(r.stderr == "", f"{name} --help stderr must be empty")

            r = run(tool, ["--version"], cwd)
            check(r.returncode == 0, f"{name} --version exit {r.returncode}")
            check(r.stdout == versions[name], f"{name} exact dual-version output")
            check(r.stderr == "", f"{name} --version stderr empty")

            for args in (["--help"], ["--version"]):
                closed = run_closed(tool, args, cwd, 1)
                check(closed.returncode == 70,
                      f"{name} {args!r} closed stdout exit "
                      f"{closed.returncode}")
                check(closed.stderr == b"",
                      f"{name} {args!r} closed stdout stderr empty")
            for args in ([], ["--bogus", str(source)], [str(bad)],
                         [str(missing)]):
                closed = run_closed(tool, args, cwd, 2)
                check(closed.returncode == 70,
                      f"{name} {args!r} closed stderr exit "
                      f"{closed.returncode}")
                check(closed.stdout == b"",
                      f"{name} {args!r} closed stderr stdout empty")
            if name == "tc-aot":
                closed = run_closed(tool, [str(source)], cwd, 1)
                check(closed.returncode == 70,
                      f"{name} closed assembly stdout exit "
                      f"{closed.returncode}")

            r = run(tool, [], cwd)
            check(r.returncode == 2, f"{name} missing source exit {r.returncode}")
            check("usage:" in r.stderr, f"{name} missing source usage")

            r = run(tool, ["--bogus", str(source)], cwd)
            check(r.returncode == 2, f"{name} unknown option exit {r.returncode}")

            r = run(tool, ["--profile=bogus", str(source)], cwd)
            check(r.returncode == 2, f"{name} unknown profile exit {r.returncode}")

            r = run(tool, ["--opt=O1", str(source)], cwd)
            check(r.returncode == 2, f"{name} invalid opt exit {r.returncode}")

            for extra in ([], ["--checked"], ["--opt=O2"], ["--profile=aarch64-baremetal"]):
                r = run(tool, [*extra, str(source)], cwd)
                if name == "tc-vm":
                    check(
                        r.returncode == 0,
                        f"{name} {' '.join(extra) or 'default'} run exit {r.returncode}",
                    )
                    check(
                        r.stdout == "" and r.stderr == "",
                        f"{name} {' '.join(extra) or 'default'} silent success",
                    )
                elif extra == ["--profile=aarch64-baremetal"]:
                    check(
                        r.returncode == 0,
                        f"{name} {' '.join(extra)} emit exit {r.returncode}",
                    )
                    check(
                        "tc_main:" in r.stdout,
                        f"{name} {' '.join(extra)} bare-metal assembly",
                    )
                else:
                    check(
                        r.returncode == 0,
                        f"{name} {' '.join(extra) or 'default'} emit exit {r.returncode}",
                    )
                    check(
                        "tc_main:" in r.stdout,
                        f"{name} {' '.join(extra) or 'default'} assembly on stdout",
                    )
                    check(r.stderr == "", f"{name} {' '.join(extra) or 'default'} stderr empty")

            r = run(tool, [str(run_source)], cwd)
            if name == "tc-vm":
                check(r.returncode == 0, f"{name} run stdout exit {r.returncode}")
                check(r.stdout == "42\n", f"{name} run stdout bytes {r.stdout!r}")
                check(r.stderr == "", f"{name} run stderr empty")
            else:
                check(r.returncode == 0, f"{name} run emit exit {r.returncode}")
                check("tc_main:" in r.stdout, f"{name} run assembly stdout")
                check(r.stderr == "", f"{name} run stderr empty")

            for extra in ([], ["--sem-only"], ["--static-only"],
                          ["--syntax-only"]):
                r = run(tool, [*extra, str(invalid_utf8)], cwd)
                label = " ".join(extra) or "default"
                check(
                    r.returncode == 1,
                    f"{name} invalid UTF-8 {label} exit {r.returncode}",
                )
                check(
                    "TC_CE_SYNTAX" in r.stderr
                    and "error[LEX]" in r.stderr
                    and r.stderr.count("invalid byte offset 19") == 1,
                    f"{name} invalid UTF-8 {label} byte-offset note",
                )

            for index, (malformed, offset) in enumerate(malformed_utf8):
                malformed_path = cwd / f"malformed_utf8_{index}.tc"
                malformed_path.write_bytes(malformed)
                for extra in ([], ["--sem-only"], ["--static-only"],
                              ["--syntax-only"]):
                    r = run(tool, [*extra, str(malformed_path)], cwd)
                    label = " ".join(extra) or "default"
                    check(
                        r.returncode == 1
                        and "TC_CE_SYNTAX" in r.stderr
                        and r.stderr.count(
                            f"invalid byte offset {offset}"
                        ) == 1,
                        f"{name} malformed UTF-8 {index} {label} offset "
                        f"{offset}",
                    )

            for extra in ([], ["--sem-only"], ["--static-only"]):
                r = run(
                    tool,
                    [
                        *extra,
                        f"--include={imported_utf8}",
                        str(imported_utf8 / "root.tc"),
                    ],
                    cwd,
                )
                label = " ".join(extra) or "default"
                check(
                    r.returncode == 1
                    and "TC_CE_SYNTAX" in r.stderr
                    and r.stderr.count("invalid byte offset 12") == 1
                    and "lib.tc" in r.stderr,
                    f"{name} imported UTF-8 {label} offset note",
                )

            if name == "tc-vm":
                broken_pipe = subprocess.run(
                    [
                        "bash",
                        "-o",
                        "pipefail",
                        "-c",
                        f"{shlex.quote(str(Path(tool).resolve()))} "
                        f"{shlex.quote(str(pipe_source))} | head -c 1 "
                        ">/dev/null",
                    ],
                    cwd=cwd,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                         "PATH": "/usr/bin:/bin"},
                )
                check(
                    broken_pipe.returncode == 1
                    and "TC_RE_IO" in broken_pipe.stderr,
                    f"{name} broken pipe reports TC_RE_IO",
                )
                closed = subprocess.run(
                    [
                        "bash",
                        "-c",
                        f"{shlex.quote(str(Path(tool).resolve()))} "
                        f"{shlex.quote(str(run_source))} >&-",
                    ],
                    cwd=cwd,
                    capture_output=True,
                    text=True,
                    timeout=20,
                    env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                         "PATH": "/usr/bin:/bin"},
                )
                check(
                    closed.returncode == 1
                    and closed.stdout == ""
                    and "TC_RE_IO" in closed.stderr,
                    f"{name} closed stdout reports TC_RE_IO",
                )

            r = run(tool, [str(trap_source)], cwd)
            if name == "tc-vm":
                check(r.returncode == 1, f"{name} trap exit {r.returncode}")
                check(r.stdout == "", f"{name} trap stdout empty")
                check(
                    r.stderr.count("error[RT]") == 1
                    and "TC_RE_DIV_ZERO" in r.stderr,
                    f"{name} trap stderr contract",
                )
            else:
                check(r.returncode == 0, f"{name} trap emit exit {r.returncode}")
                check("tc_main:" in r.stdout, f"{name} trap assembly stdout")
                check(r.stderr == "", f"{name} trap stderr empty")

            r = run(tool, ["--syntax-only", str(source)], cwd)
            check(r.returncode == 0, f"{name} --syntax-only exit {r.returncode}")
            check(r.stdout == "", f"{name} --syntax-only stdout empty")
            check(r.stderr == "", f"{name} --syntax-only stderr empty")

            r = run(tool, ["--sem-only", str(source)], cwd)
            check(r.returncode == 0, f"{name} --sem-only exit {r.returncode}")
            check(r.stdout == "", f"{name} --sem-only stdout empty")
            check(r.stderr == "", f"{name} --sem-only stderr empty")

            r = run(tool, ["--static-only", str(source)], cwd)
            check(r.returncode == 0, f"{name} --static-only exit {r.returncode}")
            check(r.stdout == "", f"{name} --static-only stdout empty")
            check(r.stderr == "", f"{name} --static-only stderr empty")

            # --static-only includes CT and IR finalization; a CT failure is a
            # stable CT diagnostic, while --sem-only stops before CT.
            r = run(tool, ["--static-only", str(ct_root)], cwd)
            check(
                r.returncode == 1,
                f"{name} --static-only CT failure exit {r.returncode}",
            )
            check(
                "TC_CE_CONST_DIV_ZERO" in r.stderr and "error[CT]" in r.stderr,
                f"{name} --static-only CT failure diagnostic",
            )
            r = run(tool, ["--sem-only", str(ct_root)], cwd)
            check(
                r.returncode == 0,
                f"{name} --sem-only partial pass exit {r.returncode}",
            )

            r = run(tool, ["--syntax-only", str(bad)], cwd)
            check(r.returncode == 1, f"{name} --syntax-only bad exit {r.returncode}")
            check(r.stdout == "", f"{name} --syntax-only bad stdout empty")
            check(
                r.stderr.count(DIAG_PREFIX) == 1,
                f"{name} --syntax-only bad exactly-one diagnostic",
            )
            check("TC_CE_SYNTAX" in r.stderr, f"{name} --syntax-only bad code")
            check("error[LEX]" in r.stderr, f"{name} --syntax-only bad stage")

            r = run(tool, [str(bad)], cwd)
            check(r.returncode == 1, f"{name} default bad exit {r.returncode}")
            check(r.stdout == "", f"{name} default bad stdout empty")
            check(
                r.stderr.count(DIAG_PREFIX) == 1,
                f"{name} default bad exactly-one diagnostic",
            )

            r = run(tool, [str(missing)], cwd)
            check(r.returncode == 70, f"{name} missing file exit {r.returncode}")
            check("tc_error_io" in r.stderr, f"{name} missing file impl error")
            check("error[" not in r.stderr, f"{name} missing file is not a language diagnostic")

            if name == "tc-aot":
                result = run(
                    tool,
                    ["--emit=asm", f"--output={cwd / 'missing-parent' / 'out.s'}", str(source)],
                    cwd,
                )
                check(result.returncode == 70, f"{name} output IO exit {result.returncode}")
                check("tc_error_io" in result.stderr, f"{name} output IO error channel")
                check("error[" not in result.stderr, f"{name} output IO is not a diagnostic")

            first = run(tool, ["--syntax-only", str(source)], cwd)
            second = run(tool, ["--syntax-only", str(source)], cwd)
            check(
                (first.returncode, first.stdout, first.stderr)
                == (second.returncode, second.stdout, second.stderr),
                f"{name} repeated run deterministic",
            )
    if FAILS:
        print(f"cli contract: {FAILS} failure(s)")
        return 1
    print("cli contract: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
