# Runtime early PI/P1, Entry Trace, compiler semantics and QEMU watch — 2026-10-09

One maintained entrypoint `.github/workflows/linux-expanded-pi-p1-runtime-v1.yml` now contains **seven independently evaluated jobs**. The earlier PI/P1/Entry Trace and compiler-semantics/QEMU-watcher workflows had identical Runtime-only push branch eligibility, each already had its own commit-tag opt-in and independent job predicates.

| Historical canonical YAML archived verbatim | Runtime Git blob | Performance Git blob | Preserved jobs |
| --- | --- | --- | --- |
| `linux-expanded-pi-p1-runtime-v1.yml` | `acd84a8de23ebf908f75eb863df285cfb83b79c0` | `0dff72daea890b37972c64a6411caa18b00cf528` | `pi-runtime`, `p1`, `trace` |
| `linux-runtime-contract-oracles-v1.yml` | `050e4a8cc965cfca63a736a2cb491e1f038bb9eb` | same | `pi-local-symbol`, `satp-micro`, `qemu-watch-cert`, `inconclusive-cert` |

The original Entry Trace job bodies are **different across branches**. That distinction remains preserved in each branch's combined YAML and original archive. All seven job bodies and original manual/push if-conditions are byte-identical to their immediately prior canonical source. Each remains a separate reported verdict with its own artifact names, cache identity, timeouts, GCC/MiniC assertions, explicit commit tags and job-specific cancellation policy.

Manual modes: `p1` (default), `pi`, `trace`, `semantics`, `watch`, `all`. Both prior entrypoints supported the same Runtime branch push scope; the combined input selector is their manual dispatch union. Modes do **not** equate or aggregate the semantics and watcher PASS outcomes.

M0 `tools/ci/check_linux_early_runtime_union_v1.py` verifies **both immediate previous canonical workflow Git blobs** and all seven completely identical job bodies, with branch-specific archive hashes. The earlier checks `check_linux_expanded_pi_p1_convergence_v1.py` and `check_linux_runtime_contract_oracles_convergence_v1.py` are adjusted only to admit the other canonical's additional jobs, preserving all their original earlier archive/correctness checks.

**No fresh runtime QEMU boot PASS is claimed.** A GREEN M0 proves workflow wiring and source preservation, not full Image provenance, a fork-init marker, or a real QEMU session completing successfully. Full Image and fixture producers remain untouched.

This consolidation changes active count from Runtime 33 to **32**, Performance 34 to **33**, distinct active workflow names 37 to **36**.
