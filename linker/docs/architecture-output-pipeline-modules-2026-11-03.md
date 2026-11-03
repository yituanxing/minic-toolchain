# MiniLD: output pipeline module boundaries (2026-11-03)

## Scope

The output-side implementation formerly occupied most of
`linker/src/linker_core.inc`.  Its original declaration and execution order
has now been preserved across six included implementation files.  These
are **physical phase boundaries**, not independently compiled objects: the
linker still has one C translation unit, and the public linker API, ELF
format decisions, and runtime behavior remain unchanged.

| Included module (in order) | Owned output-stage work | Reason for boundary |
| --- | --- | --- |
| `linker_layout.inc` | RISC-V relocation encoding, static GOT and relaxation, static/shared address layout, linker-script expression and section placement | Computes and mutates working-section placement before ELF output is serialized. |
| `linker_output_plan.inc` | `MiniLdOutputPlan`, surviving output-section identity, non-alloc placement, section headers and bytes | Projects the placed working sections into output sections; does not own original input identities. |
| `linker_final_symbols.inc` | `MiniLdFinalSymbols` plus `MiniLdFinalSectionTables` projection | Reads canonical symbols and the OutputPlan; builds the shared final ELF symbol/name/section-header trailer. |
| `linker_program_plan.inc` | Common `MiniLdProgramPlan` lifetime and final PHDR range validation | Both writers consume one output-neutral program-header contract, while each retains its own segment policy. |
| `linker_static_output.inc` | Static PT_LOAD selection and ET_EXEC writer | Owns executable segment policy and final static emission, including scripted output. |
| `linker_shared_output.inc` | `MiniLdSharedImage`, dynamic symbols/relocations, dynamic metadata, ET_DYN writer and shared-link entry point | Owns shared-object output state and its writer without mixing it into ET_EXEC policy. |

`linker_core.inc` continues to own the earlier admission, input graph,
working sections, and relocation workspace, as well as the outer link
orchestration after these includes.  The `linker_eh_frame.inc` and
`linker_version.inc` includes remain nested in the shared-output phase at
the same relative point as before.  `shared_build_layout` and
`shared_build_vdso_script_layout` intentionally remain with layout helpers
even though the shared-output writer calls them: this split does not yet
invert those intra-translation-unit dependencies.

## Invariants retained

1. Static and shared outputs both consume canonical input identities and
   relocations through their existing workspaces.  Output plans and final
   symbols are derived projections, not replacements for input ownership.
2. Linker-script output-section grouping, memory placement, relaxation and
   symbol evaluation keep their original order; phase calls are not
   normalized into a new algorithm by this split.
3. Static program segments and shared dynamic segments remain distinct
   policies.  The small scripted PT_LOAD comparator lives with the static
   segment owner, rather than general placement helpers.
4. The existing static/ET_DYN ELF writers still decide LOAD boundaries,
   relocation handling and ELF kind. No generic image writer or intermediate
   representation is introduced in this pass.

## Shared output contracts after consolidation

The subsequent ownership pass makes these boundaries operational, rather
than only moving implementation text:

- Script placement now writes each `section_owner`, output VMA and final
  output file base into the single layout-owned storage from the outset.
  The duplicate temporary mirrors and success-only ownership transfer were
  removed. The separate `output_defined` bit stays because VMA zero is valid;
  synthetic section capacity and vDSO's file-offset rewrite remain intact.
- `MiniLdOutputPlan.work_count` bounds its own work-section-to-ELF-index
  projection. `MiniLdFinalSymbols.entry_count` owns the final symbol count;
  writers do not infer these sizes from the mutable input resolver.
- `MiniLdFinalSectionTables` owns the final `.symtab`, `.strtab`, `.shstrtab`
  names, ELF section headers, byte offsets and trailer emission. Both writers
  consume it and release it through one destructor. ET_EXEC retains physical
  **symtab → strtab → shstrtab** order, while ET_DYN retains **strtab →
  symtab → shstrtab**. Shared dynamic section `sh_link`/`sh_info` adjustment
  remains in the ET_DYN writer after common header projection.
- `MiniLdProgramPlan` and its range validator move out of the static writer
  to `linker_program_plan.inc`. Static and shared segment builders continue to
  use distinct policies; the shared writer no longer relies on the static
  writer to declare its common program-header type.

The layout object is consumed by both executable and shared-object output.
Its name is `MiniLdLayout`, with direct consumers migrated rather than a
compatibility alias.

## RISC-V final relocation owner checkpoint

The existing relocation semantics are now located at explicit included
implementation boundaries without changing phase order or output formats:

- `linker_riscv_fixup.inc`: shared RV64 instruction encoders, HI20/LO12 and
  PC-relative pairing helpers, plus the single checked ADD/SUB/SET arithmetic
  relocation byte writer used by ET_EXEC and ET_DYN.
- `linker_static_reloc_apply.inc`: the ET_EXEC final relocation application
  pass, after static layout/GOT preparation.
- `linker_shared_reloc_apply.inc`: ET_DYN dynamic relocation construction,
  link-time arithmetic/final fixups and PLT/CALL finalization, still after
  shared metadata reservation and layout.

These modules are included by the same C translation unit and deliberately
retain distinct static and shared relocation policies: preemption, TLS,
GOT/PLT and dynamic relocation reservation are not target-neutral byte
patching. `R_RISCV_RELAX` instruction rewriting and paired ULEB final
relocations are not added by this ownership change.

## Validation and limits

The strict `MODE=release CFLAGS=-Werror` host gate now covers 25 MiniLD targets,
including input/work-section ownership, COMDAT/archive admission, RISC-V
relocation lifecycle, script/GC, output-section ownership, ET_EXEC and
ET_DYN output.  Its outputs are host ELF fixture checks; Linux/QEMU runtime
acceptance is separate and is not claimed here.

Two regression extensions specifically guard the consolidated output
contract: static profile phases must occur exactly once in their existing
order, and the vDSO linker-script ET_DYN path must also preserve ELF section,
symbol, version and relocation identity after an intermediate `-r` link.
The expanded vDSO sanitizer coverage additionally exposed an empty
`.rela.dyn` case: zero relocations legitimately leave its data pointer NULL.
The dynamic relocation writer now avoids calling `memset` on that absent
buffer, without changing any emitted bytes.
