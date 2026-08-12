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

bool minic_parser_token_starts_expression(MinicTokenKind kind) {
    switch (kind) {
    case MINIC_TOKEN_IDENTIFIER:
    case MINIC_TOKEN_INTEGER_CONSTANT:
    case MINIC_TOKEN_CHARACTER_CONSTANT:
    case MINIC_TOKEN_FLOATING_CONSTANT:
    case MINIC_TOKEN_STRING_LITERAL:
    case MINIC_TOKEN_WIDE_STRING_LITERAL:
    case MINIC_TOKEN_LPAREN:
    case MINIC_TOKEN_KW_SIZEOF:
    case MINIC_TOKEN_KW_ALIGNOF:
    case MINIC_TOKEN_PLUS:
    case MINIC_TOKEN_MINUS:
    case MINIC_TOKEN_BANG:
    case MINIC_TOKEN_TILDE:
    case MINIC_TOKEN_AMPERSAND:
    case MINIC_TOKEN_AMPERSAND_AMPERSAND:
    case MINIC_TOKEN_STAR:
    case MINIC_TOKEN_PLUS_PLUS:
    case MINIC_TOKEN_MINUS_MINUS:
        return true;
    default:
        return false;
    }
}

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

static bool parse_record_compound_literal(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType type,
                                          MinicExpressionId *expression_id) {
    const MinicRecord *record;
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression hidden_lvalue;
    MinicExpression compound_literal;
    MinicExpressionId hidden_lvalue_id;
    MinicExpressionId compound_literal_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function == MINIC_FUNCTION_INVALID || !minic_type_is_record(type)) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "compound literals currently require a block-scope record type");
        }
        return false;
    }
    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "record compound literal requires a complete record type");
        return false;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "cannot allocate compound literal backing object");
        return false;
    }

    (void)memset(&hidden_lvalue, 0, sizeof(hidden_lvalue));
    hidden_lvalue.kind = MINIC_EXPRESSION_LOCAL;
    hidden_lvalue.span.begin = begin;
    hidden_lvalue.span.end = parser->current.span.begin;
    hidden_lvalue.type = type;
    hidden_lvalue.value_category = MINIC_VALUE_LVALUE;
    hidden_lvalue.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &hidden_lvalue, &hidden_lvalue_id) ||
        !minic_c0_program_add_block(parser->program, &initializer_block)) {
        minic_parser_error(parser, "cannot create compound literal initializer block");
        return false;
    }

    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    success = minic_parser_parse_runtime_record_initializer(parser, hidden_lvalue_id);
    parser->current_block = parent_block;
    if (!success) {
        return false;
    }

    (void)memset(&compound_literal, 0, sizeof(compound_literal));
    compound_literal.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound_literal.span.begin = begin;
    compound_literal.span.end = parser->current.span.begin;
    compound_literal.type = type;
    compound_literal.value_category = MINIC_VALUE_LVALUE;
    compound_literal.value.compound_literal.local_id = local_id;
    compound_literal.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound_literal, &compound_literal_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, compound_literal_id, expression_id);
}

static bool parse_cast(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicExpression expression;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicType target_type;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return parse_record_compound_literal(parser, begin, target_type, expression_id);
    }
    if (!parse_unary(parser, &operand_id, true)) {
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

static bool pointer_sign_call_conversion_compatible(MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;

    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) || !minic_type_is_integer(target_pointee) ||
        !minic_type_is_integer(source_pointee) ||
        target_pointee.integer_rank != source_pointee.integer_rank ||
        target_pointee.integer_sign == source_pointee.integer_sign ||
        target_pointee.is_plain_char != source_pointee.is_plain_char) {
        return false;
    }
    if (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) {
        return false;
    }
    if (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee)) {
        return false;
    }
    return true;
}

static bool gnu_function_pointer_to_void_call_conversion_compatible(MinicType target,
                                                                    MinicType source) {
    MinicType source_pointee;
    MinicType void_pointer;

    return minic_type_pointer_to(minic_type_void(), &void_pointer) &&
           minic_type_equal(target, void_pointer) && minic_type_pointee(source, &source_pointee) &&
           minic_type_is_function(source_pointee);
}

