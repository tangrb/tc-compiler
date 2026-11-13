"""Case metadata schema for the TC 0.0.46 S0 harness.

The accepted shape is the Appendix C YAML subset plus the extensions listed in
the S0 plan: kind inference, tool/property/unit cases, normalization rules,
timeouts, device/link/UART descriptors and run/link options.  Validation is
strict: unknown fields and missing required fields are errors, never skips.
"""

import math
import os

try:
    from . import yaml_subset
    from .diag_codes import (
        COMPARE_ITEMS, DEVICE_POLICIES, NORMALIZE_RULES, RE_CODES,
        STATIC_STAGES, TERMINATIONS, expected_stage_matches,
    )
except ImportError:  # script execution: python3 tests/harness/runner.py
    import yaml_subset
    from diag_codes import (
        COMPARE_ITEMS, DEVICE_POLICIES, NORMALIZE_RULES, RE_CODES,
        STATIC_STAGES, TERMINATIONS, expected_stage_matches,
    )

KINDS = ("compile", "run", "diff", "unit", "platform", "tool", "property")

TOP_LEVEL_FIELDS = frozenset({
    "id", "profiles", "profile", "source", "include_roots", "spec",
    "timeout_s", "kind", "expect", "run", "compare", "normalize", "oracle",
    "link", "uart", "device", "checked", "opt", "tool", "unit", "property",
})

FIELD_KINDS = {
    "run": {"run"},
    "compare": {"diff"},
    "normalize": {"diff"},
    "oracle": {"diff"},
    "link": {"platform"},
    "uart": {"platform"},
    "device": {"platform"},
    "tool": {"tool"},
    "unit": {"unit"},
    "property": {"property"},
}

HEX_CHARS = frozenset("0123456789abcdefABCDEF")


class SchemaError(ValueError):
    """Raised for malformed case metadata or illegal case values."""

    def __init__(self, message, path="case"):
        self.message = message
        self.path = path
        super().__init__(f"{path}: {message}")


