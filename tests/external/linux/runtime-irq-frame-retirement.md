# IRQ frame runtime retirement contract

This cleanup retires the historical `linux-irq-frame-runtime-v0.yml` workflow after its durable runtime assertions were split across newer maintained contracts.

## Historical role

The workflow was introduced in September 2026 while the RISC-V Linux runtime frontier was still being debugged through packed/reused Core frames on the IRQ/time/scheduler path. It rebuilt a focused owner set including:

- `arch/riscv/mm/init.o`
- `arch/riscv/kernel/traps.o`
- `kernel/softirq.o`
- `kernel/locking/spinlock.o`
- `kernel/locking/spinlock_debug.o`
- `kernel/time/timekeeping.o`
- `kernel/time/vsyscall.o`
- `kernel/time/hrtimer.o`
- `kernel/sched/core.o`
- `kernel/sched/build_policy.o`

It also checked the real SATP critical window, rejected two historical runtime fault signatures, and reported page faults in the old IRQ guard address range.

Its last revision was commit `bfa7fcf9671e61c7c70cb046e797c120ada2ff4f`. GitHub Actions run `35064799007` completed SUCCESS and recorded:

- `OLD_SATP_FAULT=0`
- `OLD_IRQ_GUARD_FAULT=0`
- `IRQ_GUARD_RANGE_FAULTS=0`

## IRQ/fault-path coverage

The later `linux-check-cpu-stall-runtime-v0.yml` workflow retains the packed/reuse runtime path, rebuilds the same IRQ/time/scheduler/spinlock family plus `kernel/rcu/tree.o`, and keeps the generalized IRQ-guard page-fault rejection.

Its run `35112503404` completed SUCCESS and recorded:

- `IRQ_GUARD_RANGE_FAULTS=0`

That lane remains active for now because its RCU stall owner and generalized page-fault classification are not duplicated by this retirement.

## SATP coverage

The historical fixed-PC SATP rejection is superseded by the maintained `linux-runtime-satp-refresh-v0.yml` contract.

Canonical run `37463405710` completed SUCCESS and proves the real current owner rather than one historical PC value:

- `SATP_WINDOW=PASS`
- `REAL_SATP_WINDOW=PASS`
- current real-owner relink reached `FRONTIER_VERDICT=FRONTIER_PASS`

The SATP workflow verifies both the object-level CSR window and the stricter source-level rule that no instruction references `sp` between the real SATP CSR pair.

## Retirement decision

The historical workflow is preserved byte-for-byte as:

`.github/workflows-disabled/linux-irq-frame-runtime-v0.yml`

and removed from the active Actions set.

The following remain active:

- `linux-check-cpu-stall-runtime-v0.yml` for the broader IRQ/RCU fault-path lane;
- `linux-runtime-satp-refresh-v0.yml` for the canonical SATP differential and real-owner contract;
- `linux-fork-stack-runtime-v0.yml` because its `kernel/fork.o` owner and bad-stack first-fault oracle are separate concerns.

No historical YAML, commits, Actions runs, or artifacts are deleted.