static bool gnu_function_pointer_bridge_call_conversion_compatible(const MinicC0Program *program,
                                                                   MinicType target,
                                                                   const MinicExpression *source) {
    const MinicExpression *bridge_operand;
    MinicType bridge_pointee;
    MinicType target_pointee;
    MinicType void_pointer;

    if (program == NULL || source == NULL || source->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer) ||
        !minic_type_equal(source->type, void_pointer) ||
        !minic_type_pointee(target, &target_pointee) || !minic_type_is_function(target_pointee)) {
        return false;
    }
    bridge_operand = minic_c0_program_expression(program, source->value.unary.operand);
    return bridge_operand != NULL && minic_type_pointee(bridge_operand->type, &bridge_pointee) &&
           minic_type_is_function(bridge_pointee);
}

bool minic_parser_apply_fixed_call_argument_conversion(MinicParser *parser,
                                                       MinicType target_type,
                                                       MinicExpressionId *argument_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicSourceSpan source_span;
    bool needs_explicit_conversion;

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
    needs_explicit_conversion =
        (minic_type_is_double(target_type) && minic_type_is_integer(source->type)) ||
        pointer_sign_call_conversion_compatible(target_type, source->type) ||
        gnu_function_pointer_to_void_call_conversion_compatible(target_type, source->type) ||
        gnu_function_pointer_bridge_call_conversion_compatible(
            parser->program, target_type, source);
    if (!needs_explicit_conversion) {
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
        if (!minic_parser_apply_fixed_call_argument_conversion(
                parser, callee->parameter_types[argument_index], &argument_id)) {
            return false;
        }
        call_expression->value.call.arguments[argument_index] = argument_id;
        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL) {
            minic_parser_error(parser, "invalid converted call argument");
            return false;
        }
        if (!minic_c0_fixed_call_argument_compatible(
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
    MinicFunction callee_snapshot;
    size_t argument_count;

    if (callee == NULL) {
        return false;
    }
    callee_snapshot = *callee;
    callee = &callee_snapshot;
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

typedef struct MinicOffsetofDesignatorState {
    MinicType type;
    MinicExpressionId offset_id;
    bool is_array;
} MinicOffsetofDesignatorState;

static bool append_offsetof_term(MinicParser *parser,
                                 MinicSourcePosition begin,
                                 MinicExpressionId term_id,
                                 MinicOffsetofDesignatorState *state) {
    const MinicExpression *term;
    MinicExpression sum;

    if (parser == NULL || state == NULL || term_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    if (state->offset_id == MINIC_EXPRESSION_INVALID) {
        state->offset_id = term_id;
        return true;
    }
    term = minic_c0_program_expression(parser->program, term_id);
    if (term == NULL || !minic_type_is_integer(term->type)) {
        minic_parser_error(parser, "invalid __builtin_offsetof designator offset term");
        return false;
    }
    (void)memset(&sum, 0, sizeof(sum));
    sum.kind = MINIC_EXPRESSION_BINARY;
    sum.span.begin = begin;
    sum.span.end = term->span.end;
    sum.type = minic_type_unsigned_long();
    sum.value_category = MINIC_VALUE_RVALUE;
    sum.value.binary.operator_kind = MINIC_BINARY_ADD;
    sum.value.binary.left = state->offset_id;
    sum.value.binary.right = term_id;
    return minic_parser_add_expression(parser, &sum, &state->offset_id);
}

static bool parse_offsetof_member_segment(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType record_type,
                                          MinicOffsetofDesignatorState *state) {
    const MinicRecord *record;
    const MinicRecord *final_record;
    const MinicRecordField *final_field;
    MinicRecordFieldPath path;
    MinicSourceSpan field_span;
    MinicExpression segment;
    MinicExpressionId segment_id;
    size_t anonymous_prefix_offset;
    size_t path_index;

    if (parser == NULL || state == NULL || !minic_type_is_record(record_type) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field in __builtin_offsetof designator");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           "__builtin_offsetof member designator requires a complete record");
        return false;
    }
    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser,
                           path.ambiguous ? "record field is ambiguous in __builtin_offsetof"
                                          : "record has no such field in __builtin_offsetof");
        return false;
    }
    if (path.depth == 0U) {
        minic_parser_error(parser, "empty record field path in __builtin_offsetof");
        return false;
    }
    final_record = minic_c0_program_record(parser->program, path.record_ids[path.depth - 1U]);
    final_field = minic_c0_record_field(final_record, path.field_indices[path.depth - 1U]);
    if (final_field == NULL || final_field->is_bit_field) {
        minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");
        return false;
    }

    anonymous_prefix_offset = 0U;
    for (path_index = 0U; path_index + 1U < path.depth; ++path_index) {
        const MinicRecord *path_record;
        size_t field_offset;

        path_record = minic_c0_program_record(parser->program, path.record_ids[path_index]);
        if (path_record == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                path_record,
                path.field_indices[path_index],
                &field_offset) ||
            anonymous_prefix_offset > SIZE_MAX - field_offset) {
            minic_parser_error(parser,
                               "cannot lay out anonymous member path in __builtin_offsetof");
            return false;
        }
        anonymous_prefix_offset += field_offset;
    }

    (void)memset(&segment, 0, sizeof(segment));
    segment.kind = MINIC_EXPRESSION_OFFSETOF;
    segment.span.begin = begin;
    segment.span.end = field_span.end;
    segment.type = minic_type_unsigned_long();
    segment.value_category = MINIC_VALUE_RVALUE;
    segment.value.offsetof_value.record_id = path.record_ids[path.depth - 1U];
    segment.value.offsetof_value.field_index = path.field_indices[path.depth - 1U];
    segment.value.offsetof_value.anonymous_prefix_offset = anonymous_prefix_offset;
    if (!minic_parser_add_expression(parser, &segment, &segment_id) ||
        !append_offsetof_term(parser, begin, segment_id, state) || !minic_parser_advance(parser)) {
        return false;
    }
    state->type = final_field->type;
    state->is_array = final_field->is_array;
    return true;
}

static bool parse_offsetof_array_segment(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicOffsetofDesignatorState *state) {
    MinicExpressionId index_id;
    MinicExpressionId stride_id;
    MinicExpressionId scaled_id;
    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicSourceSpan index_span;
    MinicType index_type;
    MinicType selected_type;
    MinicType scaled_type;
    size_t element_size;

    if (parser == NULL || state == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!state->is_array) {
        minic_parser_error(parser, "__builtin_offsetof array designator requires an array field");
        return false;
    }
    selected_type = state->type;
    if (!minic_parser_advance(parser) || !parse_expression_internal(parser, &index_id, 0U, true)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    /* The expression pool may grow while adding the stride/scaled nodes below.
     * Snapshot semantic data before any append instead of retaining a pool pointer. */
    index_type = index_expression->type;
    index_span = index_expression->span;
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']' in __builtin_offsetof array designator");
        return false;
    }
    if (!minic_target_info_sizeof_type(
            parser->target_info, parser->program, selected_type, &element_size) ||
        element_size > (size_t)INT64_MAX) {
        minic_parser_error(parser, "cannot lay out __builtin_offsetof array element");
        return false;
    }

    (void)memset(&stride, 0, sizeof(stride));
    stride.kind = MINIC_EXPRESSION_INTEGER;
    stride.span = parser->current.span;
    stride.type = minic_type_unsigned_long();
    stride.value_category = MINIC_VALUE_RVALUE;
    stride.value.integer_value = (int64_t)element_size;
    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_type_integer_common(index_type, stride.type, &scaled_type)) {
        minic_parser_error(parser, "cannot type __builtin_offsetof array index scale");
        return false;
    }

    (void)memset(&scaled, 0, sizeof(scaled));
    scaled.kind = MINIC_EXPRESSION_BINARY;
    scaled.span.begin = index_span.begin;
    scaled.span.end = parser->current.span.end;
    scaled.type = scaled_type;
    scaled.value_category = MINIC_VALUE_RVALUE;
    scaled.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
    scaled.value.binary.left = index_id;
    scaled.value.binary.right = stride_id;
    if (!minic_parser_add_expression(parser, &scaled, &scaled_id) ||
        !append_offsetof_term(parser, begin, scaled_id, state) || !minic_parser_advance(parser)) {
        return false;
    }

    state->type = selected_type;
    state->is_array = false;
    if (minic_type_is_array(selected_type)) {
        nested_array = minic_c0_program_array_type(parser->program, selected_type.array_type_id);
        if (nested_array == NULL || nested_array->element_count == 0U) {
            minic_parser_error(parser,
                               "invalid nested array type in __builtin_offsetof designator");
            return false;
        }
        state->type = nested_array->element_type;
        state->is_array = true;
    }
    return true;
}

