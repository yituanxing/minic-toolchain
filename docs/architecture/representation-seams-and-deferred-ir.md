# Representation seams and Core IR / 表示边界与 Core IR 契约

## 1. Purpose / 目的

MiniC should keep semantic contracts stable while allowing physical representations and algorithms to evolve when real workloads justify a change.

MiniC 应稳定长期语义契约，同时允许物理表示与算法在真实负载证明需要时演化。

This document does **not** require a framework, virtual interface hierarchy, pointer AST, or immediate data-structure migration. Plain C functions and small value types are sufficient abstraction boundaries.

本文**不**要求引入框架、虚接口体系、改成指针 AST，或立刻迁移数据结构。普通 C 函数和小型值对象即可形成有效边界。

The guiding rule is:

核心原则：

> Stable semantics and interfaces; replaceable representation and strategy.
>
> 稳定的是语义与接口；可替换的是表示与策略。

A second rule now governs representation lowering:

> Resolve a rule where it belongs; preserve the resulting fact while a real consumer still needs it; lower it when the remaining choice belongs to the next layer.
>
> 规则在真正所属的层解释；解释产生的事实只要仍有真实 consumer 就继续保留；直到剩余选择属于下一层时再降低。

## 2. Architecture review questions / 架构审查三问

Before a structural change, answer these three questions:

每次结构调整前，先回答三个问题：

1. **Who should own this semantic rule? / 这个语义真正应该由谁拥有？**
   - Is the rule language semantics, target data layout, ABI, backend placement, or assembly emission?
   - 是否已经出现多个模块分别解释同一规则？
   - A second source of truth is a design defect even when both copies currently agree.

2. **Has the current implementation accidentally become a long-term contract? / 当前实现是否被错误地当成长期契约？**
   - Array index, pointer, linked list, register spelling, cache layout, and traversal strategy are implementation choices unless semantics require them.
   - 数组下标、指针、链表、寄存器名字、缓存方式和遍历策略原则上都不是语义契约。

3. **If this boundary is not left open now, will later change become progressively more expensive? / 如果现在不留边界，以后会不会越来越难换？**
   - A seam is justified when future replacement would otherwise force unrelated compiler layers to change.
   - If an implementation can already be replaced locally, do not add abstraction merely for symmetry.

Useful expanded checks are: what real problem is being solved; what is the canonical source of truth; what evidence would justify replacing the current strategy; and whether the proposed seam makes future AST-to-IR or multi-target work easier rather than harder.

扩展检查包括：解决的真实问题是什么、唯一真相源在哪里、什么证据才足以更换当前策略，以及该边界是否让未来 AST→IR / 多目标更容易而不是更困难。

## 3. AST contract: tree semantics, graph references, replaceable storage / AST 契约：树语义、图引用、可替换存储

The semantic AST is conceptually a tree where syntax is recursively composed, plus graph edges for semantic references such as symbols, types, globals, labels, and calls.

语义 AST 在语法组合关系上是树；符号、类型、全局对象、标签、调用目标等跨节点语义引用形成图。

The current dense ID-indexed arenas are a physical storage choice, not the conceptual AST definition.

当前稠密的 ID 索引 Arena 是物理存储选择，不是 AST 的概念定义。

The intended separation is:

```text
logical AST tree
      +
semantic graph edges
      ↓
stable handles / IDs
      ↓
access, traversal and rewrite API
      ↓
physical storage
(array / arena today; chunked pool, pointer-stable pool,
 list, or another structure later when evidence warrants)
```

Required invariants:

- Do not create a second mutable pointer AST that must stay synchronized with the ID arenas.
- Tree View / NodeRef is a view over the canonical semantic representation, not another source of truth.
- Long-lived compiler state stores handles rather than raw pointers into reallocatable storage.
- An `ExpressionId` is stable within one AST representation snapshot. A structural rewrite may deliberately replace IDs, but it must publish and apply an explicit mapping transactionally so no old reference silently changes meaning.
- The current implementation may use `id == dense array index`; higher layers must not turn that physical relationship into a permanent semantic contract.
- Passes should progressively stop depending on `program->expressions[id]`-style storage details when a small accessor/visitor boundary can express the semantic operation.
- Node-kind-specific child relationships remain explicit (`lhs/rhs`, `condition/then/else`, arguments, etc.); do not force every AST node into one generic linked-tree container.

