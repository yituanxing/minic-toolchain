#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
helpers = r'''
/* LINUX_CONSTANT_INLINE_CALL_CFG_V0: feature-disabled Linux headers often
 * expose static-inline predicates that are exactly one constant return.  Such
 * a call is safe to consume as a CFG fact when all arguments are side-effect
 * free.  Ordinary expression lowering remains unchanged. */
static bool core_cfg_pure_call_argument(const MinicCoreLowerContext *context,
                                        MinicExpressionId expression_id,
                                        unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || minic_type_is_volatile(expression->type)) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
    case MINIC_EXPRESSION_LABEL_ADDRESS:
        return true;
    case MINIC_EXPRESSION_LOCAL: {
        const MinicLocal *local = minic_c0_program_local(
            context->body->program, expression->value.local_id);
        return local != NULL && !minic_type_is_volatile(local->type);
    }
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_cfg_pure_call_argument(
            context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        return expression->value.unary.operator_kind <= MINIC_UNARY_BITWISE_NOT &&
               core_cfg_pure_call_argument(
                   context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_MEMBER:
        return core_cfg_pure_call_argument(
            context, expression->value.member.base, depth + 1U);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return core_cfg_pure_call_argument(context,
                                           expression->value.subscript.base,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.subscript.index,
                                           depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return expression->value.binary.operator_kind != MINIC_BINARY_COMMA &&
               core_cfg_pure_call_argument(context,
                                           expression->value.binary.left,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.binary.right,
                                           depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return core_cfg_pure_call_argument(context,
                                           expression->value.conditional.condition,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_true,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_false,
                                           depth + 1U);
    default:
        return false;
    }
}

static bool core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
                                          const MinicExpression *expression,
                                          MinicConstValue *value) {
    const MinicC0Program *program;
    const MinicFunction *callee;
    const MinicBlock *body;
    const MinicStatement *statement;
    MinicConstValue returned;
    size_t argument_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
        !minic_type_is_integer(expression->type)) {
        return false;
    }
    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee == NULL || !callee->is_defined || !callee->is_internal || !callee->is_inline ||
        !minic_type_is_integer(callee->return_type) || callee->body_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    for (argument_index = 0U; argument_index < expression->value.call.argument_count;
         ++argument_index) {
        if (!core_cfg_pure_call_argument(
                context, expression->value.call.arguments[argument_index], 0U)) {
            return false;
        }
    }
    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count != 1U) {
        return false;
    }
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->cleanup_context != statement->cleanup_stop_context ||
        !minic_const_eval_integer(program, context->target, statement->expression, &returned)) {
        return false;
    }
    return minic_const_value_convert_integer(program,
                                             context->target,
                                             &returned,
                                             expression->type,
                                             value);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"expected one local evaluator definition, found {text.count(anchor)}")
text = text.replace(anchor, helpers + anchor, 1)

start = text.find(anchor)
if start < 0:
    raise SystemExit("local evaluator missing after helper insertion")
call_anchor = '''    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
'''
pos = text.find(call_anchor, start)
if pos < 0:
    raise SystemExit("local evaluator cast anchor missing")
call_insert = '''    if (expression->kind == MINIC_EXPRESSION_CALL &&
        core_cfg_constant_inline_call(context, expression, value)) {
        return true;
    }
'''
text = text[:pos] + call_insert + text[pos:]

condition_fn = text.find("static MinicCoreLowerStatus lower_condition_branch(\n")
if condition_fn < 0:
    raise SystemExit("lower_condition_branch definition missing")
# Skip the forward declaration and select the definition after set_branch.
condition_fn = text.find("static MinicCoreLowerStatus lower_condition_branch(\n", condition_fn + 1)
if condition_fn < 0:
    raise SystemExit("lower_condition_branch body missing")
condition_anchor = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
pos = text.find(condition_anchor, condition_fn)
if pos < 0:
    raise SystemExit("condition validation anchor missing")
condition_insert = condition_anchor + '''    if (minic_type_is_integer(expression->type)) {
        MinicConstValue condition_constant;
        bool condition_is_zero;
        if (core_const_eval_integer_with_locals(context, expression_id, &condition_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_constant,
                                      &condition_is_zero)) {
            return set_branch(context,
                              context->block_id,
                              span,
                              condition_is_zero ? when_false : when_true);
        }
    }
'''
text = text[:pos] + condition_insert + text[pos + len(condition_anchor):]

p.write_text(text)
print("MINIC_CONSTANT_INLINE_CALL_CFG_V0=APPLIED")
