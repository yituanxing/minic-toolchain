# Post-first500 Architecture Review Checkpoint / first500 后架构审查检查点

> **Historical checkpoint / 历史检查点 — updated 2026-08-27**
>
> This file records the state immediately after the original first500 convergence.
> It is no longer the current production-architecture description. Production
> function bodies are now Core-only; the legacy AST -> RV64 function route and
> migration-era shadow/hybrid control plane have been removed. Exact head
> `770c91881854db92cbe569acc1f3871b0ec13514` passed frozen strict first500
> **500/500** with unsupported=0, error=0, preprocess_missing=0 after the Core
> backend ownership cuts and the first `core_lower.c` ownership split.
>
> Current continuation policy: preserve first500 as the behavior baseline and
> resume Linux pressure with a larger parameterized frontier rather than
> reintroducing fallback or source-mutating productizers.


## Status / 状态

This document freezes the conclusions at the end of the Linux first500 convergence phase. It is an **architecture review entry point**, not a declaration that the current implementation is the final architecture.

本文冻结 Linux first500 收口阶段结束时已经确认的事实、判断和下一步审查方法。它是**架构复盘入口**，不是“当前实现就是最终架构”的声明。

The immediate rule after this checkpoint is:

> Do not continue piling new language/toolchain features onto the current shape until the current implementation has been reread against the intended architecture.
>
> 在重新按目标架构读完当前实现之前，不继续盲目叠加新的语言能力或工具链组件。

## 1. Frozen capability checkpoint / 已冻结的能力节点

Exact semantic head:

```text
818f697d66c8a02b29e641d8304b17ad8bb4534a
branch: frontend/linux-first500-final-v1
PR: #484 frontend: converge Linux first500 remaining semantics
```

Linux 6.6.143 frozen first500 replay evidence:

```text
workflow: Linux 6.6.143 Batch Pressure #446
run:      32393868831
corpus:   frozen
selected_c_tus=500
pass=500
fail=0
preprocess_missing=0
minic_jobs=4
replay_seconds=206.354
corpus_bytes=1471371495
minic_sha256=56233823774e3b46b6b3b4b7cc0f070797e50f811df93e8363fd84343c2f95c9
```

At the same exact head:

- `Compiler C0 Gates #3999`: SUCCESS;
- `Frontend Ownership Contracts #694`: SUCCESS.

This means **500/500 is a real compiler milestone**, not an inferred score from focused fixes.

It does **not** mean:

- the complete Linux kernel builds with MiniC;
- the architecture is already clean;
- the native preprocessor/assembler/linker should start automatically;
- the current parser/initializer/backend representations should be frozen permanently.

The compiler-first roadmap explicitly keeps preprocessing, assembly, linking, CRT and sysroot external during this phase.

## 2. Original architectural intent we must preserve / 原先不能丢的架构目标

The existing architecture documents already define several long-term invariants. This checkpoint treats them as the review baseline rather than inventing a new architecture from scratch.

### 2.1 Real software drives priority, standards define semantics

Linux and other real projects expose missing capabilities. They do not authorize workload-specific compiler behavior. A Linux failure should become a minimal, general language/semantic/IR/target capability whenever possible.

### 2.2 Modular monolith, not framework soup

MiniC should stay one readable toolchain with a visible path. Boundaries exist where ownership, lifetime, invariant, target variation or independent testability justify them. Future possibility alone is not enough reason for a framework layer.

### 2.3 One persistent semantic AST

The target frontend shape is still:

```text
SourceManager
  -> Lexer / TokenCursor
  -> Parser
       -> short-lived DeclSpec / Declarator / InitializerSyntax
       -> Sema APIs
  -> Semantic AST + declaration/symbol tables
  -> verification
  -> normalization
  -> Core IR
```

We do **not** want two complete long-lived trees (`Parsed AST` + `Typed AST`) just to imitate GCC/Clang. Parser and Sema are ownership boundaries, not a requirement for two physical trees.

### 2.4 One common declaration/declarator model

The intended common shape remains:

```text
DeclSpec + Declarator
  -> Sema declaration construction
  -> Type
  -> declaration occurrence (DeclId)
  -> merged semantic entity (SymbolId / tag entity)
```

Locals, globals, typedefs, parameters, fields, function declarations/definitions and type names should not each rediscover pointer/array/function/attribute declarator rules.

### 2.5 Initializers are first-class semantics

The intended model remains conceptually:

```text
InitializerSyntax
  -> semantic checking
  -> InitPlan
       scalar value
       zero-fill range
       aggregate element/member
       relocation
       string data
  -> local Core IR or global data/relocation lowering
```

