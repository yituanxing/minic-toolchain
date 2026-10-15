# Workflow cleanup: retired external project experiments and verified full3352 (2026-10-08)

## Branch ownership and exact worktree snapshot

Runtime branch `agent/linux-expanded-kbuild-v0`, HEAD before this change `98c93fe17f93a1721904e1893debf2c97a504f72`, 79 active and 245 disabled YAML workflows. The current four remote branch names are `main`, `agent/linux-expanded-kbuild-v0`, `agent/linux-perf-boolean-domain-v1`, and `archive/all-progress-2026-10-04`.

## Six historical workflows moved without changing content

The six YAMLs below were previously triggered only by deleted `external/*` and `toolchain/*` branch names, or manual dispatch. They are not owners of today's MiniC Linux 3352 / Image / QEMU certificates. Each retains exactly its previous Git blob SHA under `.github/workflows-disabled/`. The underlying test programs, commits, artifact provenance and restored documentation remain in history.

- `busybox-failure-pool-v0.yml`: `67901fb0c3a7d50e29e96fae833c7ca7d83f5ca8`
- `busybox-full-driver-v0.yml`: `d1f95066a0f00a5dbd05a7e93793d69ef177bc0f`
- `busybox-full-toolchain-v0.yml`: `fee0680d5d114776340e29d240297bff1357f114`
- `sqlite-full-driver-v0.yml`: `a9de08c2e7adeeb0c7023c0f083ee0ad5685e1e4`
- `tinycc-full-driver-v0.yml`: `63955e9a8dde6d8aa661039c261d4d164e9200ef`
- `lua-full-driver-v0.yml`: `21ffff1f5af775dddefe25ef60c53f22beef217c`

Scope is **workflow activation**, not retirement of the ability to exercise external BusyBox, SQLite, TinyCC or Lua tests. If those become active certification obligations again, restore the exact workflow, update its branch and dependency identity and run it on the current release. This operation does not claim that its historical semantic obligations are automatically covered by another modern workflow.

**Result at this snapshot:** 73 active / 251 disabled (exact historical copies). No production source, compiler Profile, Linux Image/certification workflow, external fixture source, or remote branch ref changed.

## Newly confirmed performance profile verification

- [First500 A/B run 37760522658](https://github.com/yituanxing/minic-toolchain/actions/runs/37760522658) SUCCESS: 500/500 exact assembly matches between opt-in performance variants, paired cumulative times 1429.757 / 580.236 TU-seconds (2.4641x on these two **performance variants**, NOT versus the canonical 39-patch Runtime).
- [Focused Serio serial regeneration 37764301563](https://github.com/yituanxing/minic-toolchain/actions/runs/37764301563) demonstrated both formerly invalid input files are GCC-valid if Kbuild materialization is serial; old Runtime, candidate and three feature-controlled variants compile both inputs (10/10). The earlier 3350/3352 false-green was a combination of input corruption and CI exit-code propagation.
- **NEW verified** [full3352 run 37764736667](https://github.com/yituanxing/minic-toolchain/actions/runs/37764736667) on `adad915ff97...`: all seven shards and aggregate SUCCESS; `ALL3352_COUNTS pass=3352 fail=0 missing=0`, `ALL3352_COVERAGE=3352/3352` and `ALL3352_COMPILE_STATUS=PASS`. Shard f explicitly verified GCC accepts both `serio.i` and `libps2.i` before compiler replay.
- Sum of 3352 measured compiler TU seconds: **3881.308**. The seven shards run on distinct concurrent runners; this is neither wall time for one machine nor an apples-to-apples speedup versus legacy Runtime.
- Git comparison from `adad915ff97` to runtime HEAD `98c93fe17f93a1721904e1893debf2c97a504f72` showed **no changes to `src/`, `include/`, or active performance patch/profile scripts**; intervening commits changed distributed Image CI experiment scripts. Revalidate this assertion if compiler sources move.

## Remaining exit gates

Linux Kbuild object compilation/relocatable linking, full Linux Image certification and RISC-V QEMU boot with the **new compiler identity** remain outstanding. Do not promote performance Profile to the canonical 39-patch runtime or delete the performance branch until those passes are real. Distributed full-Image experiment is actively running; this workflow-only cleanup does not alter its inputs.

## Workflow guardrail

New experiments should use existing diagnostics or dedicated short-lived, narrow self-path triggers. On completion, archive historical experiments unchanged and update this ledger. No default `push` path broadenings to restore old deleted branches. The goal is explicit ownership and verified obligations, not an arbitrary YAML-count target.
