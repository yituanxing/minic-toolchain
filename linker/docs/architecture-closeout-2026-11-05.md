# MiniLD five-target core closeout (2026-11-05)

This checkpoint closes the remaining *architecture ownership* seams without
claiming that every user-space linking mode is implemented.  The original GNU
Linux object pools and their five QEMU P1 proofs remain the runtime baseline;
the current-head GNU AArch64 ordinal oracle and affected frozen Linux replay
are independent recertification gates.

## Single-owner path

1. **InputGraph** retains immutable ELF input symbol/section/file provenance.
   Resolver owns canonical symbols and records a linker-script PROVIDE only
   when the definition actually takes effect.  Fixed-point script trials
   restore the entire original symbol snapshot, including this provenance.
2. **WorkSections/Layout** own mutable fragment selection, synthetic GOT/PLT,
   placement and relaxation.  A synthetic work section must never be counted
   as an original BFD input-section-local dynamic symbol.
3. **Target Profile** declares ABI/mode/erratum capabilities and classifies
   machine-specific final relocations.  Driver and FinalSymbols do not
   enumerate target machines or instruction relocation numbers.
4. **FinalSymbols/OutputPlan/ProgramPlan/ELF writer** own one checked output
   projection.  Unadmitted --emit-relocs transformations fail closed; only
   the target that supplies a post-fixup classifier can enable that mode.

There are six explicit ELF descriptors (RV32, RV64, x86-64, i386 setup,
AArch64 and LoongArch64).  They use named initializers so extending a target
contract cannot silently shift positional ABI callbacks.  The public
multi-target static API no longer says ELF64 when RV32 ELF32 is admitted, and
unused RV64-only convenience entry points have been removed.

## Kernel discarded-dynsym exception

AArch64 Linux links with a discarded physical .dynsym may retain dynamic
symbol ordinals inside .rela.dyn.  BFD's live *source* input-section local
classes and actually applied hidden PROVIDEs precede unresolved weak global
symbols.  An allocated synthetic .got is not another source-local class.
Duplicate hidden undefined declarations across source objects do not add a
forced-local ordinal, and CALL/JUMP-only references do not make one.

This remains target-owned kernel behavior.  The ordinary ET_DYN shared
linker's real dynsym reservation/fill pipeline is deliberately separate.
Neither a frozen symbol name nor a hardcoded kernel symbol index is admitted.
The final ordinal is checked to fit ELF64 r_info's 32-bit symbol field.

## Qualification

- Native strict compilation and the phase/target structural gate.
- Five-target ELF boundary fixtures, including GNU BFD local-symbol oracles,
  RV32 ELF32, x86 retained final relocations and target-specific fixups.
- Cross-GNU AArch64 single/repeated hidden-UND ordinal comparison.  The test
  compares complete raw .rela.dyn records, not merely its own expected index.
- Affected frozen GNU Linux full image/QEMU P1 replay after the implementation
  changes; prior successes are a baseline, not a substitute for new-head CI.

The single translation unit plus ordered `.inc` modules is intentional: it
preserves phase order without duplicating persistent owners.  Complete
user-space PIE/DSO coverage and unrelated debug formats are feature work,
not reasons to reopen this architecture migration.
