#include "frontend/cast_normalization.h"

#include "frontend/ast_traversal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicExpressionRemapContext {
    const MinicExpressionId *mapping;
    size_t old_expression_count;
    size_t current_old_index;
} MinicExpressionRemapContext;

typedef struct MinicExpressionIdRefSet {
    MinicExpressionId **values;
    size_t count;
    size_t capacity;
} MinicExpressionIdRefSet;

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

static bool remap_child_expression_id(MinicExpressionId *expression_id, void *opaque_context) {
    MinicExpressionRemapContext *context;

    if (expression_id == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicExpressionRemapContext *)opaque_context;
    return remap_expression_id(context->mapping,
                               context->old_expression_count,
                               context->current_old_index,
                               *expression_id,
                               expression_id);
}

static bool remap_non_cast_expression(MinicExpression *expression,
                                      const MinicExpressionId *mapping,
                                      size_t old_expression_count,
                                      size_t current_old_index) {
    MinicExpressionRemapContext context;

    if (expression == NULL ||
        (expression->kind == MINIC_EXPRESSION_STATEMENT &&
         expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID &&
         !minic_type_is_void(expression->type))) {
        return false;
    }

    context.mapping = mapping;
    context.old_expression_count = old_expression_count;
    context.current_old_index = current_old_index;
    return minic_c0_expression_visit_child_id_refs(
        expression, remap_child_expression_id, &context);
}

static bool collect_external_expression_id_ref(MinicExpressionId *expression_id,
                                               void *opaque_context) {
    MinicExpressionIdRefSet *references;
    MinicExpressionId **resized;
    size_t new_capacity;

    if (expression_id == NULL || opaque_context == NULL) {
        return false;
    }
    references = (MinicExpressionIdRefSet *)opaque_context;
    if (references->count == references->capacity) {
        new_capacity = references->capacity == 0U ? 16U : references->capacity * 2U;
        if (new_capacity < references->capacity ||
            new_capacity > SIZE_MAX / sizeof(*references->values)) {
            return false;
        }
        resized = (MinicExpressionId **)realloc(
            references->values, new_capacity * sizeof(*references->values));
        if (resized == NULL) {
            return false;
        }
        references->values = resized;
        references->capacity = new_capacity;
    }
    references->values[references->count] = expression_id;
    references->count += 1U;
    return true;
}

static bool validate_external_expression_id_refs(const MinicExpressionIdRefSet *references,
                                                 const MinicExpressionId *mapping,
                                                 size_t old_expression_count) {
    size_t index;

    if (references == NULL) {
        return false;
    }
    for (index = 0U; index < references->count; ++index) {
        MinicExpressionId old_id;

        if (references->values[index] == NULL) {
            return false;
        }
        old_id = *references->values[index];
        if (mapping == NULL || old_id >= old_expression_count ||
            mapping[old_id] == MINIC_EXPRESSION_INVALID) {
            return false;
        }
    }
    return true;
}

static void apply_external_expression_id_refs(const MinicExpressionIdRefSet *references,
                                              const MinicExpressionId *mapping) {
    size_t index;

    for (index = 0U; index < references->count; ++index) {
        MinicExpressionId *expression_id;

        expression_id = references->values[index];
        *expression_id = mapping[*expression_id];
    }
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

bool minic_c0_program_normalize_casts(MinicC0Program *program) {
    MinicC0Program rewritten;
    MinicExpressionId *mapping;
    MinicExpressionIdRefSet external_references;
    size_t old_expression_count;
    size_t expression_index;
    bool success;

    if (program == NULL || (program->expression_count != 0U && program->expressions == NULL) ||
        program->expression_count > SIZE_MAX / sizeof(*mapping)) {
        return false;
    }

    (void)memset(&external_references, 0, sizeof(external_references));
    if (!minic_c0_program_visit_external_expression_id_refs(
            program, collect_external_expression_id_ref, &external_references)) {
        free(external_references.values);
        return false;
    }

    old_expression_count = program->expression_count;
    mapping = NULL;
    if (old_expression_count != 0U) {
        mapping = (MinicExpressionId *)malloc(old_expression_count * sizeof(*mapping));
        if (mapping == NULL) {
            free(external_references.values);
            return false;
        }
        for (expression_index = 0U; expression_index < old_expression_count; ++expression_index) {
            mapping[expression_index] = MINIC_EXPRESSION_INVALID;
        }
    }

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

    if (success) {
        success = validate_external_expression_id_refs(
            &external_references, mapping, old_expression_count);
    }

    if (success) {
        apply_external_expression_id_refs(&external_references, mapping);
        free(program->expressions);
        program->expressions = rewritten.expressions;
        program->expression_count = rewritten.expression_count;
        program->expression_capacity = rewritten.expression_capacity;
        rewritten.expressions = NULL;
        rewritten.expression_count = 0U;
        rewritten.expression_capacity = 0U;
    }

    free(rewritten.expressions);
    free(mapping);
    free(external_references.values);
    return success;
}
