#!/usr/bin/env python3
"""Manual audit: the ARM32 runtime must not execute binary64 in an FPU.

The target keeps the softfp ABI and uses FPv5-D for binary32, but the standard
requires binary64 to be software.  This audit scans every ARM32 runtime object
for double-register floating-point instructions; any hit is a regression.

Run:
    python3 tests/tools/s9_softfloat_audit.py --check
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATTERN = re.compile(
    r"\b(fadd|fsub|fmul|fdiv|fmac|fmsc|fnmul|fneg|fabs|fsqrt|fcmp|"
    r"fcvt|scvtf|ucvtf|fcvtzs|fcvtzu|vadd|vsub|vmul|vdiv|vmla|vmls|"
    r"vcmp|vcvt)\b[^\\n]*\\bd[0-9]+\\b")


def find_objdump() -> str | None:
    for name in ("llvm-objdump", "llvm-objdump-23", "llvm-objdump-22",
                 "llvm-objdump-21", "llvm-objdump-20"):
        path = shutil.which(name)
        if path:
            return path
    env = os.environ.get("TC_LLVM_OBJDUMP")
    if env and Path(env).is_file():
        return env
    for directory in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin",
                      "/opt/homebrew/bin", "/usr/local/bin"):
        path = Path(directory) / "llvm-objdump"
        if path.is_file():
            return str(path)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    objdump = find_objdump()
    if objdump is None:
        print("softfloat.audit: llvm-objdump not found; set TC_LLVM_OBJDUMP")
        return 1
    runtime = ROOT / "build/src/arm32-runtime"
    if not runtime.is_dir():
        print(f"softfloat.audit: missing {runtime}")
        return 1
    objects = sorted(runtime.rglob("*.o"))
    if not objects:
        print("softfloat.audit: no ARM32 runtime objects")
        return 1
    hits = 0
    for obj in objects:
        proc = subprocess.run([objdump, "-d", str(obj)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=120)
        text = proc.stdout.decode("utf-8", "replace")
        if proc.returncode != 0:
            print(f"softfloat.audit: objdump failed for {obj}")
            return 1
        for line in text.splitlines():
            if PATTERN.search(line):
                hits += 1
                print(f"softfloat.audit: double FPU instruction in {obj}: "
                      f"{line.strip()}")
    if hits:
        print(f"softfloat.audit: {hits} double FPU instruction(s)")
        return 1
    print(f"softfloat.audit: {len(objects)} ARM32 runtime object(s) contain "
          f"no double FPU instructions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
