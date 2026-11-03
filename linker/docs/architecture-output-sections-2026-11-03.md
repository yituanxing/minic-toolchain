# MiniLD: output-section ownership and AS/LD contract (2026-11-03)

## Reference models and scope

GNU `ld.bfd` documents linker scripts as a mapping from **input sections**
to **output sections**, with `SECTIONS` controlling both selection and memory
placement: <https://sourceware.org/binutils/docs/ld/Scripts.html>.

LLVM LLD independently describes `OutputSection` as a container of ELF
`InputSection` records: <https://lld.llvm.org/NewLLD.html>. Its linker-script
implementation is documented separately:
<https://lld.llvm.org/ELF/linker_script.html>.

`gcc` and `clang` are drivers here; the relevant GNU reference is binutils
`ld.bfd`, and the LLVM reference is `ld.lld`. MiniLD intentionally implements
the required RISC-V ELF subset, not every GNU script extension.

## Actual MiniLD data flow after this change

```text
input .o / archives
    |
    v
MiniLdInputFile + MiniLdInputSection (stable source identity)
    |          COMDAT / GC / original section name / LINK_ORDER target
    v
MiniLdSection work fragments (mutable bytes, relaxation, relocations)
    |
    v
Script placement: command owner + VMA/LMA + ALLOC file offsets
    |               BYTE patches, fill, alignment, dot holes
    v
MiniLdOutputPlan: ordered OutputSection -> work fragment members
    |               work_shndx + command_shndx
    +----> final non-ALLOC file spans and member offsets
    +----> common Section Header identity + SHF_LINK_ORDER
    +----> MiniLdFinalSymbols: ET_EXEC / ET_DYN .symtab
    +----> MiniLdProgramPlan: mode-specific PHDR producers
    |               ET_EXEC default/scripted, ET_DYN PIE/TLS/vDSO
    v
ELF writer: serialize headers, fill, relocated bytes and names

ET_REL -r: separate, unchanged source-fragment relocation path
```

The **working fragment** remains the unit of patching relocations and
relaxing `R_RISCV_ALIGN`. An **output section** owns the final ELF section
header, an ordered list of working-fragment IDs, the script command ID, the
output VMA/LMA/file span, type/flags/alignment, and optional fill.

The script command ID is persistent after layout. Two distinct output
commands do not merge merely because they share a string name. Final
`st_shndx` and `SHF_LINK_ORDER.sh_link` use one shared work-index-to-final-
section-index map; that same map is used by ET_DYN's `dynsym`, static
`symtab`, `.dynamic`, version sections, hashes and relocation metadata.

## Non-negotiable file-layout invariant

For every file-backed member of one output section:

```text
member.file_offset - output.file_offset
    == member.VMA - output.VMA
    == member.LMA - output.LMA
```

Script layout now preserves gaps caused by input alignment, output-dot
assignments and `BYTE()` placements as bytes at their actual output offsets.
The script owner's maximum input alignment is considered before selecting
the first file offset. Output sections include trailing dot holes in `sh_size`
and their corresponding static `PT_LOAD` span. Mixed input/BYTE outputs
materialize byte patches at absolute, checked output-relative positions.

`SHT_NOBITS` cannot be mixed blindly with `SHT_PROGBITS`, and `SHF_LINK_ORDER`
members can share one output header only when their targets resolve to the
same final output-section index. Violations fail closed with diagnostics.

## What stays separate

- `MiniLdInputSection`: immutable source identity for input selection, GC,
  COMDAT and exact `SHF_LINK_ORDER` associations.
- `MiniLdSection`: work-fragment bytes, `R_RISCV_ALIGN` relaxation,
  relocations, symbol-relative offsets and input ownership.
- `MiniLdOutputPlan`: output-owner membership, final ELF section indexes,
  non-ALLOC output/member file spans and generic section-header projection.
- `MiniLdFinalSymbols`: the read-only final ET_EXEC/ET_DYN symbol view.
- `MiniLdProgramPlan`: owns final `Elf64_Phdr[]`, independently produced for
  default/scripted static or shared/PIE/TLS/vDSO policy and checked against
  the final file size. Static `-T` retains independent LMA and `p_align=1`;
  ET_DYN RX `PT_LOAD` maps file headers, and vDSO preserves its prescribed
  four PHDR types, including a possibly empty `PT_GNU_EH_FRAME`.
- ELF writers: place synthetic symbol/name tables and serialize already
  finalized PHDRs, SHDRs, fill and relocated work bytes. Both use the same
  work-section emission function; ET_DYN adds its dynamic `sh_link/sh_info`
  policy after the common section-header projection.

