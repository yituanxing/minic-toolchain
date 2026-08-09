#include "frontend/parser_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array);
static bool parse_expression_internal(MinicParser *parser,
                                      MinicExpressionId *expression_id,
                                      unsigned int minimum_precedence,
                                      bool decay_array);
static bool type_is_complete_object(const MinicC0Program *program, MinicType type);

static bool finish_value_expression(MinicParser *parser,
                                    MinicExpressionId input_id,
                                    bool decay_array,
                                    MinicExpressionId *expression_id) {
    if (!decay_array) {
        *expression_id = input_id;
        return true;
    }
    return minic_parser_apply_array_decay(parser, input_id, expression_id);
}

static MinicType integer_literal_type(const MinicParser *parser, MinicSourceSpan span) {
    bool saw_long;
    bool saw_unsigned;
    size_t offset;

    saw_long = false;
    saw_unsigned = false;
    offset = span.end.offset;
    while (offset > span.begin.offset) {
        char character;

        character = parser->source[offset - 1U];
        if (character == 'l' || character == 'L') {
            saw_long = true;
        } else if (character == 'u' || character == 'U') {
            saw_unsigned = true;
        } else {
            break;
        }
        offset -= 1U;
    }
    if (saw_long) {
        return saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
    }
    return saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
}

