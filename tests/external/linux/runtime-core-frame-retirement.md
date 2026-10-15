# Core frame diagnostic retirement contract

This cleanup retires two historical frame-measurement workflows after moving their durable regression contract into the maintained owner-focused runtime matrix.

## Historical workflows

### linux-core-slot-reuse-frame-v0.yml

This workflow was introduced on 2026-09-15 to measure block-local Core spill-slot reuse on the Linux timekeeping/vsyscall path.

Its final historical run was GitHub Actions run `34996586916`, which completed SUCCESS and recorded:

- `CORE_FRAME_REUSE function=update_vsyscall frame=384`
- `UPDATE_VSYSCALL_FRAME=384`

The workflow's durable hard requirement was:

`update_vsyscall frame < 2256`

### linux-irq-frame-pack-v0.yml

This workflow measured the earlier value-slot-pack optimization on the IRQ/spinlock path. GitHub Actions run `34989719714` completed SUCCESS and recorded representative reductions including:

- `do_irq`: 1280 -> 768 bytes
- `handle_riscv_irq`: 464 -> 256 bytes
- `irq_enter_rcu`: 2048 -> 1120 bytes
- `arch_spin_lock`: 816 -> 464 bytes
- `do_raw_spin_lock`: 336 -> 192 bytes

It did not carry an independent long-term threshold beyond the availability of those focused functions and the frame-delta evidence.

## Canonical contract migration

Commit `367e4d84102a7400f561a80256ff322edb303f91` folds the durable vsyscall frame contract into `.github/workflows/linux-runtime-owner-focused-v1.yml`.

The canonical vsyscall lane now:

- rebuilds the exact current runtime MiniC profile;
- enables Core frame trace only for the focused vsyscall rebuild;
- extracts the current `update_vsyscall` frame;
- requires the frame value to exist;
- requires `frame < 2256`;
- preserves the frame trace and contract evidence;
- continues through early relink and the maintained QEMU frontier classifier.

Certification run `37497141242` completed SUCCESS. The vsyscall job recorded:

- `CORE_FRAME_REUSE function=update_vsyscall frame=128 value_bytes=32 values=275 objects=43`
- `UPDATE_VSYSCALL_FRAME=128`
- `VSYSCALL_FRAME_CONTRACT=PASS limit=2256`
- `QEMU_WATCH=PASS`
- `FRONTIER_VERDICT=MOVED_LATER`

This is stronger than keeping a separate static frame-only lane because the same canonical owner certification now guards both the frame bound and the resulting runtime frontier.

## Retirement decision

The following YAML files are preserved byte-for-byte under `.github/workflows-disabled/` and removed from the active Actions set:

- `linux-core-slot-reuse-frame-v0.yml`
- `linux-irq-frame-pack-v0.yml`

The newer `linux-irq-frame-runtime-v0.yml` and `linux-check-cpu-stall-runtime-v0.yml` remain active for now because they contain runtime/fault-path evidence beyond the retired static frame measurements.

No historical YAML, commits, Actions runs, or artifacts are deleted.