static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicType record_type;
    MinicOffsetofDesignatorState state;

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
    {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, record_type.record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }

    state.type = record_type;
    state.offset_id = MINIC_EXPRESSION_INVALID;
    state.is_array = false;
    if (!parse_offsetof_member_segment(parser, begin, record_type, &state)) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_offsetof_array_segment(parser, begin, &state)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicType member_record_type;

            if (state.is_array || !minic_type_is_record(state.type)) {
                minic_parser_error(parser,
                                   "__builtin_offsetof nested member designator requires a record");
                return false;
            }
            member_record_type = state.type;
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected member name after '.' in __builtin_offsetof");
                return false;
            }
            if (!parse_offsetof_member_segment(parser, begin, member_record_type, &state)) {
                return false;
            }
            continue;
        }
        minic_parser_error(parser, "unsupported __builtin_offsetof designator suffix");
        return false;
    }
    if (state.offset_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "empty __builtin_offsetof designator");
        return false;
    }
    *expression_id = state.offset_id;
    return minic_parser_advance(parser);
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

static bool
generic_types_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    return minic_c0_types_compatible(program, left, right);
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
        } else if (generic_types_compatible(parser->program, controlling_type, association_type)) {
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
    expression.value.integer_value =
        generic_types_compatible(parser->program, left_type, right_type) ? 1 : 0;
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

static bool parse_builtin_call_frame_address(MinicParser *parser,
                                             MinicCallFrameAddressKind kind,
                                             const char *spelling,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId level_id;
    const MinicExpression *level_expression;
    MinicConstValue level_value;
    MinicSourcePosition begin;
    int64_t level;

    if (parser == NULL || spelling == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, spelling)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after call-frame builtin") ||
        !parse_expression_internal(parser, &level_id, 0U, true)) {
        return false;
    }
    level_expression = minic_c0_program_expression(parser->program, level_id);
    if (level_expression == NULL || !minic_type_is_integer(level_expression->type) ||
        !minic_const_eval_integer(parser->program, parser->target_info, level_id, &level_value) ||
        !minic_const_value_as_int64(parser->program, parser->target_info, &level_value, &level)) {
        minic_parser_error(parser, "%s level must be an integer constant", spelling);
        return false;
    }
    if (level < 0 || (uint64_t)level > (uint64_t)UINT_MAX ||
        !minic_target_info_call_frame_address_supported(
            parser->target_info, kind, (unsigned int)level)) {
        minic_parser_error(parser, "%s level is not supported by target", spelling);
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after call-frame builtin level");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CALL_FRAME_ADDRESS;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.call_frame_address.kind = kind;
    expression.value.call_frame_address.level = (unsigned int)level;
    if (!minic_type_pointer_to(minic_type_void(), &expression.type)) {
        minic_parser_error(parser, "cannot form call-frame builtin result type");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_unreachable(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_unreachable")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_unreachable")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "__builtin_unreachable takes no arguments");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNREACHABLE;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
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
    MinicEnumeratorId enumerator_id;

    if (generic_token_text_equals(parser, "__builtin_unreachable")) {
        if (!parse_builtin_unreachable(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_return_address")) {
        if (!parse_builtin_call_frame_address(
                parser, MINIC_CALL_FRAME_ADDRESS_RETURN, "__builtin_return_address", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_frame_address")) {
        if (!parse_builtin_call_frame_address(
                parser, MINIC_CALL_FRAME_ADDRESS_FRAME, "__builtin_frame_address", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
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
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL ||
        parser->current.kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
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
        enumerator_id = minic_parser_find_enum_constant(parser, name_span);
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
        if (enumerator_id != MINIC_ENUMERATOR_INVALID) {
            const MinicEnumerator *enumerator;

            enumerator = minic_c0_program_enumerator(parser->program, enumerator_id);
            if (enumerator == NULL) {
                minic_parser_error(parser, "invalid enumerator entity");
                return false;
            }
            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_INTEGER;
            expression.span = name_span;
            expression.type = enumerator->type;
            expression.value_category = MINIC_VALUE_RVALUE;
            (void)memcpy(
                &expression.value.integer_value, &enumerator->bits, sizeof(enumerator->bits));
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
            MinicSourcePosition left_begin;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL) {
                minic_parser_error(parser, "invalid comma expression operand");
                return false;
            }
            left_begin = left_expression->span.begin;
            if (!minic_parser_advance(parser) ||
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
            comma_expression.span.begin = left_begin;
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
        if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&
            !minic_parser_materialize_array_object_type(parser, operand_id, &measured_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot preserve array object type for sizeof");
            }
            return false;
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

static bool parse_label_address(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourceSpan name_span;
    MinicStatementId statement_id;

    if (parser == NULL || expression_id == NULL ||
        parser->current.kind != MINIC_TOKEN_AMPERSAND_AMPERSAND) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected label name after '&&'");
        return false;
    }
    name_span = parser->current.span;
    statement_id = minic_parser_find_label_statement(parser, name_span);
    if (statement_id == MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "address of unknown label");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LABEL_ADDRESS;
    expression.span.begin = begin;
    expression.span.end = name_span.end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.label_statement_id = statement_id;
    if (!minic_type_pointer_to(minic_type_void(), &expression.type)) {
        minic_parser_error(parser, "cannot form GNU label-address type");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
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
    if (parser->current.kind == MINIC_TOKEN_AMPERSAND_AMPERSAND) {
        return parse_label_address(parser, expression_id);
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
            minic_type_is_const(operand_expression->type) ||
            minic_c0_expression_array_object_info(parser->program, operand_expression, NULL)) {
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
        } else if (minic_c0_expression_array_object_info(
                       parser->program, operand_expression, NULL)) {
            MinicType array_type;

            if (!minic_parser_materialize_array_object_type(parser, operand, &array_type) ||
                !minic_type_pointer_to(array_type, &expression.type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot form pointer to array object");
                }
                return false;
            }
        } else {
            if (minic_c0_expression_bit_field(parser->program, operand) != NULL) {
                minic_parser_error(parser, "cannot take the address of a bit-field");
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
pointer_difference_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           minic_c0_type_is_complete_object(program, left_unqualified) &&
           minic_c0_type_is_complete_object(program, right_unqualified);
}

static bool normalize_conditional_null_pointer_arm(MinicParser *parser,
                                                   MinicExpressionId *arm_id,
                                                   MinicType result_type) {
    const MinicExpression *arm;
    MinicExpression conversion;
    MinicExpressionId converted_id;

    if (parser == NULL || arm_id == NULL) {
        return false;
    }
    arm = minic_c0_program_expression(parser->program, *arm_id);
    if (arm == NULL) {
        return false;
    }
    if (minic_type_equal(arm->type, result_type) || !minic_type_is_pointer(result_type) ||
        !minic_c0_expression_is_null_pointer_constant_v0(parser->program, *arm_id)) {
        return true;
    }

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = arm->span;
    conversion.type = result_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = *arm_id;
    if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
        return false;
    }
    *arm_id = converted_id;
    return true;
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
        minic_type_is_pointer(right) &&
        minic_c0_pointer_relational_compatible(program, left, right)) {
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
        !minic_c0_pointer_arithmetic_pointee_allowed(program, pointee_type)) {
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
                       !minic_c0_pointer_arithmetic_pointee_allowed(parser->program,
                                                                    pointee_type)) {
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
        bool uses_condition_value;

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
        uses_condition_value = false;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COLON) {
            when_true = left;
            uses_condition_value = true;
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
                !parse_expression_internal(parser, &when_false, 0U, true)) {
                return false;
            }
        } else if (!parse_expression_internal(parser, &when_true, 0U, true) ||
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
        conditional.value.conditional.uses_condition_value = uses_condition_value;
        if (!minic_c0_conditional_result_type(
                parser->program, when_true, when_false, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
        if (!normalize_conditional_null_pointer_arm(parser, &when_true, conditional.type) ||
            !normalize_conditional_null_pointer_arm(parser, &when_false, conditional.type)) {
            minic_parser_error(parser, "cannot normalize conditional null pointer arm");
            return false;
        }
        conditional.value.conditional.when_true = when_true;
        conditional.value.conditional.when_false = when_false;
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
            minic_c0_expression_array_object_info(parser->program, target_expression, NULL) ||
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
                !minic_c0_type_is_complete_object(parser->program, pointee_type)) {
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
            minic_c0_expression_array_object_info(parser->program, target_expression, NULL) ||
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
            if (!minic_c0_record_value_is_copy_source(parser->program, value_id) ||
                !minic_type_is_record(value_expression->type) ||
                target_type.record_id != value_expression->type.record_id) {
                minic_parser_error(
                    parser, "record assignment expression requires a matching record copy source");
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
