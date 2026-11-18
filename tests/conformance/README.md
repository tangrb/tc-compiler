# Static conformance suites

This tree holds the LEX/SYN, IMPORT and SEM static conformance cases and the
positive acceptance/property gates.  Every case is derived from the standard,
never backfilled from compiler output.

- S1 LEX/SYN: `lex/`, `syn/`, `accept/`, `fuzz/` (source table
  `s1_cases.py`).
- S2 module discovery/IMPORT and S3 names/types/conversions/layout:
  `import/`, `sem/`, `accept_s3/` (source table `s2s3_cases.py`).  Import
  fixtures live under `import/fixtures/<case-id>/`; the same fixture tree is
  reused by `tc_sem_test` for module-order/instance checks.
- S4 CFG/DI/returns/calls/safety/annotations: `sem4/`, `accept_s4/` (source
  table `s4_cases.py`).  Each case owns `sem4/fixtures/<case-id>/` or
  `accept_s4/<case-id>/`; `@HERE@`/`@REL@` positions follow the same
  §12.5.1 conventions.  S4 acceptance tool cases use `--sem-only` because they
  assert SEM acceptance only.
- S6 per-opcode static shape: `opcode_static/<case-id>/` (source table
  `s6_static_cases.py`).  Each of the 63 opcode-table items gets one
  missing-operand SEM `TC_CE_OPERAND_COUNT` case at the mnemonic; type, mode,
  safety and result-type negatives remain in the S3/S4 tables.
- S6 VM runtime: `runtime/run/<case-id>/` (source table `runtime/s6_cases.py`).
  Each case owns `root.tc` plus optional imported `lib.tc`; the YAML declares
  profile, stdin, expected stdout bytes, first `TC_RE_*` trap, exit status and
  termination.  Normal/boundary/trap cases cover all 51 opcodes, the 15
  `TC_RE_*` codes, commit semantics, device policy, `D`, I/O tokens and the
  32/64 matrix.
- S5 CT/const/static-var: `ct/`, `accept_s5/` (source table `s5_cases.py`).
  Negative cases pin the six `TC_CE_CONST_*` CT codes, attribution/order,
  stage boundary and static-var initializers; positive cases cover the §3.10
  allowlist, const paths/struct chains, casts/bitcasts/`sizeof`, `make` and
  static initializers.  Positions follow §12.5.1 with the target-declaration
  convention documented in the table.
- S7 differential: `diff/` (`s7_cases.py`, `s7_fuzz.py`).  Cases are executed
  on the reference VM and on an AArch64 ELF built by `tc-aot --emit=elf` under
  qemu-system-aarch64; program stdout, first `TC_RE_*` and exit policy must
  match the derived expectation and each other.
- S7 ABI: `abi/s7_abi_cases.py` covers the AAPCS64 integer subset (x0-x7,
  x0 return, bool/int8/int16 extension, callee-saved bases, 16-byte alignment,
  recursion and the depth limit `D`).
- S7 toolchain: `tools/s7_toolchain_cases.py` checks `.s`/`.o`/ELF artifacts,
  reproducibility, O0/O2 zero pass, platform-instance limits and
  `tc-vm`/`tc-aot` static-result parity; `tools/s7_static_parity.py`
  compares `--static-only` over every static conformance source.
- S8 full differential: `diff/s8_cases.py` runs every W=64 S6 runtime case plus
  the full-type surface (structures, HFA/large composites, many arguments,
  indirect calls, pointers/objects/regions, static bytes, `@fast_math`) on both
  tools; absolute addresses are never printed, only defined relations.
- S8 ABI: `abi/s8_abi_cases.py` covers the full AAPCS64 surface plus an
  assembly-shape audit (v registers for HFAs, x8 indirect returns, 16-byte stack
  steps, `blr`, by-reference large composites); `abi/s8_interop_cases.py` compiles
  a C caller with clang and calls the AArch64 callee for 33 composite/HFA/spill/
  narrow-value cases, checking the published ABI independently of the VM.
  `abi/reverse_interop_cases.py` adds 19 TC-caller → C-callee cases for both
  targets; the reference edition, rule matrix and explicit ABI scope limits are
  in `abi/README.md`.
- S8 generated: `diff/s8_abi_fuzz.py`, `s8_layout_fuzz.py`, `s8_mem_fuzz.py` and
  `s8_fp_fuzz.py` derive expected behavior in Python and run fixed seed sets
  (scalable with `--seeds`); `tools/s8_static_fuzz.py` compares both tools on
  mutated sources.
- S8 checked: `checked/s8_checked_cases.py` separates checked-build UB traps
  (out-of-object, one-past, dangling, uninitialized) from mandatory traps and
  re-runs legal release fixtures under `--checked`.
