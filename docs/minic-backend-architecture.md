# MiniC five-target architecture (2026-10)

This document describes the **implemented** MiniC interfaces on
the current `main` branch (x86-64, RV32I, RV64, AArch64, LoongArch64). It is not
a promise of a universal Machine IR, LLVM-style allocator, or a rewrite of
MiniAS/MiniLD/MiniPP. The current milestone ends when ownership and regression
behavior are stable; performance improvements can follow independently.

## End-to-end ownership

```text
C source
  → Frontend (parser, C types, AST, initializers)
  → Core IR (typed values, objects, blocks, memory/flow semantics)
  → target admission and function planning
  → target lowering / instruction-family selection
  → target machine emission (assembler text)
  → external assembler, linker, real ELF runtime
```

The Core instruction kind and type/DataLayout invariants are **semantic**:
they should not be implemented independently in every ISA. Physical registers,
stack calling conventions, varargs layouts, relocation syntax, instruction
encoding and large-immediate legalization remain **target owned**.

Target selection is controlled through the existing MiniC TargetInfo/compiler
driver. No target-specific behavior belongs in MiniAS, MiniLD, or MiniPP merely
because MiniC needs it.

## Actual per-architecture shape

| Concern | x86-64 | RV32I | RV64 | AArch64 / LoongArch64 |
| --- | --- | --- | --- | --- |
| Admission | Core and local emitter checks | `rv32_emit.c` and ILP32 planner | `core_admission.c` | common direct LP64 preflight + family shape |
| ABI | `call_plan.c` SysV | `rv32_abi_plan.c` ILP32 | `function_abi_plan.c` LP64/LP64D | `direct_call_plan.c` AAPCS64 / LP64D |
| Frame lifetime | `frame.c` | `rv32_frame.c` | `frame_layout.c`, object intervals | `lp64_frame.c`, `direct_function_plan.c` |
| Scalar machine | `machine_emit.c`, `machine_fp.c` | `rv32_integer_emit.c`, `rv32_fp_emit.c` | `scalar_emit.c`, `fp_emit.c` | native `integer_emit.c`, `fp_emit.c`, `memory_emit.c`, `residual_emit.c` |
| CFG / terminator | `machine_emit.c` | `rv32_emit.c` | `core_codegen.c` | `block_emit.c` |

The two direct LP64 implementations **share the checked function preparation
algorithm** (`direct_function_plan.c`), an instruction-family classifier
(`direct_instruction_semantics.c`), and checked residual operand facts
(`direct_residual_plan.c`). They do **not** share an ABI register bank or
machine instruction emitter. RV64 has its own ABI, object-lifetime model and
inline-assembly behavior, so do not force it into an AAPCS64/LP64D-only API.

### Unified SSA spill-slot geometry (this refactor)

All five targets retain the same `MinicCoreValueId` semantic identities. Their
machine frames now use two **Common-owned, policy-qualified** SSA placement
engines, while targets still own the stack anchor, frame direction, save areas,
varargs, strict ISA emission and calling convention:

- `common/core_value_slots.{h,c}`: a shared, overflow-checked function-value
  slot planner used by SysV x86-64 and both Direct LP64 emitters. SysV's
  `LINEAR` policy preserves cumulative 8-byte/16-byte **end offsets** below
  `rbp`. AAPCS64/LoongArch64's `WIDE_TAIL` policy preserves the fixed
  8-byte-per-value Core prefix and a separate 16-byte **start-offset** sidecar
  for int128 and long-double values. The ABI physical address interpretation
  never enters Common.
- `common/core_spill_layout.{h,c}`: RV32I and RV64 consume one Core function-wide spill pass (single visit per block), replacing the obsolete sequential peak/assignment sweeps. Both retain the already-proven
  block-local last-use reuse strategy, with 4-byte and 8-byte units respectively.
  This separate algorithm is a machine-frame optimization, not a duplicated
  Core type checker.
- `common/core_frame_layout.h`: a checked align/reserve primitive shared by
  all stack planners. The Direct LP64 wide-tail alignment also uses it instead
  of an extra hard-coded integer round-up.

The frame bounds and generated per-value offsets are unchanged for existing
backends. The new `probe-core-value-slots.c` checks scalar/wide layout,
non-wide sentinel behavior, empty functions and transactional overflow failure.
Runtime, psABI and strict RV32I/RV64I checks remain separate gates.

