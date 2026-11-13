"""Runner and execution control for the TC 0.0.46 S0 harness.

The runner discovers YAML cases, validates them, and executes one decider per
case in a forked worker.  The worker runs with a fixed environment and an
isolated temporary working directory; the parent enforces a hard timeout and
kills the worker on expiry.  Any crash, exception, timeout or malformed
adapter result is a failure.
"""

import argparse
import copy
import fnmatch
import importlib
import importlib.util
import json
import multiprocessing
import os
import shutil
import sys
import tempfile
import time
import traceback

try:
    from . import case_schema, deciders
    from .deciders import DecisionRecord, STATUSES
except ImportError:  # script execution
    import case_schema
    import deciders
    from deciders import DecisionRecord, STATUSES

DEFAULT_TIMEOUT_S = 10.0
CAPTURE_LIMIT = 8192


# --------------------------------------------------------------------------
# Public execution API


def execute_case(case, adapter, default_timeout=DEFAULT_TIMEOUT_S,
                 suite_root=None, case_file=None):
    """Execute one validated case and return a JSON-able report dict."""
    timeout = float(case.get("timeout_s", default_timeout))
    workdir = tempfile.mkdtemp(prefix="tc-harness-")
    try:
        prepared = _prepare_case(case, workdir, suite_root, case_file, timeout)
        if adapter is None:
            decision = DecisionRecord(
                "not_implemented", {"reason": "no adapter configured"}
            )
            return _report(case, decision)
        payload = _execute_in_worker(prepared, adapter, timeout)
        return _report(case, _payload_decision(payload))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def _report(case, decision):
    return {
        "id": case["id"],
        "kind": case["kind"],
        "status": decision.status,
        "details": decision.details,
    }


def _payload_decision(payload):
    if not isinstance(payload, dict):
        return DecisionRecord("fail", {"reason": "worker returned no result"})
    if "exception" in payload:
        return DecisionRecord(
            "fail",
            {
                "reason": f"worker crashed: {payload['exception']}",
                "traceback": payload.get("traceback", ""),
            },
        )
    raw = payload.get("decision")
    if not isinstance(raw, dict) or raw.get("status") not in STATUSES:
        return DecisionRecord(
            "fail", {"reason": "worker returned a malformed decision record"}
        )
    details = raw.get("details")
    if not isinstance(details, dict):
        return DecisionRecord(
            "fail", {"reason": "worker returned malformed decision details"}
        )
    if payload.get("captured_stdout"):
        details["captured_stdout"] = payload["captured_stdout"]
    if payload.get("captured_stderr"):
        details["captured_stderr"] = payload["captured_stderr"]
    return DecisionRecord(raw["status"], details)


def _prepare_case(case, workdir, suite_root, case_file, timeout):
    prepared = copy.deepcopy(case)
    origin = case.get("_origin")
    case_dir = os.path.dirname(origin) if origin else (suite_root or os.getcwd())
    harness = {
        "suite_root": suite_root,
        "case_file": case_file or origin,
        "workdir": workdir,
        "timeout_s": timeout,
    }
    if "source" in case:
        harness["source_path"] = case_schema.resolve_relative(
            case["source"], case_dir, suite_root
        )
    if case.get("include_roots"):
        harness["include_roots"] = [
            case_schema.resolve_relative(root, case_dir, suite_root)
            for root in case["include_roots"]
        ]
    if "link" in case:
        harness["link_path"] = case_schema.resolve_relative(
            case["link"], case_dir, suite_root
        )
    if "run" in case:
        harness["stdin_hex"] = case["run"].get("stdin_hex", "")
    if "tool" in case:
        harness["stdin_hex"] = case["tool"].get("stdin_hex", "")
    prepared["_harness"] = harness
    return prepared


def _execute_in_worker(prepared, adapter, timeout):
    context = _fork_context()
    parent_conn, child_conn = context.Pipe(duplex=False)
    process = context.Process(
        target=_worker_main, args=(child_conn, prepared, adapter)
    )
    process.start()
    child_conn.close()
    deadline = time.monotonic() + timeout
    try:
        remaining = max(0.0, deadline - time.monotonic())
        if not parent_conn.poll(remaining):
            _terminate(process)
            return {
                "decision": {
                    "status": "fail",
                    "details": {
                        "reason": f"case timed out after {timeout:g}s",
                        "timeout_s": timeout,
                    },
                }
            }
        try:
            payload = parent_conn.recv()
        except EOFError:
            _terminate(process)
            return {
                "decision": {
                    "status": "fail",
                    "details": {"reason": _crash_reason(process)},
                }
            }
        process.join(0.5)
        if process.is_alive():
            _terminate(process)
        return payload
    finally:
        parent_conn.close()
        if process.is_alive():
            _terminate(process)


