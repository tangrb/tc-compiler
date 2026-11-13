"""The seven S0 deciders.

Each public `decide_*` function takes a validated case dict and an injected
adapter.  An adapter is either:

* an object with methods named after the action (compile/run/vm/aot/...),
* a mapping of action name to callable, or
* a callable invoked as ``adapter(case, action)``.

Deciders return a DecisionRecord.  Every failure path is closed: exceptions,
malformed results, missing output and unknown statuses become ``fail``.
"""

import hashlib
import json
import os
import re
from dataclasses import dataclass, field

try:
    from .case_schema import SchemaError, hex_to_bytes
    from .diag_codes import (
        ALL_STAGES, DIAGNOSTIC_CODES, RE_CODES, TERMINATIONS,
        actual_stage_matches,
    )
except ImportError:  # script execution
    from case_schema import SchemaError, hex_to_bytes
    from diag_codes import (
        ALL_STAGES, DIAGNOSTIC_CODES, RE_CODES, TERMINATIONS,
        actual_stage_matches,
    )

STATUSES = ("pass", "fail", "not_implemented")
COMPARE_FIELDS = {
    "stdout_bytes": "stdout",
    "trap_code": "trap",
    "exit_status": "exit",
    "termination": "termination",
}


@dataclass
class DecisionRecord:
    status: str
    details: dict = field(default_factory=dict)

    def __post_init__(self):
        if self.status not in STATUSES:
            raise ValueError(f"invalid decision status {self.status!r}")

    def to_dict(self):
        return {"status": self.status, "details": self.details}


class _AdapterNotImplemented(Exception):
    pass


class _AdapterFailure(Exception):
    pass


def _pass(**details):
    return DecisionRecord("pass", details)


def _fail(reason, **details):
    details["reason"] = reason
    return DecisionRecord("fail", details)


def _ni(reason):
    return DecisionRecord("not_implemented", {"reason": reason})


def _is_ni(result):
    return isinstance(result, dict) and result.get("status") == "not_implemented"


def _has_action(adapter, action):
    if isinstance(adapter, dict):
        return callable(adapter.get(action))
    if callable(getattr(adapter, action, None)):
        return True
    return callable(adapter)


def invoke(adapter, action, case):
    """Dispatch one adapter action, normalizing the supported adapter shapes."""
    if isinstance(adapter, dict):
        function = adapter.get(action)
        if not callable(function):
            raise _AdapterFailure(f"adapter mapping has no action {action!r}")
        return function(case)
    method = getattr(adapter, action, None)
    if callable(method):
        return method(case)
    if callable(adapter):
        return adapter(case, action)
    raise _AdapterFailure(f"adapter does not provide action {action!r}")


def _acquire(case, adapter, action):
    if adapter is None:
        raise _AdapterNotImplemented("no adapter configured")
    try:
        result = invoke(adapter, action, case)
    except _AdapterNotImplemented:
        raise
    except NotImplementedError as exc:
        raise _AdapterNotImplemented(str(exc) or "adapter is not implemented")
    except Exception as exc:  # adapter crash is a failure, never a skip
        raise _AdapterFailure(
            f"adapter action {action!r} raised {type(exc).__name__}: {exc}"
        )
    if _is_ni(result):
        reason = result.get("reason") or "adapter reported not_implemented"
        raise _AdapterNotImplemented(str(reason))
    return result


def _adapter_call(case, adapter, action):
    try:
        return _acquire(case, adapter, action), None
    except _AdapterNotImplemented as exc:
        return None, _ni(str(exc))
    except _AdapterFailure as exc:
        return None, _fail(str(exc))


# --------------------------------------------------------------------------
# Compile decider