## Final symbol-table ownership checkpoint

ET_EXEC and ET_DYN previously contained two independent `.symtab/.strtab`
builders.  The static writer also wrote `final_index` back into each mutable
resolved symbol, so output order accidentally became a property of the global
resolver's state.  Both final ELF writers now consume one temporary
`MiniLdFinalSymbols` view, built from the final `OutputPlan` and layout:

- It owns an ordered ELF64 `Elf64_Sym` array, a symbol-name string table,
  and the local/global boundary used for `sh_info`.
- It resolves `SHN_UNDEF`, `SHN_ABS`, `SHN_COMMON`, final `st_shndx`, section
  address values and TLS-block-relative `STT_TLS` values once.  The TLS
  block base is also reused by the shared dynamic-symbol writer.
- It is read-only with respect to `MiniLdResolvedSymbol`.  ET_REL keeps its separate
  source-symbol serialization, because it has not reached final layout.

This is the *final-symbol producer* boundary. The subsequent input-symbol
owner split is described in
[`architecture-symbol-owners-2026-11-03.md`](architecture-symbol-owners-2026-11-03.md):
immutable `MiniLdInputSymbol` candidates, stable-index mutable
`MiniLdResolvedSymbol` winners and read-only `MiniLdFinalSymbols` output.
Non-ALLOC member symbols include their offset inside the coalesced output
section, just as ALLOC symbols include their placed VMA.

The next output stage is now real rather than only proposed: non-ALLOC
placement, final-section headers, final program headers, validation and byte
emission have dedicated owners shared by ET_EXEC and ET_DYN. The remaining
writer-specific synthetic table offsets and ELF serialization are deliberately
kept separate from target-neutral input and symbol resolution.

`check-minild-final-symbol-view` verifies both ET_EXEC and ET_DYN, including
local-first symbol order, `sh_info`, ALLOC/non-ALLOC, ABS/UND, TLS, dynamic
symbol consistency, and static/dynamic relocation results.

## Verified contracts (host tools)

| Gate | Scope |
| --- | --- |
| `check-minild-output-section-owner` | Four stages: ordinary `-T`, GC, `-r -> -T+GC`, explicit 64-byte hole; exactly one output `.text`/`.rodata`/`.meta`, symbol indices and LINK_ORDER target |
| `check-minild-script-nonalloc-offset` | Static `-T` ordinary/GC: merged non-ALLOC sections, real `sh_offset`, preserved bytes, alignment, LINK_ORDER and final symbol offsets; ET_DYN preserves `.comment`/`.debug_info` and rejects unimplemented `-S` |
| `check-minild-segment-plan` | Five modes: static default/scripted including `AT()` LMA, shared `PT_DYNAMIC/PT_TLS`, PIE `PT_PHDR/PT_INTERP`, vDSO four-PHDR layout and empty EH header |
| `check-minild-output-script-edges` | GNU ld.bfd x86-64 oracle for output tail, mixed input/BYTE, nonzero one-byte fill, compared with MiniLD/RV64 output |
| `check-minild-shared-output-section` | Four ET_DYN modes: normal, symbol versioning, after `-r`, vDSO PHDRS/VERSION; dynsym/static symtab/sh_link/PHDR consistency |
| `check-minild-as-section-contract` | Four AS->LD cases, direct and repeated `-r`, with GC/COMDAT/LINK_ORDER |
| `check-minild-script-gc-input` | Script KEEP/DISCARD and precise input-source GC, including COMMON+.bss |
| `check-minild-riscv-abi*` | RISC-V e_flags and ABI mismatch handling |

Cross-tool GNU RISC-V and QEMU/Linux boot tests require tools and cached
Linux inputs not available in this workspace. Host structural results are
**not** a claim of runtime Linux success.

## Deliberate remaining boundaries

1. Without script or GC, historical identical-name source sections stay
   folded in the ordinary fast path.  Script and GC paths now rematerialize
   individual input sections before source-specific selection and ordering.
2. The script evaluator supports only a subset of GNU ld grammar. The new
   post-output `= fill` accepts a byte-sized pattern; larger GNU repeated
   hex patterns are diagnosed as unsupported.
3. Static custom `PHDRS`, arbitrary ET_DYN script layouts, complex mixed
   `NOBITS/PROGBITS` output sections and multi-format COFF/Mach-O are still
   outside this tested RISC-V ELF implementation.

For MiniAS this draws an exact boundary: the assembler must preserve
`.o` section flags, section names, relocation/fixup types, COMDAT group
membership, and `SHF_LINK_ORDER` targets. It does **not** decide final
output-section names, program headers, address gaps, or final `st_shndx`.
