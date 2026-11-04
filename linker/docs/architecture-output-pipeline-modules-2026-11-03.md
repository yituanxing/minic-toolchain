# MiniLD: output pipeline module boundaries (2026-11-03)

## Scope

The output-side implementation formerly occupied most of
`linker/src/linker_core.inc`. Its phase dependencies are now expressed by
separate included implementation files. These
are **physical phase boundaries**, not independently compiled objects: the
linker still has one C translation unit, and the public linker API, ELF
format decisions, and runtime behavior remain unchanged.

| Included module (in order) | Owned output-stage work | Reason for boundary |
| --- | --- | --- |
| `linker_layout.inc` | RISC-V relocation encoding, static GOT and relaxation, static/shared address layout, linker-script expression and section placement | Computes and mutates working-section placement before ELF output is serialized. |
| `linker_output_plan.inc` | `MiniLdOutputPlan`, surviving output-section identity, non-alloc placement, section headers and bytes | Projects the placed working sections into output sections; does not own original input identities. |
| `linker_final_symbols.inc` | `MiniLdFinalSymbols` plus `MiniLdFinalSectionTables` projection | Reads canonical symbols and the OutputPlan; builds the shared final ELF symbol/name/section-header trailer. |
| `linker_program_plan.inc` | Common `MiniLdProgramPlan` lifetime and final PHDR range validation | Both writers consume one output-neutral program-header contract, while each retains its own segment policy. |
| `linker_elf64_image.inc` | Final ELF64 Ehdr/PHDR/section-byte/table serialization and file sink | Consumes already-finalized plans; does not decide section selection, relocation or layout. |
| `linker_static_output.inc` | Static PT_LOAD selection and ET_EXEC writer | Owns executable segment policy and final static emission, including scripted output. |
| `linker_shared_output.inc` | `MiniLdSharedImage`, dynamic symbols/relocations, dynamic metadata, ET_DYN writer and shared-link entry point | Owns shared-object output state and its writer without mixing it into ET_EXEC policy. |

`linker_core.inc` defines the shared core types and include order, while
`linker_admission.inc` owns source-only ELF/archive/COMDAT admission.
`linker_input.inc`, `linker_work_sections.inc` and
`linker_relocations.inc` own their separate state lifetimes. The
`linker_eh_frame.inc` and
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
4. ET_EXEC and ET_DYN still decide LOAD boundaries, relocation handling,
   synthetic section links, ELF kind and symbol-table ordering. They share
   one **ELF64-only** final image serializer, not a new target-neutral image
   IR or prematurely generalized COFF/Mach-O writer.

## Shared output contracts after consolidation

The subsequent ownership pass makes these boundaries operational, rather
than only moving implementation text:

- Script placement writes work VMA/LMA/file offsets, owner-command identity,
  discard state and script output-command spans/fill directly into
  `MiniLdLayout`. The former parallel arrays, temporary mirrors and
  success-only ownership transfer are removed. Each script output retains a
  `defined` bit because VMA zero is valid; synthetic-section capacity and
  vDSO's file-offset rewrite remain intact.
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

## OutputPlan lifecycle and final ELF64 boundary

`MiniLdOutputPlan` already owns the actual ordered `OutputSection` objects,
their work members, script command ownership, `work_shndx`, physical offsets
and final section-header projection. Creating a second generic OutputSection
tree would duplicate canonical state.

Both public link drivers now build and destroy the OutputPlan after layout:
the ET_EXEC driver creates it once after final static fixups, whether scripted
or not; the ET_DYN driver creates it before dynamic symbols and relocations,
which require final output-section indexes. Both final writers borrow their
plan. Static and shared ProgramPlan builders retain their own PT_LOAD/PHDR
policies, while final symbols, section-table ownership and emission are common.

`minild_elf64_write_image` receives finalized output/program/symbol/section
plans, ELF kind and entry address. It owns the temporary image bytes and final
file sink; output-mode writers supply section-header links before invoking it.
The script-layout producer owns VMA/LMA, script BYTE and fill decisions until
OutputPlan projects them. It now holds these decisions in two typed owner
records rather than ten independently allocated, indexed arrays:

- `MiniLdLayout.work[i]` (`MiniLdLayoutWorkPlacement`) contains a working
  section's VMA, LMA, file offset, script owner command and discard bit.
- `MiniLdLayout.script_outputs[i]` (`MiniLdLayoutScriptOutput`) contains a
  script output command's address span, file base, fill policy and explicit
  `defined` bit. The former separate `output_defined` scratch array is gone.

`minild_output_plan_build` now reads those placements directly from the
layout, without a parallel file-offset argument. Its own final
`work_file_offsets` projection remains intentional: final output emission
also needs offsets for non-allocatable bytes. Both link drivers destroy their
layout unconditionally through a zero-safe destructor, including on a failed
layout build. The script-layout capacity includes synthetic working sections,
and reserved placement slots and absent script file bases carry explicit
`SIZE_MAX` sentinels.

