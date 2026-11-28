#!/usr/bin/env python3
"""Unified local and CI test entry point."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TMP = ROOT / ".tmp"


def run(command: list[str]) -> int:
    env = dict(os.environ)
    env["TMPDIR"] = str(TMP)
    try:
        return subprocess.run(command, cwd=ROOT, env=env, check=False).returncode
    except OSError as exc:
        print(f"tests/run.py: cannot execute {command[0]}: {exc}", file=sys.stderr)
        return 70


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="tests/run.py")
    parser.add_argument("--preset", default="fast",
                        choices=("fast", "ci", "release"))
    parser.add_argument("--build-dir")
    parser.add_argument("--label", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("ctest_args", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    TMP.mkdir(exist_ok=True)
    ctest_args = args.ctest_args
    if ctest_args and ctest_args[0] == "--":
        ctest_args = ctest_args[1:]
    if args.list:
        if args.build_dir:
            build_dir = str((ROOT / args.build_dir).resolve())
            command = ["ctest", "--test-dir", build_dir, "--show-only=json-v1"]
        else:
            command = ["ctest", "--preset", args.preset, "--show-only=json-v1"]
        return run(command)
    if args.build_dir:
        build_dir = str((ROOT / args.build_dir).resolve())
        if not args.no_build:
            status = run(["cmake", "-S", str(ROOT), "-B", build_dir])
            if status != 0:
                return status
    else:
        status = run(["cmake", "--preset", args.preset])
        if status != 0:
            return status
    if args.configure_only:
        return 0
    if not args.no_build:
        build_command = (["cmake", "--build", build_dir] if args.build_dir else
                         ["cmake", "--build", "--preset", args.preset])
        if args.jobs is not None:
            build_command.extend(["-j", str(args.jobs)])
        status = run(build_command)
        if status != 0:
            return status
    test_command = (["ctest", "--test-dir", build_dir] if args.build_dir else
                    ["ctest", "--preset", args.preset])
    labels = args.label or (["fast"] if args.preset == "fast" else [])
    for label in labels:
        test_command.extend(["-L", label])
    test_command.extend(["--output-on-failure"])
    test_command.extend(ctest_args)
    return run(test_command)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
