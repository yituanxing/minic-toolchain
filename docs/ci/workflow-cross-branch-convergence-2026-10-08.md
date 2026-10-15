# Cross-branch CI convergence — October 8, 2026

> **Latest 2026-10-09 supplemental checkpoint:** **51 Runtime / 50 performance** active YAMLs, **55 distinct active names**, **277 byte-preserved disabled files per branch**, four remote branches. Tables below originated before the two latest Core consolidations; use these current figures instead. [M0 37861656075](https://github.com/yituanxing/minic-toolchain/actions/runs/37861656075) SUCCESS. See [Core full3352](linux-core-full3352-workflow-convergence-2026-10-09.md), [Core focused](core-strict500-focused-five-convergence-2026-10-09.md) and [workflow liveness/artifact audit](workflow-liveness-artifact-audit-2026-10-09.md).

## Current verified exact inventories

| Branch | Active workflows now | Disabled workflow files (incl. nested) | Count at start of this cleanup sequence |
| --- | ---: | ---: | ---: |
| `agent/linux-expanded-kbuild-v0` | **51** | **277** (234 root + 38 + 5 nested) | 68 |
| `agent/linux-perf-boolean-domain-v1` | **50** | **277** (234 root + 38 + 5 nested) | 91 |
| `main` | 11 | untouched | 11 |
| `archive/all-progress-2026-10-04` | no active workflow directory | passive archival history | unchanged |

Runtime and perf now share **46** active workflow filenames, plus **5**
Runtime-only (distributor + graph preflight + 3 opt-in performance checks)
and **4** perf-only (top5 object interval, GNU constant-p ICE,
canonical optimized first500, parser scope first500).
**55 distinct active names** across the two development branches,
down from **100**. The sum of active definitions falls 159 → 101.

Four branch refs remain; no source merge and no perf branch deletion.

## Retirements and merges

1. Runtime first retirements: four old distributed workflows were
   archived unchanged: 120-object shards, the two historical pinned
   receivers, and the older full-image V1 workflow. The maintained
   independent full Image producer is
   `.github/workflows/linux-distributed-full-image-signedness-v2.yml`.
   Full future seven-shard self-producing pipeline not yet newly CI-certified.
   See `distributed-workflow-retirement-2026-10-08.md`.
2. Performance branch: 28 dormant old YAMLs, already identically archived
   on Runtime, moved in one Git atomic tree update. See
   `perf-dormant-workflow-retirement-2026-10-08.md` on perf branch.
3. MiniObjcopy: **3 → 1** workflow **on both development branches**.
   `miniobjcopy-strip-regressions-v1.yml` retains original automatic
   focused regressions and two independent, manual full-Linux/byte-exact
   Image jobs under a `mode` choice. Both old YAMLs were archived with
   original blob SHA. The historical exact-Image job still depends on
   source run `33623125809`: do not claim current reproducibility
   without refreshing its artifact fixture.
   See `miniobjcopy-workflow-convergence-2026-10-08.md`.
4. MiniPP: **3 → 1** live-Kbuild workflow **on both development branches**.
   `minipp-linux-exact-smoke.yml` now houses separate smoke2, batch18,
   batch72 jobs with original commands and distinct evidence artifacts.
   Old batch18 and batch72 workflows archived exactly. Frozen full corpus,
   focused replay, and MiniPP A0 gates stay independent.
   See `minipp-live-exact-convergence-2026-10-08.md`.

MiniObjcopy and MiniPP consolidated workflow Git blobs are identical
between runtime/performance after the `72` input was quoted as a YAML
string. These are workflow-only changes, **not** semantic coverage
certification for heavyweight specialized paths.

5. Runtime focused diagnostics: **spinlock 4→1**, **FDT 2→1**,
   **kallsyms 3→1** on both development branches. All 9 original jobs
   remain independent; six retired workflow YAMLs have exact matching
   archived blob SHA. New job-level manual-mode and explicit commit-tag
   selectors prevent accidental heavy Runtime execution. The six
   original executable job bodies were also byte-compared as unchanged.
   See [focused runtime convergence](runtime-focused-workflow-convergence-2026-10-09.md).

6. MiniAS focused/window manual diagnostics: **2 → 1** on both development
   branches. All six jobs preserved; distinct full 3536 exact and
   semantic oracles remain active. Archived Runtime and performance
   historical YAMLs have distinct original Git SHA because only perf
   previously listened on deleted branches.
   See [MiniAS focused/window ledger](minias-focused-window-convergence-2026-10-09.md).

7. Two frozen Core suites converged without discarding distinct
   predicates: full3352 compile and GNU assembly 2→1 (6 jobs); strict500
   and focused-five 2→1 (2 jobs). Exact legacy blobs preserved; old
   compile/assembly bodies compared; opt-in trigger tags moved from
   the deleted refactor branch to the two current development refs.
8. Five obsolete-only push triggers were repaired without changing
   job shell commands. Four heavy jobs remain opt-in; MiniPP A0
   runs for relevant preprocessor changes. All changes mirrored on both
   dev branches. The frozen MiniAS run sources still have nine
   unexpired artifacts in aggregate; historical MiniObjcopy exact Image
   source run 33623125809 currently has zero artifacts.

## Verifications
- [M0 37861656075](https://github.com/yituanxing/minic-toolchain/actions/runs/37861656075):
  YAML structure, exact Core archive SHAs/job executable bodies,
  five repaired trigger liveness checks, previous MiniAS/Runtime
  provenance all **PASS**. Heavy 3352/3536/Linux-QEMU jobs were not
  executed by this gate.
- [M0 37860392307](https://github.com/yituanxing/minic-toolchain/actions/runs/37860392307):
  MiniAS YAML parse, archived Git SHA, and unchanged window job bodies
  **PASS** on the restored valid structural M0 source. Intermediate M0
  iterations exposed and repaired mistakes in the *checker implementation*,
  not in the compiler/assembler or restored MiniAS job bodies.
- [M0 37807388796](https://github.com/yituanxing/minic-toolchain/actions/runs/37807388796):
  final Linux focused YAML parse, six archived Git SHA identities and
  six original job-body equivalence checks **PASS**. First checker
  attempt 37807336727 failed due to over-escaped newline literals in
  the checker (not runtime compiler); the follow-up fixed those literals
  and passed.

- [M0 37802876581](https://github.com/yituanxing/minic-toolchain/actions/runs/37802876581):
  first four distributed retirement SHA checks and independent Image
  workflow structure PASS.
- [M0 37804990761](https://github.com/yituanxing/minic-toolchain/actions/runs/37804990761):
  MiniObjcopy merged YAML parses and exact archived job SHA PASS.
- [M0 37805462637](https://github.com/yituanxing/minic-toolchain/actions/runs/37805462637):
  MiniPP + MiniObjcopy + distributed workflow YAML and exact archive
  SHA structural checks PASS (no Linux build). Linux-runtime object set
  and prefix-bisect unit checks and Kbuild wrapper tests also PASS.
- Git diff of the cross-branch focused merges changed **no**
  `src/`, `include/`, `tests/`, or `tools/` sources. Performance
  sources remain on their own independent branch.
- Crucial distinction: last [strict receiver V4
  37796369453](https://github.com/yituanxing/minic-toolchain/actions/runs/37796369453)
  showed 3352 reused MiniC C targets, successful Image link,
  **QEMU boot timeout after OpenSBI**. Its result cannot be promoted
  to an all-green Linux runtime certificate.


## Opt-in execution of relocated specialty tests

The original mode-selectable jobs remain accessible by GitHub's
`workflow_dispatch` where that event is enabled for the target ref.
Because these files live on non-default development branches, the
same jobs now also support **explicit push commit-message tags**:

- `[miniobjcopy-strip-regressions]` → canonical basic regressions.
- `[miniobjcopy-linux-tool]` → heavy Linux Kbuild MiniObjcopy/MiniLD boundary.
- `[miniobjcopy-linux-image]` → legacy exact frozen GNU/MiniObjcopy Image comparison
  (still needs historical artifact run 33623125809; may fail to restore).
- `[minipp-linux-smoke]`, `[minipp-linux-batch]`,
  `[minipp-linux-72]` → independent live-Kbuild cohorts.

MiniObjcopy tags are gated by a push on the active runtime/perf refs.
MiniPP tags require a push touching one of the workflow's scoped
preprocessor/test/workflow paths. Untagged pushes skip the expensive
specialty jobs; they may still appear as skipped Actions runs.
These dispatch routes have **not** been end-to-end tested; the structural
[M0 #37806035979](https://github.com/yituanxing/minic-toolchain/actions/runs/37806035979)
passed YAML parsing and exact archive checks after the change.

## Deliberate non-retirements

Remaining workflow groups have test contracts **not shown redundant**:

- Linux runtime GCC/frozen fixture/link/QEMU oracle, FDT/kallsyms,
  fault-context and focused owner/frontier chains: hold until a
  current full owner run proves redundancy or their unique fault context
  is captured by a superseding workflow.
- `linux-expanded-pi-runtime-v0.yml`, `linux-expanded-runtime-p1-v0.yml`,
  `linux-image-qemu-runtime-v0.yml`: unresolved historical failure
  obligations; do not retire based on titles.
- MiniAR Kbuild integration, MiniLD real static/dynamic and relocatable
  boundaries, MiniAS 3536 plus native35/C149 and the manual diagnostic
  modes: still distinct from basic regression unit tests.
- Frozen 3352 MiniC corpus, focused first500/five, and distributor full
  Linux object compilation have different identities and cannot replace
  each other without explicit proof.
- MiniPP live-Kbuild diagnostics merged; frozen exact corpus and A0
  remain independent.

Before Linux runtime bug work resumes, revalidate the consolidated
self-producing full Image path on a current exact compiler identity,
preserve GCC mixed-object runtime frontier provenance, and record current
QEMU failing first-fault evidence. Do not reactivate temporary
pinned-run workflow experiments while the maintained chains are available.
