# CI topology — authoritative active map (2026-10-09)

> This file supersedes the historical **count snapshots** that used to be prepended repeatedly to this page. Historical text is preserved in Git blob `7cb2cc9aef43238487e4b5074b871255a428d07c` and in the individual owner convergence ledgers. Always distinguish an **active YAML**, a **declared job**, a **real allocated runner**, and **certified test execution**.

## Repository refs and ownership

| Ref | Role | Active YAML |
| --- | --- | ---: |
| `main` | Historical default release/base; 2026-10 development not yet integrated | 11 |
| `agent/linux-expanded-kbuild-v0` (Runtime) | **canonical Linux/Kbuild/fixture/QEMU certification and MiniC current production profile** | **26** |
| `agent/linux-perf-boolean-domain-v1` (Performance) | Separate **55-patch candidate / paired speed and correctness** development | **12** |
| `archive/all-progress-2026-10-04` | Passive recovery umbrella (not a source-merge or CI branch) | **0** |

Runtime + Performance maintain **38 branch-local YAMLs, 27 distinct names**; **94 + 41 = 135 declared jobs**, including seven inexpensive conditional reusable calls in the Runtime tag router. This is **not** 143 runners. The branch-owned [39-row inventory](active-workflow-inventory-2026-10-09.tsv) and [trigger audit](ci-push-skip-audit-2026-10-09.md) are the machine-checked source inventory. Main's 11 historical YAMLs are **not** part of that development-branch inventory.


**Architecture audit and open defects:** [2026-10-09 source-level findings and closure criteria](ci-architecture-audit-findings-2026-10-09.md). The global MiniObjcopy cancellation was disabled after a P0 race was discovered. Its historical frozen `linux-image` mode has been **retired from active CI** because the pinned artifact run has zero artifacts; the exact original source remains SHA-archived. This is not proof of a replacement GNU Image comparison. Do not equate green T0 with healthy T3/T4.

## Five explicit test tiers

| Tier | Contract | Canonical owners | What success proves |
| --- | --- | --- | --- |
| **T0** | Structure, history SHA, route selectors, graph manifests and source guardrails | `toolchain-m0-structure.yml`, audit/check scripts | Wiring/source equivalence only. Never label kernel boot PASS. |
| **T1** | Fast compiler, MiniPP, driver, MiniAR/MiniLD focused checks | `toolchain-focused-regressions-v1.yml` (one build, eight independent oracle steps); `minipp-a0.yml`, `minic-driver-v0.yml` | A specific executed regression/check, not a full Linux image. |
| **T2** | Frozen/preprocessed and full compile/assemble coverage | `linux-core-all3352.yml`, `linux-core-shards-v1.yml`, `minipp-linux-frozen-v1.yml`, `minias-a0-gate-v1.yml` | Explicit covered TU/assembly corpus at exact compiler profile. |
| **T3** | Linux object, AR/LD/ELF integration, provenance, Image strict replay | `linux-distributed-full-image-signedness-v2.yml`, `linux-expanded-kbuild-v0.yml`, `miniar-linux-kbuild.yml`, `minild-integration-v1.yml`, `miniobjcopy-strip-regressions-v1.yml` | Link/identity/contract only if all relevant jobs actually executed. |
| **T4** | Runtime fault isolation and QEMU verdict | `linux-expanded-pi-p1-runtime-v1.yml`, `linux-runtime-focused-faults-v1.yml`, `linux-runtime-focused-owners-v1.yml`, `linux-runtime-owner-focused-v1.yml`, `linux-runtime-fdt-isolation-v1.yml`, `linux-runtime-spinlock-context-v0.yml`, `linux-runtime-gcc-baseline.yml` | Pass only when corresponding init/boot marker and verdict explicitly succeed. |
| **P** | Independent candidate speed+correctness experiments | Performance `linux-performance-experiments-v1.yml`, Runtime `linux-runtime-optin-perf-suite-v1.yml` | Comparable paired inputs/runners; does not promote runtime profile. |

## Runtime 26 owners grouped by purpose

