# Linux runtime spinlock first-context consolidation contract

`.github/workflows/linux-runtime-spinlock-first-context-v0.yml` is the candidate canonical first-spinlock diagnostic.

It preserves the complete historical `linux-runtime-spinlock-first-fail-v0.yml` workload:

- identical no-printk replacement of the three first validation paths in `debug_spin_lock_before`;
- explicit verification that all three diagnostic helpers were inserted;
- the same exact runtime MiniC profile and cumulative owner refresh;
- retained `spinlock_debug.o` symbol table, disassembly, MiniC stage-2 assembly, and focused symbol extract;
- the same early relink and event-driven QEMU watcher.

It additionally extracts the first reached diagnostic target and its resolved execution context into `first-target.json` and `first-target.txt`.

Canonical certification run `37464112725` on `agent/linux-expanded-kbuild-v0` completed SUCCESS. The augmented first-context workflow passed certified fixture restores, exact runtime MiniC profile construction, all instrumentation-presence checks, the full 18-owner refresh chain with retained `spinlock_debug` static evidence, early relink, QEMU recursion probe, first-target context extraction, and evidence upload. Therefore `linux-runtime-spinlock-first-fail-v0.yml` is superseded and may be archived without deleting its YAML history.
