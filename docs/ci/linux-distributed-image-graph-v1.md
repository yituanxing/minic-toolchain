# Full Linux Image object graph gate

The 120-object distributed Kbuild experiment succeeded in run 37762351340, but does **not**
prove complete Linux Image, archive membership, generated dependencies, or QEMU.

This path-triggered gate creates the **exact Linux Image runtime configuration** from
`linux-expanded-kbuild-v0.yml`: Linux 6.6.143 RISC-V `defconfig` plus BLK_DEV_INITRD,
DEVTMPFS, DEVTMPFS_MOUNT, SERIAL_8250 and SERIAL_8250_CONSOLE; `olddefconfig`,
`prepare scripts`. It then enumerates C object compilation commands from a real
`make -n V=1 Image`, preserving index/target/source order and splitting by 7 shards.

The manifest is a *candidate compiler C-object graph*, not the entire Kbuild graph:
assembly objects, generated rules, archives, linker scripts, kallsyms and other runtime
dependencies must be handled and certified separately. A nonzero dry-run return is
documented, never concealed; actual generated manifest must independently pass 2500-6000
bounds before use. No 55-minute full Image rebuild is triggered by this graph gate.

After the manifest gate passes, a separate isolated workflow can compile full C-object
shards with the exact runtime MiniC profile, carry `.o`/hidden `.cmd` identities,
reconstruct Kbuild archives on the receiver and actually build Image + QEMU.
