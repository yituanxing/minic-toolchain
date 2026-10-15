#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
marker = "M114_RECORD_CONDITIONAL_OBJECT"
if marker in text:
    raise SystemExit("M114 already productized")

prototype_anchor = """static MinicCoreLowerStatus lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id);
"""
prototype_insert = prototype_anchor + """static MinicCoreLowerStatus lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
static MinicCoreLowerStatus lower_record_materialized_address(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicCoreValueId *address_id);
"""
if text.count(prototype_anchor) != 1:
    raise SystemExit(f"prototype anchor mismatch: {text.count(prototype_anchor)}")
text = text.replace(prototype_anchor, prototype_insert, 1)

definition_anchor = """/* BATCH_M_RECORD_LOAD: turn an address-backed record rvalue/lvalue wrapper
"""
helper = r'''/* M114_RECORD_CONDITIONAL_OBJECT: record values remain address-backed in Core.
   Materialize one private result object and copy exactly the selected arm into
   it. Arms may be ordinary address-backed records, compound literals, direct
   record-returning calls, or nested record conditionals. */
static MinicCoreLowerStatus lower_record_materialized_address(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicCoreValueId *address_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreLowerStatus status;
    MinicType pointer_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        status = lower_record_conditional_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CALL &&
               expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        status = lower_direct_record_call_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        status = lower_record_compound_literal_object(context, expression, &object_id);
    } else {
        return lower_record_value_address(context, expression_id, address_id);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (object_id >= context->function->object_count ||
        !minic_type_pointer_to(context->function->objects[object_id].type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    const MinicExpression *false_expression;
    const MinicExpression *true_expression;
    MinicCoreBlockId false_block;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId true_block;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType false_type;
    MinicType pointer_type;
    MinicType result_type;
    MinicType true_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || result_object == NULL ||
        expression->kind != MINIC_EXPRESSION_CONDITIONAL ||
        expression->value.conditional.uses_condition_value ||
        expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
        expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &result_type) ||
        !minic_type_is_record(result_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    true_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_true);
    false_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_false);
    if (true_expression == NULL || false_expression == NULL ||
        !minic_type_is_record(true_expression->type) ||
        !minic_type_is_record(false_expression->type) ||
        !minic_type_unqualified(true_expression->type, &true_type) ||
        !minic_type_unqualified(false_expression->type, &false_type) ||
        !minic_type_equal(result_type, true_type) ||
        !minic_type_equal(result_type, false_type) ||
        !minic_type_pointer_to(result_type, &pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, result_type, result_object) ||
        !minic_core_function_add_block(context->function, &true_block) ||
        !minic_core_function_add_block(context->function, &false_block) ||
        !minic_core_function_add_block(context->function, &merge_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = lower_condition_branch(context,
                                    expression->value.conditional.condition,
                                    expression->span,
                                    true_block,
                                    false_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = true_block;
    status = lower_record_materialized_address(
        context, expression->value.conditional.when_true, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = true_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = true_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = false_block;
    status = lower_record_materialized_address(
        context, expression->value.conditional.when_false, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = false_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = false_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = merge_block;
    return MINIC_CORE_LOWER_OK;
}

'''
if text.count(definition_anchor) != 1:
    raise SystemExit(f"definition anchor mismatch: {text.count(definition_anchor)}")
text = text.replace(definition_anchor, helper + definition_anchor, 1)

return_anchor = """            } else if (minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
"""
return_insert = """            } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
                       minic_type_is_record(expression->type)) {
                status = lower_record_conditional_object(
                    context, expression, &terminator.return_object);
            } else if (minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
"""
if text.count(return_anchor) != 1:
    raise SystemExit(f"return fallback anchor mismatch: {text.count(return_anchor)}")
text = text.replace(return_anchor, return_insert, 1)

path.write_text(text)
