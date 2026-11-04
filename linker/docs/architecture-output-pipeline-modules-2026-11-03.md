# MiniLD: output pipeline module boundaries (2026-11-03)

## Scope

The output-side implementation formerly occupied most of
`linker/src/linker_core.inc`. Its phase dependencies are now expressed by
separate included implementation files. These
are **physical phase boundaries**, not independently compiled objects: the
linker still has one C translation unit, and the public linker API, ELF
format decisions, and runtime behavior remain unchanged.

| Module / owner | Owned output-stage work | Reason for boundary |
| --- | --- | --- |
| `linker_layout.inc` | Physical Layout state, byte-order helpers and default ET_EXEC/ET_DYN placement | Owns address/file placement and invokes the target's section-placement preparation at the point where section VMA becomes known. |
| `linker_relocatable_output.inc` | Standalone ET_REL projection and MiniELF serialization | `-r` consumes source/work sections, canonical resolver and relocations before any final-link Layout/OutputPlan exists; it must not borrow ET_EXEC or ET_DYN final tables. |
| `linker_riscv_align_relax.inc` | `R_RISCV_ALIGN` relocation-driven section-size adjustment, RISC-V NOP padding and offset remapping | Target-specific layout-time mutation; never part of the generic VMA/file-position algorithm. |
| `linker_static_got.inc` | Static GOT reservation, lifetime and post-layout content filling | Its reservation precedes section placement; its symbol-address writes follow it. One static relocation-workspace owner spans both steps. |
| `linker_script_layout.inc` | Script expression/provenance, input-section matching, command placement, BYTE/fill and script VMA/LMA/file offsets | Operates on the one `MiniLdLayout` before output-section projection. |
| `linker_vdso_layout.inc` | vDSO-specific PHDR membership and script file-base remapping | Keeps ET_DYN/vDSO policy separate from the general script evaluator. |
| `linker_riscv_default_symbols.inc` | GNU RISC-V default-script boundary symbols, including `__global_pointer$` | Keeps a target ABI/linker-script convention out of generic layout finalization. |
| `linker_layout_symbols.inc` | Post-layout canonical symbol-address resolution and entry-point lookup | ET_EXEC and ET_DYN share address resolution without relying on a statically named output owner. |
| `linker_layout_finalize.inc` | Post-layout constructor/destructor array-boundary symbols and static relocation application | Coordinates final static-specific work after placement; common symbol lookup and target-specific policy have dedicated owners. |
| `linker_output_plan.inc` | `MiniLdOutputPlan`, surviving output-section identity, non-alloc placement, section headers and bytes | Projects the placed working sections into output sections; does not own original input identities. |
| `linker_final_symbols.inc` | `MiniLdFinalSymbols` plus `MiniLdFinalSectionTables` projection | Reads canonical symbols and the OutputPlan; builds the shared final ELF symbol/name/section-header trailer. |
| `linker_program_plan.inc` | Common `MiniLdProgramPlan` lifetime and final PHDR range validation | Both writers consume one output-neutral program-header contract, while each retains its own segment policy. |
| `linker_final_elf64_plan.inc` | One `MiniLdFinalElf64Image` owner for final ProgramPlan, symbols and section tables | ET_EXEC and ET_DYN share non-ALLOC placement, final table construction, generic section-header projection, validation and cleanup without merging mode-specific policies. |
| `linker_elf64_image.inc` | Final ELF64 Ehdr/PHDR/section-byte/table serialization and file sink | Consumes already-finalized plans; does not decide section selection, relocation or layout. |
| `linker_static_output.inc` | Static PT_LOAD selection and ET_EXEC writer | Owns executable segment policy and final static emission, including scripted output. |
| `linker_shared_pre_layout.inc` | One `MiniLdSharedImage` owner, per-symbol dynamic plan, symbol preemption policy, dynamic metadata sizing and pre-layout relocation reservations | ET_DYN output capacities are fixed before physical layout; every resolver symbol has one consistent reservation/projection record. |
| `linker_shared_finalize.inc` | Dynamic symbol/hash/version/relocation content, `__ehdr_start` resolution and dynamic tags | Uses final addresses and OutputPlan indices without a second metadata owner. |
| `linker_shared_program_plan.inc` | ET_DYN TLS and PHDR policy plus dynamic section-header links | Keeps ET_DYN segment decisions separate from the common ELF64 serializer and ET_EXEC. |
| `linker_shared_output.inc` | ET_DYN writer, input checks and shared-link entry point | Calls phases in original order and owns the cleanup path. |

