# Current CI workflow map

> **Current verified CI inventory — 2026-10-09:** **30 active Runtime**, **31 active Performance**, **34 distinct workflow names**, **61 cross-branch YAMLs**. Runtime early PI/P1/Trace + compiler semantics/QEMU watcher are seven independent jobs in `linux-expanded-pi-p1-runtime-v1.yml`; FDT + generated Kallsyms are five independent jobs in `linux-runtime-fdt-isolation-v1.yml`; Core Strict500/Focused Five + Full3352 Compile/Assemble are eight independent jobs in `linux-core-all3352.yml`. The previous canonical YAMLs are byte-preserved under `.github/workflows-disabled/` and complete test bodies are source-checked by M0. [Runtime M0 #37877686545](https://github.com/yituanxing/minic-toolchain/actions/runs/37877686545) and [Performance M0 #37877704588](https://github.com/yituanxing/minic-toolchain/actions/runs/37877704588) both **SUCCESS**. This is **T0 structural proof only**; no new full 3352-TU proof, strict Image link or full-MiniC QEMU boot PASS was obtained. Current [61-row source inventory](https://github.com/yituanxing/minic-toolchain/blob/agent/linux-expanded-kbuild-v0/docs/ci/active-workflow-inventory-2026-10-09.tsv); owner proof ledgers: [early Runtime](early-runtime-diagnostic-union-2026-10-09.md), [FDT/Kallsyms](runtime-fdt-kallsyms-convergence-2026-10-09.md), [Core 3352](core-first500-full3352-convergence-2026-10-09.md). Older counts in historical sections below are not current.

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