This distinction matters because cast normalization currently rebuilds the expression arena. “Stable ID” therefore means a reliable handle inside a representation version, not an immutable identity that survives every compiler transformation without remapping.

## 4. FunctionBody as the ownership seam / FunctionBody 作为 ownership 边界

Function-local semantic state has a natural lifetime and ownership boundary:

```text
TranslationUnit
├── types / records / enums / globals / declarations
└── Function
    ├── signature
    └── FunctionBody
        ├── expressions
        ├── statements
        ├── blocks
        ├── locals
        ├── cleanup contexts
        └── inline asm / local labels
```

The first step does not need to move storage. A lightweight `FunctionBodyView` defines ownership and traversal while existing program-wide arenas remain canonical.

第一步不必移动任何存储。轻量 `FunctionBodyView` 定义函数体 ownership 与遍历边界，底层仍使用当前 Program-wide arenas。

The ExpressionId traversal/remap seam and FunctionBody ownership seam are related but distinct: traversal owns expression-reference relationships, while FunctionBody owns which reachable structural and semantic graph nodes belong to one function.

A later storage migration is allowed only after callers depend on the FunctionBody interface rather than raw global arenas.

## 5. Activated Core IR shadow seam / 已启用的 Core IR 影子边界

Real backend pressure has now justified activating the previously reserved seam. Direct AST→RV64 lowering accumulated target-neutral execution responsibilities such as evaluation order, CFG construction, temporary materialization, cleanup traversal and aggregate execution rules.

The ownership direction is now:

```text
source
  ↓
Parser + Sema
  ↓
Semantic AST / FunctionBody
  ↓
AST normalization and language-semantic lowering
  ↓
──────────── AST / Core boundary ────────────
  ↓
Core IR
  ↓
──────────── Core / ABI boundary ────────────
  ↓
Target ABI lowering
  ↓
──────────── ABI / machine boundary ─────────
  ↓
backend placement / Machine lowering
  ↓
RV64 assembly or future object emission
```

The boundaries are ownership rules rather than copies of another compiler's representation stack:

- Semantic AST owns what the C/GNU C program means.
- Core IR owns the execution facts that remain after source-language rules are resolved while lower-layer implementation choices stay open.
- TargetABI owns calling-convention classification and abstract argument/return placement.
- Machine lowering owns physical registers, stack addressing, instruction selection, spelling and encoding.

Language-specific rules must be resolved above the AST/Core seam. Their resulting facts cross the seam only when a real lower consumer still needs them. Core IR should not reinterpret source constructs merely to recover C semantics that the frontend could have made explicit.

Target machine placement remains below the Core/ABI and ABI/machine seams: physical register names, frame offsets, spill slots, saved-register layout, instruction spelling and encoding do not belong to Core IR.

The checked-in Core IR remains shadow-only. It proves a compact function representation, verifier, deterministic dump and a bounded normalized-FunctionBody lowering. The default assembly path is still unchanged:

```text
normalized Semantic AST
        ↓
existing RV64 backend
        ↓
assembly
```

The shadow is now wired into the real compiler after normalized FunctionBody validation and before RV64 layout. Lowering distinguishes `OK`, valid-but-`UNSUPPORTED`, and actual `ERROR`; optional shadow mode skips unsupported functions, while strict mode turns them into a CI-visible coverage failure. Supported shadow compilation must remain byte-identical to the default RV64 output. The temporary environment switch is migration plumbing rather than a public compiler interface. See `core-ir-v0-shadow.md` for the exact current contract.

## 6. DataLayout, BackendLayout and ABI boundaries / DataLayout、BackendLayout 与 ABI 边界

Do not merge all target-dependent facts into one mutable `TargetLayout` object.

不要把所有 target-dependent 信息塞进一个大而可变的 `TargetLayout`。

### DataLayout

