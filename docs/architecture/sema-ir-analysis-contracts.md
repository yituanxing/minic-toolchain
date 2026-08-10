# Sema, Constant Evaluation, IR and Analysis Contracts / 语义、常量求值、IR 与分析契约

Status: second-layer architecture baseline. The whole-toolchain topology in `toolchain-platform.md` remains authoritative; this document makes the next implementation boundaries explicit before Linux/GNU-C discovery hardens parser/codegen shortcuts into permanent interfaces.

状态：第二层架构基线。`toolchain-platform.md` 继续定义整体拓扑；本文在 Linux/GNU C 继续扩大之前，固定 Parser/Sema、ConstEval、Core IR 语义、AnalysisManager 与 inline asm 的下一层契约。

## 1. Parser and Sema are distinct responsibilities / Parser 与 Sema 分责

The parser answers **what source construct was written**. Sema answers **what that construct means in the selected C dialect and target-independent language model**.

Long-term flow:

```text
Tokens
  -> Parser / Declarator
  -> Parsed AST
  -> Sema
  -> Typed Semantic AST
  -> Normalization
  -> Core IR
```

Parser owns:

- grammar and source structure;
- declarator shape;
- source ranges and syntactic attachment points;
- extension syntax dispatch.

Sema owns:

- C namespace lookup and declaration merging;
- linkage and storage duration;
- redeclaration compatibility;
- lvalue/rvalue and function/array conversion;
- integer promotions and usual arithmetic conversions;
- pointer compatibility and qualifier composition;
- constant-expression category checks;
- attribute semantic validation;
- builtin semantic validation;
- diagnostics that depend on language meaning rather than grammar.

Migration is incremental. Existing parser-time semantic checks may remain temporarily, but new cross-context declaration semantics should prefer shared helpers or Sema-facing data instead of another context-specific parser copy.

## 2. One constant evaluator / 单一常量求值器

MiniC must converge on one target-query-aware constant evaluator rather than separate evaluators for array bounds, enums, attributes, static initializers and control-flow folding.

Conceptual result:

```text
ConstValue
  integer(APInt)
  floating(APFloat or documented host-independent equivalent)
  pointer(SymbolId + addend)
  aggregate(slice of ConstValue)
  unknown
  invalid
```

Evaluation mode describes the C rule being requested:

```text
IntegerConstantExpression
ArithmeticConstantExpression
AddressConstant
StaticInitializer
ArrayBound
EnumInitializer
CaseLabel
StaticAssert
AttributeArgument
FoldForOptimization
```

The evaluator must never turn an unsupported expression into an invented numeric value. Unsupported and invalid are explicit states.

`APInt`-style width-aware arithmetic becomes required before `_BitInt(N)` or general integers wider than the host representation become permanent features. Signedness, truncation, extension and overflow behavior are properties of the evaluated C type, not accidental host-C behavior.

## 3. Core IR semantic contract / Core IR 语义契约

Core IR is not merely a prettier AST. Every instruction must state enough semantics that later transformations can prove they preserve C behavior.

### 3.1 Integer arithmetic

Unsigned arithmetic has modular semantics at its declared width. Signed arithmetic must not be blindly given unsigned wrap semantics. The first IR may conservatively avoid exploiting signed-overflow undefined behavior; later optimization flags may encode proven facts explicitly.

Possible future facts include `no_signed_wrap` and `no_unsigned_wrap`, but these are optimization facts, not parser decorations.

### 3.2 Memory operations

Loads and stores carry explicit properties instead of relying on hidden AST context:

```text
value type
address value
alignment (known/minimum)
volatile flag
atomic ordering when atomics arrive
source range
```

Volatile operations are observable operations and cannot be removed, duplicated or freely reordered.

### 3.3 Pointer model

The first Core IR may use typed or opaque target-independent pointer values, but it must leave room for:

- object identity/provenance;
- pointer + integer offset formation;
- object bounds facts;
- null/non-null facts;
- address-space support;
- restrict/noalias facts derived by Sema/analysis.

Do not encode pointer semantics as plain host integers in the generic mid-end. Pointer-to-integer conversion is an explicit operation whose target width comes from DataLayout.

### 3.4 Aliasing and effective type

The initial optimizer is conservative. It may assume two arbitrary pointers alias unless a language rule, object identity, restrict contract or analysis proves otherwise.

Strict-alias/effective-type optimizations are opt-in capabilities added only with regression evidence. Correctness takes precedence over reproducing aggressive GCC/Clang alias assumptions early.

### 3.5 Undefined and implementation-defined behavior

IR construction records language decisions already resolved by Sema/TargetInfo. Optimization may exploit undefined behavior only when the corresponding IR contract is explicit and verifier/test coverage exists.

Implementation-defined choices such as plain-char signedness and integer widths are queried through language/target configuration and become concrete before or during IR construction; they are not guessed by individual passes.

## 4. Control flow and SSA migration / 控制流与 SSA 迁移

Core IR v0 must have explicit basic blocks, terminators and value identities even if it initially retains stack-like alloc/load/store form.

```text
Normalized AST
  -> CFG + explicit temporaries
  -> verifier
  -> canonicalization
  -> mem2reg / SSA construction
```

The representation must support either phi nodes or documented block arguments. Evaluation order cannot remain implicit in recursive code generation once a function is migrated to Core IR.

