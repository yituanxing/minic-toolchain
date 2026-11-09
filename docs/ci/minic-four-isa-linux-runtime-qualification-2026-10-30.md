# MiniC original Linux 6.6.143: four-ISA runtime qualification

This document distinguishes **building every original GNU Kbuild C owner**
from **creating and booting a full Linux Image**. It is a runbook, not a PASS
certificate. Never promote a 21-shard `.i -> MiniC -S` census, a four-ISA
compiler microtest, or an old QEMU image to a current-head Linux runtime PASS.

## Immutable differential contract

1. Use original Linux 6.6.143 archive SHA-256
   `dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932`.
   GNU builds fix Kbuild identity and freeze each target's resolved `.config`.
2. Derive **all** eligible native ELF64 C owners from original GNU Kbuild
   `.o.cmd` records. Record excluded non-C, host tools and ABI-separate ELF32
   objects; exclusions are not a MiniC pass. No silent TU substitution.
3. Produce original GNU kernel, GNU-built identical initramfs and two QEMU
   logs as the *golden runtime oracle* for each ISA before judging MiniC.
4. For a candidate, compile **every** original C owner with the exact-head
   MiniC driver and its actual sibling `minic-cc`; lock **both executable SHA
   hashes** before and after the producer. Use GNU assembler, archiver, linker
   and image utilities as permitted tooling, but **zero GCC-compiled kernel C
   fallback objects**. Record per-object ownership, ELF machine, build logs
   and SHA hashes.
5. Re-link all target kernel outputs with the candidate C objects. **After
   Kbuild relinks, independently re-enumerate the full live `.o.cmd` C-owner
   universe**, require exact equality with the frozen GNU owners, and reject
   every hash mismatch or newly generated GCC-compiled owner. Run both GNU
   and MiniC images under real system QEMU, separately P0 and P1, using the
   **same Kconfig, kernel source, initramfs, firmware and QEMU ABI**.
6. Refuse skipped QEMU, nonzero exit, timeout, kernel panic/Oops/BUG,
   missing/multiple markers, a changed syscall order, any of the 12 missing
   probes, wrong kernel release or mismatched SHA provenance. The P1 guest
   deliberately also prints one `loopback-up` setup success before the IPv6
   probe: require **exactly the 12 named tests plus that one auxiliary event,
   in source-defined order**, rejecting any unknown or duplicate event.
   Preserve full failed evidence; rerun *all* blocked owners after a batch fix.

## Distinct gates and current proof limits

| Target | Historical proof (not current-head acceptance) | Required new proof |
| --- | --- | --- |
| RV64 | Stage4 2064/2064, GNU link, Linux P1 12/12; same Image had 12 boots | Exact HEAD seven-way 2064 C, GNU link, P0/P1 and 12 clean independent boots on one SHA-locked Image |
| x86-64 | Current census 2758/2758; original GNU Linux QEMU P0/P1 logs independently validated, not MiniC | All-owner MiniC `.o`, GNU link, candidate P0/P1 and exact GNU/MiniC differential |
| AArch64 | Current census 4078/4078; GNU/QEMU Linux run pending | Same full GNU/MiniC linked QEMU differential |
| LoongArch64 | Current census 2585/2723; all 138 original failed `.i` subsequently pass locally after target-only regression fix | Exact-head 2723/2723 census, SHA-authenticated UEFI GNU P0/P1, all-owner MiniC linked QEMU differential |

The `tools/ci/linux-three-isa-strict-source-gate-v1.py` job rejects even one
failed C owner but explicitly prints `AS_LINK_QEMU=NOT_RUN`. The
`tools/ci/linux-three-isa-gnu-qemu-v1.py` oracle emits
`GNU_QEMU_P0_P1_PASS_NOT_MINIC` only after real GNU boots. The
`tools/ci/linux-three-isa-minic-candidate-v1.py` producer then attempts the
full original C-owner MiniC/GNU AS replacement, original GNU relink and real
P0/P1 QEMU candidate boots. It must preserve its failure receipt if blocked.
The
`tools/ci/linux-four-isa-runtime-diff-v1.py` consumer accepts paired actual
GNU and MiniC P0/P1 receipts and rejects missing provenance or GCC C fallback.
Passing each isolated self-test does **not** qualify a kernel image.

