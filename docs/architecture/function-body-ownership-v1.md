# FunctionBody logical ownership v1 / FunctionBody 逻辑 ownership v1

## 1. Why this boundary exists / 为什么需要这个边界

MiniC currently stores expressions, statements, blocks, locals, cleanup contexts and inline-asm objects in translation-unit-wide arrays. Those arrays are storage containers; they do not define semantic ownership.

MiniC 当前把 expression、statement、block、local、cleanup context 和 inline asm 放在 TU 级数组里。数组只负责物理存储，不能用来定义语义 ownership。

A function already has a canonical semantic root: `MinicFunction.body_block`. The parser creates that block when entering a definition, and the RV64 backend starts function-body emission from the same root. FunctionBody therefore does not need `expression_begin/count`, `statement_begin/count`, or similar permanent range promises.

函数已经天然存在唯一语义根：`MinicFunction.body_block`。Parser 在函数定义开始时创建它，RV64 backend 也从同一 root 开始遍历函数体。因此 FunctionBody 不需要新增 `expression_begin/count`、`statement_begin/count` 之类把当前数组布局固化成契约的字段。

## 2. Real correctness pressure / 真实正确性压力

The old AST verifier validates many IDs only against translation-unit-global ranges. That leaves legal representations of semantically impossible programs.

旧 AST verifier 对不少 ID 只做 TU 全局范围检查，因此一些语义上不可能成立的关系仍能通过结构验证。

Two focused examples now freeze this evidence:

```text
Function A local expression
    -> LocalId owned by Function B
```

When both locals have the same type, the old verifier can still accept the expression because the LocalId exists globally and its type matches.

```text
Function A goto
    -> LABEL statement owned by Function B
```

The old verifier can accept this because the target is globally a valid LABEL statement.

`minic_c0_program_validate_function_body_ownership()` rejects both relations while preserving ordinary AST verification as a separate concern.

## 3. Tree structure and semantic graph are different / 树结构与语义图分开

The FunctionBody logical structure is approximately:

```text
Function
└── root Block
    ├── Statement
    │   ├── expression roots
    │   │   └── Expression tree
    │   └── child Block(s)
    └── Statement ...
```

Structural tree edges include:

- `Block -> Statement`;
- `if -> then/else Block`;
- `while/switch -> body Block`;
- statement expression -> nested Block;
- compound literal -> initializer Block;
- Expression -> child Expression edges from the canonical AST traversal layer.

Semantic graph edges are different and must not be represented as generic tree children:

- `goto -> LABEL Statement`;
- label address -> LABEL Statement;
- inline-asm goto -> LABEL Statement;
- cleanup lifetime -> cleanup Expression;
- call -> Function;
- symbol/type/global references.

A FunctionBody validation pass follows both categories, but preserves their distinct meaning.

## 4. Arena membership is not FunctionBody ownership / Arena 成员不等于函数体成员

A critical invariant is:

```text
expression arena membership
    != reachable FunctionBody expression
```

Parser-time semantic expressions can be created for `_Static_assert`, GNU alignment attributes and other constant-evaluation consumers. ConstEval may consume them without attaching them to any function root.

Therefore FunctionBody validation intentionally does **not** require every `ExpressionId` in `program->expressions[]` to belong to one function.

这也是为什么不能用 expression 的连续数组区间定义 FunctionBody。合法的 parse-time/orphan semantic expression 必须继续存在。

## 5. Current physical strategy / 当前物理实现策略

The v1 validator uses temporary dense scratch storage:

```text
block owner map
statement owner map
local owner map
cleanup-context owner map
inline-asm owner map
expression seen-generation table
block worklist
statement worklist
expression worklist
```

This is an implementation strategy, not an API contract.

It is appropriate today because the Program representation is dense, traversal is sequential, and temporary arrays make ownership checks simple and cache-friendly. If profiling later shows another representation is better, FunctionBody callers should not change.

当前使用数组不是“MiniC 永远选择数组树”，而只是现阶段最适合 ownership validator 的算法实现。

The expression seen-generation counter is independent of `FunctionId` encoding. It advances as scratch state and clears the generation table only on wraparound, so even the validator's internal algorithm does not need to assume that a FunctionId can double as a traversal generation.

## 6. Ownership rules frozen by v1 / v1 已冻结的 ownership 规则

For a defined function:

- its root `body_block` is the FunctionBody root;
- a structural Block belongs to at most one FunctionBody;
- a structural Statement belongs to at most one FunctionBody;
- existing `local_begin/local_count` ranges define the current LocalId ownership until local storage is migrated separately;
- every reachable local expression must reference a LocalId owned by the same function;
- a compound literal's backing LocalId must belong to the same function;
- statement-expression and compound-literal nested blocks remain in the same FunctionBody;
- goto, inline-asm goto and label-address targets must resolve inside the same FunctionBody;
- a non-root cleanup context referenced by a reachable statement belongs to one FunctionBody; the same context may be reused inside that function, but not by another function;
- a reachable inline-asm object belongs to one FunctionBody; the same object may be reused inside that function, but not by another function;
- expression nodes are traversed by reachability, but are not globally required to have one permanent function owner.

The final point intentionally keeps room for future expression sharing/interning and parser-time transient expressions.

## 7. Cleanup and inline-asm auxiliary ownership / Cleanup 与 inline asm 辅助 ownership

Cleanup contexts and inline-asm objects are logically function-local semantic state even though they currently live in Program-wide arrays. Direct owner identity is now part of the v1 validator rather than a deferred follow-up.

Cleanup validation reuses the existing canonical lifetime rule:

```text
current cleanup context
    -> parent
    -> ...
    -> stop context
```

The validator calls `minic_c0_cleanup_context_reaches()` rather than reimplementing cleanup reachability. Every non-root context encountered along the referenced lifetime chain is claimed by the current FunctionBody, and its cleanup expression is traversed as a reachable expression.

Inline-asm validation claims the referenced `MinicInlineAsmId` for the current FunctionBody, traverses its input/output expressions, and requires every asm-goto label target to resolve to a Statement owned by the same function.

This auxiliary ownership is implemented through the same FunctionBody walk; no second block/statement walker was added.

## 8. Compiler pipeline position / 在编译管线中的位置

The current child branch validates FunctionBody ownership in the compiler pipeline:

```text
Parser + Sema
    ↓
Parsed AST verifier
    ↓
FunctionBody ownership
    ↓
cast normalization
    ↓
Normalized AST verifier
    ↓
FunctionBody ownership
    ↓
RV64 layout
```

Running the ownership check on both sides of normalization also checks that a structural rewrite preserves the FunctionBody invariants.

Long term this contract should converge with the AST verifier rather than create two conceptual verification systems. The current separate call is a bounded migration seam.

## 9. Frozen evidence / 已冻结证据

Focused tests prove:

- valid multi-function ownership passes;
- same-type cross-function `LocalId` references are rejected even though the older global verifier can accept them;
- cross-function goto targets are rejected;
- orphan parse-time-style constant expressions remain legal;
- cleanup and inline-asm auxiliary state stays within the reachable FunctionBody graph.

The production integration is no longer staged. `ast_traversal.c` and `function_body.c` are listed directly in the checked-in Makefile, the temporary source-list materializer has been removed, and the host source-inventory gate validates the real repository source list.

The clean FunctionBody head `2306b3a00f2b9444e3e7fd68e089c913063a249b` passed:

- dedicated FunctionBody focused validation;
- compiler-path host validation and frozen Foundation checks;
- the official full compiler gate, including RV64 and unchanged real-program regressions.

The final large pressure check overlaid the FunctionBody architecture delta onto the previously proven Linux semantic tail without merging discovery staging into the product branch. Run `31764081317` hard-asserted both compiler status and non-empty assembly output and completed with:

```text
cached_tu_status=0
FULL_TU_PASS lines=90928
```

During this revalidation the legacy discovery-only cleanup-remap materializer was intentionally retired in the runner workspace because `cast_normalization.c` now consumes the canonical external-ExpressionId remap API. Reintroducing the old hand-written remap path would create a second owner rather than add coverage.

## 10. Relationship to future Core IR / 与未来 IR 的关系

FunctionBody creates a natural future lowering unit:

```text
Function signature
    +
FunctionBody logical tree/graph
    ↓
future Core IR Function
```

The important point is not that the physical AST must become a pointer tree. The logical function tree and semantic graph are now increasingly independent of the current Program-wide array storage.

That keeps the future AST -> Core IR seam explicit while allowing the storage representation to change independently when real profiling or transformation pressure justifies it.