`sizeof(type)`, `alignof(type)`, record size/alignment, field offset and bit-field placement are target-dependent C semantic facts. They are exposed as read-only queries and may be used above or across the Core IR seam.

### Backend placement / FrameLayout

Local stack offsets, frame size, outgoing argument area, temporary slots, spills and saved-register placement are backend decisions below Core IR. Semantic nodes must not own these mutable results.

### TargetABI

TargetABI owns ABI classification and abstract argument/return locations. It should answer concepts such as `IGNORE`, integer slot, FP slot, aggregate chunks, or indirect passing.

Assembly register spellings such as `a0`, `a1`, `fa0` and concrete stack addressing belong to the RV64 emitter / register information layer, not to semantic ABI classification.

Caller, callee entry, frame accounting and return lowering must consume one ABI source of truth.

## 7. InitPlan and ConstEval / InitPlan 与 ConstEval

Initializer syntax and C initializer semantics are above Core IR. A future `InitPlan` should describe semantic effects such as zero ranges, scalar stores, relocations and aggregate copies without freezing one permanent container representation.

Global initialization may lower from InitPlan directly to object-data/relocation emission, while local initialization may lower to Core IR operations later.

Constant-expression consumers should converge on one ConstEval semantic service. Literal token decoding remains parser/lexer work; expression constant semantics must not be reimplemented separately by arrays, enums, attributes, static assertions, and initializers.

## 8. Replaceable subsystem strategies / 可替换的子系统策略

The same rule applies beyond AST storage. Preserve local replacement seams where change is plausible:

- symbol lookup semantics vs linear vector / hash table / StringId implementation;
- string identity vs borrowed source slice / interned storage;
- type semantics vs current value representation / possible future canonical TypeId storage;
- DataLayout queries vs recursive computation / lazy cache / side table;
- instruction semantics vs direct textual assembly / future MachineIR / object encoding;
- diagnostics vs CLI text formatting;
- source locations vs current offsets / future FileId and macro-expansion locations;
- allocation lifetime vs malloc/realloc / future TU, Function and scratch arenas.

Do not implement these abstractions speculatively. Merely avoid making current representation assumptions part of unrelated semantic APIs.

## 9. Evidence-driven replacement / 证据驱动替换

A data structure or algorithm changes because profiling, correctness pressure, a new target, or a required compiler transformation provides evidence.

Examples:

- keep dense expression arenas while sequential traversal is cheap and stable;
- introduce hash-backed symbols only when lookup cost or correctness pressure justifies it;
- use an intrusive instruction list if future IR optimization requires frequent insertion/removal;
- add BitSet/WorkList only when CFG/dataflow passes exist;
- replace direct assembly helpers with MachineIR only when target lowering or object emission requires a stored machine representation.

Real workloads drive representation choices just as real workloads drive language capability priorities.

真实软件不仅驱动语言能力，也驱动数据结构和算法选择。

## 10. Current migration rule / 当前迁移规则

The representation work has progressed through cooperating seams rather than a wholesale rewrite:

```text
canonical ExpressionId traversal/remap
        +
FunctionBody logical ownership
        +
DataLayout / TargetABI ownership seams
        ↓
normalized FunctionBody
        ↓
Core IR scalar lowering
        ↓
tri-state shadow + verify
        ↓
existing RV64 backend remains production owner
```

The current Core slice does not move FunctionBody storage or replace the AST→RV64 production path. It also deliberately does not introduce memory operations, SSA, a pass manager, alias analysis or Machine IR.

The pipeline shadow consumes the real canonical FunctionBody rather than a source-shaped test approximation. Parser normalization details such as the trailing default return are handled at the AST/Core seam without widening Core semantics merely to mirror parser storage.

The next widening must be justified by a real lowering case. Before the first Core `LOAD`/`STORE` lands, define only the minimum object/address/volatile contract required by that case. Unsupported forms remain outside the shadow coverage rather than forcing speculative IR features.

After each structural slice passes focused, Foundation and unchanged real-program gates, reread the affected code and surrounding compiler before choosing the next ownership boundary. This is an evidence-driven migration, not a mechanical checklist.
