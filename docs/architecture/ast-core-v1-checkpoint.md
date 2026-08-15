# AST and Core IR v1 checkpoint / AST 与 Core IR v1 检查点

## 1. Why this checkpoint exists / 为什么现在冻结

MiniC has now accumulated enough unchanged real-program pressure to stop designing the AST/Core seam one frontier at a time. The current architecture is sufficiently coherent to enter a scaling phase: real Linux translation units should now fill the mechanism set, while periodic architecture audits detect deformation before it becomes a rewrite.

MiniC 已经积累了足够多的 unchanged 真实程序压力，不应再为每一个 frontier 重新设计 AST/Core 边界。当前骨架已经足够稳定，可以进入规模扩张阶段：由 Linux translation unit 快速填充能力集合，并通过周期性架构审计在结构变形演化为大重构之前发现问题。

## 2. One Semantic AST, two forms / 一份 Semantic AST，两种 form

MiniC keeps one canonical semantic representation. `PARSED` and `NORMALIZED` are forms/snapshots of that representation, not two long-lived mutable trees.

```text
source
  ↓
Parser + Sema
  ↓
Semantic AST [PARSED]
  ↓ semantic normalization
Semantic AST [NORMALIZED]
  ↓
Core lowering
```

The parsed form has already resolved names, types, value categories and language legality, but may retain source-language constructs such as a C cast. Normalization replaces source-spelling distinctions with already-resolved semantic distinctions where that helps downstream consumers, for example `CAST` becoming `CONVERSION`, `BITCAST` or `DISCARD`.

Normalization must not become execution lowering. It may rewrite an already-decided semantic fact, but it must not prematurely choose CFG, duplicate evaluation, manufacture temporaries or erase source-language relationships still needed to preserve C/GNU C semantics.

## 3. Preserve meaningful source constructs / 保留有语义价值的源码结构

The normalized Semantic AST may deliberately retain distinct source constructs such as:

- `if`, `while`, `for`, `do-while`, `switch`;
- compound assignment and increment/decrement;
- short-circuit logical expressions and conditional expressions;
- statement expressions, compound literals and cleanup/lifetime semantics;
- initializer designators and evaluate-once relationships.

This is not redundant complexity. It keeps language structure readable and prevents parser-generated implementation patterns from becoming semantic contracts. In particular, lower layers should not have to infer a source `while` or `for` from internal labels or goto shapes.

The rule is:

> Preserve a source construct while it still carries language-semantic relationships that a later consumer needs. Lower it when only execution facts remain.

## 4. AST/Core boundary / AST 与 Core 的边界

The normalized Semantic AST answers:

> What does this C/GNU C program mean?

Core IR answers:

> What target-independent execution must happen after the C/GNU C rules are resolved?

Language rules remain above Core: usual arithmetic conversions, lvalue/value-category rules, null-pointer constants, source-level short-circuit semantics, initializer designator/override/evaluate-once rules, type compatibility and declaration semantics.

Core receives resolved executable facts: typed values, object identity, addresses, loads/stores, arithmetic/conversions, calls, explicit CFG and memory effects.

## 5. Core v1: executable O0 memory-form IR / Core v1：可执行的 O0 memory-form IR

Core v1 is the main target-neutral execution IR. It is not designed as an optimization IR first.

Its central model is:

```text
CoreFunction
├── CoreObject     semantic addressable storage identity
├── CoreValue      immutable computed value
├── CoreInstruction
├── CoreBlock
└── one explicit terminator per block
```

Mutable source state crosses blocks through `CoreObject` plus `LOAD`/`STORE`. A `CoreObject` is not a stack slot; stack placement is a backend decision. A `CoreValue` is produced once and is not mutable object identity.

Structured source control converges to CFG in Core:

```text
AST FOR / WHILE / DO_WHILE / IF
              ↓
Core Block + BR + COND_BR + future SWITCH
```

Core must not grow one opcode for every AST kind. Multiple source constructs should converge on a small set of execution primitives once their semantic differences have been resolved.

## 6. SSA is a future Core form, not another IR / SSA 是未来 Core form，不是另一套 IR

Core v1 deliberately does not require phi nodes, dominance analysis or mandatory SSA.

If real optimization pressure later justifies SSA, the preferred path is:

```text
Core phase = MEMORY
       ↓ mem2reg
Core phase = SSA
```

The same Core representation evolves to another form rather than introducing a parallel `SSA IR`. Immutable `CoreValue` already provides the right value discipline; mutable source variables remain explicit `CoreObject` storage until promotion is justified.

## 7. ABI and machine boundary / ABI 与机器边界

Logical calls and returns stay target-independent in Core.

```text
Core CALL / logical return
        ↓
TargetABI classification
        ↓
abstract argument/return locations, hidden sret where required
        ↓
backend placement
        ↓
physical registers / stack / target instructions
```

`a0`, `fa0`, concrete stack offsets and RV64 opcodes must not enter Core. DataLayout remains a read-only semantic/layout service where C semantics require target layout facts; mutable frame placement remains below Core.

A stored Machine IR remains deferred until real pressure from register allocation, spilling, machine-level optimization, multi-target instruction selection or object emission makes direct target emission a structural blocker.

## 8. Initializers / 初始化器

Initializers remain above Core as C/GNU C semantics. The recent GNU range-designator pressure demonstrates why arbitrary runtime initializers must not be expanded too early: a range initializer may require one RHS evaluation while affecting multiple elements.

The long-term direction is a first-class semantic initializer representation only when workload pressure proves its minimum necessary shape. It may preserve scalar/member/element/range/zero/string/relocation and evaluate-once relationships. Local initialization can then lower to Core execution; static initialization can lower to object data and relocations.

Do not build a broad `InitPlan` framework speculatively.

## 9. Scaling cadence after this checkpoint / 检查点后的扩张节奏

The development cadence changes after an architecture checkpoint.

Before the architecture is understood, grow pressure finely and reread often:

```text
1 → 5 → 10 → 30 translation units
```

After the ownership boundaries are stable, increase the pressure aggressively:

```text
100 → 500 → configured Linux full-TU inventory
```

A batch is not stopped merely because one TU fails. The batch driver should collect all failures in the selected window, summarize pass/fail counts and group diagnostics so repeated missing mechanisms can be implemented together.

Architecture review is periodic rather than per-file. Stop scaling and reread globally when evidence shows one of these structural symptoms:

- the same C semantic rule gains a second owner;
- Linux-specific source/path exceptions appear;
- Core begins reinterpreting C language rules;
- AST normalization begins choosing CFG/evaluation strategy;
- parser-generated representation details leak into Core contracts;
- target/ABI placement leaks upward;
- a representation workaround repeatedly fails to preserve semantics, indicating a missing first-class semantic object;
- a subsystem requires repeated cross-layer edits for one capability.

Otherwise keep filling the established mechanism set and grow the batch size.

## 10. Current route / 当前路线

```text
Source
  ↓
Parser + Sema
  ↓
Semantic AST [PARSED]
  ↓
small semantic normalization
  ↓
Semantic AST [NORMALIZED]
  ↓
Core IR [MEMORY form, CFG, target-neutral execution]
  ↓
optional future Core SSA form
  ↓
TargetABI
  ↓
current direct target backend
  ↓
future Machine IR only when justified
```

This checkpoint is a direction contract, not an instruction/opcode checklist. Linux, Lua and other unchanged real programs continue to decide which missing mechanisms are implemented next.
