#!/usr/bin/env python3
"""Validate signed G11 real-GDB evidence artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASE_ID = "TC-G11-EVIDENCE-0001"
SCHEMA_VERSION = 1
SIGNER_WORKFLOW = ".github/workflows/ci.yml"
REQUIRED_PROFILES = {
    "aarch64-linux-user",
    "aarch64-baremetal",
    "arm32-baremetal",
    "riscv32-baremetal",
}
REQUIRED_SUITES = {
    "toolchain.debug_elf",
    "vm.gdb.e2e",
    "toolchain.aot_debug",
    "toolchain.debug_smoke",
}
SHA256_RE = re.compile(r"sha256:[0-9a-f]{64}\Z")
COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_result(data: object, failures: list[str]) -> dict | None:
    if not isinstance(data, dict):
        failures.append(f"{CASE_ID}: result.json must contain an object")
        return None
    if data.get("schema_version") != SCHEMA_VERSION:
        failures.append(f"{CASE_ID}: unsupported or missing schema_version")
    if data.get("status") != "PASS":
        failures.append(f"{CASE_ID}: evidence status is not PASS")
    commit = data.get("commit")
    if not isinstance(commit, str) or COMMIT_RE.fullmatch(commit) is None:
        failures.append(f"{CASE_ID}: commit must be a full lowercase SHA")
    container_digest = data.get("container_digest")
    if not isinstance(container_digest, str) or \
            SHA256_RE.fullmatch(container_digest) is None:
        failures.append(f"{CASE_ID}: container_digest must be sha256:<64 hex>")
    gdb_version = data.get("gdb_version")
    if not isinstance(gdb_version, str) or "GNU gdb" not in gdb_version:
        failures.append(f"{CASE_ID}: gdb_version must contain the raw GNU gdb banner")
    profiles = data.get("profiles")
    if not isinstance(profiles, list) or not all(isinstance(item, str) for item in profiles):
        failures.append(f"{CASE_ID}: profiles must be a string array")
        profiles = []
    if set(profiles) != REQUIRED_PROFILES or len(profiles) != len(REQUIRED_PROFILES):
        failures.append(
            f"{CASE_ID}: profiles must be exactly {sorted(REQUIRED_PROFILES)}")
    suites = data.get("suites")
    if not isinstance(suites, list):
        failures.append(f"{CASE_ID}: suites must be an array")
        return None
    names: list[str] = []
    assertions = 0
    for index, suite in enumerate(suites):
        label = f"suites[{index}]"
        if not isinstance(suite, dict):
            failures.append(f"{CASE_ID}: {label} must be an object")
            continue
        name = suite.get("name")
        if not isinstance(name, str) or not name:
            failures.append(f"{CASE_ID}: {label}.name must be a nonempty string")
        else:
            names.append(name)
        suite_profiles = suite.get("profiles")
        if not isinstance(suite_profiles, list) or not suite_profiles or \
                not all(isinstance(item, str) and item in REQUIRED_PROFILES
                        for item in suite_profiles) or \
                len(set(suite_profiles)) != len(suite_profiles):
            failures.append(f"{CASE_ID}: {label}.profiles is invalid")
        cases = suite.get("cases")
        if not isinstance(cases, list) or not cases or \
                not all(isinstance(item, str) and item and "SKIP" not in item.upper()
                        for item in cases) or len(set(cases)) != len(cases):
            failures.append(f"{CASE_ID}: {label}.cases must contain unique non-SKIP names")
        count = suite.get("assertions")
        if isinstance(count, bool) or not isinstance(count, int) or count <= 0:
            failures.append(f"{CASE_ID}: {label}.assertions must be a positive integer")
        else:
            assertions += count
    if set(names) != REQUIRED_SUITES or len(names) != len(REQUIRED_SUITES):
        failures.append(
            f"{CASE_ID}: suites must be exactly {sorted(REQUIRED_SUITES)}")
    by_name = {
        suite["name"]: suite
        for suite in suites
        if isinstance(suite, dict) and isinstance(suite.get("name"), str)
    }
    for name in ("toolchain.debug_elf", "vm.gdb.e2e"):
        suite = by_name.get(name)
        profiles = suite.get("profiles") if isinstance(suite, dict) else None
        if not isinstance(profiles, list) or \
                set(profiles) != REQUIRED_PROFILES or len(profiles) != len(REQUIRED_PROFILES):
            failures.append(f"{CASE_ID}: {name} must cover all four profiles")
    total = data.get("assertions")
    if isinstance(total, bool) or not isinstance(total, int) or total != assertions:
        failures.append(f"{CASE_ID}: assertions does not match the suite total")
    transcript_digest = data.get("transcript_sha256")
    if not isinstance(transcript_digest, str) or \
            re.fullmatch(r"[0-9a-f]{64}", transcript_digest) is None:
        failures.append(f"{CASE_ID}: transcript_sha256 must be 64 lowercase hex")
    return data


def verify_attestation(path: Path, repo: str, workflow: str,
                       source_digest: str, failures: list[str]) -> None:
    gh = shutil.which("gh")
    if gh is None:
        failures.append(f"{CASE_ID}: gh is required to verify attestations")
        return
    expected_digest = sha256_file(path)
    command = [gh, "attestation", "verify", str(path), "--repo", repo,
               "--signer-workflow", workflow, "--source-digest", source_digest,
               "--format", "json"]
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        failures.append(f"{CASE_ID}: attestation verification failed for {path.name}: {error}")
        return
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        failures.append(
            f"{CASE_ID}: attestation verification failed for {path.name}: {detail}")
        return
    output = proc.stdout.decode("utf-8", "replace")
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        failures.append(
            f"{CASE_ID}: gh returned invalid attestation JSON for {path.name}: {error}")
        return
    if not isinstance(payload, list) or not payload:
        failures.append(
            f"{CASE_ID}: gh returned no attestation objects for {path.name}")
        return
    for entry in payload:
        if not isinstance(entry, dict):
            continue
        verification = entry.get("verificationResult")
        statement = verification.get("statement") if isinstance(verification, dict) else None
        subjects = statement.get("subject") if isinstance(statement, dict) else None
        if not isinstance(subjects, list):
            continue
        for subject in subjects:
            if not isinstance(subject, dict):
                continue
            digest = subject.get("digest")
            value = digest.get("sha256") if isinstance(digest, dict) else None
            if value in (expected_digest, f"sha256:{expected_digest}"):
                return
    failures.append(f"{CASE_ID}: attestation subject digest mismatch for {path.name}")


def validate_transcript(transcript: str, data: dict,
                        failures: list[str]) -> None:
    lines = transcript.splitlines()
    if not lines or lines[0] != f"commit={data['commit']}":
        failures.append(f"{CASE_ID}: transcript commit does not match result.json")
    if len(lines) < 2 or \
            lines[1] != f"container_digest={data['container_digest']}":
        failures.append(
            f"{CASE_ID}: transcript container digest does not match result.json")
    if len(lines) < 3 or not lines[2].startswith("gdb_version_json="):
        failures.append(f"{CASE_ID}: transcript gdb version header is missing")
    else:
        try:
            version = json.loads(lines[2].removeprefix("gdb_version_json="))
        except json.JSONDecodeError as error:
            failures.append(f"{CASE_ID}: transcript gdb version is invalid: {error}")
        else:
            if version != data["gdb_version"]:
                failures.append(f"{CASE_ID}: transcript gdb version does not match result.json")
    if "SKIP:" in transcript:
        failures.append(f"{CASE_ID}: transcript contains a SKIP result")

    expected_cases: set[str] = set()
    expected_suites: dict[str, int] = {}
    for suite in data["suites"]:
        expected_suites[suite["name"]] = suite["assertions"]
        for case in suite["cases"]:
            expected_cases.add(f"{suite['name']}/{case}")
    actual_cases: dict[str, int] = {}
    actual_suites: dict[str, int] = {}
    for line in lines:
        match = re.fullmatch(r"evidence-case=(.+) assertions=(\d+)", line)
        if match is not None:
            name, count = match.group(1), int(match.group(2))
            if name in actual_cases:
                failures.append(f"{CASE_ID}: duplicate transcript case {name}")
            actual_cases[name] = count
            continue
        match = re.fullmatch(r"evidence-suite=(.+) assertions=(\d+)", line)
        if match is not None:
            name, count = match.group(1), int(match.group(2))
            if name in actual_suites:
                failures.append(f"{CASE_ID}: duplicate transcript suite {name}")
            actual_suites[name] = count
    if set(actual_cases) != set(expected_cases):
        failures.append(f"{CASE_ID}: transcript cases do not match result.json")
    if set(actual_suites) != set(expected_suites):
        failures.append(f"{CASE_ID}: transcript suites do not match result.json")
    for name, count in actual_cases.items():
        if count <= 0:
            failures.append(f"{CASE_ID}: transcript case {name} has no assertions")
    for name, count in actual_suites.items():
        if expected_suites.get(name) != count:
            failures.append(f"{CASE_ID}: transcript suite {name} total does not match result.json")
        case_total = sum(
            value for case, value in actual_cases.items()
            if case.startswith(f"{name}/"))
        if case_total != count:
            failures.append(f"{CASE_ID}: transcript suite {name} case totals do not match")


def verify_evidence(artifact_dir: Path, repo: str, workflow: str,
                    expected_commit: str | None = None) -> list[str]:
    failures: list[str] = []
    result_path = artifact_dir / "result.json"
    transcript_path = artifact_dir / "transcript.log"
    if not result_path.is_file():
        failures.append(f"{CASE_ID}: missing result.json")
        return failures
    if not transcript_path.is_file():
        failures.append(f"{CASE_ID}: missing transcript.log")
        return failures
    try:
        data = json.loads(result_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"{CASE_ID}: cannot read result.json: {error}")
        return failures
    validated = validate_result(data, failures)
    if validated is None or failures:
        return failures
    if expected_commit is not None and validated.get("commit") != expected_commit:
        failures.append(f"{CASE_ID}: evidence commit does not match the audited commit")
    if validated.get("transcript_sha256") != sha256_file(transcript_path):
        failures.append(f"{CASE_ID}: transcript digest mismatch")
    try:
        transcript = transcript_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        failures.append(f"{CASE_ID}: cannot read transcript.log: {error}")
    else:
        validate_transcript(transcript, validated, failures)
    if not failures:
        source_digest = validated["commit"]
        verify_attestation(result_path, repo, workflow, source_digest, failures)
        verify_attestation(transcript_path, repo, workflow, source_digest, failures)
    return failures


def current_commit() -> str:
    value = os.environ.get("GITHUB_SHA")
    if value:
        return value
    proc = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=30)
    if proc.returncode != 0:
        raise RuntimeError("cannot determine the current commit")
    return proc.stdout.decode("ascii").strip()


def required_tool(name: str) -> str:
    value = shutil.which(name)
    if value is not None:
        return value
    for hint in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin",
                 "/opt/homebrew/bin", "/usr/bin"):
        candidate = Path(hint) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise RuntimeError(f"required tool '{name}' not found")


def required_gdb() -> str:
    for name in ("gdb-multiarch", "gdb"):
        value = shutil.which(name)
        if value is not None:
            return value
    raise RuntimeError("required tool 'gdb-multiarch' or 'gdb' not found")


def assertion_count(output: str) -> int:
    match = re.search(r"debug ELF checks: (\d+)/(\d+)", output)
    if match is not None:
        return int(match.group(1))
    matches = re.findall(r"(\d+) checks", output)
    if not matches:
        return 0
    return sum(int(item) for item in matches)


def collect_evidence(build_dir: Path, output_dir: Path,
                     container_digest: str) -> int:
    failures: list[str] = []
    if SHA256_RE.fullmatch(container_digest) is None:
        raise ValueError("container digest must be sha256:<64 hex>")
    commit = current_commit()
    if COMMIT_RE.fullmatch(commit) is None:
        raise ValueError("current commit must be a full lowercase SHA")
    vm = build_dir / "src" / "driver" / "tc-vm"
    aot = build_dir / "src" / "driver" / "tc-aot"
    fixture = build_dir / "tests" / "tc_debug_fixture"
    for path in (vm, aot, fixture):
        if not path.is_file():
            raise RuntimeError(f"missing build artifact: {path}")
    gdb = required_gdb()
    dwarfdump = required_tool("llvm-dwarfdump")
    qemu = required_tool("qemu-system-aarch64")
    version = subprocess.run([gdb, "--version"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=30)
    gdb_version = (version.stdout + version.stderr).decode("utf-8", "replace")
    if version.returncode != 0 or "GNU gdb" not in gdb_version:
        raise RuntimeError("gdb --version did not report a GNU gdb banner")
    python = sys.executable
    specs = (
        {
            "name": "toolchain.debug_elf",
            "profiles": sorted(REQUIRED_PROFILES),
            "cases": ["debug-elf:four-profile-semantic-decode"],
            "commands": [[python, str(ROOT / "tests" / "tools" / "debug_elf_test.py"),
                          str(vm), str(aot), dwarfdump, gdb]],
        },
        {
            "name": "vm.gdb.e2e",
            "profiles": sorted(REQUIRED_PROFILES),
            "cases": [
                "aarch64-linux-user:source-breakpoint",
                "aarch64-baremetal:source-breakpoint",
                "arm32-baremetal:source-breakpoint",
                "riscv32-baremetal:source-breakpoint",
            ],
            "commands": [
                [python, str(ROOT / "tests" / "tools" / "vm_gdb_e2e.py"),
                 str(vm), gdb, "--profile=aarch64-linux-user"],
                [python, str(ROOT / "tests" / "tools" / "vm_gdb_e2e.py"),
                 str(vm), gdb, "--profile=aarch64-baremetal"],
                [python, str(ROOT / "tests" / "tools" / "vm_gdb_e2e.py"),
                 str(vm), gdb, "--profile=arm32-baremetal"],
                [python, str(ROOT / "tests" / "tools" / "vm_gdb_e2e.py"),
                 str(vm), gdb, "--profile=riscv32-baremetal"],
            ],
        },
        {
            "name": "toolchain.aot_debug",
            "profiles": sorted(REQUIRED_PROFILES),
            "cases": ["aot-debug:four-profile-dwarf"],
            "commands": [[python, str(ROOT / "tests" / "conformance" / "tools" /
                                      "p4a_debug_cases.py"), "--check",
                          str(fixture), dwarfdump]],
        },
        {
            "name": "toolchain.debug_smoke",
            "profiles": ["aarch64-baremetal"],
            "cases": ["aarch64-baremetal:qemu-gdbstub-source-breakpoint"],
            "commands": [[python, str(ROOT / "tests" / "tools" / "t2_gdb_test.py"),
                          str(aot), qemu, gdb]],
        },
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    transcript_path = output_dir / "transcript.log"
    suites = []
    with transcript_path.open("w", encoding="utf-8") as transcript:
        transcript.write(f"commit={commit}\ncontainer_digest={container_digest}\n")
        transcript.write(
            f"gdb_version_json={json.dumps(gdb_version, ensure_ascii=True)}\n")
        for spec in specs:
            suite_output: list[str] = []
            suite_assertions = 0
            for case, command in zip(spec["cases"], spec["commands"]):
                transcript.write(f"\n$ {shlex.join(command)}\n")
                try:
                    proc = subprocess.run(command, cwd=ROOT,
                                          stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, timeout=300)
                except (OSError, subprocess.SubprocessError) as error:
                    output = f"{type(error).__name__}: {error}\n"
                    proc = None
                else:
                    output = proc.stdout.decode("utf-8", "replace")
                transcript.write(output)
                suite_output.append(output)
                if proc is None or proc.returncode != 0:
                    failures.append(f"{spec['name']}/{case}: command failed")
                if "SKIP:" in output:
                    failures.append(f"{spec['name']}/{case}: SKIP is not PASS")
                count = assertion_count(output)
                checks = re.search(r"debug ELF checks: (\d+)/(\d+)", output)
                if checks is not None and checks.group(1) != checks.group(2):
                    failures.append(f"{spec['name']}/{case}: incomplete assertion run")
                if count <= 0:
                    failures.append(f"{spec['name']}/{case}: no assertion count was reported")
                suite_assertions += count
                transcript.write(
                    f"evidence-case={spec['name']}/{case} assertions={count}\n")
            if suite_assertions <= 0:
                failures.append(f"{spec['name']}: no assertion count was reported")
            suites.append({
                "name": spec["name"],
                "profiles": spec["profiles"],
                "cases": spec["cases"],
                "assertions": suite_assertions,
            })
            transcript.write(
                f"evidence-suite={spec['name']} assertions={suite_assertions}\n")
    result = {
        "schema_version": SCHEMA_VERSION,
        "status": "PASS" if not failures else "FAIL",
        "commit": commit,
        "container_digest": container_digest,
        "gdb_version": gdb_version,
        "profiles": sorted(REQUIRED_PROFILES),
        "suites": suites,
        "assertions": sum(suite["assertions"] for suite in suites),
        "transcript_sha256": sha256_file(transcript_path),
    }
    (output_dir / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures:
            print(failure)
        print(f"{CASE_ID}: evidence collection failed")
        return 1
    print(f"{CASE_ID}: evidence collected at {output_dir}")
    return 0


def valid_fixture(directory: Path) -> dict:
    suites = []
    for name in sorted(REQUIRED_SUITES):
        suites.append({
            "name": name,
            "profiles": sorted(REQUIRED_PROFILES),
            "cases": [f"{name}:source-breakpoint"],
            "assertions": 1,
        })
    gdb_version = "GNU gdb (GDB) 18.1\n"
    lines = [
        f"commit={'a' * 40}",
        f"container_digest=sha256:{'b' * 64}",
        f"gdb_version_json={json.dumps(gdb_version)}",
        "all real-GDB checks passed",
    ]
    for suite in suites:
        for case in suite["cases"]:
            lines.append(
                f"evidence-case={suite['name']}/{case} assertions=1")
        lines.append(f"evidence-suite={suite['name']} assertions=1")
    transcript = directory / "transcript.log"
    transcript.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {
        "schema_version": SCHEMA_VERSION,
        "status": "PASS",
        "commit": "a" * 40,
        "container_digest": "sha256:" + "b" * 64,
        "gdb_version": gdb_version,
        "profiles": sorted(REQUIRED_PROFILES),
        "suites": suites,
        "assertions": len(suites),
        "transcript_sha256": sha256_file(transcript),
    }


def write_fixture(directory: Path, data: dict) -> None:
    (directory / "result.json").write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def selftest() -> int:
    if assertion_count("debug ELF checks: 7/8") != 7 or \
            assertion_count("suite passed: 11 checks") != 11 or \
            assertion_count("SKIP: no checks") != 0:
        print("release evidence selftest: assertion counter is incorrect")
        return 1
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="g11-evidence-", dir=ROOT / ".tmp") as tmp:
        directory = Path(tmp)
        data = valid_fixture(directory)
        write_fixture(directory, data)
        failures: list[str] = []
        validate_result(data, failures)
        if failures:
            print(failures[0])
            return 1
        mutations = (
            lambda value: value.update(status="SKIP"),
            lambda value: value.update(schema_version=SCHEMA_VERSION + 1),
            lambda value: value.update(commit="short"),
            lambda value: value.update(container_digest="sha256:bad"),
            lambda value: value.update(gdb_version="unknown"),
            lambda value: value.update(profiles=["aarch64-linux-user"]),
            lambda value: value.update(suites=value["suites"][:-1]),
            lambda value: value["suites"][0].update(assertions=0),
            lambda value: value["suites"][0].update(cases=["SKIP: gdb"]),
            lambda value: value["suites"][0].update(name=[]),
            lambda value: value["suites"][0].update(
                profiles=sorted(REQUIRED_PROFILES) + ["arm32-baremetal"]),
            lambda value: value.update(assertions=value["assertions"] + 1),
            lambda value: value.update(transcript_sha256="0" * 64),
        )
        for index, mutate in enumerate(mutations):
            changed = json.loads(json.dumps(data))
            mutate(changed)
            failures = validate_result(changed, [])
            if not failures:
                print(f"release evidence selftest: mutation {index} was not rejected")
                return 1
        transcript = (directory / "transcript.log").read_text(encoding="utf-8")
        failures = []
        validate_transcript(transcript, data, failures)
        if failures:
            print(failures[0])
            return 1
        transcript_mutations = (
            transcript.replace("assertions=1", "assertions=0", 1),
            transcript.replace("evidence-suite=toolchain.aot_debug assertions=1\n", ""),
            transcript.replace('gdb_version_json="GNU gdb (GDB) 18.1\\n"',
                               'gdb_version_json="unknown"'),
            transcript + "evidence-case=toolchain.aot_debug/aot-debug:four-profile-dwarf assertions=1\n",
        )
        for index, changed in enumerate(transcript_mutations):
            failures = []
            validate_transcript(changed, data, failures)
            if not failures:
                print(f"release evidence selftest: transcript mutation {index} was not rejected")
                return 1
        original_run = subprocess.run
        try:
            subprocess.run = lambda *args, **kwargs: subprocess.CompletedProcess(
                args[0], 1, stdout=b"", stderr=b"signature invalid")
            failures = verify_evidence(directory, "owner/repo", ".github/workflows/ci.yml")
        finally:
            subprocess.run = original_run
        if not failures:
            print("release evidence selftest: invalid signature was not rejected")
            return 1
        original_which = shutil.which

        def verified_run(*args, **kwargs):
            command = args[0]
            path = Path(command[3])
            if "--source-digest" not in command or \
                    command[command.index("--source-digest") + 1] != "a" * 40:
                return subprocess.CompletedProcess(
                    command, 1, stdout=b"", stderr=b"source digest missing")
            payload = [{
                "verificationResult": {
                    "statement": {
                        "subject": [{
                            "name": path.name,
                            "digest": {"sha256": sha256_file(path)},
                        }],
                    },
                },
            }]
            return subprocess.CompletedProcess(
                command, 0, stdout=json.dumps(payload).encode(), stderr=b"")

        try:
            shutil.which = lambda name: "/usr/bin/gh" if name == "gh" else original_which(name)
            subprocess.run = verified_run
            failures = verify_evidence(directory, "owner/repo", ".github/workflows/ci.yml")
        finally:
            subprocess.run = original_run
            shutil.which = original_which
        if failures:
            print(f"release evidence selftest: valid attestation rejected: {failures}")
            return 1
        try:
            shutil.which = lambda name: "/usr/bin/gh" if name == "gh" else original_which(name)
            subprocess.run = lambda *args, **kwargs: subprocess.CompletedProcess(
                args[0], 0, stdout=json.dumps([{
                    "verificationResult": {
                        "statement": {
                            "subject": [{"digest": {"sha256": "0" * 64}}],
                        },
                    },
                }]).encode(), stderr=b"")
            failures = verify_evidence(directory, "owner/repo", ".github/workflows/ci.yml")
        finally:
            subprocess.run = original_run
            shutil.which = original_which
        if not any("subject digest mismatch" in item for item in failures):
            print("release evidence selftest: subject digest mismatch was not rejected")
            return 1
    print(f"{CASE_ID}: evidence schema and signature failures rejected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--selftest", action="store_true")
    mode.add_argument("--check", type=Path)
    mode.add_argument("--collect", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--container-digest")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--commit")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.collect is not None:
        if args.build_dir is None or not args.container_digest:
            print(f"{CASE_ID}: --collect requires --build-dir and --container-digest")
            return 2
        try:
            return collect_evidence(args.build_dir.resolve(), args.collect.resolve(),
                                    args.container_digest)
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            print(f"{CASE_ID}: {error}")
            return 1
    if not args.repo:
        print(f"{CASE_ID}: --repo or GITHUB_REPOSITORY is required")
        return 2
    failures = verify_evidence(args.check.resolve(), args.repo,
                               SIGNER_WORKFLOW, args.commit)
    if failures:
        for failure in failures:
            print(failure)
        return 1
    print(f"{CASE_ID}: signed real-GDB evidence passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
