# Documentation map

This directory contains the **0.0.46** standard, design documents, and evaluation reports (process documents). Chinese: [README.md](README.md).

## Core documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](TC语言及编译器标准-0.0.46.md) | **Sole authority** for 0.0.46 syntax, semantics, diagnostics, compiler requirements, and platform profiles (§0–§14; 80 codes) |
| [TC Native Compiler Design 0.0.46](TC原生编译器设计说明书-0.0.46.md) | **Target architecture** for TC → assembly → machine code: flat TAC IR, semantic runtime, platform profiles, backend, and phasing |

## Status

- Branch: `tc-0.0.46` clean-sheet.
- The 0.0.46 compiler is not implemented; `src/` is a directory skeleton with no `tc-vm`/`tc-aot`.
- No C host public library, embedding API, or ABI.
- Both 32-bit and 64-bit are 0.0.46 targets; `usize`/`isize` are platform-word types. The standard and design have been revised for both 32/64 profiles (§13.2.1/§13.2.2).

## Repository docs

| Document | Purpose |
| -------- | ------- |
| [README.en.md](../README.en.md) ([中文](../README.md)) | Project entry, documents, and skeleton layout |
| [CHANGELOG.en.md](../CHANGELOG.en.md) ([中文](../CHANGELOG.md)) | 0.0.46 clean-sheet changes |
| [CONTRIBUTING.en.md](../CONTRIBUTING.en.md) ([中文](../CONTRIBUTING.md)) | Contribution workflow |
| [CODE_OF_CONDUCT.en.md](../CODE_OF_CONDUCT.en.md) ([中文](../CODE_OF_CONDUCT.md)) | Code of conduct |
| [SECURITY.en.md](../SECURITY.en.md) ([中文](../SECURITY.md)) | Security reporting |
| [AGENTS.md](../AGENTS.md) | Agent guidance |

## History

The historical implementation and old design documents were removed from this branch; recover them from git tags if needed. This branch does not maintain historical compatibility.
