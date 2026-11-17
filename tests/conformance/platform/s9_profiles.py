#!/usr/bin/env python3
"""S9 profile isolation: W=32 and W=64 static results stay separate.

The word-size matrix is never evaluated across profiles.  Each case names the
expected result for the 32-bit (`arm32-baremetal`) and 64-bit
(`aarch64-linux-user`) profiles and runs the program on the reference VM for
that profile; AOT static parity is covered separately by the S9 static parity
suite.  The differences below are the published §13.2.2 W-dependent outcomes.

Run:
    python3 tests/conformance/platform/s9_profiles.py --check
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

CASE_ID = "TC-PROF-S9-0001"
PROFILE32 = "arm32-baremetal"
PROFILE64 = "aarch64-linux-user"
DIAG_RE = re.compile(r"TC_CE_[A-Z0-9_]+")


def program(*lines: str) -> str:
    return "#program\n" + "".join(line + "\n" for line in lines)


# Each case: (name, source, expected32, expected64); an expectation is
# ("out", text), ("diag", code) or ("ok", None) for accepted no-output code.
CASES = [
    (
        "sizeof W-sized types",
        program("var a: usize = sizeof usize",
                "_ = writeln usize, a",
                "var b: usize = sizeof ptr<int32>",
                "_ = writeln usize, b"),
        ("out", "4\n4\n"), ("out", "8\n8\n"),
    ),
    (
        "isize literal range",
        program("var a: isize = copy isize, 2147483648",
                "_ = writeln isize, a"),
        ("diag", "TC_CE_LITERAL_RANGE"), ("out", "2147483648\n"),
    ),
    (
        "bitcast ptr from int32",
        program("var i: int32 = copy int32, 0",
                "unsafe then",
                "var p: ptr<int32> = bitcast ptr<int32>, i",
                "end"),
        ("ok", None), ("diag", "TC_CE_BITCAST_WIDTH"),
    ),
    (
        "bitcast ptr from int64",
        program("var i: int64 = copy int64, 0",
                "unsafe then",
                "var p: ptr<int32> = bitcast ptr<int32>, i",
                "end"),
        ("diag", "TC_CE_BITCAST_WIDTH"), ("ok", None),
    ),
    (
        "trunc int32 from isize",
        program("var a: isize = copy isize, 1",
                "var b: int32 = trunc int32, a",
                "_ = writeln int32, b"),
        ("diag", "TC_CE_TRUNC_WIDTH"), ("out", "1\n"),
    ),
    (
        "trunc isize from int64",
        program("var a: int64 = copy int64, 1",
                "var b: isize = trunc isize, a",
                "_ = writeln isize, b"),
        ("out", "1\n"), ("diag", "TC_CE_TRUNC_WIDTH"),
    ),
    (
        "W-sized bitcast identity",
        program("var a: usize = copy usize, 7",
                "var b: usize = bitcast usize, a",
                "_ = writeln usize, b"),
        ("out", "7\n"), ("out", "7\n"),
    ),
]


def run_vm(profile: str, source: Path, include: Path):
    return subprocess.run(
        [engine.tc_vm_path(), f"--profile={profile}",
         f"--include={include}", str(source)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
        stdin=subprocess.DEVNULL,
        env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
             "PATH": "/usr/bin:/bin"})


def first_code(text: str) -> str | None:
    match = DIAG_RE.search(text)
    return match.group(0) if match else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    directory = Path(tempfile.mkdtemp(prefix="tc-s9-profiles-"))
    try:
        for index, (name, source, expected32, expected64) in enumerate(CASES):
            path = directory / f"case{index}.tc"
            path.write_text(source, encoding="utf-8")
            for profile, expected in ((PROFILE32, expected32),
                                      (PROFILE64, expected64)):
                result = run_vm(profile, path, directory)
                text = result.stderr.decode("utf-8", "replace")
                kind, value = expected
                if kind == "diag":
                    if first_code(text) != value:
                        failures.append(
                            f"{CASE_ID}: {name}: {profile} code "
                            f"{first_code(text)} != {value}")
                    continue
                if result.returncode != 0:
                    failures.append(
                        f"{CASE_ID}: {name}: {profile} rejected: "
                        f"{text.strip()[:160]}")
                    continue
                actual = result.stdout.decode("utf-8", "replace")
                if kind == "out" and actual != value:
                    failures.append(
                        f"{CASE_ID}: {name}: {profile} stdout {actual!r} "
                        f"!= {value!r}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        for failure in failures:
            print(failure)
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        return 1
    print(f"{CASE_ID}: {len(CASES)} W=32/W=64 profile-isolation cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
