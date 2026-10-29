#include "frontend/parser_runtime_initializer.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runtime automatic-storage initializers: materialized expression effects.
 * Static relocations and constant bytes stay owned by parser_global.c. */

static bool add_zero_initialized_record_lvalue(MinicParser *parser,
                                               MinicExpressionId base_id,
                                               MinicSourceSpan initializer_span);
static bool add_array_object_zero_elements(MinicParser *parser,
                                           MinicExpressionId base_id,
                                           size_t element_count,
                                           MinicSourceSpan initializer_span);
static bool add_array_object_element_lvalue(MinicParser *parser,
                                            MinicExpressionId base_id,
                                            size_t index,
                                            MinicSourceSpan span,
                                            MinicExpressionId *target_id) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;
    MinicExpression index_expression;
    MinicExpression subscript;
    MinicExpressionId index_id;
    MinicSourceSpan base_span;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || target_id == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
        index > (size_t)INT64_MAX ||
        (!array_info.is_incomplete && index >= array_info.element_count)) {
        minic_parser_error(parser, "invalid runtime array initializer element");
        return false;
    }
    /* Expression storage is a growable arena. Copy values needed after the next
       add_expression call instead of retaining an interior pointer across realloc. */
    base_span = base->span;

    (void)memset(&index_expression, 0, sizeof(index_expression));
    index_expression.kind = MINIC_EXPRESSION_INTEGER;
    index_expression.span = span;
    index_expression.type = minic_type_unsigned_long();
    index_expression.value_category = MINIC_VALUE_RVALUE;
    index_expression.value.integer_value = (int64_t)index;
    if (!minic_parser_add_expression(parser, &index_expression, &index_id)) {
        return false;
    }

    (void)memset(&subscript, 0, sizeof(subscript));
    subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
    subscript.span.begin = base_span.begin;
    subscript.span.end = span.end;
    subscript.type = array_info.element_type;
    subscript.value_category = MINIC_VALUE_LVALUE;
    subscript.value.subscript.base = base_id;
    subscript.value.subscript.index = index_id;
    return minic_parser_add_expression(parser, &subscript, target_id);
}

static bool parse_runtime_record_array_element_initializer(MinicParser *parser,
                                                           MinicExpressionId target_id,
                                                           MinicType element_type) {
    MinicExpressionId source_id;
    const MinicExpression *source;

    if (parser == NULL || !minic_type_is_record(element_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return minic_parser_parse_runtime_record_initializer(parser, target_id);
    }
    if (!minic_parser_parse_expression(parser, &source_id, 0U)) {
        return false;
    }
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL || !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
        !minic_type_is_record(source->type) || source->type.record_id != element_type.record_id) {
        minic_parser_error(parser,
                           "runtime record array element requires a matching record copy source");
        return false;
    }
    return minic_parser_statement_record_initialize(parser, target_id, source_id, source->span);
}

static bool add_array_object_element_assignment(MinicParser *parser,
                                                MinicExpressionId base_id,
                                                size_t index,
                                                MinicExpressionId value_id) {
    const MinicExpression *base;
    const MinicExpression *value;
    MinicArrayObjectInfo array_info;
    MinicExpressionId target_id;
    MinicSourceSpan value_span;
    MinicStatement statement;
    MinicType element_type;

    base = minic_c0_program_expression(parser->program, base_id);
    value = minic_c0_program_expression(parser->program, value_id);
    if (base == NULL || value == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info)) {
        minic_parser_error(parser, "invalid runtime array initializer element");
        return false;
    }
    element_type = array_info.element_type;
    if (minic_type_is_record(element_type)) {
        minic_parser_error(parser, "record array elements require aggregate initialization");
        return false;
    }
    if (!minic_parser_statement_assignment_conversion(parser, element_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, element_type, value_id)) {
        minic_parser_error(parser, "runtime array initializer element type mismatch");
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL) {
        minic_parser_error(parser, "invalid converted runtime array initializer element");
        return false;
    }
    value_span = value->span;
    if (!add_array_object_element_lvalue(parser, base_id, index, value_span, &target_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = value_span;
    statement.target_expression = target_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_local_array_element_assignment(MinicParser *parser,
                                               MinicLocalId local_id,
                                               size_t index,
                                               MinicExpressionId value_id) {
    const MinicLocal *local;
    const MinicExpression *value;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    value = minic_c0_program_expression(parser->program, value_id);
    if (local == NULL || value == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer element");
        return false;
    }
    if (!minic_parser_statement_assignment_conversion(parser, local->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, local->type, value_id)) {
        minic_parser_error(parser,
                           "local array initializer element type does not match element type");
        return false;
    }
    if (!minic_parser_statement_local_lvalue(parser, local_id, local->name_span, &base_id)) {
        return false;
    }
    return add_array_object_element_assignment(parser, base_id, index, value_id);
}

static bool add_array_object_zero_element(MinicParser *parser,
                                          MinicExpressionId base_id,
                                          size_t index,
                                          MinicSourceSpan initializer_span) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;
    MinicExpression zero;
    MinicExpressionId element_id;
    MinicExpressionId zero_id;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info)) {
        minic_parser_error(parser, "invalid runtime array zero initializer target");
        return false;
    }
    if (minic_type_is_record(array_info.element_type)) {
        return add_array_object_element_lvalue(
                   parser, base_id, index, initializer_span, &element_id) &&
               add_zero_initialized_record_lvalue(parser, element_id, initializer_span);
    }
    if (minic_type_is_array(array_info.element_type)) {
        const MinicArrayType *nested_array;

        nested_array =
            minic_c0_program_array_type(parser->program, array_info.element_type.array_type_id);
        if (nested_array == NULL || nested_array->element_count == 0U ||
            nested_array->is_zero_length) {
            minic_parser_error(parser, "nested runtime array zero initializer requires fixed extent");
            return false;
        }
        return add_array_object_element_lvalue(
                   parser, base_id, index, initializer_span, &element_id) &&
               add_array_object_zero_elements(
                   parser, element_id, nested_array->element_count, initializer_span);
    }

    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    return minic_parser_add_expression(parser, &zero, &zero_id) &&
           add_array_object_element_assignment(parser, base_id, index, zero_id);
}

