# MiniC Linux 6.6.143 GNU/QEMU relink ledger — 2026-10-30

This ledger tracks **real Linux runtime qualification**, not generic MiniC
compiler-rt success, assembler-only success, or a green workflow with skipped
`gnu-boot` jobs. MiniC owns **only C compilation**; original GNU preprocessor
inputs, GNU AS, GNU Kbuild/LD, and QEMU remain the reference toolchain. No GNU
C-object fallback is permitted in the candidate kernel.

## Last completed full Linux attempt

- Evidence: [GNU/QEMU CI #41085043124](https://github.com/yituanxing/minic-toolchain/actions/runs/41085043124), exact pushed head `77491ce7`.
  All three `gnu-boot` jobs **actually ran** and failed at candidate GNU final
  link. In all three, original GNU golden Linux QEMU P0/P1 was **2/2 PASS**.
  No candidate Image or QEMU boot occurred, and no GNU C fallback was allowed.
- x86-64: **2,758/2,758** frozen C owners compiled with MiniC and GNU AS.
  Linker undefined references fell **28 -> 11**, unique symbols **24 -> 9**
  versus [previous full attempt #41050392970](https://github.com/yituanxing/minic-toolchain/actions/runs/41050392970):
  seven `__compiletime_assert_*`, three `dax_get_private`, one `crc32c`.
  The candidate ELF was 245,890,576 B vs GNU 16,075,966 B.
- AArch64: **4,108/4,108** C owners compiled with MiniC/GNU AS. Linker
  undefined references **198 -> 13**, unique symbols **109 -> 9**. Remaining
  are eight ordinary `__compiletime_assert_*` plus five
  `__kvm_nvhe___compiletime_assert_*`; GNU link also separately reported
  unexpected GOT/PLT and run-time procedure linkages. Candidate ELF
  263,322,300 B vs GNU 17,941,440 B.
- LoongArch64: **2,723/2,723** C owners compiled with MiniC/GNU AS. GNU
  linker still failed on `head.o:(.ref.text+0x15c)` -> `start_secondary` with
  `R_LARCH_B26`, measured displacement -144,063,268 B, exceeding the signed
  +/-128 MiB range by about 9.39 MiB. Candidate ELF 147,182,348 B vs GNU
  16,887,804 B; no candidate QEMU boot occurred.
- The separate success-marked native-defconfig workflow had **skipped** its
  actual `gnu-boot` jobs and is explicitly **not** Linux runtime evidence.

## Second local batch (replay evidence; awaiting new exact-head GNU/QEMU)

| ISA | Frozen original Linux input or kernel source | Confirmed local result |
| --- | --- | --- |
| x86-64 | `drivers/gpu/drm/drm_edid.i` | `__compiletime_assert_428` removed by pure, bounded `static const` record-array read/inline getter; GNU `-O2` and side-effect negative fixtures agree. |
| x86-64 | `drivers/md/dm.i` | All three remaining `dax_get_private` undefined calls removed using joint Core function/static-global-object graph; GNU `-O2` + GNU AS/`nm -u` strict comparison PASS. |
| x86-64 | `net/netfilter/nf_nat_proto.i` | Remaining `crc32c` undefined call removed by the same reachability analysis; GNU `-O2` + GNU AS/`nm -u` PASS. File ASM, `used`, section, alias, volatile roots retained. |
| AArch64 | `arch/arm64/kernel/patching.i` | `__compiletime_assert_317` removed by safe static helper cloning across two exact operand-free/identity-output empty GNU asm shapes; actual BUG table assembly remains. |
| AArch64 | KVM `arch/arm64/kvm/arm.i`, `pmu.i`, `vgic/vgic.i` | Three `kvm_make_request` static pure guard assertions removed on original frozen GNU `.i`. |
| AArch64 | KVM `hyp/nvhe/pkvm.nvhe.i`, `sys_regs.nvhe.i` | All five `__kvm_nvhe___compiletime_assert_{519,550,556,562,574}` discharged on original GNU `.i` by nested pure `__builtin_ffsll` argument and FIELD_GET guard proof. |
| LoongArch64 | Frozen vDSO `vdso.i`, `vgettimeofday.i`, `vgetcpu.i` | Shared Core RHS non-CFG snapshot elision + target immediate/one-use optimizations yielded native mnemonic counts 3,331 -> 3,269; 4,180 -> 4,142; 206 -> 200 on these samples. The B26 9.39 MiB gap is **not** yet proven solved. |

The local fixture probes include GNU `-O2` reference, x86-64 MiniC→GNU AS/LD
execution, five-target MiniC `-S`, and negative cases that must remain dynamic.
Only the full Linux CI can certify original Kbuild flags, final relocations,
ELF Image, and candidate QEMU P0/P1.

## Confirmed remaining or structurally unresolved

1. **x86-64**: still lack exact frozen `.i` for five linker-reported assertion
   owners: `_461` (`intel_gt_irq.c`), `_697` (`nf_conntrack_extend.c`),
   `_2121` (`mac80211/tx.c`), `_369` (`ioremap.c`), `_287` (`kaslr.c`). The
   sampled `fs/proc/task_mmu.i` `_347` also remains side-effect sensitive.
   The candidate owner archiver now resolves actual x86 object relocations to
   capture missing `.i/.cmd` on the *next* failed CI, not by guessing.
2. **AArch64**: `__compiletime_assert_758` in `kernel/bpf/helpers.o` remains
   unreplayed; five NVHE/four KVM repairs and fixmap require GNU relink proof.
   Independently inspect final GOT/PLT dynamic-symbol check. The GNU original
   preoverlay has 82 GOT relocations, MiniC 78, so GOT count alone is not a
   correctness diagnosis; inspect `undefined weak` direct call semantics and
   linker symbol binding without weakening linker checks.
3. **LoongArch64**: vDSO instruction reductions are real but representative
   samples cannot certify a >=9.39 MiB **whole-kernel** text reduction. Keep
   exact `R_LARCH_B26` acceptance, and pursue target-neutral SSA liveness and
   proven register residency before another relink.
4. **All**: original GNU Linux C options versus MiniC are still marked
   `INCONCLUSIVE_UNQUALIFIED` by the ledger (not falsely `equivalent`). This is
   a separate qualification gap from 100% source ownership.

## Mandatory next gate

The branch workflow `.github/workflows/minic-linux-three-isa-gnu-qemu-v1.yml`
has an explicit gate. The **last pushed commit** must modify
`tools/ci/linux-three-isa-gnu-qemu-trigger.txt`: `campaign-gate` checks
`git diff HEAD^ HEAD` of that path. The three `gnu-boot` jobs must each
actually **run**, not be `SKIPPED`, and report per-ISA GNU golden P0/P1,
MiniC all-owner C→S, GNU AS, GNU LD, candidate Image and QEMU P0/P1. After
their artifacts arrive, repair the entire observed batch and rerun on a new
exact HEAD without silently swapping compilation stages or fallback objects.
