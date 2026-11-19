#!/usr/bin/env python3
"""S5 determinism and order-perturbation checks.

Repeats every S5 source three times with a fixed environment, then runs the
whole harness over the S5 case directories under different PYTHONHASHSEED
values and with the YAML arguments reversed.  CT values, CT diagnostics and
the finalized IR are deterministic under these perturbations.

Usage:
    python3 tests/determinism/s5_determinism.py <tc-vm> <adapter> <suite> <workdir>
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

FAILS = 0


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def _env(hash_seed: str) -> dict:
    return {
        "LC_ALL": "C",
        "LANG": "C",
        "TZ": "UTC",
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "PYTHONHASHSEED": hash_seed,
    }


def _run(argv, env, timeout=180):
    return subprocess.run(
        argv, capture_output=True, timeout=timeout, env=env, cwd=os.getcwd()
    )


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: s5_determinism.py <tc-vm> <adapter> <suite> <workdir>",
            file=sys.stderr,
        )
        return 2
    tool = Path(sys.argv[1]).resolve()
    adapter = Path(sys.argv[2]).resolve()
    suite = Path(sys.argv[3]).resolve()
    workdir = Path(sys.argv[4]).resolve()
    workdir.mkdir(parents=True, exist_ok=True)
    runner = Path(__file__).resolve().parents[1] / "harness" / "runner.py"
    roots = [
        suite / "static" / "ct",
        suite / "static" / "accept_s5",
    ]
    sources = sorted(path for root in roots for path in root.rglob("*.tc"))
    check(bool(sources), "no S5 sources found")
    for source in sources:
        runs = []
        for _ in range(3):
            proc = _run([str(tool), "--static-only", str(source)], _env("0"))
            runs.append((proc.returncode, proc.stdout, proc.stderr))
        check(
            runs[0] == runs[1] == runs[2],
            f"repeated run differs for {source.relative_to(suite)}",
        )

    reports = []
    for index, seed in enumerate(("0", "1", "42")):
        out = workdir / f"s5-report-{index}.json"
        proc = _run(
            [
                sys.executable,
                str(runner),
                *[str(root) for root in roots],
                "--adapter",
                str(adapter),
                "--json-out",
                str(out),
            ],
            _env(seed),
        )
        check(proc.returncode == 0, f"runner failed with hash seed {seed}")
        reports.append(out.read_bytes())
    check(
        reports[0] == reports[1] == reports[2],
        "S5 harness JSON changes with PYTHONHASHSEED",
    )

    yaml_files = sorted(path for root in roots for path in root.rglob("*.yaml"))
    reversed_out = workdir / "s5-report-reversed.json"
    proc = _run(
        [
            sys.executable,
            str(runner),
            *[str(path) for path in reversed(yaml_files)],
            "--adapter",
            str(adapter),
            "--json-out",
            str(reversed_out),
        ],
        _env("7"),
    )
    check(proc.returncode == 0, "runner failed for reversed case order")
    check(
        reversed_out.read_bytes() == reports[0],
        "S5 harness JSON changes with case argument order",
    )

    if FAILS:
        print(f"s5 determinism: {FAILS} failure(s)")
        return 1
    print(
        f"s5 determinism: {len(sources)} sources x3, {len(yaml_files)} cases "
        "across hash seeds/order passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