static bool add_array_object_zero_elements(MinicParser *parser,
                                           MinicExpressionId base_id,
                                           size_t element_count,
                                           MinicSourceSpan initializer_span) {
    size_t index;

    for (index = 0U; index < element_count; ++index) {
        if (!add_array_object_zero_element(parser, base_id, index, initializer_span)) {
            return false;
        }
    }
    return true;
}

static bool parse_runtime_array_designator(
    MinicParser *parser, size_t element_count, size_t next_index, size_t *first, size_t *last) {
    if (!minic_parser_parse_array_designator(parser, element_count, false, first, last)) {
        return false;
    }
    if (*first < next_index) {
        minic_parser_error(parser, "backward runtime array designators are not supported yet");
        return false;
    }
    return true;
}

static bool parse_fixed_runtime_record_array_initializer(MinicParser *parser,
                                                         MinicExpressionId base_id,
                                                         size_t element_count) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;
    MinicArrayInitializerPlan plan;
    MinicSourceSpan initializer_span;
    bool success;

    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(
                parser, "fixed runtime record array initializer requires a nonempty record array");
        }
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
        !minic_type_is_record(array_info.element_type)) {
        minic_parser_error(
            parser, "fixed runtime record array initializer requires a nonempty record array");
        return false;
    }

    minic_array_initializer_plan_initialize(&plan, element_count, false);
    initializer_span.begin = parser->current.span.begin;
    success = false;
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        MinicParser probe;
        int zero_value;
        bool zero_shorthand;

        probe = *parser;
        zero_shorthand =
            minic_parser_parse_integer_value(&probe, &zero_value) && zero_value == 0;
        if (zero_shorthand && probe.current.kind == MINIC_TOKEN_COMMA) {
            zero_shorthand =
                minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_RBRACE;
        } else if (zero_shorthand) {
            zero_shorthand = probe.current.kind == MINIC_TOKEN_RBRACE;
        }
        if (zero_shorthand) {
            int parsed_zero;

            if (!minic_parser_parse_integer_value(parser, &parsed_zero)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected '}' after record-array zero initializer");
                goto done;
            }
            initializer_span.end = parser->current.span.end;
            if (!add_array_object_zero_elements(
                    parser, base_id, element_count, initializer_span) ||
                !minic_parser_advance(parser)) {
                goto done;
            }
            success = true;
            goto done;
        }
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;
        size_t target_index;
        size_t last_index;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            MinicSourceSpan zero_span;
            size_t first;
            size_t last;
            size_t previous_next;
            size_t index;

            previous_next = plan.next_index;
            if (!parse_runtime_array_designator(
                    parser, element_count, plan.next_index, &first, &last)) {
                goto done;
            }
            zero_span = parser->current.span;
            if (!minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                minic_parser_error(parser, "cannot plan runtime record array initializer");
                goto done;
            }
            for (index = previous_next; index < first; ++index) {
                if (!add_array_object_zero_element(parser, base_id, index, zero_span)) {
                    goto done;
                }
            }
            target_index = plan.actions[action_id].first_index;
        } else {
            if (plan.next_index >= element_count) {
                minic_parser_error(parser, "too many runtime array initializer elements");
                goto done;
            }
            if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
                minic_parser_error(parser, "cannot plan runtime record array initializer");
                goto done;
            }
            target_index = plan.actions[action_id].first_index;
        }
        last_index = plan.actions[action_id].last_index;

        {
            MinicExpressionId first_element_id;
            size_t index;

            if (!add_array_object_element_lvalue(
                    parser, base_id, target_index, parser->current.span, &first_element_id) ||
                !parse_runtime_record_array_element_initializer(
                    parser, first_element_id, array_info.element_type)) {
                goto done;
            }
            /* GNU range initializers evaluate their expression once. Copy
             * the fully initialized first record to the remaining slots,
             * preserving side effects and even aggregate rvalue producers. */
            for (index = target_index + 1U; index <= last_index; ++index) {
                MinicExpressionId next_element_id;
                const MinicExpression *first_element;
                MinicSourceSpan source_span;

                first_element = minic_c0_program_expression(
                    parser->program, first_element_id);
                if (first_element == NULL) {
                    goto done;
                }
                source_span = first_element->span;
                if (!add_array_object_element_lvalue(
                        parser, base_id, index, source_span, &next_element_id) ||
                    !minic_parser_statement_record_initialize(
                        parser, next_element_id, first_element_id, source_span)) {
                    goto done;
                }
            }
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            goto done;
        }
    }

    initializer_span.end = parser->current.span.end;
    {
        size_t index;

        for (index = plan.next_index; index < element_count; ++index) {
            if (!add_array_object_zero_element(parser, base_id, index, initializer_span)) {
                goto done;
            }
        }
    }
    success = minic_parser_advance(parser);

