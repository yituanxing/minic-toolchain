# Current CI workflow map

> **Latest verified CI snapshot — 2026-10-09:** **35 Runtime / 36 Performance active YAMLs; 39 distinct active workflow names; 71 cross-branch source rows.** The MiniAS `minias-a0-focused-diagnostics-v1.yml` now owns all ten independent focused, frozen-window and 3536-semantic-oracle jobs; `minias-a0-gate-v1.yml` remains a separate exact acceptance gate. The new `linux-runtime-fixture-producers-v1.yml` contains distinct `frozen-cert` and `compact-cert` jobs with the original immutable fixture identities. Originals are SHA-preserved in `.github/workflows-disabled/`. Both [Runtime M0 #37874669925](https://github.com/yituanxing/minic-toolchain/actions/runs/37874669925) and [Performance M0 #37874690722](https://github.com/yituanxing/minic-toolchain/actions/runs/37874690722) **SUCCESS**. These validate archival/source/trigger integrity, **not** a fresh 3536 semantic corpus run, linked Image certificate, or Linux boot. The [source ledger](https://github.com/yituanxing/minic-toolchain/blob/agent/linux-expanded-kbuild-v0/docs/ci/active-workflow-inventory-2026-10-09.tsv) now has 71 rows. Earlier status banners below this one are historical.

> **Current CI checkpoint — 2026-10-09:** **37 Runtime / 38 Performance active workflows; 41 distinct names** and **75 branch-path rows** in the [current source inventory](active-workflow-inventory-2026-10-09.tsv) (canonical Runtime branch copy). The EFI Stub/vDSO owner `linux-efi-vdso-focused-v1.yml` keeps two independent test jobs. The compiler-semantics/QEMU-watch owner `linux-runtime-contract-oracles-v1.yml` keeps four independent jobs and distinct cancellation groups. The Expanded PI/P1 owner now also has a **branch-specific** Entry Trace job: both archived originals and both different linked-fixture strategies remain intact. [Runtime M0 #37873896292](https://github.com/yituanxing/minic-toolchain/actions/runs/37873896292) **SUCCESS**; [Performance M0 #37873908589](https://github.com/yituanxing/minic-toolchain/actions/runs/37873908589) **SUCCESS**. These are T0 proofs, **not** new T2/T3/T4 real corpus, Image or boot certificates. Prior count and run-status paragraphs below this banner are dated history. See [low-level oracles convergence](linux-low-level-oracle-convergence-2026-10-09.md) and [branch-specific Trace convergence](linux-expanded-trace-convergence-2026-10-09.md).

> **Current source-verified CI snapshot — 2026-10-09.** Active workflow definitions: **40 Runtime**, **41 Performance**, **44 distinct names** across the two development branches, with **81 rows** in the [cross-branch source inventory](active-workflow-inventory-2026-10-09.tsv) (canonical current copy on the Runtime branch). The new `linux-expanded-pi-p1-runtime-v1.yml` unifies two legacy runtime probes without dropping their independent job bodies. Five Runtime owners now reuse [one immutable fixture cache-restore action](runtime-fixture-restore-reuse-2026-10-09.md), preserving the exact original source/frozen keys and cache-miss failure behavior. [Runtime M0 #37872841930](https://github.com/yituanxing/minic-toolchain/actions/runs/37872841930) and [Performance M0 #37872857343](https://github.com/yituanxing/minic-toolchain/actions/runs/37872857343) both **SUCCESS**. Real migrated fixture consumer jobs also returned SUCCESS for [Timer #37872681398](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681398), [RISC-V Init Codegen #37872681417](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681417), and four [Owner Focused jobs #37872681214](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681214). These are **component diagnostics**, not a new full-MiniC Linux Image or QEMU boot certificate; RCU was still in progress at this checkpoint.

> **Current source-verified CI snapshot — 2026-10-09 (post-component convergence):** **46 active Runtime / 47 active performance workflow YAMLs; 50 distinct active names** across both development branches. Original archived definitions remain recoverable. The maintained consolidation entrypoints are `minild-integration-v1.yml` (dynamic/REL/static jobs), `minipp-linux-frozen-v1.yml` (exact/focus jobs) and Runtime-only `linux-runtime-optin-perf-suite-v1.yml` (first500/all3352/constant-p). Each migrated job body is checked against byte-preserved original Git blobs by M0. The performance First500 A/B now requires manual dispatch or an explicit `[perf-first500]` push tag, avoiding unrelated `tools/ci/**` pushes. [Runtime M0 #37865474124](https://github.com/yituanxing/minic-toolchain/actions/runs/37865474124) SUCCESS and [performance M0 #37865241632](https://github.com/yituanxing/minic-toolchain/actions/runs/37865241632) SUCCESS for structure checks; **no new T2/T3/T4 whole-Image/QEMU certification is implied**. Note: performance M0's historical full compiler test is now explicit manual `mode: full` after previously failing `check-static-functions` during an unintended push; that failure remains unresolved. See [current cross-branch workflow inventory](active-workflow-inventory-2026-10-09.tsv), [MiniLD ledger](minild-integration-convergence-2026-10-09.md), [MiniPP frozen ledger](minipp-frozen-workflow-convergence-2026-10-09.md), and [Runtime candidate ledger](linux-performance-optin-workflow-convergence-2026-10-09.md) (the latter exists on the Runtime branch). Historical counts below are older snapshots.

> **Current audited state — 2026-10-09:** **51 active Runtime / 50 active performance** workflows, **55 unique active names** across development branches, **277 archived historical YAMLs per branch**, **4 remote branches**. Core full3352 compile+assemble and Core strict500+focused-five were each merged 2→1 with distinct job predicates retained. Five deleted-branch-only workflow triggers were repaired and MiniAS old fixed artifacts were verified as available through November 26–27; the old MiniObjcopy Image artifact source has **zero** downloadable files. [M0 37861656075](https://github.com/yituanxing/minic-toolchain/actions/runs/37861656075) **SUCCESS** for structural, provenance and trigger assertions. See the [Core corpus ledger](linux-core-full3352-workflow-convergence-2026-10-09.md), [Core focused ledger](core-strict500-focused-five-convergence-2026-10-09.md), and [artifact/trigger audit](workflow-liveness-artifact-audit-2026-10-09.md). Earlier numeric banners below are historical. No new complete Image or QEMU boot certification is claimed.

> **Latest inventory, 2026-10-09 (MiniAS A0 consolidation):** **53 active Runtime / 52 active performance** workflow files; **57 distinct active names** across both branches and **275 archived historical files per branch**. The focused real16/frontier/vector33 and seven-window MiniAS diagnostic entrypoints were consolidated **2 → 1** with all six original jobs retained and [M0 #37860392307](https://github.com/yituanxing/minic-toolchain/actions/runs/37860392307) **SUCCESS** after restoring the checker from the prior green version. Full 3536 exact and semantic oracle gates remain **distinct**, as do the Linux Core focused-five, all3352 and GNU-assembly tests whose assertions are not equivalent. [MiniAS convergence ledger](minias-focused-window-convergence-2026-10-09.md). Older 54/53 and 60/59 counts below are historical.

> **Latest verified state — 2026-10-09:** **54 Runtime + 53 performance** active workflow definitions, **58 distinct active workflow names** across both branches, and **274 exact-archived files on each branch** (231 root + 43 nested). The three Linux focused diagnostic families were collapsed **9 workflows → 3** while retaining nine original job bodies. [Focused convergence source/SHA ledger](runtime-focused-workflow-convergence-2026-10-09.md); [verified M0 run 37807388796](https://github.com/yituanxing/minic-toolchain/actions/runs/37807388796) SUCCESS after fixing an escaping bug in the checker. Historical counters below are dated snapshots, not the current repository inventory. No full Linux Image or QEMU re-certification was run.

> **Latest two-branch audit, 2026-10-08:** **60 active Runtime / 59 active performance** workflow definitions; **64 distinct names** across development branches, **268 byte-preserved disabled workflow files** on each branch, and the same four branch refs. Since the initial 68/91 inventory, safe workflow retirements and job-preserving MiniObjcopy/MiniPP merges reduced 40 branch-level active definitions. New [cross-branch convergence ledger](workflow-cross-branch-convergence-2026-10-08.md) records SHA provenance, exact scopes, M0 run [37805462637](https://github.com/yituanxing/minic-toolchain/actions/runs/37805462637) SUCCESS, and why several focused runtime contracts remain active. This is **CI structural certification**, not a new full Linux Image/QEMU boot certificate. Prior 64/63 and older counts below are historical snapshots.

> **Cross-branch workflow convergence, 2026-10-08:** Runtime now has **64** active workflow YAMLs, performance has **63**; the two branches together have **68 distinct active workflow filenames**, versus **100** before the two cleanup batches (68 Runtime + 32 previously perf-only). The performance branch retired 28 exact-sha historical workflows in a single atomic commit [bf41acfc](https://github.com/yituanxing/minic-toolchain/commit/bf41acfc34e14cddafd9686d069076a25c2c5e44), preserving all four performance-only validations and untouched performance/compiler source. This supersedes all older performance **91** counts below; see `docs/ci/perf-dormant-workflow-retirement-2026-10-08.md` on the perf branch.

> **Latest 2026-10-08 verified archive retirement:** **64 active**, **264 disabled** YAML workflows on `agent/linux-expanded-kbuild-v0` (221 root-level disabled + 43 nested historical). Four independent older distributed workflows were retired **verbatim** after scanning all 68 previously active workflows for their artifact/producer references. The new canonical full Image strict replay remains active and self-generating; graph preflight remains active and independently inexpensive. See [exact SHA retirement ledger](distributed-workflow-retirement-2026-10-08.md). The later 68/260, 65/259 and other counts below are dated snapshots rather than the current inventory. Full-Image strict receiver link is verified, but Linux QEMU boot still **fails** and the consolidated seven-shard workflow itself has not been rerun on its new YAML.

> **Pre-runtime audit refresh (2026-10-08; after strict receiver V4):** GitHub reports **4** remote branches, **68** active Runtime workflow YAML files, **260** disabled historical workflow files (217 top-level + 38 + 5 nested), and **91** active workflow files on the separate performance branch. The previous 65/259, 84/194, and 91/187 figures below are earlier checkpoints, not the present inventory. See [pre-runtime CI readiness](pre-runtime-ci-readiness-2026-10-08.md) for branch constraints, exact Image/replay evidence, the consolidated self-contained seven-shard workflow owner, and gates remaining before runtime work. The current full MiniC Image is linked but **not boot-certified**.

> **Verified inventory update (2026-10-08, runtime HEAD 80f65edd5348 + workflow-only retirement): 65 active workflows, 259 preserved disabled workflows (including nested historical folders), 42 active `linux-*.yml` workflows.** Remote branch refs remain four. This overrides the historical 91/187 and 84/194 counts retained below for audit context. See [external workflow retirement and verified 3352 PASS](external-workflow-retirement-and-3352-pass-2026-10-08.md) and [dormant MiniAS/MiniLD workflows](dormant-minias-minild-retirement-2026-10-08.md). **The new 55-patch profile compiled 3352/3352 valid Linux TUs; this is NOT yet a certified Linux Image/QEMU boot.**

> **Current update (2026-10-08):** After the 7-workflow dead-owner retirement, this runtime branch has **84 active / 194 disabled** workflow YAMLs (source before cleanup 91 / 187). The historical 87-workflow role breakdown below is NOT the current inventory. See [2026-10-08 deadref/verdict audit](workflow-deadrefs-and-verdict-2026-10-08.md) for exact SHA, retirement reason, two currently failing Linux TUs and outstanding gates. Full Linux Image/QEMU certification has not been refreshed for the candidate profile.

Snapshot: initial workflow ownership map established at `72d632d43ef2fa02ec029f539105533203b5ac50`, refreshed on **2026-10-08** after verified four-branch consolidation and verified [120-object cross-Runner proof](https://github.com/yituanxing/minic-toolchain/actions/runs/37762351340). Current active/disabled inventory reflects the post-retirement runtime branch; the historical runtime roles below have not been re-certified on a new full Linux Image.

This document is the current ownership map for active GitHub Actions after the 2026-10 cleanup and runtime-convergence work. It is intended to prevent historical diagnostic workflows from becoming active CI by accident and to make retirement decisions contract-based rather than name-based.

## Repository state

- Active workflow YAML files: **91** (includes the 120-object distributed Kbuild experiment and three opt-in performance/correctness workflows).
- Disabled historical workflow YAML files: **187** (including the retired one-shot branch consolidation and the now superseded 20-object distributed Kbuild experiment).
- Active `linux-*.yml` workflows: **40**
- Linux/core workflows including `core-first500-regression.yml`: **41**
- Remote branch refs remaining after verified deletion: **4** (previously 29):
  - `main` — historical default/stable branch, not yet promoted to the current runtime head.
  - `agent/linux-expanded-kbuild-v0` — canonical Linux Runtime development/ref/cache owner.
  - `agent/linux-perf-boolean-domain-v1` — separate MiniC performance/correctness workstream, with 91 active workflow YAML files at its 2026-10-08 cleanup tip.
  - `archive/all-progress-2026-10-04` — passive archive containing all 27 captured experiment tips (zero active workflows in its HEAD worktree).
- `agent/ci-runtime-cleanup-v1` was fast-forwarded to the runtime HEAD and subsequently deleted as a redundant branch ref; both the commit history and earlier CI evidence remain in the archive.
- Do not confuse the archive *ancestry* with a production source merge. Recent performance fixes and Linux Runtime changes remain on separate development branches.

GitHub Actions caches used by the Linux runtime certification chain are scoped to the canonical runtime ref. Cache-dependent runtime validation must remain strict and must not turn cache misses into passing evidence.

## Retirement policy

An active workflow may be retired only when at least one of the following is true:

1. a newer active workflow carries the same invariant and has a successful certification run;
2. the workflow is tied only to a deleted branch or a historical fault that the current frontier has passed;
3. the workflow is an orphan producer with no active cache/fixture consumer;
4. its unique assertion has first been migrated into a maintained canonical workflow and re-certified.

Retirement means a byte-for-byte move into `.github/workflows-disabled/` plus a contract/retirement note. Historical commits, runs, artifacts, caches, and evidence are not deleted.

A workflow with unresolved failure evidence is not retired merely because it is old.

## Active inventory summary

| Domain | Count | Role |
| --- | ---: | --- |
| Linux/core/runtime | 41 | frozen Linux corpus, Kbuild, runtime certification and focused Linux diagnostics |
| BusyBox | 4 | integration/failure-pool/full-toolchain coverage |
| Compiler bootstrap/runtime | 6 | bootstrap and compiler-runtime progression |
| Toolchain/meta | 3 | stage2 smoke, M0 structure and regression ledger |
| MiniC | 4 | driver, headers, MiniAS/Linux runtime and RV64 focused regressions |
| MiniPP | 6 | A0, exact and focused Linux preprocessing |
| MiniAS | 8 | A0, frozen-window, Linux inventories/sidecars and semantic oracle |
| MiniAR | 2 | regressions and Linux Kbuild integration |
| MiniLD | 7 | regressions, static/dynamic and workload-specific linker coverage |
| MiniObjcopy/MiniStrip | 3 | regression and Linux image/tool gates |
| Lua | 1 | full driver integration |
| SQLite | 1 | full driver integration |
| TinyCC | 1 | full driver integration |
| Static readiness | 4 | bootstrap, core, Linux-final and self-host readiness |
| **Total** | **91** | |

# Linux/core ownership map

## Frozen Linux core corpus

These are distinct and remain active.

- `core-first500-regression.yml` — frozen first-500 regression surface.
- `linux-core-shards-v1.yml` — canonical frozen corpus gate for global indices 500-3351; owns the six historical shard ranges and the optional TU 1573 sanitizer replay.
- `linux-core-all3352.yml` — independent exact replay of the complete 3352-TU frozen corpus.
- `linux-core-assemble-all3352.yml` — independent compile-and-assemble coverage across the complete corpus.
- `linux-core-focused-five.yml` — five cross-shard focused regressions and exact failing-source context.

`core-shards-v1-contract.md` explicitly states that the canonical shard matrix does not replace all-3352, assemble-all-3352, focused-five, or first500 coverage.

## Whole-Kbuild frontier

- `linux-expanded-kbuild-v0.yml` — mixed/focused real-Kbuild frontier and current Linux Image producer. This is not replaced by the frozen corpus gates.
- `linux-distributed-kbuild-objects120-v1.yml` — isolated, path-triggered six-runner true Kbuild .o/.cmd transfer and no-rebuild experiment; [run 37762351340](https://github.com/yituanxing/minic-toolchain/actions/runs/37762351340) proved 120/120 object reuse and RISC-V partial link. It does **not** supersede full Image or QEMU gates. The older 20-object test is an exact target subset and was retired unchanged.

## Opt-in performance profile validations

The opt-in 55-patch performance candidate retains dedicated 500-TU, all3352-TU, and GNU constant-p ICE/QEMU workflows. It does not automatically modify the previously certified 39-patch Linux Image producer. These three newly active workflows are accounted for above.

## Runtime foundation and certification

These are the maintained runtime infrastructure and should be treated as canonical unless their contract is deliberately migrated.

- `linux-runtime-gcc-baseline.yml` — GCC runtime oracle/baseline.
- `linux-runtime-frozen-cert-v1.yml` — certified frozen runtime input producer.
- `linux-runtime-link-fixture-cert-v1.yml` — canonical terminal-link fixture certification; owns compact/full equivalence and V2/legacy-V1 link equivalence.
- `linux-runtime-qemu-watch-contracts-v1.yml` — fault-aware/inconclusive watcher contract.
- `linux-runtime-frontier-v1.yml` — full frontier refresh/certification path.
- `linux-runtime-frontier-fast-v5.yml` — compact/frozen fast frontier path.
- `linux-runtime-mm-core-frontier-v0.yml` — maintained init-IRQ/mm-core bridge.
- `linux-runtime-owner-focused-v1.yml` — canonical matrix for timekeeping, vsyscall, notifier and build-policy owner chains; also owns the current `update_vsyscall < 2256` frame contract.
- `linux-runtime-compiler-semantics-v1.yml` — canonical PI-local-symbol and SATP micro compiler-semantics contract.
- `linux-runtime-satp-refresh-v0.yml` — canonical real PI/SATP/setup owner differential, CSR-window and source-level no-`sp` contract.
- `linux-runtime-fault-context-v1.yml` — generic focused current-fault context capture.

## Kallsyms, FDT and object-code differentials

These retain separate static or runtime questions and remain active.

- `linux-runtime-first-die-context-v0.yml` — cached/GCC/current-MiniC kallsyms three-way first-fault comparison.
- `linux-runtime-generated-kallsyms-first-die-v0.yml` — current generated-kallsyms first-die certification.
- `linux-runtime-kallsyms-object-diagnose-v0.yml` — detailed cached/GCC/MiniC object provenance, relocation and code-generation evidence.
- `linux-runtime-fdt-isolation-v1.yml` — runtime FDT owner isolation.
- `linux-runtime-fdt-ro-codegen-v0.yml` — exact-context MiniC/GCC `fdt_ro.o` code-generation comparison.
- `linux-runtime-riscv-init-codegen-v0.yml` — exact-context MiniC/GCC RISC-V init object comparison.

The current generated-kallsyms/FDT chain has progressed beyond the historical crc32 fault, so the old crc32-specific probes are disabled; the FDT-specific workflows remain relevant.

## Scheduler, locking, RCU and focused runtime diagnostics

These are specialized rather than general-purpose canonical gates.

- `linux-check-cpu-stall-runtime-v0.yml` — retained representative packed/reuse IRQ + RCU stall owner lane and generalized IRQ-guard page-fault rejection.
- `linux-fork-stack-runtime-v0.yml` — retains `kernel/fork.o` owner isolation and bad-stack/first-fault oracle.
- `linux-runtime-rcu-owner-v0.yml` — RCU/softirq/rest-init owner chain with kallsyms-aware runtime evidence.
- `linux-runtime-timer-focused-v0.yml` — timer/hrtimer/tick/RCU focused owner frontier.
- `linux-runtime-spinlock-codegen-v0.yml` — MiniC/GCC spinlock code-generation differential.
- `linux-runtime-spinlock-context-v0.yml` — GDB recursion lock/caller/current capture.
- `linux-runtime-spinlock-first-context-v0.yml` — canonical first spinlock validation target/context diagnostic.
- `linux-runtime-spinlock-stack-v0.yml` — canonical static spinlock stack/frame diagnostic.

The old IRQ-frame runtime lane is disabled because its generalized IRQ-guard responsibility is retained by check-cpu-stall and its SATP responsibility is owned by the canonical SATP refresh contract.

The historical `linux-idr-xarray-runtime-v0.yml` A/B lane is also disabled. Re-run `37500833588` reproduced only the old global `strlen+0x6` frontier under its stale patch stack, while current-profile Fast V5 run `37501374567` successfully rebuilt `lib/idr.o` and `lib/xarray.o`, relinked V2, and preserved the authoritative `SAME_FAULT` baseline without an IDR/XArray-specific regression.

## Full-image and architecture-specific runtime surfaces

- `linux-expanded-entry-trace-v0.yml` — explicit low-level entry tracing.
- `linux-expanded-pi-runtime-v0.yml` — historical expanded PI runtime path. Retirement is blocked until the failure evidence associated with run `34950464878` is explicitly superseded or re-certified.
- `linux-expanded-runtime-p1-v0.yml` — expanded Image P1 runtime with deterministic initramfs, layout diagnostics and 12-syscall contract. The workflow-edit run `34949261781` failed; obtain a clean current re-certification before considering retirement or consolidation.
- `linux-image-qemu-runtime-v0.yml` — minimal cached-Image QEMU runtime smoke. The workflow-edit run `34948950134` failed; do not retire solely because P1 is nominally stronger until a current P1/smoke supersession proof is recorded.
- `linux-efistub-diff-v0.yml` — independent EFI-stub differential.
- `linux-vdso-focused.yml` — independent vDSO-focused coverage.

# Non-Linux active map

## BusyBox

- `busybox-failure-pool-v0.yml`
- `busybox-full-driver-v0.yml`
- `busybox-full-toolchain-v0.yml`
- `busybox-mini-aggregation-v0.yml`

## Compiler bootstrap/runtime

- `compiler-bootstrap-b0.yml`
- `compiler-bootstrap-b1-sharded.yml`
- `compiler-bootstrap-b2-runtime.yml`
- `compiler-bootstrap-b4-linux-all3352.yml`
- `compiler-runtime-r0.yml`
- `compiler-runtime-r1-lua.yml`

## Toolchain/meta

- `stage2-linux-kbuild-cc-smoke.yml`
- `toolchain-m0-structure.yml`
- `toolchain-regression-ledger-v0.yml`

## MiniC

- `minic-driver-musl-headers-v0.yml`
- `minic-driver-v0.yml`
- `minic-minias-linux-runtime.yml`
- `minic-rv64-focused-regressions-v1.yml`

## MiniPP

- `minipp-a0.yml`
- `minipp-linux-exact-72.yml`
- `minipp-linux-exact-batch.yml`
- `minipp-linux-exact-smoke.yml`
- `minipp-linux-exact-v1.yml`
- `minipp-linux-focus-v1.yml`

The old six range workflows and the legacy Linux focus lane are disabled; the exact smoke/batch/72 paths remain separate because they are live-Kbuild diagnostics rather than frozen-range duplicates.

## MiniAS

- `minias-a0-focused-diagnostics-v1.yml`
- `minias-a0-gate-v1.yml`
- `minias-a0-window.yml`
- `minias-linux-ground-truth-inventory.yml`
- `minias-linux-runtime-gcc-single-variable.yml`
- `minias-linux-sidecars.yml`
- `minias-semantic-oracle-smoke.yml`
- `minias-semantic-oracle3536.yml`

Historical census and old Linux input-inventory workflows are disabled.

## MiniAR

- `miniar-linux-kbuild.yml`
- `miniar-regressions-v1.yml`

## MiniLD

- `minild-busybox-static.yml`
- `minild-dynamic-integration-v1.yml`
- `minild-linux-rel-boundaries-v1.yml`
- `minild-musl-shared-rebase.yml`
- `minild-regressions-v1.yml`
- `minild-sqlite-static.yml`
- `minild-static-runtime-v1.yml`

## MiniObjcopy / MiniStrip

- `miniobjcopy-linux-image-gate.yml`
- `miniobjcopy-linux-tool-gate.yml`
- `miniobjcopy-strip-regressions-v1.yml`

## Workload integration

- `lua-full-driver-v0.yml`
- `sqlite-full-driver-v0.yml`
- `tinycc-full-driver-v0.yml`

## Static readiness

- `static-readiness-bootstrap-b1.yml`
- `static-readiness-core.yml`
- `static-readiness-linux-final.yml`
- `static-readiness-toolchain-selfhost.yml`

# Cleanup invariants already established

- **Follow-up required:** some active workflow YAMLs still mention deleted branch names in historical `push.branches` filters or old cleanup-specific conditions. The branch refs are gone, but the stale strings must be audited and cleaned without broadening test triggers. Do not assume the old no-stale-reference claim still holds.
- The cleanup branch is now retired; its last tip was equal to runtime commit `1e0d5cb4e9c9671eb1e3034d17be183169752874`.
- Retired workflow YAML is preserved under `.github/workflows-disabled/`.
- A disabled-path filename collision discovered during cleanup was repaired so the original Linux discovery workflow and the later registered spinlock bridge are both preserved independently.
- Historical residual-undef consumers are disabled, and the orphan `linux-undef-diagnose-v0.yml` producer was retired only after a scan of all 89 active workflows at the preceding HEAD found zero active consumers for its cache key.
- Historical PI fast/minic-early lanes, IRQ frame lane, fast relink shadow, stale IDR/XArray A/B lane, crc32 probes, old kallsyms probes, dead-branch diagnostics and earlier owner-specific duplicates are documented by retirement contracts.

# Remaining convergence work

1. Re-certify or explicitly supersede the active workflows still protected by failure evidence:
   - `linux-expanded-pi-runtime-v0.yml`
   - `linux-expanded-runtime-p1-v0.yml`
   - `linux-image-qemu-runtime-v0.yml`
2. Run one current full-owner runtime baseline after workflow convergence so the project has one authoritative present-day frontier instead of multiple historical focused frontiers.
3. Re-run the final required toolchain/Linux certification set on the converged HEAD.
4. Merge the two `main`-only ancestry commits without replacing the converged tree.
5. Update `main` only after that merge and final certification are green.
6. **Completed 2026-10-08:** delete `agent/ci-runtime-cleanup-v1` together with 24 other exact-SHA-verified experiment refs, without promoting `main`.
7. Keep `archive/all-progress-2026-10-04` as a passive historical branch umbrella.
8. Retain `agent/linux-perf-boolean-domain-v1` until its current performance and correctness work is consciously integrated and re-certified; do not delete it just to reach three refs.
9. Audit stale references to removed branch names and remaining dormant workflow contracts before further CI retirement.

The current **four-branch** set is therefore:

- `main`
- `agent/linux-expanded-kbuild-v0`
- `agent/linux-perf-boolean-domain-v1`
- `archive/all-progress-2026-10-04`

## Verified branch convergence evidence, 2026-10-08

- One-shot run [`37751577519`](https://github.com/yituanxing/minic-toolchain/actions/runs/37751577519) finished **SUCCESS**: 25/25 SHA-leased deletion targets passed archival-ancestry checks, Git pushed 25 exact leases, and remote branch inventory checked exactly four refs.
- Historical tip-to-SHA mapping remains in `tools/ci/branch-consolidation-20261008.tsv`; the exact successful one-shot YAML was retired byte-for-byte to `.github/workflows-disabled/branch-consolidation-one-shot-v1.yml` after completion.
- The active runtime code and Linux Image/QEMU certification workflows were not modified by this retirement. Routine focused checks on the consolidation commit include MiniC RV64, MiniAR and MiniLD; a full current-head Image/QEMU certificate is still outstanding.