def decide_compile(case, adapter):
    result, error = _adapter_call(case, adapter, "compile")
    if error:
        return error
    raw = _compile_diagnostics(result)
    if isinstance(raw, DecisionRecord):
        return raw
    expected = case["expect"]
    if len(raw) == 0:
        return _fail("missing diagnostic: adapter produced the empty result")
    if len(raw) > 1:
        return _fail(
            f"extra diagnostic: expected exactly-one, got {len(raw)}",
            actual=[_diag_summary(diag) for diag in raw],
        )
    actual = raw[0]
    problems = []
    if not actual_stage_matches(
        expected["code"], expected["stage"], actual["stage"]
    ):
        problems.append(
            f"stage expected {expected['stage']!r}, got {actual['stage']!r}"
        )
    if actual["code"] != expected["code"]:
        problems.append(f"code expected {expected['code']!r}, got {actual['code']!r}")
    for field in ("file", "line", "column"):
        if actual["primary"][field] != expected["primary"][field]:
            problems.append(
                f"primary.{field} expected {expected['primary'][field]!r}, "
                f"got {actual['primary'][field]!r}"
            )
    if actual["related"] != expected["related"]:
        problems.append(
            f"related expected {expected['related']!r}, got {actual['related']!r}"
        )
    if problems:
        return _fail(
            "; ".join(problems),
            expected=_diag_summary(expected),
            actual=_diag_summary(actual),
        )
    return _pass()


def _compile_diagnostics(result):
    if isinstance(result, list):
        raw = result
    elif isinstance(result, dict):
        if "diagnostics" in result:
            raw = result["diagnostics"]
            if not isinstance(raw, list):
                return _fail("malformed adapter result: diagnostics is not a list")
        elif "stage" in result or "code" in result:
            raw = [result]
        else:
            return []
    else:
        return _fail(
            "malformed adapter result: expected a mapping or diagnostic list"
        )
    diagnostics = []
    for index, item in enumerate(raw):
        normalized = _normalize_diagnostic(item, index)
        if isinstance(normalized, DecisionRecord):
            return normalized
        diagnostics.append(normalized)
    return diagnostics


def _normalize_diagnostic(item, index):
    if not isinstance(item, dict):
        return _fail(f"malformed diagnostic[{index}]: not a mapping")
    stage = item.get("stage")
    code = item.get("code")
    primary = item.get("primary")
    if "related" not in item:
        return _fail(f"malformed diagnostic[{index}]: missing related list")
    related = item["related"]
    if not isinstance(stage, str) or stage not in ALL_STAGES:
        return _fail(f"malformed diagnostic[{index}]: invalid stage {stage!r}")
    if not isinstance(code, str) or code not in DIAGNOSTIC_CODES:
        return _fail(f"malformed diagnostic[{index}]: unknown code {code!r}")
    position = _normalize_position(primary)
    if position is None:
        return _fail(f"malformed diagnostic[{index}]: invalid primary position")
    if not isinstance(related, list):
        return _fail(f"malformed diagnostic[{index}]: related is not a list")
    related_positions = []
    for related_index, entry in enumerate(related):
        normalized = _normalize_position(entry)
        if normalized is None:
            return _fail(
                f"malformed diagnostic[{index}].related[{related_index}]"
            )
        related_positions.append(normalized)
    return {
        "stage": stage, "code": code, "primary": position,
        "related": related_positions,
    }


def _normalize_position(value):
    if not isinstance(value, dict):
        return None
    file = value.get("file")
    line = value.get("line")
    column = value.get("column")
    if not isinstance(file, str) or not file:
        return None
    if isinstance(line, bool) or not isinstance(line, int) or line < 1:
        return None
    if isinstance(column, bool) or not isinstance(column, int) or column < 1:
        return None
    return {"file": file, "line": line, "column": column}


def _diag_summary(diag):
    return {
        "stage": diag.get("stage"),
        "code": diag.get("code"),
        "primary": diag.get("primary"),
        "related": diag.get("related", []),
    }


# --------------------------------------------------------------------------
# Run decider


