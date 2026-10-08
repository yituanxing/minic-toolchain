# MiniPP live-Kbuild exact workflow consolidation — 2026-10-08

Three independent historical Linux exact diagnostic workflows are merged
into **one** active `.github/workflows/minipp-linux-exact-smoke.yml`
with manually selectable modes `smoke` (default), `batch`, `72`,
`all`. Three unique job bodies preserve the 2-input smoke,
18-TU batch and 72-TU coverage, their original flags, diagnostics,
failure pools and **distinct named evidence artifacts**. All original
per-job shell commands remain unchanged, except the job-level `if`
guards now route manual dispatch by mode while retaining historical
push commit-message triggers on the old `toolchain/minipp-*` branch
pattern. The currently active Runtime `minipp-linux-exact-v1` frozen
shard exact replay, `minipp-linux-focus-v1` focused frozen replay,
and independent `minipp-a0.yml` micro tests are **not touched**.

Old byte-identical Git blobs preserved under
`.github/workflows-disabled/`:
- `minipp-linux-exact-batch.yml` = `fbd14271a9ffcb45350a3ada76a65b8c26345fc6`.
- `minipp-linux-exact-72.yml` = `dcf0399404d1c7d671cbeb8ef00a6dab816ffe30`.

This operation changes workflow orchestration only. It **does not**
claim successful current-head execution of either live Kbuild replay,
Linux Image construction, or QEMU boot. A lightweight M0 YAML parse
and exact blob/hash guard is the appropriate cheap acceptance check.
Both the Runtime and performance branches had exact same three starting
Git blobs, allowing identical merged workflow reuse across both.
