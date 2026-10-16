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

- `preprocessor/target/riscv_linux_predefines.py` is the **single canonical product-owned implementation**; the duplicate CI-only generator has been deleted. It uses **one immutable
  RV64/LP64 C ABI macro baseline**, independently derives optimization and
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

## Current full-build gate

- [#38847872062](https://github.com/yituanxing/minic-toolchain/actions/runs/38847872062):
  seven freshly compiled SHA-attested shards reached **7/7, 2064/2064**
  with the native macro rule generator and exact GNU byte comparator; the
  joined Linux/QEMU result is tracked in that same run and must be checked
  before declaring runtime acceptance.
- [#38848622838](https://github.com/yituanxing/minic-toolchain/actions/runs/38848622838):
  following source convergence into `preprocessor/target`, the permanent
  focused product self-test and eight existing toolchain regression checks
  all passed on `main`.
- Product baseline macros are still derived from a pinned GCC13/RISC-V ABI
  reference dataset. This is **GCC-process-free macro production for the
  certified RV64 Linux configuration**, not an architecture-independent
  target-macro implementation embedded directly in the C MiniPP binary.

## Immediate next acceptance

One fresh full seven-shard run must use native macro rules for all 2064 C
owners. Then the same authenticated pool, 34 MiniPP→MiniAS assembly objects,
four dynamic MiniPP→MiniC→MiniAS owners, linker chain and final unchanged
QEMU P1 12/12 must all pass. A prior SHA-stable pool is not interchangeable
without exact provenance proof. Until that run is green, full native-rule
macro production is **pending**, regardless of the nine-context dictionary
and 224-object smoke passes.

The remaining real GNU target linker output is the RV32 compat-vDSO, deferred
as a separately scoped ELF32 MiniLD capability; two GNU linker capability
probes do not produce Linux ELF output.
