#!/usr/bin/env python3
"""Contract tests for the T4 diagnostic JSONL interface."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GOLDEN = json.loads((HERE / "t4_diag_golden.json").read_text(encoding="utf-8"))
FAILS = 0
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global FAILS, CHECKS
    CHECKS += 1
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def environment() -> dict[str, str]:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(ROOT / ".tmp")})
    return env


def run(tool: str, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run([tool, *args], cwd=cwd, capture_output=True,
                          timeout=30, env=environment())


def write(cwd: Path, name: str, data: str | bytes) -> Path:
    path = cwd / name
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_text(data, encoding="utf-8")
    return path


def records(proc: subprocess.CompletedProcess, label: str) -> list[dict]:
    try:
        items = [json.loads(line) for line in proc.stderr.decode("utf-8").splitlines()]
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        check(False, f"{label} stderr is JSONL: {exc}: {proc.stderr!r}")
        return []
    check(bool(items), f"{label} emits at least one JSONL record")
    return items


def error_records(items: list[dict]) -> list[dict]:
    return [item for item in items if item.get("severity") == "error"]


def text_error(proc: subprocess.CompletedProcess, label: str) -> tuple[str, int, int, str, str, str]:
    match = re.match(r"^(.*):(\d+):(\d+): error\[([A-Z]+)\]: (TC_[A-Z0-9_]+): (\S+)",
                     proc.stderr.decode("utf-8"))
    check(match is not None, f"{label} has one text diagnostic: {proc.stderr!r}")
    if match is None:
        return "", 0, 0, "", "", ""
    return match.group(1), int(match.group(2)), int(match.group(3)), \
        match.group(4), match.group(5), match.group(6)


def check_case(tool: str, cwd: Path, case: str, source: Path,
               extra: list[str]) -> None:
    expected = GOLDEN["cases"][case]
    args = [*extra, "--diag-format=json", str(source)]
    proc = run(tool, args, cwd)
    label = f"{Path(tool).name} {case}"
    check(proc.returncode == 1, f"{label} exit 1, got {proc.returncode}: {proc.stderr!r}")
    check(proc.stdout == b"", f"{label} does not contaminate stdout")
    items = records(proc, label)
    check(items == expected, f"{label} golden mismatch\nexpected={expected!r}\ngot={items!r}")
    errors = error_records(items)
    check(len(errors) == 1, f"{label} exactly one error record")
    if len(errors) == 1:
        keys = GOLDEN["trap_keys"] if case == "rt" else GOLDEN["error_keys"]
        check(list(errors[0]) == keys, f"{label} error key order {list(errors[0])!r}")
    for item in items:
        if item.get("severity") == "note":
            keys = GOLDEN["invalid_keys"] if item.get("note") == "invalid byte offset" \
                else GOLDEN["related_keys"]
            check(list(item) == keys, f"{label} note key order {list(item)!r}")
    if items:
        check(str(cwd) not in proc.stderr.decode("utf-8"),
              f"{label} has no absolute temporary path")

    text_args = [*extra, "--diag-format=text", str(source)]
    text = run(tool, text_args, cwd)
    check(text.returncode == proc.returncode, f"{label} text/json exit parity")
    check(text.stdout == proc.stdout, f"{label} text/json stdout parity")
    file, line, col, stage, code, print_name = text_error(text, f"{label} text")
    if errors:
        error = errors[0]
        check((file, line, col, stage, code, print_name) ==
              (error["file"], error["line"], error["col"], error["stage"],
               error["code"], error["print_name"]),
              f"{label} text/json diagnostic parity")
    notes = [item for item in items if item.get("severity") == "note"]
    if case == "lex_invalid":
        check(b"note: invalid byte offset 28\n" in text.stderr,
              f"{label} text invalid-byte note")
    elif case == "sem_related":
        check(b"note: related position\n" in text.stderr,
              f"{label} text related note")
    else:
        check(not notes, f"{label} has no unexpected notes")


def check_cli(tool: str, cwd: Path, source: Path) -> None:
    label = Path(tool).name
    bad = run(tool, ["--diag-format=xml", str(source)], cwd)
    check(bad.returncode == 2 and bad.stdout == b"" and
          bad.stderr.startswith(f"{label}: ".encode()),
          f"{label} invalid diag format is text usage error")
    duplicate = run(tool, ["--diag-format=json", "--diag-format=text",
                           str(source)], cwd)
    check(duplicate.returncode == 2 and duplicate.stdout == b"" and
          duplicate.stderr.startswith(f"{label}: ".encode()),
          f"{label} duplicate diag format is text usage error")
    missing = run(tool, ["--diag-format=json", str(cwd / "missing.tc")], cwd)
    check(missing.returncode == 70 and missing.stdout == b"" and
          missing.stderr.startswith(f"{label}: tc_error_io:".encode()) and
          not missing.stderr.lstrip().startswith(b"{"),
          f"{label} implementation errors stay textual in JSON mode")


def check_escaping(tool: str, cwd: Path) -> None:
    name = 'quo"te\\name.tc'
    source = write(cwd, name, b"#lib\npublic func f() void then\n    var x: int32\nend\n")
    proc = run(tool, ["--syntax-only", "--diag-format=json", str(source)], cwd)
    items = records(proc, f"{Path(tool).name} escaping")
    check(proc.returncode == 1 and len(error_records(items)) == 1,
          f"{Path(tool).name} escaped path diagnostic")
    if error_records(items):
        check(error_records(items)[0]["file"] == name,
              f"{Path(tool).name} JSON path round trip")
    check(b'\\"' in proc.stderr and b'\\\\' in proc.stderr,
          f"{Path(tool).name} escapes quote and backslash")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} <tc-vm> <tc-aot>", file=sys.stderr)
        return 2
    tools = [str(Path(argv[1]).resolve()), str(Path(argv[2]).resolve())]
    with tempfile.TemporaryDirectory(prefix="t4-diag-", dir=ROOT / ".tmp") as tmp:
        root = Path(tmp)
        cases = {
            "lex_invalid": (b"#program\n_ = writeln int32, \xff\n", ["--syntax-only"]),
            "syntax": ("#lib\npublic func f() void then\n"
                       "    var x: int32\nend\n", ["--syntax-only"]),
            "import": ("#program\nimport absent\n", ["--sem-only"]),
            "sem_related": ("#program\nimport lib\n", ["--sem-only"]),
            "ct": ("#program\nconst BAD: int32 = div int32, 1, 0\n",
                   ["--static-only"]),
            "rt": ("#program\nvar z: int32 = copy int32, 0\n"
                   "var x: int32 = div int32, 1, z\n", []),
        }
        for case, (data, extra) in cases.items():
            cwd = root / case
            cwd.mkdir()
            source = write(cwd, "main.tc", data)
            if case == "sem_related":
                write(cwd, "lib.tc", "#lib\npublic func f() int32 then\nend\n")
            for tool in tools:
                if case == "rt" and Path(tool).name != "tc-vm":
                    continue
                check_case(tool, cwd, case, source, extra)
        cwd = root / "cli"
        cwd.mkdir()
        source = write(cwd, "main.tc", cases["syntax"][0])
        for tool in tools:
            check_cli(tool, cwd, source)
            check_escaping(tool, cwd)
    print(f"{CHECKS} checks, {FAILS} failures")
    return 1 if FAILS else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
