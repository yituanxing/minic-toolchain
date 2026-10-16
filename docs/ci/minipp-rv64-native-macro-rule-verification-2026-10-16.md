# MiniPP Linux RV64 target macro rule convergence (2026-10-16)

## Objective

Replace live GNU GCC target builtin-macro extraction during the actual Linux
6.6.143 RISC-V preprocessing build, preserving exact target input semantics.
The GNU compiler may still be used in the *independent read-only oracle*.

This is distinct from general support for arbitrary GCC target triples,
arbitrary architecture flags, or full replacement of RV32 compat-vDSO linking.

## Accepted reference and differential evidence

- [#38845799554](https://github.com/yituanxing/minic-toolchain/actions/runs/38845799554):
  seven fresh MiniPP → MiniC → MiniAS C shards, 2064/2064, strict SHA merge,
  native 34 assembly owners and four regenerated-header objects, real QEMU
  P1 12/12 and native MiniAR/MiniLD-certified/MiniObjcopy linked Image.
  This used the pinned predefine table as the native C preprocessing source.
- [#38844537392](https://github.com/yituanxing/minic-toolchain/actions/runs/38844537392):
  authentic Kbuild option inventory; **9 observed flag contexts**, **2064 C
  owners plus four dynamic recompile roles**, and **34 native assembly owners**.
  The GCC version used for the immutable reference snapshot is 13.3.0.
- [#38846799662](https://github.com/yituanxing/minic-toolchain/actions/runs/38846799662):
  profile audit PASS: 9 flag contexts represent **7 distinct macro-content
  hashes**, with 105 macro names varying across the C and assembler profiles.
  The C variants depend on `-O2`, `-fPIC`, `-fpic`, and `-fpie`;
  repeated `-mcmodel=medany` does not change the macro content.
  RV32/ILP32 assembly differs by 70 macro definitions from RV64/LP64.
- [#38847031760](https://github.com/yituanxing/minic-toolchain/actions/runs/38847031760):
  rule generator self-check PASS for all 9 real Kbuild contexts against the
  immutable macro dictionaries, **0 GCC child processes**.
- [#38847343115](https://github.com/yituanxing/minic-toolchain/actions/runs/38847343115):
  all seven 32-owner MiniPP/MiniC/MiniAS *native-rule stages* explicitly
  reported **PASS 32/32**, 224 real object inputs byte-equal to GNU.
  The run overall FAILED because the sample-only trigger inadvertently also
  started the unrelated legacy GNU shard producer, not because native PP failed.
  The existing job's guard was corrected to exclude that redundant producer;
  this run is NOT a green workflow and is NOT the full Linux claim.

## Actual native macro production

- `preprocessor/target/riscv_linux_predefines.py` is the **single canonical product-owned implementation**; the duplicate CI-only generator has been deleted.
  It uses **one immutable RV64/LP64 C ABI macro baseline**, independently derives optimization and
  PIC/PIE macros from the exact current option context, and fails closed on
  unsupported architectures.
- Two checked RV64/LP64 and RV32/ILP32 assembler baseline profiles remain
  required. The 70-macro RV32 ABI delta is a distinct later generalization.
- Actual normal production needs **no GCC process to regenerate target macros**.
  The pinned historical GCC13 source profiles are compatibility data, not
  evidence of a fully independent universal compiler builtins implementation.
- `tests/preprocessor/build_reference_predefines.sh` retains GNU reference
  generation separately and dispatches to native rules only by explicit opt-in.
- No source rewrites or GNU target fallback are introduced by this gate.
  The pinned input and per-shard macro producer identity are SHA verified.

## Final acceptance: FULL PASS

- [#38847872062](https://github.com/yituanxing/minic-toolchain/actions/runs/38847872062),
  commit `5382dd8a6de176c3a39918e02b9449125301624a`, **SUCCESS**:
  seven freshly compiled, per-owner SHA-attested C shards, **7/7** with
  **2064/2064** actual MiniPP → MiniC → MiniAS objects. All seven shards
  explicitly recorded `MINIPP_SEVEN_NATIVE_MACRO_RULES=PASS`; the native
  macro producer was selected, GNU was used as byte-exact reference only,
  and `GCC_macro_processes=0`.
- Strict merge:
  `MINIPP_NATIVE_MACRO_MERGE=PASS owners=2064 shards=7 macro_producer=native_abi_rules snapshot_SHA_attested=true GCC_macro_generation=none`.
  This is real complete-source production, not a 224-owner smoke test.
- **34/34** native Linux `.S` objects:
  `MINIPP_NATIVE_S_PROFILE=RULE_GENERATED`,
  `MINIPP_NATIVE_S_INPUTS=PASS owners=34 GNU_original_byte_identical=true MiniAS_consumed_MiniPP=true`,
  then verified retained at both link passes.
- **4/4** generated-header C owners:
  `MINIPP_DYNAMIC_HEADER_RUNTIME=PASS native_PP=4 native_CC=4 native_AS=4 stable_native_C=2060 QEMU_P1=12/12`.
  The rule generator was explicitly selected in the current cohort's
  `COHORT_MINIPP_NATIVE_MACRO_RULES=1` replay environment.
- **QEMU P1 12/12**:
  `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`;
  `MINIPP_SEVEN_QEMU=PASS pp_input=MiniPP native_C=2064 QEMU_P1=12/12`;
  `NATIVE_UNION_RESULT=PASS`. Native MiniAR completed 643 Kbuild calls with
  zero GNU AR production fallbacks, MiniLD native 64-bit vDSO linked with
  zero GNU LD fallback, and MiniObjcopy participated in the accepted chain.
- [#38848622838](https://github.com/yituanxing/minic-toolchain/actions/runs/38848622838),
  **SUCCESS on later `main`**: after moving the target macro-rule engine into
  `preprocessor/target/riscv_linux_predefines.py` and deleting the duplicate
  `tools/ci` generator, the product self-test still matches all nine
  reference macro dictionaries, and all eight focused compiler/archiver/linker
  checks pass. The move did not modify the native rule semantics.

**Scope boundary:** source-owned target rule generation is a Python companion
to the native C MiniPP executable, not an embedded C target builtin
implementation. It uses pinned GCC13/RISC-V ABI baseline data; at runtime,
no GCC subprocess generates the target macros. GNU still supplies independent
byte-identity comparison and some non-native host/compatibility boundaries.
Do not mistake this accepted RV64 Linux configuration for a general,
unrestricted GCC replacement.

## Remaining work outside this acceptance

The remaining real GNU target linker output is the RV32 compat-vDSO, deferred
as a separately scoped ELF32 MiniLD capability; two GNU linker capability
probes do not produce Linux ELF output.
