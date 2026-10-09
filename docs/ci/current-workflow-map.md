# Current CI workflow map

> **Current source-verified CI snapshot — 2026-10-09.** Active workflow definitions: **40 Runtime**, **41 Performance**, **44 distinct names** across the two development branches, with **81 rows** in the [cross-branch source inventory](active-workflow-inventory-2026-10-09.tsv) (canonical current copy on the Runtime branch). The new `linux-expanded-pi-p1-runtime-v1.yml` unifies two legacy runtime probes without dropping their independent job bodies. Five Runtime owners now reuse [one immutable fixture cache-restore action](runtime-fixture-restore-reuse-2026-10-09.md), preserving the exact original source/frozen keys and cache-miss failure behavior. [Runtime M0 #37872841930](https://github.com/yituanxing/minic-toolchain/actions/runs/37872841930) and [Performance M0 #37872857343](https://github.com/yituanxing/minic-toolchain/actions/runs/37872857343) both **SUCCESS**. Real migrated fixture consumer jobs also returned SUCCESS for [Timer #37872681398](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681398), [RISC-V Init Codegen #37872681417](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681417), and four [Owner Focused jobs #37872681214](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681214). These are **component diagnostics**, not a new full-MiniC Linux Image or QEMU boot certificate; RCU was still in progress at this checkpoint.

> **Current source-verified CI snapshot — 2026-10-09 (post-component convergence):** **46 active Runtime / 47 active performance workflow YAMLs; 50 distinct active names** across both development branches. Original archived definitions remain recoverable. The maintained consolidation entrypoints are `minild-integration-v1.yml` (dynamic/REL/static jobs), `minipp-linux-frozen-v1.yml` (exact/focus jobs) and Runtime-only `linux-runtime-optin-perf-suite-v1.yml` (first500/all3352/constant-p). Each migrated job body is checked against byte-preserved original Git blobs by M0. The performance First500 A/B now requires manual dispatch or an explicit `[perf-first500]` push tag, avoiding unrelated `tools/ci/**` pushes. [Runtime M0 #37865474124](https://github.com/yituanxing/minic-toolchain/actions/runs/37865474124) SUCCESS and [performance M0 #37865241632](https://github.com/yituanxing/minic-toolchain/actions/runs/37865241632) SUCCESS for structure checks; **no new T2/T3/T4 whole-Image/QEMU certification is implied**. Note: performance M0's historical full compiler test is now explicit manual `mode: full` after previously failing `check-static-functions` during an unintended push; that failure remains unresolved. See [current cross-branch workflow inventory](active-workflow-inventory-2026-10-09.tsv), [MiniLD ledger](minild-integration-convergence-2026-10-09.md), [MiniPP frozen ledger](minipp-frozen-workflow-convergence-2026-10-09.md), and [Runtime candidate ledger](linux-performance-optin-workflow-convergence-2026-10-09.md) (the latter exists on the Runtime branch). Historical counts below are older snapshots.

> **Current state — 2026-10-09:** **50 active performance / 51 active Runtime** workflows, **55 unique active names**, **277 archived files per development branch**. Core compile/assemble 3352 and strict500/focused-five were both combined without discarding individual job proofs; five obsolete push branches repaired. MiniAS frozen artifact runs remain available until late November, but the historical MiniObjcopy GNU Image oracle run now has zero artifacts. [M0 37861656075](https://github.com/yituanxing/minic-toolchain/actions/runs/37861656075) SUCCESS on Runtime structural checks. See [cross-branch ledger](workflow-cross-branch-convergence-2026-10-08.md), [Core full3352](linux-core-full3352-workflow-convergence-2026-10-09.md) and [artifact audit](workflow-liveness-artifact-audit-2026-10-09.md). Earlier figures in this document are older snapshots.

> **Current inventory, 2026-10-09:** **52 performance / 53 Runtime** active workflow files, **57 distinct active workflow names**, and **275 archived Git-preserved historical files on each branch**. Six-job MiniAS focused/frozen-window consolidation verified by [M0 run 37860392307](https://github.com/yituanxing/minic-toolchain/actions/runs/37860392307); both full 3536 assembly-exact and semantic oracles remain. See [cross-branch ledger](workflow-cross-branch-convergence-2026-10-08.md) and [MiniAS ledger](minias-focused-window-convergence-2026-10-09.md). Previous inventory banners are dated snapshots, not current counts.

> **Verified current state — 2026-10-09:** **53 performance / 54 Runtime** active workflows, **58 distinct active names** across both branches and **274 archived Git-identical historical workflow files on each**. Runtime spinlock, FDT and kallsyms focused workflows were converged 9→3 while keeping all nine independent job bodies. [Shared convergence ledger](workflow-cross-branch-convergence-2026-10-08.md), [focused Runtime scope ledger](runtime-focused-workflow-convergence-2026-10-09.md), and [M0 37807388796](https://github.com/yituanxing/minic-toolchain/actions/runs/37807388796). Earlier 59/60 and 87 counts below are dated snapshots. This change does not merge compiler sources or certify Linux boot.

> **Current cross-branch status, 2026-10-08:** this performance branch has **59** active workflow definitions, and the Runtime branch has **60**, with **64 unique active workflow names** across both. The performance branch has **268** exact-archived YAML files (including 43 nested historical). The earlier 87/185 figures below are from a much older snapshot; do not use them for current cleanup. Details and safe preservation checks: [cross-branch convergence ledger](workflow-cross-branch-convergence-2026-10-08.md). Unique current performance tests (object-interval Top5, GNU constant-p ICE, optimized first500, parser ordinary namespace first500) remain active; compiler source is unchanged.

Snapshot: `72d632d43ef2fa02ec029f539105533203b5ac50`

This document is the current ownership map for active GitHub Actions after the 2026-10 cleanup and runtime-convergence work. It is intended to prevent historical diagnostic workflows from becoming active CI by accident and to make retirement decisions contract-based rather than name-based.

## Repository state

- Active workflow YAML files: **87**
- Disabled historical workflow YAML files: **185**
- Active `linux-*.yml` workflows: **36**
- Linux/core workflows including `core-first500-regression.yml`: **37**
- `agent/ci-runtime-cleanup-v1` and `agent/linux-expanded-kbuild-v0` point to the same HEAD.
- Remaining branches:
  - `main`
  - `agent/linux-expanded-kbuild-v0`
  - `agent/ci-runtime-cleanup-v1`
  - `archive/all-progress-2026-10-04`
- The development/cleanup HEAD is 3860 commits ahead of `main` and 2 commits behind it.
- The two `main`-only commits have no net file diff relative to their merge base, so they are historical ancestry to preserve rather than a competing code state.

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
| Linux/core/runtime | 37 | frozen Linux corpus, Kbuild, runtime certification and focused Linux diagnostics |
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
| **Total** | **87** | |

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

- No active workflow references a deleted branch.
- Cleanup and canonical runtime branches are kept at the same HEAD during the convergence phase.
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
6. Delete `agent/ci-runtime-cleanup-v1` after `main` and `agent/linux-expanded-kbuild-v0` contain the validated result.
7. Keep `archive/all-progress-2026-10-04` as the historical branch umbrella.

The intended final branch set is therefore:

- `main`
- `agent/linux-expanded-kbuild-v0`
- `archive/all-progress-2026-10-04`
