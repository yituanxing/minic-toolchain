# AST traversal and ExpressionId ownership slice / AST 遍历与 ExpressionId ownership 第一刀

## Goal / 目标

Solve the observed cast-normalization reference-integrity defect while establishing the first representation-neutral AST tree/reference seam.

解决已经由 unchanged Linux 暴露的 cast normalization 引用完整性缺陷，同时建立第一块不依赖物理存储的 AST 树/引用边界。

This slice does **not** move FunctionBody storage, add Core IR, redesign the parser, change DataLayout, or change RV64 ABI/code generation.

本切片**不**迁移 FunctionBody 存储、不引入 Core IR、不重写 Parser、不修改 DataLayout，也不修改 RV64 ABI/codegen。

## Evidence / 证据

The normalized-expression rewrite rebuilds the translation-unit expression arena and maps old ExpressionId values to new values. The historical implementation manually remapped expression children, statement roots, the legacy program return root, and inline-asm operands, but omitted `MinicCleanupContext.cleanup_expression`.

当前 normalization 会重建整个 TU 的 expression arena，并建立 old ExpressionId -> new ExpressionId 映射。历史实现曾手工 remap expression child、statement root、legacy program return root、inline-asm operand，却遗漏了 `MinicCleanupContext.cleanup_expression`。

Unchanged Linux exposed the concrete failure: after expression reindexing, a cleanup expression retained an old ID that was still numerically in range but referred to a different expression. Range-only verification therefore could not detect stale identity.

unchanged Linux 已经暴露真实后果：重编号后 cleanup expression 保留旧 ID；该数字仍在合法范围内，却指向了另一表达式，因此仅靠范围验证无法发现 stale identity。

The architecture defect was not merely one missing assignment. External ExpressionId ownership had no canonical enumerator, so every structural rewrite could silently grow another incomplete hand-written remap list.

## Architecture distinction / 架构区分

Two relationships must remain separate:

```text
Expression node
    -> child ExpressionId references
       logical AST tree edges

Program/function semantic state
    -> ExpressionId references
       external graph/root edges
```

Expression children include unary operands, binary lhs/rhs, subscript base/index, conditional operands, call callee/arguments, statement-expression result, and overflow builtin operands.

External references include statement expression fields, inline-asm input/output operands, cleanup expressions, and the current legacy program return root.

Do not represent these as one generic untyped edge list. Tree edges and semantic/root references have different meanings even when both physically contain an `ExpressionId` today.

不要把两者塞进一个无类型 generic edge list。即使今天物理上都保存 `ExpressionId`，逻辑树边与语义/root 引用的含义仍然不同。

## Canonical API seam / Canonical API 边界

The checked-in `src/frontend/ast_traversal.[ch]` module owns the current relationship API:

```c
typedef bool (*MinicExpressionIdRefVisitor)(MinicExpressionId *id, void *context);

bool minic_c0_expression_visit_child_id_refs(
    MinicExpression *expression,
    MinicExpressionIdRefVisitor visitor,
    void *context);

bool minic_c0_program_visit_external_expression_id_refs(
    MinicC0Program *program,
    MinicExpressionIdRefVisitor visitor,
    void *context);

bool minic_c0_program_remap_external_expression_ids(
    MinicC0Program *program,
    const MinicExpressionId *mapping,
    size_t mapping_count);
```

The API expresses relationships, not storage. Callers must not depend on `ExpressionId == dense array index` as a permanent representation rule.

An `ExpressionId` is a stable handle inside one AST representation snapshot. A structural rewrite such as cast normalization may replace handles, but only through an explicit validated mapping; old handles must never silently acquire a different meaning.

## Normalization migration / Normalization 迁移

`cast_normalization.c` no longer owns a second node-kind child map.

For every non-cast expression:

```text
copy node
  -> visit canonical child references
  -> remap each child through old->new mapping
  -> append rewritten node
```

Cast elimination still owns the semantic rewrite itself, but operand identity lookup follows the same old-to-new mapping discipline.

External references use one canonical traversal rather than independent statement / inline-asm / cleanup loops.

## Transactionality / 事务性

The normalization contract remains transactional: failed normalization leaves the original program unchanged.

Current sequence:

```text
1. build rewritten expression arena + mapping
2. validate every external ExpressionId against the complete mapping without mutation
3. remap the already validated external references through the same canonical traversal
4. only then replace the expression arena
```

The validation pass guarantees that the remap phase cannot discover a missing mapping halfway through. The focused regression freezes this property by deliberately corrupting an external cleanup reference and requiring normalization to fail without replacing the original arena or partially changing the cleanup reference.

## Relationship validation / 关系验证

The traversal module validates the storage it must enumerate before visiting external ExpressionId references, including statement, inline-asm and cleanup-context backing arrays and inline-asm operand arrays. It also validates every referenced ID against the complete remap before mutation.

Function-local semantic ownership is a separate layer. `minic_c0_program_validate_function_body_ownership()` follows reachable cleanup chains, inline-asm operands, nested blocks, local references and label targets and rejects cross-function semantic edges.

The general AST verifier remains a separate structural/type verifier today. Long term it should consume the same canonical relationship APIs where useful instead of growing another hand-written ExpressionId-owner list. Do not duplicate traversal logic merely to make the verifier look more centralized before that convergence has a concrete use.

## Focused regression / 聚焦回归

`tests/frontend/ast_traversal_test.c` freezes the Linux-discovered defect with:

```text
integer expression
      -> integer cast
      -> CleanupContext.cleanup_expression
```

The integer cast normalization expands the expression topology. The test proves that after normalization the cleanup context points to the newly mapped expression, not merely to an in-range ID.

A second case corrupts the cleanup ExpressionId before normalization and proves failure is transactional: the original expression arena, count, cast node and cleanup reference remain unchanged.

FunctionBody-focused tests separately freeze cross-function ownership failures and the fact that parse-time/orphan expressions do not have to belong to a function body.

## Build and validation / 构建与验收

`ast_traversal.c` and `function_body.c` are checked-in production sources in the real Makefile; the temporary source-list materializer used during discovery has been removed.

Validation completed in the intended order:

```text
focused traversal / FunctionBody contracts
-> host compiler-path gates
-> frozen Foundation semantics
-> official full compiler gate
-> RV64 and unchanged real-program regressions
-> frozen Linux 6.6.143 init/main.i
```

The final independent Linux revalidation overlaid the architecture delta onto the previously proven discovery semantic tail and hard-asserted:

```text
cached_tu_status=0
FULL_TU_PASS lines=90928
```

The generated `main.s` was also required to be non-empty.

## What follows / 后续

This slice is now an input to the next global reread, not a reason to continue mechanically expanding AST framework APIs.

Possible next boundaries include:

1. DataLayout vs backend placement separation;
2. canonical TargetABI ownership; or
3. another ownership defect exposed by the reread.

Do not add `expression_begin/count` or similar range fields merely to make FunctionBody traversal easy. Function ownership is defined by semantic roots/relationships, not by permanently promising that current global arrays remain contiguous ranges.
