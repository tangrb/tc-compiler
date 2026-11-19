#!/usr/bin/env python3
"""S10 far-branch regression for riscv32-baremetal.

A single function larger than the RISC-V 1 MiB `j` range cannot use plain
`j`/`beq` relocations for a block or return that is more than 1 MiB away.  The
emitter must lower such transfers with the auipc/jalr long-jump sequence.  This
case builds a 30000-instruction top-level program whose only executed branch
skips over the whole body; before the long-jump lowering, clang rejected the
emitted `j` with "fixup value out of range".

Run:
    python3 tests/conformance/platform/s10_long_branch.py --check
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import arm32_engine as engine  # noqa: E402

PROFILE = "riscv32-baremetal"
CASE_ID = "TC-RISCV32-S10-0003"
LINES = 30000


def make_source() -> str:
    lines = ["#program", "_ = br far"]
    lines.extend("_ = writeln int32, 1" for _ in range(LINES))
    lines.extend(["label far:", "_ = writeln int32, 2"])
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    directory = Path(tempfile.mkdtemp(prefix="tc-s10-longbr-"))
    failures: list[str] = []
    try:
        source = directory / "root.tc"
        source.write_text(make_source(), encoding="utf-8")
        output = directory / "out.elf"
        proc = subprocess.run(
            [engine.tc_aot_path(), f"--profile={PROFILE}", "--emit=elf",
             f"--output={output}", f"--include={directory}",
             f"--ld={engine.lld_path()}", str(source)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
        if proc.returncode != 0:
            failures.append(f"{CASE_ID}: build exit {proc.returncode}: "
                            f"{proc.stderr.decode('utf-8', 'replace')[:300]}")
        else:
            observed = engine.run_riscv32_elf(output, b"", directory)
            if observed.stdout != b"2\n" or observed.trap is not None:
                failures.append(
                    f"{CASE_ID}: stdout {observed.stdout!r} trap "
                    f"{observed.trap}")
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    if failures:
        for item in failures:
            print(item)
        return 1
    print(f"{CASE_ID}: {LINES}-instruction far branch passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
