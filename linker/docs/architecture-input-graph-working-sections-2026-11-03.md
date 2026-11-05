# MiniLD: InputGraph and WorkingSections ownership (2026-11-03)

## Boundary

The original ELF input identity and the mutable collection of sections used
by the linker now have independent owners. This is an ownership migration of
the existing RISC-V ELF linker, not a new target or linker algorithm.

| Owner | Owns | Stable or mutable identity |
| --- | --- | --- |
| `MiniLdInputGraph` (`linker_input.inc`) | Source files, original ELF sections, original symbols, copied bytes and names, file-to-section ID maps | Append-only input file/section/symbol IDs, including extracted archive members |
| `MiniLdSectionWorkspace` (`linker_work_sections.inc`) | Projected work sections, name index, `MiniLdInputPlacement[]` | Work-section indices change during projection and reorder |
| `MiniLdSymbolResolver` (`linker_symbol.inc`) | Canonical resolved symbols and global-name index | Canonical IDs remain stable; winning section/value may change |
| Final-symbol / output-section projections | Serialized symbol/section order after planning | Disposable output ELF indexes |

`MiniLdInputPlacement[i]` corresponds to `MiniLdInputGraph.sections[i]`.
Its single `work_section` field records the selected post-admission work slot;
the former `merged_section`/`current_section` pair is removed. No work
sections or placements exist during input admission. The original
`MiniLdInputSection` does not store placement,
GC section indexes or linker-script KEEP/DISCARD/selection flags. Those flags
belong to the work section.

## Admission and reordering

1. Input admission records immutable file/section/symbol identities and
   original relocations only. Its sole ELF shndx-to-source-ID mapping lives
   in `MiniLdInputFile.section_ids`. Lazy archive extraction uses the same
   source-only path as ordinary ET_REL inputs.
2. Once complete object/archive/COMDAT selection is known,
   `minild_project_admitted_inputs` builds the chosen work view **once**:
   `-r` preserves COMDAT/group and LINK_ORDER identities, ordinary final links
   coalesce compatible sources, static GC keeps exact ALLOC and LINK_ORDER
   fragments, and script links keep each eligible source fragment. The common
   `.riscv.attributes` vendor record remains merged. There is no eager merged
   byte buffer that script/GC later discards.
3. Original input IDs, bytes and source offsets remain immutable. One
   validated source-to-work map atomically rebinds placements, canonical
   winners and relocations to their final work coordinates. Exact fragments
   start at source offset zero; merged sections retain alignment gaps.
4. Input-order, output-class and output-alignment sorts use one permutation
   installer. It validates an in-range, one-to-one mapping and all referenced
   section IDs, allocates a new name index first, then remaps canonical symbol
   sections, relocation sections, LINK_ORDER targets and input placements
   together. It does not mutate original source IDs.
5. Layout, relocation indexes, ELF output-section plans and final symbol
   projections run after these work permutations; they must not retain stale
   work-section indexes across a later sort.

`MiniLdSectionWorkspace` is destroyed before `MiniLdInputGraph` because the
projected work sections borrow source names and input paths. Ownership of
each allocation remains unique, and borrowed strings are not double-freed.

## Verification and remaining scope

`make MODE=release CFLAGS=-Werror check-minild-input-work-section-boundary`
tests host-generated ELF inputs through six paths: two ET_REL rounds, direct
static link, static link after ET_REL, and both static variants under a
linker script. It compares COMDAT identity, LINK_ORDER, source ordering,
relocations, COMMON values and GC/script results. This gate runs alongside
the existing MiniLD RISC-V ABI, archive, symbol, output-section, shared,
script and static pipeline regressions.

The current gate establishes these ownership contracts; it does **not**
establish GNU linker feature parity or demonstrate QEMU/Linux boot. Dynamic
relocation, ABI and runtime coverage remain separate acceptance work.

COMDAT signature admission state belongs to `MiniLdInputGraph`. The link
driver retains phase policy, while output planning has its own owner. Future
changes must not add another copy of the source graph or canonical resolver.

## Uniform post-admission projection checkpoint

For `-r`, ET_EXEC and ET_DYN (including script/GC), the reader admits each
source section only as an immutable input-graph identity. It
records original input bytes, symbols and relocation offsets without rebasing
them into a same-named aggregate during archive/COMDAT selection. Once the
complete input sequence is known, `minild_project_admitted_inputs` builds
a mode-aware work projection in one pass. Final links retain name coalescing,
input order, alignment gaps, RISC-V attribute vendor-record concatenation and
exact `SHF_LINK_ORDER` targets. `-r` additionally preserves independent COMDAT
group members/signatures and LINK_ORDER target identities. Only the *mutable*
canonical winner symbols and relocation coordinates are rebased from their
immutable source records.

The late projection creates the only working-section owner atomically after
validating every symbol and relocation reference. Original
InputGraph IDs never change. Shared GOT/PLT/TLS/dynamic metadata preparation
and static layout occur only after that projection is complete.

`check-minild-default-input-late-coalesce` verifies direct and `-r`-then-final
ET_EXEC name merging and pointer relocations. The new
`check-minild-relocatable-input-projection` verifies two successive `-r`
rounds and final ET_EXEC: repeated relocations/addends, section alignment,
two COMDAT groups, LINK_ORDER metadata, merged `SHT_NOBITS` and RISC-V
attributes. Existing native
shared/PIE and archive/COMDAT tests exercise the same admission boundary.

**Updated boundary (2026-11-04):** Script/GC now select exact source
fragments *within* the single post-admission projection. The previous
merge-then-materialize pass and its empty discarded section holders are gone.
GC without `-T` still merges ordinary non-ALLOC metadata; scripts retain
source identity for non-ALLOC inputs and LINK_ORDER. Script selection and GC
reachability remain distinct policies; no universal linker engine was added.
GNU RISC-V/QEMU Linux compatibility has not been requalified.

## Graph-only admission checkpoint (2026-11-04)

The legacy empty WorkSection placeholders and duplicated `MiniLdSectionMap`
have been removed. `linker_admission.inc` owns ET_REL/DSO admission, archive
extraction and COMDAT selection; it only records immutable InputGraph facts,
canonical pre-layout symbol resolution and relocation origin records. It
never constructs `MiniLdSectionWorkspace` or a placement table.

Selected, not-yet-projected section definitions use
`MINILD_SECTION_PENDING` so strong/weak and archive extraction still treat
them as defined. Relocations carry the immutable `input_section` and
`input_offset` until projection fills the current work coordinates. The
projector alone allocates `MiniLdInputPlacement[]` and work sections; for
`-r`, it reconstructs GROUP flags/signature symbols and LINK_ORDER from
their original InputGraph relationships. No additional persistent group
mapping or compatibility branch was introduced.

`check-minild-graph-only-admission` asserts that ownership at source level
and runs archive (including thin), COMDAT, LINK_ORDER, repeated `-r`,
GC/script/COMMON and ET_DYN fixtures. This is architectural and host-ELF
validation, not a new feature or Linux boot acceptance.
