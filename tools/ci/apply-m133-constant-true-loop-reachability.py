#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M133_CONSTANT_TRUE_LOOP_REACHABILITY'
if marker in text:
    print('M133 constant-true loop reachability already staged')
    raise SystemExit(0)

helper_anchor = '''static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context,
'''
if helper_anchor not in text:
    raise SystemExit('lower_while anchor changed')

helper = '''/* M133_CONSTANT_TRUE_LOOP_REACHABILITY: a loop condition that Sema/const-eval
   proves to be a nonzero integer constant has no natural false edge. Keep this
   a target-neutral CFG fact: explicit break edges still make the synthetic exit
   reachable, while an otherwise unreachable exit terminates the enclosing path. */
static bool core_loop_condition_is_constant_true(const MinicCoreLowerContext *context,
                                                 MinicExpressionId expression_id) {
    MinicConstValue value;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        !minic_const_eval_integer(
            context->body->program, context->target, expression_id, &value) ||
        !minic_const_value_is_zero(
            context->body->program, context->target, &value, &is_zero)) {
        return false;
    }
    return !is_zero;
}

'''
text = text.replace(helper_anchor, helper + helper_anchor, 1)

condition_needle = '''    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID) {
        /* C defines an omitted for-condition as true. Keep an explicit Core
           condition block so break/backedge ownership remains identical to
           the conditional-loop path. */
        status = set_branch(context, condition_block, statement->span, body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = lower_condition_branch(context,
                                        statement->expression,
                                        statement->span,
                                        body_block,
                                        exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
if condition_needle not in text:
    raise SystemExit('loop condition seam changed')
condition_replacement = '''    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID ||
        core_loop_condition_is_constant_true(context, statement->expression)) {
        /* C defines an omitted for-condition as true, and a proven nonzero
           integer constant has the same CFG reachability. Keep an explicit
           Core condition block so break/backedge ownership remains identical. */
        status = set_branch(context, condition_block, statement->span, body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = lower_condition_branch(context,
                                        statement->expression,
                                        statement->span,
                                        body_block,
                                        exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
text = text.replace(condition_needle, condition_replacement, 1)

exit_needle = '''    context->block_id = exit_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID && normalized_for &&
        !core_block_has_predecessor(context->function, exit_block)) {
'''
if exit_needle not in text:
    raise SystemExit('M132 loop exit seam changed')
exit_replacement = '''    context->block_id = exit_block;
    /* M133_CONSTANT_TRUE_LOOP_REACHABILITY: reachability, not syntax spelling,
       owns loop fallthrough. Normal variable/false-capable conditions already
       contribute a false edge; omitted and constant-true conditions do not. */
    if (!core_block_has_predecessor(context->function, exit_block)) {
'''
text = text.replace(exit_needle, exit_replacement, 1)
path.write_text(text)

Path('tests/compiler/c0/m133_constant_true_loop_reachability.c').write_text(r'''static int while_terminal(int value) {
    while (1) {
        if (value)
            return value;
    }
}

static int do_while_terminal(int value) {
    do {
        if (value)
            return value;
    } while (1);
}

static int while_breakable(int value) {
    while (1) {
        if (value)
            break;
        value = 1;
    }
    return value;
}

static int while_false(int value) {
    while (0)
        value = 99;
    return value;
}

int main(void) {
    return while_terminal(1) == 1 &&
           do_while_terminal(1) == 1 &&
           while_breakable(0) == 1 &&
           while_false(7) == 7 ? 0 : 1;
}
''')

print('M133 constant-true loop reachability owner and strict regression staged')