def decide_run(case, adapter):
    result, error = _adapter_call(case, adapter, "run")
    if error:
        return error
    try:
        actual = _observation(result)
    except _AdapterFailure as exc:
        return _fail(str(exc))
    expected = case["expect"]
    problems = []
    expected_stdout = _expected_stdout(expected)
    if actual["stdout"] != expected_stdout:
        problems.append(
            f"stdout expected {expected_stdout.hex()}, got {actual['stdout'].hex()}"
        )
    for field in ("trap", "exit", "termination"):
        if actual[field] != expected[field]:
            problems.append(
                f"{field} expected {expected[field]!r}, got {actual[field]!r}"
            )
    if problems:
        return _fail("; ".join(problems), actual=_observation_summary(actual))
    return _pass()


def _expected_stdout(expect):
    return hex_to_bytes(expect["stdout_hex"], "expect.stdout_hex")


def _observation(result):
    if not isinstance(result, dict):
        raise _AdapterFailure(
            "malformed adapter result: expected a mapping observation"
        )
    stdout = _read_bytes(result, "stdout", required=True)
    if "trap" not in result:
        raise _AdapterFailure("malformed adapter result: missing trap")
    trap = result["trap"]
    if trap is not None and (not isinstance(trap, str) or trap not in RE_CODES):
        raise _AdapterFailure(f"malformed adapter result: invalid trap {trap!r}")
    if "exit" not in result:
        raise _AdapterFailure("malformed adapter result: missing exit")
    exit_status = result["exit"]
    if isinstance(exit_status, bool) or not isinstance(exit_status, int):
        raise _AdapterFailure("malformed adapter result: exit is not an integer")
    termination = result.get("termination")
    if termination not in TERMINATIONS:
        raise _AdapterFailure(
            f"malformed adapter result: invalid termination {termination!r}"
        )
    return {
        "stdout": stdout, "trap": trap, "exit": exit_status,
        "termination": termination,
    }


def _read_bytes(result, key, required=False):
    hex_key = key + "_hex"
    if hex_key in result:
        try:
            return hex_to_bytes(result[hex_key], hex_key)
        except SchemaError as exc:
            raise _AdapterFailure(f"malformed adapter result: {exc}") from exc
    if key in result:
        value = result[key]
        if isinstance(value, bytes):
            return value
        if isinstance(value, str):
            return value.encode("utf-8")
        raise _AdapterFailure(
            f"malformed adapter result: {key} must be bytes, str or {hex_key}"
        )
    if required:
        raise _AdapterFailure(f"malformed adapter result: missing {key}/{hex_key}")
    return None


def _observation_summary(observation):
    return {
        "stdout_hex": observation["stdout"].hex(),
        "trap": observation["trap"],
        "exit": observation["exit"],
        "termination": observation["termination"],
    }


# --------------------------------------------------------------------------
# Diff decider and normalization hooks


def _normalize_address_literals(data):
    pattern = re.compile(rb"(?<![0-9A-Za-z_])0[xX][0-9a-fA-F]+(?![0-9A-Za-z_])")
    return pattern.sub(b"<addr>", data)


def _normalize_stack_offsets(data):
    pattern = re.compile(
        rb"\b(sp|fp|bp)(\s*[+-]\s*)(?:0[xX][0-9a-fA-F]+|[0-9]+)"
    )
    return pattern.sub(rb"\1\2<off>", data)


# Public, documented extension point.  A rule is a bytes -> bytes callable.
NORMALIZER_HOOKS = {
    "address_literals": _normalize_address_literals,
    "stack_offsets_unobservable": _normalize_stack_offsets,
}


def normalize_stdout(data, rules):
    """Apply normalization hooks to a stdout byte string."""
    for rule in rules:
        hook = NORMALIZER_HOOKS.get(rule)
        if hook is None:
            raise _AdapterFailure(f"unknown normalization rule {rule!r}")
        data = hook(data)
    return data


