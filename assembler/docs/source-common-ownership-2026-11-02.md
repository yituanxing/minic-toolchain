# MiniAS Source/Common ownership contract — 2026-11-02

## 2026-11-02 Source semantic capability closure

SourceGraph and SourceExpansion now use `MiniAsSourceContext *` throughout
their complete recursive call graphs: physical file load, lexical conditions,
macro/repeat/IRP expansion, stable SiteID interning, typed statement events,
forced-branch rebind and layout-growth capture. Common constructs one
`MiniAsSourceSemanticServices` value when it creates the SourceContext. It
contains five narrow callbacks (absolute expression, Target conditional,
defined-symbol predicate, numeric-reference rewrite, and first-error sink),
an opaque callback owner and the immutable comment/macro lexical policy.
Source may call these capabilities but cannot dereference the Common owner.

The old `minias_source_session(MiniAs *)`, `source_const_session`,
`source_target_*` helpers and exported defined-symbol predicate were deleted,
not kept alongside the new services. Common callers now pass their
SourceContext explicitly; neither Source implementation C file has a
`MiniAs *` parameter. No extra Fragment, Symbol, SourceFrame or alias graph
is retained. The original full SourceGraph replay on layout-sensitive
conditionals and the target-owned typed relaxation policy are unchanged.
The single SourceContext is recreated with each semantic pass and carries
only its pass-local callback owner; the physical graph survives across passes.

Release `-Werror`, A0 30/30, 26 four-target old/new cases, 24 standalone
Target contract scripts, x86/RISC-V full-replay GNU oracles, changed-arm
rebind, nested macro/rept/irp and RV 2→4→8 relaxation all pass locally.
CI exact-head and full original Linux/QEMU remain separate confirmations.

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
3. Source first handles conditionals, inactive arms, macro/repeat invocation,
   leading-label tokenization and `.end`. It sends a borrowed typed
   `LABEL` (bare name) or `TEXT` (ordinary statement), both with stable
   SiteID and line, through one synchronous event sink. Common's only
   consumer calls `define_label` for LABEL or the private
   `lower_source_statement` for TEXT; neither consumer reenters Source's
   conditional/macro/control router or repeats the label lexer.
4. Common exclusively creates Symbols, Sections, Fragments and MC expressions.
   Source receives its private context with bound scalar/callback services
   and immutable Target lexical policy; it does not carry `MiniAs *` into
   SourceGraph or SourceExpansion.
5. SourceGraph rebinds immutable Common Fragment spans into a shared
   pass-local growth-worklist. MCLayout owns growth decisions and cache.
6. Common imports Source's conditional layout-sensitive signal at parse
   completion; .ifdef reads a Common-owned defined-symbol predicate.

## Constraints
Source implementation headers and .c files cannot include
minias_internal.h, dereference MiniAs or inspect TargetOps. The Shared
SourceGraph/MCLayout worklist has exactly one declaration in
minias_source_replay_contract.h. The Common statement parser remains
single-owner for ordinary TEXT; Source alone recognizes label lexical
boundaries, while Common alone creates Symbols. Macro expansion and full
semantic replays keep one pipeline.
Do not reintroduce a second state machine, text parser or legacy callback.

## Validation and next step
Use compile-time negative boundary tests, GNU macro/conditional/replay
oracles and exact-head four-target/strict runtime CI; Linux Image/QEMU P1
is a separate milestone. P06 now splits Common by semantic ownership:
Section/Symbol context, typed source lowering, Fragment emission and shared
LEB serialization have distinct translation units (see
common-owner-split-2026-11-02.md). Source callbacks hold an opaque
invocation owner so expressions and numeric labels can query the one live
Common semantic state; this is not an independent MCContext or second parser.
