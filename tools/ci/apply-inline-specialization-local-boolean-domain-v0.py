#!/usr/bin/env python3
from pathlib import Path

path = Path("src/compiler/compiler.c")
text = path.read_text()

anchor = "static bool minic_inline_boolean_range_argument(\n"
if text.count(anchor) != 1:
    raise SystemExit(f"local boolean domain helper anchor count={text.count(anchor)}")

helpers = r'''
static bool minic_inline_boolean_domain_value(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return expression->value.integer_value == 0 ||
               expression->value.integer_value == 1;
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_inline_boolean_domain_value(
            program, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            return true;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_PLUS) {
            return minic_inline_boolean_domain_value(
                program, expression->value.unary.operand, depth + 1U);
        }
        return false;
    case MINIC_EXPRESSION_BINARY:
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_EQUAL:
        case MINIC_BINARY_NOT_EQUAL:
        case MINIC_BINARY_LESS:
        case MINIC_BINARY_LESS_EQUAL:
        case MINIC_BINARY_GREATER:
        case MINIC_BINARY_GREATER_EQUAL:
        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return true;
        case MINIC_BINARY_COMMA:
            return minic_inline_boolean_domain_value(
                program, expression->value.binary.right, depth + 1U);
        default:
            return false;
        }
    case MINIC_EXPRESSION_CONDITIONAL:
        return minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_true, depth + 1U) &&
               minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_false, depth + 1U);
    case MINIC_EXPRESSION_ASSIGNMENT:
        return minic_inline_boolean_domain_value(
            program, expression->value.binary.right, depth + 1U);
    default:
        return false;
    }
}

static bool minic_inline_direct_local_target(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    MinicLocalId *local_id) {
    const MinicExpression *expression;

    if (program == NULL || local_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool minic_inline_local_is_parameter(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (!function->is_defined || function->parameter_count == 0U) {
            continue;
        }
        if (local_id >= function->local_begin &&
            local_id - function->local_begin < function->parameter_count) {
            return true;
        }
    }
    return false;
}

/*
 * Conservatively recognize ordinary integer locals whose complete direct-write
 * set is confined to {0,1}.  This covers Linux patterns such as an unsigned
 * int flag initialized to 0 and conditionally assigned 1 before being passed
 * through always-inline helpers.  Reject address escapes, compound updates,
 * increments/decrements and asm outputs so the fact is never inferred across
 * an unknown write.
 */
static bool minic_inline_local_boolean_domain(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    bool saw_assignment = false;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;

    if (program == NULL || local_id >= program->local_count ||
        !minic_type_is_integer(program->locals[local_id].type) ||
        minic_inline_local_is_parameter(program, local_id)) {
        return false;
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local != local_id) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            return false;
        }
        saw_assignment = true;
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local == local_id) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                return false;
            }
            saw_assignment = true;
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local == local_id) {
                return false;
            }
        }
    }

    return saw_assignment;
}

'''
text = text.replace(anchor, helpers + anchor, 1)

old = '''    if (expression->kind == MINIC_EXPRESSION_LOCAL && caller != NULL &&
        caller->is_integer_specialization &&
        caller->specialization_source < program->function_count) {
        const MinicFunction *source = &program->functions[caller->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_boolean_range_known[parameter_index]) {
            return false;
        }
        *used_caller_fact = true;
        return true;
    }
'''
new = '''    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;

        if (minic_inline_local_boolean_domain(program, local_id)) {
            return true;
        }
        if (caller != NULL && caller->is_integer_specialization &&
            caller->specialization_source < program->function_count) {
            const MinicFunction *source =
                &program->functions[caller->specialization_source];
            size_t parameter_index;

            if (local_id < source->local_begin) {
                return false;
            }
            parameter_index = local_id - source->local_begin;
            if (parameter_index >= source->parameter_count ||
                parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
                !caller->specialization_boolean_range_known[parameter_index]) {
                return false;
            }
            *used_caller_fact = true;
            return true;
        }
    }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"local boolean domain range anchor count={count}")
text = text.replace(old, new, 1)
path.write_text(text)
print("MINIC_INLINE_LOCAL_BOOLEAN_DOMAIN_V0=APPLIED")