def decide_diff(case, adapter):
    if adapter is None:
        return _ni("no adapter configured")
    try:
        vm_result, aot_result = _diff_results(case, adapter)
        vm = _observation(vm_result)
        aot = _observation(aot_result)
    except _AdapterNotImplemented as exc:
        return _ni(str(exc))
    except _AdapterFailure as exc:
        return _fail(str(exc))
    compare = case["compare"]
    rules = case.get("normalize", [])
    if "stdout_bytes" in compare:
        try:
            vm_stdout = normalize_stdout(vm["stdout"], rules)
            aot_stdout = normalize_stdout(aot["stdout"], rules)
        except _AdapterFailure as exc:
            return _fail(str(exc))
    else:
        vm_stdout, aot_stdout = vm["stdout"], aot["stdout"]
    problems = []
    for item in compare:
        field = COMPARE_FIELDS[item]
        left = vm_stdout if field == "stdout" else vm[field]
        right = aot_stdout if field == "stdout" else aot[field]
        if left != right:
            if field == "stdout":
                problems.append(
                    f"stdout differs: vm={left.hex()} aot={right.hex()}"
                )
            else:
                problems.append(f"{field} differs: vm={left!r} aot={right!r}")
    if problems:
        return _fail(
            "; ".join(problems),
            vm=_observation_summary(vm), aot=_observation_summary(aot),
            normalize=list(rules),
        )
    return _pass(compared=list(compare), normalize=list(rules))


def _diff_results(case, adapter):
    if _has_action(adapter, "vm") and _has_action(adapter, "aot"):
        return (
            _acquire(case, adapter, "vm"),
            _acquire(case, adapter, "aot"),
        )
    if _has_action(adapter, "diff"):
        combined = _acquire(case, adapter, "diff")
        if not isinstance(combined, dict) or "vm" not in combined or "aot" not in combined:
            raise _AdapterFailure(
                "malformed adapter result: diff must return {vm, aot}"
            )
        return combined["vm"], combined["aot"]
    raise _AdapterFailure(
        "adapter must provide vm and aot actions (or a combined diff action)"
    )


# --------------------------------------------------------------------------
# Unit decider


def decide_unit(case, adapter):
    result, error = _adapter_call(case, adapter, "unit")
    if error:
        return error
    try:
        json.dumps(result, allow_nan=False)
    except (TypeError, ValueError) as exc:
        return _fail(f"malformed adapter result: not JSON-serializable ({exc})")
    difference = _json_difference(case["expect"], result, "$")
    if difference:
        return _fail(f"unit observation differs at {difference}")
    return _pass()


def _json_difference(expected, observed, path):
    if type(expected) is not type(observed):
        return f"{path}: type {type(expected).__name__} != {type(observed).__name__}"
    if isinstance(expected, dict):
        if set(expected) != set(observed):
            missing = sorted(set(expected) - set(observed))
            extra = sorted(set(observed) - set(expected))
            return f"{path}: missing={missing} extra={extra}"
        for key in sorted(expected):
            difference = _json_difference(
                expected[key], observed[key], f"{path}.{key}"
            )
            if difference:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(observed):
            return f"{path}: length {len(expected)} != {len(observed)}"
        for index, (left, right) in enumerate(zip(expected, observed)):
            difference = _json_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    if expected != observed:
        return f"{path}: {expected!r} != {observed!r}"
    return None


# --------------------------------------------------------------------------
# Platform decider


