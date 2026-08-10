# Foundation Refactor v1 / 基础重构 v1

## Status / 状态

This document governs the migration from the Lua/Linux discovery compiler to a maintainable compiler source of truth. It is deliberately a **migration contract**, not a claim that every internal representation is final.

本文约束 Lua/Linux discovery 编译器向长期可维护实现迁移。它刻意只固定边界与迁移规则，**不把当前内部数据结构宣布为最终设计**。

Baseline:

- reliable discovery checkpoint: `ec54d18967accb0721e32915fcb504c739baf2f9`;
- frozen Linux semantic frontier at that checkpoint: unchanged Linux 6.6.143 `init/main.i`, first unsupported construct at the interleaved `extern __attribute__((format(...)))` declaration near line 2961;
- `ca17192a...` remains discovery archaeology for the attempted interleaved-attribute patch, but is not the refactor baseline because its staging anchor is broken;
- `refactor/foundation-v1` materializes the validated effective `include/src/tools/minic` source produced by the Lua + Linux discovery staging chain.

## 1. What is stable and what is replaceable / 固定什么，不固定什么

Stable invariants:

1. Each semantic concept has one canonical representation and one owning subsystem.
2. Parser grammar, semantic analysis, target ABI/layout, IR optimization, and object/link policy are separate responsibilities.
3. Checked-in compiler source is exactly the compiler CI builds; production validation never depends on ordered source-rewrite scripts.
4. Stable IDs and context-owned pools remain the ownership model unless measurement proves a better replacement.
5. Target-specific layout and calling-convention results are queried through target interfaces rather than duplicated in frontend code.
6. Every subsystem can be replaced behind a narrow interface while real-program regression gates remain green.

Replaceable implementation choices include hash-table algorithms, Type interner layout, IR instruction storage, SSA construction strategy, register allocation, object writer internals, and optimization algorithms.

The goal is not to avoid future refactors. The goal is to make a future refactor local.

## 2. Frontend pipeline: one persistent semantic AST / 前端只保留一棵长期 AST

Parser and Sema are separate responsibilities, but Foundation v1 does **not** require two complete persistent trees (`Parsed AST` plus `Typed AST`). That would duplicate memory and make migration unnecessarily expensive.

Target flow:

```text
SourceManager
  -> Lexer / TokenCursor
  -> Parser
       -> short-lived DeclSpec / Declarator / InitializerSyntax
       -> Sema APIs
  -> Semantic AST + Decl/Symbol tables
  -> semantic verification
  -> normalization
  -> Core IR
```

Parser owns grammar, source attachment points, and construction of short-lived syntax/declarator structures. Sema owns lookup, declaration merging, linkage/storage duration, type construction/compatibility, conversions, constant-expression category checks, and attribute semantics.

A parser function may call Sema while parsing; responsibility separation does not require delaying every semantic decision until a second whole-tree pass.

## 3. Declaration model / 声明模型

The current compiler repeatedly reparses or probes declarations in different contexts. Foundation v1 converges on:

```text
DeclSpec
  storage class
  function specifiers
  base type specifiers / qualifiers
  prefix attributes

Declarator
  identifier / abstract declarator
  pointer layers + qualifiers
  array suffixes
  function suffixes + parameter declarators
  parenthesized nesting
  declarator-local attributes

DeclSpec + Declarator
  -> Sema declaration construction
  -> Type
  -> declaration occurrence (`DeclId`)
  -> merged semantic entity (`SymbolId` / record/tag entity)
```

Locals, globals, typedefs, parameters, record fields, function declarations/definitions, and type names reuse the same declarator engine.

A critical distinction is **declaration occurrence vs semantic entity**: C permits multiple compatible declarations of one function/object. Attributes, linkage, visibility, section, definition state, and diagnostics must merge into one entity rather than forcing each parser context to rediscover redeclaration rules.

## 4. Attributes / 属性

GNU attributes use a static, self-hostable registry rather than distributed string chains.

Each descriptor records at least:

```text
canonical kind
accepted aliases
allowed attachment points
semantic classification
argument parser/validator
consumer
```

Attachment points include declaration specifiers, declarators, types, fields, functions/objects, and statements where supported.

The parser preserves syntactic placement; Sema normalizes valid occurrences into an `AttributeSet` owned by the semantic entity/type/field/statement that the language rules select.

Unknown ABI/layout/lifetime-changing attributes are rejected. Diagnostic or optimization metadata may be preserved before a complete consumer exists only when that state is explicit and regression-tested.

The current Linux line-2961 blocker is the first migration fixture: `extern` followed by a GNU attribute and then the type specifier must be handled by the common declaration path, not by adding another top-level probe patch.

## 5. Names, scopes, and declarations / 名称、作用域与声明实体

Migration order:

```text
source slices
  -> StringId interning
  -> namespace-aware scoped SymbolTable
```

C namespaces remain explicit: ordinary identifiers, tags, labels, and members are not collapsed into one accidental map. Scope tables use stable IDs, never pointers into rehashable storage.

Before replacing all lookup paths, instrumentation records symbol count, lookup count, probe/scan count, and peak scope sizes so the migration has measured performance evidence.

## 6. Type migration / 类型迁移

Do not replace every `MinicType` value with `TypeId` in the first refactor commit.

First introduce a `TypeContext` boundary around type construction/query operations while retaining the current value representation. Then intern canonical types behind that boundary and migrate AST fields to `TypeId` incrementally.

