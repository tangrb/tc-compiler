#!/usr/bin/env python3
"""Execute the specification-derived VM runtime goldens.

Each `kind: run` golden under `w64/` and `w32/` is run with `tc-vm` under the
matching profile and compared byte-for-byte with the reviewed expectation:
stdout bytes, the first `TC_RE_*` trap, and the exit status.  The two profile
libraries are never compared with each other.

Usage: python3 tests/golden/run.py <tc-vm>
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from validate import YamlError, parse_yaml  # noqa: E402

RT_RE = re.compile(
    r"^(?P<file>.*):(?P<line>[0-9]+):(?P<column>[0-9]+): "
    r"error\[RT\]: (?P<code>TC_RE_[A-Z0-9_]+): (?P<name>[^:\n]*)$"
)


def bytes_from_hex(value: str) -> bytes:
    return bytes.fromhex("".join(value.split()))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run.py <tc-vm>", file=sys.stderr)
        return 2
    tool = sys.argv[1]
    failures = 0
    checked = 0
    for profile in ("w64", "w32"):
        root = HERE / profile
        for path in sorted(root.glob("*.golden.yaml")):
            try:
                data = parse_yaml(path.read_text(encoding="utf-8"))
            except (OSError, YamlError) as exc:
                print(f"FAIL {path}: cannot parse: {exc}")
                failures += 1
                continue
            if not isinstance(data, dict) or data.get("kind") != "run":
                continue
            checked += 1
            expect = data["expect"]
            run = data.get("run") or {}
            stdin_bytes = bytes_from_hex(run.get("stdin_hex", ""))
            argv = [tool]
            argv.append(
                "--profile=w32"
                if data.get("profile") == "w32"
                else "--profile=aarch64-linux-user"
            )
            with tempfile.TemporaryDirectory(prefix="tc-golden-") as tmp:
                source = Path(tmp) / "root.tc"
                source.write_text(data["source"], encoding="utf-8")
                argv.append(str(source))
                proc = subprocess.run(
                    argv,
                    input=stdin_bytes,
                    capture_output=True,
                    timeout=30,
                    env={
                        "LC_ALL": "C",
                        "LANG": "C",
                        "TZ": "UTC",
                        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
                    },
                )
            stderr = proc.stderr.decode("utf-8", "replace")
            traps = [
                match
                for match in (RT_RE.match(line) for line in stderr.splitlines())
                if match
            ]
            unexpected = [
                line
                for line in stderr.splitlines()
                if line.strip() and not RT_RE.match(line)
            ]
            problems = []
            if unexpected:
                problems.append(f"unexpected stderr {unexpected!r}")
            expected_stdout = bytes_from_hex(expect["stdout_hex"])
            if proc.stdout != expected_stdout:
                problems.append(
                    f"stdout {proc.stdout.hex()} != {expected_stdout.hex()}"
                )
            expected_trap = expect["trap"]
            actual_trap = traps[0].group("code") if len(traps) == 1 else None
            if len(traps) > 1:
                problems.append("more than one runtime trap")
            if actual_trap != expected_trap:
                problems.append(f"trap {actual_trap!r} != {expected_trap!r}")
            if proc.returncode != expect["exit"]:
                problems.append(
                    f"exit {proc.returncode} != {expect['exit']}"
                )
            termination = (
                "trap" if expected_trap is not None else "normal"
            )
            if expect["termination"] != termination:
                problems.append("golden termination field disagrees with trap")
            if problems:
                print(f"FAIL {data.get('id')} ({path.name}): {'; '.join(problems)}")
                failures += 1
    if failures:
        print(f"golden.run: {failures} failure(s) of {checked} run golden(s)")
        return 1
    print(f"golden.run: {checked} runtime golden(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
