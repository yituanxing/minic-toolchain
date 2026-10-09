# MiniAS frozen-window and 3536 semantic oracle convergence — 2026-10-09

Two manually dispatched MiniAS workflows are one **canonical manual entrypoint**, `.github/workflows/minias-a0-focused-diagnostics-v1.yml`. The user now chooses **real16**, **frontier**, **vector33**, **window** (with original seven frozen-window choices), or **semantic**. The separate `minias-a0-gate-v1.yml` remains active: its 3536 exact-acceptance certificate is not interchangeable with semantic-oracle coverage.

The canonical entrypoint has ten unique jobs: six original focused/frozen-window jobs (`real16`, `frontier`, `vector33`, `resolve`, `window-shard`, `window`) and four independent semantic-oracle jobs (`c-shards`, `c149`, `native35`, `aggregate`). Semantic runs retain their original GNU comparison, C149 and native35 sources, 3536 corpus, artifact upload, timeouts, and four-part aggregate contract. The old semantic workflow's `on.workflow_dispatch` ran the whole semantic group; now only choosing `mode=semantic` does so. Its historical push-commit tag predicates were irrelevant on the current Runtime and Performance branches: Runtime source was dispatch-only and Performance source allowed push only on obsolete branches.

## Immutable source provenance

| Original | Runtime Git blob | Performance Git blob | Status |
| --- | --- | --- | --- |
| `minias-a0-focused-diagnostics-v1.yml` | `93330555c3913fb78ac1c67d180f2a70dabe951f` | same | byte-exact archived on both |
| `minias-semantic-oracle3536.yml` | `519b4b6fa9f3dd6e344761a7e7f6cbbd1de76831` | `96bb3bf93fac928613f944b0042ddbac1c4c0280` | byte-exact archived on corresponding branches; old headers differed but job bodies remained equivalent |

M0 `tools/ci/check_minias_semantic_convergence_v1.py` checks the original archived blob SHA for each branch, all ten independent job identities, *all six original focused job bodies byte-identically*, all four semantic executable bodies byte-identically except for their initial job-level `if` selectors, exact artifact run IDs, permissions, and aggregate dependencies. This is T0 source evidence; **it does not claim a new real 3536 semantic oracle pass**. Original downloadable artifact producers may have expired, so run-specific input provenance must still be checked before claiming T2 certification.

Active workflow targets after migration: **36 Runtime / 37 Performance / 40 unique names**. No compiler source or test script implementations were altered.
