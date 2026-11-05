# MiniAS terminal ObjectModel reference and output ownership (2026-11-02)

One grouped migration on Draft PR #667, after TargetSpec and RISC-V ISA
version ownership. The goal here is a **usable format-neutral terminal
reference plan**, not a renamed ELF serializer or a speculative COFF/Mach-O
implementation. LLVM's MC layer makes an analogous distinction between
shared object-streaming semantics and file-format-specific MCSection and
ObjectWriter types (see the upstream LLVM CodeGenerator documentation).

## Previous ownership problem

The Driver exposed a read-only `MiniAsElfObjectView` after Target finalization,
but its `elf_writer.c` still (1) resolved final Fragment-relative Fixup
positions and checked emitted Section extents, (2) built/sorted a Section
name index and resolved source associations/group signature SymbolIDs,
(3) computed actual ELF symbols/relocations, and (4) atomically published
the completed output file. A COFF/Mach-O implementation would have had to
copy (1), (2) and (4), which have no requirement to know an ELF section
header or relocation type.

## New single-owner terminal path

1. Driver projects one stack-scoped `MiniAsObjectModelView` after successful
   Common emission and Target finalization. It borrows the existing unique
   Context SectionID/SymbolID arrays, Emission's immutable Fixup span and
   finalized byte slices, Source-derived file names and scalar Fragment
   positions. The previous flat ELF-only copies of these borrows are removed.
2. `object_model.c` builds a short-lived `MiniAsObjectPlan`. It resolves
   group signature SymbolID, LINK_ORDER SectionID and the final byte extent
   for each Section; computes every Fragment/explicit/synthetic Fixup's
   absolute Section-relative offset and validates owner/width/bounds; and
   retains only borrowed pointers, sizes and stable numeric IDs. A sorted
   Section name index is temporary and freed before returning. No duplicate
   Section/Symbol/Fragment/Fixup entities or second layout graph survive.
3. `elf_writer.c` consumes that completed plan. ELF-only decisions stay here:
   local `.L` symbol liveness, relocation-dependent STT_SECTION folding,
   GNU private-symbol spelling, SHN_*/STT_*/OSABI, SHT_GROUP/COMDAT and
   REL/RELA fields. The existing `minielf_build_relocatable` still performs
   the only ELF image encoding. One terminal plan is released on every
   serializer success or failure.
4. `object_output.c` owns atomic object publication, preserving GNU/Linux
   `-o /dev/null` and other non-regular streams, regular file permissions,
   umask, O_EXCL temporary uniqueness, rename success and cleanup on failed
   writes. No ELF formatting detail enters this output owner.

The prior ELF-local implementations for Fragment Fixup offset resolution,
Section name lookup and output publication have been deleted, not retained
as compatibility forwarding paths. The public `minias_assemble_file*`
family remains unchanged pending a separate external-ABI decision.

## Proof and current limits

`probe-object-model-transaction.sh` enforces the new ownership boundaries,
and compiles `object_model.c` alone with a synthetic C reader. Its positive
cases cover borrowed bytes, group signatures, direct symbol/section-name/
numeric associations and Fragment final offset. Seven negative cases check
missing references, invalid Fragment, overflow, malformed origin and bad
symbol indices with no leaked plan. The existing ELF const-borrow gate
still rejects seven forbidden write/access cases.

Local before remote publication: strict release `-Werror`, A0 30/30 with
GNU RISC-V oracles, GNU x86/RV32 COMDAT with real `ld -r`, LINK_ORDER
1,024-pair comparison, duplicate-name/unique SectionKeys, and atomic output
failure recovery have passed. The independent prior-binary four-Target
object comparison and exact-head remote CI are separate final gates.

**This does not finish multi-format output.** The source Section metadata
still carries ELF-style flags/types and ELF-specific `SHF_LINK_ORDER`/
`SHT_GROUP` source directives. Future formats need format policy for their
own section attributes, COMDAT selection, symbol/relocation serialization
and validated binary/linker outputs. Do not move ELF's special section
header indices or numeric relocations into this neutral planner, or claim
that COFF/Mach-O support exists because a shared struct has been added.