The pure-BYTE regression exposed one related correctness error:
a generated work anchor for an output command with no input fragments
overwrote the script's nonzero fill hole with zeros during final copying.
That anchor is now initialized with the already-evaluated script fill before
explicit BYTE patches. The regression checks direct, `-r`-then-final and GC
links against the GNU host linker for section identity, exact gap bytes and
PT_LOAD mapping. It now additionally covers **two consecutive pure-script
output sections**, separate nonzero fill policies, four boundary symbols,
cross-section symbol index identity, `AT(...)` LMA inheritance and program
header address congruence. The synthetic work anchor is retained deliberately:
like LLVM lld's synthetic input fragments, it owns concrete generated bytes,
section membership and symbol anchors while OutputPlan owns the ordered
output-section projection. Removing it here would conflate separate owners.

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
- `linker_static_reloc_scan.inc`: ET_EXEC pre-layout static GOT site scan and
  slot reservation, called from the layout stage.
- `linker_shared_reloc_scan.inc`: ET_DYN pre-layout dynamic RELA, GOT, PLT,
  TLS GD and TEXTREL reservation, called from shared metadata preparation.

The two pre-layout scanners use the same psABI relocation **type** classifier
in `linker_riscv_fixup.inc`; each output mode keeps its own symbol-resolution,
preemption, diagnostics and reservation policy.

These modules are included by the same C translation unit and deliberately
retain distinct static and shared relocation policies: preemption, TLS,
GOT/PLT and dynamic relocation reservation are not target-neutral byte
patching. `R_RISCV_RELAX` instruction rewriting and paired ULEB final
relocations are not added by this ownership change.

### Final RV64 fixup convergence (2026-11-04)

ET_EXEC and ET_DYN now share the **final instruction encoding and PCREL
HI20/LO12 pairing owner**, in addition to the earlier relocation-type
classifier and arithmetic byte writer:

- `MiniLdRiscvPcrelPairs` owns one post-layout HI20 site table keyed by
  current working-section identity and offset. Both modes use the same
  initialization, lookup, LO12 field patch and cleanup path.
- `minild_riscv_apply_final_instruction` patches the common U/I/S, CALL,
  BRANCH/JAL/RVC and `R_RISCV_32_PCREL` instruction or data fields. Its
  callers provide the already-selected target and the output-mode-correct
  place address; the encoder does not resolve symbols or reserve metadata.
- RV64 S+A and PC-relative displacement arithmetic interpret the complete
  64-bit address space, including high-half kernel addresses, using defined
  unsigned bit operations. HI20/LO12 rounding avoids signed overflow and
  negative signed shifts. LO12 anchor offsets reject underflow/overflow.

The **mode boundary is still deliberate**: ET_EXEC resolves absolute writes,
static GOT slots and its detailed JAL overflow diagnostic in input order;
ET_DYN decides section liveness, non-ALLOC placement, preemption, GOT/TLS GD,
dynamic RELA and PLT destinations. Shared arithmetic runs before its final
instruction pass; static arithmetic stays interleaved with other input-order
relocations. Shared PLT slot generation and CALL destination selection retain
their own later phase, while CALL instruction encoding uses the target helper.
This does not add a new relocation family or change GNU compatibility scope.

## Validation and limits

The strict `MODE=release CFLAGS=-Werror` host gate now covers MiniLD targets,
including input/work-section ownership, COMDAT/archive admission, RISC-V
relocation lifecycle, script/GC, output-section ownership, ET_EXEC and
ET_DYN output.  Its outputs are host ELF fixture checks; Linux/QEMU runtime
acceptance is separate and is not claimed here.

The dedicated `check-minild-riscv-final-fixup-parity` gate decodes output
instruction fields for ET_EXEC and ET_DYN, tests positive/negative PCREL
LO12 addends, GOT and PLT metadata, branches, compressed branches and CFI PCREL32,
and checks missing-pair and out-of-range failures. A scripted static kernel
VMA at `0xffffffff80000000` covers nearby PCREL/JAL and a GC-discarded
unmatched LO12. This is linker-byte correctness evidence, not a boot test.

Two regression extensions specifically guard the consolidated output
contract: static profile phases must occur exactly once in their existing
order, and the vDSO linker-script ET_DYN path must also preserve ELF section,
symbol, version and relocation identity after an intermediate `-r` link.
The expanded vDSO sanitizer coverage additionally exposed an empty
`.rela.dyn` case: zero relocations legitimately leave its data pointer NULL.
The dynamic relocation writer now avoids calling `memset` on that absent
buffer, without changing any emitted bytes.
