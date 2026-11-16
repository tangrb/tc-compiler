#!/usr/bin/env python3
"""S7 front-end parity: tc-vm and tc-aot must produce the same static result.

`tc-aot` shares the LEX/SYN/IMPORT/SEM/CT/IR-finalization pipeline with
`tc-vm`; the backend runs only after those stages accept the source.  This test
runs `--static-only` on every conformance `.tc` file with both tools and
compares exit status, stdout and stderr byte-for-byte.  It is the direct
evidence for design §1.9/§6.4 "AOT does not change the static result" and test
plan S7 "failure diagnostics: same first diagnostic".
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STATIC = ROOT / "tests" / "conformance" / "static"


def tool(name: str, env: str) -> str:
    value = os.environ.get(env)
    if value:
        return value
    local = ROOT / "build" / "src" / "driver" / name
    if local.is_file() and os.access(local, os.X_OK):
        return str(local)
    found = __import__("shutil").which(name)
    if not found:
        raise SystemExit(f"required tool '{name}' not found (set {env})")
    return found


def run(tool_path: str, args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool_path, "--static-only", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        stdin=subprocess.DEVNULL,
        timeout=20,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
             "PATH": "/usr/bin:/bin"},
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--limit", type=int, default=0,
                        help="check only the first N sources (debugging)")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    vm = tool("tc-vm", "TC_VM")
    aot = tool("tc-aot", "TC_AOT")
    sources = sorted(STATIC.rglob("*.tc"))
    if args.limit:
        sources = sources[: args.limit]
    failures: list[str] = []
    for source in sources:
        include = f"--include={source.parent}"
        try:
            a = run(vm, [include, str(source)])
            b = run(aot, [include, str(source)])
        except subprocess.TimeoutExpired:
            failures.append(f"{source}: static check timed out")
            continue
        a_err = a.stderr.replace(b"tc-vm:", b"tc:")
        b_err = b.stderr.replace(b"tc-aot:", b"tc:")
        if (a.returncode, a.stdout, a_err) != (b.returncode, b.stdout, b_err):
            failures.append(
                f"{source}: VM rc={a.returncode} AOT rc={b.returncode}\n"
                f"  VM stderr={a.stderr!r}\n  AOT stderr={b.stderr!r}")
    if failures:
        print(f"s7.static_parity: {len(failures)} mismatch(es) "
              f"across {len(sources)} sources")
        for item in failures[:20]:
            print(f"  {item}")
        return 1
    print(f"s7.static_parity: {len(sources)} sources identical in VM/AOT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
