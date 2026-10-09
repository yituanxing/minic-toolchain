# CI entrypoints — maintainers' quick map

> **2026-10-09 push/skip audit added:** 25 Runtime / 23 Performance active YAMLs, 171 declared jobs. The new [per-entrypoint skip/trigger audit](ci-push-skip-audit-2026-10-09.md) identifies broad unscoped tag-triggered workflows, unreachable historical tag guards, branch-scope drift, and the unconditional MiniObjcopy routing runner. M0 now exports `ci-active-entrypoint-audit.tsv` alongside the previous trigger matrix. No expensive tests or historical commit-message tags were silently disabled.

> **2026-10-09 latest T1 execution consolidation:** MiniC RV64 + MiniAR + MiniLD now run as **one shared-build runner job**, with **eight independently reported regression checks** and manual owner modes retained. Physical declared Jobs **171 total** (Runtime **87**, Performance **84**), previously 175. Active YAML count is unchanged: **25 Runtime / 23 Performance / 26 distinct names / 48 branch-path entries**. New T1 real CI results must be checked separately. See [single-build implementation and source proof](ci-toolchain-focused-single-build-2026-10-09.md). Previous 175-job figures lower on this page are historical.

> **Latest 2026-10-09 Runtime focused diagnostics consolidation:** **25 Runtime / 23 Performance active workflows, 26 unique names, 48 branch-path YAMLs**. IRQ, RCU, RISC-V init codegen and Timer are now one `linux-runtime-focused-owners-v1.yml` with four **independent original diagnosis jobs** and one cheap selector. No historical fixture, linker, QEMU or output-verdict body was modified; old YAMLs are byte-preserved and M0 checks exact source equality. New actual T4 certification remains independently necessary. See [four-to-one Runtime proof](ci-runtime-focused-owners-consolidation-2026-10-09.md). Older count banners below are snapshots.

> **Latest 2026-10-09 CI snapshot: 28 Runtime / 26 Performance, 29 distinct active YAML names, 54 branch-path YAMLs.** Four Performance experiment workflows are now a single `linux-performance-experiments-v1.yml` with four preserved independent test jobs and one cheap path router. First500 retains its explicit `[perf-first500]` opt-in; expensive P runs must not be inferred from M0. See [Performance consolidation](ci-performance-experiment-consolidation-2026-10-09.md). All lower count banners are historical snapshots.

> **2026-10-09 consolidation (current):** Active workflow entrypoints now **28 Runtime / 29 Performance; 32 distinct names, 57 branch-path YAMLs.** The previously separate MiniC RV64, MiniAR and MiniLD focused regression workflows are now one `toolchain-focused-regressions-v1.yml`, with **three independent jobs and their complete original test bodies preserved**. Exact archived originals are checked by M0. Count reduction does **not** mean deletion of three distinct correctness oracles or Linux Image/QEMU certification. See [three-to-one T1 proof](ci-focused-regressions-consolidation-2026-10-09.md) and [complete T1 owner map](ci-trigger-coverage-contracts-2026-10-09.md). Older counts below are snapshots.

