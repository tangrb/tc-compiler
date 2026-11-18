#!/usr/bin/env python3
"""CLI contract test for tc-vm / tc-aot.

The static front end is implemented.  `--syntax-only` (LEX/SYN), `--sem-only`
(through SEM) and `--static-only` (through CT and IR finalization) are partial
checks with exit 0, no output.  `tc-vm` executes a normal invocation on the
reference VM and reports the first `TC_RE_*` trap on stderr with
`error[RT]`; `tc-aot` emits target assembly on a normal invocation.
Capabilities outside this hosted platform instance (a different word size or
the bare-metal device policy) still fail explicitly with the
implementation-level "unavailable" status (70).  Diagnostics exit 1 with the stable diagnostic line; file-open
failures are implementation errors (71) and are never language diagnostics.
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


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: cli_contract_test.py <tc-vm> <tc-aot>", file=sys.stderr)
        return 2
    tools = sys.argv[1:]
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

        for tool in tools:
            name = Path(tool).name
            r = run(tool, ["--help"], cwd)
            check(r.returncode == 0, f"{name} --help exit {r.returncode}")
            check("usage:" in r.stdout, f"{name} --help stdout")
            check("--syntax-only" in r.stdout, f"{name} --help documents --syntax-only")
            check(r.stderr == "", f"{name} --help stderr must be empty")

            r = run(tool, ["--version"], cwd)
            check(r.returncode == 0, f"{name} --version exit {r.returncode}")
            check("0.0.46" in r.stdout, f"{name} --version version")

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

            if name == "tc-vm":
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
            check(r.returncode == 71, f"{name} missing file exit {r.returncode}")
            check("tc_error_io" in r.stderr, f"{name} missing file impl error")
            check("error[" not in r.stderr, f"{name} missing file is not a language diagnostic")

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
