# Four-backend lowering seam and deferred Machine IR (2026-10-21)

## Decision

Establish a single Core-to-target driver seam now, **without manufacturing
Machine IR (MIR) before its first real consumer**. This change is intended to
make a later MIR addition local to the backend pipeline, not a fourth
reorganization of the frontend or target selection. The existing four emitters
remain authoritative for assembly, ABI, register choices and frame placement.

This refactor is based on four real targets and the AArch64/LoongArch64 original
BusyBox 581/581 GNU/QEMU acceptance. It does not by itself improve generated
code quality, add a register allocator or retest every workload.

## Ownership and data flow

```text
parse -> C semantics / TargetInfo -> verified Core CFG
                                       |
                                backend selection
                                       |
                            optional legacy C0 layout
                                       |
                    +------------------v-----------------+
                    | backend emit seam (backend.c)       |
                    | Today: Core -> direct GNU assembly   |
                    | Later: Core -> target legalization   |
                    |        -> Machine IR -> machine pass |
                    |        -> instruction emitter        |
                    +------------------+-----------------+
                                       |
                                   GNU AS/LD
```

1. **Core owns semantics:** typed SSA values, CFG, memory effects,
   volatile/atomic semantics, source-language resolutions. Machine register
   allocation and stack offsets must not be written into Core nodes.
2. **TargetInfo/DataLayout own target C semantics:** integer widths, record
   layout and compile-time target capability queries.
3. **Backend owns target lowering:** ABI, legalize/expand, register constraints,
   stack-frame planning, instruction selection and assembly or object emission.
   `MinicBackend.emit_program` is the current one-step implementation of this
   boundary. The compiler driver never switches on an ISA for codegen.
4. **Shared global emit** owns target-independent initializer/aggregate
   traversal and data layout checks. Relocation rendering remains a target
   callback. Shared narrow LP64 aggregate predicates are isolated from global
   emission, and **do not** imply a shared x86, RV or universal ABI.
5. **RV64 remains a first-class optimized owner:** its existing C0 layout,
   spill reuse, inline-asm callee-save logic and frame decisions remain intact.
   New targets are not forced to copy them or replace them with naive slots.

## Why no MIR implementation yet?

Machine IR needs a representation contract and pass consumers. Its natural
first consumers are a real physical-register allocator with virtual-register
liveness and spill/reload, post-selection copy elimination, or machine-level
instruction scheduling. Introducing an instruction container before defining
those requirements would merely mirror Core operations with more state and
create additional translation and validation burden.

MIR must be **target-oriented**, not a second target-neutral Core. A future
first version should support:
- function/basic-block structure and stable CFG successor edges;
- target-specific opcode and operand variants;
- virtual/physical registers with register class and implicit clobbers;
- frame indices before final stack offsets;
- explicit call ABI effects, inline asm, flags, and memory side effects;
- pass verification and valid/unsupported/error distinctions.

Introduce MIR **one target at a time**, retaining the direct path for others.
The stable interface from `compiler.c` stays
`minic_backend_emit_assembly(...)`; backend internals can switch to a
machine-function builder/optimizer/emitter without changing semantic lowering.

## Implementation scope in this PR

- Register four explicit backends in `target/backend.c`; fail closed on unknown
  arch, no accidental default to LoongArch64.
- Route driver layout and emission through `target/backend.h` rather than
  four hard-coded call sites. Keep the previous RV64 layout trace boundaries.
- Compile aggregate-global emission helpers exactly once in
  `target/common/global_emit.c`; expose typed callback contracts and move
  the shared LP64 predicates into `target/common/lp64_aggregate.c`.
- Preserve all target emitter implementations, source-owner selection and
  current GNU-QEMU validation paths unchanged.

## Validation and follow-up

1. Compile MiniC with `make build/debug/bin/minic-cc` or the repository's
   standard build target, checking full `-Wall -Wextra -Wconversion` warnings.
2. Run owner-scoped four-arch CI, then compare BusyBox 581/581 strict checks,
   original SQLite/Lua/TinyCC smoke and runtime byte-diff receipts.
3. Verify target rejection for unsupported/unknown target remains explicit.
4. Before actual MIR migration, measure load/store counts, stack-frame size,
   instruction count, runtime and compile time on representative real functions,
   then choose the first ISA with a tangible machine-level optimization case.
