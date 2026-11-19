#!/usr/bin/env python3
"""S6 runtime determinism and order-perturbation checks.

Every S6 runtime case is executed three times with a fixed environment and its
YAML-provided stdin; stdout, stderr, trap field and exit status must be
byte-identical.  The whole harness is then run under different
PYTHONHASHSEED values and with the YAML arguments reversed; the JSON reports
must be byte-identical.  W=32 and W=64 cases remain separate.

Usage:
    python3 tests/determinism/s6_determinism.py <tc-vm> <adapter> <suite> <workdir>
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from harness import case_schema  # noqa: E402

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


def _run(argv, env, timeout=60, stdin_bytes=b""):
    return subprocess.run(
        argv,
        input=stdin_bytes,
        capture_output=True,
        timeout=timeout,
        env=env,
        cwd=os.getcwd(),
    )


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: s6_determinism.py <tc-vm> <adapter> <suite> <workdir>",
            file=sys.stderr,
        )
        return 2
    tool = Path(sys.argv[1]).resolve()
    adapter = Path(sys.argv[2]).resolve()
    suite = Path(sys.argv[3]).resolve()
    workdir = Path(sys.argv[4]).resolve()
    workdir.mkdir(parents=True, exist_ok=True)
    runner = Path(__file__).resolve().parents[1] / "harness" / "runner.py"
    runtime = suite / "runtime"
    yaml_files = sorted(runtime.rglob("*.yaml"))
    check(bool(yaml_files), "no S6 runtime cases found")

    for yaml_path in yaml_files:
        case = case_schema.parse_case_file(str(yaml_path))
        case_dir = yaml_path.parent
        source = (case_dir / case["source"]).resolve()
        argv = [str(tool)]
        profile = case.get("profile")
        if isinstance(profile, str) and profile not in {"common", "w64"}:
            argv.append(f"--profile={profile}")
        if case.get("checked"):
            argv.append("--checked")
        if case.get("opt") is not None:
            argv.append(f"--opt={case['opt']}")
        argv.append(str(source))
        stdin_hex = ((case.get("run") or {}).get("stdin_hex", "") or "")
        stdin_bytes = bytes.fromhex("".join(stdin_hex.split()))
        runs = []
        for _ in range(3):
            proc = _run(argv + [], _env("0"), stdin_bytes=stdin_bytes)
            runs.append((proc.returncode, proc.stdout, proc.stderr))
        check(
            runs[0] == runs[1] == runs[2],
            f"repeated run differs for {yaml_path.name}",
        )

    reports = []
    for index, seed in enumerate(("0", "1", "42")):
        out = workdir / f"s6-report-{index}.json"
        proc = _run(
            [
                sys.executable,
                str(runner),
                str(runtime),
                "--adapter",
                str(adapter),
                "--json-out",
                str(out),
            ],
            _env(seed),
            timeout=300,
        )
        check(proc.returncode == 0, f"runner failed with hash seed {seed}")
        reports.append(out.read_bytes())
    check(
        reports[0] == reports[1] == reports[2],
        "S6 harness JSON changes with PYTHONHASHSEED",
    )

    reversed_out = workdir / "s6-report-reversed.json"
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
        timeout=300,
    )
    check(proc.returncode == 0, "runner failed for reversed case order")
    check(
        reversed_out.read_bytes() == reports[0],
        "S6 harness JSON changes with case argument order",
    )

    if FAILS:
        print(f"s6 determinism: {FAILS} failure(s)")
        return 1
    print(
        f"s6 determinism: {len(yaml_files)} cases x3 runs and harness "
        "hash-seed/order perturbations passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
