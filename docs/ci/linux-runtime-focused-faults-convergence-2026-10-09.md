# Runtime focused fault diagnostics consolidation — 2026-10-09

Four historical opt-in diagnostic entrypoints are now one `.github/workflows/linux-runtime-focused-faults-v1.yml`; the source files are preserved byte-for-byte in `.github/workflows-disabled/` on **both** development branches. This is workflow routing, not a Linux Runtime or compiler bug fix.

| Archive filename | Exact source Git blob SHA | New job | Mode | Original push tag |
| --- | --- | --- | --- | --- |
| `linux-check-cpu-stall-runtime-v0.yml` | `7fce9bc454cec54411fb852137fa627863ef8022` | `cpu-stall` | `cpu-stall` | `[linux-check-cpu-stall-runtime-v0]` |
| `linux-fork-stack-runtime-v0.yml` | `db96f28725b15954d39dad975598b99a0497a0ec` | `fork-stack` | `fork-stack` | `[linux-fork-stack-runtime-v0]` |
| `linux-runtime-fault-context-v1.yml` | `4ce700578911665cf0d5fd0085d511da7d7e3d27` | `fault-context` | `fault-context` | `[linux-runtime-fault-context-v1]` |
| `linux-runtime-satp-refresh-v0.yml` | `defbf066e5bb04d2ddd56f3695085bc00b2d3f08` | `satp-refresh` | `satp` | `[linux-runtime-satp-refresh-v0]` |

## Routing contract

- `workflow_dispatch` options: `fault-context` (default), `cpu-stall`, `fork-stack`, `satp`, `all`.
- Original push tags preserved as independent job predicates; only Runtime dev branch is eligible on push, exactly as with all four source workflows. No untargeted push will run these heavy jobs.
- Independent job bodies preserve original assertions, cache keys, GCC/MiniC isolation, QEMU commands, evidence upload and timeout. The two original `runtime` job IDs were renamed to unique `cpu-stall` and `fork-stack`; no executable test step changed.
- Original `cancel-in-progress: true` groups were moved to each job. Fork-stack is a six-case matrix, so its group additionally includes `matrix.case` to prevent one parallel case cancelling another.
- Individual selected modes replace four separate manual workflow selections. Full `all` is explicit opt-in.

## Safety and verification

`tools/ci/check_linux_runtime_focused_faults_convergence_v1.py` checks four exact archive blob SHAs, each original runnable job body after excluding only job ID, job predicates and moved concurrency scope, original tag predicates, job IDs, and fork-stack matrix safety. M0 executes this checker and parses the combined YAML. M0 SUCCESS is T0 **only**; real CPU-stall/fork/SATP/fault-QEMU execution must still pass and retain provenance before claiming T4 re-certification.

This does **not** replace independent canonical GCC baseline, frozen fixture producer, compact/full link fixture certification, fast/full frontier or QEMU watcher. Do not retire those by similarity of cached preparation commands.

Expected active workflow count: Runtime **46→43**, Performance **47→44**, cross-branch unique names **50→47**. No compiler source, test scripts or archive payload modified.