5. First MIR proof should preserve GNU/QEMU functional differences exactly and
   keep the previous direct emitter available until the new path is certified.

Do not introduce a catch-all target DSL, generic physical register allocator or
ABI-equivalence assumption as part of this seam change.

## Second pass: extracted observed two-ISA commonality

The two new emitters had identical scalar type admission and frame object
layout functions, including auxiliary binary128/int128 slots. These are now
independent, compiled-once strategies in `lp64_scalar.c` and `lp64_frame.c`.
They are **not** imposed on mature RV64 or x86. Backend-specific ABI register
assignment, calling sequences and machine instruction legalization remain in
the owning emitters. The ELF global emission pipeline and symbol handling are
also now compiled once, with a replaceable relocation callback. Future MIR
would replace the private machine path, not frontend Core semantics.

## Mature-target audit and isolation: RV64 and x86-64

RV64's existing GNU/global record, bitfield, relocation and file-asm
functions have moved intact from `codegen_function.c` to
`riscv64/global_emit.c`, preserving RV-specific directives, ABI,
layout and optimized stack-slot reuse. The driver and machine emitter
continue calling the same functions via explicit declarations.

x86-64's ~570-line target-owned global emission has moved to
`x86_64/global_emit.c`. In contrast to AArch64/LoongArch64,
its SysV-specific aggregate/weak/section handling remains independent.
The already proven `common/global_emit.c` zero-initializer relocation
walker and ELF symbol-visibility mappings are consumed with an x86
relocation callback; those helpers retain the same offset/size guards.

Four targets now separate binary data/relocation emission from Core
machine instruction lowering. Future target Machine IR should be
introduced in the latter boundary. No universal ABI or common
physical-register allocator is asserted, and RV64's optimized frame
planner remains untouched. Source movement is not a runtime proof:
original GNU/QEMU real-project owner regressions decide acceptance.

## x86 SysV ABI owns classification independently

The codegen-internal SysV scalar storage width, one/two INTEGER-eightbyte
record admission, and incoming GPR/SSE/stack argument cursor are now
compiled once in `x86_64/abi.c` with explicit `abi.h` contracts. The
full classifier code has been moved, not rewritten. Call emission and
instruction selection still use the x86 backend's machine registers;
RV64 keeps its already separate `riscv64/abi.c`.

This directly separates semantic ABI classification from target machine
emission without demanding an immediate Machine IR. No common LP64
classifier is assumed across RV64/x86/AArch64/LoongArch64. Runtime
certification still requires original project GNU differential gates.

## x86-64 frame-planning seam

`x86_64/frame.{h,c}` now owns the actual SysV AMD64 frame plan:
8-/16-byte Core SSA value slots, varargs register-save area and va_start
tags, object placements, >16-byte aligned RSP-relative object bases, and
overflow checks. `codegen.c` retains the x86 addressing sequences,
prologue/epilogue and physical register selection. ABI classifier
`x86_64/abi.c` remains a distinct module.

The refactor moves the **existing** planner, not a simplified generic
one; optimized RV64 remains independent. A later target Machine IR can
use these frame indices/plans before stack offsets are materialized.

Release compilation under `-Werror`, x86 real programs and existing
four-architecture GNU/QEMU checks are required before declaring
behavioral equivalence.

## RV64 machine-lowering responsibilities: frame requirements, not generic frame

The six production RV64 Core/psABI requirement queries are now compiled in
`riscv64/frame_requirements.{h,c}`. They decide whether to save the return
address, detect structured inline asm and dynamic stack allocation, handle
variadic parameter prefixes, and size the maximum outgoing argument area.
They consult the established `riscv64/abi.c`, not x86 SysV or the new-ISA
LP64 admission helpers.

The actual RV64 `core_frame_initialize`, value spill reuse, scalar object
lifetime/interval cache and inline asm register allocator remain in
`core_codegen.c`: extraction is *not* authorization to replace or slow down
that optimized machinery. These are stable inputs to eventual Machine IR
lowering, without modifying Core IR or register/stack semantics.