`linker_core.inc` defines the shared core types and include order, while
`linker_admission.inc` owns source-only ELF/COMDAT admission, while
`linker_archive_admission.inc` and `linker_input_sequence.inc` separately
own archive extraction and ordered input traversal.
`linker_common_allocation.inc` owns post-projection COMMON allocation for
both ET_EXEC and ET_DYN, before either layout stage starts.
`linker_input.inc`, `linker_work_sections.inc` and
`linker_relocations.inc` own their separate state lifetimes. The
`linker_static_reloc_scan.inc` remains nested under its static GOT owner;
`linker_eh_frame.inc` remains nested in shared pre-layout; `linker_version.inc`
and `linker_shared_reloc_apply.inc` remain nested in shared finalization,
with the same relative dependency order. `shared_build_layout` stays with common
default placement; `shared_build_vdso_script_layout` stays with the vDSO
script-layout policy. Both are invoked later by the shared-output driver,
without reversing their dependencies or making extra translation units.

### Layout owner split (2026-11-04)

The previous `linker_layout.inc` mixed default placement, RISC-V ALIGN
relaxation, linker-script evaluation, vDSO policy and final GOT/symbol work
in one implementation body. These responsibilities now have explicit
owners, preserving the **same `MiniLdLayout` state and the same
single-translation-unit ordering**. RISC-V relaxation changes physical
section length and also remaps symbol/relocation offsets: both the default
allocator and linker-script allocator must call the same target hook
immediately after assigning section VMA, *before* their respective cursors
advance. A separate post-layout relaxation pass would change linker-script
symbol values and output placement, so the current operation order is a
behavioral contract.

Static GOT slots are reserved before layout and filled only once symbol
addresses are known, while target-specific GNU default symbols are computed
after section placement. Common post-layout symbol resolution is consumed by
both ET_EXEC and ET_DYN, including their relocation writers and optional
entry-point lookup. These phases no longer borrow their ownership from the
generic `MiniLdLayout` implementation. No linker-script syntax, ISA,
relocation type or ELF output behavior is added.

The source-defined COMMON allocation runs after input projection and before
both static and shared layout. It belongs to work/symbol preparation rather
than physical placement, so it no longer lives in the layout implementation.

### Shared output phase owners (2026-11-04)

The formerly monolithic ET_DYN implementation is now one included phase
chain: pre-layout metadata and relocation budget; post-layout dynamic
contents and final fixups; ET_DYN-specific program and section-header
policy; final writer and link driver. All stages use the same
`MiniLdSharedImage`, `MiniLdLayout` and final OutputPlan.
`shared_finalize_ehdr_symbol` is defined beside post-layout finalization
but is still called at its original point after the OutputPlan exists.

The distinction between relocation **scan** (capacity and GOT/PLT
reservation before layout) and relocation **apply** (addresses and dynamic
relocation contents after layout) remains intact. No target relocation,
linker-script command, input kind or ELF output feature is added.

### Single ET_DYN symbol projection and Target PLT boundary (2026-11-04)

`MiniLdSharedImage.symbols[i]` owns one `MiniLdSharedSymbolPlan` for each
resolver symbol admitted before ET_DYN metadata reservation. Its fields are
the dynamic symbol index, dynamic string name offset, PLT index, ordinary
GOT offset, TLS-GD offset and the resolved preemption decision. The former
six separately allocated symbol-indexed maps (and their independent
capacity bookkeeping) are removed. `prepared_symbol_count` remains the
explicit bound: script symbol predeclaration completes **before** this
reservation, and the resolver may not append new symbol identities during
layout or dynamic table filling.

Scan and apply are deliberately separate consumers of the **same** record.
Pre-layout scanning reserves dynsym/GOT/PLT/TLS-GD storage; post-layout
relocation, dynamic hash/symbol/version tables and PLT finalization read the
already assigned indexes and addresses. This does not merge ELF local/global
binding, symbol preemption and instruction encoding into one algorithm.
In particular, missing slots continue to use explicit `SIZE_MAX` sentinels,
whereas a legitimate dynamic string offset or symbol index can be zero.

The ET_DYN owner still decides *which* symbols need PLT entries and builds
their `R_RISCV_JUMP_SLOT` dynamic relocations. The RISC-V Target fixup owner
decides *how* a PLT instruction sequence is encoded from the supplied
already resolved slot and entry addresses. Keeping those two decisions
apart prevents output-mode policy from becoming a second instruction encoder
while preserving the existing PLT byte sequence and reservation order.

Likewise, ELF64 serialization owns writing finalized header bytes, while
RISC-V target policy supplies machine/ABI/flag identity consistently with
accepted object and archive inputs and ET_REL emission. No additional
machine type, ABI configuration or output format is implied by this split.

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

