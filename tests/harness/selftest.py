"""Self-test for the TC 0.0.46 S0 harness.

Runs only in-process fakes and temporary files.  No compiler, no network and
no writes outside the system temporary directory are used.  The script exits
0 only when every named scenario behaves as specified.
"""

import contextlib
import copy
import io
import json
import os
import signal
import sys
import tempfile
import time

try:
    from . import case_schema, diag_codes, runner, yaml_subset
except ImportError:  # script execution: python3 tests/harness/selftest.py
    import case_schema
    import diag_codes
    import runner
    import yaml_subset

SCENARIOS = []


def scenario(function):
    SCENARIOS.append((function.__name__, function))
    return function


def _expect_status(case, adapter, expected, timeout=10.0):
    report = runner.execute_case(case, adapter, default_timeout=timeout)
    if report["status"] != expected:
        raise AssertionError(
            f"expected status {expected!r}, got {report['status']!r}: "
            f"{json.dumps(report['details'], ensure_ascii=False)}"
        )
    return report


def _compile_case(case_id="TC-SEM-HARNESS-0001@w64"):
    return case_schema.validate_case({
        "id": case_id,
        "profiles": ["w64"],
        "source": "sem/conv/bitcast_ptr_int_64.tc",
        "spec": "selftest only",
        "expect": {
            "stage": "SEM",
            "code": "TC_CE_BITCAST_WIDTH",
            "primary": {"file": "main.tc", "line": 7, "column": 11},
            "related": [],
            "diagnostics": "exactly-one",
        },
    })


def _diagnostic(code="TC_CE_BITCAST_WIDTH", line=7, column=11):
    return {
        "stage": "SEM",
        "code": code,
        "primary": {"file": "main.tc", "line": line, "column": column},
        "related": [],
    }


def _compile_adapter(diagnostics):
    def adapter(case, action):
        if action != "compile":
            raise AssertionError(f"unexpected action {action!r}")
        return {"diagnostics": diagnostics}
    return adapter


@scenario
def compile_correct_expectation_passes():
    _expect_status(
        _compile_case(), _compile_adapter([_diagnostic()]), "pass"
    )


@scenario
def compile_wrong_expected_code_fails():
    case = _compile_case()
    case["expect"]["code"] = "TC_CE_TRUNC_WIDTH"
    _expect_status(
        case, _compile_adapter([_diagnostic()]), "fail"
    )


@scenario
def compile_missing_diagnostic_fails():
    _expect_status(
        _compile_case(), _compile_adapter([]), "fail"
    )


@scenario
def compile_extra_diagnostic_fails():
    _expect_status(
        _compile_case(),
        _compile_adapter([_diagnostic(), _diagnostic()]),
        "fail",
    )


@scenario
def compile_wrong_primary_position_fails():
    _expect_status(
        _compile_case(),
        _compile_adapter([_diagnostic(line=8)]),
        "fail",
    )


@scenario
def run_correct_stdout_fails_nothing():
    case = case_schema.validate_case({
        "id": "TC-RT-HARNESS-0001@w64",
        "profiles": ["w64"],
        "source": "runtime/fake.tc",
        "spec": "selftest only",
        "run": {"stdin_hex": ""},
        "expect": {
            "stdout_hex": "4f 4b 0a",
            "trap": None,
            "exit": 0,
            "termination": "normal",
        },
    })

    def adapter(case, action):
        return {
            "stdout_hex": "4f 4b 0a",
            "trap": None,
            "exit": 0,
            "termination": "normal",
        }

    _expect_status(case, adapter, "pass")


@scenario
def run_timeout_fails():
    case = case_schema.validate_case({
        "id": "TC-RT-HARNESS-0002@w64",
        "profiles": ["w64"],
        "source": "runtime/slow.tc",
        "spec": "selftest only",
        "run": {"stdin_hex": ""},
        "timeout_s": 0.2,
        "expect": {
            "stdout_hex": "",
            "trap": None,
            "exit": 0,
            "termination": "normal",
        },
    })

    def adapter(case, action):
        time.sleep(5)
        return {
            "stdout": b"", "trap": None, "exit": 0, "termination": "normal",
        }

    report = _expect_status(case, adapter, "fail")
    if "timed out" not in report["details"].get("reason", ""):
        raise AssertionError(f"timeout reason missing: {report['details']!r}")