> **Current CI ownership and routing audit — 2026-10-09:** **30 Runtime / 31 Performance active workflows, 34 distinct names, 61 branch-path YAMLs.** CI entrypoint count was deliberately not reduced further. A real timekeeping-only push now routes only `route` + `owner (timekeeping)` ([successful run #37879734755](https://github.com/yituanxing/minic-toolchain/actions/runs/37879734755)); irrelevant MiniC RV64 / MiniAR / MiniLD builds no longer run on Runtime-only sentinels. The latest audit also fixed the **opposite, missed-trigger problem:** five `make all`-based regressions now include `elf/**` because the toolchain shares ELF reader/writer/rewrite source. [Runtime M0 #37881147237](https://github.com/yituanxing/minic-toolchain/actions/runs/37881147237) and [Performance M0 #37881158868](https://github.com/yituanxing/minic-toolchain/actions/runs/37881158868) **SUCCESS** with original complete job-body SHA proofs. Real Runtime MiniC RV64, MiniAR, MiniLD, MiniPP A0 and Performance MiniPP A0 checks from the repair also **SUCCESS**; `minic-driver-v0` remained opt-in and was skipped, not a green real run. The maintained [61-entry YAML inventory](active-workflow-inventory-2026-10-09.tsv) is current. [Shared ELF trigger audit](ci-shared-elf-trigger-audit-2026-10-09.md). None of this establishes a new full 3352-TU, Linux Image or QEMU boot certificate. Previous CI snapshots below are historical.

> **Current verified CI inventory — 2026-10-09:** **30 active Runtime**, **31 active Performance**, **34 distinct workflow names**, **61 cross-branch YAMLs**. Runtime early PI/P1/Trace + compiler semantics/QEMU watcher are seven independent jobs in `linux-expanded-pi-p1-runtime-v1.yml`; FDT + generated Kallsyms are five independent jobs in `linux-runtime-fdt-isolation-v1.yml`; Core Strict500/Focused Five + Full3352 Compile/Assemble are eight independent jobs in `linux-core-all3352.yml`. The previous canonical YAMLs are byte-preserved under `.github/workflows-disabled/` and complete test bodies are source-checked by M0. [Runtime M0 #37877686545](https://github.com/yituanxing/minic-toolchain/actions/runs/37877686545) and [Performance M0 #37877704588](https://github.com/yituanxing/minic-toolchain/actions/runs/37877704588) both **SUCCESS**. This is **T0 structural proof only**; no new full 3352-TU proof, strict Image link or full-MiniC QEMU boot PASS was obtained. Current [61-row source inventory](https://github.com/yituanxing/minic-toolchain/blob/agent/linux-expanded-kbuild-v0/docs/ci/active-workflow-inventory-2026-10-09.tsv); owner proof ledgers: [early Runtime](early-runtime-diagnostic-union-2026-10-09.md), [FDT/Kallsyms](runtime-fdt-kallsyms-convergence-2026-10-09.md), [Core 3352](core-first500-full3352-convergence-2026-10-09.md). Older counts in historical sections below are not current.

## CI execution routing: owner mode, provenance and cost

The **Runtime Owner Focused** workflow now runs a cheap preflight `route` job. On push, the original four trigger files map independently to timekeeping, vsyscall, notifier and build-policy matrix jobs. A push changing two trigger files schedules two owner jobs; a workflow/selector edit, an unrecognized diff or a missing pre-push commit conservatively schedules all four. Manual dispatch offers one mode or `all` (old default). Actual single-lane run [#37879420999](https://github.com/yituanxing/minic-toolchain/actions/runs/37879420999) succeeded.

MiniC RV64, MiniAR and MiniLD full regressions retain their original source/build/test glob and all real job assertions. They now **exclude only Runtime trigger sentinels and the owner selector script** from otherwise broad `tools/**` push matching. This is tested against original complete Git SHA for both branches and was verified by a second Runtime timekeeping trigger push with no corresponding three regression runs.

Early Runtime `semantics` and QEMU Watch mode selectors are independently verified across both development refs. **Manual Performance semantics actually runs the selected job** rather than being skipped by the historical Runtime push ref guard; a cache miss or QEMU inconclusive still fails as before. See [mode routing contract](ci-routing-repairs-2026-10-09.md) and [path isolation proof](ci-regression-path-isolation-2026-10-09.md).

## Which CI should I run?

| Question / change | First owner | Evidence type | Next escalation |
| --- | --- | --- | --- |
| CI wiring, trigger, archive, shell route | `toolchain-m0-structure.yml` | T0 structural | relevant real regression |
| Driver, frontend, Core, RV64 code | `minic-driver-v0.yml`, `toolchain-focused-regressions-v1.yml` | T1 | `linux-core-all3352.yml` (strict500, focused-five) |
| Linux frozen compiler coverage | `linux-core-all3352.yml` (eight independent compile/assembly jobs), `linux-core-shards-v1.yml` | T2 | real Kbuild and Image |
| Preprocessor semantics | `minipp-a0.yml` | T1 | `minipp-linux-frozen-v1.yml` (frozen exact/focus, live smoke/batch/72) |
| Assembler byte/semantic correctness | `minias-a0-focused-diagnostics-v1.yml` (real16, frontier, vector33, window, semantic), `minias-a0-gate-v1.yml` | T1/T2 | Full 3536 semantic mode independent of exact A0 Gate |
| AR and NM archive semantics | `toolchain-focused-regressions-v1.yml` | T1 | `miniar-linux-kbuild.yml` (real Linux+QEMU) |
| Linker ELF and relocation | `toolchain-focused-regressions-v1.yml` | T1 | `minild-integration-v1.yml` with modes dynamic, linux-rel, static |
| ELF binary/section rewriting | `miniobjcopy-strip-regressions-v1.yml` | T1/T3 | historical Image oracle requires fresh trustworthy producer |
| Full Linux Image without historical fixed run ID | `linux-distributed-image-graph-preflight-v1.yml`, `linux-distributed-full-image-signedness-v2.yml` | T3 | check strict 3352-object identity, final Image, then QEMU |
| Runtime baseline / certified inputs | `linux-runtime-gcc-baseline.yml` plus `linux-runtime-fixture-producers-v1.yml` (frozen or compact) | T3/T4 producer | verified frozen -> compact chain, distinct certificates |
| Linux first-fault investigation | `linux-runtime-focused-faults-v1.yml` (cpu-stall, fork-stack, fault-context, satp, fast, full, qemu) | T4 diagnosis | seven separately certified jobs; historical source canons archived |
| FDT / generated kallsyms early-boot evidence | `linux-runtime-fdt-isolation-v1.yml` (isolation, codegen, generated, three-way, object) | T4 | five independently certified jobs, not a full Image boot PASS |
| Linux EFI Stub / vDSO objects | `linux-efi-vdso-focused-v1.yml` (efi, vdso, all) | T2/T3 object oracle | compare original EFI/vDSO contracts independently |
| Runtime compiler semantics / QEMU watcher | `linux-expanded-pi-p1-runtime-v1.yml` (p1, pi, trace, semantics, watch) | T1/T4 | seven independent named job verdicts and original tags |
| Expanded PI/P1/Entry Trace startup diagnosis | `linux-expanded-pi-p1-runtime-v1.yml` (p1, pi, trace, all) | T4 diagnosis | branch-specific trace body preserved; GCC/frontier proof remains separate |
| Performance candidate on Runtime owner | `linux-runtime-optin-perf-suite-v1.yml` | P + T2/T4 | correct A/B + code integration after proof |
| Performance experimental branch | `linux-performance-experiments-v1.yml` (choose `first500`, `parser`, `top5`, `ice` or `all`; expensive jobs remain independent) | P and focused correctness | compare exact profiles and green correctness |

**Do not treat these as replacements for all focused owner jobs.** Runtime keeps dedicated `linux-runtime-fdt-isolation-v1.yml`, `linux-runtime-generated-kallsyms-first-die-v0.yml`, `linux-runtime-spinlock-context-v0.yml`, `linux-runtime-owner-focused-v1.yml`, `linux-runtime-contract-oracles-v1.yml`, `linux-runtime-mm-core-frontier-v0.yml`, `linux-runtime-rcu-owner-v0.yml`, `linux-runtime-timer-focused-v0.yml`, `linux-runtime-riscv-init-codegen-v0.yml`, `linux-expanded-entry-trace-v0.yml`, `linux-expanded-pi-runtime-v0.yml`, `linux-expanded-runtime-p1-v0.yml`, and `linux-efi-vdso-focused-v1.yml` because their exact static or runtime oracles are **not yet proven redundant**. `linux-expanded-kbuild-v0.yml` remains a distinct real Kbuild frontier and must not be confused with frozen TU compilation.

## Consolidated entrypoints: latest stage

- **Early Runtime PI/P1, Trace, semantics and QEMU Watch:** `linux-expanded-pi-p1-runtime-v1.yml`; the different Entry Trace body on each development branch remains independently frozen and checked.
- **FDT + generated kallsyms:** `linux-runtime-fdt-isolation-v1.yml`; the shared early-link trigger and all original tags are retained, while separate jobs have separate cancellation groups.
- **Core First500/Focused Five + compile/assembly 3352:** `linux-core-all3352.yml`; manual modes `compile`, `assemble`, `strict500`, `focused-five`, `all`. These are not replacements for live Kbuild/Image/QEMU certification.

## Remaining non-merge boundaries

The following remain **separate intentionally**, even though further file-count reductions are possible: fast MiniPP A0 vs expensive Frozen/Live exact (cheap always-on regression); MiniAS A0 Gate vs semantic 3536 (different oracle); MiniAR unit regression vs Linux Kbuild/QEMU integration (different push cost/trigger scopes); full seven-shard Image producer/replay vs any compact or frozen linker fixture. Do not combine path-triggered automatic checks with manual expensive jobs without a verified per-job changed-path gate. See [current combined-owner proofs](minipp-frozen-live-convergence-2026-10-09.md) and [Runtime diagnostic archive proof](runtime-diagnostic-union-2026-10-09.md).

## Rules for a new failure or regression

1. Identify the **tool owner** (MiniC/PP/AS/AR/LD/ELF/Objcopy/Runtime) and **test tier** (T0, T1, T2, T3, T4, P). The [taxonomy](ci-ownership-regression-taxonomy-2026-10-09.md) defines them.
2. Add a test case or a documented mode in that owner's canonical workflow, preferably in a checked-in script. Creating a permanent workflow is the exception, not the default.
3. A regression is green only after its real oracle produces a named PASS verdict. A passing M0 proves **structural** correctness, not actual Linux Image assembly, linking or boot.
4. For high-cost tests use manual `workflow_dispatch` modes or explicit opt-in push tags. Path-filter matching alone must not start full 3352 A/B or QEMU on routine CI maintenance.
5. Carry immutable provenance: source/compiler patch profile, Linux archive/config, object list/cache identity, artifact run, checksums and exact first-fault progress. Treat an expired required artifact as **BLOCKED**, never PASS.
6. Before retiring a workflow: list its unique job assertions and consumers; migrate all; retain exact source YAML in `.github/workflows-disabled/`, pin its original Git blob SHA; run M0 and a tier-matched real test.
7. Never overwrite or delete the passive `archive/all-progress-2026-10-04` recovery branch. Do not merge the performance branch into Runtime simply because the YAML definitions match.

## Shared Linux fixture restoration

The reusable local action `.github/actions/linux-runtime-restore-fixture/action.yml` owns the immutable Linux 6.6.143 source and frozen linker-subset cache restore contracts. Its exact original key/path/fail-on-miss assertions and original five complete workflow Git blob hashes are checked by `tools/ci/check_linux_runtime_fixture_restore_v1.py`. Do not casually move a compiler-profile-specific **linked Image cache** into this action: those depend on a different producing HEAD/config and branch-scoped cache identity. See [restoration migration ledger](runtime-fixture-restore-reuse-2026-10-09.md).

## Consolidated fixture and MiniAS entrypoints

- **MiniAS:** Use `minias-a0-focused-diagnostics-v1.yml` for focused/window/semantic diagnosis, choose `semantic` for 3536 semantic-oracle proof; preserve the separate A0 Gate. Original archive and validation: [MiniAS convergence](minias-semantic-convergence-2026-10-09.md).
- **Certified fixture producer:** Use `linux-runtime-fixture-producers-v1.yml`, `mode=frozen` before `mode=compact` when rebuilding; the two original cache producer jobs remain distinct and may independently fail on expired/missing upstream inputs. Original archive and validation: [producer convergence](linux-runtime-fixture-producer-convergence-2026-10-09.md).

## Current truth and known gates

- Toolchain structure after this convergence: Runtime M0 [#37867583911](https://github.com/yituanxing/minic-toolchain/actions/runs/37867583911) **SUCCESS**; performance M0 [#37867602327](https://github.com/yituanxing/minic-toolchain/actions/runs/37867602327) **SUCCESS**. Both verify the historical SHA-preserved jobs in the new Runtime combined entrypoints.
- **Not proven after consolidation:** current-head full seven-shard strict Image end-to-end run, newly migrated T2 MiniPP exact/focus reruns, actual three-mode MiniLD dynamic/Linux REL/static integration reruns, or a fresh full-MiniC QEMU boot PASS.
- MiniObjcopy historical fixed Image input run `33623125809` contains no downloadable artifact. Regenerate a certified input rather than substituting a random recent Image.
- Performance M0 historically failed on `check-static-functions` before it was separated into manual full-mode; this remains a T1 investigation, not a resolved correctness regression.
- Work still required before runtime fault repair: audit retained fixture producer/consumer/cache scope, establish current-head strict Image link proof and honest QEMU first-fault result, then integrate only independently verified optimization deltas.
- Other audit history: [current workflow map](current-workflow-map.md), [focused fault consolidation](linux-runtime-focused-faults-convergence-2026-10-09.md), [frontier consolidation](linux-runtime-frontier-diagnostics-convergence-2026-10-09.md), [liveness/artifact audit](workflow-liveness-artifact-audit-2026-10-09.md).

This is an **operations index**, not a mandate to run every maintained workflow every push. Its counts are as of the audited tips; the `.github/workflows/` directory is the current runnable list.
