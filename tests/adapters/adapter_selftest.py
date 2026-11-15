#!/usr/bin/env python3
"""Fail-closed self-test for the S1 harness adapter.

Uses fake tc-vm scripts to verify that the adapter accepts exactly one clean
LEX/SYN diagnostic and rejects junk stderr, a diagnostic for another file,
stdout output in `--syntax-only` mode, and junk on the property path.  This is
the regression test required by G1 audit finding A5/B3.

Usage:
    python3 tests/adapters/adapter_selftest.py <configured adapter path>
"""

from __future__ import annotations

import importlib.util
import os
import stat
import sys
import tempfile
from pathlib import Path

FAILS = 0


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def write_tool(path: Path, *, diag: bool, junk: bool = False,
               stdout_junk: bool = False, other_file: bool = False,
               exit_code: int = 1) -> None:
    lines = ["#!/bin/sh"]
    if stdout_junk:
        lines.append("printf 'STDOUT JUNK\\n'")
    if diag:
        if other_file:
            lines.append(
                "printf 'other.tc:1:1: error[LEX]: TC_CE_SYNTAX: SyntaxError\\n' >&2"
            )
        else:
            lines.append(
                "printf '%s:1:1: error[LEX]: TC_CE_SYNTAX: SyntaxError\\n' \"$2\" >&2"
            )
    if junk:
        lines.append("printf 'UNEXPECTED TOOL JUNK\\n' >&2")
    lines.append(f"exit {exit_code}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def load_adapter(path: Path):
    spec = importlib.util.spec_from_file_location("tc_adapter_selftest", path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot load adapter {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: adapter_selftest.py <configured adapter path>",
              file=sys.stderr)
        return 2
    adapter_path = Path(sys.argv[1]).resolve()
    module = load_adapter(adapter_path)
    adapter = module.ADAPTER
    original_vm = module.TC_VM

    with tempfile.TemporaryDirectory(prefix="tc-adapter-selftest-") as tmp:
        work = Path(tmp)
        source_path = work / "main.tc"
        source_path.write_text("#program\n", encoding="utf-8")
        property_case = {
            "id": "TC-ADAPTER-SELFTEST-0001",
            "kind": "property",
            "profile": "common",
            "property": {"name": "lex_random", "trials": 1, "seed": 1},
            "_harness": {"workdir": str(work)},
        }
        compile_case = {
            "id": "TC-ADAPTER-SELFTEST-0002",
            "kind": "compile",
            "profile": "common",
            "source": "main.tc",
            "_harness": {
                "workdir": str(work),
                "source_path": str(source_path),
            },
        }

        good = work / "fake-good"
        junk = work / "fake-junk"
        zero_junk = work / "fake-zero-junk"
        other = work / "fake-other"
        compile_junk = work / "fake-compile-junk"
        stdout_junk = work / "fake-stdout-junk"
        write_tool(good, diag=True)
        write_tool(junk, diag=True, junk=True)
        write_tool(zero_junk, diag=False, junk=True, exit_code=0)
        write_tool(other, diag=True, other_file=True)
        write_tool(compile_junk, diag=True, junk=True)
        write_tool(stdout_junk, diag=True, stdout_junk=True)

        module.TC_VM = str(good)
        result = adapter.property(dict(property_case))
        check(result == [], "clean property diagnostic must pass")

        module.TC_VM = str(junk)
        result = adapter.property(dict(property_case))
        check(isinstance(result, list) and len(result) > 0,
              "property diagnostic + junk must fail")

        module.TC_VM = str(zero_junk)
        result = adapter.property(dict(property_case))
        check(isinstance(result, list) and len(result) > 0,
              "property exit 0 with stderr junk must fail")

        module.TC_VM = str(good)
        result = adapter.compile(dict(compile_case))
        diagnostics = result.get("diagnostics") if isinstance(result, dict) else None
        check(isinstance(diagnostics, list) and len(diagnostics) == 1,
              "clean compile diagnostic must parse")
        if isinstance(diagnostics, list) and len(diagnostics) == 1:
            check(diagnostics[0]["stage"] == "LEX", "compile stage")
            check(diagnostics[0]["code"] == "TC_CE_SYNTAX", "compile code")
            check(diagnostics[0]["primary"]["file"] == "main.tc",
                  "compile primary file maps to metadata source")

        module.TC_VM = str(other)
        try:
            adapter.compile(dict(compile_case))
        except RuntimeError:
            pass
        else:
            check(False, "compile diagnostic for another file must raise")

        module.TC_VM = str(compile_junk)
        try:
            adapter.compile(dict(compile_case))
        except RuntimeError:
            pass
        else:
            check(False, "compile diagnostic + junk must raise")

        module.TC_VM = str(stdout_junk)
        try:
            adapter.compile(dict(compile_case))
        except RuntimeError:
            pass
        else:
            check(False, "compile stdout output must raise")

    module.TC_VM = original_vm
    if FAILS:
        print(f"adapter selftest: {FAILS} failure(s)")
        return 1
    print("adapter selftest: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
