# ABI conformance and C interop

This directory checks the published calling convention independently of the
reference VM.  The VM executes calls without an ABI, and a TC-to-TC call uses
the same argument planner on caller and callee, so neither is an independent
ABI oracle.  The C-caller and TC-caller suites are.

## Reference and scope

The AArch64 mapping follows AAPCS64 (LP64) and the ARM32 mapping follows the
base AAPCS32 PCS, as published in the ARM ABI
[2024Q3 release](https://github.com/ARM-software/abi-aa/releases/tag/2024Q3).
The 2021Q1 Rule C.4 clarification for over-aligned HFAs is part of that
reference.

TC has a compact structure layout.  It has no over-aligned types, bit-fields,
vector types or variadic functions; `@align` is a pointer-parameter
precondition, not a type-alignment attribute.  C ABI forms that require those
source constructs are outside the TC ABI mapping and are not claimed as
interoperable.  `TcABIDesc` fields are validated profile metadata; each backend
rejects a profile whose descriptor does not match its fixed classifier limits
(AArch64 16-byte composite threshold, four HFA registers, 16-byte stack
alignment; ARM32 four-byte composite threshold, no HFA register class,
8-byte stack alignment).

## Suites

| Suite | Direction | Cases | Purpose |
| --- | --- | --- | --- |
| `s8_interop_cases.py` | C caller → TC callee (AAPCS64) | 33 | Independent callee-side ABI oracle, including composites, HFA, stack spill, indirect returns and narrow values |
| `s9_arm32_interop.py` | C caller → TC callee (AAPCS32) | 14 | Base AAPCS32 callee oracle, including Rule C.5 splits and by-value composites |
| `reverse_interop_cases.py` | TC caller → C callee (AAPCS64 and AAPCS32) | 19 | Independent caller-side oracle; redirects the TC caller's `bl tc_function_0` to a C callee |

The reverse suite is test infrastructure only.  TC has no extern/FFI
declaration; the suite compiles the TC caller to assembly, makes
`tc_function_1` global, redirects its call target to a C `c_callee`, and lets a
C `tc_main` invoke it.

## Rule matrix

| Area | AArch64 evidence | ARM32 evidence | Notes |
| --- | --- | --- | --- |
| Integer/pointer register arguments | `s8_interop` 2/9-argument, pointer-spill and mixed cases; `reverse` 1/2/3 | `s9_interop` 5-argument; `reverse` 11/12 | AArch64 x0-x7; ARM32 r0-r3 |
| Narrow signed/unsigned values | `s8_interop` extension and narrow returns | `s9_interop` extension; `reverse` 9/18 | Signed values are sign-extended and unsigned values zero-extended to a full word |
| `int64`/`float64` pairs | `s8_interop` int64 cases; `reverse` 3 | `s9_interop`/`reverse` 12 | ARM32 uses even-aligned core pairs |
| Scalar `float32` | `s8_interop` float32 case; `reverse` 4 | `s9_interop`; `reverse` 15 | AArch64 s0; ARM32 one core word under softfp |
| HFA/HVA | `s8_interop` HFA2/HFA3/HFA4 and stack-spill cases; `reverse` 5/8 | `s9_interop`/`reverse` 17 (by-value words, indirect return) | HFA only for 1-4 uniquely addressable float members |
| Composites ≤ threshold | `s8_interop` packed Pair/Small/M cases; `reverse` 7 | `s9_interop` Pair/Small; `reverse` 13/16 | AArch64 x registers; ARM32 Rule C.4/C.5 memory words |
| Composites > threshold | `s8_interop` Big/R cases; `reverse` 6/10 | `s9_interop` Big; `reverse` 14/19 | AArch64 caller copy + pointer; ARM32 by-value words |
| Indirect returns | `s8_interop` 17-byte/72-byte; `reverse` 10 | `s9_interop` Big; `reverse` 19 | x8 for AArch64; hidden r0 for ARM32 |
| Stack alignment | HFA2 spill after v0-v7 and x-register exhaustion | Partial Rule C.5 split; stack words | AArch64 sp 16-byte; ARM32 sp 8-byte |
| Callee-saved registers | Frame gets/sets in `s8_interop` and `reverse` | Frame gets/sets in `s9_interop`/`reverse` | C caller/callee exposes leaks |

## Explicit gaps

- Over-aligned HFA/composite: TC source cannot express type over-alignment, so
  there is no TC-equivalent program to compare.  Recorded as an ABI scope
  limit, not a missing test.
- Bit-field composites: TC has no bit-field type; C layout is not a
  language-neutral contract.
- Variadic calls: TC has no variadic function type.
- Vector/HVA register classes: TC has no vector type.

## Running

```sh
python3 tests/conformance/abi/s8_interop_cases.py --check
python3 tests/conformance/abi/s9_arm32_interop.py --check
python3 tests/conformance/abi/reverse_interop_cases.py --check
```
