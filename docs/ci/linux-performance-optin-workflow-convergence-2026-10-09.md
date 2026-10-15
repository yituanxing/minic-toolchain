# Linux performance opt-in suite convergence — 2026-10-09

## Owner and scope

Three Runtime-branch manually invoked/one-off opt-in workflow files are now one `.github/workflows/linux-runtime-optin-perf-suite-v1.yml`. This is the existing **Runtime compiler-profile candidate**, not the separate performance branch's independent benchmark suite. Do not treat this as performance source integration.

| Archived workflow | Source Git blob SHA | Kept job ID(s) |
| --- | --- | --- |
| `linux-runtime-optin-perf-all3352-v1.yml` | `81e5d4a2b55bfd68386cc844b227fbb399b60fe6` | `shard`, `aggregate` |
| `linux-runtime-optin-perf-constant-p-qemu-v1.yml` | `65c5fc802b52c171a08b2fffddc1c15802103864` | `semantics` |
| `linux-runtime-optin-perf-first500-ab-v1.yml` | `b223ea9a7a728aa6e7eccf40ec76c930bcf58624` | `perf500` |

Jobs, all steps, timeouts, matrix partitions, artifacts, compiler profile commands, comparisons, and QEMU invocations retain their exact original executable bodies. Only job-level opt-in predicates were introduced: choose `first500` (default), `all3352`, `constant-p`, or `all` by manual dispatch, or use explicit push tags `[linux-perf500]`, `[linux-perf3352]`, `[linux-constant-p]` on an update to the canonical workflow definition. The 3352 `aggregate` retains `needs: shard` and an `always()`-based failure-aware verdict.

A direct untagged workflow-definition push used to start heavy jobs in the original three files. Those now run only on explicit opt-in, an intentional resource safety improvement. Ordinary compiler changes still require targeted performance dispatch when appropriate.

## Evidence and exit criteria

Checker `tools/ci/check_linux_perf_optin_convergence_v1.py` verifies all three archived exact Git blobs, four job bodies and opt-in conditions. M0 runs the checker plus Ruby YAML parsing. A structural pass is **not** a full 3352-TU, A/B or QEMU pass. Run real selected modes before declaring post-consolidation T2/T4 certified.

Branch-only reduction: Runtime active YAML **48→46**, performance remains **47**, distinct cross-branch names **52→50**. Historical blobs, tests and compiler code are preserved.
