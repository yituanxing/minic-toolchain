#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: anchor count={count}")
    return text.replace(old, new, 1)


path = Path("src/core/core_lower.c")
text = path.read_text()

batch_n = r'''    /* BATCH_N_GNU_OMITTED_MIDDLE_CONDITIONAL: GNU `a ?: b` reuses the
       already-evaluated condition value on the true path.  Materialize that
       converted value before branching so `a` is evaluated exactly once; the
       false path alone evaluates and overwrites with `b`. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        expression->value.conditional.uses_condition_value) {
        const MinicExpression *condition_expression;
        const MinicExpression *false_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreBlockId branch_true;
        MinicCoreBlockId branch_false;
        MinicCoreObjectId result_object;
        MinicCoreValueId condition_value;
        MinicCoreValueId true_value;
        MinicCoreValueId false_value;
        MinicCoreValueId branch_value;
        MinicCoreLowerStatus status;
        MinicCoreInstruction zero_test;
        MinicCoreTerminator terminator;
        MinicType assignment_type;

        if (expression->value.conditional.condition == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_true != expression->value.conditional.condition ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_memory_scalar_type(expression->type) ||
            minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        condition_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.condition);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (condition_expression == NULL || false_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type)) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            expression->type,
                                            expression->value.conditional.condition) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            expression->type,
                                            expression->value.conditional.when_false)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, expression->type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_expression(
            context, expression->value.conditional.condition, &condition_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (condition_value >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(expression->type) &&
            minic_type_is_integer(condition_expression->type)) {
            if (!minic_c0_integer_assignment_value_type(
                    context->body->program,
                    expression->type,
                    expression->value.conditional.condition,
                    &assignment_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = append_integer_conversion(context,
                                               condition_expression->span,
                                               assignment_type,
                                               condition_value,
                                               &true_value);
        } else if (minic_type_is_pointer(expression->type) &&
                   (minic_type_is_pointer(condition_expression->type) ||
                    minic_c0_expression_is_null_pointer_constant_v0(
                        context->body->program,
                        expression->value.conditional.condition))) {
            status = append_scalar_bitcast(context,
                                           condition_expression->span,
                                           expression->type,
                                           condition_value,
                                           &true_value);
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(context,
                                    condition_expression->span,
                                    expression->type,
                                    result_object,
                                    true_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        branch_value = condition_value;
        branch_true = true_block;
        branch_false = false_block;
        if (minic_type_is_pointer(condition_expression->type)) {
            MinicCoreBlockId original_true;

            if (!minic_type_is_pointer(context->function->values[branch_value].type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&zero_test, 0, sizeof(zero_test));
            zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test.span = expression->span;
            zero_test.type = minic_type_int();
            zero_test.result = MINIC_CORE_VALUE_INVALID;
            zero_test.value.operand = branch_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &zero_test, &branch_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            original_true = branch_true;
            branch_true = branch_false;
            branch_false = original_true;
        } else if (!minic_type_is_integer(context->function->values[branch_value].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
        terminator.span = expression->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.condition = branch_value;
        terminator.conditional.when_true = branch_true;
        terminator.conditional.when_false = branch_false;
        if (!minic_core_function_set_terminator(
                context->function, context->block_id, &terminator)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        context->block_id = true_block;
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_scalar_assignment_value(context,
                                               expression->type,
                                               expression->value.conditional.when_false,
                                               &false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(context,
                                    false_expression->span,
                                    expression->type,
                                    result_object,
                                    false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        return reload_scalar_value(
            context, expression->span, expression->type, result_object, value_id);
    }
'''

anchor = '''    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {\n        const MinicExpression *false_expression;\n        const MinicExpression *true_expression;\n        MinicCoreBlockId false_block;\n        MinicCoreBlockId merge_block;\n        MinicCoreBlockId true_block;\n        MinicCoreObjectId result_object;\n        MinicCoreValueId arm_value;\n        MinicCoreLowerStatus status;\n        MinicType false_type;\n        MinicType true_type;\n\n        /* M60_POINTER_CONDITIONAL_VALUE: C conditional values may be pointer\n'''
text = replace_once(text, anchor, batch_n + anchor, "core_lower.c GNU omitted-middle conditional")
path.write_text(text)
print("CORE_BATCH_N_PATCHED GNU omitted-middle conditional single evaluation")