done:
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool grow_runtime_array_action_values(MinicParser *parser,
                                             MinicExpressionId **values,
                                             size_t *capacity,
                                             size_t required) {
    MinicExpressionId *resized;
    size_t old_capacity;
    size_t new_capacity;
    size_t index;

    if (parser == NULL || values == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }
    if (new_capacity < required || new_capacity > SIZE_MAX / sizeof(**values)) {
        minic_parser_error(parser, "runtime array initializer action count overflows");
        return false;
    }
    resized = (MinicExpressionId *)realloc(*values, new_capacity * sizeof(**values));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while planning runtime array initializer");
        return false;
    }
    for (index = old_capacity; index < new_capacity; ++index) {
        resized[index] = MINIC_EXPRESSION_INVALID;
    }
    *values = resized;
    *capacity = new_capacity;
    return true;
}

static bool add_runtime_initializer_once_read(MinicParser *parser,
                                              MinicType value_type,
                                              MinicExpressionId value_id,
                                              MinicExpressionId *read_id) {
    const MinicExpression *value;
    MinicExpression lvalue_read;
    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicStatement assignment;
    MinicType temporary_type;

    if (parser == NULL || read_id == NULL || !minic_type_unqualified(value_type, &temporary_type) ||
        !minic_parser_statement_assignment_conversion(parser, temporary_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, temporary_type, value_id)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "runtime range initializer value type mismatch");
        }
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL) {
        return false;
    }
    minic_c0_local_initialize(&local);
    local.name_span = value->span;
    local.type = temporary_type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !minic_parser_statement_local_lvalue(parser, local_id, value->span, &target_id)) {
        minic_parser_error(parser, "cannot create evaluate-once initializer temporary");
        return false;
    }

    (void)memset(&assignment, 0, sizeof(assignment));
    assignment.kind = MINIC_STATEMENT_ASSIGN;
    assignment.span = value->span;
    assignment.target_expression = target_id;
    assignment.expression = value_id;
    assignment.target_statement = MINIC_STATEMENT_INVALID;
    assignment.cleanup_context = parser->cleanup_context;
    assignment.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    assignment.then_block = MINIC_BLOCK_INVALID;
    assignment.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &assignment)) {
        return false;
    }

    (void)memset(&lvalue_read, 0, sizeof(lvalue_read));
    lvalue_read.kind = MINIC_EXPRESSION_LVALUE_READ;
    lvalue_read.span = value->span;
    lvalue_read.type = temporary_type;
    lvalue_read.value_category = MINIC_VALUE_RVALUE;
    lvalue_read.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &lvalue_read, read_id);
}

static bool lower_runtime_scalar_array_plan(MinicParser *parser,
                                            MinicExpressionId base_id,
                                            MinicType element_type,
                                            size_t element_count,
                                            MinicSourceSpan initializer_span,
                                            const MinicArrayInitializerPlan *plan,
                                            const MinicExpressionId *action_values) {
    MinicArrayInitializerResolved resolved = {0};
    size_t action_id, index;
    bool success = false;

    if (!minic_array_initializer_plan_resolve(plan, &resolved) ||
        resolved.element_count != element_count) {
        if (parser != NULL)
            minic_parser_error(parser, "cannot resolve runtime array initializer owners");
        goto done;
    }
    for (index = 0U; index < element_count; ++index) {
        if (resolved.owners[index] == MINIC_INITIALIZER_ACTION_INVALID &&
            !add_array_object_zero_element(parser, base_id, index, initializer_span))
            goto done;
    }
    for (action_id = 0U; action_id < resolved.action_count; ++action_id) {
        MinicExpressionId lowered_value_id;
        if (resolved.final_counts[action_id] == 0U) {
            /* Preserve the existing GCC-compatible fully-overridden effect elision. */
            continue;
        }
        lowered_value_id = action_values[action_id];
        if (lowered_value_id == MINIC_EXPRESSION_INVALID)
            goto done;
        if (resolved.final_counts[action_id] > 1U &&
            !add_runtime_initializer_once_read(
                parser, element_type, lowered_value_id, &lowered_value_id))
            goto done;
        for (index = resolved.first_owned[action_id];
             index != MINIC_INITIALIZER_ACTION_INVALID;
             index = resolved.next_owned[index]) {
            if (!add_array_object_element_assignment(
                    parser, base_id, index, lowered_value_id))
                goto done;
        }
    }
    success = true;
done:
    minic_array_initializer_resolved_destroy(&resolved);
    return success;
}

static bool parse_fixed_runtime_scalar_array_initializer(MinicParser *parser,
                                                         MinicExpressionId base_id,
                                                         MinicType element_type,
                                                         size_t element_count) {
    MinicArrayInitializerPlan plan;
    MinicExpressionId *action_values;
    MinicSourceSpan initializer_span;
    size_t action_capacity;
    bool success;

    action_values = NULL;
    action_capacity = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, false);
    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE ||
        minic_type_is_record(element_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid runtime scalar array initializer");
        }
        goto done;
    }
    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;
        size_t action_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(parser, element_count, false, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many runtime array initializer elements");
            goto done;
        }
        if (!grow_runtime_array_action_values(
                parser, &action_values, &action_capacity, action_id + 1U) ||
            !minic_parser_parse_expression(parser, &value_id, 0U)) {
            goto done;
        }
        action_values[action_id] = value_id;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            goto done;
        }
    }
    initializer_span.end = parser->current.span.end;
    if (!minic_parser_advance(parser) ||
        !lower_runtime_scalar_array_plan(
            parser, base_id, element_type, element_count, initializer_span, &plan, action_values)) {
        goto done;
    }
    success = true;

done:
    free(action_values);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                  MinicExpressionId base_id,
                                                  size_t element_count);
static bool parse_runtime_character_array_string_initializer(MinicParser *parser,
                                                             MinicExpressionId base_id,
                                                             MinicType element_type,
                                                             size_t element_count);

