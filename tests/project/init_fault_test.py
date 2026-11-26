#!/usr/bin/env python3
"""Fault-inject every forward init mutation and verify atomic rollback."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

FAILS = 0
REPO = Path(__file__).resolve().parents[2]


def check(condition: bool, message: str) -> None:
    global FAILS
    if not condition:
        print(f"FAIL: {message}")
        FAILS += 1


def snapshot(root: Path) -> dict[str, bytes | None]:
    result: dict[str, bytes | None] = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in dirs + files:
            path = base / name
            rel = str(path.relative_to(root))
            if path.is_symlink():
                result[rel] = None
            elif path.is_dir():
                result[rel] = None
            else:
                result[rel] = path.read_bytes()
    return result


def build_driver(directory: Path) -> Path:
    compiler = os.environ.get("CC", "clang")
    if shutil.which(compiler) is None:
        compiler = "cc"
    driver = directory / "init_fault_driver"
    command = [
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-I", str(REPO / "src"),
        str(REPO / "tests/project/init_fault_driver.c"),
        str(REPO / "src/project/init.c"),
        str(REPO / "src/project/name.c"),
        str(REPO / "src/driver/cli.c"),
        str(REPO / "src/contract/optimizer.c"),
        "-o", str(driver),
    ]
    result = subprocess.run(command, capture_output=True, timeout=60)
    if result.returncode != 0:
        print(result.stderr.decode(errors="replace"), file=sys.stderr)
        raise RuntimeError("cannot compile init fault driver")
    return driver


def run(driver: Path, root: Path, fail_at: int, trace: Path,
        extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    env = {"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "PATH": "/usr/bin:/bin",
           "TC_INIT_TRACE": str(trace)}
    if extra_env:
        env.update(extra_env)
    return subprocess.run([str(driver), "demo", str(fail_at)], cwd=root,
                          capture_output=True, timeout=20, env=env)


def trace_operations(trace: Path) -> list[str]:
    if not trace.exists():
        return []
    return [line.split(" ", 1)[1] for line in trace.read_text().splitlines()
            if " " in line]


def exercise(driver: Path, base: Path, label: str,
             prepare) -> list[str]:
    root = base / f"{label}-probe"
    root.mkdir()
    prepare(root)
    before = snapshot(root)
    trace = base / f"{label}-probe.trace"
    result = run(driver, root, 0, trace)
    check(result.returncode == 0, f"{label} unfaulted probe exit 0")
    check(result.stderr == b"", f"{label} unfaulted probe stderr empty")
    operations = trace_operations(trace)
    counts = Counter(operations)
    check(counts["mkdir"] >= 1 and counts["create"] >= 1 and
          counts["write"] >= 1 and counts["close"] >= 1,
          f"{label} seam exercises mkdir/create/write/close")
    for index, operation in enumerate(operations, 1):
        case = base / f"{label}-{index}"
        case.mkdir()
        prepare(case)
        original = snapshot(case)
        case_trace = base / f"{label}-{index}.trace"
        fault = run(driver, case, index, case_trace)
        check(fault.returncode == 70,
              f"{label} failure {index} ({operation}) exit 70, got {fault.returncode}")
        check(fault.stdout == b"", f"{label} failure {index} ({operation}) stdout empty")
        check(snapshot(case) == original,
              f"{label} failure {index} ({operation}) leaves original tree unchanged")
        check(fault.stderr.startswith(b"tc: tc_error_io: "),
              f"{label} failure {index} ({operation}) reports initial IO error")
        check(b"cleanup failed:" not in fault.stderr,
              f"{label} failure {index} ({operation}) has no unexpected cleanup residue")
    return operations


def prepare_empty(root: Path) -> None:
    return None


def prepare_existing(root: Path) -> None:
    (root / "src").mkdir()
    (root / "tests").mkdir()
    (root / "src/main.tc").write_bytes(b"existing source")
    (root / ".gitignore").write_bytes(b"existing ignore\n")
    (root / ".gitattributes").write_bytes(b"existing attributes\n")


def check_cleanup_failures(driver: Path, base: Path) -> None:
    root = base / "cleanup-probe"
    root.mkdir()
    trace = base / "cleanup-probe.trace"
    result = run(driver, root, 8, trace)
    cleanup = [op for op in trace_operations(trace) if op in ("unlink", "rmdir")]
    check(result.returncode == 70 and result.stdout == b"" and snapshot(root) == {},
          "forward failure rolls back the complete empty tree")
    check(len(cleanup) == 4, "rollback exercises file unlink and three rmdir calls")
    for index, operation in enumerate(cleanup, 1):
        case = base / f"cleanup-{index}"
        case.mkdir()
        case_trace = base / f"cleanup-{index}.trace"
        fault = run(driver, case, 8, case_trace,
                    {"TC_INIT_FAIL_CLEANUP_AT": str(index)})
        check(fault.returncode == 70 and fault.stdout == b"",
              f"cleanup failure {index} ({operation}) exits 70 with empty stdout")
        check(b"cleanup failed:" in fault.stderr,
              f"cleanup failure {index} ({operation}) reports residue")
        check(snapshot(case) != {},
              f"cleanup failure {index} ({operation}) preserves unremoved entry")


def check_oom_report(driver: Path, base: Path) -> None:
    root = base / "oom"
    root.mkdir()
    (root / "src").mkdir()
    trace = base / "oom.trace"
    result = run(driver, root, 0, trace)
    operations = trace_operations(trace)
    check(result.returncode == 0, "OOM probe exits 0")
    check("open_dir" in operations, "OOM probe exercises directory open")
    if "open_dir" not in operations:
        return
    root = base / "oom-failure"
    root.mkdir()
    (root / "src").mkdir()
    original = snapshot(root)
    result = run(driver, root, operations.index("open_dir") + 1,
                 base / "oom-failure.trace", {"TC_INIT_FAIL_ENOMEM": "1"})
    check(result.returncode == 70 and result.stdout == b"",
          "init OOM exits 70 with empty stdout")
    check(result.stderr ==
          b"tc: TC_ERR_OUT_OF_MEMORY: OutOfMemory: memory allocation failed\n",
          "init OOM prints the exact unified diagnostic once")
    check(snapshot(root) == original, "init OOM leaves the original tree unchanged")


def check_identity_recheck(driver: Path, base: Path) -> None:
    root = base / "replaced"
    root.mkdir()
    trace = base / "replaced.trace"
    result = run(driver, root, 8, trace, {"TC_INIT_REPLACE_FIRST": "1"})
    check(result.returncode == 70 and result.stdout == b"",
          "replaced journal entry exits 70 with empty stdout")
    check(snapshot(root) == {"tc.project": b"replacement"},
          "rollback preserves replacement and removes every other created entry")
    check(result.stderr.count(b"cleanup failed: tc.project\n") == 1,
          "identity mismatch reports tc.project exactly once")
    check(result.stderr.startswith(b"tc: tc_error_io: ") and
          result.stderr.endswith(b"cleanup failed: tc.project\n"),
          "identity mismatch stderr contains only initial and cleanup errors")


def main() -> int:
    if len(sys.argv) > 2:
        print("usage: init_fault_test.py [init-fault-driver]", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="tc-init-fault-") as tmp:
        base = Path(tmp)
        driver = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else build_driver(base)
        operations = exercise(driver, base, "empty", prepare_empty)
        exercise(driver, base, "existing", prepare_existing)
        check_cleanup_failures(driver, base)
        check_identity_recheck(driver, base)
        check_oom_report(driver, base)
        print(f"injected {len(operations)} empty-tree and "
              f"{len(trace_operations(base / 'existing-probe.trace'))} existing-tree failure points")
    if FAILS:
        print(f"{FAILS} init fault check(s) failed")
        return 1
    print("init fault checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
