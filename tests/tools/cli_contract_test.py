#!/usr/bin/env python3
"""S1 CLI contract test for tc-vm / tc-aot.

S1 provides LEX/SYN.  A well-formed source has no diagnostic under
`--syntax-only` (exit 0, no output), while a normal invocation still fails
explicitly with the implementation-level "unavailable" status (70) because
IMPORT/SEM/CT and code generation start in later phases.  LEX/SYN diagnostics
exit 1 with the stable diagnostic line; file-open failures are implementation
errors (71) and are never language diagnostics.
"""

from __future__ import annotations

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
        missing = cwd / "missing.tc"

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
                check(
                    r.returncode == 70,
                    f"{name} {' '.join(extra) or 'default'} unavailable exit {r.returncode}",
                )
                check(
                    "tc_error_unavailable" in r.stderr,
                    f"{name} {' '.join(extra) or 'default'} stderr contract",
                )
                check(r.stdout == "", f"{name} {' '.join(extra) or 'default'} stdout empty")

            r = run(tool, ["--syntax-only", str(source)], cwd)
            check(r.returncode == 0, f"{name} --syntax-only exit {r.returncode}")
            check(r.stdout == "", f"{name} --syntax-only stdout empty")
            check(r.stderr == "", f"{name} --syntax-only stderr empty")

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
