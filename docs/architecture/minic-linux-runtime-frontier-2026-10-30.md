# MiniC Linux 6.6.143 strict runtime frontier (2026-10-30)

Status: RV64 **full strict Linux runtime and twelve independent repeat boots
certified**; the other three target architectures still have GNU-link frontiers.
This is an evidence ledger, **not** a declaration of multi-architecture Linux
success.

## Unchanged acceptance contract

- The original GNU Kbuild supplies the configuration, frozen preprocessed `.i`,
  source ASM, all non-C objects, GNU assembler, linker and QEMU baseline.
- MiniC must compile **every** original C owner for a target. The final
  candidate must have zero GCC-produced C fallback objects.
- Count GNU AS, GNU LD, actual candidate Image and QEMU P0/P1 separately.
  A `*.s`, `*.o`, partial link or original GNU-only boot is not Linux PASS.
- Re-run complete exposed failures after each coherent implementation batch;
  retain same-head SHA, object inventory and immutable Image hashes.

## Latest certified RV64 campaign: `925ef4c2a25228f25382d226154f24c1313c7f48`

Run: https://github.com/yituanxing/minic-toolchain/actions/runs/41024910824

The exact-head `gcc-shard-verify` job `123961794370` finished **SUCCESS**.
All seven `gcc-shards (0..6)` jobs also finished **SUCCESS**. The receiver
verified 295 owners each for shards 0–5 and 294 for shard 6 (total **2064**),
with `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`.
Each of the seven separate job logs also emitted exact-head
`RV64_FULL_EXACT_HEAD=PASS`, `MINIC_ROUTE_AUDIT=PASS minic_compiled=295`
(294 for shard 6), and `GNU_MINIC_SHARD_READY=PASS` for its partition;
the aggregate receiver's cache hit is therefore **reuse of verified MiniC
objects**, not skipped compilation or an undocumented GCC-C fallback.

| Strict stage | Exact completed-job evidence |
| --- | --- |
| Every original C owner | `GNU_FULL_C_OBJECTS=PASS selected=2064`; `COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`; `COHORT_ABI=PASS objects=2064` |
| No GCC C fallback | `COHORT_CANDIDATE_CACHE=HIT objects=2064`; `COHORT_COMPILE=SKIPPED reason=exact-object-cache-hit objects=2064`; seven authenticated MiniC shards merged; `COHORT_GNU_UNSELECTED=PASS derived=40 pinned_elf_and_transform_identity=true` checks explicitly derived GNU Kbuild transforms and unchanged nonselected objects |
| Original GNU link and first QEMU boot | `COHORT_TIMING trial=full_all stage=qemu_fast elapsed_ms=22127 rc=0`; `COHORT_TRIAL name=full_all count=2064 verdict=PASS` |
| Full GNU Image and QEMU P1 | `COHORT_TIMING trial=full_all_verified stage=qemu_full elapsed_ms=44647 rc=0`; `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`; `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true` |
| Twelve **new, independent QEMU processes** on the same Image | Twelve distinct `RV64_FULL_REPEAT=PASS attempt=N/12 ... syscalls=12 qemu_exit=0` markers for **all N=1..12**, plus `RV64_FULL_IDENTICAL_IMAGE=PASS repeats=12/12 syscalls_each=12/12` |

The candidate Image is **150,491,136 bytes** (original GNU golden Image:
22,033,920 bytes; ratio **6.83x**). The unchanged Image SHA256 across the
full verification and **all twelve new boots** is:

`037a1c31ca527c904f0bf262a24a78010a9d1b79b54d8c71ae738ec85160ab6d`

For this greater-than-128-MiB Image, the cohort's authenticated loader used
**2 GiB** guest RAM, original 16-KiB kernel stack, and initrd start
**`0xA0000000`**. The repeat step explicitly reused the already-certified
`full_all_verified/full-crossblock-virt.dtb`, verified its `/chosen`
initrd-start/end against the unchanged initramfs size, and passed:

- `RV64_REPEAT_LOADER_SELFTEST=PASS ... unsafe_overlap=rejected`
- `RV64_REPEAT_LOADER=PASS mode=large ... memory=2G initrd_start=a0000000 original_image=1 original_initramfs=1`
- `RV64_FULL_IDENTICAL_IMAGE=PASS repeats=12/12 syscalls_each=12/12 ... head=925ef4c2a25228f25382d226154f24c1313c7f48`

