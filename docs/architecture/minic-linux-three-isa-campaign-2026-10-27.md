# MiniC Linux 6.6.143 three-ISA campaign — issue #675
## Ownership
x86-64, AArch64 and LoongArch64 run **simultaneous golden Kbuild discovery**.
The original RV32/RV64 Linux chain remains the reference method. No self-hosting.

Common once: original Linux SHA, GNU `.cmd`-based C owner census
(`linux-runtime-gcc-full-c-objects-v1.py`), 7-shard cached object union,
failure classification, provenance SHA, exact-head acceptance. Target cells:
Kbuild ARCH, GCC/AS/LD, genuine Kconfig, ELF e_machine/ABI, boot image,
QEMU machine/firmware, inline ASM/register/relocation legality. These MUST
never leak as `if (arch == ...)` into Frontend/Core algorithms.

## Stage 0 / what the NEW opt-in workflow proves
`minic-linux-three-isa-discovery.yml` uses one GNU build script for all 3
targets, pinned upstream Linux 6.6.143 SHA, independent original GNU Kconfigs
and fresh builds. A shared Python wrapper DERIVES actual GCC C owners from
the existing canonical `.o.cmd` verifier and matches target ELF machine.
It emits `discovery.json`, `full-c-owners.txt`, source/config/GCC metadata,
original object SHA manifest and exclusions, or fails closed.

**The verdict is intentionally `GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC`.**
The workflow does not start the kernel, compile C with MiniC, check Linux
runtime, or reuse an RV64 object count. A new target's GNU image must first
run in matching QEMU with a frozen initramfs before it becomes an accepted
golden; LoongArch's EFI/firmware setup is distinct.

## Next grouped execution, not one-error-at-a-time
1. Freeze each target's genuine GNU QEMU booted image and `.config`.
2. Produce SHA-bound `.i` for each `.cmd` C owner and 7-way per-target
   whole-source census (`make -k`) with one compiler revision. Collect
   ALL errors, grouped by root cause and target intersection.
3. Fix cross-architecture language/Core capabilities exactly once; leave
   ABI/register/inline-asm specifics in target backends. Replay only the
   affected `.i` with `linux-runtime-failed-tu-replay-v1.py`, then GNU AS
   and a narrowed actual QEMU differential. Reuse verified successful inputs.
4. Only after one coherent batch and all owners are clean: SHA-verify ALL 7
   shards, actual GNU Kbuild link, single Image and QEMU P0/P1. Enforce zero
   GNU C fallback and no skipped/shard-only false PASS.
5. Validate RV32I/ILP32 1741/1741 and RV64 2064/2064 source/ISA/QEMU
   requirements whenever Common or their target code changes.

Existing #674 exact-head strict, 4-ISA runtime, RV32 software, x86 BusyBox,
RV64 origin smoke and GNU baseline CI passed. The historical `2064` Linux
Image/P1 is not the new three-architecture qualification.