Both x86-64 (frame/abi/global) and RV64 (frame requirements/abi/global)
are now explicitly part of the multi-arch modularization work. The
original GNU/QEMU source-owner and Linux gates remain mandatory.

## Architecture-wide pass after extraction (2026-10-21)

Four backend files were reviewed together rather than seeking maximum
source deduplication. Current ownership boundaries:

| ISA | ABI admission/placement | Frame and SSA spill layout | Target emission |
| --- | --- | --- | --- |
| RV64 | `riscv64/abi.c`, `frame_requirements.c` | `riscv64/spill_layout.c` and advanced `core_codegen.c` intervals/object cache | `riscv64/core_codegen.c` |
| x86-64 | `x86_64/abi.c` SysV classifiers | `x86_64/frame.c` SysV varargs/alignment | `x86_64/codegen.c` |
| AArch64 | target-owned placement, narrow `common/lp64_aggregate.c` admission | optional bounded `common/lp64_frame.c` | `aarch64/codegen.c` |
| LoongArch64 | target-owned placement, narrow `common/lp64_aggregate.c` admission | optional bounded `common/lp64_frame.c` | `loongarch64/codegen.c` |

Global variables are orthogonal to Machine IR: RV64 and x86 own their
ABI/format-specific ELF global entry points; AArch64 and LoongArch64 share
the same verified simple global and relocation code; all use shared
DataLayout helpers only for proven equivalent operations.

IR/liveness seam: the former RV64 `core_visit_*` use enumeration is
now `core/value_uses.{h,c}`. Only the Core **semantic value-use walk**
is common. SSA spill allocation remains a separately optional
`riscv64/spill_layout.c` policy. Neither physical registers, stack offsets
nor a target ABI leak back into Core. Unsupported Core opcode visitation
continues to return failure. This makes the future Machine IR handoff
easier to implement without adding an unused instruction representation.

Next machine-level priority should be measured (load/store redundancy,
frame size, emitted machine instructions, compile and runtime cost) against
real BusyBox/SQLite functions before imposing a generic register allocator.
No LLVM-style instruction table or catch-all Backend vtable has been added.

Acceptance gate: native warning-as-error build, existing RV64 Linux and
x86 real-source owners, dual original AArch64/LoongArch64 581/581 BusyBox
GNU/QEMU byte-differential. A source-equivalent move alone does not pass.

## ABI correctness: AArch64 HFA/HVA is not a generic LP64 record

The old `backend_abi_record.h` patch in PR #528 is based on a removed
header and must not be merged verbatim. Its valid finding is retained:
AAPCS64 B.3 classifies HFAs/HVAs *before* the generic indirect aggregate
rule. A 4x 16-byte `long double` record is not a GP pointer argument.

The new `aarch64/abi.{h,c}` module wraps the already-shared bounded
indirect classification, rejecting aggregates containing floating, SIMD,
SVE or otherwise unknown element types until the actual AAPCS64 register
classifier and marshaling are implemented. Other targets keep their own
calling convention. This avoids polluting `common/lp64_aggregate.c` with
an architecture switch and preserves the existing LoongArch LP64D behavior.

A dedicated original-GNU AArch64 negative test ensures that the legal HFA
signature is rejected by MiniC *at function preflight* and no partial
assembly remains. Existing positive 144-byte integer record probes remain.

## RV64 object-liveness ownership: preserve the mature per-function optimizer

The remaining RV64 machine codegen file had a coherent ~980-line
object/record-liveness domain: immutable RECORD_LOAD call snapshot
storage reuse, CoreObject address lifetime intervals, optional reuse
indices, slot coloring, and a function-scoped cache. The complete
existing implementation now lives in `riscv64/object_liveness.{h,c}`.

A narrow target interface exports scalar-type admission, record-call
snapshot reuse qualification, object slot reuse layout, and the
previous optional cross-block focus flag setter. No lifetime rule,
data representation, or stack-offset policy was changed.

`riscv64/core_codegen.c` retains advanced frame assembly, structured
inline asm, register effects, machine operations, and all GNU emission.
`x86_64/frame.c` and the other ISAs' optional LP64 frame strategy are
unchanged. This is **target-owned reuse analysis**, not a universal
regalloc/ABI/Machine IR, and Core IR remains architecture independent.

