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

## Original Linux 3x7 SHA-pinned replay (2026-10-27)

An opt-in runner reuses the frozen full Linux GNU Kbuild C/.S corpus
from immutable run `40461070882`, without re-running GCC. It verifies
the saved source and all .s/.cmd SHA per file, and pins manifests:
x86-64 `9065a6925d9e5081336894e42f7544f9d671efcb5d70eb499a24884988d00ac1`,
AArch64 `e2d034013185b214763c9fa42aef3f3eb4e9d96e276f8678717c1f9d675969cf`,
LoongArch64 `203ed9df2cb4408cd8d0ee48b5de9f2a6444d115e8cdca649cf70995d6fc7017`.
21 independent MiniAS jobs classify genuine first blockers after target
registration. This is NOT GNU assembled object parity, Image, or P1.

The Common parser now recognizes strict `# N "file"` provenance markers
from GNU-preprocessed native .S, without weakening AArch64 immediate
syntax or suppressing actual ISA errors. CFI/.eh_frame remains a semantic
implementation obligation, not a directive to ignore.

## Efficient post-target-change Linux recheck (2026-10-27)

After adding x86 CET endbr64, x86 group-1 immediate arithmetic and
LoongArch JR, the 3x7 original Linux attempt was almost complete but
x86 shard-3 twice received an external runner shutdown (exit 143), not a
MiniAS diagnostic. A child-only 2 GiB RLIMIT_AS and 25-owner progress
markers now bound work without relaxing ISA. Rather than recompiling GNU
Linux or rerunning unaffected AArch64 CFI blockers, a **2 ISA × 7 shard**
replay reuses SHA-pinned original x86 and LoongArch Kbuild .s/.cmd,
and aggregates only those 14 result sets. The aggregator accepts explicit
target selections while retaining exact source, unique owner, coverage,
and manifest/config consistency checks. This run is diagnostic and neither
ELF semantic acceptance nor Linux P1.

## Scanning accuracy and next failure-family batch (2026-10-27)

For the original full-input GNU Kbuild parity census, a MiniAS return code
of 0 is now only `ASSEMBLED_UNVERIFIED` if the produced file is **real ELF64
little-endian ET_REL with the expected target e_machine**; success without
an ELF object is an infrastructure error, not a false positive.

Per-file original diagnostics remain intact, while the aggregator also emits
architecture-specific mnemonic families, e.g. `unsupported-x86_64-operand:movq`
or `unsupported-loongarch64-operands:pcalau12i`, collapsing offset/index/
register spellings only for Pareto analysis (never for correctness decisions).
An opt-in SHA-pinned `2x7` run is triggered after this grouped change,
reusing all C-produced assembly unmodified and without another Linux build.

## LoongArch-only seven-shard exact-head incremental census

One target-local opcode batch must not automatically rescan an unrelated
architecture. The existing frozen-GNU Kbuild replay now accepts the explicit
`[minias-replay-loong-7]` trailer to select **only 7 LoongArch64 shards**
and `aggregate --targets loongarch64`; `[minias-replay-2x7]` keeps
the two-target 14-shard mode for shared changes. Both modes pin the
unchanged Linux 6.6.143 GNU config/compiler/assembly manifest. The
automatic workflow does not start on normal one-line code pushes.