### Canonical Core semantic families and full PR-head x86 certification

A single exhaustive Core opcode-to-family table now supplies the semantic
classification of all 68 operations to x86-64, RV32, RV64 and the Common
Direct LP64 planner (AArch64/LoongArch64). It replaces target-local opcode
category copies; Core verification stays solely in Common FunctionPlan.
Machine support, ISA feature selection, register assignment, atomics,
variadic ABI and ELF emitter policy remain exclusively target-owned.

An exhaustive -Wswitch-enum -Werror compile and native probe reject
uncategorized opcodes. The declaration-only traversal and the prepared
definition traversal now use one visibility/reachability predicate.

Tagging the PR [minic-full] runs fresh full x86 original BusyBox 593/593
and six independent real-software acceptance jobs. This does not qualify
RV32 original same-Image QEMU P1, RV64 P1 or fixed-point Stage3/Stage4.
### Exact-head acceptance infrastructure

The full x86 original source and seven-shard original RV32 kernel CI is
PR-head gated by [minic-full], and checks out the real commit rather than
GitHub's synthetic merge commit. RV32 producers select Ubuntu's signed public
archive to avoid slow Azure apt mirrors. Four-backend paired A/B is also
PR-head gated and compares frozen original tinyAES/cJSON sources against the
pre-refactor compiler; the runner verifies the candidate checkout SHA. Only
completed runs prove acceptance.

### Final common-machine-visit and ABI boundary audit

Five target machine emitters now consume exactly one Common Core block cursor
(`common/core_block_walk.h`) for lexical block ordering, target-chosen
reachability filters and bounds-checked immutable instruction views. The
machine label, prologue, terminator, register, ISA and psABI handling remains
physically target-owned: it is not correct to share x86 SysV and AAPCS64 frame
arithmetic or LoongArch and RV32 instructions. This also replaces five
separately maintained block and instruction address access patterns.

There are **two necessary levels of planning**, not three independent Common
pipelines: `core_function_plan` is the once-per-definition five-target owner
of verified Core/facts/calls/objects, while `direct_function_plan` is an
AAPCS64/LoongArch LP64D-only **downstream physical ABI** planner. Its
preflight/record/varargs/CALL placement are *not* authoritative for x86/RV32
or RV64. Adding a new Core language shape must start with Common facts and
verification; new machine policy goes to a target or an explicitly shared
ABI family, never to a second target-local Core verifier.

The common visitor additionally snapshots va_start result slots for x86,
removing its production full-instruction rescan (standalone frame APIs retain
their checked compatibility fallback). The five-target output transaction
creates an exclusive temporary assembly file **in the output directory**,
verifies close and target legalization, then atomically renames it into place.
On any emission, legalization or rename failure the existing destination
remains unchanged, not merely removed. The standalone transaction probe
checks preservation at all five failure stages, including finalization.

RISC-V common function-symbol declarations, visibility and ELF directives use
`MinicRiscvFunctionSymbol` and `minic_riscv_*symbol*` names. No RV32 code
references a misleading RV64 symbol-class name. This renaming is symbol-policy
only; RV64 psABI/strict ISA and RV32 ILP32 remain unmodified.

### Retiring the per-object scan fallback

The RV32/RV64 frame planner now invokes the Common interval index for every
nonempty Core function, not only those above the old 8-object/100-instruction
cutoff. The old repeated per-object and whole-function address-use fallback
is removed. If optional index allocation fails, objects retain private stack
storage; stack-slot reuse is never guessed and compilation can continue.
The existing first-real-access-is-a-write check remains mandatory before
cross-block sharing.

The common RV32/RV64 SSA spill planner also caches each value's immutable
DataLayout slot size across visits, while keeping the established last-use
release policy, 4B/8B units, stack bounds and target frame semantics.

### Single-pass Direct LP64 CALL admission and ABI planning

