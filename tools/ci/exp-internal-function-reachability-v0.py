#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
old = '''    return source->is_defined && source->is_internal && source->is_inline &&
           !minic_inline_source_has_noncore_root(program, source_id);
'''
new = '''    /* M179_INTERNAL_CORE_REACHABILITY: parser-level is_referenced is not an
     * executable root for any internal function once Core has been lowered.
     * Reachable CALL/FUNCTION_ADDRESS instructions pull static functions back
     * into the graph; aliases, entry/force_emit and global relocations remain
     * explicit non-Core roots.  This also prunes static non-inline bodies whose
     * only source-level calls disappeared after constant-CFG folding. */
    return source->is_defined && source->is_internal &&
           !minic_inline_source_has_noncore_root(program, source_id);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one internal reachability predicate, found {count}")
p.write_text(text.replace(old, new, 1))
print("M179_INTERNAL_CORE_REACHABILITY=APPLIED")

# Keep specialization pruning and backend emission on the same set of legal
# source-label re-entry roots.
label_patch = Path("tools/ci/exp-core-reachability-label-roots-v0.py")
exec(compile(label_patch.read_text(), str(label_patch), "exec"))

# M181_CONSTANT_CONDITION_BRANCH:
# lower_if() must sometimes keep the general CFG path because a cleanup edge is
# attached to the source statement.  In that path it lowers the then/else bodies
# before lowering the condition.  When the condition is nevertheless provably
# constant, make the Core edge direct so the existing reachable-block pass can
# discard the unselected body (including dead BUILD_BUG/compiletime_assert
# calls) without bypassing cleanup construction or legal label re-entry roots.
lower = Path("src/core/core_lower.c")
source = lower.read_text()
marker = "M181_CONSTANT_CONDITION_BRANCH"
if marker in source:
    raise SystemExit("M181 constant condition branch already present")
function_anchor = '''static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
'''
function_start = source.find(function_anchor)
if function_start < 0:
    raise SystemExit("lower_condition_branch anchor not found")
function_end = source.find(
    "\nstatic bool core_switch_label_has_function_reentry", function_start)
if function_end < 0:
    raise SystemExit("lower_condition_branch end anchor not found")
body = source[function_start:function_end]
anchor = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
'''
insert = '''    /* M181_CONSTANT_CONDITION_BRANCH: collapse a target/local-aware integer
       condition to one executable edge even when lower_if had to construct the
       cleanup-sensitive general CFG first.  Backend Core reachability then owns
       removal of the unselected body. */
    if (minic_type_is_integer(expression->type) && context->target != NULL) {
        MinicConstValue constant_condition;
        bool constant_is_zero;

        if (core_const_eval_integer_with_locals(
                context, expression_id, &constant_condition) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &constant_condition,
                                      &constant_is_zero)) {
            return set_branch(context,
                              context->block_id,
                              span,
                              constant_is_zero ? when_false : when_true);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
'''
count = body.count(anchor)
if count != 1:
    raise SystemExit(f"expected one lower_condition_branch logical-not anchor, found {count}")
body = body.replace(anchor, insert, 1)
source = source[:function_start] + body + source[function_end:]
lower.write_text(source)
print("M181_CONSTANT_CONDITION_BRANCH=APPLIED")
