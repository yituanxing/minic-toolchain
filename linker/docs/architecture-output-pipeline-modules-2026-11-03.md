# MiniLD: output pipeline module boundaries (2026-11-03)

## Scope

The output-side implementation formerly occupied most of
`linker/src/linker_core.inc`.  Its original declaration and execution order
has now been preserved across five included implementation files.  These
are **physical phase boundaries**, not independently compiled objects: the
linker still has one C translation unit, and the public linker API, ELF
format decisions, and runtime behavior remain unchanged.

| Included module (in order) | Owned output-stage work | Reason for boundary |
| --- | --- | --- |
| `linker_layout.inc` | RISC-V relocation encoding, static GOT and relaxation, static/shared address layout, linker-script expression and section placement | Computes and mutates working-section placement before ELF output is serialized. |
| `linker_output_plan.inc` | `MiniLdOutputPlan`, surviving output-section identity, non-alloc placement, section headers and bytes | Projects the placed working sections into output sections; does not own original input identities. |
| `linker_final_symbols.inc` | `MiniLdFinalSymbols`, final symbol-value and symbol-table projection | Reads the placed working sections and their canonical symbols for either final ELF kind. |
| `linker_static_output.inc` | `MiniLdProgramPlan`, static PT_LOAD policy and ET_EXEC writer | Owns executable segment policy and final static emission, including scripted output. |
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
4. The existing static/ET_DYN ELF writers still decide final offsets,
   relocations, and headers.  No new writer abstraction or intermediate
   representation is introduced in this pass.

## Validation and limits

The strict `MODE=release CFLAGS=-Werror` host gate covers 19 MiniLD targets,
including input/work-section ownership, COMDAT/archive admission, RISC-V
relocation lifecycle, script/GC, output-section ownership, ET_EXEC and
ET_DYN output.  Its outputs are host ELF fixture checks; Linux/QEMU runtime
acceptance is separate and is not claimed here.
