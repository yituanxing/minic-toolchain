#!/usr/bin/env python3
from pathlib import Path

# CFG-only helper evaluation with callee parameter facts. Runtime expression
# lowering is unchanged; unknown/impure arguments remain fail-closed.
p = Path("src/core/core_lower_internal.h")
text = p.read_text()
old = '''    MinicCoreLocalIntegerConstant *local_integer_constants;
    MinicCoreBlockId *statement_blocks;
'''
new = '''    MinicCoreLocalIntegerConstant *local_integer_constants;
    unsigned int cfg_constant_call_depth;
    MinicCoreBlockId *statement_blocks;
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"constant-call depth context anchor: expected one, found {count}")
p.write_text(text.replace(old, new, 1))

p = Path("src/core/core_lower.c")
text = p.read_text()

anchor = '''static bool core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"constant-call helper anchor: expected one, found {count}")

guard_old = '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
'''
guard_new = '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression == NULL || value == NULL ||
        context->cfg_constant_call_depth >= 16U ||
        expression->kind != MINIC_EXPRESSION_CALL ||
'''
count = text.count(guard_old)
if count != 1:
    raise SystemExit(f"constant-call recursion guard anchor: expected one, found {count}")
text = text.replace(guard_old, guard_new, 1)

eval_old = '''                !minic_const_eval_integer(program,
                                          context->target,
                                          statement->expression,
                                          &returned) ||
'''
eval_new = '''                !core_cfg_eval_callee_return_expression(context,
                                                        callee,
                                                        expression,
                                                        statement->expression,
                                                        &returned) ||
'''
count = text.count(eval_old)
if count != 1:
    raise SystemExit(f"constant-call return evaluator anchor: expected one, found {count}")
text = text.replace(eval_old, eval_new, 1)

insert_anchor = "static MinicCoreLowerStatus lower_condition_branch("
pos = text.find(insert_anchor)
if pos < 0:
    insert_anchor = "static MinicCoreLowerStatus\nlower_condition_branch("
    pos = text.find(insert_anchor)
if pos < 0:
    raise SystemExit("lower_condition_branch anchor missing")

# The evaluator helper is intentionally inserted very early in core_lower.c so
# lower_condition_branch and later CFG helpers can use it.  Declare the two
# local-fact routines it consumes at the same insertion point; declaring them
# at core_cfg_constant_inline_call is too late on current source ordering.
helper = r'''static void core_local_constant_set(MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    const MinicConstValue *value);
static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value);

static bool core_cfg_eval_callee_return_expression(
    const MinicCoreLowerContext *caller_context,
    const MinicFunction *callee,
    const MinicExpression *call_expression,
    MinicExpressionId return_expression,
    MinicConstValue *value) {
    MinicCoreLowerContext callee_context;
    MinicCoreLocalIntegerConstant *facts = NULL;
    size_t parameter_index;
    bool success;

    if (caller_context == NULL || caller_context->body == NULL ||
        caller_context->body->program == NULL || caller_context->target == NULL ||
        callee == NULL || call_expression == NULL || value == NULL ||
        call_expression->kind != MINIC_EXPRESSION_CALL ||
        callee->parameter_count != call_expression->value.call.argument_count ||
        caller_context->cfg_constant_call_depth >= 16U) {
        return false;
    }
    if (callee->local_count != 0U) {
        facts = (MinicCoreLocalIntegerConstant *)calloc(
            callee->local_count, sizeof(*facts));
        if (facts == NULL) {
            return false;
        }
    }
    callee_context = *caller_context;
    callee_context.source_function = callee;
    callee_context.local_integer_constants = facts;
    callee_context.cfg_constant_call_depth =
        caller_context->cfg_constant_call_depth + 1U;

    for (parameter_index = 0U; parameter_index < callee->parameter_count;
         ++parameter_index) {
        MinicConstValue argument;
        MinicLocalId local_id;

        if (!minic_type_is_integer(callee->parameter_types[parameter_index])) {
            continue;
        }
        local_id = callee->local_begin + parameter_index;
        if (callee->is_integer_specialization &&
            callee->specialization_integer_known[parameter_index]) {
            argument.type = callee->parameter_types[parameter_index];
            argument.bits = callee->specialization_integer_bits[parameter_index];
            core_local_constant_set(&callee_context, local_id, &argument);
            continue;
        }
        if (!core_const_eval_integer_with_locals(
                caller_context,
                call_expression->value.call.arguments[parameter_index],
                &argument)) {
            continue;
        }
        core_local_constant_set(&callee_context, local_id, &argument);
    }

    success = core_const_eval_integer_with_locals(
        &callee_context, return_expression, value);
    free(facts);
    return success;
}

'''
text = text[:pos] + helper + text[pos:]
p.write_text(text)
print("MINIC_CONSTANT_CALL_PARAMETER_FACTS_V0=APPLIED depth_limit=16 specialization_seed=1")