@scenario
def adapter_crash_fails():
    case = _compile_case()

    def adapter(case, action):
        os.kill(os.getpid(), signal.SIGKILL)
        return {}

    report = _expect_status(case, adapter, "fail")
    reason = report["details"].get("reason", "")
    if "signal" not in reason and "crash" not in reason:
        raise AssertionError(f"crash reason missing: {report['details']!r}")


@scenario
def not_implemented_is_not_pass():
    case = _compile_case()

    def adapter(case, action):
        raise NotImplementedError("S0 fake")

    report = _expect_status(case, adapter, "not_implemented")
    if report["status"] == "pass":
        raise AssertionError("not-implemented was counted as pass")


@scenario
def not_implemented_gate_exits_nonzero():
    with tempfile.TemporaryDirectory(prefix="tc-harness-selftest-") as tmp:
        suite = os.path.join(tmp, "suite")
        os.makedirs(suite)
        case_path = os.path.join(suite, "case.yaml")
        with open(case_path, "w", encoding="utf-8") as handle:
            handle.write(
                "id: TC-SEM-HARNESS-9001@w64\n"
                "profiles: [w64]\n"
                "source: fake.tc\n"
                "spec: selftest only\n"
                "expect:\n"
                "  stage: SEM\n"
                "  code: TC_CE_BITCAST_WIDTH\n"
                "  primary: {file: main.tc, line: 7, column: 11}\n"
                "  related: []\n"
                "  diagnostics: exactly-one\n"
            )
        adapter_path = os.path.join(tmp, "fake_adapter.py")
        with open(adapter_path, "w", encoding="utf-8") as handle:
            handle.write(
                "def adapter(case, action):\n"
                "    raise NotImplementedError('S0 fake')\n"
            )
        report_path = os.path.join(tmp, "report.json")
        command = [
            suite, "--adapter", adapter_path, "--json-out", report_path,
        ]
        first_code, first_output = _run_cli(command)
        if first_code == 0:
            raise AssertionError(
                "gate exited zero with a not-implemented case: "
                f"{first_output!r}"
            )
        with open(report_path, "r", encoding="utf-8") as handle:
            report = json.load(handle)
        summary = report["summary"]
        if summary["pass"] != 0 or summary["not_implemented"] != 1:
            raise AssertionError(f"bad summary: {summary!r}")
        allowed_code, allowed_output = _run_cli(
            command + ["--allow-not-implemented"]
        )
        if allowed_code != 0:
            raise AssertionError(
                "--allow-not-implemented did not exit zero: "
                f"{allowed_output!r}"
            )
        with open(report_path, "r", encoding="utf-8") as handle:
            allowed_report = json.load(handle)
        if allowed_report["summary"]["pass"] != 0:
            raise AssertionError("not-implemented leaked into pass count")


def _run_cli(arguments):
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = runner.main(arguments)
    return code, out.getvalue() + err.getvalue()


@scenario
def address_normalization_makes_diff_pass():
    base = {
        "id": "TC-DIFF-HARNESS-0001",
        "profile": "aarch64-linux-user",
        "source": "diff/fake.tc",
        "spec": "selftest only",
        "compare": [
            "stdout_bytes", "trap_code", "exit_status", "termination",
        ],
        "normalize": ["address_literals"],
        "expect": {"diff": "identical"},
    }
    case = case_schema.validate_case(base)
    unnormalized = copy.deepcopy(base)
    unnormalized["id"] = "TC-DIFF-HARNESS-0002"
    del unnormalized["normalize"]
    plain_case = case_schema.validate_case(unnormalized)

    def adapter(case, action):
        if action == "vm":
            stdout = b"addr 0x1000\n"
        elif action == "aot":
            stdout = b"addr 0x2000\n"
        else:
            raise AssertionError(f"unexpected action {action!r}")
        return {
            "stdout": stdout, "trap": None, "exit": 0,
            "termination": "normal",
        }

    _expect_status(case, adapter, "pass")
    _expect_status(plain_case, adapter, "fail")


@scenario
def skip_and_unknown_status_are_never_pass():
    # Metadata-level skip is a schema error, not a skip status.
    case = _compile_case()
    case["skip"] = True
    try:
        case_schema.validate_case(case)
    except case_schema.SchemaError:
        pass
    else:
        raise AssertionError("schema accepted a skip field")

    # An adapter that tries to report a skip status must not become pass.
    report = runner.execute_case(
        _compile_case(), lambda c, a: {"status": "skip"}, default_timeout=5.0
    )
    if report["status"] == "pass":
        raise AssertionError("unknown adapter status was counted as pass")
    if report["status"] not in ("fail", "not_implemented"):
        raise AssertionError(f"unexpected status {report['status']!r}")


