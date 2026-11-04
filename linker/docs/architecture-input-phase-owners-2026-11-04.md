# MiniLD: input phase owner boundaries (2026-11-04)

## Scope

This is a source-architecture pass only. ELF admission, archive extraction,
source-to-work projection, script selection, garbage collection and final
linking still use the same algorithms and the same ordered single C
translation unit. No input format, relocation, linker-script feature or
target architecture is added.

## Input ownership

| Owner | Stage and retained state |
| --- | --- |
| `linker_input.inc` | Canonical InputGraph with original file, section and symbol identities |
| `linker_admission.inc` | ET_REL/ET_DYN object acceptance and COMDAT/group selection into InputGraph |
| `linker_archive_admission.inc` | Regular/thin archive decoding, member identity and demand-driven extraction |
| `linker_input_sequence.inc` | Command-line object/archive ordering and archive-group fixed-point traversal |
| `linker_work_sections.inc` | Mutable WorkSection workspace created only after complete admission |
| `linker_input_projection.inc` | The one InputGraph-to-WorkSection projection and work-section order remaps |
| `linker_script_input_selection.inc` | Source-fragment matching and script /DISCARD/ selection before final layout |
| `linker_input_gc.inc` | Reachability, KEEP roots, section/link-order/group dependency closure and GC pruning |
| `linker.c` | Public ET_EXEC link driver, CLI GC switch and ordered phase calls |

The archive parser and extraction stage does **not** construct a provisional
WorkSection. A member is admitted as canonical source identities; later
projection chooses either merged work sections or exact input fragments.
The archive owner holds its private archive structures instead of leaking
them through the shared Core type declarations.

## Required phase order

1. Resolve the input sequence and archive groups; select COMDAT members
   into the InputGraph.
2. Project admitted inputs exactly once into WorkSections; restore source
   order when script matching requires it.
3. Apply script input selection and /DISCARD/ rules, then perform static
   reachability GC where requested.
4. Reserve synthetic work bytes and relocations before physical layout,
   including mode-specific metadata and GOT requirements.
5. Lay out addresses, produce the OutputPlan, apply final relocations and
   serialize ELF64.

ET_DYN keeps its distinct script pruning and dynamic symbol predeclaration
before metadata reservation. ET_REL keeps its existing projection and
emission path. The static pipeline remains the only ET_EXEC phase driver;
the input owner files are not separate compiler objects or additional
competing pipeline controllers.

## Validation

The dedicated input owner boundary gate checks physical include ordering,
unique function/type ownership and actual host ELF fixtures. Existing
InputGraph-only, archive/COMDAT, source identity, GC, linker-script,
ET_REL, ET_EXEC and ET_DYN gates remain the correctness oracles. For this
architecture pass, Linux/QEMU boot and GNU cross-toolchain comparison are
separate later acceptance tasks; no new runtime claim is made here.