The cohort's **one boot containing 12 syscall checks** and the subsequent
**12 independent boots, each containing 12 syscall checks and exiting QEMU
with status zero**, are separate attestations. The loader change addresses
only QEMU's large-Image initrd placement; it does not change the kernel,
initramfs, source flags or the GNU assembler/linker boundary. Small Images
retain the original 512-MiB/default `-initrd` path.

## Latest three-ISA GNU candidate campaign: `925ef4c2` (link frontiers remain)

Run: https://github.com/yituanxing/minic-toolchain/actions/runs/41024910818

The original GNU Linux 6.6.143 builds and real QEMU P0/P1 oracles passed
**2/2 on each target**. All original GNU-frozen `.i` C owners compiled via
MiniC and GNU AS at the same exact head. The **MiniC candidate** did not
reach QEMU on any of these three targets because original GNU Kbuild relink
failed. Do not conflate GNU-only boots with candidate boots.

| Target | All MiniC C objects via GNU AS | GNU candidate first linker frontier | Exact change from `d81261dd` |
| --- | ---: | --- | --- |
| x86-64 | 2758/2758 | Final `vmlinux` unresolved symbols and unexpected GOT entries | Displayed unresolved-reference lines **386 -> 105** (-72.8%), distinct symbols **221 -> 52** (-76.5%); `__compiletime_assert_*` 250 -> 53, `__bad_copy_from/to` 20 -> **0**. The new reachable-block codegen is effective on the complete original corpus. |
| AArch64 | 4108/4108 | 261 unresolved references, GOT/PLT assertions and 31 secondary dangerous relocations | **No change**: 199 compile-time asserts, 51 kallsyms references, six BTF and five EFI references. The newer self-contained BUG ASM clone support is **not in this tested SHA**. |
| LoongArch64 | 2723/2723 | Final `vmlinux` `R_LARCH_B26`: `head.o(.ref.text+0x15c)` cannot branch to `start_secondary` | Same GNU relocation blocker; C executable bytes 272,143,396 -> **272,362,396** (+219,000, 0.0805%). The growth came from only three Zstd owners; 2720 other owners' executable byte counts are unchanged. |

The LoongArch GNU C executable baseline is **16,887,804 bytes**, versus
MiniC's **272,362,396** (16.1278x). This is a distributed Core SSA
stack roundtrip problem; the target register-location architecture RFC
must be implemented and validated rather than modifying original GNU
relocation ranges/linker scripts. On x86-64 the remaining `__field_overflow`
and repeated `__compiletime_assert_212` are real constant-guard frontiers;
`kallsyms_*` and ELF GOT/section policy must be tracked independently.

Subsequent changes to AArch BUG ASM cloning, GNU function diagnostic
metadata, x86 non-PIC GOT ownership, constant-guard folding and Loong
duplicate-alignment removal **postdate this campaign**. They cannot be
credited to the `925ef4c2` Linux evidence without a new exact-head run.

## Previous campaign: `d81261dda21dfdb760b89613831ca5f9c0a09633` (historical)

| Target at d81261dd | MiniC C → GNU AS | GNU candidate link | Candidate QEMU | First strict frontier at that SHA |
| --- | --- | --- | --- | --- |
| RV64 (historical d812 only) | 2064/2064; seven shards 7/7 | Full Image PASS | One full Image P1 12 syscalls PASS | 12 *independent* exact-Image P1 reboots not yet certified at this older SHA: first run used the wrong default initrd load address; **resolved at 925ef4c2 above** |
| x86-64 | 2758/2758 | Final GNU `vmlinux` FAIL | Not run | 386 displayed undefined-reference lines, 221 distinct symbols; vDSO link now passes |
| AArch64 | 4108/4108 | Final GNU link FAIL | Not run | 261 displayed undefined references plus GOT/PLT assertions; no reduction from previous `9060aff0` |
| LoongArch64 | 2723/2723 | `vmlinux.o` and kallsyms1/2 PASS; final `vmlinux` FAIL | Not run | `R_LARCH_B26` from `head.o` `.ref.text` to `start_secondary`, due distributed executable-text expansion |

