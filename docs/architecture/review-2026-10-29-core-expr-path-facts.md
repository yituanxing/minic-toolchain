# MiniC Core expression CFG — source-local fact P0 fix

Date: 2026-10-29. Scoped owners: `src/core/core_lower_expr_flow.c`,
`src/core/core_lower.c`, `src/core/local_facts.c/h`, and the new focused test.
The concurrent `src/core/core_lower_if.c` refactor was **read-only**.

## Reproduced fault (before the fix)

GNU and MiniC x86-64 produced different native results for the same valid
program. The representative source is:

```c
int test(int condition) {
    int x = 0;
    condition ? (x = 1) : 0;
    if (x) return 1;
    return 0;
}
```

With `condition == 0`, the true arm is not executed; GNU returns `0`.
MiniC emitted valid machine code but treated the source local `x` as known
nonzero at the subsequent guard, so the first comprehensive native test
failed with status `13` (the check for `ternary_guard(0)`). GNU returned the
expected status `42`. Standalone `return x` path tests passed, proving the
error was in local-fact-based **Core CFG folding**, not the physical STORE.

## Root cause and ownership

- `core_lower_expr_flow.c` previously lowered true `?:` arm before false
  without restoring local facts. A fact established in the first arm was
  visible while lowering the second and after the merge. The same issue was
  present in ordinary scalar, void-result, and GNU omitted-middle variants.
- `core_lower.c` recursively emits the RHS of `&&` / `||` after branching on
  the LHS. Its source-fact table used to retain changes from the RHS despite
  the short-circuit path that does not execute it.
- `core_lower_if.c` already snapshots/reconciles whole statement branches,
  but this guard did not cover nested **expression** CFG joins. Its existing
  implementation was studied, not modified due concurrent ownership.

## Fix

1. Added `minic_core_local_facts_meet(context,left,right)` as a single
   branch-join primitive. Known integer constants survive only if both
   reachable paths agree on type/bits and neither escaped; escaped and
   specialization-modified metadata use OR. Stable zero and specialization
   proofs must agree on both paths. A missing snapshot fails closed.
2. Dynamic `?:` expression lowering saves facts **after condition evaluation**
   (so unconditional condition side effects remain visible), separately
   lowers both arms from the same incoming fact state, then meets the two
   resulting paths. Every early error frees fork snapshots. This covers
   ordinary scalar, discarded-void, and GNU `a ?: b` branches.
3. `&&` and `||` lower through one unified short-circuit CFG path. Immediately
   after the LHS, it snapshots the state for the skipped-RHS edge; after
   lowering the RHS, it meets the evaluated/skipped edge states. Compile-time
   pure constant LHS shortcuts continue to emit just the actually evaluated
   path. No blanket fact invalidation or speculative machine/backend logic.

This preserves source evaluation order and the existing return/goto Core
terminator ownership. No `core_lower_if.c` or target backend edits.

## Verification

- **Before fix:** MiniC x86 native status `13` versus GNU native status `42`
  on the new regression. Both source compilation and GNU assembler succeeded.
- **After fix:**
  `bash tests/compiler/c0/probe-core-expr-branch-local-facts.sh build/debug/bin/minic-cc`
  — `CORE_EXPR_BRANCH_FACTS=PASS cases=25 source_isa=5 x86_GNU_native=1`.
  This exercises scalar value and `if(x)` guard folding for `?:`, `&&`, `||`,
  equal-valued arms, nested logicals, side-effecting LHS, GNU `?:` omission,
  and void conditionals; C runs under both GNU and MiniC and all five source
  ISA emitters accept it.
- `make -j8 build/debug/bin/minic-cc CFLAGS=-Werror` — **PASS**.
- Existing `run-gnu-omitted-conditional.sh` — **PASS**.
- Existing `probe-gnu-mixed-void-conditional.sh` — **PASS** (GNU runtime,
  five targets and malformed negative cases).
- Existing `run-conditional-pointer-qualifiers.sh` — **PASS**.
- Existing `run-conditional-null-pointer-constant.sh` — **PASS**.
- Existing three RV64-native script harnesses need unavailable
  `riscv64-linux-gnu-gcc` in this workspace; not claimed as PASS.
- `git diff --check` for all four modified production files — **PASS**.

Not yet verified against a fresh exact-head Linux seven-shard CI or
architecture-native QEMU for the remaining four targets. Parent integration
should run those after resolving concurrent shared-tree modifications.
