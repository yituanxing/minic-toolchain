# Performance experiment Workflow consolidation — 2026-10-09

## What changed

Four separately triggered Performance-branch YAMLs are replaced by **one** `.github/workflows/linux-performance-experiments-v1.yml`, with a cheap `route` job and **four independent original experiment jobs**.

| Archived original (byte-identical) | New named job | Pinned Git Blob SHA |
| --- | --- | --- |
| `linux-core-object-interval-top5-ab-v1.yml` | `top5` | `5366fbb35022c973f80a11c6a8d3d349cde25d65` |
| `linux-gnu-constant-p-ice-regression-v1.yml` | `ice` | `01090b5d83f9687bfc1cdc0616e753e650f4e515` |
| `linux-optimized-first500-verify-v1.yml` | `first500` | `e3cee016ced9ea01f9c6e0690839f7bce141f999` |
| `linux-parser-scope-first500-ab-v1.yml` | `parser` | `df4ae079496233b826c05aebb6867c9014215f4d` |

All previous YAMLs are frozen under `.github/workflows-disabled/` and remain recoverable. Original build/compile/test steps, environment, job timeouts, corpus restoration, profiling and artifact-verdict steps were transplanted **without modifying their bodies**. M0 checks SHA and exact job-body equality.

**Independent evidence remains independent**: `top5` tests object interval performance/assembly fingerprints; `ice` tests GNU constant-p semantic correctness; `first500` measures integrated optimization on 500 frozen Linux TU with four workers; `parser` tests parser-scope A/B on the same corpus. A common source corpus does not mean identical experiment parameters or output claims.

## Automatic routing and cost boundary

The new lightweight selector `tools/ci/select_perf_experiment_modes_v1.py` checks every changed path against each original workflow's *own* push globs. This is needed because simply unioning paths on one YAML would cause **all expensive jobs to run on an unrelated change**. For instance:
- `tools/ci/apply-perf-core-object-interval-onepass-v1.py` selects `top5` and, by historical broad dependency, `first500`, **but First500 still requires `[perf-first500]`**.
- `tests/compiler/c0/gnu_constant_p_ice_regression.c` selects `ice`, `parser`, and (tag-conditional) `first500`.
- `src/frontend/**` selects `parser`, plus (tag-conditional) `first500`, not `top5` or `ice`.
- Changes solely to CI routing/YAML enter the lightweight route/M0 but do not run all expensive experiments.

If Git push history cannot be resolved, the selector fails open for the affected changed-path evidence; First500 still requires its original commit-tag gate. Manual dispatch provides `ice`, `top5`, `first500`, `parser`, `all`; `ice` is default to avoid accidentally running two expensive 500-TU pairs.

**Important limitation:** This is **routing and ownership consolidation**, not the integration of P15 or parser patches into MiniC source. There is not yet a new 500-TU or full Linux Image/QEMU certificate at this combined workflow HEAD. M0 is T0 static proof; run real selected Job on Performance ref to obtain new tier-specific evidence.

## Count reconciliation

| Item | Before | After |
| --- | ---: | ---: |
| Runtime active YAML | 28 | 28 |
| Performance active YAML | 29 | **26** |
| Distinct active YAML names across branches | 32 | **29** |
| Branch-path active YAML declarations | 57 | **54** |
| Runtime job declarations | 88 | 88 |
| Performance job declarations | 84 | **85** (the 4 experiments are preserved, plus 1 cheap router) |

The 500 TU A/B workflows still duplicate some source materialization and profile build time; do not equate a file-count reduction to lower billed CPU minutes. An independent build-system deduplication experiment would require validated source/profile/corpus hashes and the original paired-oracle outputs.

Details of other overlap clusters and Linux Runtime clean-up candidates: [CI owner and runner-cost audit](ci-active-overlap-cost-audit-2026-10-09.md).