Initializer semantics should not be permanently encoded as parser-time flattened assignment/storage accidents.

### 2.6 One ConstEval semantic service

Array bounds, enum initializers, static assertions, case labels, attribute arguments and static initializers should converge on one constant-expression semantic service. Token-level evaluators are compatibility plumbing, not another semantic source of truth.

### 2.7 DataLayout, ABI and machine placement are different owners

- `DataLayout`: `sizeof`, `alignof`, record size/alignment, field/bit-field placement and object layout facts.
- `TargetABI`: calling-convention classification and abstract parameter/return locations.
- backend/frame/machine lowering: stack slots, physical registers, instructions, spills and textual/encoded machine output.

These must not collapse back into one mutable target state container.

### 2.8 Core IR is a semantic execution boundary, not a fashionable IR stack

The intended ownership remains:

```text
Semantic AST / FunctionBody
  -> language-semantic normalization/lowering
  -> Core IR
  -> Target ABI lowering
  -> machine/backend lowering
  -> RV64 assembly / future object emission
```

SSA, phi nodes, dominance, pass managers, Machine IR and register allocation are **not prerequisites**. They are introduced only when real transformations or targets require them.

## 3. What first500 pressure revealed that we underestimated / first500 暴露出的预想之外问题

### 3.1 Initializers became a central compiler subsystem much earlier than expected

Linux pressure did not merely require “support struct initializers”. It required interacting semantics including:

- nested records and arrays;
- designated initializers;
- backward designator overwrite / last-writer-wins behavior;
- GNU range designators;
- active union-member selection;
- union members with different flattened semantic shapes but the same physical storage;
- symbolic object/function/label relocations;
- relocation addends and nested member paths;
- zero-length arrays;
- flexible-array-member tails;
- record-valued FAM elements;
- strings and zero-fill;
- static-local and file-scope ownership.

The current `initializer_values[] + relocation side data + union-selection side data + flexible-array count` machinery proved that these facts are real and need representation. It also shows that the old **flattened initializer slot count is at risk of becoming an accidental long-term contract**.

The index-6 union failure made this especially visible: a union member represented by four scalar semantic slots and another represented by one scalar slot can occupy the same physical bytes. Semantic initialization and physical storage span are not the same concept.

**Review consequence:** `InitPlan` is no longer speculative architecture. The audit must determine how to introduce it incrementally without throwing away the working 500-TU semantics.

### 3.2 Declarator/attribute grammar is more cross-cutting than it looked

The path to 500/500 repeatedly exposed the same class of ownership problem in different contexts:

- file-scope declarations;
- static locals;
- inferred arrays such as `T[][N]`;
- function return pointer levels;
- GNU attributes between pointer levels;
- function-pointer declarations;
- typedef/field/parameter declaration forms.

A concrete late example was:

```c
char * __attribute__((__unused__)) *fn(void)
```

The problem was not “one strange Linux macro”. It was that multiple parser paths still know pieces of declarator grammar.

**Review consequence:** Foundation v1's common `DeclSpec + Declarator -> Sema` plan has not been fully realized. We must measure how many declaration engines/partial probes still exist before adding more syntax.

### 3.3 Declaration occurrence vs semantic entity matters in real C

Linux redeclarations, attributes, linkage, visibility, section state and definitions make the distinction between “this declaration occurrence” and “the merged semantic object/function entity” increasingly important.

The compiler has already grown side tables and ownership helpers to preserve facts across declarations. This is evidence for the planned `DeclId`/`SymbolId` distinction, not evidence for adding more parser-local booleans.

### 3.4 DataLayout became valuable before native object/linker work

Nested relocation offsets, record/union layout, FAM storage, zero-sized objects and active-member emission all required one layout source of truth. This validates the decision to make `DataLayout` a first-class query layer even while GNU assembler/linker remain external.

**Positive result:** this boundary should be preserved and strengthened, not folded back into frontend or RV64 emitter special cases.

### 3.5 Backend diagnostics are part of architecture, not cosmetic output

A generic `cannot write RISC-V assembly` message was insufficient once Linux TUs contained thousands of globals/functions. Adding exact owner diagnostics (`global object 'x'`, `function 'y'`) materially improved ownership debugging.

**Review consequence:** during the architecture audit, failure provenance should be treated as a compiler interface between layers. A layer that can only return `false` with no owner/kind information will become expensive as coverage grows.

### 3.6 Core IR was activated, but it is still shadow-only

The representation-seams work correctly identified that direct AST -> RV64 accumulated target-neutral execution responsibilities: evaluation order, conversions, local object identity, memory effects and CFG construction.

