# MiniAS — phase-limited Target read projections (2026-11-02)

This batch closes three related Common/Target ownership leaks together:

1. **PreLayout** used to receive the complete `MiniAsStmt *` vector and
   number of Sections. RISC-V scanned instruction source spelling and
   LoongArch scanned MCInst opcodes to choose target-wide policies.
2. **PostEmit** used to borrow the entire Common `MiniAsStmt *` and
   `MiniAsSection *` arrays, including mutable nested `char *name` and
   `unsigned char *data` despite the outer `const` qualifier. AArch64
   validates instruction sites and creates mapping symbols, RISC-V checks
   strict Base-I words and emits mapping symbols, and LoongArch checks
   fixed-width instructions.
3. **Relax/Encode and ELF fixup policy** handed Target callbacks a raw
   `const MiniAsSymbol *`. The public struct contained Common alias chains,
   anchor state and mutable symbol names. The same SymbolID may move while
   the MCLayout fixed point grows Fragment widths.

The public Target ABI now has opaque `MiniAsPreLayoutView` and
`MiniAsPostEmitView` handles. Each is a stack-scoped reference to the **live**
Common owner. Accessors copy only required scalar fields into:

| Value projection | Exposed fields | Source of truth |
| --- | --- | --- |
| `MiniAsFragmentView` | kind, const source opcode spelling, line/Section/offset/size, immutable site bits, guarded instruction opcode and alignment-fill policy | parsed Fragment and current MCLayout |
| `MiniAsSectionView` | const name and const finalized bytes, type/flags/align/size | committed Common Section, queried afresh |
| `MiniAsSymbolView` | const name, section/subsection/value, binding/type, definition and alias-status flags | stable SymbolID resolved in this phase |

The mutable `MiniAsSection`, `MiniAsSubsection`, `MiniAsSectionState`, and
`MiniAsSymbol` definitions have been moved out of `minias_target_api.h`
into `minias_internal.h`. No Target can compile direct access to them.
`minias_project_symbol` is the single Common conversion shared by
MCLayout, Emission and ELF fixup-policy dispatch. Missing symbols still
return **false**; Target branch/fixup policy retains its former unresolved
semantics. The finalization sink still exclusively owns mapping-symbol and
Section-alignment writes. A fresh Section query sees earlier allowed
finalization changes within the same pass; no cached parallel graph appears.

This is **not** a redesign of instruction encoding or ELF object format.
`MiniAsStmt` and its Target-owned MCInst remain visible to the single
instruction parse/measure/relax/encode lifecycle; these callbacks already
operate under narrow stage contexts and must be tightened separately if an
immutable MCInst operand view becomes necessary. No same-phase mutable
fragment copy, second symbol table, SourceGraph replay or writer exists.

## Safety and verification

- `probe-mc-target-readonly-projections.sh` negative-compiles attempts to
  access the two opaque stage views and two now-hidden mutable records,
  plus writes to const nested pointers; checks all five Target translation
  units and the unique Common projection owners. It is part of the MiniAS
  Target ISA/ABI workflow.
- `probe-mc-target-readonly-finalization.sh` checks the emitted AArch64
  mapping sequence `$d@0`, `$x@4`, `$d@12` and final `.text` alignment 4
  after the Common sink's allowed changes; this does not require a GNU
  cross-assembler and runs in the same Target workflow.
- `probe-mc-target-boundary.sh` checks the new accessor boundary while
  retaining the original parse/measure/relax/encode and finalization order
  invariants. The Fragment union probe checks guarded LoongArch opcode
  access through the phase projection.
- The unchanged GNU byte/relocation and macro/branch/SourceGraph tests
  exercise the same Target logic through the narrower interfaces.
- `compare-target-readonly-baseline.sh` accepts an independently compiled
  previous-head MiniAS binary: across 26 fixed source fixtures and all four
  targets, 18 successful ELF objects match **byte-for-byte** and eight inputs
  fail closed on both versions with no partial object. It is an opt-in local
  comparison, not a substitute for GNU reference assembly.
- The local x86 GNU targeted group additionally exercises symbol-binding
  relocations, i386 PIC GOT/GOTOFF/GOTPC/PLT shared links, ELF32 REL,
  real-mode/code16 boot probes (38 cases), vDSO `.eh_frame`, SourceGraph
  replay, deferred `.org` and single-commit/NOBITS semantics.
- Full 3,536 optional GNU cases and Linux/QEMU P1 acceptance remain distinct
  from the required exact-head workflow conclusions.
