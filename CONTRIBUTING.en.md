# Contributing

Thanks for your interest in TC-Compiler. This guide covers issue reports, pull requests, and local development expectations.

中文：[CONTRIBUTING.md](CONTRIBUTING.md)

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security vulnerabilities **privately** per [SECURITY.md](SECURITY.md)—do not open a public issue.

## Development setup

- C99 compiler, CMake, Make
- Python 3 (structure-check scripts)
- TC-AOT `--run` needs a host `cc` (gcc/clang style; on Windows use MinGW, not MSVC)

```sh
make
./build/vm/bin/tc-vm tests/valid/example.tc
bash scripts/run_tests.sh --filter example
```

Optional git hooks (strips the Cursor Co-authored-by trailer, among other checks):

```sh
make hooks
```

Teaching examples live under [`examples/`](examples/). Full conformance suites are under `tests/`.

## Branches

| Branch | Purpose |
| ------ | ------- |
| `master` | Main development line |
| `tc-0.0.xx` | Version closeout / release branches (aligned with CI triggers) |

Open short-lived branches from current `master` (or a maintainer-designated version branch), e.g. `fix/…`, `feat/…`, `docs/…`.

## Issues

Search existing issues and docs first. Bug reports should include:

- `tc-vm --version` (or commit / tag)
- OS and compiler
- Minimal reproducing `.tc` and full command line
- Expected vs actual stdout/stderr
- Whether the bug is VM-only, AOT-only, or both differ

Language/semantics changes must follow the [TC Language Specification](docs/TC语言标准设计说明书-0.0.42.md); the spec is authoritative over the implementation.

Use the repository [issue templates](.github/ISSUE_TEMPLATE/).

## Pull requests

1. Fork and create a branch.
2. Keep the diff minimal; avoid unrelated reformatting.
3. Run the relevant tests below.
4. For user-visible changes, update `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).
5. Fill out the [PR template](.github/PULL_REQUEST_TEMPLATE.md).
6. Commit style: `feat:` / `fix:` / `refactor:` / `docs:` / `test:` / `chore:` plus a short “why”.
7. **Do not** add `Co-authored-by: Cursor <cursoragent@cursor.com>` (hooks strip it; do not bypass with `--no-verify`).

This project **does not require a CLA**. Contributions are licensed under the [Apache License 2.0](LICENSE).

## Required tests and hard rules

```sh
bash scripts/run_tests.sh                # VM + AOT + unit
bash scripts/run_tests.sh --filter <name>
make test-unit                           # when changing tests/unit
make ci                                  # local entry aligned with core remote gates
```

| Change | Extra requirement |
| ------ | ----------------- |
| New `.tc` cases | Register in `scripts/vm/run_tests.sh` (and `scripts/aot/run_tests.sh` when AOT-relevant) |
| New `TcRhsKind` | `python3 scripts/sync/check_rhs_coverage.py` |
| New `TcErrorKind` | `tc_error_kind_name()` + unit test + compiler spec §11.4 |
| New `src/` module | `python3 scripts/sync/check_source_naming.py` (`tc_*.h` ↔ `tc_*.c`) |
| Type kernel | `check-types` / `check_type_fact_source.py` |
| Control flow / uninit | `test_cfg` + matching `.tc` |
| Embed | `check-embed` / `check-embed-aot` |
| Static error fixtures | **one error per file** |

See `.cursor/skills/tc-architecture/test-map.md` for maintainer/Agent maps (optional for human contributors).

## Coding standards (summary)

- C99; **4-space indent**, no tabs; `/* … */` comments only (no `//`)
- K&R braces; always brace `if` / `while` / `for`
- Public APIs: `tc_` + snake_case; types: `Tc` + PascalCase; enum values: `TC_` + SCREAMING_SNAKE
- Paired names: `tc_<module>.h` ↔ `tc_<module>.c`
- OOM: return `-1`, `TC_ERR_OUT_OF_MEMORY`, message `"memory allocation failed"`
- Fail-fast; single-slot `TcDiagnostic`; return `0` / `-1`

Match existing `src/` style and `.cursor/rules/coding-standards.mdc`.

## Documentation

- Doc map: [docs/README.md](docs/README.md)
- Changelog: [CHANGELOG.md](CHANGELOG.md)
- Release steps: [docs/release-checklist.md](docs/release-checklist.md)

When changing language semantics, error codes, or public APIs, update the matching `docs/*-0.0.42.md` (or current-version specs).

## Cursor Agent (optional)

Maintainer-oriented Cursor context: [AGENTS.md](AGENTS.md) and [`.cursor/README.md`](.cursor/README.md). **Not required** for human contributions.

## Getting help

- General questions and features: GitHub Issues
- Security: [SECURITY.md](SECURITY.md)
- Maintainer: 唐荣兵 — yanhuang8923@qq.com
