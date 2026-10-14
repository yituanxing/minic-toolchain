# MiniAS Linux earlycon: frozen, single-object differential (2026-10-14)

## Scope and immutable input

The next GNU fallback to address is **GNU AS**, without perturbing the
already-accepted 2064 MiniC C objects or rebuilding Linux. The focused lane
uses the **760,450-byte** `trials/full_all/earlycon-source/earlycon-minic.s`
from [full Linux MiniC/MiniAR/MiniLD/MiniObjcopy QEMU P1 run #38426379946](https://github.com/yituanxing/minic-toolchain/actions/runs/38426379946).
Its SHA-256 is
`5a2d6035541018dbacbcc8b7a838f24239d934fd3c3b7fdb3e414e23ddacc02b`.
This is real MiniC-generated kernel assembly, not a hand-written toy.

The existing `gcc-shard-verify` receiver downloads that exact artifact.
All seven Linux C shard jobs, full Kbuild relink, GNU golden restore,
and QEMU execution are skipped. Only cross GNU binutils and the current
MiniAS binary are needed. Run command tag:
`[linux-minias-earlycon-frozen]`.

## Verified outcome (the gap is not closed)

- [Run #38490052109](https://github.com/yituanxing/minic-toolchain/actions/runs/38490052109):
  GNU AS 13 ms, MiniAS 34 ms, both accepted the input. The test was initially
  invalid because it omitted `-march=rv64gc -mabi=lp64d` from MiniAS only.
  The incorrect `e_flags=0x0` was **not** evidence of a MiniAS defect.
- [Run #38490213575](https://github.com/yituanxing/minic-toolchain/actions/runs/38490213575):
  corrected identical ISA/ABI flags yield `e_flags=0x5` on both sides.
  Exported symbols have the same **names**, but address and sizes differ.
- [Run #38490421901](https://github.com/yituanxing/minic-toolchain/actions/runs/38490421901):
  GNU and MiniAS match ELF64/RISC-V and `e_flags=0x5`, both assemble in
  ~21/60 ms, and **defined global symbol names match**.
  RV64GC `.text` bytes do not match. Even with RVC *disabled on both sides*
  (`rv64g`), GNU `.text` is 2152 bytes vs MiniAS 2164 bytes.
- [Run #38490556658](https://github.com/yituanxing/minic-toolchain/actions/runs/38490556658):
  first no-RVC raw text byte difference at offset `0xA`. GNU emits
  `auipc t6,0; addi t6,t6,12` with GNU
  `R_RISCV_PCREL_HI20/LO12` relocations and `R_RISCV_RELAX` markers,
  whereas MiniAS uses an added `.Lminias_pcrel_1` anchor and an addi
  immediate of zero with `R_RISCV_PCREL_LO12_I`. **Different raw relocation
  placeholders are not automatically an incorrect linked value.** Do not
  diagnose this one immediate as an unproven runtime bug.

A no-RVC normalized disassembly shows ~7855 GNU vs 7861 MiniAS instructions.
Aside from expected symbol-label and PCREL anchor differences, MiniAS has
extra pseudo-instruction expansion (e.g., an added `addiw` at offset 0x228,
and a GNU single `addiw` expanded to `addi; slli; slli` in MiniAS).
This explains at least part of the real size difference, but does not
alone prove wrong semantics. The original RV64GC difference is much bigger:
GNU `.text=0x5ae`, MiniAS `.text=0x874`; missing automatic RVC
compression accounts for a large proportion of the size gap.

## Correctness/next gate

1. Compare object relocation targets, symbol/addend and alignment with
   GNU while allowing equivalent relocation *placeholders* and legal
   instruction-sequence variation. A bytewise `.text` mismatch is a
   strict equivalence gate, not a proof that execution must fail.
2. Make a tiny GNU-vs-MiniAS differential fixture for the first missing
   `li`/PCREL alignment forms, then fix the **general** pseudo-expansion
   and RVC paths; run focused assembler tests after each change.
3. Use an explicit **single `earlycon.o` overlay** on the immutable 2064
   MiniC object set (not modification of the cached baseline), relink,
   and run QEMU fast/shell/P1. Until that real runtime gate passes, GNU AS
   remains enabled in the fully P1-certified toolchain.

Keep full QEMU P1-successful native AR/LD/Objcopy results intact. No separate
workflow/job/branch proliferation to chase a 760KB assembler gap.
