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
static bool
parse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array);
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
    unsigned int long_count;
    bool saw_unsigned;
    size_t offset;

    long_count = 0U;
    saw_unsigned = false;
    offset = span.end.offset;
    while (offset > span.begin.offset) {
        char character;

        character = parser->source[offset - 1U];
        if (character == 'l' || character == 'L') {
            long_count += 1U;
        } else if (character == 'u' || character == 'U') {
            saw_unsigned = true;
        } else {
            break;
        }
        offset -= 1U;
    }
    if (long_count >= 2U) {
        return saw_unsigned ? minic_type_unsigned_long_long() : minic_type_long_long();
    }
    if (long_count == 1U) {
        return saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
    }
    return saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
}

static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    MinicType literal_type;
    int64_t value;

    span = parser->current.span;
    literal_type = parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT
                       ? minic_type_int()
                       : integer_literal_type(parser, span);
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = literal_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    if (parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        int character_value;

        if (!minic_parser_parse_integer_value(parser, &character_value)) {
            return false;
        }
        value = (int64_t)character_value;
    } else if (minic_type_is_unsigned_integer(literal_type)) {
        uint64_t unsigned_value;

        if (!minic_parser_parse_unsigned_integer_value64(parser, &unsigned_value)) {
            return false;
        }
        (void)memcpy(&value, &unsigned_value, sizeof(value));
    } else if (!minic_parser_parse_integer_value64(parser, &value)) {
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
    }

    (void)memset(&base_expression, 0, sizeof(base_expression));
    base_expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    base_expression.span = name_span;
    base_expression.type = object->type;
    base_expression.value_category = MINIC_VALUE_LVALUE;
    base_expression.value.global_object_id = global_object_id;
    if (!minic_parser_add_expression(parser, &base_expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

static bool parse_fixed_register_reference(MinicParser *parser,
                                           MinicSourceSpan name_span,
                                           MinicFixedRegisterBindingId binding_id,
                                           MinicExpressionId *expression_id) {
    const MinicFixedRegisterBinding *binding;
    MinicExpression expression;
    MinicExpressionId base_id;

    binding = minic_c0_program_fixed_register_binding(parser->program, binding_id);
    if (binding == NULL) {
        minic_parser_error(parser, "invalid fixed register reference");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_FIXED_REGISTER;
    expression.span = name_span;
    expression.type = binding->type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.fixed_register_binding_id = binding_id;
    if (!minic_parser_add_expression(parser, &expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

static bool parse_function_reference(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicFunctionId function_id,
                                     MinicExpressionId *expression_id) {
    const MinicFunction *function;
    MinicExpression expression;
    MinicExpressionId base_id;
    MinicType function_type;

    function = minic_c0_program_function(parser->program, function_id);
    if (function == NULL) {
        minic_parser_error(parser, "invalid function reference");
        return false;
    }
    if (function->is_variadic) {
        minic_parser_error(parser, "variadic function designator is not supported yet");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    if (!minic_c0_program_add_function_type(parser->program,
                                            function->return_type,
                                            function->parameter_types,
                                            function->parameter_count,
                                            &function_type) ||
        !minic_type_pointer_to(function_type, &expression.type)) {
        minic_parser_error(parser, "cannot form function pointer type");
        return false;
    }
    expression.kind = MINIC_EXPRESSION_FUNCTION;
    expression.span = name_span;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.function_id = function_id;
    if (!minic_parser_add_expression(parser, &expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

static bool token_starts_cast_type(const MinicParser *parser, MinicToken token) {
    return minic_parser_token_starts_type_name(parser, token);
}

static bool parenthesis_starts_cast(const MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicLexer lookahead;
    MinicToken token;

    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }

    lookahead = parser->lexer;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    if (!minic_lexer_next(&lookahead, &token, &diagnostic)) {
        return false;
    }
    return token_starts_cast_type(parser, token);
}

static bool expression_is_integer_zero(const MinicExpression *expression) {
    return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
}

static bool type_is_condition_scalar(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool same_floating_type(MinicType left, MinicType right) {
    return minic_type_equal(left, right) &&
           ((minic_type_is_double(left) && minic_type_is_double(right)) ||
            (minic_type_is_float(left) && minic_type_is_float(right)));
}

static bool parse_cast(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicExpression expression;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicType target_type;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type") ||
        !parse_unary(parser, &operand_id, true)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand != NULL && same_floating_type(target_type, operand->type)) {
        *expression_id = operand_id;
        return true;
    }
    if (operand == NULL ||
        (!minic_type_is_void(target_type) &&
         !minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
        minic_parser_error(parser, "unsupported cast between these types");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span.begin = begin;
    expression.span.end = operand->span.end;
    expression.type = target_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = operand_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool variadic_argument_type_supported(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) || minic_type_is_double(type);
}

static bool apply_fixed_call_argument_conversion(MinicParser *parser,
                                                 MinicType target_type,
                                                 MinicExpressionId *argument_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicSourceSpan source_span;

    if (parser == NULL || argument_id == NULL) {
        return false;
    }
    source_id = *argument_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid call argument conversion source");
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, source_id)) {
        return true;
    }
    if (!minic_type_is_double(target_type) || !minic_type_is_integer(source->type)) {
        return true;
    }
    source_span = source->span;

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source_span;
    conversion.type = target_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, argument_id);
}

static bool parse_call_argument(MinicParser *parser,
                                MinicExpression *call_expression,
                                const MinicFunction *callee,
                                size_t argument_index) {
    const MinicExpression *argument;
    MinicExpressionId argument_id;

    if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_parser_parse_expression(
            parser, &call_expression->value.call.arguments[argument_index], 0U)) {
        return false;
    }
    argument_id = call_expression->value.call.arguments[argument_index];
    argument = minic_c0_program_expression(parser->program, argument_id);
    if (argument == NULL) {
        minic_parser_error(parser, "invalid call argument");
        return false;
    }
    if (argument_index < callee->parameter_count) {
        if (!apply_fixed_call_argument_conversion(
                parser, callee->parameter_types[argument_index], &argument_id)) {
            return false;
        }
        call_expression->value.call.arguments[argument_index] = argument_id;
        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL) {
            minic_parser_error(parser, "invalid converted call argument");
            return false;
        }
        if (!minic_c0_assignment_compatible(
                parser->program, callee->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "call argument type does not match declaration");
            return false;
        }
    } else if (!variadic_argument_type_supported(argument->type)) {
        minic_parser_error(parser, "unsupported variadic argument type");
        return false;
    }
    return true;
}

static bool parse_call_arguments(MinicParser *parser,
                                 MinicExpression *call_expression,
                                 const MinicFunction *callee) {
    size_t argument_count;

    argument_count = 0U;
    while (argument_count < callee->parameter_count) {
        if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            minic_parser_error(parser, "call argument count does not match declaration");
            return false;
        }
        if (!parse_call_argument(parser, call_expression, callee, argument_count)) {
            return false;
        }
        argument_count += 1U;
        if (argument_count < callee->parameter_count) {
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                minic_parser_error(parser, "call argument count does not match declaration");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }

    if (!callee->is_variadic) {
        if (parser->current.kind != MINIC_TOKEN_RPAREN) {
            minic_parser_error(parser, "call argument count does not match declaration");
            return false;
        }
        call_expression->value.call.argument_count = argument_count;
        return true;
    }

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

static bool current_identifier_is(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_builtin_expect(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicExpression conversion;
    MinicExpressionId value_id;
    const MinicExpression *value;
    int64_t expected_value;

    if (parser == NULL || expression_id == NULL ||
        !current_identifier_is(parser, "__builtin_expect")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_expect") ||
        !minic_parser_parse_expression(parser, &value_id, 0U)) {
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL || !minic_type_is_integer(value->type)) {
        minic_parser_error(parser, "__builtin_expect first argument must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser) ||
        !minic_parser_parse_integer_constant_expression(parser, &expected_value)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "__builtin_expect second argument must be an integer constant");
        }
        return false;
    }
    (void)expected_value;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_expect arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* GCC's documented type is long. The prediction hint itself has no runtime semantics,
     * so lower the builtin to the ordinary value conversion and leave optimization metadata
     * for a future IR/branch-probability pass. */
    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span.begin = begin;
    conversion.span.end = end;
    conversion.type = minic_type_long();
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = value_id;
    return minic_parser_add_expression(parser, &conversion, expression_id);
}

static bool current_is_builtin_offsetof(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    size_t field_index;
    size_t field_name_length;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }
    field_span = parser->current.span;
    field_name_length = minic_parser_span_length(field_span);
    field_index = 0U;
    while (field_index < record->field_count) {
        const MinicRecordField *field;

        field = &record->fields[field_index];
        if (field->name_length == field_name_length &&
            memcmp(field->name, parser->source + field_span.begin.offset, field_name_length) == 0) {
            break;
        }
        field_index += 1U;
    }
    if (field_index == record->field_count) {
        minic_parser_error(parser, "record has no such field in __builtin_offsetof");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_offsetof");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_OFFSETOF;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.offsetof_value.record_id = record_type.record_id;
    expression.value.offsetof_value.field_index = field_index;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool generic_token_text_equals(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool generic_types_compatible(MinicType left, MinicType right) {
    MinicType left_unqualified;
    MinicType right_unqualified;

    if (!minic_type_unqualified(left, &left_unqualified) ||
        !minic_type_unqualified(right, &right_unqualified)) {
        return minic_type_equal(left, right);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}

static bool
parse_generic_selection(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId controlling_id;
    const MinicExpression *controlling;
    MinicExpressionId selected_id = MINIC_EXPRESSION_INVALID;
    MinicExpressionId default_id = MINIC_EXPRESSION_INVALID;
    MinicType controlling_type;
    bool saw_matching_type = false;
    bool saw_default = false;

    if (!generic_token_text_equals(parser, "_Generic") || !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after _Generic") ||
        !parse_expression_internal(parser, &controlling_id, 0U, true)) {
        return false;
    }
    controlling = minic_c0_program_expression(parser->program, controlling_id);
    if (controlling == NULL) {
        minic_parser_error(parser, "invalid _Generic controlling expression");
        return false;
    }
    controlling_type = controlling->type;
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected ',' after _Generic controlling expression")) {
        return false;
    }

    for (;;) {
        bool is_default = parser->current.kind == MINIC_TOKEN_KW_DEFAULT;
        MinicType association_type = minic_type_void();
        MinicExpressionId association_id;

        if (is_default) {
            if (saw_default) {
                minic_parser_error(parser, "duplicate default association in _Generic");
                return false;
            }
            saw_default = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_type_name(parser, &association_type)) {
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_COLON, "expected ':' in _Generic association") ||
            !parse_expression_internal(parser, &association_id, 0U, decay_array)) {
            return false;
        }

        if (is_default) {
            default_id = association_id;
        } else if (generic_types_compatible(controlling_type, association_type)) {
            if (saw_matching_type) {
                minic_parser_error(parser, "multiple compatible type associations in _Generic");
                return false;
            }
            saw_matching_type = true;
            selected_id = association_id;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after _Generic associations")) {
        return false;
    }
    if (!saw_matching_type) {
        selected_id = default_id;
    }
    if (selected_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "no compatible association and no default in _Generic");
        return false;
    }
    *expression_id = selected_id;
    return true;
}

static bool parse_builtin_types_compatible_p(MinicParser *parser,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicType left_type;
    MinicType right_type;

    if (!generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &left_type) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &right_type)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = generic_types_compatible(left_type, right_type) ? 1 : 0;
    return minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_types_compatible_p") &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool
parse_builtin_choose_expr(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId condition_id;
    MinicExpressionId when_true_id;
    MinicExpressionId when_false_id;
    MinicConstValue condition_value;
    bool condition_is_zero;

    if (!generic_token_text_equals(parser, "__builtin_choose_expr")) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_choose_expr") ||
        !parse_expression_internal(parser, &condition_id, 0U, true) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected first ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_true_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_false_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_choose_expr")) {
        return false;
    }
    if (!minic_const_eval_integer(
            parser->program, parser->target_info, condition_id, &condition_value) ||
        !minic_const_value_is_zero(
            parser->program, parser->target_info, &condition_value, &condition_is_zero)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    *expression_id = condition_is_zero ? when_false_id : when_true_id;
    return true;
}

static bool object_extent_direct_object(const MinicParser *parser,
                                        MinicExpressionId expression_id,
                                        size_t *extent) {
    const MinicExpression *expression;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;
        size_t element_size;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        if (local == NULL) {
            return false;
        }
        if (!local->is_array) {
            return minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, extent);
        }
        if (local->element_count == 0U ||
            !minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, &element_size) ||
            element_size > SIZE_MAX / local->element_count) {
            return false;
        }
        *extent = element_size * local->element_count;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object =
            minic_c0_program_global_object(parser->program, expression->value.global_object_id);
        return object != NULL && minic_target_info_sizeof_type(
                                     parser->target_info, parser->program, object->type, extent);
    }
    return false;
}

static bool
object_extent_exact_start(const MinicParser *parser, MinicExpressionId pointer_id, size_t *extent) {
    const MinicExpression *pointer;
    const MinicExpression *operand;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL) {
        return false;
    }
    if (pointer->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(pointer->type)) {
        const MinicExpression *cast_operand;

        cast_operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
        if (cast_operand != NULL && minic_type_is_pointer(cast_operand->type)) {
            return object_extent_exact_start(parser, pointer->value.unary.operand, extent);
        }
    }
    if (pointer->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
    if (operand == NULL) {
        return false;
    }
    if (object_extent_direct_object(parser, pointer->value.unary.operand, extent)) {
        return true;
    }
    if (operand->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *index;

        index = minic_c0_program_expression(parser->program, operand->value.subscript.index);
        if (index == NULL || index->kind != MINIC_EXPRESSION_INTEGER ||
            !minic_type_is_integer(index->type) || index->value.integer_value != 0) {
            return false;
        }
        return object_extent_direct_object(parser, operand->value.subscript.base, extent);
    }
    return false;
}

static bool parse_builtin_object_size(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId pointer_id;
    const MinicExpression *pointer;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t mode;
    size_t extent;
    uint64_t result_bits;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_object_size")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_object_size") ||
        !parse_expression_internal(parser, &pointer_id, 0U, true)) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL || !minic_type_is_pointer(pointer->type)) {
        minic_parser_error(parser, "__builtin_object_size first argument must be a pointer");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_object_size") ||
        !minic_parser_parse_integer_constant_expression(parser, &mode)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "__builtin_object_size mode must be an integer constant");
        }
        return false;
    }
    if (mode < 0 || mode > 3) {
        minic_parser_error(parser, "__builtin_object_size mode must be between 0 and 3");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_object_size arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (object_extent_exact_start(parser, pointer_id, &extent)) {
        result_bits = (uint64_t)extent;
    } else {
        result_bits = mode < 2 ? UINT64_MAX : UINT64_C(0);
    }

    /* This is a compile-time query: keep the parsed pointer expression only as
     * semantic/provenance input and never retain a runtime edge. Unknown objects
     * use GCC's conservative mode-dependent fallback. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_EXPRESSION_INTEGER;
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_unsigned_long();
    result.value_category = MINIC_VALUE_RVALUE;
    (void)memcpy(&result.value.integer_value, &result_bits, sizeof(result_bits));
    return minic_parser_add_expression(parser, &result, expression_id);
}

static bool object_extent_direct_object(const MinicParser *parser,
                                        MinicExpressionId expression_id,
                                        size_t *extent) {
    const MinicExpression *expression;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;
        size_t element_size;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        if (local == NULL) {
            return false;
        }
        if (!local->is_array) {
            return minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, extent);
        }
        if (local->element_count == 0U ||
            !minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, &element_size) ||
            element_size > SIZE_MAX / local->element_count) {
            return false;
        }
        *extent = element_size * local->element_count;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object =
            minic_c0_program_global_object(parser->program, expression->value.global_object_id);
        return object != NULL && minic_target_info_sizeof_type(
                                     parser->target_info, parser->program, object->type, extent);
    }
    return false;
}

static bool
object_extent_exact_start(const MinicParser *parser, MinicExpressionId pointer_id, size_t *extent) {
    const MinicExpression *pointer;
    const MinicExpression *operand;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL) {
        return false;
    }
    if (pointer->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(pointer->type)) {
        const MinicExpression *cast_operand;

        cast_operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
        if (cast_operand != NULL && minic_type_is_pointer(cast_operand->type)) {
            return object_extent_exact_start(parser, pointer->value.unary.operand, extent);
        }
    }
    if (pointer->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
    if (operand == NULL) {
        return false;
    }
    if (object_extent_direct_object(parser, pointer->value.unary.operand, extent)) {
        return true;
    }
    if (operand->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *index;

        index = minic_c0_program_expression(parser->program, operand->value.subscript.index);
        if (index == NULL || index->kind != MINIC_EXPRESSION_INTEGER ||
            !minic_type_is_integer(index->type) || index->value.integer_value != 0) {
            return false;
        }
        return object_extent_direct_object(parser, operand->value.subscript.base, extent);
    }
    return false;
}

static bool parse_builtin_object_size(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId pointer_id;
    const MinicExpression *pointer;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t mode;
    size_t extent;
    uint64_t result_bits;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_object_size")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_object_size") ||
        !parse_expression_internal(parser, &pointer_id, 0U, true)) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL || !minic_type_is_pointer(pointer->type)) {
        minic_parser_error(parser, "__builtin_object_size first argument must be a pointer");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_object_size") ||
        !minic_parser_parse_integer_constant_expression(parser, &mode)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "__builtin_object_size mode must be an integer constant");
        }
        return false;
    }
    if (mode < 0 || mode > 3) {
        minic_parser_error(parser, "__builtin_object_size mode must be between 0 and 3");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_object_size arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (object_extent_exact_start(parser, pointer_id, &extent)) {
        result_bits = (uint64_t)extent;
    } else {
        result_bits = mode < 2 ? UINT64_MAX : UINT64_C(0);
    }

    /* This is a compile-time query: keep the parsed pointer expression only as
     * semantic/provenance input and never retain a runtime edge. Unknown objects
     * use GCC's conservative mode-dependent fallback. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_EXPRESSION_INTEGER;
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_unsigned_long();
    result.value_category = MINIC_VALUE_RVALUE;
    (void)memcpy(&result.value.integer_value, &result_bits, sizeof(result_bits));
    return minic_parser_add_expression(parser, &result, expression_id);
}

static bool parse_builtin_constant_p(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicConstValue constant_value;
    bool is_constant;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_constant_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_constant_p") ||
        !parse_expression_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_constant_p operand");
        return false;
    }
    end = parser->current.span.end;
    is_constant =
        minic_const_eval_integer(parser->program, parser->target_info, operand_id, &constant_value);
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* __builtin_constant_p is a compile-time query. The operand is parsed for C
     * semantics but its AST edge is intentionally not retained, so it is never
     * evaluated at runtime. This first generic implementation recognizes the
     * integer constant-expression subset already shared by choose_expr; unknown
     * expressions conservatively produce 0, as required by GCC's contract. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_EXPRESSION_INTEGER;
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_int();
    result.value_category = MINIC_VALUE_RVALUE;
    result.value.integer_value = is_constant ? 1 : 0;
    return minic_parser_add_expression(parser, &result, expression_id);
}

static bool parse_builtin_unary(MinicParser *parser,
                                MinicBuiltinUnaryOperator operator_kind,
                                const char *spelling,
                                MinicExpressionId *expression_id) {
    MinicExpression conversion;
    MinicExpression expression;
    const MinicExpression *operand;
    MinicExpressionId converted_id;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicType argument_type;

    if (parser == NULL || spelling == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, spelling)) {
        return false;
    }
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        argument_type = minic_type_unsigned_long_long();
        break;
    default:
        return false;
    }

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after unary builtin") ||
        !parse_expression_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL || !minic_type_is_integer(operand->type)) {
        minic_parser_error(parser, "unary builtin requires an integer operand");
        return false;
    }
    if (!minic_type_equal(operand->type, argument_type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = operand->span;
        conversion.type = argument_type;
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = operand_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        operand_id = converted_id;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after unary builtin operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNARY;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.builtin_unary.operator_kind = operator_kind;
    expression.value.builtin_unary.operand = operand_id;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_overflow(MinicParser *parser,
                                   MinicOverflowOperator operator_kind,
                                   MinicExpressionId *expression_id) {
    MinicExpression expression;
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *result_pointer;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    MinicExpressionId result_pointer_id;
    MinicSourcePosition begin;
    MinicType result_type;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after overflow builtin") ||
        !parse_expression_internal(parser, &left_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected first ',' in overflow builtin") ||
        !parse_expression_internal(parser, &right_id, 0U, true) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in overflow builtin") ||
        !parse_expression_internal(parser, &result_pointer_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after overflow builtin")) {
        return false;
    }

    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    result_pointer = minic_c0_program_expression(parser->program, result_pointer_id);
    if (left == NULL || right == NULL || result_pointer == NULL ||
        !minic_type_pointee(result_pointer->type, &result_type) ||
        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type)) {
        minic_parser_error(parser,
                           "overflow builtin currently requires matching non-bool integer operands "
                           "and result pointee");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_OVERFLOW;
    expression.span.begin = begin;
    expression.span.end = result_pointer->span.end;
    expression.type = minic_type_bool();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.overflow.operator_kind = operator_kind;
    expression.value.overflow.left = left_id;
    expression.value.overflow.right = right_id;
    expression.value.overflow.result_pointer = result_pointer_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpression expression;
    MinicExpressionId primary_id;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicFunctionId function_id;
    MinicGlobalObjectId global_object_id;
    MinicFixedRegisterBindingId fixed_register_binding_id;
    int enum_value;
    bool is_enum_constant;

    if (generic_token_text_equals(parser, "__builtin_clzll")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CLZLL, "__builtin_clzll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_constant_p")) {
        if (!parse_builtin_constant_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_object_size")) {
        if (!parse_builtin_object_size(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_object_size")) {
        if (!parse_builtin_object_size(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_add_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_ADD, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_sub_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_SUBTRACT, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_mul_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_MULTIPLY, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
        if (!parse_builtin_types_compatible_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_choose_expr")) {
        if (!parse_builtin_choose_expr(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "_Generic")) {
        if (!parse_generic_selection(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
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
    if (current_identifier_is(parser, "__builtin_expect")) {
        if (!parse_builtin_expect(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (current_is_builtin_offsetof(parser)) {
            return parse_builtin_offsetof(parser, expression_id);
        }
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        function_id = minic_parser_find_function(parser, name_span);
        global_object_id = minic_parser_find_global_object(parser, name_span);
        fixed_register_binding_id = minic_parser_find_fixed_register_binding(parser, name_span);
        is_enum_constant = minic_parser_find_enum_constant(parser, name_span, &enum_value);
        if (!minic_parser_advance(parser)) {
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_LPAREN && function_id != MINIC_FUNCTION_INVALID) {
            MinicSourcePosition call_end;
            const MinicFunction *callee;

            callee = minic_c0_program_function(parser->program, function_id);
            if (callee == NULL || callee->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "unsupported function call signature");
                return false;
            }

            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_CALL;
            expression.span.begin = name_span.begin;
            expression.type = callee->return_type;
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.call.function_id = function_id;
            if (!parse_call_arguments(parser, &expression, callee)) {
                return false;
            }
            call_end = parser->current.span.end;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            expression.span.end = call_end;
            if (!minic_parser_add_expression(parser, &expression, &primary_id) ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }

        if (local_id != MINIC_LOCAL_INVALID) {
            if (!parse_local_reference(parser, name_span, local_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            if (!parse_global_reference(parser, name_span, global_object_id, true, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (fixed_register_binding_id != MINIC_FIXED_REGISTER_BINDING_INVALID) {
            if (!parse_fixed_register_reference(
                    parser, name_span, fixed_register_binding_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (function_id != MINIC_FUNCTION_INVALID) {
            if (!parse_function_reference(parser, name_span, function_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (is_enum_constant) {
            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_INTEGER;
            expression.span = name_span;
            expression.type = minic_type_int();
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.integer_value = enum_value;
            if (!minic_parser_add_expression(parser, &expression, &primary_id) ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        minic_parser_error(parser, "use of undeclared local");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicSourcePosition begin;

        begin = parser->current.span.begin;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            if (!minic_parser_parse_statement_expression(parser, begin, &primary_id) ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU statement expression") ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (!parse_expression_internal(parser, &primary_id, 0U, decay_array)) {
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_COMMA) {
            const MinicExpression *left_expression;
            const MinicExpression *right_expression;
            MinicExpression comma_expression;
            MinicExpressionId right_id;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL || !minic_parser_advance(parser) ||
                !parse_expression_internal(parser, &right_id, 0U, true)) {
                return false;
            }
            right_expression = minic_c0_program_expression(parser->program, right_id);
            if (right_expression == NULL) {
                minic_parser_error(parser, "invalid comma expression operand");
                return false;
            }
            (void)memset(&comma_expression, 0, sizeof(comma_expression));
            comma_expression.kind = MINIC_EXPRESSION_BINARY;
            comma_expression.span.begin = left_expression->span.begin;
            comma_expression.span.end = right_expression->span.end;
            comma_expression.type = right_expression->type;
            comma_expression.value_category = MINIC_VALUE_RVALUE;
            comma_expression.value.binary.operator_kind = MINIC_BINARY_COMMA;
            comma_expression.value.binary.left = primary_id;
            comma_expression.value.binary.right = right_id;
            if (!minic_parser_add_expression(parser, &comma_expression, &primary_id)) {
                return false;
            }
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    minic_parser_error(parser, "expected expression");
    return false;
}

static bool local_array_without_array_type(const MinicParser *parser,
                                           const MinicExpression *expression) {
    const MinicLocal *local;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
        minic_type_is_array(expression->type)) {
        return false;
    }
    local = minic_c0_program_local(parser->program, expression->value.local_id);
    return local != NULL && local->is_array;
}

static bool current_is_sizeof(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_KW_SIZEOF;
}

static bool current_is_alignof(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_KW_ALIGNOF;
}

static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    int64_t alignment;

    if (!minic_parser_parse_alignof_type_value(parser, &alignment, &span)) {
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = alignment;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_sizeof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicType measured_type;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_LPAREN && parenthesis_starts_cast(parser)) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_type_name(parser, &measured_type)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_RPAREN) {
            minic_parser_error(parser, "expected ')' after sizeof type");
            return false;
        }
        end = parser->current.span.end;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else {
        MinicExpressionId operand_id;
        const MinicExpression *operand;

        if (!parse_unary(parser, &operand_id, false)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL) {
            minic_parser_error(parser, "invalid sizeof operand");
            return false;
        }
        measured_type = operand->type;
        if (local_array_without_array_type(parser, operand)) {
            const MinicLocal *local;

            local = minic_c0_program_local(parser->program, operand->value.local_id);
            if (local == NULL ||
                !minic_c0_program_add_array_type(
                    parser->program, local->type, local->element_count, &measured_type)) {
                minic_parser_error(parser, "cannot preserve local array type for sizeof");
                return false;
            }
        }
        end = operand->span.end;
    }

    {
        size_t measured_size;

        if (!minic_target_info_sizeof_type(
                parser->target_info, parser->program, measured_type, &measured_size)) {
            minic_parser_error(parser, "sizeof requires a supported complete type");
            return false;
        }
        (void)measured_size;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_SIZEOF;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.sizeof_type = measured_type;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    MinicExpressionId result_id;
    const MinicExpression *operand_expression;

    if (current_is_sizeof(parser)) {
        return parse_sizeof(parser, expression_id);
    }
    if (current_is_alignof(parser)) {
        return parse_alignof(parser, expression_id);
    }
    if (parenthesis_starts_cast(parser)) {
        return parse_cast(parser, expression_id);
    }
    if (parser->current.kind != MINIC_TOKEN_PLUS && parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG && parser->current.kind != MINIC_TOKEN_TILDE &&
        parser->current.kind != MINIC_TOKEN_AMPERSAND && parser->current.kind != MINIC_TOKEN_STAR &&
        parser->current.kind != MINIC_TOKEN_PLUS_PLUS &&
        parser->current.kind != MINIC_TOKEN_MINUS_MINUS) {
        return parse_primary(parser, expression_id, decay_array);
    }

    operator_token = parser->current;
    if (!minic_parser_advance(parser) ||
        !parse_unary(parser, &operand, operator_token.kind != MINIC_TOKEN_AMPERSAND)) {
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand);
    if (operand_expression == NULL) {
        minic_parser_error(parser, "invalid unary operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.span.begin = operator_token.span.begin;
    expression.span.end = operand_expression->span.end;
    expression.value.unary.operand = operand;

    if (operator_token.kind == MINIC_TOKEN_PLUS_PLUS ||
        operator_token.kind == MINIC_TOKEN_MINUS_MINUS) {
        MinicType pointee_type;

        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(operand_expression->type)) {
            minic_parser_error(parser, "prefix update requires a modifiable scalar lvalue");
            return false;
        }
        if (minic_type_is_pointer(operand_expression->type)) {
            if (!minic_type_pointee(operand_expression->type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
        } else if (!minic_type_is_integer(operand_expression->type)) {
            minic_parser_error(parser, "prefix update requires integer or pointer lvalue");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_UNARY;
        expression.type = operand_expression->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.unary.operator_kind = operator_token.kind == MINIC_TOKEN_PLUS_PLUS
                                                   ? MINIC_UNARY_PRE_INCREMENT
                                                   : MINIC_UNARY_PRE_DECREMENT;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }

    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {
        MinicType function_type;

        if (operand_expression->kind == MINIC_EXPRESSION_FUNCTION &&
            minic_type_pointee(operand_expression->type, &function_type) &&
            minic_type_is_function(function_type)) {
            expression.type = operand_expression->type;
        } else if (minic_type_is_function(operand_expression->type)) {
            if (!minic_type_pointer_to(operand_expression->type, &expression.type)) {
                minic_parser_error(parser, "cannot form pointer to function designator");
                return false;
            }
        } else {
            if (local_array_without_array_type(parser, operand_expression)) {
                minic_parser_error(parser, "address-of local array object is not supported yet");
                return false;
            }
            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_pointer_to(operand_expression->type, &expression.type)) {
                minic_parser_error(parser,
                                   "address-of requires an lvalue object or function designator");
                return false;
            }
        }
        expression.kind = MINIC_EXPRESSION_ADDRESS_OF;
        expression.value_category = MINIC_VALUE_RVALUE;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }

    if (operator_token.kind == MINIC_TOKEN_STAR) {
        if (!minic_type_pointee(operand_expression->type, &expression.type)) {
            minic_parser_error(parser, "dereference requires a pointer operand");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_DEREFERENCE;
        expression.value_category = MINIC_VALUE_LVALUE;
        if (!minic_parser_add_expression(parser, &expression, &result_id)) {
            return false;
        }
        return finish_value_expression(parser, result_id, decay_array, expression_id);
    }

    expression.kind = MINIC_EXPRESSION_UNARY;
    expression.value_category = MINIC_VALUE_RVALUE;
    if (operator_token.kind == MINIC_TOKEN_BANG) {
        if (!type_is_condition_scalar(operand_expression->type)) {
            minic_parser_error(parser, "logical not requires an integer or pointer operand");
            return false;
        }
        expression.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
        expression.type = minic_type_int();
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (minic_type_is_double(operand_expression->type)) {
        if (operator_token.kind != MINIC_TOKEN_PLUS && operator_token.kind != MINIC_TOKEN_MINUS) {
            minic_parser_error(parser, "floating unary arithmetic requires '+' or '-'");
            return false;
        }
        expression.value.unary.operator_kind =
            operator_token.kind == MINIC_TOKEN_PLUS ? MINIC_UNARY_PLUS : MINIC_UNARY_NEGATE;
        expression.type = operand_expression->type;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary arithmetic requires an integer or double operand");
        return false;
    }
    if (operator_token.kind == MINIC_TOKEN_TILDE) {
        expression.value.unary.operator_kind = MINIC_UNARY_BITWISE_NOT;
        if (!minic_type_integer_common(
                operand_expression->type, operand_expression->type, &expression.type)) {
            return false;
        }
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (operator_token.kind == MINIC_TOKEN_PLUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_PLUS;
        if (!minic_type_integer_common(
                operand_expression->type, operand_expression->type, &expression.type)) {
            return false;
        }
    } else {
        expression.value.unary.operator_kind = MINIC_UNARY_NEGATE;
        if (!minic_type_integer_common(
                operand_expression->type, operand_expression->type, &expression.type)) {
            return false;
        }
    }
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static unsigned int binary_precedence(MinicTokenKind kind) {
    switch (kind) {
    case MINIC_TOKEN_STAR:
    case MINIC_TOKEN_SLASH:
    case MINIC_TOKEN_PERCENT:
        return 70U;
    case MINIC_TOKEN_PLUS:
    case MINIC_TOKEN_MINUS:
        return 60U;
    case MINIC_TOKEN_LESS_LESS:
    case MINIC_TOKEN_GREATER_GREATER:
        return 50U;
    case MINIC_TOKEN_LESS:
    case MINIC_TOKEN_LESS_EQUAL:
    case MINIC_TOKEN_GREATER:
    case MINIC_TOKEN_GREATER_EQUAL:
        return 40U;
    case MINIC_TOKEN_EQUAL_EQUAL:
    case MINIC_TOKEN_BANG_EQUAL:
        return 30U;
    case MINIC_TOKEN_AMPERSAND:
        return 20U;
    case MINIC_TOKEN_CARET:
        return 10U;
    case MINIC_TOKEN_PIPE:
        return 5U;
    case MINIC_TOKEN_AMPERSAND_AMPERSAND:
        return 2U;
    case MINIC_TOKEN_PIPE_PIPE:
        return 1U;
    default:
        return 0U;
    }
}

static MinicBinaryOperator binary_operator(MinicTokenKind kind) {
    switch (kind) {
    case MINIC_TOKEN_PLUS:
        return MINIC_BINARY_ADD;
    case MINIC_TOKEN_MINUS:
        return MINIC_BINARY_SUBTRACT;
    case MINIC_TOKEN_STAR:
        return MINIC_BINARY_MULTIPLY;
    case MINIC_TOKEN_SLASH:
        return MINIC_BINARY_DIVIDE;
    case MINIC_TOKEN_PERCENT:
        return MINIC_BINARY_REMAINDER;
    case MINIC_TOKEN_LESS_LESS:
        return MINIC_BINARY_SHIFT_LEFT;
    case MINIC_TOKEN_GREATER_GREATER:
        return MINIC_BINARY_SHIFT_RIGHT;
    case MINIC_TOKEN_AMPERSAND:
        return MINIC_BINARY_BITWISE_AND;
    case MINIC_TOKEN_CARET:
        return MINIC_BINARY_BITWISE_XOR;
    case MINIC_TOKEN_PIPE:
        return MINIC_BINARY_BITWISE_OR;
    case MINIC_TOKEN_EQUAL_EQUAL:
        return MINIC_BINARY_EQUAL;
    case MINIC_TOKEN_BANG_EQUAL:
        return MINIC_BINARY_NOT_EQUAL;
    case MINIC_TOKEN_LESS:
        return MINIC_BINARY_LESS;
    case MINIC_TOKEN_LESS_EQUAL:
        return MINIC_BINARY_LESS_EQUAL;
    case MINIC_TOKEN_GREATER:
        return MINIC_BINARY_GREATER;
    case MINIC_TOKEN_GREATER_EQUAL:
        return MINIC_BINARY_GREATER_EQUAL;
    case MINIC_TOKEN_AMPERSAND_AMPERSAND:
        return MINIC_BINARY_LOGICAL_AND;
    case MINIC_TOKEN_PIPE_PIPE:
        return MINIC_BINARY_LOGICAL_OR;
    default:
        return MINIC_BINARY_ADD;
    }
}

static bool binary_is_comparison(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_EQUAL_EQUAL || kind == MINIC_TOKEN_BANG_EQUAL ||
           kind == MINIC_TOKEN_LESS || kind == MINIC_TOKEN_LESS_EQUAL ||
           kind == MINIC_TOKEN_GREATER || kind == MINIC_TOKEN_GREATER_EQUAL;
}

static bool binary_is_equality(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_EQUAL_EQUAL || kind == MINIC_TOKEN_BANG_EQUAL;
}

static bool binary_is_logical(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_AMPERSAND_AMPERSAND || kind == MINIC_TOKEN_PIPE_PIPE;
}

static bool binary_is_shift(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_LESS_LESS || kind == MINIC_TOKEN_GREATER_GREATER;
}

static bool binary_is_double_arithmetic(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_PLUS || kind == MINIC_TOKEN_MINUS || kind == MINIC_TOKEN_STAR ||
           kind == MINIC_TOKEN_SLASH;
}

static bool type_is_complete_object(const MinicC0Program *program, MinicType type) {
    if (program == NULL || minic_type_is_void(type) || minic_type_is_function(type)) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) || minic_type_is_float(type) ||
        minic_type_is_double(type)) {
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               type_is_complete_object(program, array_type->element_type);
    }
    return false;
}

static bool pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                               MinicType pointee_type) {
    return minic_type_is_void(pointee_type) || minic_type_is_function(pointee_type) ||
           type_is_complete_object(program, pointee_type);
}

static bool pointer_arithmetic_shape(MinicTokenKind kind,
                                     MinicType left,
                                     MinicType right,
                                     MinicType *pointer_type) {
    if (pointer_type == NULL) {
        return false;
    }
    if (kind == MINIC_TOKEN_PLUS) {
        if (minic_type_is_pointer(left) && minic_type_is_integer(right)) {
            *pointer_type = left;
            return true;
        }
        if (minic_type_is_integer(left) && minic_type_is_pointer(right)) {
            *pointer_type = right;
            return true;
        }
        return false;
    }
    if (kind == MINIC_TOKEN_MINUS && minic_type_is_pointer(left) && minic_type_is_integer(right)) {
        *pointer_type = left;
        return true;
    }
    return false;
}

static bool
pointer_relational_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           type_is_complete_object(program, left_pointee) &&
           type_is_complete_object(program, right_pointee);
}

static bool
pointer_difference_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           type_is_complete_object(program, left_unqualified) &&
           type_is_complete_object(program, right_unqualified);
}

static bool conditional_result_type(MinicType when_true, MinicType when_false, MinicType *result) {
    bool has_double_operand;
    bool has_numeric_operands;

    if (result == NULL) {
        return false;
    }
    if (minic_type_equal(when_true, when_false)) {
        *result = when_true;
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands = (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
                           (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

static bool binary_result_type(const MinicC0Program *program,
                               MinicTokenKind kind,
                               MinicType left,
                               MinicType right,
                               MinicType *result) {
    MinicType pointer_type;
    MinicType pointee_type;
    bool has_double_operand;
    bool has_numeric_operands;

    if (result == NULL) {
        return false;
    }
    if (binary_is_logical(kind)) {
        if (!type_is_condition_scalar(left) || !type_is_condition_scalar(right)) {
            return false;
        }
        *result = minic_type_int();
        return true;
    }
    if (minic_type_is_integer(left) && minic_type_is_integer(right)) {
        if (binary_is_comparison(kind)) {
            *result = minic_type_int();
            return true;
        }
        if (binary_is_shift(kind)) {
            return minic_type_integer_common(left, left, result);
        }
        return minic_type_integer_common(left, right, result);
    }
    has_double_operand = minic_type_is_double(left) || minic_type_is_double(right);
    has_numeric_operands = (minic_type_is_double(left) || minic_type_is_integer(left)) &&
                           (minic_type_is_double(right) || minic_type_is_integer(right));
    if (binary_is_comparison(kind) && has_double_operand && has_numeric_operands) {
        *result = minic_type_int();
        return true;
    }
    if (binary_is_comparison(kind) && !binary_is_equality(kind) && minic_type_is_pointer(left) &&
        minic_type_is_pointer(right) && pointer_relational_compatible(program, left, right)) {
        *result = minic_type_int();
        return true;
    }
    if (has_double_operand && has_numeric_operands && binary_is_double_arithmetic(kind)) {
        *result = minic_type_double();
        return true;
    }
    if (kind == MINIC_TOKEN_MINUS && minic_type_is_pointer(left) && minic_type_is_pointer(right) &&
        pointer_difference_compatible(program, left, right)) {
        *result = minic_type_long();
        return true;
    }
    if (!pointer_arithmetic_shape(kind, left, right, &pointer_type) ||
        !minic_type_pointee(pointer_type, &pointee_type) ||
        !pointer_arithmetic_pointee_allowed(program, pointee_type)) {
        return false;
    }
    *result = pointer_type;
    return true;
}

static bool parse_expression_internal(MinicParser *parser,
                                      MinicExpressionId *expression_id,
                                      unsigned int minimum_precedence,
                                      bool decay_array) {
    MinicExpressionId left;

    if (!parse_unary(parser, &left, decay_array)) {
        return false;
    }
    for (;;) {
        MinicTokenKind token_kind;
        unsigned int precedence;
        MinicExpressionId right;
        MinicExpression expression;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        token_kind = parser->current.kind;
        precedence = binary_precedence(token_kind);
        if (precedence == 0U || precedence < minimum_precedence) {
            break;
        }
        if (!minic_parser_apply_array_decay(parser, left, &left) || !minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &right, precedence + 1U, true)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_error(parser, "invalid binary operands");
            return false;
        }

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.binary.operator_kind = binary_operator(token_kind);
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (binary_is_equality(token_kind) &&
            minic_c0_pointer_equality_compatible(parser->program, left, right)) {
            expression.type = minic_type_int();
        } else if (!binary_result_type(parser->program,
                                       token_kind,
                                       left_expression->type,
                                       right_expression->type,
                                       &expression.type)) {
            MinicType pointer_type;
            MinicType pointee_type;
            MinicType left_type;
            MinicType right_type;
            bool has_pointer_arithmetic_shape;

            left_type = left_expression->type;
            right_type = right_expression->type;
            has_pointer_arithmetic_shape =
                pointer_arithmetic_shape(token_kind, left_type, right_type, &pointer_type);
            if (binary_is_logical(token_kind)) {
                minic_parser_error(parser, "logical operator requires integer or pointer operands");
            } else if (has_pointer_arithmetic_shape &&
                       minic_type_pointee(pointer_type, &pointee_type) &&
                       !pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
                minic_parser_error(parser,
                                   "pointer arithmetic requires a complete object type or GNU "
                                   "byte-sized void/function pointee");
            } else if (token_kind == MINIC_TOKEN_PLUS || token_kind == MINIC_TOKEN_MINUS) {
                minic_parser_error(parser, "unsupported pointer arithmetic operands");
            } else {
                minic_parser_error(parser, "binary operator requires int operands");
            }
            return false;
        }
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_QUESTION) {
        const MinicExpression *condition_expression;
        const MinicExpression *true_expression;
        const MinicExpression *false_expression;
        MinicExpression conditional;
        MinicSourceSpan condition_span;
        MinicExpressionId when_true;
        MinicExpressionId when_false;

        if (!minic_parser_apply_array_decay(parser, left, &left)) {
            return false;
        }
        condition_expression = minic_c0_program_expression(parser->program, left);
        if (condition_expression == NULL || !type_is_condition_scalar(condition_expression->type)) {
            minic_parser_error(parser,
                               "conditional expression requires an integer or pointer condition");
            return false;
        }
        condition_span = condition_expression->span;
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &when_true, 0U, true) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
            !parse_expression_internal(parser, &when_false, 0U, true)) {
            return false;
        }
        true_expression = minic_c0_program_expression(parser->program, when_true);
        false_expression = minic_c0_program_expression(parser->program, when_false);
        if (true_expression == NULL || false_expression == NULL) {
            minic_parser_error(parser, "invalid conditional expression operand");
            return false;
        }

        (void)memset(&conditional, 0, sizeof(conditional));
        conditional.kind = MINIC_EXPRESSION_CONDITIONAL;
        conditional.span.begin = condition_span.begin;
        conditional.span.end = false_expression->span.end;
        conditional.value_category = MINIC_VALUE_RVALUE;
        conditional.value.conditional.condition = left;
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
        if (!conditional_result_type(
                true_expression->type, false_expression->type, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
        if (!minic_parser_add_expression(parser, &conditional, &left)) {
            return false;
        }
    }
    if (minimum_precedence == 0U && (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_MINUS_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_STAR_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_SLASH_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_PERCENT_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_PIPE_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_CARET_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_LESS_LESS_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_GREATER_GREATER_EQUAL)) {
        const MinicExpression *target_expression;
        const MinicExpression *value_expression;
        MinicExpression assignment;
        MinicExpressionId value_id;
        MinicSourceSpan target_span;
        MinicTokenKind assignment_token;
        MinicType target_type;
        MinicBinaryOperator compound_operator;

        assignment_token = parser->current.kind;
        switch (assignment_token) {
        case MINIC_TOKEN_PLUS_EQUAL:
            compound_operator = MINIC_BINARY_ADD;
            break;
        case MINIC_TOKEN_MINUS_EQUAL:
            compound_operator = MINIC_BINARY_SUBTRACT;
            break;
        case MINIC_TOKEN_STAR_EQUAL:
            compound_operator = MINIC_BINARY_MULTIPLY;
            break;
        case MINIC_TOKEN_SLASH_EQUAL:
            compound_operator = MINIC_BINARY_DIVIDE;
            break;
        case MINIC_TOKEN_PERCENT_EQUAL:
            compound_operator = MINIC_BINARY_REMAINDER;
            break;
        case MINIC_TOKEN_AMPERSAND_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_AND;
            break;
        case MINIC_TOKEN_PIPE_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_OR;
            break;
        case MINIC_TOKEN_CARET_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_XOR;
            break;
        case MINIC_TOKEN_LESS_LESS_EQUAL:
            compound_operator = MINIC_BINARY_SHIFT_LEFT;
            break;
        case MINIC_TOKEN_GREATER_GREATER_EQUAL:
            compound_operator = MINIC_BINARY_SHIFT_RIGHT;
            break;
        default:
            minic_parser_error(parser, "unsupported compound assignment expression");
            return false;
        }

        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type) ||
            minic_type_is_record(target_expression->type)) {
            minic_parser_error(
                parser, "compound assignment expression requires a modifiable scalar lvalue");
            return false;
        }
        target_span = target_expression->span;
        target_type = target_expression->type;
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &value_id, 0U, true)) {
            return false;
        }
        value_expression = minic_c0_program_expression(parser->program, value_id);
        if (value_expression == NULL) {
            minic_parser_error(parser, "invalid compound assignment expression value");
            return false;
        }

        if (minic_type_is_pointer(target_type)) {
            MinicType pointee_type;

            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_type_pointee(target_type, &pointee_type) ||
                !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(
                    parser,
                    "pointer compound assignment expression requires += or -= with an integer");
                return false;
            }
        } else if (minic_type_is_double(target_type)) {
            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT &&
                 compound_operator != MINIC_BINARY_MULTIPLY &&
                 compound_operator != MINIC_BINARY_DIVIDE) ||
                (!minic_type_is_double(value_expression->type) &&
                 !minic_type_is_integer(value_expression->type))) {
                minic_parser_error(parser,
                                   "double compound assignment requires arithmetic operands");
                return false;
            }
        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target_type) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_type_integer_common(target_type, value_expression->type, &common_type)) {
                minic_parser_error(parser,
                                   "compound assignment expression requires integer operands");
                return false;
            }
        }

        (void)memset(&assignment, 0, sizeof(assignment));
        assignment.kind = MINIC_EXPRESSION_COMPOUND_ASSIGNMENT;
        assignment.span.begin = target_span.begin;
        assignment.span.end = value_expression->span.end;
        assignment.type = target_type;
        assignment.value_category = MINIC_VALUE_RVALUE;
        assignment.value.binary.operator_kind = compound_operator;
        assignment.value.binary.left = left;
        assignment.value.binary.right = value_id;
        if (!minic_parser_add_expression(parser, &assignment, &left)) {
            return false;
        }
    }
    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {
        const MinicExpression *target_expression;
        const MinicExpression *value_expression;
        MinicExpression assignment;
        MinicExpressionId value_id;
        MinicSourceSpan target_span;
        MinicType target_type;

        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type)) {
            minic_parser_error(parser, "assignment expression requires a modifiable object lvalue");
            return false;
        }
        target_span = target_expression->span;
        target_type = target_expression->type;

        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &value_id, 0U, true)) {
            return false;
        }
        value_expression = minic_c0_program_expression(parser->program, value_id);
        if (value_expression == NULL) {
            minic_parser_error(parser, "invalid assignment expression value");
            return false;
        }
        if (minic_type_is_record(target_type)) {
            if (value_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_is_record(value_expression->type) ||
                target_type.record_id != value_expression->type.record_id) {
                minic_parser_error(parser,
                                   "record assignment expression requires matching record lvalues");
                return false;
            }
        } else {
            if (!minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
                MinicExpression cast_expression;

                if (minic_type_is_pointer(target_type) ||
                    minic_type_is_pointer(value_expression->type) ||
                    !minic_type_cast_compatible(target_type, value_expression->type)) {
                    minic_parser_error(parser,
                                       "assignment expression type does not match target type");
                    return false;
                }
                (void)memset(&cast_expression, 0, sizeof(cast_expression));
                cast_expression.kind = MINIC_EXPRESSION_CAST;
                cast_expression.span = value_expression->span;
                cast_expression.type = target_type;
                cast_expression.value_category = MINIC_VALUE_RVALUE;
                cast_expression.value.unary.operand = value_id;
                if (!minic_parser_add_expression(parser, &cast_expression, &value_id)) {
                    return false;
                }
            }
            value_expression = minic_c0_program_expression(parser->program, value_id);
            if (value_expression == NULL ||
                !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
                minic_parser_error(parser, "assignment expression conversion failed");
                return false;
            }
        }

        (void)memset(&assignment, 0, sizeof(assignment));
        assignment.kind = MINIC_EXPRESSION_ASSIGNMENT;
        assignment.span.begin = target_span.begin;
        assignment.span.end = value_expression->span.end;
        assignment.type = target_type;
        assignment.value_category = MINIC_VALUE_RVALUE;
        assignment.value.binary.left = left;
        assignment.value.binary.right = value_id;
        if (!minic_parser_add_expression(parser, &assignment, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool
parse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId left;

    if (!parse_expression_internal(parser, &left, 0U, decay_array)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicExpression sequence;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &right, 0U, decay_array)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_error(parser, "invalid comma expression operands");
            return false;
        }

        (void)memset(&sequence, 0, sizeof(sequence));
        sequence.kind = MINIC_EXPRESSION_BINARY;
        sequence.span.begin = left_expression->span.begin;
        sequence.span.end = right_expression->span.end;
        sequence.type = right_expression->type;
        sequence.value_category = MINIC_VALUE_RVALUE;
        sequence.value.binary.operator_kind = MINIC_BINARY_COMMA;
        sequence.value.binary.left = left;
        sequence.value.binary.right = right;
        if (!minic_parser_add_expression(parser, &sequence, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

bool minic_parser_parse_expression_no_decay(MinicParser *parser, MinicExpressionId *expression_id) {
    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    return parse_expression_internal(parser, expression_id, 0U, false);
}

bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return parse_expression_internal(parser, expression_id, minimum_precedence, true);
}

bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id) {
    return parse_comma_expression(parser, expression_id, true);
}
