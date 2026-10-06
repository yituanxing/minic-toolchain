# Registered spinlock bridge retirement

This cleanup retires the historical registered-path runtime bridge stored under the stale filename `.github/workflows/linux-6.6.143-discovery.yml`.

## Why the filename no longer describes the workflow

The file originally served Linux discovery work, but on 2026-09-23 it was repurposed as a temporary GitHub Actions registration bridge. Its current workflow name is `Linux Runtime Spinlock Registered Bridge V0`, and its only push path is `tools/ci/runtime-mm-core-trigger.txt`.

The bridge rebuilds the runtime MiniC profile, refreshes the PI/SATP/setup/scheduler/IRQ/spinlock owner set, verifies the SATP CSR window, relinks the early image, and executes the current runtime frontier. A later revision also refreshes `kernel/sched/build_utility.o` to diagnose the historical `calc_global_load` frontier.

## Formal maintained bridge

The maintained workflow is `.github/workflows/linux-runtime-mm-core-frontier-v0.yml` (workflow name `Linux Runtime Init IRQ Bridge V0`).

It owns the same `tools/ci/runtime-mm-core-trigger.txt` path and additionally carries `tools/ci/runtime-init-irq-trigger.txt`. The 2026-10-06 consolidation commit `a62dff4ee42eefaeb7df4836b8f5c623a01b052c` folded the complete init-IRQ contract into this formal workflow.

Historical same-trigger evidence already showed the two workflows running in parallel successfully:

- trigger commit `6ee2b3cd042dc143f839de482d75c3a72e1300e4`
  - formal bridge run `35890062876`: SUCCESS
  - registered bridge run `35890062994`: SUCCESS
- trigger commit `5d66ae01cd3a896681d61e757e372bf5e81fb53a`
  - formal bridge run `37007882942`: SUCCESS
  - registered bridge run `37007882881`: SUCCESS

## Current post-consolidation certification

A pure trigger commit, `641831285b02ae3de163c7c32f9ab34cd9c3025f`, changed only `tools/ci/runtime-mm-core-trigger.txt` to recertify the maintained bridge after cleanup/runtime convergence and the init-IRQ consolidation.

Formal bridge run `37492352866` completed SUCCESS. It records:

- `PI_OWNER_REFRESH=PASS local_string=lla`
- `SATP_OWNER_REFRESH=PASS target=arch/riscv/mm/init.o`
- `SETUP_OWNER_REFRESH=PASS target=arch/riscv/kernel/setup.o`
- `SCHED_OWNER_REFRESH=PASS target=kernel/sched/core.o`
- `IRQ_OWNER_REFRESH=PASS target=arch/riscv/kernel/irq.o`
- both spinlock owners refreshed successfully
- `SATP_WINDOW=PASS`
- `EARLY_LINK_V2=PASS`
- `QEMU_WATCH=PASS`
- `FRONTIER_VERDICT=MOVED_LATER`
- `FRONTIER_FAULT=store_page_fault:calc_global_load+0x2a`
- `FRONTIER_HIGHEST_PROGRESS=init-irq`

The registered bridge's additional `kernel/sched/build_utility.o` refresh is not unique coverage. That owner is already refreshed by maintained workflows including:

- `linux-runtime-generated-kallsyms-first-die-v0.yml`
- `linux-runtime-first-die-context-v0.yml`
- `linux-runtime-owner-focused-v1.yml`
- `linux-runtime-spinlock-first-context-v0.yml`
- `linux-runtime-get-current-owner-v0.yml`

## Retirement decision

`.github/workflows/linux-6.6.143-discovery.yml` is preserved byte-for-byte under `.github/workflows-disabled/linux-6.6.143-discovery.yml` and removed from the active Actions set.

The maintained `.github/workflows/linux-runtime-mm-core-frontier-v0.yml` remains active as the registered init-IRQ/mm-core runtime bridge.

No compiler source, runtime implementation, historical YAML, commits, workflow runs, or uploaded evidence are deleted by this change.