Campaigns:

- RV64: https://github.com/yituanxing/minic-toolchain/actions/runs/41020466920
- x86/AArch64/LoongArch64: https://github.com/yituanxing/minic-toolchain/actions/runs/41020466906

### RV64 at d81261dd: runtime achieved, repetition harness not yet proved

`full_all` and `full_all_verified` both passed original GNU LD, QEMU
and P1's 12 system-call checks, with the same 150,491,136-byte Image SHA256:

`8e8a6c72a9be4b73ee191f62108b7710184c23307ef969b6cfff8af49b80ca41`

The extra 12-QEMU-process step failed before any guest execution because
QEMU default `-initrd` occupied `0x88200000`, inside this Image's range
`0x80200000..0x89185000`. The already-passed cohort used a verified virt
DTB, 2 GiB guest RAM, and initramfs at `0xA0000000`. The follow-up
workflow fix must reuse exactly that map, preserve `qemu_exit=0` and check
12 system calls on **each** of 12 independent reboots. The earlier result
does **not** certify the independent 12/12 repetition.

### x86-64: classify before changing optimization

At `d81261dd`, GNU-only Linux QEMU P0/P1 is 2/2 and the previous phantom
vDSO hidden-symbol error is gone. The final candidate GNU linker reports
at least 386 references across 221 distinct names (GNU LD suppresses some
repetitions):

- `__compiletime_assert_[0-9]+`: 250 displayed / 154 distinct.
- `__field_overflow`, `__bad_copy_from`, `__bad_copy_to`: 43 / 3.
- `kallsyms_*`: 29 / 8; late-link generation must be assessed separately.
- `__cfi_sites*`: 2 / 2.
- Other CONFIG/inline/optional runtime symbols: 62 / 54.

Do not replace these with fake definitions or change the original GNU
linker script. Trace the actual GNU-preprocessed C owner to a proof about
constant guards, `__builtin_object_size`, owner reachability or genuine ABI.

### AArch64: literal Linux BUG ASM matters

On both `9060aff0` and `d81261dd`, GNU AS accepted 4108/4108 MiniC
objects, but the GNU link had 199 displayed `__compiletime_assert_*`
references, 51 `kallsyms_*`, six BTF sentinels and five EFI references,
plus GOT/PLT complaints. The 31 risky PC-relative relocations all target
the same missing kallsyms/BTF/EFI symbols; do not assume 31 separate
machine-code defects. A minimal `fix_to_virt` / `patch_map`
constant-argument model proves the existing AST specialization is limited
to `static inline`: ordinary TU-local `static patch_map` can retain a dead
compile-time assert where GNU `-O2` removes it.

However, the real Linux `patch_map` also contains `BUG_ON` source assembly
with balanced `__bug_table`/`.rodata.str` and numeric-local labels. The
conservative new specialization experiment refuses static bodies with
opaque ASM. A separate real-ASM source probe is intentionally **RED**;
the no-ASM model's PASS must not be promoted to real Linux acceptance.
Target-independent GNU `error` attribute semantics and provably clone-safe
ASM ownership still need design/verification.

### LoongArch64: global code-size architecture problem

The earlier EFI duplicate-definition error disappeared at `d81261dd`;
do not retain its obsolete trace workaround. GNU `vmlinux.o` and both
kallsyms passes now link, but a source-owned `R_LARCH_B26` short branch
cannot reach `start_secondary` in the final GNU script layout.

The 2723 C owners' executable text is 16,887,804 GNU bytes versus
272,143,396 MiniC bytes (16.1148x). Median per-owner ratio is 16.63x;
the top 20 object deltas are only 9.21% of the excess. A real `vvar_fault`
assembly comparison has 1,567 MiniC instructions versus 66 GNU; 840
MiniC instructions directly store/reload SSA/frame slots. Repeating local
address calculation compounds this. Follow the proposed
`minic-core-block-register-plan-rfc-2026-10-30.md`: shared block-local
SSA location planning with target-owned register/ABI policies and strict
spill fallback, then delete superseded target-local transport paths.
