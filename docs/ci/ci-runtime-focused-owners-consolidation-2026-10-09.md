# Runtime IRQ / RCU / Init / Timer 4-to-1 owner convergence — 2026-10-09

## Current result and scope

Four permanently active focused Runtime entrypoints are replaced by **one** `.github/workflows/linux-runtime-focused-owners-v1.yml` on each development branch. Four original independent diagnostic jobs and their **complete executable job bodies** remain separate. The fifth job, `route`, is a 3-minute-cap lightweight changed-file selector. Real Runtime/QEMU outcomes are **not** certified by M0; certify selected lanes independently.

| Original source, immutable archive under `.github/workflows-disabled/` | Original SHA | Unchanged job / new manual mode |
| --- | --- | --- |
| `linux-runtime-mm-core-frontier-v0.yml` | `27d159c6c54cc01bff9a07f720ddd9ef00f08976` | `init-irq-bridge` / `irq` |
| `linux-runtime-rcu-owner-v0.yml` | `33e93692052a2a4457b7cc26d14a6e5c9b4fc11a` | `rcu-softirq-owner-frontier` / `rcu` |
| `linux-runtime-riscv-init-codegen-v0.yml` | `5355fd4ffad244b9c415c567d37387d2f3dfb1f4` | `riscv-init-codegen` / `codegen` |
| `linux-runtime-timer-focused-v0.yml` | `d17ddd8d66943fffaccd870dcb0c7a5742a9fcca` | `timer-frontier` / `timer` |

Legacy top-level concurrency/cancellation groups are carried to **job-level concurrency**, one group per original owner. This preserves each lane's original cancellation isolation. Historical pinned full Linux fixture and frozen source-cache keys, compiler profile, object sets, relink command, QEMU watcher and individual artifact paths are untouched. No cross-branch cache identity or dependency is invented.

## Changed-path → independent job routing

| Push path | Actual new route output | Original policy |
| --- | --- | --- |
| `tools/ci/runtime-mm-core-trigger.txt` | `irq=true` | old MM/IRQ workflow |
| `tools/ci/runtime-init-irq-trigger.txt` | `irq=true` | old MM/IRQ workflow |
| `tools/ci/linux-runtime-rest-init-frontier-v0.json` | `rcu=true` | old RCU workflow |
| `tools/ci/runtime-timer-trigger.txt` | `timer=true` | old timer workflow |
| codegen-only source | no automatic expensive run | prior codegen auto-push was its *own* retired YAML |
| new canonical YAML or route selector | `route` only; no QEMU/diagnostic jobs | deliberately cheaper, M0 source-proof owns maintenance changes |
| two separate sentinels in one commit | two affected jobs | preserved independent ownership |
| diff unavailable / force push | route **fails INCONCLUSIVE**, not a false green PASS | explicit safe failure, manual owner selection required |

New canonical auto-push remains **Runtime-only**, as all four originals did. It is manual-dispatchable on either branch and offers `irq`, `rcu`, `timer`, `codegen`, and explicit `all`. The safe default is `codegen` instead of accidentally running 3 QEMU-consuming jobs. Neither old fixtures nor full Image/QEMU certification are changed.

## Verification and accounting

M0 executes `select_runtime_focused_owners_v1.py --self-test` and `check_runtime_focused_owner_convergence_v1.py` on both branches. The latter checks 4 pinned Git Blob SHA values against byte-identical archives and compares **all four executable bodies character-for-character** after stripping only the added job-level `needs`, mode/push `if`, and original cancellation keys. It also checks complete owner declarations and branch/manual selection. An actual Runtime sentinel push or an explicit manual dispatch is required for a tier-matched T4/QEMU verdict. A green route alone is only T0.

| Count | Previous | Current |
| --- | ---: | ---: |
| Runtime workflow YAML | 28 | **25** |
| Performance workflow YAML | 26 | **23** |
| Unique active names across refs | 29 | **26** |
| Branch-path YAML copies | 54 | **48** |
| Runtime job declarations | 88 | **89** |
| Performance job declarations | 85 | **86** |

Original four jobs still perform independent fixture/cache restoration and MiniC profile builds, but **only triggered owner(s)** do that work. Merging their build caches/jobs would require separate reproducibility and identity evidence. Additional archive/history remains recoverable; active YAMLs are no longer cluttered with these four legacy entrypoints.
