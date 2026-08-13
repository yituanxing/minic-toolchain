# AST traversal and ExpressionId ownership slice / AST 遍历与 ExpressionId ownership 第一刀

## Goal / 目标

Solve the observed cast-normalization reference-integrity defect while establishing the first representation-neutral AST tree/reference seam.

解决已经由 unchanged Linux 暴露的 cast normalization 引用完整性缺陷，同时建立第一块不依赖物理存储的 AST 树/引用边界。

This slice does **not** move FunctionBody storage, add Core IR, redesign the parser, change DataLayout, or change RV64 ABI/code generation.

本切片**不**迁移 FunctionBody 存储、不引入 Core IR、不重写 Parser、不修改 DataLayout，也不修改 RV64 ABI/codegen。

## Evidence / 证据

The current normalized-expression rewrite rebuilds the translation-unit expression arena and maps old ExpressionId values to new values. It already remaps expression children, statement roots, the legacy program return root, and inline-asm operands, but formal Foundation does not remap `MinicCleanupContext.cleanup_expression`.

当前 normalization 会重建整个 TU 的 expression arena，并建立 old ExpressionId -> new ExpressionId 映射。formal Foundation 已经手工 remap expression child、statement root、legacy program return root、inline-asm operand，但没有 remap `MinicCleanupContext.cleanup_expression`。

Unchanged Linux exposed the concrete failure: after expression reindexing, a cleanup expression retained an old ID that was still numerically in range but referred to a different expression. Range-only verification therefore could not detect stale identity.

unchanged Linux 已经暴露真实后果：重编号后 cleanup expression 保留旧 ID；该数字仍在合法范围内，却指向了另一表达式，因此仅靠范围验证无法发现 stale identity。

The verifier also does not currently include `cleanup_contexts` in its program-storage invariant, confirming that external ExpressionId ownership is not represented canonically.

Verifier 当前也没有把 `cleanup_contexts` 纳入 program-storage invariant，进一步证明 external ExpressionId ownership 尚无 canonical owner。

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

## Proposed API seam / 建议 API 边界

Introduce a small `src/frontend/ast_traversal.[ch]` module. The exact spelling may change during implementation, but the semantic contract is:

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
```

The API expresses relationships, not storage. Callers must not rely on `ExpressionId == dense array index` beyond the current storage implementation.

该 API 表达的是节点关系，不是数组布局。调用者不得把 `ExpressionId == dense array index` 当成长期语义契约。

## Normalization migration / Normalization 迁移

`cast_normalization.c` should stop owning a second node-kind child map.

For every non-cast expression:

```text
copy node
  -> visit canonical child references
  -> remap each child through old->new mapping
  -> append rewritten node
```

Cast elimination still owns the semantic rewrite itself, but operand identity lookup uses the same mapping rules.

External references use one canonical traversal rather than independent statement / inline-asm / cleanup loops.

## Transactionality / 事务性

Preserve the current contract: failed normalization must leave the original program unchanged.

Recommended sequence:

```text
1. build rewritten expression arena + mapping
2. validate every external ExpressionId against the complete mapping without mutation
3. only after all validation succeeds, commit the new expression arena
4. remap the already-validated external references through the same traversal
```

The validation pass must guarantee that the final remap cannot fail midway. Do not trade the current transactional property for a simpler loop.

保持现有契约：normalization 失败时原 Program 必须完全不变。不能为了简化 remap 而引入半更新状态。

## Verifier strengthening / Verifier 加强

At minimum add invariants for cleanup storage and identity:

- `cleanup_contexts/count/capacity` obey normal storage invariants;
- every cleanup context refers only to an earlier valid parent or root;
- every cleanup expression is a valid ExpressionId;
- statement cleanup/stop contexts are valid and their reachability relationship is well formed where required.

The verifier should progressively consume the same relationship model instead of creating another independent list of ExpressionId owners.

Verifier 后续应逐步消费同一 relationship model，避免再形成第二份 ExpressionId owner 清单。

## Focused regression / 聚焦回归

Extend `tests/frontend/ast_contract_test.c` with a case that creates:

```text
integer expression
      -> integer cast
      -> CleanupContext.cleanup_expression
```

The integer cast normalization currently expands the expression topology. The test must prove that after normalization the cleanup context points to the newly mapped expression, not merely to an in-range ID.

Required sequence:

```text
parsed verifier PASS
normalize PASS
cleanup ExpressionId changed to mapped normalized node
normalized verifier PASS
```

Also add malformed cleanup-context verifier cases so storage/parent/expression ownership cannot silently regress.

## Build and validation / 构建与验收

Add the traversal implementation to both normal production sources and the AST-contract test source set.

Validation order:

```text
focused AST contract
-> host debug/release/sanitize gates
-> frozen frontend/C0 gates
-> tiny-AES / cJSON / Parson / linenoise / SDS / Lua
-> unchanged Linux 6.6.143 init/main.i
```

The final Linux acceptance remains:

```text
cached_tu_status=0
FULL_TU_PASS lines=90928
```

If any existing gate moves backward, stop and inspect ownership rather than adding a compatibility special case.

## What follows / 后续

After this slice is green, re-read the affected AST/parser/normalization/verifier code globally.

Only then decide whether the next step is:

1. a lightweight FunctionBody View built from `function.body_block` and the canonical traversal relationships;
2. DataLayout vs backend placement separation; or
3. another ownership defect exposed by the refactor.

Do not add `expression_begin/count` or similar range fields merely to make FunctionBody traversal easy. Function ownership should be defined by semantic roots/relationships, not by permanently promising that current global arrays remain contiguous ranges.
