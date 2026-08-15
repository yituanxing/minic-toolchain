#include "core/core_lower.h"

#include <string.h>

static MinicCoreLowerStatus lower_expression(const MinicFunctionBodyView *body,
                                             MinicCoreFunction *function,
                                             MinicCoreBlockId block_id,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;

    if (body == NULL || body->program == NULL || function == NULL || value_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.value.integer_value = expression->value.integer_value;
        return minic_core_function_append_value_instruction(
                   function, block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(body, function, block_id, expression->value.binary.left, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(body, function, block_id, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   function, block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

static MinicCoreLowerStatus validate_unreachable_return_tail(const MinicFunctionBodyView *body,
                                                             const MinicBlock *block) {
    size_t statement_index;

    if (body == NULL || body->program == NULL || block == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (statement_index = 1U; statement_index < block->statement_count; ++statement_index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(body->program, block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (statement->kind != MINIC_STATEMENT_RETURN) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               MinicCoreFunction *output) {
    const MinicFunction *source_function;
    const MinicBlock *source_block;
    const MinicStatement *statement;
    MinicCoreFunction lowered;
    MinicCoreBlockId block_id;
    MinicCoreTerminator terminator;
    MinicCoreLowerStatus status;

    if (body == NULL || body->program == NULL || output == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source_function = minic_c0_function_body_function(body);
    source_block = minic_c0_program_block(body->program, minic_c0_function_body_root_block(body));
    if (source_function == NULL || source_block == NULL || source_function->name == NULL ||
        source_function->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (source_block->statement_count == 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    statement = minic_c0_program_statement(body->program, source_block->statements[0]);
    if (statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (statement->kind != MINIC_STATEMENT_RETURN ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = validate_unreachable_return_tail(body, source_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    minic_core_function_initialize(&lowered);
    if (!minic_core_function_set_signature(&lowered,
                                           source_function->name,
                                           source_function->name_length,
                                           source_function->return_type,
                                           source_function->parameter_types,
                                           source_function->parameter_count) ||
        !minic_core_function_add_block(&lowered, &block_id)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    if (minic_type_is_void(source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            minic_core_function_destroy(&lowered);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else {
        if (statement->expression == MINIC_EXPRESSION_INVALID) {
            minic_core_function_destroy(&lowered);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(
            body, &lowered, block_id, statement->expression, &terminator.return_value);
        if (status != MINIC_CORE_LOWER_OK) {
            minic_core_function_destroy(&lowered);
            return status;
        }
    }
    if (!minic_core_function_set_terminator(&lowered, block_id, &terminator) ||
        !minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    minic_core_function_destroy(output);
    *output = lowered;
    return MINIC_CORE_LOWER_OK;
}
