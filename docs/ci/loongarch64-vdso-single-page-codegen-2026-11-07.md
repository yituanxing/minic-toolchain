# LoongArch64 vDSO: conservative selected-MIR code-size probe (2026-11-07)

This is a **local, isolated** Loong target emitter probe.  It is not a claim
that a complete kernel or Debian runtime passed.  No GitHub Actions run was
started from this worktree.

## Official frozen inputs and original outputs

- Official Linux 6.6.143 Loong seven-shard candidate run `#42378020139`:
  `2723/2723` MiniC C objects compiled through GNU `as`; its static GNU
  `/init` faults in glibc while reading the vDSO ELF.  Original GNU candidate
  is a separate passing control.
- Frozen original `arch/loongarch/vdso/vgettimeofday.i` from official source
  artifact `#13572541006`: SHA256
  `6a06d42008f4f2dc40dacb45d17fe3ff1ded3b561b55c9a0eae6bf545d15004a`.
- Original MiniC-generated `vgettimeofday.s` from seven-shard artifact
  `#13575833950`: SHA256
  `91cb084a3e1cf05331b08ea5f4ddd6c7917b86750ab42cff632f8e9351210f7b`;
  4,632 lines; its GNU-assembled `vgettimeofday.o` `.text` is `0x3d70`
  (15,728 bytes).
- Original MiniC GNU-assembled `vgetcpu.o` from artifact `#13576442748`:
  `.text=0x328` (808 bytes).  **Combined these two `.text` sections total
  `0x4098` (16,536 bytes)**, already exceeding LoongArch's 16,384-byte page
  before ELF headers or the other vDSO objects.
- Linux's Loong `vdso_info.size` is `PAGE_SIZE`, and the special mapping uses
  that fixed length.  This is an evidence-backed **over-one-page hypothesis**,
  not a replacement for inspection of the exact final `vdso.so` PT_LOAD
  FileSiz/MemSiz and runtime fault address.

## Local emitter probe (identical frozen `.i`)

Command:

```sh
build/debug/bin/minic-cc --target=loongarch64-linux -fPIE -mabi=lp64s \
  -S vgettimeofday.i -o vgettimeofday.peephole.s
```

SHA256 of new `.s`:
`c3def2c5397985672fc4f24e1748059f8829bd5ada9bcaefd18a1a2c63304309`;
4,275 lines.  A zero-context exact diff against original `.s` reports
**357 deletions and zero additions or substitutions**, namely:

| Selected-MIR-safe omission | Occurrences | Fixed-width bytes |
| --- | ---: | ---: |
| Same GPR, same `$sp`+offset immediately after `st.d`: omit `ld.d` | 196 | 784 |
| Unconditional `b` to immediate following physical block | 161 | 644 |
| **Total** | **357** | **1,428** |

Expected `vgettimeofday.o` `.text` becomes `0x37dc` (14,300 bytes) if
GNU `as` preserves one Loong 4-byte instruction per deleted line.
Original `vgetcpu.s` from shard-6 artifact contains 5 analogous load and 15
fallthrough branch **candidates** (80 bytes, not yet rebuilt from frozen
`vgetcpu.i`).  Combined two vDSO C `.text` sections would then become
`0x3ab4` (15,028 bytes), leaving **1,356 bytes** for ELF segment headers,
alignments, `elf.o`, `sigreturn.o`, etc.  Therefore 16KB **not yet proven**.

## Safety boundaries and verification

The change lives in one target emitter.  It only omits plain non-volatile,
unsealed adjacent physical selected machine instructions with identical
64-bit GPR and `$sp`+displacement, or a branch whose target is explicitly
the immediately next physical block.  There is no cross-label/CFI/source-ASM
rewriting and no frontend/Core IR/ABI change.

`make check-direct-machine-gnu-asm` passes, including positive and negative
cases for frame operands, actual target addresses, distinct destination
register, volatile load, different offsets, different base, and branch to a
non-next block.  A local Loong GNU cross-assembler was not available, so
the exact ELF section size, linker result, final ELF PT_LOAD and runtime
success require a separate official cached-cohort rebuild.