AAPCS64 and LoongArch64 formerly traversed the same verified Common CALL
index twice: once for return/argument preflight, again for ABI placement,
re-resolving every Core CALL argument type. This refactor retires the separate
`direct_call_preflight.{c,h}` owner and the redundant per-CALL adapter
`minic_lp64_plan_direct_call`. The remaining Direct LP64 call planner now
validates each semantic CALL and immediately classifies the same resolved
argument-type vector under the chosen target psABI. Machine emission is
unchanged. Its checked output preserves the exact rejected Core instruction
and the old `call_preflight` versus `call_planning` failure stages. Standalone
callers still build a checked Common CallIndex and production borrows its
single prepared index. No second CALL index traversal is performed.

### Computations done once

- SysV incoming arguments and outgoing calls receive a prepared set of
  native locations. Machine emission consumes these locations; variadic
  frame setup uses the same incoming cursor.
- RV64 Core admission runs before function emission, rather than again for
  each emitted instruction. Its named ABI placements move into the frame
  without reclassifying all parameters.
- RV64 object address-use indexes, conservative intervals and stack-slot
  coloring are owned by the frame; no process-global per-function cache or
  cross-block policy switch survives.
- AArch64 and LoongArch64 share instruction-family/result-slot checks and
  integer-cast widths/signs/128-bit Core stack-slot facts; native machine
  lowering consumes those facts.
- Each direct LP64 ISA has one target-owned `scalar_normalize.c` primitive
  reused directly by FP, integer, ABI and residual machine families. No
  block-emitter callback or forwarding parameter remains across modules
  inside the same ISA. This is independent of the Core type-width classifier.

### Failure and lifetime invariants

- A rejected Core instruction must fail closed with the diagnostic context,
  never silently emit an arbitrary register value.
- Every dynamically allocated per-function plan has one cleanup owner.
  Transfer of named RV64 ABI locations to the frame is an explicit move.
- An effect-only Core instruction is not required to produce a scalar SSA slot.
  Wide 128-bit values require their dedicated 16-byte frame placement.
- The raw stack pointer and the stable frame anchor differ under dynamic
  alloca. AArch64 and LoongArch64 preserve their own concrete lowering.
- Never merge unrelated ISA-specific register, varargs, stack, GOT and ELF
  relocation policies under the name of a generic ABI.
- A refactor is not done until its old duplicate calculation/ownership path
  is removed; moving the same function to a smaller file does not qualify.

## Adding further architectures after RV32

RV32 is already the fifth implemented target. The following is a review
checklist for possible future targets or narrower 8/16/32-bit ABI profiles,
not a requirement to implement Machine IR:

1. Implement TargetInfo/DataLayout and prove integer widths, endianness,
   aggregates, alignment, and record padding against an external toolchain.
2. Implement the target call/return ABI classifier and named/outgoing argument
   planners. Reuse Core call views/semantic facts, not another ISA's call
   register assignment.
3. Build a function-owned frame and stack-state plan, plus admission for the
   exact set of Core instructions the new machine emitter supports.
4. Dispatch verified Core instruction families to native emitters. Leave
   machine immediates, scratch registers, signed extension, and relocation
   syntax to the new target.
5. Keep the terminator path and function prologue/epilogue target owned.
   Where large-offset legalization is needed, make it explicit and tested.
6. Compare original third-party C source builds against GNU/Clang output,
   then link and run on hardware or QEMU. Only expand the frontier after the
   existing validated cases continue to pass.

If a future Machine IR demonstrably avoids meaningful duplication across all
four existing emitters, it can be added between function planning and target
instruction emission. The current code deliberately exposes that seam without
enforcing a premature, universal physical register/selection model.

## Regression contract and ownership checks

Run `bash tests/compiler/c0/check-minic-target-boundaries.sh` after changing
machine family ownership. It rejects regressions such as embedding the LP64
residual switch back into both block emitters, resurrecting per-function static
RV64 object caches, or reintroducing a native normalize callback between files
of the same target.

The apt-free `MiniC Native Strict Refactor Gate` executes that checker, then
builds with `-Werror` and exercises native x86 ABI/runtime behavior. The
focused regression suite separately exercises original source programs,
cross-toolchain linking and the RV64/AArch64/LoongArch64 GNU/QEMU oracles.
A green native gate does **not** imply the full cross-target suite passed.

Scope: **MiniC only**. A failing test in MiniAS/MiniLD may still be informative,
but the backend-architecture refactor does not modify those toolchains.

## GNU inline assembly and ABI cursor extension contract (2026-10-23)

