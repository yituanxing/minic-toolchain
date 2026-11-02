# MiniAS Source/Common ownership contract — 2026-11-02

## Design comparison
LLVM separates SourceMgr/MCAsmParser, MCContext, MCStreamer,
MCTargetAsmParser and MCObjectStreamer. GNU GAS separates input scrubbing,
macro expansion, generic source directives, target md_assemble and
fragment/fixup materialization. MiniAS adopts the ownership separation
without cloning LLVM's class hierarchy or GNU's parser.

## Implemented dataflow and lifetime
1. Persistent SourceGraph owns normalized physical input, stable sites,
   expansion identities and forced-branch replay records.
2. Each semantic replay creates a SourceContext owning conditions,
   macros/repeats and the current SourceStatementSink. Common binds it
   into the assembly session; Source allocates and destroys it.
3. A writable, borrowed logical statement and stable SiteID are delivered
   synchronously to Common's **only** private lower_source_statement.
4. Common exclusively creates Symbols, Sections, Fragments and MC expressions.
   Source receives just its private context, target lexical config and
   Common-owned expression/layout query services.
5. SourceGraph rebinds immutable Common Fragment spans into a shared
   pass-local growth-worklist. MCLayout owns growth decisions and cache.
6. Common imports Source's conditional layout-sensitive signal at parse
   completion; .ifdef reads a Common-owned defined-symbol predicate.

## Constraints
Source implementation headers and .c files cannot include
minias_internal.h, dereference MiniAs or inspect TargetOps. The Shared
SourceGraph/MCLayout worklist has exactly one declaration in
minias_source_replay_contract.h. The Common statement parser remains
single-owner; macro expansion and full semantic replays keep one pipeline.
Do not reintroduce a second state machine, text parser or legacy callback.

## Validation and next step
Use compile-time negative boundary tests, GNU macro/conditional/replay
oracles and exact-head four-target/strict runtime CI; Linux Image/QEMU P1
is a separate milestone. P06 now splits Common by semantic ownership:
Section/Symbol context, typed source lowering, Fragment emission and shared
LEB serialization have distinct translation units (see
common-owner-split-2026-11-02.md). Source services still receive an opaque
Common session token for expression/layout queries; this is intentionally
not claimed as a completely independent MCContext implementation.
