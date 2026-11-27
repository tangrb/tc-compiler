#!/usr/bin/env python3
"""T2 public linemap, debug CLI and deterministic artifact checks."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TMP_ROOT = ROOT / ".tmp"
SOURCE = """#program
import lib
var a: int32 = copy int32, 7
var b: int32 = add int32, a, 5
var c: int32 = call lib.f, b
_ = writeln int32, c
"""
LIBRARY = """#lib
public func f(x: int32) int32 then
var y: int32 = add int32, x, 1
_ = return y
end
"""
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def project(base: Path, name: str) -> Path:
    root = base / name / "project"
    write(root / "tc.project",
          'tc-project 1\nname = "t2-debug"\ninclude = "src"\n'
          'include = "../external"\n')
    write(root / "src/main.tc", SOURCE)
    write(base / name / "external/lib.tc", LIBRARY)
    return root


def environment(tmp: Path) -> dict[str, str]:
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                "TMPDIR": str(tmp)})
    return env


def run(command: list[str], cwd: Path, *, expected: int = 0,
        env: dict[str, str] | None = None, limit: int | None = None,
        shell: bool = False) -> subprocess.CompletedProcess:
    def set_limits() -> None:
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

    result = subprocess.run(
        command, cwd=cwd, env=env or environment(cwd), capture_output=True,
        timeout=120, shell=shell,
        preexec_fn=set_limits if limit is not None else None,
    )
    check(result.returncode == expected,
          f"{command!r} exited {result.returncode}, expected {expected}: "
          f"stdout={result.stdout!r} stderr={result.stderr!r}")
    return result


def load_map(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    check(list(data) == ["format", "version", "profile", "source", "entries"],
          "linemap top-level key order")
    check(data["format"] == "tc-linemap" and data["version"] == 3,
          "linemap schema identity")
    return data


def check_entries(data: dict, *, vm: bool, base: int = 0) -> None:
    for index, entry in enumerate(data["entries"]):
        keys = ["kind", "module", "func", "bb", "insn", "file", "line", "col"]
        if vm:
            keys.append("vm_addr")
        if entry.get("external") is True:
            keys.append("external")
        check(list(entry) == keys, f"linemap entry {index} key order")
        check(entry["file"] == entry["file"].lstrip("/") and
              not entry["file"].startswith(("./", "../")) or
              entry.get("external") is True,
              f"linemap entry {index} display path")
        if vm:
            check(entry["vm_addr"] == base + 4 * index,
                  f"linemap entry {index} synthetic address")


def ir_spans(path: Path) -> list[tuple[int, int]]:
    return [(int(line), int(column)) for line, column in
            re.findall(r"@(\d+):(\d+)", path.read_text(encoding="utf-8"))]


def check_static(vm: str, aot: str, root: Path) -> None:
    source = root / "src/main.tc"
    vm_ir = root / "vm.ir"
    vm_map = root / "vm.json"
    aot_ir = root / "aot.ir"
    aot_map = root / "aot.json"
    run([vm, "--static-only", f"--dump-ir={vm_ir}",
         f"--dump-linemap={vm_map}", str(source)], root)
    run([aot, "--static-only", f"--dump-ir={aot_ir}",
         f"--dump-linemap={aot_map}", str(source)], root)
    vm_data = load_map(vm_map)
    aot_data = load_map(aot_map)
    check(vm_data["source"] == "src/main.tc" and
          aot_data["source"] == "src/main.tc", "root-relative source display")
    check(vm_data["profile"] == aot_data["profile"] == "aarch64-linux-user",
          "linemap profile identity")
    spans = ir_spans(vm_ir)
    check(spans == ir_spans(aot_ir), "VM/AOT finalized IR spans agree")
    check(len(vm_data["entries"]) == len(spans) and
          [(entry["line"], entry["col"]) for entry in vm_data["entries"]] == spans,
          "every finalized IR instruction has one linemap entry")
    check_entries(vm_data, vm=True, base=0x544300000000)
    check_entries(aot_data, vm=False)
    stripped = [{key: value for key, value in entry.items()
                 if key != "vm_addr"} for entry in vm_data["entries"]]
    check(stripped == aot_data["entries"], "VM/AOT linemap identity agrees")
    check(any(entry.get("external") is True for entry in vm_data["entries"]),
          "external import is marked")
    check(str(root.resolve()).encode() not in vm_map.read_bytes() and
          str(root.resolve()).encode() not in aot_map.read_bytes(),
          "absolute project root leaked into linemap")

    w32_map = root / "w32.json"
    run([vm, "--profile=w32", "--static-only", f"--dump-linemap={w32_map}",
         str(source)], root)
    check_entries(load_map(w32_map), vm=True, base=0x54430000)

    trace = root / "trace.jsonl"
    proc = run([vm, f"--dump-linemap={root / 'normal.json'}",
                "--trace=ir,call,mem,io,trap", f"--trace-out={trace}",
                "--trace-format=jsonl", str(source)], root)
    check(proc.stdout == b"13\n" and proc.stderr == b"",
          "normal VM linemap preserves program streams")
    normal = load_map(root / "normal.json")
    records = [json.loads(line) for line in
               trace.read_text(encoding="utf-8").splitlines()]
    data = [item for item in records if "seq" in item]
    by_identity = {(entry["module"], entry["func"], entry["bb"],
                    entry["insn"]): entry for entry in normal["entries"]}
    for record in data:
        key = (record["module"], record["func"], record["bb"], record["insn"])
        check(key in by_identity, f"trace record maps to linemap: {record!r}")
        entry = by_identity[key]
        check(record["span"] ==
              f"{entry['file']}:{entry['line']}:{entry['col']}",
              f"trace span maps to linemap: {record!r}")
    check(any(sum((item["module"], item["func"], item["bb"], item["insn"]) ==
                  (entry["module"], entry["func"], entry["bb"], entry["insn"])
                  for item in data) > 1 for entry in normal["entries"]),
          "trace-to-linemap many-to-one case exercised")


def check_cli(vm: str, aot: str, root: Path) -> None:
    source = root / "src/main.tc"
    for tool in (vm, aot):
        name = Path(tool).name
        for args in (
            ["--dump-linemap", f"--dump-linemap={root / 'repeat.json'}"],
            [f"--dump-linemap={root / 'repeat.json'}", "--dump-linemap"],
            ["--dump-linemap="],
            ["--syntax-only", "--dump-linemap"],
            ["--sem-only", "--dump-linemap"],
        ):
            proc = run([tool, *args, str(source)], root, expected=2)
            check(proc.stdout == b"" and proc.stderr != b"",
                  f"{name} invalid linemap CLI diagnostics")
        run([tool, "--static-only", "--dump-linemap", str(source)], root)
    run([vm, "--debug", str(source)], root, expected=2)
    run([aot, "--static-only", "--debug", str(source)], root, expected=2)
    run([aot, "--syntax-only", "--debug", str(source)], root, expected=2)
    run([aot, "--sem-only", "--debug", str(source)], root, expected=2)

    proc = run([vm, "--static-only", "--dump-ir", "--dump-linemap",
                str(source)], root)
    check(proc.stdout == b"" and proc.stderr.startswith(b"tc-ir "),
          "default IR dump precedes default linemap")
    lines = proc.stderr.decode("utf-8").splitlines()
    check(lines[-1].startswith('{"format":"tc-linemap"'),
          "default linemap is compact JSON on stderr")

    trap = write(root / "trap.tc", "#program\nunsafe then\n_ = trap\nend\n")
    proc = run([vm, "--dump-ir", "--dump-linemap", "--trace=trap", "--stats",
                str(trap)], root, expected=1)
    text = proc.stderr.decode("utf-8")
    markers = ["tc-ir ", '{"format":"tc-linemap"', "# tc-trace 3",
               "# tc-trace end", "error[RT]", "# tc-stats 1"]
    positions = [text.index(marker) for marker in markers]
    check(positions == sorted(positions),
          "shared stderr order is IR, linemap, trace, trap, stats")

    stats_map = root / "aot-stats.json"
    proc = run([aot, "--debug", "--emit=asm", f"--output={root / 'debug.s'}",
                f"--dump-linemap={stats_map}", "--stats", str(source)], root)
    check(proc.stdout == b"" and proc.stderr.startswith(b"# tc-stats"),
          "AOT stats remain on stderr")
    check(stats_map.exists(), "AOT debug and linemap combine through public CLI")


def check_output_safety(vm: str, aot: str, root: Path) -> None:
    source = root / "src/main.tc"
    manifest = root / "tc.project"
    imported = root.parent / "external/lib.tc"
    before = {path: path.read_bytes() for path in (source, manifest, imported)}
    directory = root / "directory"
    directory.mkdir()
    fifo = root / "fifo"
    os.mkfifo(fifo)
    for tool in (vm, aot):
        for target in (directory, fifo):
            run([tool, "--static-only", f"--dump-linemap={target}",
                 str(source)], root, expected=2)
    for index, original in enumerate((source, manifest, imported)):
        symlink = root / f"alias-{index}.json"
        symlink.symlink_to(original)
        run([vm, "--static-only", f"--dump-linemap={symlink}",
             str(source)], root, expected=2)
        symlink.unlink()
        hardlink = root / f"hardlink-{index}.json"
        os.link(original, hardlink)
        run([aot, "--static-only", f"--dump-linemap={hardlink}",
             str(source)], root, expected=2)
        hardlink.unlink()
    check(all(path.read_bytes() == data for path, data in before.items()),
          "input identities survive rejected linemap outputs")

    duplicate = root / "duplicate.out"
    duplicate.write_text("keep", encoding="utf-8")
    run([vm, "--static-only", f"--dump-ir={duplicate}",
         f"--dump-linemap={duplicate}", str(source)], root, expected=2)
    check(duplicate.read_text(encoding="utf-8") == "keep",
          "duplicate observation output is not truncated")
    run([aot, "--static-only", "--stats", f"--stats-out={duplicate}",
         f"--dump-linemap={duplicate}", str(source)], root, expected=2)
    run([vm, "--trace=ir", f"--trace-out={duplicate}",
         f"--dump-linemap={duplicate}", str(source)], root, expected=2)
    run([aot, "--debug", f"--output={duplicate}",
         f"--dump-linemap={duplicate}", str(source)], root, expected=2)

    limited = root / "limited.json"
    run([vm, "--static-only", f"--dump-linemap={limited}", str(source)],
        root, expected=70, limit=1)
    check(not limited.exists(), "failed linemap output is discarded")
    command = 'exec "$@" 2>&-'
    proc = run(["/bin/sh", "-c", command, "sh", vm, "--static-only",
                "--dump-linemap", str(source)], root, expected=70, shell=False)
    check(proc.stdout == b"", "closed default linemap stream keeps stdout clean")


def check_public_debug(aot: str, dwarfdump: str, root: Path) -> None:
    source = root / "src/main.tc"
    linemap = root / "public.json"
    assembly = root / "public.s"
    run([aot, "--debug", f"--dump-linemap={linemap}",
         f"--output={assembly}", str(source)], root)
    data = load_map(linemap)
    text = assembly.read_text(encoding="utf-8")
    check(text.count("  .loc ") == len(data["entries"]),
          "public --debug emits one location per IR instruction")
    check(text.count("  # source: ") == len(data["entries"]),
          "public --debug emits one source comment per IR instruction")
    obj = root / "public.o"
    run([aot, "--debug", "--emit=obj", f"--output={obj}", str(source)], root)
    if dwarfdump:
        run([dwarfdump, "--verify", str(obj)], root)
        info = run([dwarfdump, "--debug-info", str(obj)], root)
        check(b"version = 0x0004" in info.stdout and
              b"DW_TAG_compile_unit" in info.stdout,
              "public --debug emits DWARF v4 CU")


def collect_cross_root(aot: str, root: Path) -> dict[str, tuple[bytes, bytes]]:
    source = root / "src/main.tc"
    temporary = root / "tmp"
    temporary.mkdir()
    env = environment(temporary)
    artifacts: dict[str, tuple[bytes, bytes]] = {}
    for emit in ("asm", "obj", "elf", "flat"):
        for path in temporary.iterdir():
            path.unlink()
        output = root / f"cross.{emit}"
        run([aot, "--debug", "--profile=aarch64-baremetal",
             f"--emit={emit}", f"--output={output}", "--keep-temps",
             str(source)], root, env=env)
        final = output.read_bytes()
        check(str(root.resolve()).encode() not in final,
              f"cross-root {emit} leaks absolute root")
        if emit == "asm":
            assembly = final
        else:
            candidates = [path for path in temporary.glob("tc-aot-*.s")
                          if b".loc " in path.read_bytes()]
            check(len(candidates) == 1,
                  f"cross-root {emit} intermediate assembly count")
            assembly = candidates[0].read_bytes()
        if emit == "flat":
            check(b".debug_info" not in final and b".debug_line" not in final,
                  "flat image has no debug sections")
        artifacts[emit] = (final, assembly)
    return artifacts


def check_cross_root(aot: str, base: Path) -> None:
    first_root = project(base, "first")
    second_root = project(base, "second")
    first = collect_cross_root(aot, first_root)
    second = collect_cross_root(aot, second_root)
    for emit in first:
        check(first[emit] == second[emit],
              f"cross-root {emit} artifact or assembly differs")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("vm")
    parser.add_argument("aot")
    parser.add_argument("dwarfdump", nargs="?")
    args = parser.parse_args()
    vm = str(Path(args.vm).resolve())
    aot = str(Path(args.aot).resolve())
    dwarfdump = str(Path(args.dwarfdump).resolve()) if args.dwarfdump else ""
    TMP_ROOT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="t2-debug-", dir=TMP_ROOT) as tmp:
        base = Path(tmp)
        root = project(base, "root")
        check_static(vm, aot, root)
        check_cli(vm, aot, root)
        check_output_safety(vm, aot, root)
        check_public_debug(aot, dwarfdump, root)
        check_cross_root(aot, base)
    print(f"T2 debug checks passed: {CHECKS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
