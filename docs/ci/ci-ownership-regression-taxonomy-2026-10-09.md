# CI ownership and regression taxonomy (working contract) — 2026-10-09

> This document introduced the initial taxonomy before later reductions. **Latest workflow counts and entrypoints are in [CI operator index](README.md): 28 Runtime / 29 Performance / 32 distinct active names as of 2026-10-09.** Source count banners below (51/50 etc.) are historical.

Status: **historical design/audit baseline; see the active 57-branch-entry [inventory](active-workflow-inventory-2026-10-09.tsv) and [current overlap review](ci-active-overlap-cost-audit-2026-10-09.md); not proof of new Linux Image/QEMU certification**. This is the starting point for contract-preserving consolidation. No workflow may be retired solely because its filename appears under the same tool.

## Current ground truth

- Runtime: 51 active workflow YAMLs; performance: 50; 55 distinct names across the two development branches; 277 archived historical YAMLs per branch (see `current-workflow-map.md`).
- Runtime ref: `agent/linux-expanded-kbuild-v0`; performance ref: `agent/linux-perf-boolean-domain-v1`. Preserve separate branch-scoped cache/provenance until validated integration.
- Structural M0: run 37861656075 SUCCESS. Not evidence of complete 3352 Image/QEMU certification.
- Self-producing Image owner: `linux-distributed-full-image-signedness-v2.yml`: graph -> seven shards -> strict object replay/relink -> QEMU. Current consolidated HEAD has not yet received an end-to-end pass.
- MiniObjcopy historical Image oracle run 33623125809 has no downloadable artifact; its image comparison is blocked, not a compiler regression.
- Existing independent contracts must remain independent even when jobs later share a YAML file.

## Two-dimensional test model

Every test is assigned **a tool/domain owner** AND **an evidence tier**. Workflow filenames are entrypoints, not the unit of coverage.

| Evidence tier | Question answered | Normal trigger | Proof required |
| --- | --- | --- | --- |
| T0: structural/maintenance | Are workflow definitions, contracts, scripts, references and archive SHAs internally consistent? | cheap push + manual | static check log, revision |
| T1: local unit and focused regression | Does a specific operation obey its contract? | changed-path push + manual | exact test and oracle result |
| T2: differential and frozen corpus | Does a tool match GNU/GCC or pass canonical immutable corpora? | manual/targeted | source identity, count, oracle, failure list |
| T3: real integration | Does the *whole tool invocation chain* work under real Kbuild/BusyBox/etc.? | controlled opt-in | generated inputs, verified artifacts, command trace |
| T4: executable/runtime certificate | Does produced binary link, execute, and reach a defined QEMU marker? | manual/controlled | exact Image/compiler hashes, first fault or PASS marker |
| P: performance | Did the optimization improve a **same-scenario** baseline without correctness regressions? | opt-in/manual | hardware/runner, wall/CPU time, counts, HEAD, paired correctness |

A green T0 must never be reported as green T2/T3/T4.

## Toolchain component ownership (source confirmed against Makefile)

| Component / produced binary | Local responsibility | Representative existing regression target | Distinct integration obligation |
| --- | --- | --- | --- |
| MiniC driver `minic` | command invocation/staging, CLI | `minic-driver-v0.yml`, driver tests | real invocation through Kbuild wrapper |
| Compiler `minic-cc` | frontend parser/types/semantic and Core/RV64 generation | `make check-fast`, `make check-c0-runtime`, `core-first500-regression.yml` | frozen 3352 compilation, GNU assembly, full Image |
| MiniPP `minic-cpp` | preprocessing/macros/includes | `make check-minipp-a0`, `minipp-a0.yml` | frozen and live-Kbuild GCC exact output |
| MiniAS `minic-as` | RISC-V assembly to ELF | `make check-minias-a0`, `minias-a0-focused-diagnostics-v1.yml` | 3536 exact + independent semantic oracle; native sidecars |
| MiniAR `minic-ar` | static archives, regular/thin | `make check-miniar-a0`, `make check-miniar-a1` | Linux Kbuild archive consumption |
| MiniLD `minic-ld` | relocations, linker scripts, static/dynamic linking | `make check-minild-a0` through `a6`, script `a0/a1` | Linux final link, dynamic/runtime boundary |
| MiniNM `minic-nm` | ELF symbol listing/archives | `make check-mininm-a0/a1` | real archive/object inspection |
| MiniObjcopy `minic-objcopy` | ELF to raw binary, rewrite/strip sections | `make check-miniobjcopy-a0/a1` | GNU Image byte comparison; currently blocked on historical fixture |
| MiniStrip `minic-strip` | strip ELF metadata | `make check-minicstrip-a0` | real ELF consumers |
| Shared MiniELF library | object reader, writers, rewrite | `make check-minielf-reader-a0` | cross-tool contract: AS/LD/AR/NM/Objcopy/Strip |
| End-to-end Linux / Runtime | Kbuild object identity, linked Image, boot markers | `linux-core-all3352.yml`, `linux-distributed-full-image-signedness-v2.yml` | strict replay and QEMU are separate verdicts |
| Real applications | BusyBox, Lua, SQLite, TinyCC etc. | existing app gates | do not replace with synthetic C0-only tests |

