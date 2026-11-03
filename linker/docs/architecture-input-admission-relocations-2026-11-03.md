# MiniLD: input admission and relocation ownership (2026-11-03)

## Why this pass

`MiniLdInputGraph` and `MiniLdSectionWorkspace` had already separated the
original ELF identities from reorderable working sections. Three remaining
pieces of the same dataflow still lived as independent arrays directly in
`MiniLdState`: COMDAT selection, DSO input facts and relocations with their
lazy RISC-V ALIGN lookup. The state is now held by its phase owner; the ELF
output plans remain unchanged.

## Owners and stable IDs

| Owner | Data and lifetime | Contract |
| --- | --- | --- |
| `MiniLdInputGraph` | Original ET_REL file/section/symbol identities and copies; first-wins COMDAT signature registry | A discarded COMDAT group cannot contribute section bytes, strong definitions or undefined relocations. Archive extraction enters through the same admission path. |
| `MiniLdInputGraph.dso` (`MiniLdDsoCatalog`) | Ordered DT_NEEDED names and DSO exported-symbol names | Both are input facts, not `.dynamic`, `.dynsym` or `.rela.dyn` output buffers. Their existing admission and lookup order is preserved. |
| `MiniLdRelocationWorkspace` | Canonical relocation worklist, allocated capacity and a lazy per-work-section CSR index | Every relocation carries an immutable input-section ID and original offset alongside mutable current section/offset and canonical symbol ID. |
| `MiniLdSectionWorkspace` | Working section buffers, placement mapping and name lookup | No redundant per-section relocation count is maintained. |

Input SHT_GROUP flags/signature metadata used only while admitting a file do
not need a second persistent copy on `MiniLdInputSection`. For relocatable
output, WorkingSections still preserve all required SHT_GROUP signature and
member metadata, and the original `group_input` and `link_order_input`
relations remain available to GC.

## Relocation phase contract

1. ET_REL admission appends one relocation with a stable origin ID/offset and
   a distinct, empty source-section work placeholder. A single post-admission
   projection chooses the `-r` or final-link work section and its mutable
   offset; repeated `-r` is handled by re-admission.
2. Final-link materialization restores each surviving relocation's original
   offset from the input section. Section permutations remap relocation
   current-section IDs simultaneously with resolved symbols, LINK_ORDER
   targets and input placements.
3. Script discard and static GC compact the worklist before final layout;
   discarded-section relocations never leak into GOT/PLT/dynamic metadata.
4. RISC-V ALIGN relaxation lazily builds a CSR index over the **current work
   sections**. Build cost is O(sections + relocations), with stable list order
   within each section. Relaxation may update offsets and section data, but
   not move a relocation to another section.
5. Once CSR exists, admission, materialization, reordering and GC/script
   compaction are rejected. Synthetic sections created during later layout
   are allowed but cannot add relocation records to the frozen worklist.
6. Static relocation application and shared dynamic relocation construction
   consume the same canonical worklist. `MiniLdFinalSymbols`,
   `MiniLdOutputPlan`, `MiniLdProgramPlan` and `MiniLdSharedImage` retain their
   existing separate output lifetimes.

7. RISC-V relocation *type* classification is shared by the static and shared
   pre-layout scanners. The static GOT reservation and shared GOT/PLT/TLS,
   dynamic RELA and TEXTREL reservation are separate included phase owners;
   output-mode symbol preemption and all final application remain separate.

No extra parallel relocation counter is stored in work sections: its old
`relocation_count` value was incremented or reset but never read.

## Gates and non-goals

`make MODE=release CFLAGS=-Werror check-minild-reloc-owner-lifecycle`
exercises eight paths: single/double `-r`, direct and second-round static GC,
direct and second-round script GC, plus direct and second-round ET_DYN.
Fixtures include two R_RISCV_ALIGN sites, COMDAT winner/loser, 32-byte
alignment after relaxation, canonical symbol pointer fixups, dead relocations,
five dynamic relocations and three PLT relocations. The older input-work,
archive, shared, symbol and output-plan gates remain required.

This refactor does not introduce new GNU ld semantics or reorder output
planning. Host-generated ELF fixture passes are **not** evidence of Linux
runtime boot, which requires separate toolchain and QEMU acceptance.
