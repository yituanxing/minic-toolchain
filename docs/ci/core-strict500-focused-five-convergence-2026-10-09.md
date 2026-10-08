# Core frozen strict500 and exact focused five — 2026-10-09

Two Core Linux semantic/compile regressions are maintained in one
mode-selected workflow `.github/workflows/core-first500-regression.yml`.
The independent `strict500` (frozen 500 TU Core-only contract)
and `focused-five` (five known indices 254,775,779,879,881) jobs
retain their respective original checks, cache restore identities,
failure-context diagnostics, timeouts and artifacts.

The retired old focused workflow is **exactly archived** at
`.github/workflows-disabled/linux-core-focused-five.yml`
with Git blob `df5828d4035d41ee178da5ba5ada05a8abb59a09`; parent live original primary
blob was `f964f076486146b66ce71ccdd56c00322cbb0f9c`. Both dev branches used identical source
blobs. Job-level manual `mode` selects `strict500`,
`focused-five` or `all`. Explicit `[regress500]` or
`[linux-five]` commit tags can run the appropriate check on current
dev branches for source/test path-scoped pushes. Historical push branch
`refactor/declaration-sema-v1` no longer exists and was dropped.
No expensive checks run on an ordinary untagged push.

No source changes, no fixture key changes, no weakened PASS check
and **no claim of current runtime execution**. Historic Core
3352 compiler corpus and GNU assembly corpus remain separate,
as do certified Linux distributed object / Image workflows.
The full exact and mini 5-TU tests may require historical frozen
cache availability or rebuilding before they pass. M0 verifies
workflow YAML, two jobs, exact archived blob SHA and byte-identical
historical focused-five executable steps.