Core IR now has a real shadow consumer and verifier, but the production assembly path still uses the legacy/direct RV64 route for the full language surface.

This is a useful migration state, but it creates an important question after 500/500:

> Do we keep widening the production AST -> RV64 path, or has the evidence now justified migrating selected production categories through Core IR?

We must answer this from code ownership and duplication, not from ideology.

### 3.7 Temporary productizers became their own source of complexity

Foundation v1 said checked-in compiler source should be the exact source CI builds and production validation should not depend on ordered semantic source-rewrite scripts.

The first500 convergence phase temporarily violated that ideal intentionally through productizers/materializers. That was useful for rapid validated convergence, but by the end it produced its own failure modes:

- ordering/CAS races while bot commits and replay triggers advance the branch;
- consumed textual anchors;
- clang-format-sensitive idempotency problems;
- duplicated test insertion;
- obsolete materializers failing after their product already exists;
- current `Productize Linux first500 final PR v1 #175` fails in `Materialize semantic patch` with:

```text
cannot locate aggregate-array action replay blocks
```

At the same exact head, C0, ownership and first500 are green. Therefore this red workflow is now evidence that the **migration scaffolding itself has outlived part of its purpose**, not evidence that the compiler semantic product is red.

**Review consequence:** productizer retirement/cleanup is P0 architecture hygiene after freezing the 500/500 product.

### 3.8 The convergence branch is history, not yet a clean architectural baseline

At the 500/500 head, PR #484 contains roughly:

```text
171 commits
83 changed files
+7419 / -541
```

That history was useful to converge semantics under pressure. It should not automatically become the branch on which the next multi-month architecture phase is stacked.

A clean checkpoint should preserve the permanent compiler/test product and evidence while retiring transient replay/productizer scaffolding.

## 4. What went better than expected / 比原先预想更好的部分

### 4.1 Stable IDs and context-owned pools survived real pressure

We did not need a pointer-AST rewrite to reach 500/500. Dense ID arenas remain viable as a physical representation. This supports the principle that storage strategy is replaceable and should not be changed without evidence.

### 4.2 FunctionBody/traversal/remap seams are useful without a framework

The compiler can define ownership and traversal boundaries with plain C APIs while retaining existing storage. This is exactly the kind of low-cost seam the project wanted.

### 4.3 DataLayout and active semantic side data prevented Linux-specific hacks

Although some side metadata is transitional, preserving relocations, active union selections and layout queries as explicit semantic facts allowed fixes to remain general rather than keyed to Linux filenames/indices.

### 4.4 Focused -> real workload validation became a strong development loop

The workflow that emerged is worth keeping:

```text
real workload exposes failure
-> identify semantic owner
-> reduce/generalize
-> focused regression
-> exact real-TU replay
-> broader gate
```

This is now proven at scale.

### 4.5 Frozen first500 is a fast architecture safety net

The 500-TU frozen corpus replay completes in about 206 seconds at `-j4` on the recorded runner. This is valuable for refactoring because representation-only changes can be checked against a large unchanged semantic corpus without rematerializing Linux every time.

## 5. Things we must NOT conclude from 500/500 / 500/500 之后不能误判的事

1. **Do not start a native preprocessor automatically.** The roadmap explicitly requires a separate milestone decision.
2. **Do not start assembler/linker/runtime work automatically.** The current compiler architecture itself needs review first.
3. **Do not rewrite everything because the current code looks complicated.** We now possess a large amount of proven semantics and regression evidence; a rewrite must justify what it preserves and what it simplifies.
4. **Do not freeze current physical representations just because they reached 500/500.** Coverage validates behavior, not representation quality.
5. **Do not add SSA/MachineIR/advanced optimization because mature compilers have them.** Add them only when a real consumer requires them.
6. **Do not continue source-rewrite productizers as a normal development architecture.** Their exit criteria have arrived.

## 6. Architecture questions the full reread must answer / 下一轮完整阅读必须回答的问题

The next review is incomplete until it can answer these questions with source evidence.

### Frontend / declaration ownership

1. How many distinct paths parse declaration specifiers and declarators today?
2. Which rules are duplicated across global, local, typedef, parameter, field and function parsing?
3. Which semantic rules are still owned directly by `parser_*` code that should belong to Sema/query services?
4. Is the planned declaration-occurrence/entity distinction now necessary, and what is the smallest migration slice?

### Types and constants

5. Which parts of `MinicType`/array/record representation are semantic and which are storage debt?
6. Do we need `TypeContext`/canonical `TypeId` now, or can current value types remain behind a cleaner API?
7. How many independent constant-evaluation paths remain?