Acceptance: verify full RV64 Linux original compilation and performance
receipts, x86 owner targets and AArch64/LoongArch64 original GNU/QEMU
regressions. A moved byte-identical algorithm is not a CI verdict.

## RV64 machine legalization: the next true lowering responsibility

After extraction of target ABI, Core semantic SSA uses, value spill
layout and object liveness, RV64 still interleaved scalar type widths,
Core memory/record operation support and checked calling convention
admission with the concrete GNU assembly emitter. A 690-line cluster
of 12 production legality queries now lives in
`riscv64/legalize.{h,c}` with the exact source bodies retained.

The module consumes the established RV64 psABI classifier and DataLayout
queries. All unknown or unsupported width/record/overflow/call shapes
still fail closed. `riscv64/core_codegen.c` retains the actual
per-op dispatch, inline-asm and register handling, mature frame
allocation, instruction emission, and ABI parameter construction.

This split prepares future target legalization -> optional machine IR
without inventing machine instructions yet. Cross-architecture review
confirmed that x86 SysV and AArch64 HFA conventions cannot be blindly
normalized into RV64 psABI; shared Core operand-use analysis remains
target-neutral and is deliberately not moved into this module.

**Validation boundary:** historical CI remains a baseline only; the
current PR must pass warning-as-error toolchain build, four-ISA
GNU/QEMU real-source regression, and RV64 Linux owners before merging.

## Cross-ISA program output: reuse lifecycle, keep ABI/instruction-private

Both AArch64 and LoongArch64 had *byte-identical* `fail` and
`machine_emit_reject` helpers and two near-identical TU-level output
drivers with the same mandatory no-partial-assembly, opt-in full TU
census and GNU global initializer loop. These are now compiled once
as `target/common/direct_program_emit.{c,h}`.

The explicit configuration contains only ISA identification, GNU-stack
note text, a function-emitter callback, and optional post-output
legalizer. AArch64 retains its verified large-SP memory text legalization;
LoongArch64 continues emitting its own large stack accesses immediately.
Both continue to own their entire ABI register placement, Core legality,
function codegen and assembler instructions. RV64's Linux-native
driver and x86 SysV driver are **not** redirected into this narrower
direct-LP64 driver, because their TU/ABI contracts are more elaborate.

This is output/diagnostic policy ownership, not Machine IR or a
universal target emitter. Original GNU-QEMU BusyBox full-owner
verdicts and negative AArch64 HFA diagnostics are authoritative.

## ISA-specific final lowering (preserve different machine mechanisms)

AArch64's two original GNU text passes (large SP displacement and
dynamic alloca rebase from SP to callee-saved x19) have moved, unchanged,
to `aarch64/asm_legalize.{h,c}`. The TU driver still calls the
AArch64-only post-emission legalization callback; no other backend does.

LoongArch64's original stack/large-offset address routines are now in
`loongarch64/frame_emit.{h,c}`. They still use reserved $t3 and s0,
and retain the local `emit_sp_mem` macro inside the function emitter:
only fixed frame references are based on s0 for alloca, while outgoing
ABI stack arguments remain SP-based.

These are deliberately target-owned low-level machine operations;
a future Machine IR must distinguish frame indices from explicit
outgoing stack positions. Do not merge ISA machine registers or invent
common LP64 opcode emission merely to deduplicate code.

## Whole-compiler audit: the largest coupling is now Core lowering, not four target ABIs

Re-check all repository source owners after the four-backend work: the
largest core file, `core_lower.c`, has ~16.7k source lines, with one
`lower_expression()` dispatch ~4.4k lines. None of these should be split
by arbitrary file length into target-specific ABI layers: it performs
AST/CFG and Core semantic lowering, and already consumes selected
`TargetInfo` for C DataLayout.

One real independent stateful island has now been moved intact into
`core/reentry_index.{h,c}`: construction/cleanup of the scoped incoming
goto/asm-goto/label-address index, indexed discarded-branch membership
query, and internal-loop label query. The two downstream Core decision
sites now use narrow functions, rather than directly touching the cache
struct or counters. OOM/invalid AST retains the previous conservative
fallback scans. It is a *process-global scoped index* and is explicitly
not a concurrent compilation capability; do not infer thread-safety.

