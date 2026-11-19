# Security Policy

This branch is the **0.0.46 clean-sheet documentation/skeleton branch**. It does not contain an executable compiler, VM, AOT runtime, C host public library, or embedding API. Security issues here concern specification/design ambiguities, rules that could lead to unsafe future implementations, and repository/toolchain configuration.

## Reporting

**Do not open a public Issue for exploitable details.**

Report privately by email to:

**yanhuang8923@qq.com**

Please include:

- affected document and section;
- issue type (spec ambiguity, safety issue, toolchain configuration);
- for 32/64 issues, the word size and an example;
- expected rule vs current text;
- possible security impact (future UB, out-of-bounds, privilege bypass, etc.).

## What to expect

1. Acknowledgement within **7 days** of a clear report.
2. Assessment as a specification, design, or repository issue.
3. A revision or mitigation plan; coordinated disclosure when practical.
4. Credit in `CHANGELOG` if desired (opt-out available).

## Scope

**In scope**:

- ambiguities in the specification/design that could lead to undefined behavior, memory unsafety, or safety-model bypass in a future implementation;
- gaps in the safety model (`unsafe`, `@fast`, pointer state, write protection);
- repository configuration (dependencies, build skeleton) issues.

**Out of scope**:

- feature requests and language-design preferences;
- documented fail-fast/diagnostic behavior;
- issues requiring an already-compromised host.

## Public channels

Public Issues, PRs, and Discussions must not include exploit details before a fix.