static bool parse_fixed_runtime_nested_array_initializer(
    MinicParser *parser, MinicExpressionId base_id, size_t element_count) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;
    const MinicArrayType *nested_array;
    MinicSourceSpan initializer_span;
    size_t index;

    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "nested runtime array initializer requires braces");
        }
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
        !minic_type_is_array(array_info.element_type)) {
        minic_parser_error(parser, "invalid nested runtime array initializer target");
        return false;
    }
    nested_array =
        minic_c0_program_array_type(parser->program, array_info.element_type.array_type_id);
    if (nested_array == NULL || nested_array->element_count == 0U ||
        nested_array->is_zero_length) {
        minic_parser_error(parser, "nested runtime array initializer requires fixed inner extent");
        return false;
    }

    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId element_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            minic_parser_error(
                parser, "designated nested runtime array initializers are not supported yet");
            return false;
        }
        if (index >= element_count) {
            minic_parser_error(parser, "too many nested runtime array initializer elements");
            return false;
        }
        if (!add_array_object_element_lvalue(
                parser, base_id, index, parser->current.span, &element_id)) {
            return false;
        }
        if (minic_parser_static_string_element_compatible(
                parser, nested_array->element_type, parser->current.kind)) {
            if (!parse_runtime_character_array_string_initializer(parser,
                                                                  element_id,
                                                                  nested_array->element_type,
                                                                  nested_array->element_count)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            if (!parse_fixed_runtime_array_initializer(
                    parser, element_id, nested_array->element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "invalid nested runtime array initializer element");
                }
                return false;
            }
        } else {
            minic_parser_error(parser, "invalid nested runtime array initializer element");
            return false;
        }
        index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested runtime array initializer");
            return false;
        }
    }
    initializer_span.end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    while (index < element_count) {
        if (!add_array_object_zero_element(parser, base_id, index, initializer_span)) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                  MinicExpressionId base_id,
                                                  size_t element_count) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info)) {
        return false;
    }
    if (minic_type_is_record(array_info.element_type)) {
        return parse_fixed_runtime_record_array_initializer(parser, base_id, element_count);
    }
    if (minic_type_is_array(array_info.element_type)) {
        return parse_fixed_runtime_nested_array_initializer(parser, base_id, element_count);
    }
    return parse_fixed_runtime_scalar_array_initializer(
        parser, base_id, array_info.element_type, element_count);
}

bool minic_parser_parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                        MinicExpressionId base_id,
                                                        size_t element_count) {
    return parse_fixed_runtime_array_initializer(parser, base_id, element_count);
}

static bool parse_local_character_array_string_initializer(MinicParser *parser,
                                                           MinicLocalId local_id,
                                                           bool infer_count,
                                                           size_t declared_count) {
    const MinicLocal *local;
    MinicParser probe;
    MinicSourceSpan initializer_span;
    uint64_t string_size;
    int *values;
    size_t element_size;
    size_t element_count;
    size_t index;
    bool success;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array ||
        !minic_parser_static_string_element_compatible(parser, local->type,
                                                       parser->current.kind)) {
        return false;
    }
    initializer_span = parser->current.span;
    element_count = declared_count;
    if (infer_count) {
        probe = *parser;
        if (!minic_parser_parse_string_literal_size(&probe, &string_size) ||
            !minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, &element_size) ||
            element_size == 0U || string_size == 0U || string_size % (uint64_t)element_size != 0U ||
            string_size / (uint64_t)element_size > SIZE_MAX) {
            minic_parser_error(parser, "cannot infer local character array extent from string");
            return false;
        }
        element_count = (size_t)(string_size / (uint64_t)element_size);
        parser->program->locals[local_id].element_count = element_count;
    }
    if (element_count == 0U) {
        minic_parser_error(parser, "character array string initializer requires nonzero extent");
        return false;
    }

    values = (int *)calloc(element_count, sizeof(*values));
    if (values == NULL) {
        minic_parser_error(parser, "out of memory while decoding local string initializer");
        return false;
    }
    success = minic_parser_parse_bounded_string_literal_values(parser, element_count, values);
    for (index = 0U; success && index < element_count; ++index) {
        MinicExpression value;
        MinicExpressionId value_id;

        (void)memset(&value, 0, sizeof(value));
        value.kind = MINIC_EXPRESSION_INTEGER;
        value.span = initializer_span;
        value.type = minic_type_int();
        value.value_category = MINIC_VALUE_RVALUE;
        value.value.integer_value = (int64_t)values[index];
        success = minic_parser_add_expression(parser, &value, &value_id) &&
                  add_local_array_element_assignment(parser, local_id, index, value_id);
    }
    free(values);
    return success;
}

static bool grow_runtime_array_action_values(MinicParser *parser,
                                             MinicExpressionId **values,
                                             size_t *capacity,
                                             size_t required);
static bool lower_runtime_scalar_array_plan(MinicParser *parser,
                                            MinicExpressionId base_id,
                                            MinicType element_type,
                                            size_t element_count,
                                            MinicSourceSpan initializer_span,
                                            const MinicArrayInitializerPlan *plan,
                                            const MinicExpressionId *action_values);