@scenario
def yaml_subset_parses_appendix_c_shape():
    text = """
id: TC-SEM-CONV-0042@w32
profiles: [w32]
source: sem/conv/bitcast_ptr_int_32.tc
include_roots: [fixtures/include]
spec: "section 3.11 bitcast matrix"
expect:
  stage: SEM
  code: TC_CE_BITCAST_WIDTH
  primary: {file: main.tc, line: 7, column: 11}
  related: []
  diagnostics: exactly-one
nullable: null
count: 7
hexint: 0x1f
quoted: 'it''s ok'
items:
  - {file: a.tc, line: 1, column: 2}
  - file: b.tc
    line: 3
    column: 4
"""
    data = yaml_subset.parse(text)
    assert data["id"] == "TC-SEM-CONV-0042@w32"
    assert data["profiles"] == ["w32"]
    assert data["include_roots"] == ["fixtures/include"]
    assert data["expect"]["primary"] == {
        "file": "main.tc", "line": 7, "column": 11,
    }
    assert data["expect"]["related"] == []
    assert data["nullable"] is None
    assert data["count"] == 7
    assert data["hexint"] == 31
    assert data["quoted"] == "it's ok"
    assert data["items"][0] == {"file": "a.tc", "line": 1, "column": 2}
    assert data["items"][1]["column"] == 4


@scenario
def schema_rejects_unknown_and_missing_fields():
    good = {
        "id": "TC-SEM-HARNESS-0001@w64",
        "profiles": ["w64"],
        "source": "fake.tc",
        "spec": "selftest only",
        "expect": {
            "stage": "SEM",
            "code": "TC_CE_BITCAST_WIDTH",
            "primary": {"file": "main.tc", "line": 7, "column": 11},
            "related": [],
            "diagnostics": "exactly-one",
        },
    }
    bad_cases = []
    unknown = copy.deepcopy(good)
    unknown["mystery"] = 1
    bad_cases.append((unknown, "unknown field"))
    missing = copy.deepcopy(good)
    del missing["spec"]
    bad_cases.append((missing, "missing required field 'spec'"))
    bad_position = copy.deepcopy(good)
    bad_position["expect"]["primary"]["line"] = 0
    bad_cases.append((bad_position, "1-based integer"))
    bad_expect = copy.deepcopy(good)
    bad_expect["expect"]["trap"] = None
    bad_cases.append((bad_expect, "unknown field"))
    both_profiles = copy.deepcopy(good)
    both_profiles["profile"] = "w64"
    bad_cases.append((both_profiles, "exactly one of profiles/profile"))
    bad_code = copy.deepcopy(good)
    bad_code["expect"]["code"] = "TC_CE_NOT_A_CODE"
    bad_cases.append((bad_code, "cannot be reported"))
    for index, (data, needle) in enumerate(bad_cases):
        try:
            case_schema.validate_case(data)
        except case_schema.SchemaError as exc:
            if needle not in str(exc):
                raise AssertionError(
                    f"case {index}: expected {needle!r} in error, got {exc}"
                )
            continue
        raise AssertionError(f"case {index}: schema accepted invalid metadata")


@scenario
def frozen_diagnostic_table_counts():
    if len(diag_codes.DIAGNOSTIC_CODES) != 80:
        raise AssertionError("diagnostic table is not 80 codes")
    if len(diag_codes.CE_CODES) != 65 or len(diag_codes.RE_CODES) != 15:
        raise AssertionError("diagnostic table is not 65 CE + 15 RE")
    if len(diag_codes.IMPORT_CODES) != 5:
        raise AssertionError("IMPORT code set is not 5 codes")


def main():
    passed = 0
    failed = 0
    for name, function in SCENARIOS:
        try:
            function()
        except Exception as exc:
            failed += 1
            print(f"FAIL {name}: {type(exc).__name__}: {exc}")
        else:
            passed += 1
            print(f"PASS {name}")
    print(
        f"selftest: {passed} passed, {failed} failed, "
        f"{passed + failed} scenarios"
    )
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
