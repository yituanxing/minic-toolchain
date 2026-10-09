# Focused Fault and Frontier diagnostic entrypoint convergence — 2026-10-09

The existing canonical `.github/workflows/linux-runtime-focused-faults-v1.yml` now contains both related diagnostic families as **seven separate job verdicts**. No new QEMU/first-fault PASS is claimed.

| Original canonical workflow (now archived) | Exact Git blob SHA | Jobs |
| --- | --- | --- |
| `linux-runtime-focused-faults-v1.yml` | `770c2bb3c8831b35d7eabebbc8dba8d09dc32384` | `cpu-stall`, `fork-stack`, `fault-context`, `satp-refresh` |
| `linux-runtime-frontier-diagnostics-v1.yml` | `2e6b1ff2a4b6f668432a0329e01b01376a20d8d7` | `candidate`, `frontier-fast-v5`, `qemu-runtime` |

The maintained entrypoint offers `fault-context` (default), `cpu-stall`, `fork-stack`, `satp`, `fast`, `full`, `qemu` or `all` as separate manual choices. All seven original job definitions (including original distinct opt-in tags, QEMU classification, timeout, cache keys, artifacts, concurrency policy and fork-stack matrix guarding) are **byte-identical** to their source canons. Both old source canons are archived, and the preexisting M0 tests for all seven older historical diagnostic source blobs are still active with their expected job-set updated.

M0 `tools/ci/check_linux_runtime_diagnostic_union_v1.py` proves exact SHA/whole-job equality of both archived canonical sources and the final seven jobs. This is structural evidence only: a `MOVED_LATER`, `INCONCLUSIVE`, `QEMU_RC=0` or green T0 job is **not** a boot PASS. The GCC baseline/frozen producer/compact linker producer and full distributed Image workflow are unchanged.

Active workflow counts after convergence: Runtime **34→33**, Performance **35→34**, unique names **38→37**. No compiler source or test scripts were changed.