Arrays must converge on one canonical type representation. `MinicLocal.is_array + element_count` and `MinicRecordField.element_count/is_array` are migration debt once `ArrayType` is authoritative.

## 7. Initializers are a first-class subsystem / 初始化不是赋值语句展开

Foundation v1 adds an explicit initializer representation instead of expanding `{0}` or aggregate initialization into large numbers of assignment AST nodes during parsing.

Conceptual flow:

```text
InitializerSyntax
  -> Sema initializer checking
  -> InitPlan
       scalar value
       zero-fill range
       aggregate element/member
       relocation
       string data
  -> IR/global-data lowering
```

This removes parser-time AST explosion, unifies static/local aggregate semantics, and gives object emission and later IR one canonical source of initializer truth.

## 8. Constant evaluation / 常量求值

One semantic `ConstEval` evaluates already-parsed semantic expressions. It serves array bounds, enum initializers, case labels, static assertions, attribute arguments, static initializers, compile-time builtins, and later optimization folding.

Parser-time token evaluators are migration compatibility only. Unsupported evaluation returns `unknown`/`invalid`; it never invents zero.

`sizeof`, `alignof`, and `offsetof` query `DataLayout` rather than carrying a second frontend copy of RV64 layout logic.

## 9. Target layout and ABI / Target layout 与 ABI

Target-neutral semantic nodes must stop owning mutable RV64 layout results.

Target-owned side data:

```text
RecordLayout[RecordId]
ObjectLayout[Decl/SymbolId]
FunctionFrameLayout[FunctionId]
```

Calling convention uses one classifier for caller and callee:

```text
TargetABI
  -> ABIArgInfo
  -> ABIReturnInfo
  -> CallLoweringPlan
```

Aggregate register chunks, stack placement, varargs, alignment, and return classification must not be independently reimplemented by call emission and function-entry emission.

## 10. Core IR boundary / Core IR 边界

Core IR is introduced only after source-of-truth and frontend ownership are stable enough that IR does not inherit temporary parser debt.

IR v0 requirements:

- explicit functions, basic blocks, terminators, and stable value identities;
- explicit evaluation order;
- explicit loads/stores, volatile state, calls, conversions, aggregate copy/zero operations, branches/switch;
- target-independent types and pointer operations;
- verifier and deterministic dump;
- stack-like memory form is allowed initially; SSA is a later pass.

The first IR implementation is intentionally replaceable. The stable boundary is `Semantic AST -> IR Builder -> Core IR -> Target lowering`, not one immutable instruction encoding.

## 11. Migration sequence / 迁移顺序

### Phase 0 — Source of truth

- [x] create `refactor/foundation-v1` from the reliable `ec54d189` checkpoint;
- [x] materialize the effective Lua + Linux compiler source into checked-in C;
- [x] add validation that builds checked-in source without semantic source rewriting;
- [ ] move/retire active discovery staging scripts after all legacy workflows no longer depend on them;
- [ ] preserve the old Lua/Linux branches as read-only archaeology until consolidation is accepted.

### Phase 1 — Declaration convergence

1. static Attribute registry + semantic classifications;
2. common `DeclSpec` and declarator representation;
3. replace top-level `static/extern/function/object` probe parsers with single-pass declaration parsing;
4. migrate function-pointer declarators in parameters/globals/typedefs/fields to the common engine;
5. consume the line-2961 interleaved-attribute fixture through the common path;
6. keep all existing focused gates green.

### Phase 2 — Measurement and lookup foundations

1. compiler phase and structure telemetry;
2. SourceManager + StringId;
3. namespace-aware scoped SymbolTable;
4. declaration/entity IDs and redeclaration merge ownership.

### Phase 3 — Type/initializer/constant convergence

1. TypeContext boundary, then TypeId interning;
2. canonical array representation;
3. InitializerSyntax + InitPlan;
4. single ConstEval;
5. remove legacy statement-level assignment/copy representations after expression-level equivalents cover all users.

### Phase 4 — Target boundaries

1. DataLayout interface + record/object side tables;
2. TargetABI classifier + caller/callee CallLoweringPlan;
3. move RV64 offsets/sizes out of semantic AST;
4. keep textual RV64 assembly backend as the validation target.

### Phase 5 — Core IR

1. IR v0 + verifier/dump;
2. migrate one function/category at a time under old/new differential validation;
3. PassManager/AnalysisManager;
4. CFG simplification, constant propagation, then mem2reg/SSA;
5. later Machine IR, register allocation, object emission.

## 12. Refactor gates / 重构门禁

No migration deletes an old path until the replacement passes:

```text
unit/focused semantic tests
-> frozen tiny-AES/cJSON/Parson/linenoise/SDS gates
-> Lua current capability gates
-> Linux unchanged init/main.i frontier
-> target ABI/runtime differential where relevant
```

Representation-only changes prefer deterministic AST/IR/state dumps or equivalent behavior rather than relying only on assembly grep tests.

## 13. Branch policy during the refactor / 重构期分支规则

Only these lines are active inputs:

```text
main                                  frozen production baseline
external/lua-discovery                discovery archaeology / stacked base
external/linux-6.6.143-discovery-v2   discovery archaeology / stopped frontier
architecture/compiler-platform-foundation architecture rationale
refactor/foundation-v1                active convergence implementation
```

Old merged `frontend/*`, `riscv/*`, and superseded external working refs are not separate architectures. Do not delete them while the consolidation audit is still using them as evidence; after Foundation v1 has a validated source-of-truth checkpoint, perform one explicit allowlist cleanup while preserving `archive/*` and `backup/*` history.
