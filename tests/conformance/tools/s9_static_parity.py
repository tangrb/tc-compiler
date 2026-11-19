#!/usr/bin/env python3
"""S9 static parity: tc-vm and tc-aot agree for each bare-metal profile.

The static front end is shared by both tools.  This suite runs
`--static-only` on every conformance `.tc` source under `arm32-baremetal`
(W=32) and separately under `aarch64-baremetal` (W=64), comparing exit status,
stdout and normalized stderr for each profile.  Results from the two profiles
are never compared with each other.

Run:
    python3 tests/conformance/tools/s9_static_parity.py --check
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STATIC = ROOT / "tests" / "conformance" / "static"
PROFILES = ("arm32-baremetal", "aarch64-baremetal")


def tool(name: str, env: str) -> str:
    value = os.environ.get(env)
    if value:
        return value
    local = ROOT / "build" / "src" / "driver" / name
    if local.is_file() and os.access(local, os.X_OK):
        return str(local)
    found = shutil.which(name)
    if not found:
        raise SystemExit(f"required tool '{name}' not found (set {env})")
    return found


def run(tool_path: str, profile: str, args: list[str]):
    return subprocess.run(
        [tool_path, "--static-only", f"--profile={profile}", *args],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        stdin=subprocess.DEVNULL, timeout=30,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
             "PATH": "/usr/bin:/bin"})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    vm = tool("tc-vm", "TC_VM")
    aot = tool("tc-aot", "TC_AOT")
    sources = sorted(STATIC.rglob("*.tc"))
    failures: list[str] = []
    for profile in PROFILES:
        for source in sources:
            include = f"--include={source.parent}"
            try:
                first = run(vm, profile, [include, str(source)])
                second = run(aot, profile, [include, str(source)])
            except subprocess.TimeoutExpired:
                failures.append(f"{profile} {source}: timed out")
                continue
            first_err = first.stderr.replace(b"tc-vm:", b"tc:")
            second_err = second.stderr.replace(b"tc-aot:", b"tc:")
            if (first.returncode, first.stdout, first_err) != (
                    second.returncode, second.stdout, second_err):
                failures.append(
                    f"{profile} {source}: VM rc={first.returncode} "
                    f"AOT rc={second.returncode}")
    if failures:
        print(f"s9.static_parity: {len(failures)} mismatch(es)")
        for item in failures[:20]:
            print(f"  {item}")
        return 1
    print(f"s9.static_parity: {len(sources)} sources identical under "
          f"{len(PROFILES)} bare-metal profiles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
