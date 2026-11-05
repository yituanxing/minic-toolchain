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
   mapping-symbol allocation. Hash slots and capacity bookkeeping live in an
   opaque `MiniAsContextIndexState` and are inaccessible to Layout/Writer.
   Its two release functions preserve the established Target-state
   destruction order.
3. **Lowering** (`common_lowering.c`) is the only statement/directive parser.
   It creates semantic Fragments, including immutable DATA snapshots and
   retained variable-size expression programs. It does not materialize
   output Section bytes. An opaque `MiniAsLoweringState` owns all parser
   worklists and the source-Fragment destructor, publishing only a value-type
   layout-control plan after parsing.
4. **MCLayout** decides final Fragment offsets and validates placement;
   an opaque `MiniAsLayoutState` owns Subsection storage, the SectionID +
   numbered-subsection lookup index and the persistent replay anchor graph.
   CFI reads the current Subsection through a `const` lookup, never by
   traversing or mutating Layout's private vector.
5. **Emission** (`common_emission.c`) sorts Fragment references, checks
   measured sizes, stages all instruction and typed DATA writes, validates
   fragment-relative Fixups and synthetic anchors, and commits complete
   EncodeResult transactions to Sections. Explicit section relocations and
   generated DWARF retain their deliberate section-owned path. Its mutable
   `MiniAsEmissionState` is opaque outside `common_emission.c` and the shared
   request encoder `mc_encode_result.c`.
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
- The driver allocates an Emission state before SourceGraph parsing and
  releases it on every success/failure path. Layout and ELF serialization
  borrow a `const MiniAsFixupSpan` over committed Fixups; they cannot append
  or alter them. MCData obtains its staged-byte cursor per field from the
  Emission owner so `.` still advances within an uncommitted DATA Fragment.
- The private `MiniAsEncodeResult`, transactional write kinds, Fixup
  requests, fragment guards and synthetic-anchor ordinal are absent from the
  shared `MiniAs` definition. They have one private definition, included by
  exactly two transaction owner files.
- Context, Lowering and MCLayout similarly own their allocation metadata,
  vectors and caches privately. Every pass constructs all three before
  SourceGraph parsing or the initial `.text` subsection; the existing failure
  unwind destroys Layout first, then Context/Target/Lowering/Emission in the
  established order. An absent state fails before parsing, not during an
  unguarded partial assembly.
- The shared SectionID/SymbolID/Stmt records are intentionally **not** copied
  or boxed: Layout must adjust offsets/aliases, Lowering must create typed
  Fragments, CFI/DWARF must attach symbols and Emission must materialize
  Section bytes. No second object graph or forwarding getter layer appears.

## Validation and limitations

The structural gate `probe-common-context-emission-boundary.sh` checks
unique function owners, compiles a negative opaque-field access, verifies
read-only query consumers, tests Context/Lowering/Layout negative C field
accesses and rejects a renewed Lowering/emitter cycle.
Existing Fragment transaction, Source replay, Target capability, GNU
byte/relocation and DWARF/CFI probes must also pass at the exact commit.
Full 3x7 Linux GNU Kbuild replay, 3,536 opt-in semantics and Image/QEMU
P1 are separate milestone gates, not implicit results of a local build.

These increments encapsulate the Emission transaction, persistent identity
indexes, parser-local worklists and MCLayout subsection/anchor storage—not
the shared mutable Section, Symbol or Fragment records. Separating the
records' true phase-aware ownership (without duplicating or returning mutable
raw pointers to Targets) remains a boundary. The subsequent
[Fragment payload migration](fragment-payload-2026-11-02.md) has now completed
the kind-discriminated union without adding an ObjectModel. A `const` Fixup span also carries a
lifetime constraint: it cannot be retained across later Emission appends.