The driver still selects one of four `MinicBackend` implementations; the
Core-to-Backend machine emission seam is unchanged. New GNU multi-operand
assembly forms now qualify through the selected backend's optional
`qualify_inline_asm` callback. x86 owns GNU register constraints, instruction
whitelists and template qualification in `x86_64/asm_contract.c`; the Core
`core_lower_asm_target.c` only turns target-approved operand roles into typed
SSA values/addresses and a structured side-effect instruction. The x86
`machine_asm.c` continues to own physical registers, SysV preservation,
clobbers and assembly output. Both qualification and emission use the same
x86 template predicates, preventing the two rule sets from drifting.
Other ISAs retain their existing ordered Core GNU asm probes until a proven
new contract is supplied. Unsupported forms must not mutate Core IR.

`va_copy` follows `TargetInfo.variadic_copy_kind`: scalar cursor assignment,
ABI state copy, or explicit unsupported. The Core ABI-state sequence allocates
independent cursor storage, copies its semantic state, then writes back the
cursor address. The target checks supported cursor types; x86 still owns its
24-byte SysV tag copy and frame layout. The transitional scalar policy of the
other three ISAs is preserved, not upgraded to full structured ABI support.

The mandatory backend-boundary gate now rejects restoration of x86 Core
architecture dispatch and ensures that template classification stays owned
by the ISA. Extend the architecture's existing qualifier/emitter rather than
adding another `core_asm_try_<ISA>` branch for each new Linux inline asm.

## Target language admission ownership (five-backend pass)

TargetInfo now owns the declaration-time type admission decisions previously
duplicated by architecture allowlists in `parser_type.c` and `ast_verifier.c`.
The frontend uses the same query for extended floating point identities, and
TargetInfo owns header-only opaque vector names plus the builtin va_list cursor
identity. RV32 continues to reject the extended floating declarations; x86-64
retains its ABI-state cursor, and AArch64 alone recognizes its opaque NEON/SVE
header spellings. No ABI marshaling, storage layout or arithmetic support is
implied by accepting a declaration. The existing emission and Core lowering
paths are intentionally unchanged.

The boundary gate rejects new `MINIC_TARGET_ARCH_` allowlists in the frontend.
The native strict gate additionally runs the five-target descriptor contract.
This is the first semantic-ownership pass; the RV32/RV64 machine legalization
and product-driver predefined-macro unification remain separate follow-ups.

## RISC-V family compiled-once word-operation legalization

The RV32 two-word long-long and RV64 two-word int128 emitters now share
`riscv/word_operations.c` for Base-I add/subtract carry/borrow, bitwise
binary/unary instructions, and one-word CLZ/CTZ counting loops. Register
preconditions are explicit (t0/t1 values, t2/t3 second argument, t4/t5
scratch), and the callers still own frame loads/stores, ABI, signedness,
integer normalization and wide half selection. The shared bit-scan helper
accepts XLEN and checked bit width; it does not reinterpret a 64-bit RV32
value as one 64-bit register. This removes two machine-algorithm duplicates
without reworking the working Linux/Linux-ABI code paths.

The native focused probe emits RV32I 64-bit and RV64I 128-bit source C
and checks the actual assembler text. It complements, but does not replace,
the existing real GNU/QEMU and Linux gates.

## Second global read and wide-shift ownership

At the refactor head, the audit again fetched all 302 MiniC production
C/header files (compiler, frontend, Core, five targets, two MiniC tools and
public entrypoint) before selecting the next deletion. This exposed three
ownership constraints: driver predefines intentionally remain a **RV64-only
driver pipeline**, canonical default RV64 layouts are confined to RV64
consumers or legacy default API wrappers, and existing multiple target
runtime/original-program receipts must be preserved. Do not claim the
user-facing `minic` product driver supports every `minic-cc` backend.

The source-level RV32 and RV64 two-limb shifts had independently reimplemented
the same Base-I word-shift algorithm. After both full source routes were
reviewed, the `riscv/word_operations.c` family implementation now handles
XLEN-dependent 32/64/128 boundaries, preserving sign-fill and non-M machine
instructions. RV32 still independently classifies shift-count storage and
passes the high word when its count is 64-bit; RV64 continues to reject
unqualified wide shift-count types. The focused compile probe tests the
low/high-count labels on both targets; QEMU matrices remain authoritative
for dynamic execution.