static bool parse_inferred_runtime_array_initializer(MinicParser *parser,
                                                     MinicLocalId local_id,
                                                     MinicType element_type,
                                                     MinicSourceSpan name_span) {
    MinicArrayInitializerPlan plan;
    MinicExpressionId *action_values;
    MinicSourceSpan initializer_span;
    size_t action_capacity;
    size_t extent;
    bool scalar_elements;
    bool success;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    action_values = NULL;
    action_capacity = 0U;
    scalar_elements = !minic_type_is_record(element_type);
    initializer_span.begin = parser->current.span.begin;
    initializer_span.end = parser->current.span.end;
    minic_array_initializer_plan_initialize(&plan, 0U, true);
    success = false;
    if (!minic_parser_advance(parser)) {
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;
        size_t index;

        if (plan.next_index == SIZE_MAX) {
            minic_parser_error(parser, "too many local array initializers");
            goto done;
        }

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;
            size_t previous_next;

            previous_next = plan.next_index;
            if (!minic_parser_parse_array_designator(
                    parser, 0U, true, &first, &last) ||
                (!scalar_elements && (first != last || first < previous_next)) ||
                !minic_array_initializer_plan_add_designated(
                    &plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL &&
                    parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        scalar_elements
                            ? "cannot plan inferred local array designator"
                            : "inferred record-array designators require monotonic single indices");
                }
                goto done;
            }
            if (!scalar_elements) {
                MinicExpressionId base_id;
                size_t gap_index;

                if (!minic_parser_statement_local_lvalue(parser, local_id, name_span, &base_id)) {
                    goto done;
                }
                for (gap_index = previous_next; gap_index < first; ++gap_index) {
                    if (!add_array_object_zero_element(
                            parser, base_id, gap_index, parser->current.span)) {
                        goto done;
                    }
                }
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "out of memory while planning local array initializer");
            goto done;
        }

        index = plan.actions[action_id].first_index;
        if (!scalar_elements) {
            MinicExpressionId base_id;
            MinicExpressionId element_id;

            if (!minic_parser_statement_local_lvalue(parser, local_id, name_span, &base_id) ||
                !add_array_object_element_lvalue(
                    parser, base_id, index, parser->current.span, &element_id) ||
                !parse_runtime_record_array_element_initializer(
                    parser, element_id, element_type)) {
                goto done;
            }
        } else {
            MinicExpressionId value_id;

            if (!grow_runtime_array_action_values(
                    parser, &action_values, &action_capacity, action_id + 1U) ||
                !minic_parser_parse_expression(parser, &value_id, 0U)) {
                goto done;
            }
            action_values[action_id] = value_id;
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in local array initializer");
            goto done;
        }
    }

    extent = minic_array_initializer_plan_element_count(&plan);
    if (extent == 0U) {
        minic_parser_error(parser, "inferred local array initializer must not be empty");
        goto done;
    }
    parser->program->locals[local_id].element_count = extent;
    initializer_span.end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        goto done;
    }

    if (scalar_elements) {
        MinicExpressionId base_id;

        if (!minic_parser_statement_local_lvalue(parser, local_id, name_span, &base_id) ||
            !lower_runtime_scalar_array_plan(parser,
                                             base_id,
                                             element_type,
                                             extent,
                                             initializer_span,
                                             &plan,
                                             action_values)) {
            goto done;
        }
    }
    success = true;

done:
    free(action_values);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}


bool
minic_parser_runtime_local_array(MinicParser *parser, MinicLocalId local_id, bool infer_count) {
    const MinicLocal *local;
    MinicSourceSpan name_span;
    MinicType element_type;
    size_t declared_count;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer target");
        return false;
    }
    declared_count = local->element_count;
    element_type = local->type;
    name_span = local->name_span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (minic_parser_static_string_element_compatible(parser, element_type,
                                                       parser->current.kind)) {
        return parse_local_character_array_string_initializer(
            parser, local_id, infer_count, declared_count);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
    if (!infer_count) {
        MinicExpressionId base_id;

        if (!minic_parser_statement_local_lvalue(parser, local_id, name_span, &base_id)) {
            return false;
        }
        return parse_fixed_runtime_array_initializer(parser, base_id, declared_count);
    }
    return parse_inferred_runtime_array_initializer(parser, local_id, element_type, name_span);
}

