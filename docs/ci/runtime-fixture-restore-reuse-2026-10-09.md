# Shared immutable Linux runtime fixture restoration — 2026-10-09

## Why this change

A source audit found **12** byte-identical `actions/cache/restore@v4` steps for pinned Linux 6.6.143 source and **7** byte-identical steps for certified frozen linker inputs across focused Runtime workflows. These were repeated inline, making cache-key drift hard to audit and requiring changes in many locations.

The first step of real reuse is the local composite action:
`.github/actions/linux-runtime-restore-fixture/action.yml`.

Its `fixture` input accepts exactly `source` or `frozen`. Each mode dispatches the **same GitHub cache restore action, path, cache key and fail-on-cache-miss=true** as before. Unsupported modes fail. It does not generate new caches or alter branch-scoped GitHub cache access.

## Migrated workflows

| Active workflow, both development branches | Original Git blob SHA | Source restore(s) | Frozen restore(s) |
| --- | --- | ---: | ---: |
| `linux-runtime-mm-core-frontier-v0.yml` | `4c3d6fade380edab6349abf97a88390f92f24bcc` | 1 | 1 |
| `linux-runtime-rcu-owner-v0.yml` | `a3041a6f9f03351c9785e08d6c379b024d714bc9` | 1 | 0 |
| `linux-runtime-timer-focused-v0.yml` | `b24b8225264a1b25808ba5eca2ee0790002c2834` | 1 | 1 |
| `linux-runtime-owner-focused-v1.yml` | `a0a39616cf5631bbcdf01500456df2aad9201554` | 1 | 1 |
| `linux-runtime-riscv-init-codegen-v0.yml` | `6021f20acfc51df96d8e455af2e500e1f663f7e3` | 1 | 0 |

This removes **8 repeated inlined restore implementations** across five owners without removing their unique jobs. Different linked Image fixture caches and compiler profiles are **not** unified.

## Formal static regression gate

`tools/ci/check_linux_runtime_fixture_restore_v1.py` checks the composite action's **exact Git blob SHA** and cache-mode identities. It then reverses only the five workflows' exact two-line action invocations back into the original six-line cache restore steps. The reconstructed original YAML **must match the historical complete Git blob SHA**, proving everything else (job body, artifact, dependencies, trigger policy) is byte-identical.

Runtime and Performance M0 structural gates run the checker and Ruby YAML parsing. No YAML-copy archive is needed: the historical original workflow SHA remains in Git history and is checked by reconstruction.

## Actual current migration test evidence

The shared composite was exercised by five current Runtime owner workflows after the migration:

| Workflow run | Result | What the result establishes |
| --- | --- | --- |
| [Timer Focused #37872681398](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681398) | SUCCESS | source and frozen fixture restores, plus its own focused diagnostic contract |
| [RISC-V Init Codegen #37872681417](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681417) | SUCCESS | pinned source restore and GCC/MiniC object-generation comparison |
| [Owner Focused #37872681214](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681214) | 4/4 matrix jobs SUCCESS | source/frozen fixture restores and each owner's focused gates |
| [RCU Softirq #37872681285](https://github.com/yituanxing/minic-toolchain/actions/runs/37872681285) | FAILURE at QEMU fork-init progress assertion | cache restore and object refresh passed; first QEMU probe reached `console-init`, second probe was `INCONCLUSIVE` with no explicit PASS marker |
| [Runtime M0 #37872841930](https://github.com/yituanxing/minic-toolchain/actions/runs/37872841930) | SUCCESS | exact action YAML/blob, five original complete workflow SHA reconstruction and Ruby parsing |
| [Performance M0 #37872857343](https://github.com/yituanxing/minic-toolchain/actions/runs/37872857343) | SUCCESS | same structural check on independent branch |

**RCU last real classifier:** `FRONTIER_VERDICT=INCONCLUSIVE`, `FRONTIER_FAULT=none`, `FRONTIER_HIGHEST_PROGRESS=console-init`, `FRONTIER_PASS_MARKERS=none`, `QEMU_RC=0`. Here `QEMU_RC=0` is the controlled watcher lifecycle, **not** a successful Linux boot. Whether this is an older known runtime frontier or a semantic regression requires separate GCC/MiniC fixture/HEAD comparison. Keep that investigation for the runtime-debugging stage; do not alter certified fixture keys to force green.

## Execution caveat

A successful M0 proves exact source-equivalent configuration, **not** that GitHub Actions has restored these caches or that QEMU has booted. A composite-action invocation adds a GitHub Actions indirection boundary; require a real cache-hit selected mode before marking T3/T4 migration accepted. Treat a missing branch-scoped cache as BLOCKED, not as a compiler regression or a PASS.

Other Spinlock, FDT and generated-kallsyms workflows still use the original literal inline cache steps: their existing M0 gates require historical job bodies unchanged. Do not rewrite them without reconciling their earlier job-body proof first.

No source compiler code, static/object or runtime test scripts were changed, and the active workflow counts remain **41 Runtime / 42 Performance / 45 distinct names**.
