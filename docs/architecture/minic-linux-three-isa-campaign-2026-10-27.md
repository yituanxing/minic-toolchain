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

## Stage 1 / strict 3 × 7 runtime replay operating contract

This is the operational continuation of the historical RV Linux and MiniAS
seven-way campaigns, **not a new compiler/build architecture**. The authoritative
workflow is `.github/workflows/minic-linux-three-isa-seven-runtime-v1.yml`;
the owner of the transport is `tools/ci/linux-three-isa-seven-runtime-v1.py`,
and the unchanged GNU relink and QEMU oracle belongs to
`tools/ci/linux-three-isa-minic-candidate-v1.py`.

1. **Original GNU phase, once per frozen workload.** Build real upstream Linux
   6.6.143 for each ISA with its genuine Kbuild, toolchain, resolved `.config`,
   full `.o.cmd` C-owner list, original GNU C `.o`, `.i`, `.cmd`, any `.incbin`
   sidecars, GNU boot image and an independent GNU QEMU P0/P1 oracle. Freeze
   the exact sources, flags, target ELF ABI, ordered C-owner census and their
   SHA receipts. The initial reviewed exporter run is **41108659343**.
   Its 21 input artifacts are reused until a deliberately regenerated and
   re-certified source epoch is promoted; the artifact retention period is
   finite, so do not assume they will remain downloadable indefinitely.
2. **One compiler, 21 independent producers.** Build one immutable MiniC/driver
   pair at the current head. For each x86-64/AArch64/LoongArch64 target,
   split the full frozen ordered C-owner census into seven disjoint slices
   (`rows[shard::7]`). Each slice compiles every C input using **MiniC for C**
   and target **GNU AS for assembly**. Real `.cmd` flags and `.incbin`
   dependencies remain attached to the same source owner. GCC compiling any
   candidate C object is forbidden. Shards publish exact per-object `.o` and
   `.s` hashes, compiler identity, toolchain identity and original GNU source
   epoch. MiniAS assembly-only files are *not* additional MiniC C owners.
3. **Cache by provenance, never by claimed success.** Cache only a SHA-verified
   shard from the identical source/config/ordered owners/flags/compiler/GNU AS
   and external-resource identity. Cache hit still produces real candidate
   artifacts; it does not certify GNU LD or QEMU. A source file, `.cmd`,
   target ABI, compiler, assembler or required sidecar change invalidates the
   affected entry/partition. Preserve independently successful shards while
   repairing the other blockers; do not rebuild the entire GNU kernel for
   every compiler source edit.
4. **Fail before the costly receiver.** Download all seven candidate artifacts
   for a target *before* restoring the large GNU Kbuild cache. `preflight`
   rejects missing/extra shards, partial counts, duplicate owners, malformed
   success receipts, mismatched frozen GNU and MiniC identities, foreign ELF
   machine, missing object or SHA mismatch. This is a transport check only;
   `receive/collect` must then compare the **exact ordered source and GNU ABI
   membership** against the authenticated GNU build. Neither check declares
   a boot PASS.
5. **Three GNU LD/QEMU receivers, independently.** Restore the unchanged
   original GNU Kbuild/headers/linker scripts/other GNU objects and its own
   verified P0/P1 golden, replace **all and only** the original GNU C owners
   with the complete 7-way MiniC union, run the authentic GNU Kbuild link,
   and verify every live regenerated C owner through the existing bounded
   `-E -> MiniC -> GNU AS` epochs. Then boot the relinked actual Image in QEMU
   for P0 and P1, comparing with the independent GNU golden. Preserve linker
   diagnostics, source-owner receipts and both QEMU logs for each target.
   x86, arm64 and LoongArch receivers may fail independently; never suppress
   one architecture's evidence because another failed.

### How to advance without the old false positives

- **Explicit campaign trigger:** update only
  `tools/ci/linux-three-isa-seven-runtime-trigger.txt` in a commit on
  `refactor/minic-target-language-boundary`. Routine compiler commits must
  never start the large 3 × 7 replay implicitly. A manual `workflow_dispatch`
  declaration also exists for when this workflow is installed on `main`, but
  GitHub does not enable that entry point for a workflow existing only on this
  development branch; the branch marker is the reliable present entry point.
- **Explicit original GNU regeneration:** change the same marker with
  `[minic-regen-gnu]` in the head commit message. All 21 producers then consume
  this *same workflow run's* newly generated inputs instead of the pinned
  exporter. Do not update `MINIC_LINUX_PINNED_INPUT_RUN` until the new source
  run has all required GNU, link, P0/P1 and identity evidence. A new Kconfig
  profile/source version is a separate golden epoch, never a silent cache hit.
- **First collect all failures:** after a full campaign, enumerate *every*
  exposed blocker across all target C owners and all three receiver stages.
  Assign independent agents to distinct root-cause/ISA groups. Fix the whole
  exposed batch, replay affected owners locally or with the existing cache,
  and trigger one new explicit campaign after the batch. Do not push tiny
  one-error commits, rerun unchanged failures or replace this with cherry-picked
  smoke cases. Reuse the older RV/MiniAS full-set, prefix/single-owner and
  all-except diagnostic technique **only to isolate post-compile failures**;
  a partial set cannot be called a Linux acceptance.
- **Truthful labels:** `gate=success` proves only authorization;
  `21/21=success` proves only frozen C -> MiniC -> GNU AS coverage;
  receiver `GNU LD=success` proves a linked Image, not runtime;
  **Linux PASS** requires all three original GNU/QEMU golden references,
  zero GCC candidate C fallback, exact full owner/ABI union, accepted C-option
  equivalence, fresh candidate relink and actual independent QEMU P0 and P1
  PASS. Record separate per-ISA stage counts, run URL/SHA and changed-line
  totals with each meaningful batch.

Do not confuse the common-profile 9,589 MiniC C-owner corpus
(x86-64 2,758; AArch64 4,108; LoongArch64 2,723) with MiniAS's 9,645
assembly-inclusive owners. Nor does historic RV64 2,064/2,064 or any green
gate-only CI satisfy this campaign's three actual Linux runtimes.
