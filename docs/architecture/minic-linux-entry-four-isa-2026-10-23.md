# MiniC four-backend Linux entry audit — 2026-10-23

## Qualification policy

Build output is not a compiler capability until the genuine original-source
Kbuild C owners have been SHA-matched, the resulting executable has linked
with the native GNU target linker, and it has matched a baseline under QEMU.
This distinction is deliberate: MiniC supports C object production; its
MiniAS/AR/LD peers do not yet provide a four-ISA Linux-capable pipeline.

The existing immutable BusyBox 1.36.1 source commit is
`1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4`.
The exact enabled Kconfig options and C-owner count are **target-specific**.
The existing x86 certification is 593/593 and 11 GNU-byte-exact fixtures;
AArch64 and LoongArch64 independently certify 581/581, 38 GNU/QEMU
fixtures. RV64 previously passed a different BusyBox 1.38.0 frozen
408-applet setup: that is not a BusyBox 1.36.1 four-ISA certification.

This change adds RV64 to the existing original BusyBox cross-ISA acceptance
code and existing focused T1 job. It uses the same pinned source and
`allyes_sweep.py`/real-Kbuild object-replacement/SHA/QEMU machinery.
For RV64, the acceptance count is **discovered**, with an initial >500-object
sanity bound; every discovered owner must be MiniC, no GNU C fallback. Do not
retroactively call this milestone accepted until its GitHub Actions
`FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=PASS target=riscv64` receipt appears.
A failing GNU Kbuild, MiniC object compile, relocation, or GNU/QEMU byte
difference is a **failure**, not a discovery success.

## Current compiler architecture and Linux readiness

Pipeline: original source preprocessing -> Frontend semantics -> Core SSA/CFG
-> selected `MinicBackend` -> Target ABI/frame/ISA machine emission ->
target GNU assembler/linker -> executable/QEMU.

1. **Selected backend remains central.** Four registered architectures in
   `src/target/backend.c` select concrete `prepare_program` and
   `emit_program` functions. No parallel Linux-only compiler emission
   pipeline is warranted. RV64 prelayout is an intentional target strategy.
2. **New x86 inline ASM reuses the real abstraction.**
   `x86_64/asm_contract.c` classifies GNU constraints, and
   `core_lower_asm_target.c` materializes the approved operand roles into
   existing structured Core effects; `x86_64/machine_asm.c` emits them.
   New ISA forms must extend target qualification, not introduce Core
   `if (target == x86)` branches. Existing ordered RV64 probes remain
   transitional; align on actual repeated capability demand.
3. **Vararg cursor policy is target-owned.** `TargetInfo.variadic_copy_kind`
   separates scalar cursor assignment and state copying. The current x86
   SysV state copy is a 24-byte machine operation. Do not treat AArch64's
   transitional scalar cursor policy as proof of full AAPCS64 semantics.
4. **Common passes are semantic; ABI remains specific.**
   Shared Core and type/data-layout facts are correct ownership. Do not
   merge independent SysV, psABI, AAPCS64 and LoongArch register/stack
   rules merely to reduce source line counts. Physical registers and
   relocation encoding never enter Core. A Machine IR is an extension seam,
   not an excuse to rewrite all proven machine emitters before Linux.
5. **Remaining Linux-era risks.** Driver/Target preprocessing predefines,
   target ABI aggregates/varargs, inline asm admissibility, TLS/GOT/PIE,
   atomics, relocations, stack alignment, assembler/linker support,
   and Kbuild feature configurations will vary among architectures.
   A successful BusyBox link does not validate these kernel requirements.
6. **CI contract.** One branch (`main`), strict build/ABI checks first,
   one original BusyBox per ISA, then reuse the established seven-shard
   RV64 Linux 6.6.143 compile/SHA/GNU link/QEMU P1 acceptance approach
   when moving each additional ISA to Linux. Never reuse RV64's 2064
   object count for an unrelated kernel configuration/ISA.

## Linux staging gates

First preserve RV64 Linux 6.6.143 2064/2064 and QEMU P1 as the immutable
reference *method*, not a cross-ISA performance target. For each of
x86-64, AArch64, LoongArch64: freeze native GCC Kconfig, real owner
inventory, target GNU assembler/linker and target QEMU boot contract;
preflight input macros, ABI and asm by **class**; implement each
missing capability through the existing Frontend/Core/Target interface.
Batch coherent fixes, review the whole resulting data path, then perform
one seven-shard original-image validation before merging and pruning CI.
Never pass a producer receipt off as a bootable-image receipt.