**Source/compiler regression (7):** `toolchain-m0-structure.yml`, `toolchain-focused-regressions-v1.yml`, `minipp-a0.yml`, `minipp-linux-frozen-v1.yml`, `minic-driver-v0.yml`, `linux-core-all3352.yml`, `linux-core-shards-v1.yml`.

**Linux Image and integration (7):** `linux-distributed-image-graph-preflight-v1.yml` (standalone fast graph check), `linux-distributed-full-image-signedness-v2.yml` (graph → shards → strict receiver), `linux-expanded-kbuild-v0.yml`, `linux-runtime-fixture-producers-v1.yml`, `miniar-linux-kbuild.yml`, `minild-integration-v1.yml`, `miniobjcopy-strip-regressions-v1.yml`.

**Linux Runtime diagnostics (9):** `linux-expanded-pi-p1-runtime-v1.yml`, `linux-runtime-focused-faults-v1.yml`, `linux-runtime-focused-owners-v1.yml`, `linux-runtime-owner-focused-v1.yml`, `linux-runtime-fdt-isolation-v1.yml`, `linux-runtime-spinlock-context-v0.yml`, `linux-runtime-gcc-baseline.yml`, `linux-efi-vdso-focused-v1.yml`, `minias-a0-gate-v1.yml`. MiniAS gate belongs to assembler-level Linux readiness rather than a standalone QEMU verdict.

**Explicit opt-in/performance/routing (3):** `linux-runtime-optin-perf-suite-v1.yml`, `minias-a0-focused-diagnostics-v1.yml` (manual focused oracle), and `linux-legacy-tag-router-v1.yml` (the one Runtime broad historical-tag push entrypoint).

The two distributed graph workflows have **different, disjoint push path filters**: the fast graph owner responds to its own YAML and `linux-distributed-image-plan-v1.py`; the integrated Image owner responds to its dedicated `run-linux-distributed-image-signedness-v2.trigger`. They are **not** two independent 3352-TU builds running on ordinary source pushes. The fast preflight is retained as an independent cheap graph contract.

## Performance 12 owners grouped by purpose

**Actual P owner:** `linux-performance-experiments-v1.yml`. **Shared compiler T0–T2 owners:** `toolchain-m0-structure.yml`, `toolchain-focused-regressions-v1.yml`, `linux-core-all3352.yml`, `minipp-a0.yml`, `minipp-linux-frozen-v1.yml`, `minic-driver-v0.yml`. **Shared integration/diagnostic owners:** `minild-integration-v1.yml`, `miniobjcopy-strip-regressions-v1.yml`, `linux-runtime-gcc-baseline.yml`, `linux-efi-vdso-focused-v1.yml`, `minias-a0-focused-diagnostics-v1.yml`. The historically Runtime-branch-only Performance PI/P1 owner was retired to the SHA-preserved Performance archive and is not part of active Performance CI. Historical removed Performance Runtime-only YAMLs are SHA-preserved in `.github/workflows-disabled/`.

## Trigger ownership; duplicate-run prevention

```text
Runtime push
 ├─ relevant compiler/CI path -> scoped T0/T1/T2 gate
 ├─ explicit historical [label] -> one linux-legacy-tag-router-v1
 │      ├─ Kbuild / MiniAR / certified fixture / MiniAS
 │      ├─ early PI/P1 semantics & QEMU watch / focused faults & QEMU
 │      └─ MiniLD (Runtime only)
 ├─ ELF or MiniObjcopy source -> MiniObjcopy regression router
 └─ explicit Image trigger path -> self-contained graph -> shards -> receiver -> QEMU

Performance push
 ├─ relevant compiler/performance path -> scoped M0/T1/T2/P owner
 ├─ explicit MiniLD [label] -> minild-integration (Performance direct push only)
 └─ MiniObjcopy source or legacy tag -> MiniObjcopy router
```

