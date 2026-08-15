#include "core/core_lower.h"

#include <string.h>

static bool lower_expression(const MinicFunctionBodyView *body,
                             MinicCoreFunction *function,
                             MinicCoreBlockId block_id,
                             MinicExpressionId expression_id,
                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;

    if (body == NULL || body->program == NULL || function == NULL || value_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(body->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        if (!minic_type_is_integer(expression->type)) {
            return false;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.value.integer_value = expression->value.integer_value;
        return minic_core_function_append_value_instruction(
            function, block_id, &instruction, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
        minic_type_is_integer(expression->type)) {
        MinicCoreValueId left;
        MinicCoreValueId right;

        if (!lower_expression(body, function, block_id, expression->value.binary.left, &left) ||
            !lower_expression(body, function, block_id, expression->value.binary.right, &right)) {
            return false;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
            function, block_id, &instruction, value_id);
    }
    return false;
}

bool minic_core_lower_function(const MinicFunctionBodyView *body, MinicCoreFunction *output) {
    const MinicFunction *source_function;
    const MinicBlock *source_block;
    const MinicStatement *statement;
    MinicCoreFunction lowered;
    MinicCoreBlockId block_id;
    MinicCoreTerminator terminator;

    if (body == NULL || body->program == NULL || output == NULL) {
        return false;
    }
    source_function = minic_c0_function_body_function(body);
    source_block = minic_c0_program_block(body->program, minic_c0_function_body_root_block(body));
    if (source_function == NULL || source_block == NULL || source_block->statement_count != 1U ||
        source_function->name == NULL || source_function->name_length == 0U) {
        return false;
    }
    statement = minic_c0_program_statement(body->program, source_block->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
        return false;
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
        return false;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    if (minic_type_is_void(source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            minic_core_function_destroy(&lowered);
            return false;
        }
    } else if (statement->expression == MINIC_EXPRESSION_INVALID ||
               !lower_expression(
                   body, &lowered, block_id, statement->expression, &terminator.return_value)) {
        minic_core_function_destroy(&lowered);
        return false;
    }
    if (!minic_core_function_set_terminator(&lowered, block_id, &terminator) ||
        !minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return false;
    }
    minic_core_function_destroy(output);
    *output = lowered;
    return true;
}