def _fork_context():
    try:
        return multiprocessing.get_context("fork")
    except ValueError as exc:
        raise RuntimeError(
            "hard per-case timeout requires a platform with fork()"
        ) from exc


def _terminate(process):
    if process.is_alive():
        process.terminate()
        process.join(1.0)
    if process.is_alive() and hasattr(process, "kill"):
        process.kill()
        process.join(1.0)


def _crash_reason(process):
    process.join(0.2)
    code = process.exitcode
    if code is None:
        return "worker crashed without a decision record"
    if code < 0:
        return f"worker killed by signal {-code}"
    return f"worker exited with status {code} without a decision record"


def _worker_main(connection, case, adapter):
    workdir = case["_harness"]["workdir"]
    out_path = os.path.join(workdir, ".harness.stdout")
    err_path = os.path.join(workdir, ".harness.stderr")
    inherited_path = os.environ.get("PATH")
    os.chdir(workdir)
    os.environ.clear()
    fixed = {"LC_ALL": "C", "LANG": "C", "TZ": "UTC"}
    if inherited_path:
        fixed["PATH"] = inherited_path
    os.environ.update(fixed)
    payload = {}
    saved_out = saved_err = None
    try:
        saved_out = os.dup(1)
        saved_err = os.dup(2)
        with open(out_path, "wb") as out, open(err_path, "wb") as err:
            os.dup2(out.fileno(), 1)
            os.dup2(err.fileno(), 2)
            try:
                decision = deciders.decide(case, adapter)
                payload["decision"] = decision.to_dict()
            except BaseException as exc:  # adapter SystemExit included
                payload["exception"] = f"{type(exc).__name__}: {exc}"
                payload["traceback"] = traceback.format_exc(limit=8)
            finally:
                sys.stdout.flush()
                sys.stderr.flush()
    except BaseException as exc:
        payload.setdefault("exception", f"{type(exc).__name__}: {exc}")
        payload.setdefault("traceback", traceback.format_exc(limit=8))
    finally:
        if saved_out is not None:
            try:
                os.dup2(saved_out, 1)
            except OSError:
                pass
            os.close(saved_out)
        if saved_err is not None:
            try:
                os.dup2(saved_err, 2)
            except OSError:
                pass
            os.close(saved_err)
        payload["captured_stdout"] = _read_capture(out_path)
        payload["captured_stderr"] = _read_capture(err_path)
    try:
        connection.send(payload)
    except BaseException:
        pass
    finally:
        connection.close()


