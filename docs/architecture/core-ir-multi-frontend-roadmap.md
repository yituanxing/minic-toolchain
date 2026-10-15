# Core IR Multi-Frontend Roadmap

Status: recorded design direction; **not an instruction to interrupt current frozen-first500 Core convergence**.

## Why this exists

Core IR has grown beyond a C-AST shadow into a real typed value/CFG IR. Before the current Linux first500 convergence work finishes, preserve the intended next-stage direction so temporary C-lowering details do not become permanent Core architecture by accident.

The goal is not to clone LLVM. The goal is a compact, target-neutral common IR that can be produced by more than one language frontend and consumed by more than one backend/XLEN implementation.

## Current Core strengths

Core already owns the important common-IR skeleton:

- dynamically allocated `MinicCoreValueId` values;
- one defining instruction per value;
- explicit value/instruction types;
- explicit basic blocks and terminators;
- `branch`, `conditional branch`, `return`, and `unreachable` CFG edges;
- explicit `load` / `store` and volatile memory effects;
- object/global/function/block/field addresses and pointer offsets;
- direct and indirect calls;
- address-backed aggregate objects, record loads/copies, and aggregate call transport;
- target-neutral semantic operations whose RV64 realization remains in the backend.

This is already sufficient for a second frontend to construct Core mechanically, but it is not yet a clean public multi-frontend contract.

## Current limitations

### 1. SSA is block-local, not full CFG SSA

The verifier resets the per-block available-value set on entry to each basic block. A value defined in one block therefore cannot currently flow directly into a successor block. Lowering transports such values through Core objects with explicit stores/loads.

That is a valid transitional model, but it prevents Core from having LLVM-like cross-CFG SSA and causes temporary spill/reload traffic that should not be fundamental IR semantics.

### 2. Core types are still C-owned

`core_ir.h` directly depends on frontend token/type headers and Core entities carry `MinicType`, `MinicRecordId`, and `MinicFunctionTypeId`.

`MinicType` contains C-language concepts such as integer rank (`char`, `short`, `int`, `long`, ...), enum identity, C qualifiers, and frontend record/function IDs. A non-C frontend would therefore have to pretend to speak C types in order to produce Core.

That is the main blocker to calling Core a genuinely language-neutral multi-frontend IR.

## Ownership boundary to preserve now

Frontend/Sema owns **language legality and language meaning**.

Examples:

- C integer promotions and usual arithmetic conversions;
- C pointer compatibility and GNU pointer extensions;
- typedef/enum identity and C qualifiers;
- lvalue/rvalue and initialization rules.

Core owns **execution representation and common semantics** after the frontend has committed them.

Examples:

- typed scalar values;
- arithmetic/comparison operations;
- addresses and pointer offsets;
- loads/stores and volatility;
- calls;
- aggregate object transport;
- basic blocks and CFG edges.

A current concrete example is GNU function-pointer/`void *` equality: the C frontend decides whether the expression is legal; Core should only normalize the already-approved pointer values to a common representation and compare them.

## Post-first500 target architecture

```text
C frontend -----------\
                       \
Other frontend --------> Core IR -> common optimization/legalization -> RISC-V backend
                       /                                      \
Future frontend ------/                                        -> future backend
```

A second frontend must lower **directly to Core**. It must never be required to synthesize C AST nodes or `MinicType` merely to reuse the backend.

## Planned evolution

### Phase 1: finish current Core single-path convergence

Do not interrupt the current first500 work for speculative IR infrastructure.

Exit criteria remain:

1. Core-owned subsets never silently fall back.
2. Every frozen-first500 defined function lowers semantic AST -> Core.
3. first500 function bodies emit Core -> RV64.
4. legacy AST -> RV64 function-body paths can be retired.

Then freeze the observed Core semantic primitive set before restructuring representation.

### Phase 2: Core-owned type system

Introduce a Core type identity (`MinicCoreTypeId` or equivalent) owned outside the C frontend.

Keep the first version intentionally small, for example:

- `void`;
- fixed-width integers (`i1`, `i8`, `i16`, `i32`, `i64`, `i128` as actually required);
- floating types when their Core consumers are real;
- pointers;
- arrays/aggregate layout identities needed by Core objects;
- function signatures.

The C frontend performs an explicit semantic-type -> Core-type lowering step.

Do **not** carry C-only notions such as `long` rank, plain-char identity, typedef identity, or C assignment compatibility into the final Core type contract. `volatile` should primarily remain an explicit memory/effect property rather than a general SSA value qualifier.

Typed pointers are acceptable initially; opaque pointers are not required merely to resemble LLVM.

Acceptance milestone: `core_ir.h` no longer needs `frontend/type.h`, and a Core function can be verified without consulting C frontend Program state.

### Phase 3: full CFG SSA

Upgrade block-local SSA to cross-block SSA.

Prefer **basic-block parameters + branch arguments** over adding a traditional PHI instruction unless implementation evidence favors PHI.

Conceptually:

```text
entry:
    condbr %cond, left, right

left:
    %a = ...
    br merge(%a)

right:
    %b = ...
    br merge(%b)

merge(%v: i32):
    ...
```

For loops:

```text
entry:
    br loop(%init)

loop(%i: i32):
    %next = add %i, 1
    condbr %cond, loop(%next), exit(%i)
```

The verifier must validate predecessor argument count/type and dominance/availability across the CFG rather than clearing all availability at each block boundary.

### Phase 4: remove transport spills that only exist because SSA is block-local

Once block arguments/full SSA exist, audit `spill_scalar_value` / `reload_scalar_value` uses.

Keep real address-taken objects and real memory semantics. Remove only artificial transport through memory that exists to move scalar values between blocks or preserve evaluation ordering that can instead be represented in SSA/CFG.

Do not force aggregates into scalar SSA merely for stylistic similarity with LLVM; the current address-backed aggregate model is a legitimate design if it continues to simplify C and ABI lowering.

### Phase 5: validate with a real second frontend

Do not declare the abstraction complete based only on hypothetical extensibility.

Build a deliberately small but real second frontend that can exercise:

- constants/arithmetic;
- local values;
- branches and loops;
- functions/calls;
- explicit memory;
- at least one aggregate or pointer use if the language supports it.

Hard acceptance criterion:

> The second frontend can generate and verify Core IR without including C AST or C frontend type headers and without constructing `MinicC0Program`/`MinicType` as an adapter format.

That second consumer should drive any additional abstraction instead of pre-building LLVM/GCC-scale TargetMachine, Subtarget, PassManager, MachineIR, or language-neutral infrastructure without evidence.

## Non-goals

- Do not rewrite Core from scratch.
- Do not stop first500 convergence to build a speculative LLVM clone.
- Do not require every aggregate to become an SSA register value.
- Do not copy LLVM APIs or pass infrastructure merely for familiarity.
- Do not move C semantic legality into Core.
- Do not make a second frontend lower through C AST/`MinicType`.

## Architectural checkpoint

The current Core direction is sound: typed virtual values, explicit CFG, explicit memory, calls, and target-neutral operations are already the correct common-IR foundation.

The two structural upgrades that turn that foundation into a genuinely multi-frontend IR are:

1. **Core-owned, language-neutral types**;
2. **cross-basic-block SSA, preferably via block parameters/branch arguments**.

Implement them after the current first500 Core convergence has produced a stable semantic instruction set, then prove the boundary with a real second frontend.
