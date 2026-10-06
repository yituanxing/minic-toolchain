# Init-IRQ frontier retirement contract

The historical `.github/workflows/linux-runtime-init-irq-frontier-v0.yml` is superseded by `.github/workflows/linux-runtime-mm-core-frontier-v0.yml` (workflow name: Linux Runtime Init IRQ Bridge V0).

The bridge workflow preserves the complete old init-IRQ contract:

- the same certified linked fixture, pinned Linux source, and frozen linker subset;
- the same current MiniC runtime profile and PI owner refresh;
- refresh of `arch/riscv/mm/init.o`, `arch/riscv/kernel/setup.o`, `kernel/sched/core.o`, and `arch/riscv/kernel/irq.o`;
- the same SATP critical CSR-window validation;
- the same `tools/ci/linux-runtime-init-irq-frontier-v0.json` QEMU classifier;
- the same frontier result, summary, watcher metadata, and interrupt evidence.

It then extends the bridge with current MiniC `kernel/locking/spinlock.o` and `spinlock_debug.o` refresh plus nm/objdump owner evidence.

The old `tools/ci/runtime-init-irq-trigger.txt` path trigger is carried forward into the bridge workflow before retirement. The historical workflow YAML is preserved byte-for-byte under `.github/workflows-disabled/`.