### Initializers / globals

8. Exactly which modules interpret flattened initializer slots?
9. Can `InitPlan` be introduced as a semantic view over current storage first, before changing storage?
10. Can local and global initialization converge on the same checked plan while lowering to different consumers?
11. Are relocation paths, active-union selection and FAM size facts owned in the correct layer?

### AST / Core IR

12. Which target-neutral execution rules are still duplicated in the direct RV64 backend?
13. Which current Linux-supported function categories could migrate through Core IR without requiring speculative SSA/MachineIR?
14. Is the shadow path reducing duplication, or has shadow maintenance itself started duplicating semantics?

### Target / ABI / backend

15. Is all record/object layout now queried through DataLayout, or does frontend/backend still recompute pieces?
16. Do caller and callee use one TargetABI classification source of truth?
17. Which RV64 helpers mix ABI, frame layout, instruction selection and textual emission?
18. Where do raw `false` failures still lose enough provenance to make debugging expensive?

### Infrastructure / maintainability

19. Which materializers/productizers are now obsolete and can be deleted after preserving the permanent product?
20. What are current LOC/module-size hotspots, and are they large because of inherent semantics or duplicated ownership?
21. Which public/internal interfaces have the highest fan-out and change amplification?
22. What is the minimum clean branch that preserves 500/500 while removing convergence scaffolding?

## 7. How we start the next phase / 下一阶段从哪里开始

Do **not** start with a rewrite commit. Start with an evidence pass.

### Stage A — Freeze and clean the milestone

1. Preserve exact `818f697d...` + first500 #446 evidence.
2. Freeze the 500/500 compiler/test product as a named checkpoint.
3. Separate semantic product from productizer/materializer/replay scaffolding.
4. Do not merge PR #484 merely because 500/500 is green; first decide the clean consolidation path.

### Stage B — Build an actual architecture inventory

Read the complete checked-in compiler, not only the files touched by the last failure.

Produce:

- source tree/module inventory;
- LOC and large-function hotspots;
- internal/public dependency map;
- state/ownership map (`MinicC0Program`, parser, FunctionBody, target state, globals);
- semantic-source-of-truth map;
- list of duplicated/rederived rules;
- target-specific references outside target-owned modules;
- obsolete migration scripts and workflows.

### Stage C — Trace representative end-to-end cases

At minimum trace these through the real code:

1. a complicated declaration with attributes + pointer/array/function declarators;
2. a global aggregate initializer with union selection + relocation + FAM;
3. an expression requiring conversion/ConstEval;
4. a function call/return exercising ABI classification;
5. control flow from normalized AST through Core shadow and direct RV64 production.

For each trace, record which layer owns each decision and where the same decision is recomputed.

### Stage D — Compare actual vs intended architecture

Build a debt matrix. Each candidate refactor gets scored by:

- correctness risk;
- change amplification;
- code reduction/simplification potential;
- multi-target blocker;
- future preprocessor/assembler/linker interaction;
- self-hosting blocker;
- testability;
- migration cost while preserving 500/500.

### Stage E — Choose one first refactor slice

Only after the inventory should we choose between the two strongest current candidates:

- declaration/Sema convergence; or
- first-class initializer/InitPlan convergence.

The first slice must:

- have one clear owner;
- remove an existing duplication or accidental representation contract;
- keep semantics unchanged;
- add deterministic structural evidence where possible;
- pass focused gates, C0/ownership, and unchanged first500.

Core IR production migration is a separate decision informed by the duplication audit; it should not be mixed into the first cleanup slice unless the evidence specifically requires it.

## 8. Working rule for future conversations / 后续接续规则

When resuming MiniC architecture work, start from this checkpoint and the current source, not from conversational memory alone.

Before proposing the next large architectural change:

1. verify the exact repository head/checkpoint;
2. reread the affected canonical source;
3. compare against `principles.md`, `foundation-refactor-v1.md`, and `representation-seams-and-deferred-ir.md`;
4. distinguish confirmed source facts from hypotheses;
5. preserve 500/500 as the behavior safety net;
6. prefer local convergence over a whole rewrite unless the audit proves local replacement is more expensive.

This checkpoint should be updated or superseded when the full post-first500 architecture audit is complete.

## Post-1000 cleanup checkpoint

The 1000-TU frozen baseline is now the semantic safety net for architecture cleanup. Local semantic defaults and structured AST-verifier failure provenance are converged first; array representation migration remains producer-first and must not change consumer semantics until legacy producers are eliminated.
