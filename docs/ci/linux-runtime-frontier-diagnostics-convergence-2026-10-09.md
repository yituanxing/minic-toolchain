# Runtime full/fast/QEMU frontier diagnostics convergence — 2026-10-09

Three independent runtime diagnostic probes become one mode-selectable workflow `.github/workflows/linux-runtime-frontier-diagnostics-v1.yml`, with three independent jobs and **unchanged executable test steps**. This is entrypoint consolidation only, **not a new runtime certificate**.

| Original archived workflow | Git blob SHA | Preserved job ID | Explicit tag | Manual mode |
| --- | --- | --- | --- | --- |
| `linux-runtime-frontier-v1.yml` | `ba568cb98cd92feabf75d45489d74ba003d83baa` | `candidate` | `[linux-runtime-frontier-v1]` | `full` |
| `linux-runtime-frontier-fast-v5.yml` | `457a53c1cdb01b022a1566c7fe9f0d4b4e438d7b` | `frontier-fast-v5` | `[linux-runtime-frontier-fast-v5]` | `fast` |
| `linux-image-qemu-runtime-v0.yml` | `d537267fc422bb4c14f143e2311e39a1fb9d46c5` | `qemu-runtime` | `[linux-image-qemu-runtime-v0]` | `qemu` |

The old workflows listened to Runtime development branch pushes but required distinct message tags to run. The new unified entrypoint preserves the original branch-only push trigger, tag semantics and manual dispatch ability. Default manual mode `fast`, additional `full`, `qemu`, `all`.

Workflow concurrency (`cancel-in-progress: true` for each original workflow) now applies to each job with the same group identity, preventing an unrelated lane from cancelling another. Original checkout, pinned fixture/cache keys, timeouts, GCC/MiniC oracle and QEMU verdicts, artifact names and upload commands remain unchanged. Do **not** equate full `candidate`, compact `frontier-fast-v5`, and Image `qemu-runtime`: they use different certified inputs and can independently fail.

All three archived YAMLs are byte-for-byte original Git blobs. `tools/ci/check_linux_runtime_frontier_diagnostics_convergence_v1.py` verifies exact old SHAs, all job bodies, tags, modes and concurrency. M0 statically checks these and YAML syntax. Do not report T4 PASS before a current-head real QEMU/first-fault run.

Expected after commit: Runtime **43→41**, Performance **44→42**, unique active names **47→45**. No compiler/test implementation or linked Linux input was altered.
