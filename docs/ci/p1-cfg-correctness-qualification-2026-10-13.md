# P1 Core stack-slot correctness qualification (2026-10-13)

## Scope and acceptance boundaries

**Keep the successful Linux Image immutable.** The eight-owner MiniC
experimental Image SHA-256 is
`6f094d58e21de4b41fac7fffc1207b42dd04f5dd739c24f3d7107b1a944084f4`.
Actual QEMU runtime evidence: run **#38390921455** (24/24 independent
P1/IPv6 loopback passes with original 16KiB stacks, 2064/2064 MiniC C
objects and zero GNU C objects) and **#38396413292** (extra real P0,
interactive shell, and P1 all passed on the same *unchanged* Image,
without compiling or linking).

The corrected P1 candidate combines (A) the opt-in write-before-read
cross-basic-block Core scalar-stack slot reuse and (B) the opt-in
elimination of genuinely side-effect-free discarded local address
comparisons. Both are **EXPERIMENTAL**; the first remains blocked from
universal promotion. P1 boot success does not constitute compiler
correctness for arbitrary C control flow, pointer aliases or loop edges.

## Correctness obligations for scalar stack-slot sharing

A proof must be established for every *candidate object*, not guessed
from its C source identifier or a visually small frame:

1. **Scalar and ABI:** size 1..8 bytes, alignment <=8, no aggregate
   CALL/RECORD_LOAD/return-object uses, and no special ABI storage.
2. **Complete value-use accounting:** every address derived by
   OBJECT_ADDRESS has *only* direct LOAD/STORE uses inside one verified
   basic block. Any pointer escape via call, stored pointer value,
   indirect access, RETURN, asm, terminator, or unknown SSA use rejects
   sharing. Do not treat address identity comparisons as harmless.
3. **Initialization dominance:** the first actual access in the *sole
   owner block* must be a write; any read-first value that could arrive
   from a predecessor or loop backedge **must not** share across blocks.
   A parameter-object declaration must not be assumed equivalent to an
   ordinary full scalar STORE without examining where the backend
   materializes it in the prologue.
4. **Same-block temporal overlap:** live intervals in one block
   interfere whenever their first/last instruction positions overlap;
   the alias-analysis shortcut must never bypass this check.
5. **Across-block exclusivity:** sharing with another block is only
   permitted if *both* candidates independently satisfy the same
   no-escape / sole-block / first-write proof. Any unknown or malformed
   instruction, missing object metadata, or new IR opcode fails closed.

The checked-in experiment is
`tools/ci/apply-riscv64-p1-defined-before-use-crossblock-v1.py`;
it injects its predicate after the original conservative no-cross-block
stack-allocation hotfix. The profile is a build-time patch: do **not**
mistake the unpatched default compiler for the tested experiment.

## Actual new runtime differential: passed

[CI #38398681355](https://github.com/yituanxing/minic-toolchain/actions/runs/38398681355)
at commit `ecc5d4f0e7bada285cbe6f59f9434b6a35c9d50a`: **SUCCESS**.
The frozen 2064-object baseline cache was restored and SHA-checked;
the exact opt-in runtime-profile MiniC compiler was compiled; the
semantic-only mode **did not recompile or relink Linux**.

The real `qemu-riscv64` (not a syntax-only or symbolic test) ran:

| Oracle | Result |
| --- | --- |
| Existing CFG family | PASS; 255 inputs across seven functions |
| New adversarial family | PASS; 1023 inputs across eight functions |
| GCC, baseline MiniC, opt-in experimental MiniC | all three agreed on the eight new functions |
| Independent discarded-address semantic test | PASS; GCC / old MiniC / opt-in MiniC |
| Linux compile/link/24boot cost in this probe | zero |

Adversarial functions deliberately exercise **read-first live loop
backedges, write-first alternate scopes, switch fallthrough and
continue, goto joins, pointer addresses surviving across control-flow,
external calls which mutate `&local`, volatile loop state**, and
discarded local address identity comparisons. Every runtime value is
checked against GNU (O0) and the unmodified allocator of the same
MiniC binary; on mismatch the script prints input value, test case
and all three results and exits nonzero.

The T1 step reuses the **existing** gcc-shard-verify workflow job and
ends after the differential gates; no additional CI workflow/job or
new 2064-TU kernel campaign was created. Test implementation:
`tests/compiler/c0/run-core-cfg-slot-reuse-adversarial-p1.sh`.
This gate is also installed *before expensive kernel linking* in the
regular eight-owner P1 trial harness.

### What this validates — and what it does not

These results validate 15 focused C functions over explicit input
ranges, *not* a formal proof of all GCC-valid C programs. GNU O0 is
the reference for these fixtures; randomized differential fuzzing,
alternative optimization levels, and negative/fault-injection checks
would strengthen the oracle. In particular, the exact conditions
under which `PARAMETER_OBJECT` is initialized on every incoming CFG
edge need source and IR confirmation before global promotion.

## Subsequent qualification gates, in order

**T1 — Semantic expansion:** add deterministic-seed generator trials
over several CFG shapes and pointer-escape forms; compare GCC O0/O2,
unmodified MiniC and opt-in MiniC, and save small .i/.s/ELF/log
reproducers. Reject UB-producing fixtures; report a useful failure
trace, not just a CI color.

**T2 — Formal allocator audit:** instrument the actual Core IR
consumer graph and verify each object's single owner block,
first-definition instruction, predecessor/backedge liveness and
address-escape accounting; independently recalculate interference
from the emitted frame layout and check slot collisions.

**T3 — Runtime acceptance:** retain #38390921455's immutable Image
and #38396413292's independently passing P0/shell/P1 contract as
baseline; certify new variants on fresh images without changing
16KiB stacks, and assert stable success across repeated runs with
same SHA and independent workload families.

**T4 — Integration:** only after compiler correctness gates pass,
replace GNU AS/AR/Objcopy/LD one at a time while preserving SHA-
tracked C object inputs and the exact Linux runtime oracle. Native
tool-only successes do not implicitly validate the MiniC compiler.

**Rule:** an opt-in experiment or a narrower function/object
whitelist remains opt-in until both source-level lifetime proof and
real runtime regression tests are green. Never silently promote
unsafe CFG sharing globally or substitute GNU-compiled C objects
into a claimed all-MiniC kernel.
