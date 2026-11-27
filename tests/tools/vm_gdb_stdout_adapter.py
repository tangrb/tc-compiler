#!/usr/bin/env python3
"""Read the committed logical stdout stream of a VM GDB journal."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tempfile

NAME = re.compile(r"record-([0-9]{20})\.bin\Z")
HIGH = re.compile(r"[0-9]{20}\Z")


def read_stream(directory: Path) -> bytes:
    high_path = directory / ".tc-gdb-stdout.next"
    try:
        text = high_path.read_text(encoding="ascii")
    except (OSError, UnicodeError) as error:
        raise ValueError(f"invalid journal high-water mark: {error}") from error
    if HIGH.fullmatch(text) is None:
        raise ValueError("invalid journal high-water mark")
    high = int(text, 10)
    if high > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("journal high-water mark overflow")
    output = []
    for path in directory.iterdir():
        match = NAME.fullmatch(path.name)
        if match is None or int(match.group(1)) > high:
            continue
        number = int(match.group(1))
        if number == 0 or not path.is_file() or path.is_symlink():
            raise ValueError(f"invalid committed journal record: {path.name}")
        output.append((number, path.read_bytes()))
    return b"".join(data for _, data in sorted(output))


def selftest() -> None:
    root = Path(__file__).resolve().parents[2] / ".tmp"
    root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vm-gdb-adapter-", dir=root) as tmp:
        directory = Path(tmp)
        high = directory / ".tc-gdb-stdout.next"
        high.write_text("00000000000000000003", encoding="ascii")
        (directory / "record-00000000000000000003.bin").write_bytes(b"third")
        (directory / "record-00000000000000000001.bin").write_bytes(b"first")
        (directory / "record-00000000000000000009.bin").write_bytes(b"future")
        (directory / "record-00000000000000000002.tmp").write_bytes(b"temporary")
        if read_stream(directory) != b"firstthird":
            raise AssertionError("journal ordering or gap handling failed")
        high.write_text("18446744073709551616", encoding="ascii")
        try:
            read_stream(directory)
        except ValueError:
            pass
        else:
            raise AssertionError("overflowing high-water mark accepted")
        high.write_text("00000000000000000001", encoding="ascii")
        (directory / "record-00000000000000000001.bin").unlink()
        (directory / "record-00000000000000000001.bin").symlink_to("target")
        try:
            read_stream(directory)
        except ValueError:
            pass
        else:
            raise AssertionError("symlink record accepted")
    print("vm_gdb_stdout_adapter selftest passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("journal", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.journal is None:
        parser.error("journal is required unless --selftest is used")
    try:
        data = read_stream(args.journal)
        if args.output is None:
            sys.stdout.buffer.write(data)
        else:
            args.output.write_bytes(data)
    except (OSError, ValueError) as error:
        print(f"vm-gdb-stdout: {error}", file=sys.stderr)
        return 70
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