Verifier minimums:

- every block ends in exactly one terminator;
- every referenced value/instruction exists;
- operand and result types are compatible;
- branch targets belong to the current function;
- SSA dominance rules are checked once SSA is enabled;
- memory/volatile/atomic flags are internally consistent.

## 5. AnalysisManager / 分析管理器

PassManager schedules transformations. AnalysisManager owns reusable derived facts and their invalidation.

Initial analysis families:

```text
CFG indexing
DominatorTree
PostDominatorTree
UseDef
LoopInfo
Liveness (Machine IR)
```

Later analyses:

```text
AliasAnalysis
ModRef
CallGraph
Range/known-bits analysis
Escape analysis
MemorySSA or another explicit memory-dependence representation
```

A pass declares which analyses it requires and which facts remain valid after it runs. CFG-mutating passes must not leave stale dominance/loop information visible to later passes.

Keep the implementation data-oriented: stable analysis IDs/descriptors plus context-owned caches are preferred over a deep object-inheritance framework.

## 6. Attribute semantics / 属性语义

Parsing an attribute and implementing an attribute are separate states that must never be confused.

Every registered attribute has:

```text
name aliases
allowed attachment points
argument grammar
language/dialect availability
semantic classification
preserved metadata
lowering/diagnostic consumer
```

Useful classifications include:

- diagnostic-only (`deprecated`, GCC `error`/`warning`);
- optimization hint (`always_inline`, `pure`, `malloc` where not otherwise semantic);
- control-flow/lifetime (`noreturn`, `cleanup`);
- ABI/calling convention;
- layout/alignment/packing;
- symbol/object metadata (`visibility`, `weak`, `alias`, `section`).

ABI/layout/lifetime-changing attributes may never be accepted by merely skipping their tokens. Diagnostic attributes may initially be preserved before their complete consumer exists, but that temporary state must have an explicit exit criterion and focused test.

## 7. GNU inline asm / GNU 内联汇编

Linux will eventually require inline asm to become a first-class frontend/target feature rather than a string that the parser ignores.

Semantic representation:

```text
InlineAsm
  template string
  outputs[]
  inputs[]
  clobbers[]
  goto_labels[]
  volatile
  has_memory_clobber
  dialect/options
  SourceRangeId
```

Frontend owns syntax, operand expressions, lvalue requirements and attachment diagnostics. Target owns constraint interpretation and physical register/register-class legality.

Target-facing constraint model must support incremental RISC-V forms without baking RISC-V strings into generic Sema:

```text
constraint token -> TargetConstraint
TargetConstraint -> register class / immediate predicate / memory operand policy
```

`asm goto`, early-clobber, tied operands and memory clobbers are explicit later milestones. Unknown constraints are rejected rather than guessed.

Core IR may represent inline asm as a side-effecting call-like instruction with explicit operands, clobbers and memory effects. Optimizers must treat volatile asm and memory clobbers conservatively.

## 8. Compile-time diagnostic functions / 编译期诊断函数

GNU attributes such as `__attribute__((error("...")))` and `warning` need a semantic consumer, not permanent parse-only handling.

Required path:

```text
Function declaration
  -> AttributeSet preserves diagnostic attribute
  -> Sema/IR call site knows callee attribute
  -> proven-unreachable calls disappear through constant/CFG folding
  -> reachable call to `error` function emits the requested compile-time diagnostic
```

This is especially important for Linux `compiletime_assert`/`BUILD_BUG` families: correctness means both accepting valid constant-false assertion paths and rejecting a path whose diagnostic call remains reachable.

Until AttributeSet is materialized, discovery-only parsing of this attribute must be labeled temporary and followed by a focused semantic gate before the Linux object/link milestone is frozen.

## 9. Architecture-driven blocker rule / 由架构驱动的真实源码阻塞处理

For each new Linux/Lua blocker:

1. capture the unchanged source construct;
2. classify it as parser, Sema, ConstEval, IR, TargetABI, Object or Driver behavior;
3. check the Python history/oracle for known semantics and old bug classes;
4. implement the narrow generic capability;
5. if the same semantic logic already exists in another parser context, extract/reuse it instead of copying it;
6. add a focused gate that names the invariant;
7. return immediately to unchanged upstream input;
8. materialize discovery patches before they become cross-feature dependencies.

This rule makes real-program pressure an architecture input rather than an excuse for permanent special cases.

## 10. Near-term implementation order / 近期实现顺序

Without blocking the active Linux discovery frontier:

```text
A. share declaration/attribute helpers where Linux exposes context duplication
B. materialize staged source of truth
C. separate reusable declaration/Sema operations from parser contexts
D. centralize ConstEval entry points
E. introduce SourceManager/StringId/SymbolTable/TypeId foundations
F. move target layout/ABI to side tables
G. Core IR v0 + verifier
H. AnalysisManager skeleton + CFG/Dominator/UseDef
I. migrate RV64 lowering function-by-function
J. add constant CFG folding and then mem2reg/SSA
K. introduce AttributeSet consumers, including diagnostic attributes
L. design/implement GNU inline asm when unchanged upstream first requires it
```

The ordering is deliberate: it fixes source-of-truth and semantic ownership before optimization sophistication, while Linux continues to reveal which generic capabilities deserve priority.
