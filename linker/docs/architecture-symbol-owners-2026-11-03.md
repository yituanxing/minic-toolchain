# MiniLD: original symbols, resolved symbols and final ELF views (2026-11-03)

## Purpose and scope

MiniLD already distinguished original input files/sections from mutable work
fragments, and already built a temporary final-symbol view. The remaining
problem was that a single `MiniLdSymbol` record simultaneously held the
*winning input definition's provenance* and mutable state for resolution,
   script overrides, GC source projection, relaxation and final ELF output.

This pass separates **ownership**, without changing the canonical symbol
index used by relocations or introducing new GNU-compatible symbol features.
The relevant reference models are GNU binutils `ld.bfd`'s ELF input/global
symbol resolution and LLVM LLD's distinction between input-file symbols,
resolved symbols and synthesized output tables. It remains a RISC-V ELF
implementation with deliberate subset semantics.

## Data and index ownership

| Owner | Lifetime / mutability | Identity / consumers |
| --- | --- | --- |
| `MiniLdInputSymbol` | One non-null original ET_REL symbol per source object. Original ELF section, value, size, binding, type, visibility and name never change after input. A stable input-section ID and canonical link are attached during initial ingestion. | `MiniLdInputFile.first_symbol/symbol_count` owns a contiguous source range, including lazy archive members; `file` + `elf_index` identifies the exact source. `canonical_symbol` maps it to a resolved slot, or `SIZE_MAX` for discarded local COMDAT. Losing definitions still have records. |
| `MiniLdResolvedSymbol` | One canonical resolution slot; `section`, `value`, `size`, `info`, `other`, and winning `source_symbol` can change during global resolution and later layout. Synthetic/script-owned values use `source_symbol == SIZE_MAX`. | Stable `state->symbols[]` index. `MiniLdReloc.symbol`, local object symbol maps, COMDAT signature references, the global-name index and dynamic-link maps continue using it. |
| `MiniLdFinalSymbols` | Disposable read-only projection after output-section layout for ET_EXEC/ET_DYN. | Owns class-neutral `MiniElfSymbol` descriptors, string table, local/global partition and final section indices. The final ELF Writer encodes ELF32/64 records. Never edits source or resolved records. ET_REL keeps its source-fragment serialization path. |

Source candidates and canonical resolution are **not** in one-to-one
correspondence: weak and strong definitions from different files can map to
the same canonical ID, but only its `source_symbol` selects the winning input
definition. A discarded COMDAT local source has no canonical mapping. Script
definitions and linker-synthesized symbols have no input source.

## Phase contracts

1. Parse an input file (including an extracted archive member), recording
   each original symbol and its original ELF index *before* dropping a local
   from a discarded COMDAT group. Resolve original section IDs to stable
   input-section IDs. Do not rewrite the original value when calculating a
   concatenated working-fragment offset.
2. Map each input symbol to its stable resolved symbol ID. For global names,
   the existing weak/strong winner rules update the resolved record's
   `source_symbol` only when a candidate wins; losing source records remain
   intact. Archive extraction and relocations keep canonical numeric IDs.
3. During the one post-admission projection, recover the selected canonical
   symbol's original offset from the winning `MiniLdInputSymbol`, then set
   its work section and value directly. Script/GC exact fragments have no
   merged-section base; ordinary coalescing applies source alignment once.
   Never recover input identity by reverse-engineering section names.
4. Script assignments may override a canonical definition, clearing its
   `source_symbol` because the current value no longer belongs to an input
   object. COMMON selection merges tentative candidate sizes and alignments
   in that same canonical owner; COMMON allocation, relaxation and final VMA
   assignment likewise mutate canonical state only.
5. Output plan and final-symbol projection translate canonical IDs into
   final ELF section indexes and values. ET_EXEC/ET_DYN `.symtab` share this
   producer; the shared writer's `.dynsym` follows the same resolution.

### Invariants

- Adding another input does not renumber earlier resolved symbols or
  relocations; input source IDs are append-only.
- A winning symbol always refers to its own raw source value, even if earlier
  weak definitions, discarded COMDATs or script/GC reordered fragments.
- Script and synthesized definitions cannot accidentally resurrect an
  old input value through stale provenance.
- The final ELF writer neither picks a winning input nor mutates the global
  resolver in order to assign output symbol indexes.

## Verification

`make MODE=release CFLAGS=-Werror check-minild-symbol-ownership` exercises
three independent link modes with host tools:

- Static script link: weak replaced by strong, archive member extracted only
  on demand, referenced COMMON and PROVIDE symbols, input ABS not overridden
  by PROVIDE, unresolved weak and relocated pointers to final definitions.
- Same inputs under `--gc-sections`: live source sections survive, symbol
  offsets and real cross-section relocations remain correct.
- Shared link: static `.symtab` and `.dynsym` agree on resolved winner,
  archive/COMMON/ABS handling and weak undefined behavior.

The existing input-section, output-plan, script, PHDR, final-symbol,
RISC-V ABI and static/shared regression gates remain required. These are
host structural checks, **not** QEMU Linux boot evidence; that requires
the GNU RISC-V toolchain and cached Linux objects not present here.

## Intentionally deferred

- Resolving every GNU edge case beyond the admitted COMMON precedence and
  archive-selection rules, new language-level symbol semantics, or broader
  archive/LTO support.
- Changing stable canonical indices into pointers or reordering them for
  output. Only the temporary final-symbol view controls ELF table order.
- Rewriting the ET_REL writer or the global index during this owner split.

This is an architectural boundary for later work, not a claim that all GNU
linker semantics are now complete.
