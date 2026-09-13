#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''static bool core_inline_asm_immediate_text(\n'''
helper = r'''
static bool core_inline_asm_local_integer_fact_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || context->source_function == NULL || value == NULL ||
        depth > 16U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t local_index;
        const MinicCoreLocalIntegerConstant *fact;

        if (context->local_integer_constants == NULL ||
            local_id < context->source_function->local_begin) {
            return false;
        }
        local_index = local_id - context->source_function->local_begin;
        if (local_index >= context->source_function->local_count) {
            return false;
        }
        fact = &context->local_integer_constants[local_index];
        if (!fact->known || fact->escaped) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &fact->value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        MinicConstValue operand_value;
        if (!core_inline_asm_local_integer_fact_depth(context,
                                                      expression->value.unary.operand,
                                                      &operand_value,
                                                      depth + 1U)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    return false;
}

static bool core_inline_asm_local_integer_fact(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value) {
    return core_inline_asm_local_integer_fact_depth(context, expression_id, value, 0U);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("expected one immediate resolver anchor")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (minic_const_eval_integer(
            context->body->program, context->target, operand->expression, &constant) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value)) {
'''
new = '''    if ((minic_const_eval_integer(
             context->body->program, context->target, operand->expression, &constant) ||
         core_inline_asm_local_integer_fact(context, operand->expression, &constant)) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value)) {
'''
if text.count(old) != 1:
    raise SystemExit("expected one immediate integer const-eval block")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_ASM_LOCAL_INTEGER_FACTS_V0=APPLIED")
