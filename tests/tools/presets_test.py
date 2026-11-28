#!/usr/bin/env python3
"""Validate CMake presets, the unified runner, and the fixed CTest label set."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ALLOWED = {"unit", "conformance", "toolchain", "aot", "slow", "fast"}
FAST_FORBIDDEN = re.compile(
    r"(^|\.)(gdb|qemu|aot_debug|debug_smoke|codegen_invariance)|"
    r"long_branch|longterm|frontend_scale|fuzz|release\.s10_audit"
)
CHECKS = 0
FAILS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS, FAILS
    CHECKS += 1
    if not condition:
        FAILS += 1
        print(f"FAIL: {message}")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} BUILD_DIR", file=sys.stderr)
        return 2
    build_dir = Path(sys.argv[1]).resolve()
    try:
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"FAIL: cannot read CMakePresets.json: {exc}")
        return 1
    check(presets.get("version") == 3, "preset schema version is 3")
    configure = {item.get("name"): item for item in presets.get("configurePresets", [])}
    build = {item.get("name"): item for item in presets.get("buildPresets", [])}
    test = {item.get("name"): item for item in presets.get("testPresets", [])}
    for name in ("fast", "ci", "release"):
        check(name in configure, f"missing configure preset {name}")
        check(name in build, f"missing build preset {name}")
        check(name in test, f"missing test preset {name}")
        if name in configure:
            check(".tmp/" in configure[name].get("binaryDir", ""),
                  f"preset {name} builds beneath .tmp")
    check(test.get("fast", {}).get("filter", {}).get("include", {}).get("label") == "^fast$",
          "fast test preset selects the fast label")
    result = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    check(result.returncode == 0, "ctest --show-only=json-v1 succeeds")
    if result.returncode != 0:
        return 1
    report = json.loads(result.stdout.decode("utf-8"))
    tests = report.get("tests", [])
    check(bool(tests), "CTest registers tests")
    names = [item.get("name", "") for item in tests]
    check(len(names) == len(set(names)), "CTest names are unique")
    for required in ("toolchain.reduce", "toolchain.presets"):
        check(required in names, f"missing required test {required}")
    for item in tests:
        name = item.get("name", "")
        properties = {prop.get("name"): prop.get("value")
                      for prop in item.get("properties", [])}
        raw_labels = properties.get("LABELS", [])
        if isinstance(raw_labels, str):
            raw_labels = raw_labels.split(";")
        labels = set(filter(None, raw_labels))
        check(bool(labels), f"{name} has at least one label")
        check(labels <= ALLOWED, f"{name} uses only fixed labels")
        check(not ("fast" in labels and "slow" in labels),
              f"{name} fast and slow are mutually exclusive")
        if "fast" in labels:
            check("aot" not in labels, f"{name} fast does not require AOT tooling")
            check(not FAST_FORBIDDEN.search(name), f"{name} fast excludes slow/tooling cases")
    runner = subprocess.run(
        [sys.executable, str(ROOT / "tests" / "run.py"), "--list",
         "--build-dir", str(build_dir)],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    check(runner.returncode == 0, "tests/run.py --list succeeds")
    check(runner.stdout.lstrip().startswith(b"{"), "tests/run.py --list emits CTest JSON")
    print(f"toolchain.presets: {CHECKS} checks, {FAILS} failures")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