## Target spelling and product pipeline registration

Target architecture identities and canonical CLI spellings are now registered
once in `target_catalog.c`. Both the standalone `minic` driver and the
full `minic-cc` use this catalog; TargetInfo continues to own **all** C
semantic widths, data layout, ABI and per-invocation RISC-V ISA facts.

`minic --target=<one-of-five> -S preprocessed.i -o out.s` delegates
directly to the five-backend compiler, preserving the exact machine result
and function-section flags. This does **not** claim the other targets have
MiniPP/MiniAS/MiniLD linking pipelines; `-c` and link mode remain RV64
staged-toolchain-only and fail closed for all four other targets. Option
validation for RV32I/ILP32 uses the unchanged compiler ISA resolver; RV64
legacy profiles continue to be qualified by the established driver policy.
The shared catalog deliberately does not introduce shadow type sizes.

## Per-invocation target options: language ABI is not ISA

A fresh `MinicTargetInvocation` copies the immutable target descriptor and
its integer model for each translation unit. The `-fsigned-char`,
`-funsigned-char`, `-fno-signed-char`, `-fno-unsigned-char` switches
modify the invocation's plain-char signedness, which affects source semantics,
literal arithmetic, integer promotion and the generated machine scalar
normalization. It must never mutate global TargetInfo data. `-fshort-wchar`
changes the wchar representation to unsigned 16-bit and
`-fno-short-wchar` restores that **target's actual default**, not an
assumed target-independent type (RV32/RV64 Linux defaults are now signed 32-bit wchar_t).
The standalone `minic` driver forwards these switches to `minic-cc`
even through its qualified RV64 CPP/AS/LD pipeline; RV64 CPP also tracks
the effective plain-char `__CHAR_UNSIGNED__` predefine. PIC switches are
forwarded on the supported preprocessed-C `-S` path only.

GCC/Clang option ownership guidance:

| Axis | Representative options | Effect and MiniC status |
| --- | --- | --- |
| Target C data model | target triple, ILP32/LP64, endian | ABI object sizes, alignment, pointer and long widths, predefined macros; five existing little-endian targets only |
| ISA availability | `-march` / RISC-V extensions | Native emission and required soft libcalls; exact RV32I and RV64 profiles with final ISA audit |
| Calling convention | `-mabi`, float ABI | Register/stack argument and return placement; never infer an ABI solely from an ISA name |
| CPU tuning | `-mcpu`, `-mtune` | Scheduling/cost model, should **not** alter ABI; not implemented, reject |
| Addressing | `-mcmodel`, `-fpic/-fPIC/-fpie/-fPIE`, `-mrelax` | Relocations, code model, GOT/PLT, ELF load model; RV64/x86/AArch64/LoongArch64 have target-owned PIC address lowering; RV32 and unrelated code-model options remain fail-closed. AArch/Loong complete GNU PIE linking/QEMU validation remains a separate acceptance gate |
| C language ABI | `-fsigned-char`, `-funsigned-char`, `-fshort-wchar`, `-fshort-enums` | Char sign and promoted values, wchar size, enum layout; first two option families supported, short-enums not qualified |
| C semantic/optimization assumptions | `-fwrapv`, `-fstrict-aliasing`, `-ffast-math`, `-O*` | Observable overflow, aliasing, FP and optimization behavior; no false compatibility: remain unimplemented |
| Runtime/object contract | `-ffreestanding`, `-fno-builtin`, `-fcommon`, `-fvisibility`, TLS, stack protector, unwind flags | Header/runtime expectations, linkage, dynamic TLS and exception/security sequences; supported cases stay narrowly scoped |

A frontend type being accepted never implies arithmetic lowering or ABI
qualification. The driver and the compiler reject options that could silently
change code or ABI until their owners and validation are implemented.
For the GCC taxonomy see GCC RISC-V Options and Code Generation Options;
for LLVM see Clang Command Line Argument Reference.

## RISC-V ISA/ABI explicit profile catalog