The read-only Core SSA use visitor remains `core/value_uses.c`; full AST
lowering stays separate from RV64 `legalize.c` and all machine-level
frame/ABI modules. Gate with reentry/goto/switch/inline-asm regression
and Linux original owners, not just a successful rebuild.

## Compiler diagnostics: source inventory is not machine legalization

The opt-in `MINIC_CORE_USAGE` implementation was previously a ~200-line
static-function header. The two new-ISA emitters consumed it via their TU
driver; retaining such header functions for more targets would duplicate
compiled code and blur the diagnostic / target code boundary.
Move the three original implementations to separately compiled
`target/backend_core_usage.c`, retaining a declaration-only header.
Counters, data-width bins and `support_verified=0` remain unchanged.

The Core usage census reports observed workload operations; it does not
certify instruction legalization or ABI correctness. Four-ISA acceptance
requires GNU/QEMU real workloads and negative ABI guards.

## Global audit: AST specialization, Core CFG, and target Machine IR are different stages

The prior sequence moved RV64 ABI / liveness / legality and the two
LP64 direct program drivers into named compilation units. Reviewing the
full source tree revealed a *larger misplaced responsibility*: the
`src/compiler/compiler.c` stage driver still contained >2,700 lines of
AST inline specialization and lowered-Core reachability logic. These are
neither target instruction emission nor frontend parser code.

They are now separated without changing the pipeline order:

- `compiler/inline_specialize.{c,h}` keeps the established three AST
  specialization families (integer, symbolic, boolean-domain), its
  per-TU fact cache reset, and source reference finalization.
- `compiler/core_reachability.{c,h}` keeps the original CFG/block walk,
  source-label and indirect-branch roots, non-Core relocation/function
  alias roots, allocation-failure fallback, and pruning of dead clones.
- `compiler/core_function_set.h` expresses the real translation-unit
  Core function/status lifetime contract consumed by both driver and
  reachability pass. No machine register/frame data enters this type.
- `compiler/compiler.c` remains the actual parse/normalize/verify,
  optimize, Core-lower, prune, target-emit **stage coordinator**.

This audit also confirms the 800-kB `core/core_lower.c` and large
target `codegen.c` files are separate future cleanup concerns;
do not prematurely merge AST optimization, Core legality, and target
legalization into a single invented Machine IR abstraction. Every
new lowering layer must retain existing LLVM/GNU-QEMU oracle behavior.

All complete specialization and CFG algorithms are source-preserved,
with only required external linkage changes for six top-level stage
operations. Run four-ISA GNU/QEMU and RV64 Linux owners before claiming
the new translation-unit split is verified.

## Re-audit of AST specialization families: two private dependencies only

After moving 2,345 lines from the stage driver to the AST
specialization module, a **second complete review** found four
semantically separate families. No machine target knows about them.

- `compiler/inline_integer.c`: checked integer parameter clones.
- `compiler/inline_symbolic.c`: symbolic expression and parameter clones.
- `compiler/inline_boolean.c`: local boolean fact cache and range clones.
- `compiler/inline_reference.c`: final AST/non-Core reference preservation.

The only cross-family implementation dependencies are two validated
AST clone constructors, now explicitly declared in
`inline_specialize_internal.h` (integer used by symbolic, symbolic
used by boolean). These are **not** placed into `core/core_ir.h` or
any backend function table. The compiler driver continues to run
integer -> symbolic -> boolean -> final-reference stages in its
original order and resets the per-TU boolean cache at both boundaries.

The 33 original private/entry functions were checked unchanged in
the first extraction. This second grouping preserves the same function
bodies with exactly two required private linkage changes. RV64
Linux/native optimized paths and the x86/AArch64/LoongArch64 backends
remain byte-for-byte untouched by the AST organization.

## Global compiler/target audit: own the Core-set transaction separately

The four-ISA integration previously concentrated on backend modules.
Reviewing the *entire* compiler pipeline after AST specialization and
Core CFG reentry extraction showed that `compiler.c` still owned the
function-set allocation/lowering transaction and the global incoming-edge
index lifetime. Its `minic_core_function_set_initialize/destroy` and
`minic_prepare_core_function_set` production functions now live in
`compiler/core_function_set.{h,c}`, and the driver calls the same
functions through explicit compiler-internal signatures.

