# Project convergence audit — 2026-10-07

This audit records the repository state after the CI/runtime cleanup and identifies the remaining work required before the converged development history can be treated as a coherent current release line.

## Snapshot

Audited HEAD: `63aec401297f365d78e441ea22e07411023b7b7e`.

- active GitHub Actions workflow YAML files: **87**;
- disabled historical workflow YAML files: **185**;
- active `linux-*.yml` workflows: **36**;
- `tools/ci/` entries: **208**;
- total `tools/ci/apply-*.py` scripts: **142**;
- centralized Linux runtime profile semantic overlays: **37**;
- remaining branches: `main`, `agent/linux-expanded-kbuild-v0`, `agent/ci-runtime-cleanup-v1`, and `archive/all-progress-2026-10-04`;
- cleanup and runtime branches are synchronized at the same HEAD.

The workflow cleanup phase is largely complete. The highest-risk remaining work is no longer deleting old YAML; it is reconnecting compiler source, Stage2 self-host evidence, full-Image production, and runtime certification into one authoritative chain.

# P0 — release-blocking convergence gaps

## P0.1 Productize the Linux runtime compiler profile

The deepest Linux runtime workflows do not currently build the compiler represented by a clean checkout.

`tools/ci/linux-runtime-build-minic-profile-v1.sh` applies 37 ordered source-mutating scripts before building MiniC. A concrete example is the checked-in RV64 global-address emitter, which still uses ordinary `la`; the runtime profile changes assembler-local symbol behavior through `apply-riscv64-pi-local-symbol-address-v0.py`.

Consequences:

- direct `make` and Linux runtime CI validate different compiler states;
- patch order becomes hidden compiler semantics;
- release/source review cannot reason about the runtime compiler from `src/` alone;
- old profile scripts accumulate even after the underlying experiment is complete.

This is now explicitly tracked as `DEV-0005`. Do not claim a current full-Linux compiler baseline until its exit criteria are satisfied.

## P0.2 Reconnect current Linux runtime validation to Stage2 self-hosting

`docs/milestones/compiler-v1-frozen.md` certifies B1 Stage0→Stage1→Stage2 fixed point and B2 Stage2 real-program runtime. The current Linux runtime profile, however, is produced by mutating a clean checkout and rebuilding MiniC with the host compiler.

The intended chain:

```text
productized source
  -> Stage0
  -> Stage1
  -> Stage2 fixed point
  -> Linux Kbuild owners
  -> full Image
  -> QEMU runtime
```

is therefore not continuous today.

After profile productization, B1/B2 must be repeated and the resulting Stage2/current release compiler must become the compiler identity used by the authoritative Linux runtime baseline.

## P0.3 Establish one current full-Image/runtime baseline

The authoritative frontier file `tools/ci/linux-runtime-frontier-v1.json` still records the historical `strlen+0x6` / `paging-init` baseline certified by run `35877266177`, while newer focused owner/FDT/kallsyms workflows have progressed substantially beyond it.

A current full Image is also not yet certified. Run `37560634367` used the centralized current runtime profile and successfully completed **61 of 73** explicit historical final-link owner targets before the 55-minute job timeout cancelled the remaining build. The run did not reach the final Image link.

Required closure:

1. complete a current-profile full Image build;
2. publish it under an exact certified Image cache identity;
3. boot that exact Image through minimal smoke and the P1 contract;
4. use current full-owner evidence to replace the stale global frontier baseline;
5. re-evaluate focused runtime workflows against that single baseline.

# P1 — CI/reproducibility engineering

## P1.1 Full-Image producer runtime and resumability

The first current-profile producer run passed 61/73 explicit owner targets before timing out. The remaining set was concentrated in large MM/network objects such as `mm/mprotect.o`, `mm/mremap.o`, `mm/vmalloc.o`, `mm/page_alloc.o`, `mm/memcontrol.o`, `mm/memfd.o`, and `net/core/filter.o`.

The producer needs either:

- a bounded longer certification timeout for the first full rebuild, plus exact caching for subsequent runs; or
- checkpointed owner cohorts so an interrupted first rebuild does not restart all expensive objects.

A final full-Image certification must not depend on an old restore-key fallback.

## P1.2 Consumers must require certified exact Image identity

Historical P1/smoke recertification at `1eb8a252...` showed:

```text
CACHE_HIT=false
CACHE_MATCHED_KEY=linux-expanded-mixed-v1-...-d305...
```

so consumers silently tested an old 264 MiB Image instead of the current HEAD.

The old Image:

- overlaps QEMU's default initrd placement in the minimal smoke;
- with explicit initrd placement still faults before the Linux banner in the very-early paging/entry path.

P1 and minimal Image smoke must therefore restore only a successful exact `linux-expanded-image-v1` cache and fail closed on cache miss.

## P1.3 Keep workflow ownership map canonical

`docs/ci/current-workflow-map.md` is now the ownership index for active CI. Any future active workflow must have a unique invariant, and retirements must update the map/contract in the same convergence change.

The active count should not be driven toward an arbitrary number. Independent EFI, vDSO, static codegen, runtime isolation, frozen-corpus and tool-specific gates remain valid when their invariant is unique.

# P1 — project-state/documentation drift

## P1.4 Public status must match the repository

Several project entry points lagged behind actual implementation:

- README wording still described native preprocessing/assembly/linking as future work even though MiniPP/MiniAS/MiniAR/MiniLD and object utilities now exist with independent gates;
- `toolchain-component-boundaries.md` still described the early MiniAS-active M0 state without a later-status note;
- the Makefile `bootstrap` target claimed no bootstrap stages existed even though B1/B2 self-host certification is a frozen milestone.

These should describe the actual isolation/certification status rather than historical sequencing.

# P2 — known semantic/architecture debt

The existing active deviations remain real release-boundary constraints:

- `DEV-0002`: GNU asm-goto dynamic immediate still requires future specialization/inlining;
- `DEV-0003`: call-frame introspection semantics differ when GCC would inline wrappers;
- `DEV-0004`: enum compatible-integer representation still uses bounded cached refresh rather than the final target/type model;
- `DEV-0005`: Linux runtime compiler semantics are dynamically overlaid in CI.

These are not reasons to expand the compiler speculatively. They are explicit limits on what can be claimed as fully productized/canonical.

# P2 — CI helper archaeology

`tools/ci/` currently contains 208 entries, including 142 `apply-*.py` scripts. Many are valuable historical migrations or reproducibility probes, but the directory mixes:

- current canonical profile mutators;
- obsolete productization patches;
- one-off diagnostic instrumentation;
- historical migration scripts.

After DEV-0005 productization, classify them into current verifier/build helpers versus historical migration evidence. Do not delete historical scripts blindly, but stop making obsolete patch scripts part of the production compiler identity.

# P2 — final branch/main convergence

The cleanup and runtime branches are synchronized. `main` still carries two ancestry-only commits whose net tree change is zero relative to their merge base.

Final branch convergence should happen only after:

1. DEV-0005 productization plan is either completed or deliberately kept as an explicit blocker;
2. current full Image/runtime baseline is certified;
3. final toolchain/Linux certification set is green;
4. main-only ancestry is merged without replacing the validated converged tree.

Then remove `agent/ci-runtime-cleanup-v1` and retain:

- `main`;
- `agent/linux-expanded-kbuild-v0` while runtime development remains active;
- `archive/all-progress-2026-10-04`.

# Completion definition

The cleanup project is not complete merely when workflow count is low. It is complete when a clean checkout has one explainable compiler/toolchain state and the following chain is reproducible without hidden semantic overlays or stale-cache fallback:

```text
checked-in source
  -> validated compiler / Stage2 identity
  -> exact Linux owner build
  -> certified full Image
  -> QEMU runtime contract
  -> authoritative current frontier
```
