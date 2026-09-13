#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
start = text.find("static bool minic_inline_symbolic_argument_expression(\n")
end = text.find("\nstatic bool minic_inline_symbolic_variant_matches(\n", start)
if start < 0 or end < 0 or end <= start:
    raise SystemExit("expected symbolic argument helper block")

replacement = r'''static bool minic_inline_close_symbolic_expression_depth(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *closed_id,
    bool *used_caller_integer_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    const MinicFunction *caller_source;
    MinicExpression closed;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    bool left_used = false;
    bool right_used = false;

    if (program == NULL || target == NULL || caller == NULL || closed_id == NULL ||
        used_caller_integer_fact == NULL || depth > 32U ||
        !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
        return false;
    }
    *used_caller_integer_fact = false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    caller_source = &program->functions[caller->specialization_source];

    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;
        uint64_t bits;

        if (local_id < caller_source->local_begin) {
            return false;
        }
        parameter_index = local_id - caller_source->local_begin;
        if (parameter_index >= caller_source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(caller_source->parameter_types[parameter_index])) {
            return false;
        }
        (void)memset(&closed, 0, sizeof(closed));
        closed.kind = MINIC_EXPRESSION_INTEGER;
        closed.span = expression->span;
        closed.type = caller_source->parameter_types[parameter_index];
        closed.value_category = MINIC_VALUE_RVALUE;
        bits = caller->specialization_integer_bits[parameter_index];
        (void)memcpy(&closed.value.integer_value, &bits, sizeof(bits));
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL) {
            if (!minic_inline_close_symbolic_expression_depth(
                    program,
                    target,
                    caller,
                    expression->value.unary.operand,
                    &left_id,
                    &left_used,
                    depth + 1U) ||
                !left_used) {
                return false;
            }
            *closed_id = left_id;
            *used_caller_integer_fact = true;
            return true;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_ADDRESS_OF ||
        expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.unary.operand,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.unary.operand = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        bool base_static = minic_inline_symbolic_static_expression(
            program, target, expression->value.subscript.base);
        if (base_static) {
            left_id = expression->value.subscript.base;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.base,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.subscript.index, &(MinicConstValue){0})) {
            right_id = expression->value.subscript.index;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.index,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.subscript.base = left_id;
        closed.value.subscript.index = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.member.base)) {
            *closed_id = expression_id;
            return true;
        }
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.member.base,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.member.base = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.binary.left)) {
            left_id = expression->value.binary.left;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.left,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.binary.right, &(MinicConstValue){0})) {
            right_id = expression->value.binary.right;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.right,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.binary.left = left_id;
        closed.value.binary.right = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    return false;
}

static bool minic_inline_symbolic_argument_expression(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *symbolic_expression,
    bool *used_caller_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicExpressionId closed_id;
    bool used_integer_fact = false;

    if (program == NULL || target == NULL || symbolic_expression == NULL ||
        used_caller_fact == NULL || depth > 32U) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_inline_symbolic_static_expression(program, target, expression_id)) {
        *symbolic_expression = expression_id;
        return true;
    }
    if (caller == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
        return false;
    }

    if (minic_inline_close_symbolic_expression_depth(
            program,
            target,
            caller,
            expression_id,
            &closed_id,
            &used_integer_fact,
            depth + 1U) &&
        used_integer_fact &&
        minic_inline_symbolic_static_expression(program, target, closed_id)) {
        *symbolic_expression = closed_id;
        *used_caller_fact = true;
        return true;
    }

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_inline_symbolic_argument_expression(
            program,
            target,
            caller,
            expression->value.unary.operand,
            symbolic_expression,
            used_caller_fact,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source = &program->functions[caller->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        *symbolic_expression =
            caller->specialization_symbolic_expression[parameter_index];
        *used_caller_fact = true;
        return *symbolic_expression != MINIC_EXPRESSION_INVALID;
    }
    return false;
}
'''

text = text[:start] + replacement + text[end:]
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_INTEGER_CLOSURE_V0=APPLIED")