static bool add_zero_assignment_to_lvalue(MinicParser *parser,
                                          MinicExpressionId target_id,
                                          MinicSourceSpan initializer_span) {
    const MinicExpression *target;
    MinicExpression zero;
    MinicExpressionId value_id;
    MinicStatement statement;
    MinicType target_type;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "invalid aggregate zero target");
        return false;
    }
    target_type = target->type;
    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    if (!minic_parser_add_expression(parser, &zero, &value_id) ||
        !minic_parser_statement_assignment_conversion(parser, target_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
        minic_parser_error(parser, "aggregate zero initializer does not match member type");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = initializer_span;
    statement.target_expression = target_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_zero_initialized_record_lvalue(MinicParser *parser,
                                               MinicExpressionId base_id,
                                               MinicSourceSpan initializer_span) {
    const MinicExpression *base;
    const MinicRecord *record;
    MinicExpression address;
    MinicExpressionId address_id;
    MinicRecordId record_id;
    MinicSourceSpan base_span;
    MinicType base_type;
    size_t field_index;
    size_t field_limit;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "aggregate zero initializer requires a record lvalue");
        return false;
    }
    base_span = base->span;
    base_type = base->type;
    record_id = base_type.record_id;
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "aggregate zero initializer requires a complete record");
        return false;
    }
    field_limit = record->is_union ? (record->field_count == 0U ? 0U : 1U) : record->field_count;

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base_span;
    if (!minic_type_pointer_to(base_type, &address.type)) {
        minic_parser_error(parser, "record initializer address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }

    for (field_index = 0U; field_index < field_limit; ++field_index) {
        const MinicRecordField *field;
        MinicExpression member;
        MinicExpressionId member_id;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL) {
            minic_parser_error(parser, "invalid record field in aggregate initialization");
            return false;
        }
        /* Unnamed bit-fields are layout-only padding/alignment slots, not
           members of the aggregate initializer sequence. In particular musl's
           struct timespec uses target-dependent zero-width unnamed bit-fields;
           emitting an assignment for them creates a non-addressable lvalue. */
        if (field->is_flexible_array || (field->is_bit_field && field->name_length == 0U)) {
            continue;
        }
        (void)memset(&member, 0, sizeof(member));
        member.kind = MINIC_EXPRESSION_MEMBER;
        member.span = initializer_span;
        member.type = field->type;
        if (minic_type_is_const(base_type) && !minic_type_add_const(member.type, &member.type)) {
            minic_parser_error(parser, "cannot propagate const to zero-initialized record member");
            return false;
        }
        member.value_category = MINIC_VALUE_LVALUE;
        member.value.member.base = address_id;
        member.value.member.record_id = record_id;
        member.value.member.field_index = field_index;
        if (!minic_parser_add_expression(parser, &member, &member_id)) {
            return false;
        }
        if (field->is_array) {
            if (!add_array_object_zero_elements(
                    parser, member_id, field->element_count, initializer_span)) {
                return false;
            }
        } else if (minic_type_is_record(field->type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
    }
    return true;
}

static bool add_record_field_lvalue(MinicParser *parser,
                                    MinicExpressionId target_id,
                                    MinicRecordId record_id,
                                    size_t field_index,
                                    MinicSourceSpan span,
                                    MinicExpressionId *member_id) {
    const MinicExpression *target;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicExpression address;
    MinicExpression member;
    MinicExpressionId address_id;
    MinicType target_type;

    if (parser == NULL || member_id == NULL) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, target_id);
    record = minic_c0_program_record(parser->program, record_id);
    field = minic_c0_record_field(record, field_index);
    if (target == NULL || record == NULL || field == NULL ||
        target->value_category != MINIC_VALUE_LVALUE || !minic_type_is_record(target->type) ||
        target->type.record_id != record_id || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported positional record initializer field");
        return false;
    }
    target_type = target->type;

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = target->span;
    if (!minic_type_pointer_to(target_type, &address.type)) {
        minic_parser_error(parser, "record initializer address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = target_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }

    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span = span;
    member.type = field->type;
    if (minic_type_is_const(target_type) && !minic_type_add_const(member.type, &member.type)) {
        minic_parser_error(parser, "cannot propagate const to record initializer member");
        return false;
    }
    member.value_category = MINIC_VALUE_LVALUE;
    member.value.member.base = address_id;
    member.value.member.record_id = record_id;
    member.value.member.field_index = field_index;
    return minic_parser_add_expression(parser, &member, member_id);
}

static bool parse_runtime_character_array_string_initializer(MinicParser *parser,
                                                             MinicExpressionId base_id,
                                                             MinicType element_type,
                                                             size_t element_count) {
    MinicSourceSpan initializer_span;
    int *values;
    size_t index;
    bool success;

    if (parser == NULL || element_count == 0U ||
        !minic_parser_static_string_element_compatible(parser, element_type,
                                                       parser->current.kind)) {
        return false;
    }
    initializer_span = parser->current.span;
    values = (int *)calloc(element_count, sizeof(*values));
    if (values == NULL) {
        minic_parser_error(parser, "out of memory while decoding record character array string");
        return false;
    }
    success = minic_parser_parse_bounded_string_literal_values(parser, element_count, values);
    for (index = 0U; success && index < element_count; ++index) {
        MinicExpression value;
        MinicExpressionId value_id;

        (void)memset(&value, 0, sizeof(value));
        value.kind = MINIC_EXPRESSION_INTEGER;
        value.span = initializer_span;
        value.type = minic_type_int();
        value.value_category = MINIC_VALUE_RVALUE;
        value.value.integer_value = (int64_t)values[index];
        success = minic_parser_add_expression(parser, &value, &value_id) &&
                  add_array_object_element_assignment(parser, base_id, index, value_id);
    }
    free(values);
    return success;
}

static bool add_runtime_record_member_assignment(MinicParser *parser,
                                                 MinicExpressionId member_id,
                                                 MinicExpressionId value_id) {
    const MinicExpression *member;
    const MinicExpression *value;
    MinicSourceSpan member_span;
    MinicStatement statement;
    MinicType member_type;

    member = minic_c0_program_expression(parser->program, member_id);
    if (member == NULL || member->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "record initializer member is not assignable");
        return false;
    }
    member_span = member->span;
    member_type = member->type;
    if (!minic_parser_statement_assignment_conversion(parser, member_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, member_type, value_id)) {
        minic_parser_error(parser, "record initializer value type mismatch");
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL) {
        minic_parser_error(parser, "invalid record initializer value");
        return false;
    }

    if (minic_type_is_record(member_type)) {
        if (!minic_type_is_record(value->type) || value->type.record_id != member_type.record_id ||
            !minic_c0_record_value_is_copy_source(parser->program, value_id)) {
            minic_parser_error(parser,
                               "record initializer member requires a matching record copy source");
            return false;
        }
        return minic_parser_statement_record_initialize(parser, member_id, value_id, value->span);
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = member_span.begin;
    statement.span.end = value->span.end;
    statement.target_expression = member_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool zero_runtime_record_fields_from(MinicParser *parser,
                                            MinicExpressionId target_id,
                                            MinicRecordId record_id,
                                            size_t *field_index,
                                            size_t field_limit,
                                            MinicSourceSpan initializer_span) {
    const MinicRecord *record;

    if (parser == NULL || field_index == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL || !record->is_complete || field_limit > record->field_count) {
        return false;
    }
    while (*field_index < field_limit) {
        MinicExpressionId member_id;

        if (record->fields[*field_index].is_flexible_array) {
            *field_index += 1U;
            continue;
        }
        if (!add_record_field_lvalue(
                parser, target_id, record_id, *field_index, initializer_span, &member_id)) {
            return false;
        }
        if (record->fields[*field_index].is_array) {
            if (!add_array_object_zero_elements(parser,
                                                member_id,
                                                record->fields[*field_index].element_count,
                                                initializer_span)) {
                return false;
            }
        } else if (minic_type_is_record(record->fields[*field_index].type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
        *field_index += 1U;
    }
    return true;
}

static bool parse_runtime_record_designated_tail(MinicParser *parser, MinicExpressionId target_id);

static bool parse_positional_runtime_record_initializer_from(MinicParser *parser,
                                                             MinicExpressionId target_id,
                                                             MinicSourcePosition begin,
                                                             size_t initial_field_index) {
    const MinicExpression *target;
    const MinicRecord *record;
    MinicRecordId record_id;
    MinicSourceSpan initializer_span;
    size_t field_index;
    size_t field_limit;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type)) {
        minic_parser_error(parser, "positional initializer requires a record lvalue");
        return false;
    }
    record_id = target->type.record_id;
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           "positional runtime initializer requires a complete record type");
        return false;
    }
    field_limit = record->is_union ? (record->field_count == 0U ? 0U : 1U) : record->field_count;
    initializer_span.begin = begin;
    initializer_span.end = parser->current.span.end;
    field_index = initial_field_index;
    if (field_index > field_limit) {
        minic_parser_error(parser, "invalid positional record initializer resume field");
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        MinicExpressionId member_id;
        MinicSourceSpan member_span;

        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many positional record initializers");
            return false;
        }
        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported positional record initializer field");
            return false;
        }
        member_span.begin = parser->current.span.begin;
        member_span.end = parser->current.span.end;
        if (!add_record_field_lvalue(
                parser, target_id, record_id, field_index, member_span, &member_id)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE && field->is_array) {
            if (!parse_fixed_runtime_array_initializer(parser, member_id, field->element_count)) {
                return false;
            }
        } else if (field->is_array && parser->current.kind == MINIC_TOKEN_STRING_LITERAL &&
                   minic_type_is_char_integer(field->type)) {
            if (!parse_runtime_character_array_string_initializer(
                    parser, member_id, field->type, field->element_count)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE &&
                   minic_type_is_record(field->type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (field->is_array) {
                minic_parser_error(parser,
                                   "runtime record array field initializer requires braces");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U)) {
                return false;
            }
            if (minic_type_is_record(field->type)) {
                const MinicExpression *value;
                MinicConstValue constant;
                MinicSourceSpan value_span;
                bool is_zero;
                bool matching_record_value;

                value = minic_c0_program_expression(parser->program, value_id);
                matching_record_value =
                    value != NULL && minic_type_is_record(value->type) &&
                    value->type.record_id == field->type.record_id &&
                    minic_c0_record_value_is_copy_source(parser->program, value_id);
                if (matching_record_value) {
                    /* BRACE_ELISION_RECORD_VALUE: a positional aggregate field
                       may be initialized by a matching record-valued expression
                       (including compound literals and GNU statement-expression
                       wrappers).  Preserve the established record-copy owner;
                       the integer-zero shorthand below is an additional route,
                       not a replacement for ordinary record initialization. */
                    if (!add_runtime_record_member_assignment(parser, member_id, value_id)) {
                        return false;
                    }
                } else {
                    if (value == NULL ||
                        !minic_const_eval_integer(
                            parser->program, parser->target_info, value_id, &constant) ||
                        !minic_const_value_is_zero(
                            parser->program, parser->target_info, &constant, &is_zero) ||
                        !is_zero) {
                        minic_parser_error(
                            parser,
                            "brace-elided aggregate initializer requires a matching record value or integer constant zero");
                        return false;
                    }
                    value_span = value->span;
                    if (!add_zero_initialized_record_lvalue(parser, member_id, value_span)) {
                        return false;
                    }
                }
            } else if (!add_runtime_record_member_assignment(parser, member_id, value_id)) {
                return false;
            }
        }
        field_index += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            if (parser->current.kind == MINIC_TOKEN_DOT) {
                initializer_span.end = parser->current.span.end;
                if (!zero_runtime_record_fields_from(parser,
                                                     target_id,
                                                     record_id,
                                                     &field_index,
                                                     field_limit,
                                                     initializer_span)) {
                    return false;
                }
                return parse_runtime_record_designated_tail(parser, target_id);
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in positional record initializer");
            return false;
        }
    }
    initializer_span.end = parser->current.span.end;

    if (!zero_runtime_record_fields_from(
            parser, target_id, record_id, &field_index, field_limit, initializer_span)) {
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_runtime_record_designator_target(MinicParser *parser,
                                                   MinicExpressionId base_id,
                                                   MinicExpressionId *target_id,
                                                   size_t *top_level_field_index) {
    MinicExpressionId current_id;

    if (parser == NULL || target_id == NULL || top_level_field_index == NULL ||
        parser->current.kind != MINIC_TOKEN_DOT) {
        minic_parser_error(parser, "expected designated record initializer");
        return false;
    }
    *top_level_field_index = SIZE_MAX;
    current_id = base_id;
    do {
        MinicExpressionId member_id;

        if (!minic_parser_parse_direct_member(parser, current_id, &member_id)) {
            return false;
        }
        if (*top_level_field_index == SIZE_MAX) {
            const MinicExpression *top_member =
                minic_c0_program_expression(parser->program, member_id);
            if (top_member == NULL || top_member->kind != MINIC_EXPRESSION_MEMBER) {
                minic_parser_error(parser, "invalid top-level record designator member");
                return false;
            }
            *top_level_field_index = top_member->value.member.field_index;
        }
        current_id = member_id;
        while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            const MinicExpression *array_expression;
            MinicArrayObjectInfo array_info;
            MinicSourceSpan designator_span;
            size_t first;
            size_t last;

            array_expression = minic_c0_program_expression(parser->program, current_id);
            if (array_expression == NULL ||
                !minic_c0_expression_array_object_info(
                    parser->program, array_expression, &array_info) ||
                array_info.is_incomplete || array_info.is_zero_length) {
                minic_parser_error(parser,
                                   "record designator subscript requires a fixed array member");
                return false;
            }
            designator_span = parser->current.span;
            if (!minic_parser_parse_array_designator_component(parser,
                                                               array_info.element_count,
                                                               false,
                                                               &first,
                                                               &last) ||
                first != last) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "record designator requires one array element");
                }
                return false;
            }
            if (!add_array_object_element_lvalue(
                    parser, current_id, first, designator_span, &current_id)) {
                return false;
            }
        }
    } while (parser->current.kind == MINIC_TOKEN_DOT);
    *target_id = current_id;
    return true;
}