**Do not confuse component owner and integration owner**: e.g. MiniLD unit tests cannot replace Linux linker integration; complete frozen 3352 C compilations cannot prove Image runtime.

## Consolidation rules

1. Each T1–T4 assertion must have exactly one **canonical owner** in the final catalogue. Related diagnostic modes can be jobs/parameters of that owner; preserve explicit named verdicts.
2. Keep cheap T0/T1 from launching T3/T4. Large work must use controlled dispatch, narrow path+tag or explicit reusable workflow entry.
3. Consolidate duplicate *setup/build logic* into scripts/reusable workflows or composites **only after** matching environment, toolchain version, cache boundaries and generated inputs. Joining YAML without deduplicating implementations is incomplete.
4. Preserve artifact provenance: producing HEAD, compiler profile/hash, Linux config/hash, object manifest, source run, expiration, and exact consumer identity. A missing artifact is **BLOCKED**, not PASS.
5. Before retiring an entrypoint: (a) enumerate all unique job assertions and active consumers; (b) migrate each assertion; (c) compare expected and actual trigger policy; (d) get a real pass at the required tier, not only M0; (e) archive original YAML byte-for-byte and record its Git blob SHA; (f) verify consumers and failure diagnosis remain accessible.
6. Treat `archive/all-progress-2026-10-04` as recovery, not as a runnable owner. Never delete historical scripts/corpora for cosmetic count improvements.
7. New fault investigations add test cases or matrix modes under an existing owner; a new top-level workflow requires a documented security, resource, credential, lifecycle, or independent certification reason.

## Proposed canonical families (not yet committed migration)

- `ci-structure`: M0, static workflow/contract and archive verification (T0).
- `compiler-regressions`: driver/frontend/Core/RV64 targeted unit suites (T1).
- `compiler-linux-corpus`: strict500 + focused five + 3352 compile + GNU-as stages (T2; separate verdict jobs).
- `preprocessor`: MiniPP A0, frozen exact, live Kbuild (T1/T2/T3).
- `assembler`: MiniAS A0/focused/frozen windows, complete 3536 exact and semantic (T1/T2).
- `elf-binutils`: MiniELF/MiniAR/MiniNM/MiniLD/Objcopy/Strip fast regressions (T1/T2), retain distinct real-Linux tool integrations (T3).
- `linux-image`: graph preflight and exact seven-shard full Image build/strict replay (T3).
- `linux-runtime`: GCC baseline, frozen/link fixtures, QEMU watcher, owner isolation and first-fault focused matrix (T4).
- `applications`: BusyBox/Lua/SQLite/TinyCC integration (T3/T4 where applicable).
- `performance`: paired First500/full3352 and parser/codegen profiles (P).

Family names describe **test ownership**, not a mandate for exactly ten YAML files or immediate mergers.

## Candidate queue and blockers (first pass)

| Candidate | Potential change | Gate / why not delete yet |
| --- | --- | --- |
| Compiler first500/3352 | Already converged two pairs; audit shared setup, not verdict elimination | full corpus compile and assembly have different assertions |
| MiniAS A0 focused/frozen windows | Already 2 -> 1; audit shared setup next | full 3536 exact and semantic need independent re-run |
| MiniObjcopy historical Image | Repair a producer backed by immutable identity; mark historical mode BLOCKED until verified | run 33623125809 lacks artifact, cannot silently substitute |
| Runtime FDT/kallsyms/spinlock | Already merged focused entrypoints; centralize reusable GCC/fixture setup if hashes agree | unique per-owner first fault/codegen evidence must survive |
| Seven-shard Linux Image | no further merge now; execute canonical self-producing flow | lack of current HEAD real complete Image certificate |
| Perf candidate | paired measurements and correctness, then source integration | runtime/perf branches diverged; cannot treat a structural M0 as equivalent |

## Exit criteria before Runtime fault fixing

- Exact canonical owner for every *active* workflow/job recorded (path, domain, T-tier, trigger, upstream dependency, downstream consumer, evidence, disposition).
- No dead branch-only push trigger; manually dispatched-only jobs explicitly labelled.
- No expired **required** artifact without a trusted regeneration path, or explicitly marked BLOCKED/archival.
- T0 gate green; T1 and T2 representative gates green after migration; T3 seven-shard strict replay current HEAD completed with signed provenance; T4 QEMU outcome recorded honestly even if failing.
- Performance deltas independently evaluated, accepted changes merged only after correctness proof.
- A new workflow requires owner/contract justification; target YAML count is guidance, never a substitute for proof.

## Next audit artifact

Produce a **machine-readable complete active-workflow ledger** for both development branches with columns:
`branch,path,owner_domain,tier,trigger,unique_assertions,producer,consumers,hardcoded_run_ids,cache_scope,last_real_evidence,proposed_owner,disposition,blocker`.
The current document is a taxonomy, **not** that exhaustive inventory. Do not claim all 51/50 workflows were audited from this document alone.