static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    MinicType literal_type;
    int value;

    span = parser->current.span;
    literal_type = parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT
                       ? minic_type_int()
                       : integer_literal_type(parser, span);
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = literal_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    if (!minic_parser_parse_integer_value(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_floating(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    char *end;
    char *text;
    double value;
    size_t length;

    _Static_assert(sizeof(double) == sizeof(uint64_t), "MiniC requires binary64 host double");

    if (parser->current.kind != MINIC_TOKEN_FLOATING_CONSTANT) {
        minic_parser_error(parser, "expected floating constant");
        return false;
    }
    span = parser->current.span;
    length = span.end.offset - span.begin.offset;
    if (length == SIZE_MAX) {
        minic_parser_error(parser, "floating constant is too long");
        return false;
    }
    text = (char *)malloc(length + 1U);
    if (text == NULL) {
        minic_parser_error(parser, "out of memory while parsing floating constant");
        return false;
    }
    (void)memcpy(text, parser->source + span.begin.offset, length);
    text[length] = '\0';

    errno = 0;
    end = NULL;
    value = strtod(text, &end);
    if (end != text + length || errno == ERANGE) {
        free(text);
        minic_parser_error(parser, "invalid or out-of-range floating constant");
        return false;
    }
    free(text);

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_FLOATING;
    expression.span = span;
    expression.type = minic_type_double();
    expression.value_category = MINIC_VALUE_RVALUE;
    (void)memcpy(&expression.value.floating_bits, &value, sizeof(value));
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_local_reference(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicLocalId local_id,
                                  MinicExpressionId *expression_id) {
    const MinicLocal *local;
    MinicExpression base_expression;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL) {
        minic_parser_error(parser, "invalid local reference");
        return false;
    }

    (void)memset(&base_expression, 0, sizeof(base_expression));
    base_expression.kind = MINIC_EXPRESSION_LOCAL;
    base_expression.span = name_span;
    base_expression.type = local->type;
    base_expression.value_category = MINIC_VALUE_LVALUE;
    base_expression.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &base_expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

static bool parse_global_reference(MinicParser *parser,
                                   MinicSourceSpan name_span,
                                   MinicGlobalObjectId global_object_id,
                                   bool allow_record_lvalue,
                                   MinicExpressionId *expression_id) {
    const MinicGlobalObject *object;
    MinicExpression base_expression;
    MinicExpressionId base_id;
    bool array_object;

    object = minic_c0_program_global_object(parser->program, global_object_id);
    if (object == NULL) {
        minic_parser_error(parser, "invalid global object reference");
        return false;
    }
    array_object = minic_type_is_array(object->type);
    if (!array_object && minic_type_is_record(object->type)) {
        if (parser->current.kind != MINIC_TOKEN_DOT && !allow_record_lvalue) {
            minic_parser_error(parser, "global record object requires member access");
            return false;
        }
    } else if (!array_object) {
        minic_parser_error(parser, "invalid global object reference");
        return false;
    }
    (void)memset(&base_expression, 0, sizeof(base_expression));
    base_expression.kind = MINIC_EXPRESSION_GLOBAL;
    base_expression.span = name_span;
    base_expression.type = object->type;
    base_expression.value_category = MINIC_VALUE_LVALUE;
    base_expression.value.global_object_id = global_object_id;
    if (!minic_parser_add_expression(parser, &base_expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

static bool expression_is_scalar(const MinicExpression *expression) {
    return expression != NULL && (minic_type_is_integer(expression->type) ||
                                  minic_type_is_pointer(expression->type) ||
                                  minic_type_is_float(expression->type) ||
                                  minic_type_is_double(expression->type));
}

static bool expression_is_integer(const MinicExpression *expression) {
    return expression != NULL && minic_type_is_integer(expression->type);
}

static bool expression_is_modifiable_lvalue(const MinicExpression *expression) {
    return expression != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
           !minic_type_is_const(expression->type) && !minic_type_is_array(expression->type) &&
           !minic_type_is_function(expression->type);
}

static bool expression_is_complete_object(const MinicParser *parser,
                                          const MinicExpression *expression) {
    return expression != NULL && type_is_complete_object(parser->program, expression->type);
}

static bool type_is_complete_object(const MinicC0Program *program, MinicType type) {
    if (minic_type_is_void(type) || minic_type_is_function(type)) {
        return false;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete;
    }
    return true;
}

static bool parser_add_cast(MinicParser *parser,
                            MinicExpressionId source_id,
                            MinicType target_type,
                            MinicExpressionId *result_id) {
    const MinicExpression *source;
    MinicExpression cast;

    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL || result_id == NULL) {
        return false;
    }
    (void)memset(&cast, 0, sizeof(cast));
    cast.kind = MINIC_EXPRESSION_CAST;
    cast.span = source->span;
    cast.type = target_type;
    cast.value_category = MINIC_VALUE_RVALUE;
    cast.value.unary.operand = source_id;
    if (!minic_parser_add_expression(parser, &cast, result_id)) {
        minic_parser_error(parser, "cannot add cast expression");
        return false;
    }
    return true;
}

static bool parser_add_lvalue_read(MinicParser *parser,
                                   MinicExpressionId source_id,
                                   MinicExpressionId *result_id) {
    const MinicExpression *source;
    MinicExpression read;

    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL || source->value_category != MINIC_VALUE_LVALUE || result_id == NULL) {
        return false;
    }
    (void)memset(&read, 0, sizeof(read));
    read.kind = MINIC_EXPRESSION_LVALUE_READ;
    read.span = source->span;
    read.type = source->type;
    read.value_category = MINIC_VALUE_RVALUE;
    read.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &read, result_id);
}

static bool parser_prepare_value(MinicParser *parser, MinicExpressionId *expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, *expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE && !minic_type_is_array(expression->type) &&
        !minic_type_is_function(expression->type) && !minic_type_is_record(expression->type)) {
        return parser_add_lvalue_read(parser, *expression_id, expression_id);
    }
    return true;
}

static bool parser_convert_value(MinicParser *parser,
                                 MinicExpressionId source_id,
                                 MinicType target_type,
                                 MinicExpressionId *result_id) {
    const MinicExpression *source;

    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL || result_id == NULL) {
        return false;
    }
    if (minic_type_equal(source->type, target_type)) {
        *result_id = source_id;
        return true;
    }
    if ((minic_type_is_integer(source->type) && minic_type_is_integer(target_type)) ||
        (minic_type_is_float(source->type) && minic_type_is_double(target_type)) ||
        (minic_type_is_double(source->type) && minic_type_is_float(target_type)) ||
        (minic_type_is_pointer(source->type) && minic_type_is_pointer(target_type))) {
        return parser_add_cast(parser, source_id, target_type, result_id);
    }
    return false;
}

static bool parse_call_argument(MinicParser *parser,
                                MinicExpression *call_expression,
                                const MinicFunction *callee,
                                size_t argument_index) {
    MinicExpressionId argument_id;
    MinicExpressionId converted_id;
    MinicType target_type;

    if (!minic_parser_parse_expression(parser, &argument_id, 0U)) {
        return false;
    }
    if (!parser_prepare_value(parser, &argument_id)) {
        return false;
    }
    if (argument_index < callee->parameter_count) {
        target_type = callee->parameter_types[argument_index];
        if (!parser_convert_value(parser, argument_id, target_type, &converted_id)) {
            minic_parser_error(parser, "call argument type does not match declaration");
            return false;
        }
        argument_id = converted_id;
    }
    call_expression->value.call.arguments[argument_index] = argument_id;
    return true;
}

static bool parse_call_arguments(MinicParser *parser,
                                 MinicExpression *call_expression,
                                 const MinicFunction *callee) {
    size_t argument_count;

    argument_count = 0U;
    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
        call_expression->value.call.argument_count = 0U;
        return true;
    }
    if (!parse_call_argument(parser, call_expression, callee, argument_count)) {
        return false;
    }
    argument_count += 1U;

    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (argument_count >= 8U) {
            minic_parser_error(parser, "variadic call supports at most 8 arguments");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !parse_call_argument(parser, call_expression, callee, argument_count)) {
            return false;
        }
        argument_count += 1U;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "call argument count does not match declaration");
        return false;
    }
    call_expression->value.call.argument_count = argument_count;
    return true;
}