### First actual exact-head result (2026-10-30)

Run [40986864965](https://github.com/yituanxing/minic-toolchain/actions/runs/40986864965)
at `b085508010beb7fc449f0eb317cb01ec2a6a3ca6` completed all 21 MiniC
source shards: x86-64 **2758/2758**, AArch64 **4078/4078**, LoongArch64
**2585/2723**. The new `strict-source-gate` **failed correctly** on the 138
LoongArch C-owner errors. This is only source/assembly-text coverage, never
Kbuild link or QEMU runtime success. The RV64 run
[40986864977](https://github.com/yituanxing/minic-toolchain/actions/runs/40986864977)
failed all seven workers at missing GNU golden Actions cache, *before* MiniC
compilation; it is not seven compiler regressions. The reproducible cache
regeneration path must be retested independently.

The follow-up [RV64 run 40988245718](https://github.com/yituanxing/minic-toolchain/actions/runs/40988245718)
has now **reconstructed and passed** the original GNU golden Linux/QEMU
preflight. Its seven candidate shards subsequently stopped at the existing
GNU-reference object precheck: `usr/initramfs_data.o` differed after Kbuild
regeneration. This is a generated assembly initramfs payload, **not** one of
the 2064 eligible C owners. The replay now pins the same GNU Kbuild identity;
if this single generated payload still varies, the precheck explicitly
requires its original `.S` Kbuild command, the complete 2064-C owner manifest,
and the object's absence from that C manifest. Every other GNU object keeps
its bytewise SHA gate, and the **entire** original GNU snapshot (including
the generated payload) is restored before MiniC. This must be retested before
calling any RV64 MiniC runtime PASS; original GNU P0/P1 acceptance alone
does not qualify the candidate.
The independent [GNU three-ISA run 40987410907](https://github.com/yituanxing/minic-toolchain/actions/runs/40987410907)
produced the x86-64 original Linux build, 2758 frozen `.i`, and actual GNU
P0/P1 serial logs. Its original checker rejected the thirteenth success
event, but this was the `loopback-up` auxiliary event explicitly printed by
`tests/external/linux/runtime_initramfs/probe.c`; applying the corrected
strict 13-event verifier to both preserved original GNU logs passes. This
does **not** establish a MiniC runtime PASS. That run also completed original
AArch64 GNU Linux and froze **4078/4078** C sources, but QEMU P0 exited before
starting the guest because the runner lacked `efi-virtio.rom` for its implicit
default NIC. The revised three-ISA QEMU command uses `-nic none`, preserving
the P1 guest's internal IPv6 loopback test without depending on network ROMs.
LoongArch's first GNU run stopped before building because the EFI firmware
input was absent; the subsequent SHA-pinned firmware download requires a
fresh exact-head run.

## Campaigns

- Three-ISA 21-shard source census: bump `tools/ci/linux-three-isa-trigger.txt`
  as the last commit's change to run exactly one complete campaign.
- RV64 seven-shard full-QEMU replay: bump
  `tools/ci/linux-rv64-runtime-trigger.txt` on that exact PR HEAD.
- Three-ISA original GNU/QEMU oracle: bump
  `tools/ci/linux-three-isa-gnu-qemu-trigger.txt` on that exact PR HEAD.
  LoongArch firmware is mandatory, not an optional skipped success. The PR
  workflow downloads Loongson's official QEMU virt `QEMU_EFI.fd` at pinned
  upstream commit `9e2b59f4f0d0c40258fade9769cd93c8034fd436` and verifies
  its published SHA-256 `b375639ebcf7a873d2cab67f0c9c1c6c208fc3f1bcba4083172fa20c4fb7e1ab`
  before starting real QEMU; the resulting GNU boot still must independently
  succeed before any MiniC acceptance.

Always report each campaign's **exact commit SHA, actual jobs, C owner counts,
native `.o` and image identity, QEMU result, and unresolved blockers**. A
successfully queued job, green source-census collector with failed owners,
or skipped expensive matrix is not a successful runtime qualification.
