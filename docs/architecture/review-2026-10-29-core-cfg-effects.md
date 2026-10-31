# Core CFG condition folding: preserve observable evaluation (2026-10-29)

## Reproduction / oracle

`tests/compiler/c0/run-core-cfg-fold-effectful-conditions.sh` preprocesses each
source with GNU cc, compiles it with native GNU cc and the MiniC x86-64 backend,
links the MiniC assembly with GNU cc, and executes both binaries. Each program
returns zero only if every required call/increment actually ran.

Before these fixes:

- `if (always_null(tick()))` was optimized to the false arm without calling
  `tick()`; the MiniC executable returned 12, while GNU returned 0.
- `if (((_Bool)tick()) > 1)` was optimized to false without calling `tick()`;
  an isolated version of this case returned 16 in the combined test when the
  null-helper condition did not fold, while GNU returned 0.

After the fixes both dedicated regression programs pass GNU/MiniC runtime
comparison. `make -j4 CFLAGS=-Werror build/debug/bin/minic-cc` passed.

## Mechanism and ownership

- `core_lower_const_facts.c`: a direct call to an unconditional-null helper
  now proves an elidable null expression only when **all arguments** pass the
  shared source-expression purity predicate. The callee's first return must
  also be an independently pure literal-derived null. This prevents using
  caller-local facts for the separately scoped callee body.
- `core_lower_const_facts.c`: boolean-range comparison shortcuts cannot
  conclude an expression is discardable unless the **whole comparison**
  passes the shared purity predicate. Range proofs alone describe values, not
  evaluation effects.
- `core_lower_if.c`: the local-fact integer CFG shortcut requires a pure
  source condition. Ordinary target-aware compile-time constant evaluation
  retains its existing behavior; pointer-null CFG pruning uses the refined
  known-null proof, avoiding an unnecessary global rejection of pure helpers.
- `core_lower_if.c` hygiene: removed the unconditional hard-coded
  `genl_op_from_split` / `GENL_IF_DECISION` diagnostic output. It ran during
  ordinary compilation and did not participate in CFG decision-making.

The shared `minic_core_cfg_pure_call_argument` conservative check lives in
`cfg_constant_calls.c`, keeping the effect model in Core (no target-specific
workaround). It rejects calls, updates, assignments and volatile reads in
argument/boolean expression subtrees. A safe but unrecognized expression now
uses normal runtime lowering rather than risking missed observable effects.

## Validation scope / follow-up

- Host x86-64 GNU/MiniC semantic oracle: PASS (both programs).
- Full compiler `-Werror` build: PASS.
- `-S` for both test programs: PASS on x86-64, RV32, RV64, AArch64 and
  LoongArch64 targets (10 compile-only paths).
- Multi-architecture Linux 7-way compile and native/QEMU runtime suites are
  **not** certified by these focused tests; they remain separate CI gates.
- Other uses of source-level constant facts should continue to be audited for
  "known value" versus "safe to omit evaluation" conflation.
