# MiniLD: InputGraph and WorkingSections ownership (2026-11-03)

## Boundary

The original ELF input identity and the mutable collection of sections used
by the linker now have independent owners. This is an ownership migration of
the existing RISC-V ELF linker, not a new target or linker algorithm.

| Owner | Owns | Stable or mutable identity |
| --- | --- | --- |
| `MiniLdInputGraph` (`linker_input.inc`) | Source files, original ELF sections, original symbols, copied bytes and names, file-to-section ID maps | Append-only input file/section/symbol IDs, including extracted archive members |
| `MiniLdSectionWorkspace` (`linker_work_sections.inc`) | Merged and materialized work sections, name index, `MiniLdInputPlacement[]` | Work-section indices change during materialization and reorder |
| `MiniLdSymbolResolver` (`linker_symbol.inc`) | Canonical resolved symbols and global-name index | Canonical IDs remain stable; winning section/value may change |
| Final-symbol / output-section projections | Serialized symbol/section order after planning | Disposable output ELF indexes |

`MiniLdInputPlacement[i]` corresponds to `MiniLdInputGraph.sections[i]`.
`merged_section` records the original working merge slot used **before**
materialization; `current_section` records the live materialized fragment
after sorting. The original `MiniLdInputSection` no longer stores placement,
GC section indexes or linker-script KEEP/DISCARD/selection flags. Those flags
belong to the work section.

## Admission and reordering

1. Input admission records a source section and appends exactly one placement;
   the coordination helper checks equal counts and rolls back the placement
   count on a failed source-record operation. Lazy archive extraction uses the
   same path as ordinary ET_REL inputs.
2. Work-section name merging excludes `distinct` COMDAT and LINK_ORDER
   fragments. `indexable` records this property explicitly, including after
   rebuilding the name index.
3. Script/GC materialization copies only selected original section data into
   new work fragments, preserving source IDs and original symbol offsets.
   Work placement then points to each new fragment.
4. Input-order, output-class and output-alignment sorts use one permutation
   installer. It validates an in-range, one-to-one mapping and all referenced
   section IDs, allocates a new name index first, then remaps canonical symbol
   sections, relocation sections, LINK_ORDER targets and input placements
   together. It does not mutate original source IDs or `merged_section`.
5. Layout, relocation indexes, ELF output-section plans and final symbol
   projections run after these work permutations; they must not retain stale
   work-section indexes across a later sort.

`MiniLdSectionWorkspace` is destroyed before `MiniLdInputGraph` because the
materialized work sections borrow source names and input paths. Ownership of
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

COMDAT signature admission state and some driver/output planning state still
reside directly in `MiniLdState`. Their next ownership boundaries should be
decided from the full admission and output dataflows rather than adding
parallel caches or a transitional copy of the input graph.

## Ordinary final-link deferred coalescing checkpoint

For no-script/no-GC ET_EXEC and no-script ET_DYN, the reader now admits each
source section as its own initially empty working-section identity. It
records original input bytes, symbols and relocation offsets without rebasing
them into a same-named aggregate during archive/COMDAT selection. Once the
complete input sequence is known, `minild_finish_default_input_merge` builds
the historical name-coalesced work projection in one pass, preserving input
order, alignment gaps, RISC-V attribute vendor-record concatenation and
exact `SHF_LINK_ORDER` targets. It then remaps the *mutable* canonical winner
symbols and relocation coordinates from their immutable source records.

The late projection replaces its former temporary working-section owner
atomically after validating every symbol and relocation reference. Original
InputGraph IDs never change. Shared GOT/PLT/TLS/dynamic metadata preparation
and static layout occur only after that projection is complete.

`check-minild-default-input-late-coalesce` verifies direct and `-r`-then-final
ET_EXEC links with repeated `.text.same`/`.data.refs` from separate objects,
different alignments, one final section per name, and eight checked final
pointer relocations. Existing native shared/PIE and archive/COMDAT tests also
exercise the delayed no-script path.

**Boundary:** `-r`, scripted/GC static links and scripted shared links still
use their earlier eager-merge or materialize-before-select paths. This is a
real default-link migration, not a claim that all input admission is now
uniform or that GNU RISC-V/QEMU Linux compatibility has been requalified.
