#!/usr/bin/env python3
"""Contract checks for the deterministic tc-reduce front end."""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

CHECKS = 0
FAILS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS, FAILS
    CHECKS += 1
    if not condition:
        FAILS += 1
        print(f"FAIL: {message}")


def run(reducer: str, args: list[str], cwd: Path, **kwargs: object) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    return subprocess.run([reducer, *args], cwd=cwd, env=env, capture_output=True,
                          timeout=30, check=False, **kwargs)


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} TC_REDUCE TC_VM TC_AOT", file=sys.stderr)
        return 2
    reducer, vm, aot = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="tc-reduce-test-") as temporary:
        root = Path(temporary)
        write(root / "unused.tc", b"#lib\n")
        seed = root / "seed.tc"
        original = (b"#program\n"
                    b"import unused\n"
                    b"const BAD: int32 = div int32, 1, 0\n"
                    b"_ = writeln int32, 12345\n")
        write(seed, original)
        result = run(reducer, [str(seed), "--", vm, "--static-only", "{}"], root)
        check(result.returncode == 0, f"basic reduction exits 0, got {result.returncode}")
        check(result.stdout == b"#program\nconst BAD: int32 = div int32, 1, 0\n",
              "basic reduction removes unrelated lines deterministically")
        check(seed.read_bytes() == original, "default reduction leaves source unchanged")
        check(not list(root.glob("tc_reduce_*.tc")), "candidate files are cleaned up")

        second = run(reducer, [str(seed), "--", vm, "--static-only"], root)
        check(second.returncode == 0 and second.stdout == result.stdout,
              "missing placeholder appends the candidate and is deterministic")

        output = root / "reduced.tc"
        result = run(reducer, [str(seed), f"--output={output}", "--", vm,
                               "--static-only", "{}"], root)
        check(result.returncode == 0, "explicit output reduction exits 0")
        check(result.stdout == b"", "explicit output suppresses default stdout")
        check(output.read_bytes() == b"#program\nconst BAD: int32 = div int32, 1, 0\n",
              "explicit output receives the reduced source")
        check(not list(root.glob(".tc-reduce-result-*.tmp")),
              "result temporary files are cleaned up")

        clean = root / "clean.tc"
        write(clean, b"#program\n_ = writeln int32, 1\n")
        result = run(reducer, [str(clean), "--", vm], root)
        check(result.returncode == 2 and result.stdout == b"",
              "clean seed has no fingerprint and exits 2 without output")

        result = run(reducer, [str(seed), "--", vm, "--static-only",
                               "--diag-format=text", "{}"], root)
        check(result.returncode == 2 and result.stdout == b"",
              "text diagnostics are rejected")

        result = run(reducer, [str(seed), "--", "/bin/true", "{}"], root)
        check(result.returncode == 2 and result.stdout == b"",
              "non-built-in tools are rejected")

        wrapper_dir = root / "wrappers"
        wrapper = wrapper_dir / "tc-vm"
        write(wrapper, b"#!/bin/sh\nexec " + vm.encode() + b" \"$@\"\n")
        wrapper.chmod(0o755)
        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = str(wrapper_dir) + os.pathsep + old_path
        try:
            result = run(reducer, [str(seed), "--", "tc-vm", "--static-only", "{}"], root)
        finally:
            os.environ["PATH"] = old_path
        check(result.returncode == 2, "same-name PATH wrappers are not authenticated")

        with seed.open("r+b") as shared:
            result = subprocess.run([reducer, str(seed), "--", vm, "--static-only", "{}"],
                                    cwd=root, stdout=shared, stderr=subprocess.PIPE,
                                    timeout=30, check=False)
        check(result.returncode == 2, "stdout aliasing source exits 2")
        check(seed.read_bytes() == original, "stdout alias rejection does not write")

        with seed.open("r+b") as shared:
            result = subprocess.run([reducer, str(seed), "--", vm, "--static-only", "{}"],
                                    cwd=root, stdout=subprocess.PIPE, stderr=shared,
                                    timeout=30, check=False)
        check(result.returncode == 2, "stderr aliasing source exits 2")
        check(result.stderr is None, "stderr alias check is silent")
        check(seed.read_bytes() == original, "stderr alias rejection does not write")

        imported = root / "imported.tc"
        write(imported, b"#lib\npublic func f() int32 then\n    1\nend\n")
        import_seed = root / "import_seed.tc"
        write(import_seed, b"#program\nimport imported\nconst BAD: int32 = div int32, 1, 0\n")
        result = run(reducer, [str(import_seed), f"--output={imported}", "--", vm,
                               "--static-only", "{}"], root)
        check(result.returncode == 2, "result aliasing an import exits 2")
        check(imported.read_bytes().startswith(b"#lib"), "import alias rejection preserves import")

        inplace = root / "inplace.tc"
        write(inplace, original)
        result = run(reducer, [str(inplace), f"--output={inplace}", "--", vm,
                               "--static-only", "{}"], root)
        check(result.returncode == 0, "explicit in-place output is allowed")
        check(inplace.read_bytes() == b"#program\nconst BAD: int32 = div int32, 1, 0\n",
              "explicit in-place output replaces the source atomically")

        collision = root / "collision"
        write(collision / "tc_reduce_1.tc", b"#lib\n")
        write(collision / "lib.tc", b"#lib\n")
        collision_seed = collision / "seed.tc"
        write(collision_seed, b"#program\nimport tc_reduce_1\nconst BAD: int32 = div int32, 1, 0\n")
        result = run(reducer, [str(collision_seed), "--", vm, "--static-only", "{}"], root)
        check(result.returncode == 0, "candidate names skip existing and imported names")
        check((collision / "tc_reduce_1.tc").read_bytes() == b"#lib\n",
              "pre-existing candidate-name entry is preserved")
        (collision / "tc_reduce_1.tc").unlink()

        journal = root / "journal"
        journal.mkdir()
        result = run(reducer, [str(clean), "--timeout=1", "--", vm, "--gdb",
                               f"--gdb-stdout-journal={journal}", "{}"], root)
        check(result.returncode == 1, "seed timeout exits 1")
        check(result.stdout == clean.read_bytes(), "seed timeout delivers the original input")

        aot_seed = root / "aot.tc"
        write(aot_seed, b"#program\nconst BAD: int32 = div int32, 1, 0\n")
        result = run(reducer, [str(aot_seed), "--", aot, "--static-only", "{}"], root)
        check(result.returncode == 0 and result.stdout == aot_seed.read_bytes(),
              "tc-aot is authenticated and accepted")

        mode = os.stat(output).st_mode & 0o777
        check(mode == 0o600, "result files are created with private permissions")

        for options in (["--timeout=1", "--timeout=1"],
                        ["--timeout=-1"],
                        ["--timeout=18446744073709551616"],
                        ["--stdin="],
                        ["--output="],
                        ["extra"]):
            result = run(reducer, [str(seed), *options, "--", vm, "--static-only", "{}"], root)
            check(result.returncode == 2, f"invalid reducer options {options!r} exit 2")
        result = run(reducer, [str(seed), "{}", "--", vm, "--static-only"], root)
        check(result.returncode == 2, "placeholder before separator exits 2")

        result = run(reducer, [str(seed), f"--output={output}", "--", vm,
                               "--static-only", "--stats-out={}".format(output), "{}"], root)
        check(result.returncode == 2, "child output aliasing result exits 2")
        result = run(reducer, [str(seed), "--", vm, "--static-only",
                               "--dump-ir={}".format(seed), "{}"], root)
        check(result.returncode == 2, "child output aliasing source exits 2")

        result = run(reducer, [str(seed), "--output=/dev/stdout", "--", vm,
                               "--static-only", "{}"], root)
        check(result.returncode == 0 and result.stdout.startswith(b"#program\n"),
              "output resolving to fd1 writes the default result channel")

        result = subprocess.run([reducer, str(seed), "--", vm, "--static-only", "{}"],
                                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=False)
        check(result.returncode == 2, "fd1/fd2 identity collision exits 2")

        fifo = root / "result-fifo"
        os.mkfifo(fifo)
        result = run(reducer, [str(seed), f"--output={fifo}", "--", vm,
                               "--static-only", "{}"], root)
        check(result.returncode == 2, "FIFO result is rejected without blocking")

        result = run(reducer, [str(clean), "--", aot, "--profile=w32", "{}"], root)
        check(result.returncode == 70 and result.stdout == b"",
              "seed tool 70 propagates without publishing a result")

        trap = root / "trap.tc"
        write(trap, b"#program\nvar zero: int32 = copy int32, 0\n"
                    b"var result: int32 = div int32, 10, zero\n"
                    b"_ = writeln int32, result\n")
        result = run(reducer, [str(trap), "--", vm, "{}"], root)
        check(result.returncode == 0, "runtime trap seed reduces successfully")
        reduced_trap = root / "reduced-trap.tc"
        write(reduced_trap, result.stdout)
        verified = subprocess.run([vm, "--diag-format=json", str(reduced_trap)],
                                  cwd=root, capture_output=True, timeout=20, check=False)
        check(verified.returncode == 1 and b'"code":"TC_RE_DIV_ZERO"' in verified.stderr,
              "reduced runtime trap preserves the structured fingerprint")

        crlf = root / "crlf.tc"
        write(crlf, b"#program\r\n\r\nconst BAD: int32 = div int32, 1, 0\r\n")
        result = run(reducer, [str(crlf), "--", vm, "--static-only", "{}"], root)
        check(result.returncode == 0 and b"\n" not in result.stdout.replace(b"\r\n", b""),
              "reduction preserves CRLF logical line endings")

        stdin_path = root / "stdin.bin"
        write(stdin_path, b"0\n")
        stdin_seed = root / "stdin.tc"
        write(stdin_seed, b"#program\nvar zero: int32 = read int32\n"
                          b"var result: int32 = div int32, 10, zero\n"
                          b"_ = writeln int32, result\n")
        result = run(reducer, [str(stdin_seed), f"--stdin={stdin_path}", "--", vm, "{}"], root)
        check(result.returncode == 0 and b"read int32" in result.stdout,
              "stdin snapshot is accepted and preserved in the reduced seed")
        stdin_fifo = root / "stdin-fifo"
        os.mkfifo(stdin_fifo)
        result = run(reducer, [str(stdin_seed), f"--stdin={stdin_fifo}", "--", vm, "{}"], root)
        check(result.returncode == 70, "non-regular stdin exits 70 without blocking")

        read_fd, write_fd = os.pipe()
        os.close(read_fd)
        try:
            result = subprocess.run([reducer, str(seed), "--", vm, "--static-only", "{}"],
                                    cwd=root, stdout=write_fd, stderr=subprocess.PIPE,
                                    timeout=30, check=False, pass_fds=(write_fd,))
        finally:
            os.close(write_fd)
        check(result.returncode == 70, "stdout EPIPE exits 70 instead of SIGPIPE")
        check(not list(root.rglob("tc_reduce_*.tc")), "all candidate files are cleaned up")
    print(f"toolchain.reduce: {CHECKS} checks, {FAILS} failures")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