static bool parse_runtime_record_designated_tail(MinicParser *parser, MinicExpressionId target_id) {
    for (;;) {
        MinicExpressionId member_id;
        const MinicExpression *member;
        size_t top_level_field_index;
        MinicArrayObjectInfo member_array;
        MinicType member_type;
        bool member_is_array;

        if (!parse_runtime_record_designator_target(
                parser, target_id, &member_id, &top_level_field_index) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after record designator")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "expected designated record initializer");
            }
            return false;
        }
        member = minic_c0_program_expression(parser->program, member_id);
        if (member == NULL || member->value_category != MINIC_VALUE_LVALUE) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
        member_type = member->type;
        member_is_array =
            minic_c0_expression_array_object_info(parser->program, member, &member_array);
        if (parser->current.kind == MINIC_TOKEN_LBRACE && member_is_array) {
            if (member_array.is_incomplete || member_array.is_zero_length ||
                !parse_fixed_runtime_array_initializer(
                    parser, member_id, member_array.element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "unsupported designated record array initializer");
                }
                return false;
            }
        } else if (member_is_array && parser->current.kind == MINIC_TOKEN_STRING_LITERAL &&
                   minic_type_is_char_integer(member_array.element_type) &&
                   !member_array.is_incomplete && !member_array.is_zero_length) {
            if (!parse_runtime_character_array_string_initializer(parser,
                                                                  member_id,
                                                                  member_array.element_type,
                                                                  member_array.element_count)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE &&
                   minic_type_is_record(member_type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (member_is_array) {
                minic_parser_error(parser,
                                   "runtime record array field initializer requires braces");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "record designated initializer type mismatch");
                }
                return false;
            }
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                return minic_parser_advance(parser);
            }
            if (parser->current.kind != MINIC_TOKEN_DOT) {
                const MinicExpression *root =
                    minic_c0_program_expression(parser->program, target_id);
                const MinicRecord *root_record =
                    root != NULL && minic_type_is_record(root->type)
                        ? minic_c0_program_record(parser->program, root->type.record_id)
                        : NULL;
                size_t field_limit =
                    root_record == NULL ? 0U
                                        : (root_record->is_union
                                               ? (root_record->field_count == 0U ? 0U : 1U)
                                               : root_record->field_count);
                size_t next_field = top_level_field_index + 1U;

                if (root_record == NULL || top_level_field_index == SIZE_MAX ||
                    next_field > field_limit) {
                    minic_parser_error(
                        parser, "cannot resume positional initializer after record designator");
                    return false;
                }
                return parse_positional_runtime_record_initializer_from(
                    parser, target_id, member->span.begin, next_field);
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in designated record initializer");
            return false;
        }
        return minic_parser_advance(parser);
    }
}

bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,
                                                   MinicExpressionId target_id) {
    MinicSourceSpan initializer_span;
    MinicSourceSpan zero_span;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    zero_span = parser->current.span;
    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        const MinicExpression *target;
        const MinicRecord *record;
        MinicParser probe;
        int zero_value;
        bool zero_shorthand;

        target = minic_c0_program_expression(parser->program, target_id);
        record = target != NULL && minic_type_is_record(target->type)
                     ? minic_c0_program_record(parser->program, target->type.record_id)
                     : NULL;
        probe = *parser;
        zero_shorthand = record != NULL && !record->is_union &&
                         minic_parser_parse_integer_value(&probe, &zero_value) && zero_value == 0;
        if (zero_shorthand && probe.current.kind == MINIC_TOKEN_COMMA) {
            zero_shorthand =
                minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_RBRACE;
        } else if (zero_shorthand) {
            zero_shorthand = probe.current.kind == MINIC_TOKEN_RBRACE;
        }
        if (zero_shorthand) {
            int parsed_zero;

            if (!minic_parser_parse_integer_value(parser, &parsed_zero)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected '}' after aggregate zero initializer");
                return false;
            }
            initializer_span.end = parser->current.span.end;
            return minic_parser_advance(parser) &&
                   add_zero_initialized_record_lvalue(parser, target_id, initializer_span);
        }
    }
    if (parser->current.kind != MINIC_TOKEN_DOT) {
        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            initializer_span.end = parser->current.span.end;
            return minic_parser_advance(parser) &&
                   add_zero_initialized_record_lvalue(parser, target_id, initializer_span);
        }
        return parse_positional_runtime_record_initializer_from(
            parser, target_id, initializer_span.begin, 0U);
    }
    if (!add_zero_initialized_record_lvalue(parser, target_id, zero_span)) {
        return false;
    }

    return parse_runtime_record_designated_tail(parser, target_id);
}
