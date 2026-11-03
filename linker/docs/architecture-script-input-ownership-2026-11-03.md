# MiniLD script input-section ownership checkpoint (2026-11-03)

This checkpoint continues
[`architecture-input-sections-as-contract-2026-11-03.md`](architecture-input-sections-as-contract-2026-11-03.md)
and
[`architecture-output-sections-2026-11-03.md`](architecture-output-sections-2026-11-03.md).
The implementation target remains **RV64 ELF**; this is neither a claim of
full GNU ld script support nor evidence of a new Linux/QEMU runtime pass.

## Problem and data-flow correction

`process_input_data` originally concatenates same-named ET_REL input sections
into a mutable `MiniLdSection` before the linker script gets to choose them.
`InputFile`, `InputSection`, original symbol offsets and original relocation
offsets are fortunately kept separately.  Merely adding a filename-aware
script parser would still leave the script with one merged section to place.

The final static **script** and **GC** paths, together with the vDSO-style
shared **script** path, now share the following input-owner sequence:

1. Read ET_REL/archive files, preserve exact admitted input section identity,
   and select COMDAT winners before recording definitions/relocations.
2. Re-materialize each admitted ordinary source section as a distinct
   mutable working fragment.  The fragment points to its stable input name
   and source file path; copy only its own bytes, not inter-input merge
   padding.  Exempt RISC-V vendor attributes (`SHT_RISCV_ATTRIBUTES`), which
   have special aggregate encoding.  Shared script links preserve independent
   `.eh_frame` inputs as well: the CFI indexer walks all surviving fragments
   and builds one sorted search table.  GC without `-T` only rematerializes
   allocatable/link-order sections, avoiding unnecessary copies of large
   debugging metadata in Linux objects.
3. Rebind symbol values and relocation offsets from their **original input**
   coordinates; remap `SHF_LINK_ORDER` targets and restore admission order.
4. Allocate COMMON, then run first-match script input selection ahead of any
   derived metadata.  With GC, trace entry roots, retains, relocation graph,
   COMDAT and link-order dependencies.  In shared-script links, apply
   `/DISCARD/` **before** `.dynsym`, GOT/PLT, TLS and `.rela.dyn` sizing:
   remove discarded relocation sources, reject live relocations that reference
   discarded definitions, and discard dependent `SHF_LINK_ORDER` metadata.
   An explicit `/DISCARD/` does not force the rest of a COMDAT group to die.
5. Run linker script placement against the individual working fragments,
   including filename-qualified input selectors and `SORT_BY_NAME`; decide
   final output ownership and align the output section against precisely
   its selected inputs.
6. Run existing RISC-V layout/relaxation and fixups.  Build the final
   `MiniLdOutputPlan` which owns output-section grouping, symbol section
   indices, link-order links, script holes and bytes for the ELF writer.

The materializer is intentionally **before** layout/relaxation.  In shared
mode it is also before COMMON allocation and dynamic metadata preparation,
whose section indices cannot safely be remapped afterwards.  No script/no-GC
links stay on the mature original path.  The `-r` pipeline stays independent:
it must preserve group signatures, input `sh_link`, relocations and the
distinction between intermediate and final link decisions.

The static ET_EXEC **driver itself is now singular**.  Earlier revisions
defined one no-GC driver inside `linker_core.inc`, renamed its public symbols
through preprocessor macros during inclusion, then defined a second script/GC
driver in `linker.c`.  Both duplicated parsing, entry choice, input loading,
COMMON/GOT preparation, layout, relocation, writing and destruction.
`minild_link_static_pipeline` now owns that lifecycle once.  It activates
materialization and source-order restoration only for scripts or GC;
ordinary no-script/no-GC links retain their merged fast path.  All modes
share the same phase profiling and cleanup.  This is a stage-ownership
change, not a new feature or unconditional rematerialization.

## Ownership and AS/LD boundaries