Both the driver and compiler read one bounded ISA profile catalog:
`rv32i`, `rv64i`, `rv64im`, `rv64imafd`, plus
`rv64ima_zicsr_zifencei` and `rv64imac_zicsr_zifencei`. These latter
two profiles admit source A/CSR/fence.i (and C) through `-S`, while
compiler-owned assembly remains **Base-I**. They are deliberately rejected
for the built-in `-c`/link chain until MiniAS has independently verified
the extra encodings and ELF attributes.

A profile distinguishes target-available `features` from the smaller
compiler-selected `codegen_features`. The target may support M while MiniC
still uses the original Base-I software-runtime implementation. ISA and ABI
are independently checked: `rv64imafd/lp64` selects the qualified LP64
soft-float path, while `rv64imafd/lp64d` retains hard-float emission.
Unknown extension spellings stay fail-closed; no broad ISA grammar or
Machine IR is introduced.

The historical implicit RV64 target still uses its separately qualified
`rv64gc/lp64d` assembler path. Replacing it with an explicit profile is
**not** part of this refactor, because the default ELF acceptance contract
and the strict instruction audit have different scopes.

## Source GNU ASM versus compiler-selected ISA: stable RV32/RV64 owner boundary

A Linux kernel build may set a superset `-march` for the actual assembler
because **source-owned** inline or file-scope ASM uses A/CSR/M/privileged/
vendor extensions. MiniC's plain-C generator is still entitled to emit only
its qualified Base-I path. The two demands must not be conflated:

- **MiniC:** C semantics, target XLEN/data model, ABI, operand/clobber
  substitution for supported inline asm shapes, and Base-I machine output.
  The current qualified RV64 profiles can already optionally select native M
  for certain operations, but it is an optimization/legalization decision
  with the Base-I or runtime-call fallback retained.
- **GNU AS (or later MiniAS):** parse instructions/directives from upstream
  assembly and source asm, validate the assembler's full `-march` and
  assembler-scoped options, encode and relocate instructions. MiniC should
  not implement all extensions merely to preserve that source assembly.
- **Driver/linker:** forward or resolve the correct ABI, output format and
  assembler/linker flags. A correct `-mabi` affects C calling and object
  layout; an assembler feature chosen solely to admit upstream asm need not
  enable a codegen optimization inside MiniC.

The former opt-in `MINIC_RV64I_ASM_AUDIT=1` diagnostic path now runs
by default. The existing inline/file-asm emission writes nonexecuting
source-origin comments around only user assembly templates. Strict RV64I
emission checks compiler-owned instructions while leaving source asm and
its directives to GNU AS; external ELF ISA/ABI checks remain separate. This
removes false Core rejection for code whose assembler needs more extensions
than MiniC's own Base-I producer. It does not claim unrestricted GCC inline
asm constraints: unsupported operand forms still reject at the frontend/
target transport boundary.

## Unified RV32/RV64 optional-M scalar selection

After reviewing the whole compiler and the two emitted ISA paths, the
RISC-V feature/ABI state is declared in `riscv/isa_features.h` (not
owned by a 64-bit machine emitter). The shared
`minic_riscv_plan_integer_arithmetic` is a **small decision point**, not
a new MIR: it accepts Core multiply/divide/remainder, XLEN and machine
capability and chooses either `NATIVE_M` or `RUNTIME_LIBCALL` with an
actual opcode/symbol. Both the RV32 integer emitter and the RV64 arithmetic
helper now consume this one plan and retain their own register/stack/ABI
transport, canonicalization and RA-liveness responsibilities.

Explicit RV32I/ILP32 always uses base-I plus external standard runtime
helpers, and RV64I/LP64 does the same. The qualified RV64IM native M path
and historical RV64 default remain unchanged. C alone does not select
any arithmetic replacement; a synthetic RV32M feature is exercised only
in a **planner unit test**, not exposed as a newly supported `-march`.
Unrecognized extension combinations stay fail-closed while future M/C/Z
optimizations can plug into this machine-level selection without changing
C language acceptance or inline/file GNU assembler provenance.

## Optional extension monotonicity: RV64I/IM wide comparisons

The incremental machine capability model must be monotonic: exposing
`M` can change the implementation of a C multiply, but must **not**
remove C comparisons previously legal in pure I. Strict RV64I/LP64 and
RV64IM/LP64 now use the same `__cmpti2`/`__ucmpti2` compiler-runtime
fallback for two-word `__int128` less/equality operations. The saved-RA
frame planner accounts for the call regardless of the M feature bit.
This does not broaden inline/source GNU assembler policy, nor claim
that the compiler emits a complete M implementation or supports all
possible RISC-V extension spellings.

