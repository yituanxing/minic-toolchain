# Linux Runtime Validation v1

## Purpose

The Linux static corpus proves that MiniC can compile real Linux translation units. This milestone proves that the generated code actually executes correctly inside a real Linux kernel.

The runtime program remains deliberately outside the compiler implementation:

```text
Linux source
  -> external Kbuild/GCC preprocessing
  -> MiniC C compiler
  -> external GNU assembler/linker/Kbuild
  -> Image / modules
  -> qemu-system-riscv64
  -> observable runtime contracts
```

No compiler fallback is permitted inside a MiniC-owned object set. Mixed GCC/MiniC kernels are a diagnostic and incremental-validation technique, not a claim of full takeover.

## Historical Python evidence being carried forward

The Python MiniC line established five useful validation layers.

### R0 — userspace execution smoke

Small RV64 programs were compiled, linked, and executed under qemu-riscv64. Process exit status was the oracle.

The C rewrite already has this layer in `tests/compiler/c0/run-runtime.sh`.

### R1 — initramfs shell gate

A GCC/Kbuild reference kernel was first proven with the same Linux 6.6.143 source, config, initramfs, QEMU and OpenSBI environment.

Two stable endpoints were used:

- `rdinit=/init`: require `Run /init as init process`, `USER_SHELL_OK`, and `DONE_COMMANDS`.
- `rdinit=/bin/sh`: require `Run /bin/sh as init process`, `RDINIT_SH_OK`, and `DONE_RDINIT`.

A shell prompt alone is not sufficient. Any kernel Oops, BUG, panic, unhandled signal, soft lockup or hung task makes the run fail.

### R2 — exact mixed-object takeover and bisection

For a candidate MiniC object set:

1. restore the complete known-good GCC object set;
2. overlay only the selected MiniC objects;
3. invalidate/rebuild the affected archives and final Image;
4. verify object provenance/hashes after relink;
5. boot the exact resulting Image;
6. require the runtime endpoint and reject all bad-kernel markers.

If a large candidate set fails, bisect the object set until a single object or minimal cluster is isolated. Fix the general compiler defect, then replay the focused runtime gate and the accumulated runtime baseline.

This is the runtime analogue of the current focused-sample/full-corpus static workflow.

### R3 — rootfs and syscall/functionality gate

The historical rootfs flow attached a disposable qcow2 overlay backed by the frozen raw RISC-V rootfs image through `virtio-blk-device`.

The regression ladder covered, as available in the active kernel config:

- initramfs and external rootfs mount;
- switch_root and shell;
- fork/exec/waitpid;
- eventfd/timerfd/signalfd;
- epoll/inotify;
- socketpair;
- IPv4/IPv6 and TCP;
- io_uring;
- overlayfs/squashfs;
- block/loop and filesystem read/write;
- a post-test `make-still-works` style userspace sanity check.

Rootfs images must never be modified in place by parallel experiments. Use a qcow2 overlay for each run.

### R4 — modules and real functional gates

Module validation compares GCC and MiniC lanes using the same kernel/runtime harness.

A module is not promoted merely because it links or insmods. The gate checks:

- module load/registration;
- expected symbols, dependencies, vermagic and undefined-reference closure before runtime;
- real behavior where possible;
- zero functional failures;
- endpoint marker;
- normal guest powerdown;
- zero bad kernel/module markers.

The Python line eventually reached 145 `MODULE_PASS`, 203 `FUNCTION_PASS`, zero failures, a final endpoint marker and normal powerdown. Real behavior included KVM API probing and cryptographic known-answer/digest checks.

## C MiniC runtime rollout

The first C runtime milestone should not begin with an all-MiniC kernel. Use the following pressure ladder:

```text
R0 current compiler runtime suite
  -> R1 GCC reference initramfs gate
  -> R1 one/few MiniC critical objects
  -> R2 focused mixed-object batches
  -> R2 progressively larger batches
  -> R3 all-MiniC built-in kernel + initramfs/rootfs
  -> R4 modules and functional gates
```

Suggested built-in takeover sizes are evidence-driven rather than ceremonial, but a useful first schedule is:

```text
8 critical -> 16 -> 64 -> 256 -> 1024 -> all
```

When a larger step fails, immediately narrow the newly added MiniC set instead of debugging a full kernel monolithically.

## Mandatory runtime oracle

Every Linux runtime run must record:

- exact compiler commit;
- exact Linux commit/config;
- exact object manifest;
- GCC/MiniC provenance for every replaced object;
- Image/vmlinux hashes;
- QEMU/OpenSBI/toolchain versions;
- QEMU exit status;
- required endpoint markers;
- count/list of forbidden markers;
- normal powerdown status where that profile expects powerdown.

The common forbidden-marker set is:

```text
Kernel panic
Oops
BUG:
Unable to handle kernel
unhandled signal
soft lockup
hung task
Unknown symbol
invalid module format
PLT error
```

A timeout is a failure unless the specific profile explicitly defines an interactive endpoint and has already emitted its completion marker.

## Promotion rule

A runtime capability is promoted only when:

1. the GCC reference lane passes;
2. the MiniC lane passes the same observable contract;
3. MiniC object provenance is exact;
4. there are no forbidden markers;
5. the focused reproducer is added to permanent compiler/runtime regression when a compiler defect was fixed.

Static compile success alone is never runtime evidence.