def parse_case_file(path):
    """Read one YAML file and return a validated case dict."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as exc:
        raise SchemaError(f"cannot read case file: {exc}") from exc
    try:
        data = yaml_subset.parse(text)
    except yaml_subset.YamlError as exc:
        raise SchemaError(f"YAML error: {exc}") from exc
    return validate_case(data, origin=str(path))


def validate_case(data, origin=None):
    """Validate a parsed mapping and return it with an inferred `kind`."""
    if not isinstance(data, dict):
        raise SchemaError("top level must be a mapping")
    unknown = sorted(set(data) - TOP_LEVEL_FIELDS)
    if unknown:
        raise SchemaError(f"unknown field(s): {', '.join(unknown)}")
    for field in ("id", "spec", "expect"):
        if field not in data:
            raise SchemaError(f"missing required field {field!r}", field)
    _string(data["id"], "id")
    _string(data["spec"], "spec")
    _validate_profiles(data)
    if "source" in data:
        _string(data["source"], "source")
    if "include_roots" in data:
        _string_list(data["include_roots"], "include_roots")
    if "timeout_s" in data:
        value = data["timeout_s"]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise SchemaError("must be a number", "timeout_s")
        if not math.isfinite(value) or value <= 0:
            raise SchemaError("must be a positive finite number", "timeout_s")
    if "checked" in data and not isinstance(data["checked"], bool):
        raise SchemaError("must be a boolean", "checked")
    if "opt" in data:
        value = data["opt"]
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            raise SchemaError("must be a non-empty string or integer", "opt")
        if isinstance(value, str) and not value.strip():
            raise SchemaError("must be non-empty", "opt")
    if "device" in data:
        data["device"] = _device(data["device"])

    kind = _infer_kind(data)
    _validate_cross_fields(data, kind)
    _require_source(data, kind)
    _validate_expect_container(data["expect"], kind)

    if kind == "compile":
        _validate_compile(data)
    elif kind == "run":
        _validate_run(data)
    elif kind == "diff":
        _validate_diff(data)
    elif kind == "unit":
        _validate_unit(data)
    elif kind == "platform":
        _validate_platform(data)
    elif kind == "tool":
        _validate_tool(data)
    else:
        _validate_property(data)

    result = dict(data)
    result["kind"] = kind
    if origin:
        result["_origin"] = origin
    return result


def _infer_kind(data):
    if "kind" in data:
        kind = data["kind"]
        if kind not in KINDS:
            raise SchemaError(
                f"must be one of {', '.join(KINDS)}", "kind"
            )
        return kind
    if "tool" in data:
        return "tool"
    if "property" in data:
        return "property"
    if "unit" in data:
        return "unit"
    expect = data.get("expect")
    if isinstance(expect, dict):
        if "diff" in expect or "compare" in data:
            return "diff"
        if any(key in data for key in ("link", "uart", "device")):
            return "platform"
        if set(expect) & {"stage", "code", "primary", "diagnostics", "related"}:
            return "compile"
        if "run" in data or set(expect) & {
            "stdout_hex", "trap", "exit", "termination"
        }:
            return "run"
    raise SchemaError(
        "cannot infer decider kind; add kind: compile|run|diff|unit|"
        "platform|tool|property"
    )


def _validate_cross_fields(data, kind):
    for field, allowed in FIELD_KINDS.items():
        if field in data and kind not in allowed:
            raise SchemaError(
                f"field {field!r} is only valid for kind "
                f"{'/'.join(sorted(allowed))} (case kind is {kind})", field
            )
    expect = data["expect"]
    if isinstance(expect, dict) and "diff" in expect and kind != "diff":
        raise SchemaError("only diff cases may use expect.diff", "expect.diff")


def _require_source(data, kind):
    if kind in {"compile", "run", "diff", "platform"} and "source" not in data:
        raise SchemaError(f"missing required field 'source' for {kind} case")
    if "source" in data:
        _string(data["source"], "source")


def _validate_profiles(data):
    if "profiles" in data and "profile" in data:
        raise SchemaError("use exactly one of profiles/profile")
    if "profiles" in data:
        values = data["profiles"]
        _string_list(values, "profiles")
        if not values:
            raise SchemaError("must contain at least one profile", "profiles")
    elif "profile" in data:
        _string(data["profile"], "profile")
    else:
        raise SchemaError("missing required field 'profiles' or 'profile'")


def _validate_expect_container(expect, kind):
    if kind == "unit":
        _check_json(expect, "expect")
        return
    if not isinstance(expect, dict):
        raise SchemaError("must be a mapping", "expect")


def _validate_compile(data):
    expect = data["expect"]
    allowed = {"stage", "code", "primary", "related", "diagnostics"}
    _reject_unknown(expect, allowed, "expect")
    for field in sorted(allowed):
        if field not in expect:
            raise SchemaError(f"missing required field {field!r}", f"expect.{field}")
    stage = _string(expect["stage"], "expect.stage")
    if stage not in STATIC_STAGES:
        raise SchemaError(
            f"static stage must be one of {', '.join(sorted(STATIC_STAGES))}",
            "expect.stage",
        )
    code = _string(expect["code"], "expect.code")
    if not expected_stage_matches(code, stage):
        raise SchemaError(
            f"code {code} cannot be reported in stage {stage} "
            "(see standard section 12.2/12.3)",
            "expect.stage",
        )
    _position(expect["primary"], "expect.primary")
    if not isinstance(expect["related"], list):
        raise SchemaError("must be a list", "expect.related")
    for index, item in enumerate(expect["related"]):
        _position(item, f"expect.related[{index}]")
    if expect["diagnostics"] != "exactly-one":
        raise SchemaError(
            "only 'exactly-one' is valid for static diagnostics",
            "expect.diagnostics",
        )


def _validate_run(data):
    if "run" in data:
        _validate_run_block(data["run"])
    expect = data["expect"]
    allowed = {"stdout_hex", "trap", "exit", "termination"}
    _reject_unknown(expect, allowed, "expect")
    for field in sorted(allowed):
        if field not in expect:
            raise SchemaError(f"missing required field {field!r}", f"expect.{field}")
    hex_to_bytes(expect["stdout_hex"], "expect.stdout_hex")
    _trap(expect["trap"], "expect.trap")
    _exit(expect["exit"], "expect.exit")
    _termination(expect["termination"], "expect.termination")


def _validate_run_block(run):
    if not isinstance(run, dict):
        raise SchemaError("must be a mapping", "run")
    _reject_unknown(run, {"stdin_hex"}, "run")
    if "stdin_hex" in run:
        hex_to_bytes(run["stdin_hex"], "run.stdin_hex")


def _validate_diff(data):
    expect = data["expect"]
    _reject_unknown(expect, {"diff"}, "expect")
    if "diff" not in expect:
        raise SchemaError("missing required field 'diff'", "expect.diff")
    if expect["diff"] != "identical":
        raise SchemaError("only 'identical' is valid", "expect.diff")
    if "compare" not in data:
        raise SchemaError("missing required field 'compare'")
    compare = _string_list(data["compare"], "compare")
    if not compare:
        raise SchemaError("must contain at least one item", "compare")
    for index, item in enumerate(compare):
        if item not in COMPARE_ITEMS:
            raise SchemaError(
                f"unknown compare item {item!r}; expected one of "
                f"{', '.join(sorted(COMPARE_ITEMS))}", f"compare[{index}]"
            )
    if len(set(compare)) != len(compare):
        raise SchemaError("duplicate compare item", "compare")
    if "normalize" in data:
        rules = _string_list(data["normalize"], "normalize")
        for index, rule in enumerate(rules):
            if rule not in NORMALIZE_RULES:
                raise SchemaError(
                    f"unknown normalization rule {rule!r}", f"normalize[{index}]"
                )
        if len(set(rules)) != len(rules):
            raise SchemaError("duplicate normalization rule", "normalize")
    if "oracle" in data:
        oracle = _string(data["oracle"], "oracle")
        if oracle not in {"vm", "aot"}:
            raise SchemaError("must be 'vm' or 'aot'", "oracle")


def _validate_unit(data):
    if "unit" in data and not isinstance(data["unit"], dict):
        raise SchemaError("must be a mapping passed to the adapter", "unit")
    _check_json(data["expect"], "expect")


def _validate_platform(data):
    if not any(field in data for field in ("link", "uart", "device")):
        raise SchemaError(
            "platform case must declare at least one of link/uart/device"
        )
    for field in ("link", "uart"):
        if field in data:
            _string(data[field], field)
    expect = data["expect"]
    allowed = {
        "uart_hex", "stdout_hex", "artifacts", "symbols",
        "trap", "exit", "termination",
    }
    _reject_unknown(expect, allowed, "expect")
    for field in ("trap", "exit"):
        if field not in expect:
            raise SchemaError(f"missing required field {field!r}", f"expect.{field}")
    _trap(expect["trap"], "expect.trap")
    _exit(expect["exit"], "expect.exit")
    if "termination" in expect:
        _termination(expect["termination"], "expect.termination")
    if "uart_hex" in expect:
        hex_to_bytes(expect["uart_hex"], "expect.uart_hex")
    if "stdout_hex" in expect:
        hex_to_bytes(expect["stdout_hex"], "expect.stdout_hex")
    if "artifacts" in expect:
        _string_list(expect["artifacts"], "expect.artifacts")
    if "symbols" in expect:
        _string_list(expect["symbols"], "expect.symbols")
    if not set(expect) & {"uart_hex", "stdout_hex", "artifacts", "symbols"}:
        raise SchemaError(
            "platform case needs an output, artifact or symbol expectation"
        )


def _validate_tool(data):
    if "tool" not in data:
        raise SchemaError("missing required field 'tool' for tool case")
    tool = data["tool"]
    if not isinstance(tool, dict):
        raise SchemaError("must be a mapping", "tool")
    _reject_unknown(tool, {"argv", "stdin_hex", "repeat"}, "tool")
    if "argv" not in tool:
        raise SchemaError("missing required field 'argv'", "tool.argv")
    argv = _string_list(tool["argv"], "tool.argv")
    if not argv:
        raise SchemaError("must contain at least one argument", "tool.argv")
    if "stdin_hex" in tool:
        hex_to_bytes(tool["stdin_hex"], "tool.stdin_hex")
    repeat = tool.get("repeat", 2)
    if isinstance(repeat, bool) or not isinstance(repeat, int) or repeat < 2:
        raise SchemaError(
            "must be an integer >= 2 (reproducibility requires two runs)",
            "tool.repeat",
        )
    expect = data["expect"]
    allowed = {"exit", "stdout_hex", "stderr_hex", "artifacts"}
    _reject_unknown(expect, allowed, "expect")
    if "exit" not in expect:
        raise SchemaError("missing required field 'exit'", "expect.exit")
    _exit(expect["exit"], "expect.exit")
    for field in ("stdout_hex", "stderr_hex"):
        if field in expect:
            hex_to_bytes(expect[field], f"expect.{field}")
    if "artifacts" in expect:
        _string_list(expect["artifacts"], "expect.artifacts")


def _validate_property(data):
    if "property" in data:
        prop = data["property"]
        if not isinstance(prop, dict):
            raise SchemaError("must be a mapping", "property")
        _reject_unknown(prop, {"name", "trials", "seed"}, "property")
        if "name" in prop:
            _string(prop["name"], "property.name")
        if "trials" in prop:
            trials = prop["trials"]
            if isinstance(trials, bool) or not isinstance(trials, int) or trials <= 0:
                raise SchemaError("must be a positive integer", "property.trials")
        if "seed" in prop and (
            isinstance(prop["seed"], bool)
            or not isinstance(prop["seed"], (int, str))
        ):
            raise SchemaError("must be an integer or string", "property.seed")
    expect = data["expect"]
    allowed = {"counterexamples"}
    _reject_unknown(expect, allowed, "expect")
    if expect.get("counterexamples", "empty") != "empty":
        raise SchemaError("only 'empty' is valid", "expect.counterexamples")


def hex_to_bytes(value, path="hex"):
    """Parse a metadata hex string: optional whitespace between byte pairs.

    The empty string denotes zero bytes and is valid for stdin/stdout_hex.
    """
    if not isinstance(value, str):
        raise SchemaError("must be a hex string", path)
    cleaned = "".join(value.split())
    if cleaned == "":
        return b""
    if len(cleaned) % 2:
        raise SchemaError("hex string must contain whole bytes", path)
    if any(char not in HEX_CHARS for char in cleaned):
        raise SchemaError("contains non-hexadecimal characters", path)
    return bytes.fromhex(cleaned)


def _reject_unknown(mapping, allowed, path):
    unknown = sorted(set(mapping) - allowed)
    if unknown:
        raise SchemaError(
            f"unknown field(s): {', '.join(unknown)}", path
        )


def _string(value, path):
    if not isinstance(value, str) or not value.strip():
        raise SchemaError("must be a non-empty string", path)
    return value


def _string_list(value, path):
    if not isinstance(value, list):
        raise SchemaError("must be a list", path)
    for index, item in enumerate(value):
        _string(item, f"{path}[{index}]")
    return value


def _position(value, path):
    if not isinstance(value, dict):
        raise SchemaError("must be a mapping with file/line/column", path)
    _reject_unknown(value, {"file", "line", "column"}, path)
    for field in ("file", "line", "column"):
        if field not in value:
            raise SchemaError(f"missing required field {field!r}", f"{path}.{field}")
    _string(value["file"], f"{path}.file")
    for field in ("line", "column"):
        number = value[field]
        if isinstance(number, bool) or not isinstance(number, int) or number < 1:
            raise SchemaError("must be a 1-based integer", f"{path}.{field}")


def _exit(value, path):
    if isinstance(value, bool) or not isinstance(value, int):
        raise SchemaError("must be an integer exit status", path)


def _trap(value, path):
    if value is None:
        return
    if not isinstance(value, str) or value not in RE_CODES:
        raise SchemaError(
            "must be null or one of the 15 TC_RE_* runtime trap codes", path
        )


def _termination(value, path):
    if value not in TERMINATIONS:
        raise SchemaError(
            f"must be one of {', '.join(sorted(TERMINATIONS))}", path
        )


def _device(value):
    if not isinstance(value, str):
        raise SchemaError("must be a device policy string", "device")
    text = value.strip()
    if text.startswith("TC_DEV_"):
        text = text[len("TC_DEV_"):].lower()
    else:
        text = text.lower()
    if text not in DEVICE_POLICIES:
        raise SchemaError(
            f"must be one of {', '.join(sorted(DEVICE_POLICIES))}", "device"
        )
    return text


def _check_json(value, path):
    if value is None or isinstance(value, (bool, int, str)):
        return
    if isinstance(value, float):
        if not math.isfinite(value):
            raise SchemaError("non-finite numbers are not JSON", path)
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            _check_json(item, f"{path}[{index}]")
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if not isinstance(key, str):
                raise SchemaError("JSON object keys must be strings", path)
            _check_json(item, f"{path}.{key}")
        return
    raise SchemaError(f"not JSON-serializable: {type(value).__name__}", path)


def resolve_relative(value, case_dir, suite_root):
    """Resolve a case path against the case directory, then the suite root."""
    if os.path.isabs(value):
        return os.path.normpath(value)
    first = os.path.normpath(os.path.join(case_dir, value))
    if os.path.exists(first) or not suite_root:
        return first
    second = os.path.normpath(os.path.join(suite_root, value))
    return second if os.path.exists(second) else first
