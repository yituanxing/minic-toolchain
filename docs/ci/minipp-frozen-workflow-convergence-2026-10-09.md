# MiniPP frozen Linux oracle convergence — 2026-10-09

Two opt-in frozen MiniPP workflow entrypoints are now **one canonical** `.github/workflows/minipp-linux-frozen-v1.yml` with separate `exact-shard` and `cached-focus` jobs. Basic automatic MiniPP A0 and the separate live-Kbuild Smoke/Batch18/72 test family are unaffected.

## Preserved assertions
- `exact-shard` was in `minipp-linux-exact-v1.yml`, full frozen product corpus with GNU/GCC exact replay; push tag `[minipp-exact]`. Original workflow concurrency had `cancel-in-progress: false`.
- `cached-focus` was in `minipp-linux-focus-v1.yml`, targeted frozen cache replay; push tag `[minipp-focus]`. Original concurrency had `cancel-in-progress: true`.
- Both original executable job bodies are byte-for-byte identical between development branches; only their original workflow headers differ. The combined body preserves all steps, assertions, matrices, cache identities, GNU oracle commands and artifact names, except for the deliberate manual selection guards and job-scoped concurrency.
- `workflow_dispatch` selects `exact` (default), `focus`, `all`; original two push tags remain and are gated by each job.
- Per-job concurrency includes `matrix.id` to avoid serializing or cancelling different matrix shards. Original cancellation policies remain lane-specific.

## Original archive blob SHAs

| Source archived to .github/workflows-disabled/ | Runtime SHA | Performance SHA |
| --- | --- | --- |
| `minipp-linux-exact-v1.yml` | `92e53a34032177800251d00ff0ca2fd142ffdd15` | `ee164e55bbeae0e1377f307ae70314dd4030143f` |
| `minipp-linux-focus-v1.yml` | `08ca506dc4d6933f81648d7156cf3f698fc0a17d` | `796e5b21a380aa9a2d9426ca0ecf1d12b49cf5fe` |

## Validation

`tools/ci/check_minipp_frozen_convergence_v1.py` verifies the original Git blob SHAs for each branch, original vs. new job bodies, unique IDs, tag predicates and matrix-aware concurrency. M0 also parses the combined YAML using Ruby.

M0 green confirms static structural proof, **not** renewed frozen exact corpus or focused replay success; those require true T2 runs before claiming full post-migration certification. Old external frozen corpus artifacts may expire; no source SHA or cache contract was loosened.

Expected active workflow totals after successful migration: Runtime **49→48**, performance **48→47**, distinct names **53→52**.
