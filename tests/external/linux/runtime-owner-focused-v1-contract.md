# Linux runtime owner focused V1 contract

`.github/workflows/linux-runtime-owner-focused-v1.yml` consolidates the four stable owner-focused runtime frontier workflows into one matrix without collapsing their target-specific evidence.

The matrix contains four ordered owner-chain modes:

- `timekeeping`: refresh through `kernel/time/timekeeping.o` and retain `timekeeping_advance`.
- `vsyscall`: refresh through `kernel/time/vsyscall.o` and retain `update_vsyscall`.
- `notifier`: refresh through `kernel/notifier.o` and retain `raw_notifier_call_chain`.
- `build-policy`: refresh through `kernel/sched/build_policy.o` and retain `account_process_tick`.

Every mode restores the same certified full linked fixture, pinned Linux 6.6.143 source, frozen linker source subset, builds the exact centralized MiniC runtime profile, refreshes the PI owner and the same cumulative frontier-owner chain used by the historical focused workflow, relinks the early image, and runs the same event-driven frontier watcher against `linux-runtime-init-irq-frontier-v0.json`.

## Canonical certification

GitHub Actions run `37456865812` executed on the canonical `agent/linux-expanded-kbuild-v0` runtime branch after installing this consolidated workflow there for cache-scope validation.

All four modes completed successfully at the same HEAD. Each mode passed certified cache restore, runtime MiniC profile construction, cumulative owner-object refresh, early relink, QEMU frontier execution, and evidence upload.

Therefore the four dedicated historical focused workflows are superseded and may be archived without deleting their YAML history:

- `linux-runtime-timekeeping-focused-v0.yml`
- `linux-runtime-vsyscall-focused-v0.yml`
- `linux-runtime-notifier-focused-v0.yml`
- `linux-runtime-build-policy-focused-v0.yml`

The older non-focused owner-differential workflows are governed separately by `runtime-owner-differential-retirement.md`.

## Cache-scope note

The certified full linked fixture, pinned Linux source, and frozen linker subset are GitHub Actions caches scoped to the canonical runtime ref. The cleanup branch cannot independently restore those caches, so a cache miss there is not accepted as a runtime result and `fail-on-cache-miss` remains strict.

## Vsyscall frame regression contract

Commit `367e4d84102a7400f561a80256ff322edb303f91` extends the canonical `vsyscall` matrix lane with the durable frame-size requirement previously carried by `linux-core-slot-reuse-frame-v0.yml`.

For the focused `kernel/time/vsyscall.o` rebuild only, the workflow enables MiniC Core frame tracing, extracts the `update_vsyscall` frame size, and requires:

`UPDATE_VSYSCALL_FRAME < 2256`

Canonical run `37497141242` completed SUCCESS. Its vsyscall job recorded:

- `CORE_FRAME_REUSE function=update_vsyscall frame=128 value_bytes=32 values=275 objects=43`
- `UPDATE_VSYSCALL_FRAME=128`
- `VSYSCALL_FRAME_CONTRACT=PASS limit=2256`
- `QEMU_WATCH=PASS`
- `FRONTIER_VERDICT=MOVED_LATER`

The canonical owner-focused lane therefore now guards both the owner/runtime frontier and the historical vsyscall frame bound. The separate static frame-measurement workflow is governed by `runtime-core-frame-retirement.md`.