def _read_capture(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read(CAPTURE_LIMIT + 1)
    except OSError:
        return ""
    truncated = len(data) > CAPTURE_LIMIT
    text = data[:CAPTURE_LIMIT].decode("utf-8", "replace")
    if truncated:
        text += "\n[harness capture truncated]"
    return text


# --------------------------------------------------------------------------
# Discovery, filtering and loading


def discover_cases(inputs):
    """Return [(absolute file, suite_root)] sorted deterministically."""
    found = {}
    for item in inputs:
        path = os.path.abspath(item)
        if os.path.isdir(path):
            root = path
            for directory, dirnames, filenames in os.walk(path):
                dirnames[:] = sorted(
                    name for name in dirnames if not name.startswith(".")
                )
                for filename in sorted(filenames):
                    if filename.startswith("."):
                        continue
                    if not filename.endswith((".yaml", ".yml")):
                        continue
                    file_path = os.path.join(directory, filename)
                    found.setdefault(file_path, root)
        elif os.path.isfile(path):
            found.setdefault(path, os.path.dirname(path))
        else:
            raise FileNotFoundError(f"case path does not exist: {item}")
    return sorted(found.items())


def load_cases(entries):
    cases = []
    errors = []
    for path, root in entries:
        try:
            case = case_schema.parse_case_file(path)
        except case_schema.SchemaError as exc:
            errors.append(f"{os.path.abspath(path)}: {exc}")
            continue
        case["_origin"] = os.path.abspath(path)
        case["_suite_root"] = root
        cases.append(case)
    seen = {}
    for case in cases:
        previous = seen.get(case["id"])
        if previous is not None:
            errors.append(
                f"duplicate case id {case['id']!r}: {previous} and "
                f"{case['_origin']}"
            )
        else:
            seen[case["id"]] = case["_origin"]
    cases.sort(key=lambda item: (item["id"], item["_origin"]))
    return cases, errors


def filter_cases(cases, pattern):
    if not pattern:
        return cases
    wildcard = any(char in pattern for char in "*?[")
    selected = []
    for case in cases:
        if wildcard:
            match = fnmatch.fnmatchcase(case["id"], pattern)
        else:
            match = pattern in case["id"] or pattern in case["_origin"]
        if match:
            selected.append(case)
    return selected


def load_adapter(spec):
    if spec is None:
        return None
    if ":" in spec and not os.path.exists(spec):
        module_part, attribute = spec.rsplit(":", 1)
        module = _load_adapter_target(module_part)
        try:
            return getattr(module, attribute)
        except AttributeError as exc:
            raise RuntimeError(
                f"adapter module has no attribute {attribute!r}"
            ) from exc
    return _load_adapter_target(spec)


def _load_adapter_target(target):
    if os.path.isfile(target):
        path = os.path.abspath(target)
        adapter_dir = os.path.dirname(path)
        if adapter_dir not in sys.path:
            sys.path.insert(0, adapter_dir)
        name = "tc_harness_adapter"
        module_spec = importlib.util.spec_from_file_location(name, path)
        if module_spec is None or module_spec.loader is None:
            raise RuntimeError(f"cannot load adapter file {target}")
        module = importlib.util.module_from_spec(module_spec)
        sys.modules[name] = module
        module_spec.loader.exec_module(module)
        return _adapter_object(module)
    try:
        module = importlib.import_module(target)
    except Exception as exc:
        raise RuntimeError(f"cannot import adapter {target!r}: {exc}") from exc
    return _adapter_object(module)


def _adapter_object(module):
    for name in ("ADAPTER", "adapter"):
        if hasattr(module, name):
            return getattr(module, name)
    return module


# --------------------------------------------------------------------------
# CLI


def build_parser():
    parser = argparse.ArgumentParser(
        prog="tc-harness",
        description="TC 0.0.46 S0 metadata harness (standard library only)",
    )
    parser.add_argument(
        "paths", nargs="+",
        help="YAML case file(s) or directories searched recursively",
    )
    parser.add_argument(
        "--adapter",
        help="adapter file.py, module, or module:attribute "
             "(callable or object); omit to mark all cases not-implemented",
    )
    parser.add_argument(
        "--filter", default=None,
        help="case-id substring, or an fnmatch pattern containing * ? [",
    )
    parser.add_argument(
        "--list", action="store_true", dest="list_cases",
        help="list selected cases and exit without running them",
    )
    parser.add_argument(
        "--json-out", default=None,
        help="write a deterministic JSON report to this path",
    )
    parser.add_argument(
        "--allow-not-implemented", action="store_true",
        help="exit 0 when only not_implemented cases remain (still not pass)",
    )
    parser.add_argument(
        "--timeout-s", type=float, default=DEFAULT_TIMEOUT_S,
        help=f"default hard timeout per case (default {DEFAULT_TIMEOUT_S:g})",
    )
    return parser


def _validate_case_paths(cases):
    """Validate source/include/link paths for selected cases before execution.

    Paths are resolved against the suite root (the directory passed to the
    runner, or the case file's directory for an explicit file).  This keeps a
    missing fixture from silently becoming a not-implemented/pass case.
    """
    errors = []
    for case in cases:
        root = os.path.abspath(case["_suite_root"])
        case_dir = os.path.dirname(case.get("_origin") or root)
        checks = []
        if "source" in case:
            checks.append(("source", case["source"]))
        for index, value in enumerate(case.get("include_roots", [])):
            checks.append((f"include_roots[{index}]", value))
        if "link" in case:
            checks.append(("link", case["link"]))
        for label, value in checks:
            if not isinstance(value, str) or not value:
                errors.append(f"{case['id']}: {label} is empty")
                continue
            path = case_schema.resolve_relative(value, case_dir, root)
            if not os.path.exists(path):
                errors.append(f"{case['id']}: {label} does not exist: {value}")
    return errors


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.timeout_s <= 0:
        parser.error("--timeout-s must be positive")
    try:
        entries = discover_cases(args.paths)
    except FileNotFoundError as exc:
        print(f"tc-harness: {exc}", file=sys.stderr)
        return 2
    cases, errors = load_cases(entries)
    if errors:
        for error in errors:
            print(f"SCHEMA-ERROR {error}", file=sys.stderr)
        _write_report(
            args.json_out,
            {"errors": errors, "cases": [], "summary": _summary([]),
             "exit_code": 2},
        )
        return 2
    selected = filter_cases(cases, args.filter)
    if args.filter and not selected:
        print(
            f"tc-harness: filter {args.filter!r} matched no cases",
            file=sys.stderr,
        )
        return 2
    if not selected:
        # Fail closed: an empty suite must not look like a successful run.
        print("tc-harness: no cases selected", file=sys.stderr)
        return 2
    path_errors = _validate_case_paths(selected)
    if path_errors:
        for error in path_errors:
            print(f"PATH-ERROR {error}", file=sys.stderr)
        _write_report(
            args.json_out,
            {"errors": path_errors, "cases": [], "summary": _summary([]),
             "exit_code": 2},
        )
        return 2
    if args.list_cases:
        for case in selected:
            print(f"{case['id']}\t{case['kind']}\t{case['_origin']}")
        return 0
    try:
        adapter = load_adapter(args.adapter)
    except (RuntimeError, OSError) as exc:
        print(f"tc-harness: {exc}", file=sys.stderr)
        _write_report(
            args.json_out,
            {"errors": [str(exc)], "cases": [], "summary": _summary([]),
             "exit_code": 2},
        )
        return 2
    reports = []
    for case in selected:
        try:
            reports.append(
                execute_case(
                    case, adapter,
                    default_timeout=args.timeout_s,
                    suite_root=case["_suite_root"],
                    case_file=case["_origin"],
                )
            )
        except Exception as exc:  # harness failure is still a case failure
            reports.append({
                "id": case["id"], "kind": case["kind"], "status": "fail",
                "details": {
                    "reason": f"harness exception: {type(exc).__name__}: {exc}"
                },
            })
    reports.sort(key=lambda report: report["id"])
    summary = _summary(reports)
    fail_count = summary["fail"]
    ni_count = summary["not_implemented"]
    exit_code = 1 if fail_count or (ni_count and not args.allow_not_implemented) else 0
    for report in reports:
        if report["status"] == "pass":
            print(f"PASS            {report['id']} [{report['kind']}]")
        elif report["status"] == "not_implemented":
            reason = report["details"].get("reason", "")
            print(f"NOT-IMPLEMENTED {report['id']} [{report['kind']}]: {reason}")
        else:
            reason = report["details"].get("reason", "failed")
            print(f"FAIL            {report['id']} [{report['kind']}]: {reason}")
    print(
        "SUMMARY "
        f"pass={summary['pass']} fail={summary['fail']} "
        f"not_implemented={summary['not_implemented']} total={summary['total']}"
        + (" (not-implemented allowed)" if args.allow_not_implemented else "")
    )
    payload = {
        "cases": reports,
        "summary": summary,
        "exit_code": exit_code,
        "allow_not_implemented": bool(args.allow_not_implemented),
    }
    if not _write_report(args.json_out, payload):
        return 2
    return exit_code


def _summary(reports):
    summary = {
        "pass": 0, "fail": 0, "not_implemented": 0, "total": len(reports),
    }
    for report in reports:
        summary[report["status"]] += 1
    return summary


def _write_report(path, payload):
    if not path:
        return True
    try:
        directory = os.path.dirname(os.path.abspath(path))
        if directory and not os.path.isdir(directory):
            raise OSError(f"report directory does not exist: {directory}")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2,
                      sort_keys=True, allow_nan=False)
            handle.write("\n")
    except (OSError, ValueError) as exc:
        print(f"tc-harness: cannot write --json-out: {exc}", file=sys.stderr)
        return False
    return True


if __name__ == "__main__":
    sys.exit(main())