| Producer/owner | Retained contract | Link-time consumer |
| --- | --- | --- |
| MiniAS ELF writer | `(file, shndx)` and exact bytes/alignment | MiniLD InputSection / script placement |
| MiniAS `SHT_GROUP`/COMDAT | signature, membership, flags | Admission winner before symbols; GC group liveness; `-r` writer |
| MiniAS `SHF_LINK_ORDER` | source `sh_link` target | Source mapping -> working section -> final output `sh_link` |
| MiniAS relocations | relocation kind, symbol, source offset; RISC-V relax/ALIGN metadata | Target relocation/relax code after stable input placement |
| Link script | input filename glob + section name glob; KEEP, DISCARD, SORT_BY_NAME | Independent input selection and explicit output-section ordering |
| MiniLD OutputPlan | final section identity, VMA/LMA/offset, holes and synthetic sections | Static/shared ELF serialization |

The selector implementation intentionally handles the current supported
GNU subset (`file.o(.name)`, `*foo.o(.name)`, `*(SORT_BY_NAME(.name.*))`,
`SORT`, `KEEP`, `/DISCARD/`).  The lexer still lacks `?`, bracket character
classes and escaped glob syntax.  Archive members currently retain a member
path, but not the containing archive's path, so GNU's full
`archive.a:member.o` selector semantics are **not** yet implemented.

The materializer also separates ordinary non-allocatable input metadata.
`.riscv.attributes` deliberately retains its special vendor-record merge
policy instead of copying duplicate format markers.  Some program-section
types and GNU's full script wildcards remain outside this subset.

Shared `-T`/vDSO **is now migrated for the supported script shape**.  Its
metadata builder keeps working-section indices, so rematerialization and
order restoration occur before COMMON and shared metadata preparation.
The multi-input `--eh-frame-hdr` indexer scans every surviving source
`.eh_frame`, resolves FDE PCs against final addresses, then sorts the search
table by PC (not input-object order).  Explicit source-qualified `/DISCARD/`
of ordinary input text/data, including relocation-bearing inputs, now takes
effect before dynamic metadata is constructed.  Surviving references to
discarded definitions fail closed; late synthetic allocated sections still
have a fail-closed guard if a script tries to discard them.  This is a
supported script subset, not a claim of full GNU ld handling of all wildcard
grammar, output section types, or arbitrary CFI placement transformations.

## Acceptance evidence

`make MODE=release CFLAGS=-Werror check-minild-input-identity-script-order`
passes all three focused cases with a GNU ld.bfd x86-64 oracle:

- Ordinary static `-T` selects `.text.same` separately from `alpha.o` and
  `zeta.o` in **reverse of the command-line object order**.
- With `--gc-sections`, `KEEP`, `/DISCARD/`, source selection, sorting and
  absolute relocations remain correct.
- After `-r`, original source filenames no longer exist, so subsequent
  final linking uses section-only selection, preserving the intermediate
  object's actual input order and its remapped relocations.

The existing focused output owner, script holes/bytes, GC selection,
input-section GC, MiniAS section contract, shared/vDSO output and RISC-V
ABI gates also pass with `-Werror`.  Native `run-a0..a6` require a GNU
RISC-V cross toolchain, unavailable in this workspace; their absence is
not a successful runtime test.

`check-minild-shared-input-identity` independently compares GNU ld.bfd's
ET_DYN output with MiniLD's RV64 output for file-qualified source sections,
`SORT_BY_NAME`, dynamic symbols, dynamic relocations, `SHF_LINK_ORDER` and
four vDSO PHDRs.  The original shared script path failed with an unplaced
`.text.same` section; the materialized shared path now passes both halves
of this oracle.  It also verifies two FDEs from separate `.eh_frame` input
producers under `--eh-frame-hdr`, including individual FDE-to-PC mappings and
search-table order when input order differs from final text order; and verifies
that allocated inputs selected by `/DISCARD/` disappear from shared output
without leaving dynamic relocation entries.  The focused
`check-minild-shared-discard-inputs` gate compares GNU and native ELF for
source-specific discard, surviving dynamic relocation counts, and
`SHF_LINK_ORDER` dependent metadata pruning.

`check-minild-static-pipeline-boundaries` separately exercises four ET_EXEC
invocations (plain, script, GC and script+GC).  It checks the original
no-script fast path, final symbol and relocation addresses, liveness,
and all five `MINILD_PROFILE_PHASES` checkpoints.  No RISC-V GNU cross
toolchain is required for this structural regression.
