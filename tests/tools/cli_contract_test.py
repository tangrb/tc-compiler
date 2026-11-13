#!/usr/bin/env python3
"""S0 CLI contract test for tc-vm / tc-aot.

The tools are skeletons: valid invocations must fail explicitly with the
implementation-level "unavailable" status (70), never emit an artifact and
never claim success.  Usage errors are 2; --help/--version are 0.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

FAILS = 0


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
        source.write_text("#program\n", encoding="utf-8")
        for tool in tools:
            name = Path(tool).name
            r = run(tool, ["--help"], cwd)
            check(r.returncode == 0, f"{name} --help exit {r.returncode}")
            check("usage:" in r.stdout, f"{name} --help stdout")
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
    if FAILS:
        print(f"cli contract: {FAILS} failure(s)")
        return 1
    print("cli contract: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
