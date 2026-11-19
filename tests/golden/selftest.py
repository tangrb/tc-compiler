#!/usr/bin/env python3
"""Negative/positive self-test for tests/golden/validate.py.

Creates temporary golden trees and verifies that the validator accepts the
real sample and rejects: unknown code, illegal stage, missing source, a
placeholder reviewer, and duplicate keys.  No compiler or network is used.
"""

from __future__ import annotations

import contextlib
import io
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import validate  # noqa: E402


def _run(root: Path) -> tuple[int, str]:
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as err:
        code = validate.main([str(root)])
    return code, err.getvalue()


def _bad_case(root: Path, name: str, transform) -> int:
    case_root = root / (name.replace(".golden.yaml", "") + "-root")
    target = case_root / "common" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    text = (HERE / "common" / "lex_syntax_comment_slash.golden.yaml").read_text(encoding="utf-8")
    target.write_text(transform(text), encoding="utf-8")
    return _run(case_root)[0]


def main() -> int:
    failures = 0

    def check(condition, message):
        nonlocal failures
        if not condition:
            failures += 1
            print(f"FAIL: {message}")

    with tempfile.TemporaryDirectory(prefix="tc-golden-ok-") as tmp:
        root = Path(tmp)
        (root / "common").mkdir()
        shutil.copy2(
            HERE / "common" / "lex_syntax_comment_slash.golden.yaml",
            root / "common" / "sample.golden.yaml",
        )
        check(_run(root)[0] == 0, "real sample must pass")
        wrong_profile = root / "w64"
        wrong_profile.mkdir()
        (wrong_profile / "profile.golden.yaml").write_text(
            (HERE / "common" / "lex_syntax_comment_slash.golden.yaml").read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        check(_run(root)[0] == 1, "profile/directory mismatch must fail")

    with tempfile.TemporaryDirectory(prefix="tc-golden-bad-") as tmp:
        root = Path(tmp)
        (root / "common").mkdir()
        check(
            _bad_case(root, "badcode.golden.yaml",
                      lambda t: t.replace("code: TC_CE_SYNTAX", "code: TC_CE_NOT_A_CODE")) == 1,
            "unknown code must fail",
        )
        check(
            _bad_case(root, "badstage.golden.yaml",
                      lambda t: t.replace("stage: LEX", "stage: NOT_A_STAGE")) == 1,
            "illegal stage must fail",
        )
        check(
            _bad_case(root, "wrongstage.golden.yaml",
                      lambda t: t.replace("stage: LEX", "stage: SEM")) == 1,
            "code/stage mismatch must fail",
        )
        check(
            _bad_case(root, "unknownfield.golden.yaml",
                      lambda t: "unknown_field: 1\n" + t) == 1,
            "unknown top-level field must fail",
        )
        check(
            _bad_case(root, "badreview.golden.yaml",
                      lambda t: t.replace(
                          "gate-auditor (independent read-only subagent)",
                          "placeholder reviewer",
                      )) == 1,
            "placeholder reviewer must fail",
        )
        check(
            _bad_case(root, "duplicate.golden.yaml",
                      lambda t: "id: TC-DUP-0001\n" + t) == 1,
            "duplicate key must fail",
        )
        missing_root = root / "missing-root"
        missing_root.mkdir(parents=True, exist_ok=True)
        missing = missing_root / "missing.golden.yaml"
        missing.write_text(
            "id: TC-MISSING-0001\n"
            "kind: diagnostic\n"
            "profile: common\n"
            "spec: \"§12.3\"\n"
            "derivation: test\n"
            "review:\n"
            "  reviewer: independent reviewer\n"
            "  reviewer_independent: true\n"
            "  status: approved\n"
            "  date: \"2026-11-13\"\n"
            "expect:\n"
            "  stage: LEX\n"
            "  code: TC_CE_SYNTAX\n"
            "  primary:\n"
            "    file: main.tc\n"
            "    line: 1\n"
            "    column: 1\n"
            "  diagnostics: exactly_one\n",
            encoding="utf-8",
        )
        missing_code, missing_err = _run(missing_root)
        check(missing_code == 1, "missing source must fail")
        check("source" in missing_err.lower(), "missing-source error must mention source")

    if failures:
        print(f"golden selftest: {failures} failure(s)")
        return 1
    print("golden selftest: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
