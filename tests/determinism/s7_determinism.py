#!/usr/bin/env python3
"""S7 determinism checks: zero-pass assembly and stable qemu execution.

The zero-pass contract (design §10.1) makes O0 and O2 the same IR pipeline, so
their assembly must be byte-identical.  Repeating a build must also produce the
same `.s`, and repeating the linked image must produce the same observable
behavior (stdout, first trap, exit status).
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

SOURCE = """#program
import lib
var i: int32 = copy int32, 0
var s: int32 = copy int32, 0
label loop:
var c: bool = lt int32, i, 5
_ = br_if c, body
_ = br done
label body:
s = add int32, s, i
i = add int32, i, 1
_ = br loop
label done:
_ = writeln int32, s
var k: int32 = call lib.mix, 7, 3
_ = writeln int32, k
var x: int16 = read int16
_ = writeln int16, x
"""

LIB = """#lib
public func mix(a: int32, b: int32) int32 then
var q: int32 = mul int32, a, b
var r: int32 = add int32, q, 1
_ = return r
end
"""


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aot(source: Path, include: Path, output: Path, opt: str,
        emit: str = "asm") -> int:
    import subprocess
    argv = [engine.tc_aot_path(), f"--emit={emit}", f"--output={output}",
            f"--profile=aarch64-linux-user", f"--include={include}",
            f"--opt={opt}", str(source)]
    return subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=120).returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    failures: list[str] = []
    work = Path(tempfile.mkdtemp(prefix="tc-s7-det-"))
    try:
        (work / "root.tc").write_text(SOURCE, encoding="utf-8")
        (work / "lib.tc").write_text(LIB, encoding="utf-8")
        source = work / "root.tc"
        digests = []
        for index in range(3):
            out = work / f"det{index}.s"
            code = aot(source, work, out, "O2" if index == 2 else "O0")
            if code != 0:
                failures.append(f"assembly build {index} failed with {code}")
                break
            digests.append(sha(out))
        if len(set(digests)) > 1:
            failures.append("O0/O2 or repeated .s output is not byte-identical")

        case = engine.Case(
            case_id="TC-DET-S7-0001",
            profile="aarch64-linux-user",
            files={"root.tc": SOURCE, "lib.tc": LIB},
            stdout="10\n22\n-7\n",
            trap=None,
            exit=0,
            stdin=b"-7\n",
        )
        elf = work / "det.elf"
        second_elf = work / "det2.elf"
        if aot(source, work, elf, "O2", emit="elf") != 0:
            failures.append("first ELF build failed")
        elif aot(source, work, second_elf, "O2", emit="elf") != 0:
            failures.append("second ELF build failed")
        else:
            if elf.read_bytes() != second_elf.read_bytes():
                failures.append("repeated ELF builds are not byte-identical")
            observations = []
            for index in range(3):
                obs = engine.run_elf(elf if index < 2 else second_elf,
                                     case.stdin, work / f"det{index}.diag",
                                     work)
                observations.append((obs.stdout, obs.trap, obs.exit))
            if len(set(observations)) != 1:
                failures.append(f"qemu observations are not stable: {observations}")
            if observations[0] != (b"10\n22\n-7\n", None, 0):
                failures.append(f"unexpected observation {observations[0]}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"s7.determinism: {len(failures)} failure(s)")
        for item in failures:
            print(f"  {item}")
        return 1
    print("s7.determinism: O0/O2 and repeated build/run outputs stable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