static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpression expression;
    MinicExpressionId primary_id;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicFunctionId function_id;
    MinicGlobalObjectId global_object_id;

    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT ||
        parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        if (!parse_integer(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_FLOATING_CONSTANT) {
        if (!parse_floating(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_parse_string_literal(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        function_id = minic_parser_find_function(parser, name_span);
        global_object_id = minic_parser_find_global_object(parser, name_span);
        if (!minic_parser_advance(parser)) {
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            MinicSourcePosition call_end;
            const MinicFunction *callee;

            if (local_id != MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "called object is a local variable");
                return false;
            }
            if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
                minic_parser_error(parser, "called object is a global object");
                return false;
            }
            if (function_id == MINIC_FUNCTION_INVALID) {
                minic_parser_error(parser, "call to function not yet declared");
                return false;
            }
            callee = minic_c0_program_function(parser->program, function_id);
            if (callee == NULL || callee->parameter_count > 8U || !minic_parser_advance(parser)) {
                minic_parser_error(parser, "unsupported function call signature");
                return false;
            }
            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_CALL;
            expression.span.begin = name_span.begin;
            expression.type = callee->return_type;
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.call.function_id = function_id;
            expression.value.call.is_indirect = false;
            expression.value.call.callee_expression = MINIC_EXPRESSION_INVALID;
            if (!parse_call_arguments(parser, &expression, callee)) {
                return false;
            }
            call_end = parser->current.span.end;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            expression.span.end = call_end;
            if (!minic_parser_add_expression(parser, &expression, &primary_id)) {
                return false;
            }
            return minic_parser_parse_postfix(parser, primary_id, expression_id);
        }

        if (local_id != MINIC_LOCAL_INVALID) {
            if (!parse_local_reference(parser, name_span, local_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (function_id != MINIC_FUNCTION_INVALID) {
            if (!parse_function_designator(parser, name_span, function_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            if (!parse_global_reference(
                    parser, name_span, global_object_id, !decay_array, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        minic_parser_error(parser, "use of undeclared local");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser probe;
        MinicType cast_type;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (token_starts_type_name(probe.current.kind)) {
            if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &cast_type) ||
                !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type") ||
                !parse_unary(parser, &primary_id, true)) {
                return false;
            }
            if (!build_cast_expression(parser, primary_id, cast_type, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &primary_id, 0U) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    minic_parser_error(parser, "expected primary expression");
    return false;
}

/* Remaining expression parsing implementation unchanged. */