## Two-target Direct LP64 machine-dispatch single owner

AArch64 and LoongArch64 no longer carry byte-for-byte copies of the Core
block loop and machine-family switch. `target/common/direct_machine_dispatch.h`
owns **only** lexical Core traversal, source-label ordering, common
instruction-shape admission, compiler-barrier elision, dispatch by semantic
machine family and whether a completed instruction needs a GP writeback.
Its immutable `MinicDirectLP64BlockContext` is borrowed for each callback.

The two targets provide native family callbacks (record, incoming parameter,
load/store, FP, integer, call ABI and residual), ISA-specific GP result
register/stack transfer and terminators. Thus AAPCS64 and LP64D have one
**control-flow dispatcher**, not a fictitious universal calling convention
or opcode emitter. A new target can enhance this adapter with hooks rather
than copying the old 150-line machine-family switch.

The standalone `probe-direct-lp64-dispatch.c` exercises both successful
family/writeback traversal and failure on native emit, GP writeback,
terminator and Core shape, preserving exact block and instruction diagnostics.
Strict compilation, native four-target acceptance, x86 upstream BusyBox,
RV32 upstream software, RV64I Linux frozen shards and paired A/B are the
separate exact-head correctness/performance gates.

### One-pass Common semantic facts and va_start indexing

The Common FunctionPlan formerly analyzed Core instructions once for facts,
then scanned again for variadic sites. Prepared emitters now call
`minic_core_function_facts_analyze_collect`, which builds the compact
va_start result list as part of the **same** instruction traversal. The
visitor owns any lazily grown result array and destroys it with the function
plan. Standalone `minic_core_function_facts_analyze` remains allocation-free;
no target-owned Core rescan or ABI-specific site assumptions are introduced.
The standalone probe covers multi-site order, calls and ownership.


## Direct LP64 function emission: single orchestration owner

`target/common/direct_function_emit.h` now owns the complete function
lifecycle, downstream ABI planning, ELF section/symbol/visibility directives,
constructor registration, construction of the immutable Core block context,
staged dynamic-stack rewrite and all cleanup. AArch64 and LoongArch64 expose
small native entry points for the prologue, instruction blocks and optional
target-only rewrite. All actual register saves, native instruction sequences,
ELF stack adjustment and target-specific ABI classifier policies remain
inside the targets. The common function envelope does **not** imply a shared
psABI or require code generation to emit the same physical instructions.

This follows the previous shared `direct_machine_dispatch.h` consolidation:
one common algorithm for semantic machine-family routing and one common
function envelope, but independently qualified native emitters. The resulting
backend shape is an **enhancement boundary**, not two parallel orchestration
implementations that diverge whenever a feature is added.

## Incremental source review ledger (no repeated scanning)

`docs/architecture/minic-backend-review-ledger.json` records every checked-in
`src/target/*.{c,h}` file with its Git *blob* fingerprint, module owner and
source-inspection depth. This prevents conflating **present in a directory**
with **read and reviewed** or **executed by a passing CI**:

- `inventory`: enumerated and fingerprinted, not deeply read.
- `targeted`: relevant procedures/boundaries inspected; remainder pending.
- `full`: complete source was read and assessed in this architectural pass.
- `ci_touched_in`: prior PRs changing the file; these indicate historical
  tested changes, **not** that every code path has been reviewed.

`python3 tools/ci/minic-backend-review-ledger.py --summary` summarizes review
depth by target. `--changed` reports the prioritized unreviewed owners and
**stale** source fingerprints, so unchanged already-reviewed files are not
reopened on every iteration. `--check` is an automated CI inventory gate:
new or deleted target files cannot silently bypass the ledger. The baseline
commit identifies the starting source snapshot; a passing build is separately
recorded by the actual CI run, never inferred from review depth.

**Next unreviewed boundaries** are explicit in the ledger's
`next_deep_review`: RV32/RV64/x86 physical frame and register handling, and
cross-ISA CALL marshaling. Inspect those owners rather than reviewing the
already-completed Core CFG or shared Direct LP64 dispatchers again.