### Output naming and final ELF ownership checkpoint (2026-11-04)

`MiniLdSection` no longer carries an `output_name` mirror. Work fragments
retain their input source name/path for GC, COMDAT, and section matching.
Script output names are borrowed from the parsed command into
`MiniLdLayout.script_outputs[command].name` and projected by
`minild_output_plan_build`; default and GC projections use the original
input name (or the canonical work name for synthesized sections). This is
especially important for GC, where a fragment's private work name differs
from the ELF output section's public name.

`MiniLdLayoutScriptOutput` is deliberately **not** merged with
`MiniLdOutputSection`: the former exists for every script command and is
needed during expression evaluation, BYTE/fill handling, synthetic work
anchor creation and vDSO file-base adjustment. The latter represents only
surviving final ELF sections, after layout has finalized mutable work sizes,
ownership and offsets. A premature OutputPlan would introduce a second
partially initialized output state.

ET_EXEC and ET_DYN now borrow one `MiniLdFinalElf64Image` lifetime owner
for `MiniLdProgramPlan`, `MiniLdFinalSymbols` and
`MiniLdFinalSectionTables`. A common final preparation pass handles
non-ALLOC placement, final symbols, synthetic tables, generic ELF section
headers and PHDR validation. The separate writers still choose their
respective PHDR policies, file-layout extent, symbol-table physical order
(`SYMTAB_FIRST` versus `STRTAB_FIRST`) and ET_DYN's dynamic header links.
The existing `minild_elf64_write_image` remains the sole file serializer;
no new target-neutral writer, output format or relocation semantics are
introduced.

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
- `linker_static_got.inc`: static GOT workspace lifetime and pre/post-layout
  orchestration. Its nested `linker_static_reloc_scan.inc` performs the
  ET_EXEC pre-layout site scan and slot reservation, while GOT contents are
  materialized with final resolved addresses after placement.
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

### Target and symbol resolution boundary (2026-11-04)

The architecture pass separates code by **who decides**, not just by when
it runs. Generic layout decides VMA/LMA/file placement and script command
ownership; RISC-V decides whether an `R_RISCV_ALIGN` site shortens its
working section and which NOP bytes remain. Both default and scripted
placement preserve that target operation at the original section-address
assignment point. The target's offset rewriting is internal to its owner;
generic placement only observes the resulting section size.

The RISC-V default linker-script symbol calculation, including GNU's
`__global_pointer$` window rule, is separate from output-neutral constructor
array marker synthesis. A common post-layout resolver supplies canonical
symbol VMAs to both relocation modes. Static GOT reservation/fill retain
their own lifetime instead of residing alongside `MiniLdLayout`, which is
neither a GOT nor a symbol-value store. The script work/output owners,
vDSO PHDR policy, OutputPlan projection, ET_EXEC/ET_DYN final images and
ELF serializer remain unchanged.

### Final RV64 fixup convergence (2026-11-04)

ET_EXEC and ET_DYN now share the **final instruction encoding and PCREL
HI20/LO12 pairing owner**, in addition to the earlier relocation-type
classifier and arithmetic byte writer:

- `MiniLdTargetRelocPairs` owns one post-layout HI20 site table keyed by
  current working-section identity and offset. Both modes use the same
  initialization, lookup, LO12 field patch and cleanup path.
- `minild_target_apply_final_instruction` patches the common U/I/S, CALL,
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

### Target relocation policy boundary (2026-11-04)

Static/shared pre-layout scanners and post-layout fixup drivers now consume
the same **target-owned semantic relocation contract**. `MiniLdTargetRelocClass`
names no-op, alignment, absolute 32/64-bit, arithmetic, final instruction,
PCREL LO12, call, GOT and TLS GD operations; only the RISC-V owner maps
`R_RISCV_*` numbers to those operations. The output-mode stages do not contain
RISC-V relocation-number switch statements or instruction encodings.

The RISC-V target also owns:

- byte width and store/truncation rules for absolute 32/64-bit writes;
- ELF relocation numbers for generic loader actions (`RELATIVE`, dynamic
  absolute, TLS module/offset and `JUMP_SLOT`);
- PCREL pair lifetime, final instruction fields and PLT instruction words.

ET_DYN still owns whether a relocation exists, the dynamic symbol index,
preemptibility, TLS GD/GOT/PLT slot allocation and final source liveness.
ET_EXEC keeps its original input-order application and error text. Thus this
boundary prepares future ISA adapters without pretending that the CLI or ELF
writer already support another target. The final-fixup probe now checks this
separation structurally as well as comparing actual ET_EXEC/ET_DYN bytes.

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
