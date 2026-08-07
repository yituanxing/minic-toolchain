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

static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    int value;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.type = minic_type_int();
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
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            minic_parser_error(parser, "global record object requires member access");
            return false;
        }
    } else if (!array_object) {
        minic_parser_error(parser, "invalid global object reference");
        return false;
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

static bool token_starts_cast_type(const MinicParser *parser, MinicToken token) {
    switch (token.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        return minic_parser_find_local(parser, token.span) == MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
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
        (!minic_type_cast_compatible(target_type, operand->type) &&
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
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool parse_call_argument(MinicParser *parser,
                                MinicExpression *call_expression,
                                const MinicFunction *callee,
                                size_t argument_index) {
    const MinicExpression *argument;
    MinicExpressionId argument_id;

    if (argument_index >= 8U ||
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
        if (!minic_type_assignment_compatible(callee->parameter_types[argument_index],
                                              argument->type)) {
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
            if (!parse_global_reference(parser, name_span, global_object_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        minic_parser_error(parser, "use of undeclared local");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &primary_id, 0U, decay_array) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
            return false;
        }
        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
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
    return local != NULL && local->element_count > 1U;
}

static bool current_is_sizeof(const MinicParser *parser) {
    size_t length;

    if (parser->current.kind == MINIC_TOKEN_KW_SIZEOF) {
        return true;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == 6U &&
           memcmp(parser->source + parser->current.span.begin.offset, "sizeof", 6U) == 0;
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
        if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &measured_type)) {
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

    if (!type_is_complete_object(parser->program, measured_type)) {
        minic_parser_error(parser, "sizeof requires a complete object type");
        return false;
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
    if (parenthesis_starts_cast(parser)) {
        return parse_cast(parser, expression_id);
    }
    if (parser->current.kind != MINIC_TOKEN_PLUS && parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG && parser->current.kind != MINIC_TOKEN_AMPERSAND &&
        parser->current.kind != MINIC_TOKEN_STAR) {
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

    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {
        if (local_array_without_array_type(parser, operand_expression)) {
            minic_parser_error(parser, "address-of local array object is not supported yet");
            return false;
        }
        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_pointer_to(operand_expression->type, &expression.type)) {
            minic_parser_error(parser, "address-of requires an lvalue operand");
            return false;
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
    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary arithmetic requires an integer operand");
        return false;
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

static bool binary_pointer_equality_compatible(MinicTokenKind kind,
                                               const MinicExpression *left,
                                               const MinicExpression *right) {
    if (!binary_is_equality(kind) || left == NULL || right == NULL) {
        return false;
    }
    if (minic_type_pointer_equality_compatible(left->type, right->type)) {
        return true;
    }
    return (minic_type_is_pointer(left->type) && expression_is_integer_zero(right)) ||
           (expression_is_integer_zero(left) && minic_type_is_pointer(right->type));
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

static bool binary_result_type(const MinicC0Program *program,
                               MinicTokenKind kind,
                               MinicType left,
                               MinicType right,
                               MinicType *result) {
    MinicType pointer_type;
    MinicType pointee_type;

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
    if (minic_type_is_double(left) && minic_type_is_double(right) &&
        binary_is_double_arithmetic(kind)) {
        *result = minic_type_double();
        return true;
    }
    if (!pointer_arithmetic_shape(kind, left, right, &pointer_type) ||
        !minic_type_pointee(pointer_type, &pointee_type) ||
        !type_is_complete_object(program, pointee_type)) {
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
        if (binary_pointer_equality_compatible(token_kind, left_expression, right_expression)) {
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
                       !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(parser, "pointer arithmetic requires a complete object type");
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
    *expression_id = left;
    return true;
}

bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return parse_expression_internal(parser, expression_id, minimum_precedence, true);
}
