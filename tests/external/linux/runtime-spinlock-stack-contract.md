# Linux runtime spinlock stack consolidation contract

`.github/workflows/linux-runtime-spinlock-stack-v0.yml` is the candidate canonical static stack/frame diagnostic for `kernel/locking/spinlock_debug.o`.

It already rebuilds `spinlock_debug.o` through the exact runtime MiniC profile and derives reverse call-chain, frame-size, restore-size, and stack-mutation evidence from the generated stage-2 assembly.

It now additionally preserves the complete historical `linux-runtime-spinlock-frame-v1.yml` evidence for `debug_spin_lock_before`:

- full object disassembly and symbol table;
- extracted `debug_spin_lock_before` body;
- detected frame size;
- base address and `+0x1c` instruction;
- the first 24 prologue instructions.

Canonical certification run `37467194575` on `agent/linux-expanded-kbuild-v0` completed SUCCESS after the repaired augmented workflow was installed. It passed certified fixture restores, exact runtime MiniC rebuild of `spinlock_debug.o`, the full legacy `debug_spin_lock_before` frame evidence extraction, reverse call-chain/frame/restore analysis, and evidence upload. Therefore `linux-runtime-spinlock-frame-v1.yml` is superseded and may be archived without deleting its YAML history.
