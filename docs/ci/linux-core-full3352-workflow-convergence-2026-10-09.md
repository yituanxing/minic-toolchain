# Linux Core frozen 3352 compile/assemble workflow convergence — 2026-10-09

Two historical full-coverage, distinct Linux Core proofs are grouped under
one live `.github/workflows/linux-core-all3352.yml` with manually selected
`compile`, `assemble` or `all` mode. The original **six separate
jobs** remain independently evaluated:
`first500`, `shards`, `all3352` preserve strict 3352 Core
compilation; `asm-first500`, `asm-shards`, `asm-all3352`
preserve compilation **plus GNU assembly**. The latter job IDs
were namespaced to avoid collisions, and corresponding `needs`
references were updated to those IDs. Per-step shell tests, compiler
flags, cache keys, matrix cohort identity and PASS markers were kept.

Archived original:
`.github/workflows-disabled/linux-core-assemble-all3352.yml`
blob SHA `6a0ce243dd6cf16e038588fb1c81e5ca993e77fb` (identical in Runtime and performance).
Original live compiler workflow: `395065686acd57496d7ff3894f7cfc0021b22360`.
Both before this change listened only to the **deleted**
`refactor/declaration-sema-v1` push branch. Combined workflow now
accepts explicit `[all3352]`, `[linux-core-all3352]` or
`[linux-assemble-all3352]` opt-in commit tags on current development
branches with scoped source/test paths. Untagged pushes skip six
expensive jobs. Manual inputs can select both checks in one run.
This changes triggers but does not silently run a Linux rebuild.

The separate `linux-core-focused-five.yml` historical five-TU
regression and `linux-core-shards-v1.yml` certified frozen shards
are **not** superseded and remain active. Legacy frozen cache access
may still be branch-scoped and need current-run verification. A
green structural M0 can prove YAML schema, exact archive SHA and
retained executable commands, but it is not an all3352 / Image /
QEMU certificate.
