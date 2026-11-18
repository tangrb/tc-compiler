#!/usr/bin/env python3
"""Adversarial static-parity fuzz for tc-vm and tc-aot.

The static front end is shared, so any mutation of a conformance source must
produce byte-identical exit status, stdout and stderr from
``tc-vm --static-only`` and ``tc-aot --static-only``.  This suite mutates a
deterministic sample of the static corpus (byte flips, insertions, deletions,
truncation and invalid bytes) and checks four properties:

  - both tools agree exactly;
  - a rejected source reports exactly one ``error[...]`` line and no warning
    output;
  - an accepted source prints nothing;
  - neither tool crashes or exceeds the per-run timeout.

Run:
    python3 tests/conformance/tools/s8_static_fuzz.py --check
"""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STATIC = ROOT / "tests" / "conformance" / "static"
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

CASE_ID = "TC-SFUZZ-S8-0001"


def corpus() -> list[Path]:
    files = []
    for path in sorted(STATIC.rglob("root.tc")):
        files.append(path)
    for path in sorted(STATIC.rglob("*.tc")):
        if path.name != "root.tc":
            continue
    return files


def mutate(data: bytes, rng: random.Random) -> bytes:
    out = bytearray(data)
    kind = rng.randrange(6)
    if kind == 0 and out:
        pos = rng.randrange(len(out))
        out[pos] ^= 1 << rng.randrange(8)
    elif kind == 1:
        pos = rng.randrange(len(out) + 1)
        out[pos:pos] = bytes([rng.randrange(32, 127)])
    elif kind == 2 and out:
        pos = rng.randrange(len(out))
        del out[pos]
    elif kind == 3 and len(out) > 1:
        out = out[: rng.randrange(1, len(out))]
    elif kind == 4:
        pos = rng.randrange(len(out) + 1)
        out[pos:pos] = bytes([rng.choice([0x00, 0x80, 0xC0, 0xC3, 0xFF])])
    elif out:
        pos = rng.randrange(len(out))
        out[pos] = rng.randrange(256)
    return bytes(out)


def run_tool(tool: str, source: Path, include: Path,
             profile: str) -> tuple[int, bytes, bytes]:
    argv = [tool, "--static-only", f"--profile={profile}",
            f"--include={include}", str(source)]
    proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=20)
    return proc.returncode, proc.stdout, proc.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--profile", default="aarch64-linux-user")
    parser.add_argument("--seeds", type=int, default=240)
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    files = corpus()
    if not files:
        print(f"{CASE_ID}: no static corpus found")
        return 1
    failures: list[str] = []
    vm = engine.tc_vm_path()
    aot = engine.tc_aot_path()
    for seed in range(args.seeds):
        rng = random.Random(seed)
        base = files[rng.randrange(len(files))]
        data = base.read_bytes()
        mutated = mutate(data, rng)
        directory = Path(tempfile.mkdtemp(prefix="tc-sfuzz-"))
        try:
            source = directory / "root.tc"
            source.write_bytes(mutated)
            # The original fixture may import a sibling module; keep it visible.
            include = base.parent
            vm_result = run_tool(vm, source, include, args.profile)
            aot_result = run_tool(aot, source, include, args.profile)
        except subprocess.TimeoutExpired as exc:
            failures.append(f"seed {seed} ({base.name}): timeout: {exc}")
            continue
        finally:
            import shutil
            shutil.rmtree(directory, ignore_errors=True)
        if vm_result != aot_result:
            failures.append(
                f"seed {seed} ({base.relative_to(ROOT)}): VM/AOT differ "
                f"(VM rc={vm_result[0]}, AOT rc={aot_result[0]}, "
                f"VM err={vm_result[2]!r}, AOT err={aot_result[2]!r})")
            continue
        rc, stdout, stderr = vm_result
        if rc not in (0, 1):
            failures.append(
                f"seed {seed} ({base.relative_to(ROOT)}): unexpected exit {rc}")
            continue
        text = stderr.decode("utf-8", "replace")
        if "warning[" in text:
            failures.append(f"seed {seed}: warning output: {text!r}")
        if rc == 1:
            if text.count("error[") != 1:
                failures.append(
                    f"seed {seed}: {text.count('error[')} diagnostics: {text!r}")
            if "TC_CE_" not in text:
                failures.append(f"seed {seed}: no TC_CE_ code: {text!r}")
        if rc == 0 and stdout:
            failures.append(f"seed {seed}: accepted source printed to stdout")
        if failures:
            break
    if failures:
        print(f"{CASE_ID}: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    print(f"{CASE_ID}: {args.seeds} mutated sources agree in VM/AOT static results")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