def decide_platform(case, adapter):
    result, error = _adapter_call(case, adapter, "platform")
    if error:
        return error
    if not isinstance(result, dict):
        return _fail("malformed adapter result: expected a mapping")
    try:
        data = _platform_data(result)
        workdir = _workdir(case)
        artifacts = _check_artifacts(case, data, workdir)
        symbols = _check_symbols(case, data)
        problems = []
        expect = case["expect"]
        if "uart_hex" in expect:
            actual_uart = _read_bytes(data, "uart", required=True)
            expected_uart = hex_to_bytes(expect["uart_hex"], "expect.uart_hex")
            if actual_uart != expected_uart:
                problems.append(
                    f"uart expected {expected_uart.hex()}, got {actual_uart.hex()}"
                )
        if "stdout_hex" in expect:
            actual_stdout = _read_bytes(data, "stdout", required=True)
            expected_stdout = hex_to_bytes(expect["stdout_hex"], "expect.stdout_hex")
            if actual_stdout != expected_stdout:
                problems.append(
                    f"stdout expected {expected_stdout.hex()}, "
                    f"got {actual_stdout.hex()}"
                )
        if "trap" not in data:
            raise _AdapterFailure("malformed adapter result: missing trap")
        trap = data["trap"]
        if trap is not None and (not isinstance(trap, str) or trap not in RE_CODES):
            raise _AdapterFailure(
                f"malformed adapter result: invalid trap {trap!r}"
            )
        if trap != expect["trap"]:
            problems.append(f"trap expected {expect['trap']!r}, got {trap!r}")
        if "exit" not in data:
            raise _AdapterFailure("malformed adapter result: missing exit")
        exit_status = data["exit"]
        if isinstance(exit_status, bool) or not isinstance(exit_status, int):
            raise _AdapterFailure(
                "malformed adapter result: exit is not an integer"
            )
        if exit_status != expect["exit"]:
            problems.append(
                f"exit expected {expect['exit']!r}, got {exit_status!r}"
            )
        if "termination" in data and data["termination"] not in TERMINATIONS:
            raise _AdapterFailure(
                f"malformed adapter result: invalid termination "
                f"{data['termination']!r}"
            )
        if "termination" in expect:
            if data.get("termination") != expect["termination"]:
                problems.append(
                    f"termination expected {expect['termination']!r}, "
                    f"got {data.get('termination')!r}"
                )
    except _AdapterFailure as exc:
        return _fail(str(exc))
    if problems:
        return _fail("; ".join(problems), artifacts=artifacts, symbols=symbols)
    return _pass(artifacts=artifacts, symbols=symbols)


def _platform_data(result):
    data = dict(result)
    output = data.pop("output", None)
    if output is None:
        return data
    if not isinstance(output, dict):
        raise _AdapterFailure("malformed adapter result: output is not a mapping")
    for key, value in output.items():
        if key in data:
            raise _AdapterFailure(
                f"malformed adapter result: duplicate output field {key!r}"
            )
        data[key] = value
    return data


def _workdir(case):
    harness = case.get("_harness")
    if not isinstance(harness, dict) or not harness.get("workdir"):
        raise _AdapterFailure("internal error: case has no isolated work directory")
    return harness["workdir"]


def _check_artifacts(case, data, workdir):
    expected = case["expect"].get("artifacts", [])
    actual = data.get("artifacts", [])
    if not isinstance(actual, list):
        raise _AdapterFailure("malformed adapter result: artifacts is not a list")
    paths = []
    for relative in list(expected) + actual:
        if not isinstance(relative, str) or not relative:
            raise _AdapterFailure("malformed artifact path")
        full = _artifact_path(workdir, relative)
        if not os.path.isfile(full):
            raise _AdapterFailure(f"expected artifact does not exist: {relative}")
        paths.append(relative)
    return sorted(set(paths))


def _artifact_path(workdir, relative):
    if os.path.isabs(relative):
        raise _AdapterFailure(f"artifact path must be relative: {relative}")
    full = os.path.normpath(os.path.join(workdir, relative))
    root = os.path.normpath(workdir)
    if os.path.commonpath([root, full]) != root:
        raise _AdapterFailure(f"artifact path escapes work directory: {relative}")
    return full


def _check_symbols(case, data):
    expected = case["expect"].get("symbols")
    if expected is None:
        return []
    actual = data.get("symbols")
    if not isinstance(actual, list) or not all(isinstance(s, str) for s in actual):
        raise _AdapterFailure("malformed adapter result: symbols is not a string list")
    if set(actual) != set(expected):
        raise _AdapterFailure(
            f"symbol set mismatch: expected {sorted(set(expected))}, "
            f"got {sorted(set(actual))}"
        )
    return sorted(set(actual))


# --------------------------------------------------------------------------
# Tool decider