**Six Runtime reusable owner YAMLs** (Kbuild, MiniAR, fixture, MiniAS, early Runtime, focused faults) have no direct `on.push`; MiniLD is `workflow_call` for Runtime and retains original tag-gated `on.push` **only on Performance**. On an untagged Runtime CI-maintenance push, the router produces a SKIPPED workflow card with **zero route runners**. The original Job predicates, cache identities, source blobs and watcher verdicts remain under SHA reconstruction tests. A real special-tag smoke proved reusable-call push event inheritance [#37902327902](https://github.com/yituanxing/minic-toolchain/actions/runs/37902327902). The probe was **removed** from active YAML afterward.

**Known residual cost:** `miniobjcopy-strip-regressions-v1.yml` still has an unscoped push with an actual short `route` Runner on both refs, even on non-ELF pushes, to preserve historical commit-message opt-in and fail-open ELF diff discovery. It is the remaining potential low-value runner; do not remove an opt-in tag without equivalent branch-specific routing and T1 rerun proof. Other opt-in gates are path-scoped or manual/disabled as documented.

**Manual dispatch caveat:** `main` does not carry these development workflow definitions; an `on.workflow_dispatch` declaration on a development ref is **not** proof that GitHub UI/API can start it. Keep the verified commit-tag route until a default-branch entrypoint is proven.

## Maintenance exit criteria (do not turn CI cleanup into permanent work)

1. Both development M0 jobs must be green at their respective latest source heads with exact ledger and executable-contract checks.
2. Recent untagged Runtime CI change must not trigger costly Linux/Image/T3/T4 or P comparisons. Confirm actual GitHub job conclusions, not only YAML guards.
3. The old four-owner reusable smoke (#37902327902) establishes push/event inheritance. Do **not** start a full 3352/Linux QEMU rebuild simply to prove the router.
4. T1 independent oracles and P comparisons require explicit own execution evidence when code changes. M0 cannot replace them.
5. Freeze the CI topology after these gates; return to **Performance candidate integration** and **Linux Runtime fault isolation**. Full Linux boot certification is still **open**.

### Explicit performance qualification entrypoint
The Runtime `linux-runtime-optin-perf-suite-v1.yml` was discovered with *self-YAML-only push scope*, making its historical [linux-perf3352], [linux-constant-p], and [linux-perf500] source-push labels unreachable. The existing Runtime `linux-legacy-tag-router-v1.yml` now owns those precise tags through an audited reusable call. No new YAML or source-test body was added. Treat M0 success as trigger/archival proof, not as a performed 3352 Linux compile or 500 paired perf benchmark.

### Scoped Runtime diagnostics reachable by intentional labels
`linux-runtime-spinlock-context-v0.yml` and `linux-runtime-fdt-isolation-v1.yml` preserve their original narrow sentinel-file push triggers, while their four and five originally tag-gated Jobs are now addressable through the **same** Runtime legacy router's two new conditionally delegated calls. No new YAML, Runner-on-housekeeping or permanent branch was created. The spinlock workflow-wide cancellation race was separately disabled. Exact reconstructed original Git Blobs and the complete unchanged diagnostic job bodies are checked in M0.


## Performance-only PI/P1 Runtime diagnostic retirement (2026-10-09)
`linux-expanded-pi-p1-runtime-v1.yml` had seven expensive QEMU/semantics jobs but only a `push` branch filter for **Runtime**, even in the Performance tree. Its only fallback `workflow_dispatch` was development-branch local while the default `main` lacks this definition. This file therefore contributed no reachable Performance push test and no verified manual Performance dispatch. The real Runtime owner is kept **active** on Runtime with the existing tag router.

The complete **Performance-only** definition, Git Blob `2d09b865c9e69ef0266c53f5e2c64cd3fb1b1fd3`, is now stored unchanged under `.github/workflows-disabled/performance-retired-2026-10-09/linux-expanded-pi-p1-runtime-v1.yml`. Its older historical version `0dff72daea890b37972c64a6411caa18b00cf528` remains separately archived under the original disabled path. Existing four source/body convergence gates on Performance now read the archived Performance source; Runtime continues to verify the live source. No QEMU job body was rewritten or deleted from Git history.

This removes one unreachable active YAML and seven declarations from Performance, without adding workflows, test modes, branches or runner cost. Scope: Runtime 26, Performance **12**, total **38 branch-local YAML copies / 27 distinct names / 135 declarations**. Audit success only proves preserved source; future Performance-specific Linux Runtime certification must use a verified owner/input if genuinely needed. 
