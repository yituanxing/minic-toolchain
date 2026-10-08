# Pre-runtime CI / branch readiness audit — 2026-10-08

This ledger is a *pre-runtime stabilization checkpoint*, not a new QEMU or
compiler correctness claim. Do not use the latest OpenSBI-only output to
overwrite the older certified mixed-GCC/MiniC `fork-init` frontier.

## Verified remote inventory

Source branch before the current consolidation:
`agent/linux-expanded-kbuild-v0` at
`07523859f66342e414646a244b4232a39d6b3aa1`.

- Exactly **4** remote branches:
  `main`, `agent/linux-expanded-kbuild-v0`,
  `agent/linux-perf-boolean-domain-v1`, and
  `archive/all-progress-2026-10-04`.
- Runtime HEAD had **68** active `.github/workflows/*.yml` definitions.
- Disabled archive had **260** historical files: 217 top-level files plus
  38 in `retired-20260826` and 5 in `retired-20260827`.
  Two directories were not miscounted as workflows.
- Separate performance branch had **91** active workflow definitions.
- Performance branch and runtime branch are **diverged**: 87 perf-only and
  70 runtime-only commits at this audit. Never delete/merge perf by branch
  count alone; isolate and verify its semantic and performance deltas.
- `main` remains distinct; prior audit documents 2 main-only ancestry
  commits. Preserve the historical archive as a passive recovery umbrella.

## Current full-Image proof, without overclaiming boot

Pinned producer [37787452745](https://github.com/yituanxing/minic-toolchain/actions/runs/37787452745)
compiled 3352/3352 C objects in seven shards. The later dedicated strict
receiver [37796369453](https://github.com/yituanxing/minic-toolchain/actions/runs/37796369453)
confirmed authenticated transfer of 6704 producer files, 37 warm-up MiniC
target recompilations, restoration of the original 3352 targets, one
proven `arch/riscv/lib/.delay.o.cmd` command-context reconciliation,
**zero** planned-target recompiles on the final Kbuild pass and successful
Image link. QEMU **did not** meet the init marker: OpenSBI-only output and
90-second timeout. This is not a Linux boot certificate.

The former V4 receiver `.github/workflows/linux-distributed-image-receiver-signedness-v2.yml`
uses hard-pinned producer run 37787452745 with shard artifact retention
**4 days**. Its historical success at *strict Image reuse* does not make
that runner permanently reproducible after artifact expiration.

## Self-contained canonical full-Image path prepared

`.github/workflows/linux-distributed-full-image-signedness-v2.yml` now
owns **graph → 7 shards → strict-replay receiver → QEMU → evidence**, retaining
its path-narrow trigger and manual dispatch. It has been updated to carry
the receiver-proven `MINIC_DISTRIBUTED_STRICT_REPLAY=1` and
`MINIC_DISTRIBUTED_CANONICALIZE_DELAY_CMD=1` verification contract,
syntax checks for the associated provenance helpers, and the full
warm-up/relink/identity evidence artifact set. It contains **no pinned
historical run ID**. The job still must run successfully end to end before
this **workflow change itself** is certified.

This is a workflow-only convergence: no MiniC parser/code generator source,
Linux fixture/cache key, existing passive archive, branch ref, or historical
workflow artifact has been modified.

## Do not prematurely retire

These workflow *definitions* presently retain distinct contracts or
historical diagnostics; do not delete on name similarity alone:

- `linux-distributed-full-image-v1.yml`: manual earlier compiler profile;
- `linux-distributed-image-receiver-reuse-v1.yml`: diagnostic receiver for
  older immutable shard identity;
- `linux-distributed-image-receiver-signedness-v2.yml`: short-lived
  pinned-receiver exact replay oracle, subject to artifact expiration;
- `linux-distributed-image-graph-preflight-v1.yml`: graph generation
  preflight with its own narrow change triggers;
- `linux-distributed-kbuild-objects120-v1.yml`: independent historical
  cross-runner object-transfer contract;
- full frontier, fixture producer, frozen cert, generated-kallsyms, FDT,
  watcher, MiniPP/MiniAS/MiniAR/MiniLD and M0 gates in the workflow map.

After an integrated full-Image self-contained run confirms strict identity,
review the above one-by-one and retire only true supersets/duplicates,
moving exact historical YAML bytes to `.github/workflows-disabled/`,
recording original blob SHA and supersession run ID.

## Acceptance gates before resuming Linux runtime debugging

- [x] Confirm branch inventory, divergence and archive identity.
- [x] Restore one independent distributed full-Image owner able to generate
      its own artifacts (no pinned run-id consumer prerequisite).
- [x] Preserve proven receiver-only strict replay and the OpenSBI-only
      failure evidence; do not label QEMU a success.
- [ ] Run the updated integrated workflow and confirm its own 3352-target
      final-link identity checks; this edit has **not** yet been CI-certified.
- [ ] Obtain a passing relevant M0/workflow-structure gate on the converged
      HEAD after remaining cleanup changes.
- [ ] Retire identified redundant workflows only after independently
      checking ownership, active consumers, and supersession evidence.
- [ ] Freeze a current-head baseline, record exact configs/compilers/artifact
      identities and separate previous mixed-kernel runtime frontier from
      current full-MiniC Image runtime evidence.

Do not create a new long-lived branch for this cleanup. Finish on the
runtime owner and keep performance integration as a separate, verified task.
