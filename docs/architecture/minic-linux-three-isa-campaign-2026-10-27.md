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
targets, pinned upstream Linux 6.6.143 SHA, and **ONE common, versioned Kconfig
feature policy** (`tools/ci/linux-three-isa-common.config`) applied to GNU
`make KCONFIG_ALLCONFIG=... allnoconfig` on all three architectures.
This is the default **first-stage comparable C/IR discovery baseline**, not
three dissimilar vendor `defconfig` stress tests. Linux still resolves
arch-only CPU/ABI/boot/platform dependencies separately. The script validates
every requested shared feature against each effective `.config` and saves a
profile SHA and resolution receipt; silently dropped shared features FAIL.
The original vendor `defconfig` is opt-in `MINIC_LINUX_CONFIG_PROFILE=broad`
for a later extended real-software stress run and must never be compared as
equivalent work units to the common profile. Both profiles freeze source,
toolchain, target Kconfig SHA and complete original GNU C-owner manifests,
with honest scope labels.

A shared Python wrapper DERIVES actual GCC C owners from
the existing canonical `.o.cmd` verifier and matches target ELF machine.
It emits `discovery.json`, `full-c-owners.txt`, source/config/GCC metadata,
original object SHA manifest and exclusions, or fails closed.

**The verdict is intentionally `GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC`.**
The workflow does not start the kernel, compile C with MiniC, check Linux
runtime, or reuse an RV64 object count. A new target's GNU image must first
run in matching QEMU with a frozen initramfs before it becomes an accepted
golden; LoongArch's EFI/firmware setup is distinct.

## Next grouped execution, not one-error-at-a-time
1. First freeze the three **same-feature common** GNU builds and their
   resulting arch-resolved `.config` manifests; record all residual
   Kconfig and C-owner differences. Separately certify each ISA's original
   GNU/QEMU boot image; the common first-stage C corpus alone is not boot proof.
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

## Scope and architecture rule

Do **not** demand byte-identical `.config` across mutually exclusive
`CONFIG_X86_64`, `CONFIG_ARM64`, and `CONFIG_LOONGARCH`; demand one shared
feature policy and independent Kconfig dependency resolution with explicit
per-target attestation. Do not normalize C-owner counts, since build output
still includes legitimate ISA-specific files. A broader config is a **second
workload**, not the first comparable baseline. Reuse existing Kbuild,
C-owner selection, GNU preprocessing, frozen-input replay, and shard manifest
contracts instead of copying an architecture-specific compilation engine.
