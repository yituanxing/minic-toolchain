# MiniC Linux 6.6.143 GNU/QEMU relink ledger — 2026-10-30

This ledger tracks **real Linux runtime qualification**, not generic MiniC
compiler-rt success, assembler-only success, or a green workflow with skipped
`gnu-boot` jobs. MiniC owns **only C compilation**; original GNU preprocessor
inputs, GNU AS, GNU Kbuild/LD, and QEMU remain the reference toolchain. No GNU
C-object fallback is permitted in the candidate kernel.

## Last completed full Linux attempt

- Evidence: [GNU/QEMU CI #41050392970](https://github.com/yituanxing/minic-toolchain/actions/runs/41050392970), `f6cd6586`.
- x86-64: 2,758 original C owners reached MiniC/GNU AS; GNU final relink
  failed with 28 undefined references to 24 symbols (20 compile-time assert
  references, two `__cfi_sites*`, six other references).
- AArch64: 4,108 original C owners reached MiniC/GNU AS; GNU relink failed
  with 198 undefined references to 109 symbols (predominantly static
  compilation guards, including KVM `__fls`).
- LoongArch64: 2,723 original C owners reached MiniC/GNU AS; GNU relink
  failed at `head.o:(.ref.text+0x15c)` with `R_LARCH_B26` out of range to
  `start_secondary`. Candidate `.text` was 145,119,440 bytes (about 8.75x
  the GNU baseline), above the approximately 128 MiB direct-branch range.
- The next two source fixes, `ba3a5b6d` and `61ec96f6`, did **not** cause a
  full-Linux rerun: corresponding `gnu-boot` jobs were skipped. Their runtime
  result must not be invented from the 4/4 general runtime workflow.

## Current local batch (replay evidence; awaiting exact-head GNU/QEMU)

| ISA | Frozen original Linux input or kernel source | Confirmed local result |
| --- | --- | --- |
| x86-64 | `drivers/gpu/drm/i915/i915_hwmon.i` | `__compiletime_assert_461`–`465` no longer appear in MiniC→GNU AS `nm -u`. Normal static helper integer specialization matches GNU `-O2`. |
| x86-64 | Linux 6.6.143 `arch/x86/kernel/alternative.c`, CONFIG-off `__apply_fineibt` | Non-inline empty internal void calls with pure arguments no longer retain `__cfi_sites` and `__cfi_sites_end`; exact-shape GNU/MiniC AS+LD+runtime fixture passes. Complete kernel owner `.i` was not archived in the failure sample. |
| x86-64 | `drivers/md/dm.i` | `dm_zone_endio` removed in MiniC→GNU AS→`nm -u`; original GNU oracle agrees. Other `dm` unresolved references remain. |
| AArch64 | `mm/ksm.i` | `__compiletime_assert_508`, `509` removed using same-global-object DataLayout address facts. |
| AArch64 | `drivers/ufs/core/ufshcd.i` | `__compiletime_assert_609`–`618` removed using bounded compound-literal byte and bitfield layout facts; unknown bits remain unknown. |
| LoongArch64 | Frozen Linux vDSO `.i` files | Immediate arithmetic, unaligned byte paths, and elimination of left Core SSA memory snapshot for literal RHS lower native instruction count; no evidence yet of enough size reduction to cure B26. |

The local fixture probes include GNU `-O2` reference, x86-64 MiniC→GNU AS/LD
execution, five-target MiniC `-S`, and negative cases that must remain dynamic.
Only the full Linux CI can certify original Kbuild flags, final relocations,
ELF Image, and candidate QEMU P0/P1.

## Confirmed remaining or structurally unresolved

1. **x86-64**: remaining sampled assertions in `fs/proc/task_mmu.i`
   (`__compiletime_assert_347`, nested `__builtin_constant_p`/CPU-feature
   `asm goto`) and `drivers/gpu/drm/drm_edid.i` (`__compiletime_assert_428`,
   global read-only VIC table). Preserve inline ASM and dynamic evaluations.
   The frozen owner set is a sample, not a proof that only these remain.
2. **x86-64**: `dax_get_private` (three references), `crc32c` (one), and
   possibly `dm_set_zones_restrictions` require safe joint static-object and
   function reachability. Do **not** indiscriminately prune `.initcall`,
   `.altinstructions`, `__used`/`retain`, callbacks, alias, or asm roots.
3. **AArch64**: other owners, especially KVM guard / `__fls` references, are
   not covered by the two replayed repaired files. Await exact-head all-owner
   relink and collect the next full unresolved list.
4. **LoongArch64**: measured optimization on vDSO samples is useful but much
   smaller than the approximately 11 MiB (preferably >=20 MiB margin)
   code-size gap to the B26 bound. Need architecture-safe general Core SSA
   use/liveness and backend code-size work; linker/assembly bypasses are not
   accepted as compiler correctness.

## Mandatory next gate

The branch workflow `.github/workflows/minic-linux-three-isa-gnu-qemu-v1.yml`
has an explicit gate. The **last pushed commit** must modify
`tools/ci/linux-three-isa-gnu-qemu-trigger.txt`: `campaign-gate` checks
`git diff HEAD^ HEAD` of that path. The three `gnu-boot` jobs must each
actually **run**, not be `SKIPPED`, and report per-ISA GNU golden P0/P1,
MiniC all-owner C→S, GNU AS, GNU LD, candidate Image and QEMU P0/P1. After
their artifacts arrive, repair the entire observed batch and rerun on a new
exact HEAD without silently swapping compilation stages or fallback objects.