The module preserves one `reentry_index_begin/end` scope around all
original/clone function lowerings and keeps per-function bootstrap
diagnostics opt-in. The Core status validation and user-facing diagnostic
policy deliberately remain in `compiler.c`, at the pipeline boundary.
This prevents architecture-specific code or future Machine IR work from
owning AST/CFG lifetime. The main driver remains orchestration only.

Remaining large source risks identified in the audit:
`core/core_lower.c` (about 800 KB) is an AST semantics/CFG state machine,
not a single extractable pass. Its local fact propagation, expression
lowering, statement/control-flow lowering and cleanup edges should be
isolated by *dependency domain*, with source-specific tests for computed
goto, switch labels and inline asm. Blind line-range chopping would risk
violating the per-TU reentry index and specialization invariants.
Four target backends, the shared ELF/LP64 services and existing GNU/QEMU
owner gates remain unchanged in this source-ownership refactor.

## Source AST loop shape recognition is not machine lowering

After reviewing the recently split compiler specialization, Core CFG
reentry ownership and all four target backends, the remaining ~16.6k-line
`core/core_lower.c` still owned parser-provenance checks for normalized
`do {} while (0)`, constant-true `do/while`, and synthetic `for`
continue/update labels. These are front-end/source CFG semantics, not
target-specific instructions or ABI policy.

The coherent ~320-line AST-only group now lives in
`core/source_loop_shapes.{h,c}`, with seven explicitly declared source
shape queries. Both the source-body predicates and all call sites are
mechanically preserved; the private position-equality helper stays local.
The shape tests include strict source-position, label identity, cleanup
context, and failure-on-unrecognized-form guards. No AST, Core IR or
function CFG representation changes.

Important exclusion: Core branch emission, block predecessor accounting,
the scoped reentry index, and machine target legalization remain with
their existing owners. This is a *semantic boundary*, not a generic
loop optimization pass or an excuse to infer unproven goto reachability.

Verification needs compiler -Werror build, normalized-for/continue,
do-while/zero/true/return, CFG reentry and all four original GNU/QEMU
owner regressions; source identity alone is not runtime certification.

## Global AST/Core check: local fact state is not machine lowering

The global review shows that previous optimization extraction improved the
machine backends (RV64 ABI, legality, advanced object lifetimes, x86 SysV
frame and two new-ISA native accessors), while the Core source lowerer
still owns more than 16k lines of expression, CFG and local fact code.
The next real seam is ABOVE target-specific machine lowering.

A source-audited 10-function local integer and null-pointer fact state
family now compiles once in `core/local_facts.{h,c}`. Existing code
for source specialization diagnostics, array-backed facts, snapshots,
restore, escape barriers, clear-to-known-specialization, null pointer
checks and scalar reads was moved without changing conditions.

Critically, `core_local_constant_set` and source-wide stable-zero proof
remain in `core_lower.c`, where they must consult the existing
source-level external goto/asm label reentry oracle. Do not relocate
source-wide CFG invariants into an isolated fact hash or an ISA backend,
and never treat unknown/volatile/escaped values as unconditional facts.

This is a semantic Core pass interface, not speculative Machine IR.
Revalidate compiler warnings, original RV64 Linux, x86 SysV projects,
AArch64/LoongArch64 original BusyBox GNU/QEMU and negative HFA ABI gate.

## Global audit follow-up: constant-call CFG oracle

The dominant remaining module after the four-backend and compiler pass
splits is AST-to-Core lowering. This commit moves the **existing bounded**
constant-return callee and small-loop CFG evaluator from the giant
`core_lower.c` into `core/cfg_constant_calls.{c,h}`. The fact-dependent
Core helpers remain in `core_lower.c`, with three narrow private
prototypes instead of reimplementing any source-wide proof. The algorithm,
cleanup-edge conditions, iteration guards and fallback rejection paths
are unchanged. There is no Machine IR, instruction selection, ABI
change or speculative common lowering policy.

Accept only after warning-as-error toolchain compilation and all original
RV64, x86-64, AArch64, LoongArch64 regression evidence. This is a
responsibility separation, not a performance claim.
