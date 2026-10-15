# MiniAS focused and frozen-window workflow consolidation — 2026-10-09

The two historical diagnostic workflows have been merged into one
manual-mode dispatcher while keeping all six original **independent** jobs:

- Canonical active owner: `.github/workflows/minias-a0-focused-diagnostics-v1.yml`
- Archived exact historical workflow:
  `.github/workflows-disabled/minias-a0-window.yml`
  (Runtime blob SHA `e7763f8faa294d08b341e0b079b32477b54e9cc6`;
  performance branch has different historical SHA
  `36098a4d72c6e3f1c9fffd93fe52fbbd990a5105` only because of
  two push branches that no longer exist).
- Six jobs remain: `real16`, `frontier`, `vector33`, `resolve`,
  `window-shard`, `window`. The last two preserve their
  original `needs` graph and all original commands/artifact names.
- Manual `mode` selects `real16`, `frontier`, `vector33` or
  `window`. When `window` is selected, a second optional
  `window` choice selects `first500`, `new500`, `next500`,
  `next500b`, `next500c`, `next500d` or `final352`.
- Only the legacy window `resolve` job-level condition changed, so a
  real16/frontier/vector33 dispatch cannot start an unrelated window.
  Every original executable job body is unchanged. The **same**
  canonical merged workflow Git blob is used across both dev branches.
- No automatic branch push was added. The performance branch's old
  `toolchain/minias-m0-boundary-freeze` and
  `agent/ci-runtime-cleanup-v1` push listeners were *dead* (the remote
  branches do not exist), so dropping those legacy listener entries
  does not drop a runnable current branch path.
- Historical source corpus / sidecar artifact IDs
  `33186855250` and `33237304382` and config identity remain
  exact. Older artifacts may have expired; no current
  downloadable-fixture proof is implied by this cleanup.
- `minias-a0-gate-v1.yml` **retains independent exact 3536
  acceptance/corpus/native35/C149 contracts**.
- `minias-semantic-oracle3536.yml` **retains independent GNU-as to
  MiniAS semantic equivalence contracts**.

The M0 structural checker should parse the canonical workflow, assert
six preserved jobs and identical original window job bodies after
removing only the modified `resolve.if` condition, and verify SHA of the
retired original YAML (on Runtime). Heavy MiniAS corpus rerun,
QEMU and Linux runtime work are not part of this consolidation.

## Post-consolidation verification

- Both dev branches have the same merged canonical MiniAS blob
  `93330555c3913fb78ac1c67d180f2a70dabe951f`.
- The full three original focused job bodies and original window
  shard/aggregate job bodies remain unchanged in the merged definition.
- [M0 #37860392307](https://github.com/yituanxing/minic-toolchain/actions/runs/37860392307)
  verified YAML validity, six jobs, old archive SHA, and the original
  window execution body without running a Linux build or QEMU.
- Intermediate M0 runs were red due to defects introduced into the
  temporary checker; the checker was reconstructed from the preceding
  green `5bafdaa7...` Git blob before minimal MiniAS assertions were
  added. The final green M0 is the authoritative structural proof.