- S8 consistency: `consistency/s8_consistency_cases.py` backs the standard
  §13.4 eight items with static parity sources and differential cases.
- S8 artifact: `tools/s8_artifact_cases.py` checks ELF entry/type/machine,
  symbols and undefined-symbol freedom, code/static/stack region layout, static
  initializer bytes, `--icf=none` and checked-build emission.
- S9 bare-metal platform: `platform/s9_baremetal_cases.py` builds the
  `aarch64-baremetal` ELF and runs it on qemu-system-aarch64 virt without
  semihosting: PL011 UART0 stdout and framed stdin, UART1 exit record, PSCI
  termination, the four device policies, static initializer bytes, ELF/flat
  artifacts and an ELF/symbol/no-libc/dynamic-dependency audit.
- S9 ARM32: `platform/s9_arm32_cases.py` runs 29 fixed W=32 semantics/ABI cases
  plus the complete S6 runtime and S8 differential corpora as same-profile
  VM↔AOT comparisons on qemu-system-arm MPS2 AN500; it also audits the ARM32
  ELF header/entry/symbols and the absence of dynamic or libc imports.
  `abi/s9_arm32_interop.py` compiles a C caller with clang and exercises 14
  AAPCS32 composite/int64/float64/narrow cases; `tools/s9_static_parity.py` compares
  VM/AOT static results over every source under both bare-metal profiles;
  `platform/s9_profiles.py` pins the §13.2.2 W=32/W=64 differences.
- S10 RISC-V: `platform/s10_riscv32_cases.py` builds `riscv32-baremetal` ELF32
  images and runs them on qemu-system-riscv32 virt: 29 fixed W=32 semantics/ABI
  cases plus the complete S6/S8 corpora as same-profile VM↔AOT comparisons,
  ELF/symbol/no-dynamic/no-libc audits, the four device policies, checked/UB
  separation, UART framed stdin and flat artifacts.  `abi/s10_riscv32_interop.py`
  adds 32 C-caller ILP32 cases (including the one-register 2×XLEN split) and
  `abi/s10_reverse_interop.py` adds 14 TC-caller → C-callee cases;
  `tools/s10_static_parity.py` compares VM/AOT first diagnostics over every
  static source under the RISC-V profile.  `longterm/s10_longterm.py` is the
  long-term corpus: sampled standard/boundary sources, differential cases,
  fuzz regression seeds and a platform program on all three targets.
  `abi/s10_abi_fuzz.py` generates random scalar/composite signatures for all
  three AOT targets (C→TC and TC→C); `platform/s10_limits_cases.py` covers
  frame/static-region budgets, deep recursion, device-window end, empty input
  and long trap-record paths; `tools/s8_static_fuzz.py --profile=riscv32-baremetal`
  checks 240 mutated sources;
  `tools/s10_frontend_scale.py` emits an 800-case switch, above the lexer's
  32768-token growth step; `checked/s10_checked_cases.py` reruns the 39
  checked-build UB fixtures on RISC-V and requires VM/AOT agreement;
  `platform/s10_long_branch.py` builds a 30000-instruction function and checks
  the auipc/jalr far-branch lowering.  `tests/tools/s10_release_audit.py` checks the
  release surface.

```text
tests/conformance/
  static/
    s1_cases.py        auditable source table for the S1 cases
    s2s3_cases.py      auditable source table for S2/S3 cases
    s4_cases.py        auditable source table for S4 cases
    s5_cases.py        auditable source table for S5 CT cases
    lex/               LEX diagnostics (TC_CE_SYNTAX, TC_CE_LITERAL_LEX_RANGE)
    syn/               SYN diagnostics (the other five SYN codes + TC_CE_SYNTAX)
    accept/            positive LEX/SYN acceptance sources
    import/            IMPORT cases + root/library fixtures
    sem/               SEM name/type/conversion/layout cases
    sem4/              S4 CFG/DI/call/safety/annotation cases
    ct/                S5 CT/const/static-var cases
    accept_s3/         positive IMPORT/SEM acceptance sources
    accept_s4/         positive CFG/DI/call/safety/annotation sources
    accept_s5/         positive CT acceptance sources
  opcode_static/      S6 per-opcode static shape cases (one directory per case)
  runtime/            S6 VM runtime cases (one directory per case)
  diff/               S7/S8 VM↔AOT differential cases and fixed-seed fuzz
  abi/                S7/S8 AAPCS64, S9 AAPCS32 and S10 RISC-V ILP32 ABI cases; reverse TC→C interop
  checked/            S8 checked-build cases
  consistency/        S8 standard §13.4 eight-item consistency cases
  platform/           S9 bare-metal/ARM32 and S10 RISC-V platform cases
  longterm/           S10 standard/boundary/differential/fuzz/platform corpus
  tools/              S7/S8/S9/S10 artifact/toolchain and static-parity checks
  static/fuzz/        L7 property case (random bytes; no crash/hang)
```

