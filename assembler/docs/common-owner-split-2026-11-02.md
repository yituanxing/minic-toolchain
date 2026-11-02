# MiniAS P06 Common domain ownership — 2026-11-02

## Purpose and existing behavior

This is a single-owner extraction of existing Common implementation. The
assembler still accepts the same `.s`, performs SourceGraph semantic replay,
keeps typed Fragments through MCLayout, and emits a relocatable ELF through
the existing Target CodeEmitter and `elf_writer`. There is no new parser,
Source replay algorithm, ISA/ABI policy or ObjectModel writer.

## Data and call flow

1. **Driver** (`assembler.c`) constructs and destroys an assembly pass,
   invokes the SourceGraph/Lowering, Target pre-layout, MCLayout, Emission,
   read-only validation, Target finalization and ELF writer in that order.
2. **Common Context** (`common_context.c`) maintains the SectionKey-to-ID
   and Symbol/name-to-ID indexes, symbol alias dependencies, binding and
   mapping-symbol allocation. Its two release functions preserve the
   established Target-state destruction order.
3. **Lowering** (`common_lowering.c`) is the only statement/directive parser.
   It creates semantic Fragments, including immutable DATA snapshots and
   retained variable-size expression programs. It does not materialize
   output Section bytes.
4. **MCLayout** decides final Fragment offsets and validates placement;
   it retains its own replay anchor graph and is not merged into Context.
5. **Emission** (`common_emission.c`) sorts Fragment references, checks
   measured sizes, stages all instruction and typed DATA writes, validates
   fragment-relative Fixups and synthetic anchors, and commits complete
   EncodeResult transactions to Sections. Explicit section relocations and
   generated DWARF retain their deliberate section-owned path.
6. **Shared LEB** (`mc_leb.c`) encodes signed/unsigned 64-bit integers for
   source expressions and DWARF producers without touching Section state.

The previous `minias_common_emit_data_stmt` and `emit_common_pattern`
implementations are *moved*, not wrapped: there is one private DATA
materializer named `minias_emit_data_fragment` in Emission.

## Invariants to preserve

- A source Fragment is either fully staged and committed once, or assembly
  fails closed. No Target may choose a Section or mutate its finalized cursor.
- A Fixup remains owned by a Fragment-relative offset; explicit `.reloc`
  and generated DWARF are intentionally section-owned.
- SectionID and SymbolID are stable across layout passes; aliases can be
  refreshed against final symbol anchors without reinterpreting source text.
- Error handling uses the existing first-error-wins session and pass unwind.
  The Driver delegates Section/Symbol and emission allocation cleanup to
  their actual owner modules.
- The SourceGraph replay, Target ISA state and ELF serialization policies
  are unchanged in this pass.

## Validation and limitations

The structural gate `probe-common-context-emission-boundary.sh` checks
unique function owners and rejects a renewed Lowering/emitter cycle.
Existing Fragment transaction, Source replay, Target capability, GNU
byte/relocation and DWARF/CFI probes must also pass at the exact commit.
Full 3x7 Linux GNU Kbuild replay, 3,536 opt-in semantics and Image/QEMU
P1 are separate milestone gates, not implicit results of a local build.

P06 does **not** yet make `MiniAs` fields opaque between Common domains:
Lowering, MCLayout, Context, Emission and Writer still share their mutable
arrays through the Common-private session. That remaining field-level
capability reduction is the next architecture boundary, before type-complete
Fragment payload and neutral ObjectModel work.
