# MiniC Linux 6.6.143 strict runtime frontier (2026-10-30)

Status: evidence ledger, **not** a declaration of multi-architecture Linux success.

## Unchanged acceptance contract

- The original GNU Kbuild supplies the configuration, frozen preprocessed `.i`,
  source ASM, all non-C objects, GNU assembler, linker and QEMU baseline.
- MiniC must compile **every** original C owner for a target. The final
  candidate must have zero GCC-produced C fallback objects.
- Count GNU AS, GNU LD, actual candidate Image and QEMU P0/P1 separately.
  A `*.s`, `*.o`, partial link or original GNU-only boot is not Linux PASS.
- Re-run complete exposed failures after each coherent implementation batch;
  retain same-head SHA, object inventory and immutable Image hashes.

## Last certified campaign: `d81261dda21dfdb760b89613831ca5f9c0a09633`

| Target | MiniC C → GNU AS | GNU candidate link | Candidate QEMU | Current first strict frontier |
| --- | --- | --- | --- | --- |
| RV64 | 2064/2064; seven shards 7/7 | Full Image PASS | One full Image P1 12 syscalls PASS | 12 *independent* exact-Image P1 reboots not yet certified: first run used the wrong default initrd load address |
| x86-64 | 2758/2758 | Final GNU `vmlinux` FAIL | Not run | 386 displayed undefined-reference lines, 221 distinct symbols; vDSO link now passes |
| AArch64 | 4108/4108 | Final GNU link FAIL | Not run | 261 displayed undefined references plus GOT/PLT assertions; no reduction from previous `9060aff0` |
| LoongArch64 | 2723/2723 | `vmlinux.o` and kallsyms1/2 PASS; final `vmlinux` FAIL | Not run | `R_LARCH_B26` from `head.o` `.ref.text` to `start_secondary`, due distributed executable-text expansion |

Campaigns:

- RV64: https://github.com/yituanxing/minic-toolchain/actions/runs/41020466920
- x86/AArch64/LoongArch64: https://github.com/yituanxing/minic-toolchain/actions/runs/41020466906

### RV64: runtime achieved, repetition harness not yet proved

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
