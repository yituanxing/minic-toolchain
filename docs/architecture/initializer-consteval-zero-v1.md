# Initializer ConstEval zero convergence v1

This slice removes an initializer-specific copy of integer constant-zero
semantics. Aggregate initialization previously recognized only a literal integer
zero (plus a recursively wrapped pointer cast) when deciding whether zero/null
values may initialize aggregate members. That made initializer behavior depend on
syntax shape instead of the compiler's canonical integer constant evaluator.

Integer zero detection now uses `minic_const_eval_integer()` followed by
`minic_const_value_is_zero()`, with the parser's existing `TargetInfo` passed
explicitly. As a result, any integer constant expression already supported by
ConstEval has the same zero semantics in aggregate initialization. The focused
regression deliberately uses `1 - 1` and `(void *)(2 - 2)` so the former
literal-only implementation cannot pass accidentally.

The outer pointer-cast handling remains intentionally narrow. This slice strips
the pointer cast and delegates the underlying integer-zero question to ConstEval;
it does not redesign the broader C null-pointer-constant contract.
`minic_c0_expression_is_null_pointer_constant_v0()` therefore remains unchanged
and should be reassessed separately after a global reread rather than folded into
this initializer change.

This is semantic-owner convergence, not an InitPlan framework. Global and local
initializer representation still needs a separate ownership review: global
objects currently combine scalar value slots, relocations and zero-initialized
state, while local aggregate initialization lowers directly to statements. A
future InitPlan should be introduced only if that reread shows it removes real
duplication after constant semantics have first converged.

The slice preserves the deferred Core IR seam by keeping C constant-expression
semantics above the seam in the frontend. No target placement or machine facts
are introduced.

The candidate passed the official full compiler gate and frozen Linux 6.6.143
hybrid pressure run `31787594101`, which rebuilt the proven semantic,
ABI and layout ownership stack, applied this initializer patch cleanly, and
completed with `INITIALIZER_CONSTEVAL_HYBRID=1`, `cached_tu_status=0`, and
`FULL_TU_PASS lines=90928`.
