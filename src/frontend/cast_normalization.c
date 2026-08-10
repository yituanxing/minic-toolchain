#include "frontend/cast_normalization.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool remap_expression_id(const MinicExpressionId *mapping,
                                size_t old_expression_count,
                                size_t current_old_index,
                                MinicExpressionId old_id,
                                MinicExpressionId *new_id) {
    if (mapping == NULL || new_id == NULL || old_id >= current_old_index ||
        old_id >= old_expression_count || mapping[old_id] == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    *new_id = mapping[old_id];
    return true;
}

static bool remap_non_cast_expression(MinicExpression *expression,
                                      const MinicExpressionId *mapping,
                                      size_t old_expression_count,
                                      size_t current_old_index) {
    size_t argument_index;

    if (expression == NULL) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_FLOATING:
    case MINIC_EXPRESSION_LOCAL:
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
        return true;
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
    case MINIC_EXPRESSION_UNARY:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.unary.operand,
                                   &expression->value.unary.operand);
    case MINIC_EXPRESSION_CAST:
        return false;
    case MINIC_EXPRESSION_SUBSCRIPT:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.subscript.base,
                                   &expression->value.subscript.base) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.subscript.index,
                                   &expression->value.subscript.index);
    case MINIC_EXPRESSION_MEMBER:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.member.base,
                                   &expression->value.member.base);
    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
    case MINIC_EXPRESSION_BINARY:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.binary.left,
                                   &expression->value.binary.left) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.binary.right,
                                   &expression->value.binary.right);
    case MINIC_EXPRESSION_CONDITIONAL:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.condition,
                                   &expression->value.conditional.condition) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.when_true,
                                   &expression->value.conditional.when_true) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.conditional.when_false,
                                   &expression->value.conditional.when_false);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.left,
                                   &expression->value.overflow.left) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.right,
                                   &expression->value.overflow.right) &&
               remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.overflow.result_pointer,
                                   &expression->value.overflow.result_pointer);
    case MINIC_EXPRESSION_STATEMENT:
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.statement_expression.result,
                                   &expression->value.statement_expression.result);
    case MINIC_EXPRESSION_CALL:
        if (expression->value.call.argument_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            return false;
        }
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID &&
            !remap_expression_id(mapping,
                                 old_expression_count,
                                 current_old_index,
                                 expression->value.call.callee,
                                 &expression->value.call.callee)) {
            return false;
        }
        for (argument_index = 0U; argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (!remap_expression_id(mapping,
                                     old_expression_count,
                                     current_old_index,
                                     expression->value.call.arguments[argument_index],
                                     &expression->value.call.arguments[argument_index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool append_normalized_bitcast(MinicC0Program *rewritten,
                                      const MinicExpression *cast_expression,
                                      MinicExpressionId mapped_operand,
                                      MinicExpressionId *normalized_id) {
    MinicExpression normalized_expression;

    (void)memset(&normalized_expression, 0, sizeof(normalized_expression));
    normalized_expression.kind = MINIC_EXPRESSION_BITCAST;
    normalized_expression.span = cast_expression->span;
    normalized_expression.type = cast_expression->type;
    normalized_expression.value_category = MINIC_VALUE_RVALUE;
    normalized_expression.value.unary.operand = mapped_operand;
    return minic_c0_program_add_expression(rewritten, &normalized_expression, normalized_id);
}

static bool append_normalized_discard(MinicC0Program *rewritten,
                                      const MinicExpression *cast_expression,
                                      MinicExpressionId mapped_operand,
                                      MinicExpressionId *normalized_id) {
    MinicExpression discard;

    (void)memset(&discard, 0, sizeof(discard));
    discard.kind = MINIC_EXPRESSION_DISCARD;
    discard.span = cast_expression->span;
    discard.type = minic_type_void();
    discard.value_category = MINIC_VALUE_RVALUE;
    discard.value.unary.operand = mapped_operand;
    return minic_c0_program_add_expression(rewritten, &discard, normalized_id);
}

static bool append_normalized_conversion(MinicC0Program *rewritten,
                                         const MinicExpression *cast_expression,
                                         MinicExpressionId mapped_operand,
                                         MinicExpressionId *normalized_id) {
    MinicExpression conversion;

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CONVERSION;
    conversion.span = cast_expression->span;
    conversion.type = cast_expression->type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = mapped_operand;
    return minic_c0_program_add_expression(rewritten, &conversion, normalized_id);
}

static bool append_normalized_cast(MinicC0Program *rewritten,
                                   const MinicExpression *cast_expression,
                                   MinicExpressionId mapped_operand,
                                   MinicExpressionId *normalized_id) {
    const MinicExpression *operand_expression;

    if (rewritten == NULL || cast_expression == NULL || normalized_id == NULL ||
        mapped_operand >= rewritten->expression_count) {
        return false;
    }
    operand_expression = &rewritten->expressions[mapped_operand];

    if (minic_type_is_void(cast_expression->type)) {
        return append_normalized_discard(rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if ((minic_type_is_double(cast_expression->type) &&
         (minic_type_is_integer(operand_expression->type) ||
          minic_type_is_float(operand_expression->type))) ||
        (minic_type_is_integer(cast_expression->type) &&
         minic_type_is_double(operand_expression->type))) {
        return append_normalized_conversion(
            rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if (minic_type_is_pointer(cast_expression->type) &&
        operand_expression->kind == MINIC_EXPRESSION_INTEGER &&
        minic_type_is_integer(operand_expression->type) &&
        operand_expression->value.integer_value == 0) {
        return append_normalized_bitcast(rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if ((minic_type_is_pointer(cast_expression->type) &&
         (minic_type_is_pointer(operand_expression->type) ||
          minic_type_is_integer(operand_expression->type))) ||
        (minic_type_is_integer(cast_expression->type) &&
         minic_type_is_pointer(operand_expression->type))) {
        return append_normalized_bitcast(rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if (minic_type_is_integer(cast_expression->type) &&
        minic_type_is_integer(operand_expression->type)) {
        MinicExpression zero_expression;
        MinicExpression normalized_expression;
        MinicExpressionId zero_id;

        (void)memset(&zero_expression, 0, sizeof(zero_expression));
        zero_expression.kind = MINIC_EXPRESSION_INTEGER;
        zero_expression.span = cast_expression->span;
        zero_expression.type = minic_type_int();
        zero_expression.value_category = MINIC_VALUE_RVALUE;
        zero_expression.value.integer_value = 0;
        if (!minic_c0_program_add_expression(rewritten, &zero_expression, &zero_id)) {
            return false;
        }

        (void)memset(&normalized_expression, 0, sizeof(normalized_expression));
        normalized_expression.kind = MINIC_EXPRESSION_BINARY;
        normalized_expression.span = cast_expression->span;
        normalized_expression.type = cast_expression->type;
        normalized_expression.value_category = MINIC_VALUE_RVALUE;
        normalized_expression.value.binary.operator_kind = MINIC_BINARY_ADD;
        normalized_expression.value.binary.left = mapped_operand;
        normalized_expression.value.binary.right = zero_id;
        return minic_c0_program_add_expression(rewritten, &normalized_expression, normalized_id);
    }

    return false;
}

static bool remap_program_expression_id(const MinicExpressionId *mapping,
                                        size_t old_expression_count,
                                        MinicExpressionId *expression_id) {
    if (expression_id == NULL) {
        return false;
    }
    if (*expression_id == MINIC_EXPRESSION_INVALID) {
        return true;
    }
    if (mapping == NULL || *expression_id >= old_expression_count ||
        mapping[*expression_id] == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    *expression_id = mapping[*expression_id];
    return true;
}

bool minic_c0_program_normalize_casts(MinicC0Program *program) {
    MinicC0Program rewritten;
    MinicExpressionId *mapping;
    MinicStatement *remapped_statements;
    MinicExpressionId remapped_return_expression;
    size_t old_expression_count;
    size_t expression_index;
    size_t statement_index;
    size_t inline_asm_index;
    bool success;

    if (program == NULL || (program->expression_count != 0U && program->expressions == NULL) ||
        (program->statement_count != 0U && program->statements == NULL) ||
        program->expression_count > SIZE_MAX / sizeof(*mapping) ||
        program->statement_count > SIZE_MAX / sizeof(*remapped_statements)) {
        return false;
    }

    old_expression_count = program->expression_count;
    mapping = NULL;
    if (old_expression_count != 0U) {
        mapping = (MinicExpressionId *)malloc(old_expression_count * sizeof(*mapping));
        if (mapping == NULL) {
            return false;
        }
        for (expression_index = 0U; expression_index < old_expression_count; ++expression_index) {
            mapping[expression_index] = MINIC_EXPRESSION_INVALID;
        }
    }

    remapped_statements = NULL;
    if (program->statement_count != 0U) {
        remapped_statements =
            (MinicStatement *)malloc(program->statement_count * sizeof(*remapped_statements));
        if (remapped_statements == NULL) {
            free(mapping);
            return false;
        }
        (void)memcpy(remapped_statements,
                     program->statements,
                     program->statement_count * sizeof(*remapped_statements));
    }
    remapped_return_expression = program->return_expression;

    minic_c0_program_initialize(&rewritten);
    success = true;
    for (expression_index = 0U; success && expression_index < old_expression_count;
         ++expression_index) {
        const MinicExpression *old_expression;
        MinicExpressionId new_id;

        old_expression = &program->expressions[expression_index];
        if (old_expression->kind == MINIC_EXPRESSION_CAST) {
            MinicExpressionId mapped_operand;

            success = remap_expression_id(mapping,
                                          old_expression_count,
                                          expression_index,
                                          old_expression->value.unary.operand,
                                          &mapped_operand) &&
                      append_normalized_cast(&rewritten, old_expression, mapped_operand, &new_id);
        } else {
            MinicExpression copied_expression;

            copied_expression = *old_expression;
            success = remap_non_cast_expression(
                          &copied_expression, mapping, old_expression_count, expression_index) &&
                      minic_c0_program_add_expression(&rewritten, &copied_expression, &new_id);
        }
        if (success) {
            mapping[expression_index] = new_id;
        }
    }

    for (statement_index = 0U; success && statement_index < program->statement_count;
         ++statement_index) {
        MinicStatement *statement;

        statement = &remapped_statements[statement_index];
        success =
            remap_program_expression_id(
                mapping, old_expression_count, &statement->target_expression) &&
            remap_program_expression_id(mapping, old_expression_count, &statement->expression);
    }
    if (success) {
        success =
            remap_program_expression_id(mapping, old_expression_count, &remapped_return_expression);
    }
    for (inline_asm_index = 0U; success && inline_asm_index < program->inline_asm_count;
         ++inline_asm_index) {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = &program->inline_asms[inline_asm_index];
        for (operand_index = 0U; success && operand_index < inline_asm->output_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->outputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
        for (operand_index = 0U; success && operand_index < inline_asm->input_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->inputs[operand_index].expression;
            success = old_id < old_expression_count && mapping != NULL &&
                      mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
    }

    if (success) {
        for (inline_asm_index = 0U; inline_asm_index < program->inline_asm_count;
             ++inline_asm_index) {
            MinicInlineAsm *inline_asm;
            size_t operand_index;

            inline_asm = &program->inline_asms[inline_asm_index];
            for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
                inline_asm->outputs[operand_index].expression =
                    mapping[inline_asm->outputs[operand_index].expression];
            }
            for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
                inline_asm->inputs[operand_index].expression =
                    mapping[inline_asm->inputs[operand_index].expression];
            }
        }
        free(program->expressions);
        program->expressions = rewritten.expressions;
        program->expression_count = rewritten.expression_count;
        program->expression_capacity = rewritten.expression_capacity;
        rewritten.expressions = NULL;
        rewritten.expression_count = 0U;
        rewritten.expression_capacity = 0U;
        if (program->statement_count != 0U) {
            (void)memcpy(program->statements,
                         remapped_statements,
                         program->statement_count * sizeof(*remapped_statements));
        }
        program->return_expression = remapped_return_expression;
    }

    free(rewritten.expressions);
    free(remapped_statements);
    free(mapping);
    return success;
}
