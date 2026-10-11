#!/usr/bin/env python3
"""Keep speculative local constants from leaking backwards into an IF guard.

Core builds the then/else bodies before lowering the condition terminator at
the earlier condition_block. The bodies may update a non-SSA per-local fact
table, but those updates cannot dominate the condition block.

Linux 6.6.143 genl_op_from_split() is a concrete witness:
  __ret_warn_on = !!(!cnt);
  if (__builtin_expect(!!(__ret_warn_on), 0)) do { asm("ebreak"); } while (0);
Before this barrier, lowering the THEN body can change the local facts and
core_const_eval_integer_with_locals() later folds the guard to true, emitting
an unconditional jump to ebreak.

Only the real dynamic-IF Core seam is affected. Source-language compile-time
constant conditions already return from lower_if before reaching this seam.
"""
from pathlib import Path

path = Path("src/core/core_lower.c")
src = path.read_text()
start_marker = "static MinicCoreLowerStatus\nlower_if(MinicCoreLowerContext *context,"
end_marker = "static bool internal_while_label_pair("
if src.count(start_marker) != 1:
    raise SystemExit(f"DOMINANCE_IF=ERROR function_count={src.count(start_marker)}")
start = src.index(start_marker)
end = src.index(end_marker, start)
fragment = src[start:end]
old = """    status = lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
"""
new = """    /* DOMINANCE_IF_GUARD_FACT_BARRIER: speculative body facts do not
       dominate the earlier condition_block. Rebuild the runtime predicate
       with only immutable source/specialization facts, not body updates. */
    core_local_constants_clear_known(context);
    status = lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
"""
n = fragment.count(old)
if n != 1:
    raise SystemExit(f"DOMINANCE_IF=ERROR guarded_condition_count={n}")
fragment = fragment.replace(old, new, 1)
path.write_text(src[:start] + fragment + src[end:])
print("DOMINANCE_IF=APPLIED body_facts_cleared_before_runtime_guard=true")
