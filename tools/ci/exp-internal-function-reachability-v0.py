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

lower = Path("src/core/core_lower.c")
source = lower.read_text()

# M181_CONSTANT_CONDITION_BRANCH:
# lower_if() must sometimes keep the general CFG path because a cleanup edge is
# attached to the source statement.  In that path it lowers the then/else bodies
# before lowering the condition.  When the condition is nevertheless provably
# constant, make the Core edge direct so the existing reachable-block pass can
# discard the unselected body without bypassing cleanup construction.
marker = "M181_CONSTANT_CONDITION_BRANCH"
if marker in source:
    raise SystemExit("M181 constant condition branch already present")
definition_anchor = '''static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false) {
'''
function_start = source.find(definition_anchor)
if function_start < 0:
    raise SystemExit("lower_condition_branch definition anchor not found")
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

# M182_TERMINATING_GUARD_FACTS:
# A no-else guard such as `if (err) goto failed;` has exactly one path that can
# reach the following statement: the condition-false path.  If the condition is
# side-effect-free and the then arm is a single control transfer, no unescaped
# local is changed on that surviving path.  Keep the incoming local-constant
# facts instead of discarding them at the synthetic CFG join.  This is critical
# for Linux feature gates such as `huge = false; ... if (err) goto failed;
# if (huge) ...`, where losing the fact resurrects configuration-dead code.
if "M182_TERMINATING_GUARD_FACTS" in source:
    raise SystemExit("M182 terminating guard facts already present")
if_anchor = '''static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
'''
if_start = source.find(if_anchor)
if if_start < 0:
    raise SystemExit("lower_if definition not found")
if_end = source.find("\nstatic bool internal_while_label_pair", if_start)
if if_end < 0:
    raise SystemExit("lower_if end not found")
if_body = source[if_start:if_end]
old_decl = '''    bool needs_merge;
    bool then_terminated;
'''
new_decl = '''    bool needs_merge;
    bool then_terminated;
    bool preserve_guard_facts;
'''
if if_body.count(old_decl) != 1:
    raise SystemExit(f"lower_if declaration anchor count={if_body.count(old_decl)}")
if_body = if_body.replace(old_decl, new_decl, 1)
setup_anchor = '''    /* BusyBox and ordinary GNU C intentionally leave impossible references in
'''
setup_insert = '''    /* M182_TERMINATING_GUARD_FACTS: preserve incoming facts across a pure,
       no-else guard whose only taken-arm statement leaves the continuation via
       goto/break/continue.  The taken arm cannot merge back, while the surviving
       false path has not modified any local object. */
    preserve_guard_facts = false;
    if (else_source == NULL && then_source->statement_count == 1U &&
        core_cfg_pure_call_argument(context, statement->expression, 0U)) {
        const MinicStatement *guard_statement = minic_c0_program_statement(
            context->body->program, then_source->statements[0]);
        preserve_guard_facts =
            guard_statement != NULL &&
            (guard_statement->kind == MINIC_STATEMENT_GOTO ||
             guard_statement->kind == MINIC_STATEMENT_BREAK ||
             guard_statement->kind == MINIC_STATEMENT_CONTINUE);
    }

    /* BusyBox and ordinary GNU C intentionally leave impossible references in
'''
if if_body.count(setup_anchor) != 1:
    raise SystemExit(f"lower_if setup anchor count={if_body.count(setup_anchor)}")
if_body = if_body.replace(setup_anchor, setup_insert, 1)
pre_clear = '''    core_local_constants_clear_known(context);
    condition_block = context->block_id;
'''
pre_keep = '''    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
    }
    condition_block = context->block_id;
'''
if if_body.count(pre_clear) != 1:
    raise SystemExit(f"lower_if pre-clear anchor count={if_body.count(pre_clear)}")
if_body = if_body.replace(pre_clear, pre_keep, 1)
post_clear = '''    context->block_id = continuation_block;
    core_local_constants_clear_known(context);
    *terminated = !needs_merge;
'''
post_keep = '''    context->block_id = continuation_block;
    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
    }
    *terminated = !needs_merge;
'''
if if_body.count(post_clear) != 1:
    raise SystemExit(f"lower_if post-clear anchor count={if_body.count(post_clear)}")
if_body = if_body.replace(post_clear, post_keep, 1)
source = source[:if_start] + if_body + source[if_end:]

lower.write_text(source)
print("M181_CONSTANT_CONDITION_BRANCH=APPLIED")
print("M182_TERMINATING_GUARD_FACTS=APPLIED")
