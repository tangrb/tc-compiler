# Security Policy

## Supported versions

| Version | Supported |
| ------- | --------- |
| `v0.0.42` (latest release) | Yes |
| `master` | Yes (development tip) |
| Older tags (`v0.0.41` and below) | No — please upgrade |

Security fixes are applied to `master` and included in the next patch or minor release as appropriate.

## Reporting a vulnerability

**Do not open a public GitHub Issue for security vulnerabilities.**

Please report privately by email to:

**yanhuang8923@qq.com**

Include as much of the following as possible:

- Affected version (`tc-vm --version` / tag / commit)
- OS and toolchain (compiler, CMake)
- Minimal reproduction (preferably a short `.tc` and exact command line)
- Impact assessment (crash, incorrect codegen, sandbox escape in embed hosts, etc.)
- Whether the issue appears in TC-VM, TC-AOT, libtc, and/or TC-Embed

### What to expect

1. **Acknowledgement** within **7 days** of a clear report.
2. An initial severity and scope assessment.
3. A fix or mitigation plan; coordinated disclosure timing agreed with the reporter when practical.
4. Credit in release notes / `CHANGELOG.md` if desired (opt-out available).

## Scope

In scope examples:

- Memory safety issues in the compiler, VM, AOT runtime, or embed API
- Incorrect code generation that could cause undefined behavior when executed
- Denial-of-service that is trivial to trigger from untrusted TC source (when that source is intended to be sandboxed by the host)

Out of scope examples (please use a normal Issue or discussion instead):

- Feature requests and language design proposals
- Documented diagnostics / intentional fail-fast behavior
- Issues that require an already-compromised host process

## Prefer private channels

Public Issues, PRs, and Discussions must not include exploit details for unfixed vulnerabilities. After a fix is released, a public advisory or changelog entry may summarize the issue at a high level.
