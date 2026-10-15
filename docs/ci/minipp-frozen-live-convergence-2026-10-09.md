# MiniPP frozen and live-Kbuild exact workflow convergence — 2026-10-09

The maintained `.github/workflows/minipp-linux-frozen-v1.yml` now contains **five separately evaluated jobs**: `exact-shard`, `cached-focus`, `linux-exact-smoke`, `linux-exact-batch` and `linux-exact-72`. Frozen-GCC byte-exact and Live-Kbuild exact are **different oracles** and keep separate success conditions and artifacts.

| Former workflow now archived | Exact original Git blob SHA | Jobs |
| --- | --- | --- |
| `minipp-linux-frozen-v1.yml` | `2f2d22fd0183f4fc4e399497b1b3745b89d78da8` | frozen exact / focus |
| `minipp-linux-exact-smoke.yml` | `02c08d5b76501be16277ef233751f7ed191c15cd` | live smoke / batch / 72 |

All five executable job bodies are byte-identical after removing only dispatch/branch predicates and the moved Live-Kbuild concurrency group. The older `minipp-linux-exact-v1.yml` and `minipp-linux-focus-v1.yml` original archived sources also remain separately checked by the existing frozen-convergence M0 script.

## Exact routing contract

Manual `workflow_dispatch` modes: `exact` (default), `focus`, `smoke`, `batch`, `72`, `all`. Original named modes and original five push opt-in tags are preserved. The push path filter is the union of the two prior inputs (with archived YAML entrypoint paths mapped to the new owner). The former `toolchain/minipp-*` Live-only branch glob is retained; frozen jobs have an explicit development-branch condition so they **cannot** begin on that old Live-only branch.

The original Live workflow-wide `minipp-linux-exact-${{ github.ref }}` concurrency key now operates on each Live job with a literal distinct job ID suffix. This prevents separately requested Live cohorts from cancelling one another while retaining `cancel-in-progress: true`. Frozen exact/focus matrix-scoped concurrency keys and cancellation policy are unchanged.

Both archived YAMLs are original Git blobs; `tools/ci/check_minipp_live_frozen_merge_v1.py` verifies each SHA, all five executable job bodies, original tags, modes, branch gates, frozen SHA256 and independent Live concurrency. Existing `check_minipp_frozen_convergence_v1.py` still enforces its earlier exact-shard and cached-focus source-blobs. Runtime M0 also preserves the historical 18-batch and 72-path archival identity checks.

This is a **T0/M0 source and structure proof only**: true Frozen 3352 exact replay, Live Kbuild smoke/batch/72 execution and pinned source artifact availability require their respective current-head T2/T3 runs. No Linux boot, runtime correctness or performance certification is claimed. MiniPP A0 remains a separate path-triggered fast unit check.

Expected active count: Runtime **35→34**, Performance **36→35**, distinct cross-branch names **39→38**; no compiler or test implementation changed.
