#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M183_TAIL_CFG_CLOSURE"
if marker in text:
    print("MINIC_TAIL_CFG_CLOSURE_V0=ALREADY")
    raise SystemExit(0)

# M177 already owns constant switch dispatch.  Its original selector check used
# the frontend-only constant evaluator, which cannot see integer facts baked
# into an inline-specialization clone.  Use the Core CFG evaluator for the
# selector only; case labels remain ordinary C integer constant expressions.
switch_marker = "/* M177_CONSTANT_SWITCH_CFG_OWNER:"
switch_start = text.find(switch_marker)
if switch_start < 0:
    raise SystemExit("M177 switch owner missing")
switch_end = text.find(
    "    status = set_branch(context, context->block_id, statement->span, dispatch_target);",
    switch_start,
)
if switch_end < 0:
    raise SystemExit("M177 switch dispatch end missing")
switch_body = text[switch_start:switch_end]
selector_old = '''        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     statement->expression,
                                     &selector_constant) &&
'''
selector_new = '''        if (core_const_eval_integer_with_locals(context,
                                                statement->expression,
                                                &selector_constant) &&
'''
if switch_body.count(selector_old) != 1:
    raise SystemExit(
        f"M177 selector evaluator anchor: expected one, found {switch_body.count(selector_old)}"
    )
switch_body = switch_body.replace(selector_old, selector_new, 1)
text = text[:switch_start] + switch_body + text[switch_end:]

# Symbolic inline specialization already records static function-address facts
# for pointer parameters.  Consume exactly that fact for pointer ==/!= in CFG
# conditions.  This is identity folding only: no numeric function address is
# invented, and unrelated pointer ordering remains untouched.
fn_anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
fn_pos = text.find(fn_anchor)
if fn_pos < 0:
    raise SystemExit("local integer evaluator missing for M183")
helper = r'''/* M183_TAIL_CFG_CLOSURE: symbolic function-pointer identity in a
 * specialized inline clone.  The recorded symbolic expression is a parsed,
 * immutable static expression; recursively unwrap only representation-preserving
 * casts/reads and parameter facts until a function designator is reached. */
static bool core_cfg_specialized_function_symbol(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth,
    MinicFunctionId *function_id) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || function_id == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        *function_id = expression->value.function_id;
        return *function_id < program->function_count;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_cfg_specialized_function_symbol(
            context, expression->value.unary.operand, depth + 1U, function_id);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *specialized = context->source_function;
        const MinicFunction *source;
        MinicLocalId local_id;
        size_t parameter_index;

        if (!specialized->is_integer_specialization ||
            specialized->specialization_source >= program->function_count) {
            return false;
        }
        source = minic_c0_program_function(program, specialized->specialization_source);
        if (source == NULL) {
            return false;
        }
        local_id = expression->value.local_id;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !specialized->specialization_symbolic_known[parameter_index] ||
            specialized->specialization_symbolic_expression[parameter_index] ==
                MINIC_EXPRESSION_INVALID) {
            return false;
        }
        return core_cfg_specialized_function_symbol(
            context,
            specialized->specialization_symbolic_expression[parameter_index],
            depth + 1U,
            function_id);
    }
    return false;
}

'''
text = text[:fn_pos] + helper + text[fn_pos:]

compare_anchor = '''        uint64_t left_bits;
        uint64_t right_bits;
        bool predicate;
        if (left != NULL && right != NULL &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
'''
compare_replacement = '''        uint64_t left_bits;
        uint64_t right_bits;
        MinicFunctionId left_function;
        MinicFunctionId right_function;
        bool predicate;
        if (left != NULL && right != NULL &&
            (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
             expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            core_cfg_specialized_function_symbol(
                context, expression->value.binary.left, 0U, &left_function) &&
            core_cfg_specialized_function_symbol(
                context, expression->value.binary.right, 0U, &right_function)) {
            predicate = left_function == right_function;
            if (expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
                predicate = !predicate;
            }
            value->type = expression->type;
            value->bits = predicate ? 1U : 0U;
            return true;
        }
        if (left != NULL && right != NULL &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
'''
if text.count(compare_anchor) != 1:
    raise SystemExit(
        f"residual pointer comparison anchor: expected one, found {text.count(compare_anchor)}"
    )
text = text.replace(compare_anchor, compare_replacement, 1)

p.write_text(text)
print("MINIC_TAIL_CFG_CLOSURE_V0=APPLIED local_switch=1 symbolic_function_eq=1")