## Source table and generated files

`static/s1_cases.py` is the **auditable source table** (text/marker cases plus
`RAW_CASES` byte fixtures with explicit positions): it records each case
id, source, `spec` citation, expected stage/code/primary position, and whether
a position comes from an explicit §12.5.1 rule or from the documented
`@convention` (the first token that cannot be consumed by the EBNF parse).  The
marker `@HERE@` records where the primary diagnostic must point; the generator
removes it and computes the 1-based line and Unicode-scalar column.  It never
reads compiler output.

```sh
# regenerate the S1, S2/S3, S4 and S5 YAML+source files
python3 tests/conformance/static/s1_cases.py --write
python3 tests/conformance/static/s2s3_cases.py --write
python3 tests/conformance/static/s4_cases.py --write
python3 tests/conformance/static/s5_cases.py --write
python3 tests/conformance/runtime/s6_cases.py --write
python3 tests/conformance/static/s6_static_cases.py --write

# gate: every generated file must match the table
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/conformance/static/s5_cases.py --check
python3 tests/conformance/runtime/s6_cases.py --check
python3 tests/conformance/static/s6_static_cases.py --check
python3 tests/conformance/diff/s7_cases.py --check
python3 tests/conformance/diff/s7_fuzz.py --check
python3 tests/conformance/abi/s7_abi_cases.py --check
python3 tests/conformance/tools/s7_toolchain_cases.py --check
python3 tests/conformance/tools/s7_static_parity.py --check
```

`@HERE@` and `@REL@` mark primary/related positions before marker removal.
Compile cases now run the full static pipeline (`--static-only`, including CT
and IR finalization), so an imported module's LEX/SYN diagnostic still wins
over an earlier IMPORT error (stage priority).  Tool acceptance cases name
`--syntax-only`, `--sem-only` or `--static-only` explicitly.  Profile-sensitive cases set `profile: w32` or
`profile: w64`; the adapter passes `--profile=w32` for the former and the
default 64-bit profile for the latter.

Runtime case expectations are derived from standard §6/§11 for integers,
bools and floats; floating output uses Python's independent exact decimal
renderer (`format(value, '.6g')`), never `tc-vm` output.  The harness `run`
decider compares stdout bytes, trap code, exit status and termination, and the
adapter rejects any unexpected stderr line or more than one trap.

Do not edit generated `.tc`/`.yaml` files by hand; update the table and
regenerate.  CRLF/CR and invalid-UTF-8 byte fixtures are marked `-text` in `.gitattributes`
so Git preserves their exact bytes; `--check` compares bytes, not normalized
text.  `RAW_CASES` pins the malformed-numeric-candidate recovery (`1.5.5` LEX at
the number start) and invalid-UTF-8 line/scalar-column behavior.  A new case needs a `spec` citation and, for positions not
enumerated in §12.5.1, an explicit `@convention` note.

## Case ids

Negative cases use `TC-LEX-SYN-NNNN` (LEX) and `TC-SYN-<DOMAIN>-NNNN` (SYN).
Positive tool cases use `TC-TOOL-SYN-NNNN`; the property case uses
`TC-FZ-LEX-NNNN`.  All ids are unique across the selected suite.

## Running

The harness requires an adapter.  `tests/CMakeLists.txt` configures
`build/tc_adapter.py` with this build tree's `tc-vm` path and registers:

```sh
ctest --test-dir build -R harness.conformance --output-on-failure
ctest --test-dir build -R 'determinism.s1|determinism.s2s3|determinism.s4' --output-on-failure
ctest --test-dir build -R 'sem.unit|conformance.s2s3_cases|conformance.s4_cases' --output-on-failure
```

`adapter.selftest` uses fake tc-vm scripts to prove the adapter is fail-closed
(wrong file, junk stderr, stdout, property path).  The adapter runs
`tc-vm --syntax-only` and parses the stable diagnostic line;
compile cases require exactly one diagnostic (stage/code/primary/related).
Acceptance cases are tool cases: exit 0, empty stdout/stderr, two runs
byte-identical.  The property case generates deterministic random byte inputs
and requires no crash/hang and, on rejection, exactly one LEX/SYN diagnostic.

`not_implemented` is never allowed by the gate; every selected case is either a
pass or a failure.