def decide_tool(case, adapter):
    tool = case["tool"]
    repeat = tool.get("repeat", 2)
    runs = []
    for index in range(repeat):
        result, error = _adapter_call(case, adapter, "tool")
        if error:
            if index == 0:
                return error
            return _fail(
                f"tool run {index + 1}/{repeat} was not implemented",
                runs=len(runs),
            )
        try:
            run = _tool_run(result, case)
        except _AdapterFailure as exc:
            return _fail(str(exc))
        if run["exit"] != case["expect"]["exit"]:
            return _fail(
                f"exit expected {case['expect']['exit']!r}, got {run['exit']!r}",
                run=index + 1,
            )
        expected_stdout = case["expect"].get("stdout_hex")
        if expected_stdout is not None:
            expected = hex_to_bytes(expected_stdout, "expect.stdout_hex")
            if run["stdout"] != expected:
                return _fail(
                    f"stdout expected {expected.hex()}, got {run['stdout'].hex()}",
                    run=index + 1,
                )
        expected_stderr = case["expect"].get("stderr_hex")
        if expected_stderr is not None:
            expected = hex_to_bytes(expected_stderr, "expect.stderr_hex")
            if run["stderr"] != expected:
                return _fail(
                    f"stderr expected {expected.hex()}, got {run['stderr'].hex()}",
                    run=index + 1,
                )
        runs.append(run)
    for index in range(1, len(runs)):
        if runs[index] != runs[0]:
            return _fail(
                f"tool output is not reproducible: run {index + 1} differs "
                "from run 1"
            )
    return _pass(runs=len(runs))


def _tool_run(result, case):
    if not isinstance(result, dict):
        raise _AdapterFailure("malformed adapter result: expected a mapping")
    if "exit" not in result:
        raise _AdapterFailure("malformed adapter result: missing exit")
    exit_status = result["exit"]
    if isinstance(exit_status, bool) or not isinstance(exit_status, int):
        raise _AdapterFailure("malformed adapter result: exit is not an integer")
    stdout = _read_bytes(result, "stdout", required=True)
    stderr = _read_bytes(result, "stderr", required=True)
    workdir = _workdir(case)
    artifacts = {}
    actual_artifacts = result.get("artifacts", [])
    if not isinstance(actual_artifacts, list):
        raise _AdapterFailure("malformed adapter result: artifacts is not a list")
    expected_artifacts = case["expect"].get("artifacts", [])
    for relative in list(expected_artifacts) + actual_artifacts:
        if not isinstance(relative, str) or not relative:
            raise _AdapterFailure("malformed artifact path")
        full = _artifact_path(workdir, relative)
        if not os.path.isfile(full):
            raise _AdapterFailure(f"expected artifact does not exist: {relative}")
        artifacts[relative] = _sha256_file(full)
    return {
        "exit": exit_status, "stdout": stdout, "stderr": stderr,
        "artifacts": artifacts,
    }


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------------------
# Property decider


def decide_property(case, adapter):
    result, error = _adapter_call(case, adapter, "property")
    if error:
        return error
    if isinstance(result, dict) and "counterexamples" in result:
        counterexamples = result["counterexamples"]
    elif isinstance(result, list):
        counterexamples = result
    else:
        return _fail(
            "malformed adapter result: expected a counterexample list"
        )
    if not isinstance(counterexamples, list):
        return _fail(
            "malformed adapter result: counterexamples is not a list"
        )
    if counterexamples:
        try:
            sample = json.dumps(
                counterexamples[:3], ensure_ascii=False, allow_nan=False
            )
        except (TypeError, ValueError) as exc:
            return _fail(f"malformed counterexample is not JSON ({exc})")
        return _fail(
            f"property violated by {len(counterexamples)} counterexample(s)",
            sample=sample,
        )
    return _pass()


DECIDERS = {
    "compile": decide_compile,
    "run": decide_run,
    "diff": decide_diff,
    "unit": decide_unit,
    "platform": decide_platform,
    "tool": decide_tool,
    "property": decide_property,
}


def decide(case, adapter):
    """Dispatch to the decider named by the validated case `kind`."""
    kind = case.get("kind")
    function = DECIDERS.get(kind)
    if function is None:
        return _fail(f"unknown decider kind {kind!r}")
    return function(case, adapter)
