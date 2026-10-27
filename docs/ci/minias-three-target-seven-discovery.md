# MiniAS 3x7 architecture-scoped discovery (2026-10-27)

## Source architecture audit before change

- RISC-V production target exists and has separate seven-shard frozen Linux history.
- AArch64 has limited MCInst/ELF64 support; x86-64 and LoongArch64 do not have MiniAS targets.
- GNU C->S baselines are NOT equivalent across ISA. Failures are clustered independently by target.
- The preceding architecture batch made the CLI independent of ISA/ABI policy and moved profile resolution to each target.

## New first capability-discovery batch

- 3 compiler-produced source-assembly corpora: x86_64 (GCC), aarch64 (cross-GCC), loongarch64 (LLVM Clang, explicit provenance).
- 70 reproducibly selected real C tests per target; any compiler failures are producer errors, never assembler errors.
- Every frozen assembly input has a SHA; seven shards per target partition READY inputs with index mod 7, 21 jobs total.
- Candidate problems are recorded individually and as first-blocker Pareto **within that target only**.
- Each collected shard runs to completion, even with hundreds of candidate assembler failures.
- ASSEMBLED_UNVERIFIED is not PASS; no GNU object parity, full Linux, or P1 is claimed.
- Aggregation requires exactly 21 shard IDs, a matching frozen manifest for each architecture, unique owners, and complete coverage.
- The expensive 21-runner CI triggers only on manual dispatch or special [minias-discovery-3x7] commit trailer.

## Post-change architecture audit

- Discovery logic is entirely outside the assembler and target backend source. The common MCInst, Fragment, Symbol, ELF and ISA policies are untouched.
- Target-specific errors remain separated; only proven common Expression/Fragment/Fixup/Relaxation mechanisms will be promoted to Common.
- This bounded compiler-C-test census is only a fast lane. Linux whole-image GNU baseline, frozen full corpus and semantic GNU-as comparison remain separate milestones.
- Next development batch: inspect real blocker counts, implement related target capabilities together, and rerun targeted affected shards before L3/L4.

## First exact-head discovery result (head 553d0718)

- Workflow run 40453774777: 3 producers and all 21 consumer shards completed.
- x86_64 baseline: 70 C->S READY; MiniAS 0 ASSEMBLED_UNVERIFIED / 70 candidate failures, first blocker unsupported-target.
- aarch64 baseline: 46 READY and 24 distinct C->S producer failures; MiniAS 0/46, first blocker unsupported-directive .arch.
- loongarch64 baseline: 44 READY and 26 producer failures; MiniAS 0/44, first blocker unsupported-target.
- All outputs are discovery failures; workflow success means complete evidence, NOT supported assembly or GNU equivalence.
- Fast-lane follow-up: add validated AArch64 .arch armv8-a (reject armv9), then use pinned run 40453774777 artifact to replay only seven AArch64 shards. No regeneration of any C or frozen assembly, no redundant 21-job rerun.
- Source architecture after this batch remains target-bound: the .arch directive is implemented only in AArch64 target, and the generic shard aggregator adds optional target filtering without encoding architecture semantics.
