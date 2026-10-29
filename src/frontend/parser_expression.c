#include "frontend/parser_internal.h"
#include "frontend/expression_semantics.h"
#include "frontend/parser_runtime_initializer.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array);
bool minic_parser_expression_parse_internal(MinicParser *parser,
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

bool minic_parser_finish_value_expression(MinicParser *parser,
                                    MinicExpressionId input_id,
                                    bool decay_array,
                                    MinicExpressionId *expression_id) {
    const MinicExpression *input;

    if (!decay_array) {
        *expression_id = input_id;
        return true;
    }
    input = minic_c0_program_expression(parser->program, input_id);
    if (input != NULL && minic_type_is_function(input->type) &&
        input->kind == MINIC_EXPRESSION_DEREFERENCE) {
        const MinicExpression *function_pointer =
            minic_c0_program_expression(parser->program, input->value.unary.operand);
        MinicType pointee;

        /* `(*fnptr)` is a function designator.  In value context the C
           function-to-pointer conversion yields fnptr itself (with all
           original operand effects preserved), not an unassignable function
           lvalue. `sizeof` and `typeof` deliberately use no-decay mode. */
        if (function_pointer != NULL &&
            minic_type_pointee(function_pointer->type, &pointee) &&
            minic_type_is_function(pointee)) {
            *expression_id = input->value.unary.operand;
            return true;
        }
    }
    return minic_parser_apply_array_decay(parser, input_id, expression_id);
}

static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    MinicType literal_type;
    int64_t value;

    span = parser->current.span;
    if (parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        int character_value;

        literal_type = minic_type_int();
        if (!minic_parser_parse_integer_value(parser, &character_value)) {
            return false;
        }
        value = (int64_t)character_value;
    } else {
        MinicIntegerLiteralBase base;
        uint64_t unsigned_value;
        unsigned int long_count;
        bool has_unsigned_suffix;

        if (!minic_parser_current_integer_literal_syntax(
                parser, &base, &has_unsigned_suffix, &long_count) ||
            !minic_parser_parse_unsigned_integer_value64(parser, &unsigned_value) ||
            !minic_target_info_integer_literal_type(parser->target_info,
                                                    base,
                                                    has_unsigned_suffix,
                                                    long_count,
                                                    unsigned_value,
                                                    &literal_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "integer constant has no representable target type");
            }
            return false;
        }
        (void)memcpy(&value, &unsigned_value, sizeof(value));
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = literal_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static void minic_binary128_from_binary64(double value, uint64_t *low, uint64_t *high) {
    uint64_t bits;
    uint64_t exponent;
    uint64_t fraction;
    uint64_t sign;

    (void)memcpy(&bits, &value, sizeof(bits));
    sign = bits >> 63U;
    exponent = (bits >> 52U) & UINT64_C(0x7ff);
    fraction = bits & UINT64_C(0x000fffffffffffff);
    if (exponent == 0U) {
        if (fraction == 0U) {
            *low = 0U;
            *high = sign << 63U;
            return;
        }
        {
            unsigned int highest = 51U;
            int unbiased;
            uint64_t payload;
            unsigned int shift;
            uint64_t high_fraction;

            while ((fraction & (UINT64_C(1) << highest)) == 0U) {
                --highest;
            }
            unbiased = (int)highest - 1074;
            exponent = (uint64_t)(unbiased + 16383);
            payload = fraction ^ (UINT64_C(1) << highest);
            shift = 112U - highest;
            if (shift >= 64U) {
                *low = 0U;
                high_fraction = payload << (shift - 64U);
            } else {
                *low = payload << shift;
                high_fraction = payload >> (64U - shift);
            }
            *high = (sign << 63U) | (exponent << 48U) |
                    (high_fraction & UINT64_C(0x0000ffffffffffff));
            return;
        }
    }
    if (exponent == UINT64_C(0x7ff)) {
        *low = fraction << 60U;
        *high = (sign << 63U) | UINT64_C(0x7fff000000000000) | (fraction >> 4U);
        return;
    }
    exponent = exponent - UINT64_C(1023) + UINT64_C(16383);
    *low = fraction << 60U;
    *high = (sign << 63U) | (exponent << 48U) | (fraction >> 4U);
}

static bool parse_floating(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    MinicType literal_type;
    char *end;
    char *text;
    double double_value;
    float float_value;
    uint32_t float_bits;
    size_t length;
    size_t numeric_length;
    char suffix;

    _Static_assert(sizeof(double) == sizeof(uint64_t), "MiniC requires binary64 host double");
    _Static_assert(sizeof(float) == sizeof(uint32_t), "MiniC requires binary32 host float");

    if (parser->current.kind != MINIC_TOKEN_FLOATING_CONSTANT) {
        minic_parser_error(parser, "expected floating constant");
        return false;
    }
    span = parser->current.span;
    length = span.end.offset - span.begin.offset;
    if (length == 0U || length == SIZE_MAX) {
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

    suffix = text[length - 1U];
    numeric_length = length;
    literal_type = minic_type_double();
    if (suffix == 'f' || suffix == 'F') {
        literal_type = minic_type_float();
        numeric_length -= 1U;
    } else if (suffix == 'l' || suffix == 'L') {
        literal_type = minic_type_long_double();
        numeric_length -= 1U;
    } else {
        suffix = '\0';
    }
    text[numeric_length] = '\0';

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_FLOATING;
    expression.span = span;
    expression.type = literal_type;
    expression.value_category = MINIC_VALUE_RVALUE;

    errno = 0;
    end = NULL;
    if (minic_type_is_float(literal_type)) {
        float_value = strtof(text, &end);
        if (end != text + numeric_length) {
            free(text);
            minic_parser_error(parser, "invalid floating constant");
            return false;
        }
        /* C translation accepts implementation-range results for floating
           constants. Preserve the target binary32 result selected by strtof:
           overflow becomes +/-infinity and underflow becomes the rounded
           subnormal/zero rather than a frontend hard error. */
        (void)memcpy(&float_bits, &float_value, sizeof(float_bits));
        expression.value.floating_bits = (uint64_t)float_bits;
    } else {
        double_value = strtod(text, &end);
        if (end != text + numeric_length) {
            free(text);
            minic_parser_error(parser, "invalid floating constant");
            return false;
        }
        if (minic_type_is_long_double(literal_type)) {
            minic_binary128_from_binary64(double_value,
                                          &expression.value.floating128_bits.low,
                                          &expression.value.floating128_bits.high);
        } else {
            (void)memcpy(&expression.value.floating_bits, &double_value, sizeof(double_value));
        }
    }
    free(text);

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
    if (object->is_thread_local) {
        minic_parser_error(parser,
            "thread-local object access requires target TLS relocation support");
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
    expression.value_category =
        minic_target_info_fixed_register_write_supported(
            parser->target_info, binding->register_name, binding->register_name_length)
            ? MINIC_VALUE_LVALUE : MINIC_VALUE_RVALUE;
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
    if (function == NULL ||
        !minic_c0_program_set_function_referenced(parser->program, function_id, true)) {
        minic_parser_error(parser, "invalid function reference");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    if (!minic_c0_program_add_variadic_function_type(parser->program,
                                                     function->return_type,
                                                     function->parameter_types,
                                                     function->parameter_count,
                                                     function->is_variadic,
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
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type);
}

static bool same_floating_type(MinicType left, MinicType right) {
    return minic_type_equal(left, right) &&
           ((minic_type_is_double(left) && minic_type_is_double(right)) ||
            (minic_type_is_float(left) && minic_type_is_float(right)) ||
            (minic_type_is_long_double(left) && minic_type_is_long_double(right)));
}

static bool parse_static_duration_compound_literal(MinicParser *parser,
                                                   MinicSourcePosition begin,
                                                   MinicType type,
                                                   MinicExpressionId *expression_id) {
    char symbol_name[80];
    MinicExpression expression;
    MinicGlobalObjectId object_id;
    MinicExpressionId base_id;
    const MinicGlobalObject *object;
    int symbol_length;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function != MINIC_FUNCTION_INVALID ||
        (!minic_type_is_record(type) && !minic_type_is_array(type))) {
        return false;
    }
    if (minic_type_is_record(type) &&
        !minic_parser_require_complete_object_type(
            parser, type, "file-scope compound literal requires a complete record type")) {
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             ".Lminic_compound_%zu",
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize file-scope compound literal");
        }
        return false;
    }
    object = minic_c0_program_global_object(parser->program, object_id);
    if (object == NULL) {
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.begin;
    expression.type = object->type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.global_object_id = object_id;
    if (!minic_parser_add_expression(parser, &expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
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

    minic_c0_local_initialize(&local);
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

static bool parse_array_compound_literal(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicType type,
                                         MinicExpressionId *expression_id) {
    const MinicArrayType *array_type;
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression hidden_lvalue;
    MinicExpression compound_literal;
    MinicExpressionId hidden_lvalue_id;
    MinicExpressionId compound_literal_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    size_t element_count;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function == MINIC_FUNCTION_INVALID || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
    if (array_type == NULL || array_type->is_zero_length) {
        minic_parser_error(parser, "array compound literal requires a valid array type");
        return false;
    }
    element_count = array_type->element_count;
    if (element_count == 0U) {
        if (!minic_parser_inspect_array_initializer_extent(parser, &element_count) ||
            element_count == 0U ||
            !minic_c0_program_complete_array_type(parser->program, type, element_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot infer array compound literal extent");
            }
            return false;
        }
        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
    }

    minic_c0_local_initialize(&local);
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = array_type->element_type;
    local.element_count = element_count;
    local.is_array = true;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "cannot allocate array compound literal backing object");
        return false;
    }

    (void)memset(&hidden_lvalue, 0, sizeof(hidden_lvalue));
    hidden_lvalue.kind = MINIC_EXPRESSION_LOCAL;
    hidden_lvalue.span.begin = begin;
    hidden_lvalue.span.end = parser->current.span.begin;
    hidden_lvalue.type = local.type;
    hidden_lvalue.value_category = MINIC_VALUE_LVALUE;
    hidden_lvalue.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &hidden_lvalue, &hidden_lvalue_id) ||
        !minic_c0_program_add_block(parser->program, &initializer_block)) {
        return false;
    }
    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    success = minic_parser_parse_fixed_runtime_array_initializer(
        parser, hidden_lvalue_id, element_count);
    parser->current_block = parent_block;
    if (!success) {
        return false;
    }

    (void)memset(&compound_literal, 0, sizeof(compound_literal));
    compound_literal.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound_literal.span.begin = begin;
    compound_literal.span.end = parser->current.span.begin;
    /* Array convergence is still transitional: local arrays expose their
       element type plus array-object metadata. Keep the same representation
       here so Core can reuse the existing repeated-local object owner. */
    compound_literal.type = local.type;
    compound_literal.value_category = MINIC_VALUE_LVALUE;
    compound_literal.value.compound_literal.local_id = local_id;
    compound_literal.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound_literal, &compound_literal_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, compound_literal_id, expression_id);
}

static bool parse_scalar_compound_literal(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType type,
                                          MinicExpressionId *expression_id) {
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression hidden_lvalue;
    MinicExpression compound_literal;
    MinicExpressionId hidden_lvalue_id;
    MinicExpressionId value_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    MinicStatement assignment;
    const MinicExpression *value;
    MinicSourceSpan initializer_span;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function == MINIC_FUNCTION_INVALID ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_double(type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "scalar compound literal requires a supported scalar type");
        }
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, type, "scalar compound literal requires a complete object type")) {
        return false;
    }

    minic_c0_local_initialize(&local);
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "cannot allocate scalar compound literal backing object");
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
        minic_parser_error(parser, "cannot create scalar compound literal initializer block");
        return false;
    }

    initializer_span.begin = parser->current.span.begin;
    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    success = minic_parser_advance(parser) && minic_parser_parse_expression(parser, &value_id, 0U);
    if (success) {
        value = minic_c0_program_expression(parser->program, value_id);
        if (value == NULL) {
            success = false;
        } else if (!minic_c0_assignment_compatible(parser->program, type, value_id)) {
            MinicExpression conversion;

            if (minic_type_is_pointer(type) || minic_type_is_pointer(value->type) ||
                !minic_type_cast_compatible(type, value->type)) {
                minic_parser_error(parser, "scalar compound literal initializer type mismatch");
                success = false;
            } else {
                (void)memset(&conversion, 0, sizeof(conversion));
                conversion.kind = MINIC_EXPRESSION_CAST;
                conversion.span = value->span;
                conversion.type = type;
                conversion.value_category = MINIC_VALUE_RVALUE;
                conversion.value.unary.operand = value_id;
                success = minic_parser_add_expression(parser, &conversion, &value_id) &&
                          minic_c0_assignment_compatible(parser->program, type, value_id);
            }
        }
    }
    if (success && parser->current.kind == MINIC_TOKEN_COMMA) {
        success = minic_parser_advance(parser);
    }
    if (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "scalar compound literal requires exactly one initializer");
        success = false;
    }
    if (success) {
        value = minic_c0_program_expression(parser->program, value_id);
        if (value == NULL) {
            success = false;
        } else {
            initializer_span.end = parser->current.span.end;
            (void)memset(&assignment, 0, sizeof(assignment));
            assignment.kind = MINIC_STATEMENT_ASSIGN;
            assignment.span.begin = begin;
            assignment.span.end = value->span.end;
            assignment.target_expression = hidden_lvalue_id;
            assignment.expression = value_id;
            assignment.target_statement = MINIC_STATEMENT_INVALID;
            assignment.cleanup_context = parser->cleanup_context;
            assignment.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
            assignment.then_block = MINIC_BLOCK_INVALID;
            assignment.else_block = MINIC_BLOCK_INVALID;
            success =
                minic_parser_add_statement(parser, &assignment) && minic_parser_advance(parser);
        }
    }
    parser->current_block = parent_block;
    if (!success) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize scalar compound literal initializer");
        }
        return false;
    }

    (void)memset(&compound_literal, 0, sizeof(compound_literal));
    compound_literal.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound_literal.span.begin = begin;
    compound_literal.span.end = initializer_span.end;
    compound_literal.type = type;
    compound_literal.value_category = MINIC_VALUE_LVALUE;
    compound_literal.value.compound_literal.local_id = local_id;
    compound_literal.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound_literal, &value_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, value_id, expression_id);
}

/* GCC's `(union U)scalar` extension selects the union member whose declared
   type matches the scalar.  Materialize its initializer once, as an ordinary
   record compound literal, so the Core record-copy path preserves the union
   representation instead of pretending a record is a scalar cast. */
static bool parse_gnu_scalar_to_union_cast(MinicParser *parser,
                                           MinicSourcePosition begin,
                                           MinicType union_type,
                                           MinicExpressionId operand_id,
                                           MinicExpressionId *expression_id) {
    const MinicRecord *record;
    const MinicRecordField *selected;
    const MinicExpression *operand;
    MinicType operand_type;
    MinicSourceSpan span;
    MinicLocal local;
    MinicLocalId local_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    MinicExpression lvalue;
    MinicExpression address;
    MinicExpression member;
    MinicExpression compound;
    MinicExpression cast_result;
    MinicStatement statement;
    MinicExpressionId lvalue_id;
    MinicExpressionId address_id;
    MinicExpressionId member_id;
    MinicExpressionId compound_id;
    size_t member_index;

    record = minic_c0_program_record(parser->program, union_type.record_id);
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (record == NULL || !record->is_complete || !record->is_union || operand == NULL) {
        return false;
    }
    operand_type = operand->type;
    span.begin = begin;
    span.end = operand->span.end;
    selected = NULL;
    for (member_index = 0U; member_index < record->field_count; ++member_index) {
        const MinicRecordField *candidate = &record->fields[member_index];
        MinicType unqualified_member;
        MinicType unqualified_operand;

        if (!candidate->is_array && !candidate->is_bit_field &&
            minic_type_unqualified(candidate->type, &unqualified_member) &&
            minic_type_unqualified(operand_type, &unqualified_operand) &&
            minic_c0_types_compatible(parser->program, unqualified_member, unqualified_operand)) {
            selected = candidate;
            break;
        }
    }
    if (selected == NULL) {
        return false;
    }
    minic_c0_local_initialize(&local);
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = union_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !minic_c0_program_add_block(parser->program, &initializer_block)) {
        minic_parser_error(parser, "cannot allocate GNU union cast backing object");
        return false;
    }
    (void)memset(&lvalue, 0, sizeof(lvalue));
    lvalue.kind = MINIC_EXPRESSION_LOCAL;
    lvalue.span = span;
    lvalue.type = union_type;
    lvalue.value_category = MINIC_VALUE_LVALUE;
    lvalue.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &lvalue, &lvalue_id)) {
        return false;
    }
    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = span;
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = lvalue_id;
    if (!minic_type_pointer_to(union_type, &address.type) ||
        !minic_parser_add_expression(parser, &address, &address_id)) {
        minic_parser_error(parser, "cannot take GNU union cast backing address");
        return false;
    }
    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span = span;
    member.type = selected->type;
    member.value_category = MINIC_VALUE_LVALUE;
    member.value.member.base = address_id;
    member.value.member.record_id = union_type.record_id;
    member.value.member.field_index = member_index;
    if (!minic_parser_add_expression(parser, &member, &member_id)) {
        return false;
    }
    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    if (!minic_parser_statement_assignment_conversion(parser, selected->type, &operand_id) ||
        !minic_c0_assignment_compatible(parser->program, selected->type, operand_id)) {
        parser->current_block = parent_block;
        minic_parser_error(parser, "GNU union cast member type mismatch");
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = span;
    statement.target_expression = member_id;
    statement.expression = operand_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &statement)) {
        parser->current_block = parent_block;
        return false;
    }
    parser->current_block = parent_block;
    (void)memset(&compound, 0, sizeof(compound));
    compound.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound.span = span;
    compound.type = union_type;
    compound.value_category = MINIC_VALUE_LVALUE;
    compound.value.compound_literal.local_id = local_id;
    compound.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound, &compound_id)) {
        return false;
    }
    /* Unlike a C compound literal, a cast result is never a modifiable
       lvalue. A same-record typed cast preserves that value category while
       using the existing aggregate value/copy lowering contract. */
    (void)memset(&cast_result, 0, sizeof(cast_result));
    cast_result.kind = MINIC_EXPRESSION_CAST;
    cast_result.span = span;
    cast_result.type = union_type;
    cast_result.value_category = MINIC_VALUE_RVALUE;
    cast_result.value.unary.operand = compound_id;
    return minic_parser_add_expression(parser, &cast_result, expression_id);
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
        if (parser->current_function == MINIC_FUNCTION_INVALID &&
            (minic_type_is_record(target_type) || minic_type_is_array(target_type))) {
            return parse_static_duration_compound_literal(parser, begin, target_type, expression_id);
        }
        if (minic_type_is_record(target_type)) {
            return parse_record_compound_literal(parser, begin, target_type, expression_id);
        }
        if (minic_type_is_array(target_type)) {
            return parse_array_compound_literal(parser, begin, target_type, expression_id);
        }
        return parse_scalar_compound_literal(parser, begin, target_type, expression_id);
    }
    if (!parse_unary(parser, &operand_id, true)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand != NULL && minic_type_is_record(target_type) &&
        !minic_type_is_record(operand->type) &&
        parse_gnu_scalar_to_union_cast(parser, begin, target_type, operand_id, expression_id)) {
        return true;
    }
    if (operand != NULL && same_floating_type(target_type, operand->type)) {
        *expression_id = operand_id;
        return true;
    }
    if (operand == NULL ||
        (!minic_type_is_void(target_type) &&
         !minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_record(target_type) && minic_type_is_record(operand->type) &&
           minic_c0_types_compatible(parser->program, target_type, operand->type)) &&
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
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_double(type) || minic_type_is_long_double(type) ||
           minic_type_is_record(type);
}

static bool gnu_enum_integer_pointer_call_conversion_compatible(
    const MinicC0Program *program, MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;
    MinicType target_unqualified;
    MinicType source_unqualified;
    MinicType enum_integer_type;

    if (program == NULL || !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) ||
        !minic_type_unqualified(target_pointee, &target_unqualified) ||
        !minic_type_unqualified(source_pointee, &source_unqualified)) {
        return false;
    }
    if (minic_type_is_enum(target_unqualified) && minic_type_is_integer(source_unqualified)) {
        return minic_c0_type_effective_integer_type(
                   program, target_unqualified, &enum_integer_type) &&
               minic_type_equal(enum_integer_type, source_unqualified);
    }
    if (minic_type_is_integer(target_unqualified) && minic_type_is_enum(source_unqualified)) {
        return minic_c0_type_effective_integer_type(
                   program, source_unqualified, &enum_integer_type) &&
               minic_type_equal(enum_integer_type, target_unqualified);
    }
    return false;
}

static bool gnu_function_pointer_to_void_call_conversion_compatible(MinicType target,
                                                                    MinicType source) {
    MinicType source_pointee;
    MinicType void_pointer;

    return minic_type_pointer_to(minic_type_void(), &void_pointer) &&
           minic_type_equal(target, void_pointer) && minic_type_pointee(source, &source_pointee) &&
           minic_type_is_function(source_pointee);
}

/* glibc's GNU __SOCKADDR_ARG transparent union is ABI-equivalent to
 * a pointer argument and is intentionally accepted in function-pointer
 * declarations such as BusyBox get_lsa(fd, getsockname). Match complete
 * signatures, differing ONLY where a transparent union's validated first
 * pointer member is identical to the corresponding pointer parameter.
 * Other incompatible function-pointer prototypes remain rejected. */
static bool gnu_transparent_union_function_pointer_call_compatible(
    const MinicC0Program *program, MinicType target, MinicType source) {
    MinicType target_fn, source_fn;
    const MinicFunctionType *want, *have;
    size_t i;
    bool saw_transparent_union = false;

    if (program == NULL || !minic_type_pointee(target, &target_fn) ||
        !minic_type_is_function(target_fn))
        return false;
    if (minic_type_is_function(source)) {
        source_fn = source;
    } else if (!minic_type_pointee(source, &source_fn) ||
               !minic_type_is_function(source_fn)) {
        return false;
    }
    want = minic_c0_program_function_type(program, target_fn.function_type_id);
    have = minic_c0_program_function_type(program, source_fn.function_type_id);
    if (want == NULL || have == NULL ||
        want->parameter_count != have->parameter_count ||
        want->is_variadic != have->is_variadic ||
        !minic_c0_types_compatible(program, want->return_type, have->return_type))
        return false;
    for (i = 0U; i < want->parameter_count; ++i) {
        MinicType target_abi, source_abi;
        const MinicRecord *w = minic_type_is_record(want->parameter_types[i])
            ? minic_c0_program_record(program, want->parameter_types[i].record_id)
            : NULL;
        const MinicRecord *h = minic_type_is_record(have->parameter_types[i])
            ? minic_c0_program_record(program, have->parameter_types[i].record_id)
            : NULL;
        if (minic_c0_types_compatible(program, want->parameter_types[i],
                                     have->parameter_types[i]))
            continue;
        if (!((w != NULL && w->is_transparent_union) ||
              (h != NULL && h->is_transparent_union)) ||
            !minic_c0_fixed_parameter_abi_type(
                program, want->parameter_types[i], &target_abi) ||
            !minic_c0_fixed_parameter_abi_type(
                program, have->parameter_types[i], &source_abi) ||
            !minic_type_is_pointer(target_abi) ||
            !minic_type_is_pointer(source_abi) ||
            !minic_c0_types_compatible(program, target_abi, source_abi))
            return false;
        saw_transparent_union = true;
    }
    return saw_transparent_union;
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
        ((minic_type_is_integer(target_type) || minic_type_is_float(target_type) ||
          minic_type_is_double(target_type) || minic_type_is_long_double(target_type)) &&
         (minic_type_is_integer(source->type) || minic_type_is_float(source->type) ||
          minic_type_is_double(source->type) || minic_type_is_long_double(source->type))) ||
        minic_type_gnu_pointer_sign_compatible(target_type, source->type) ||
        gnu_enum_integer_pointer_call_conversion_compatible(
            parser->program, target_type, source->type) ||
        gnu_function_pointer_to_void_call_conversion_compatible(target_type, source->type) ||
        gnu_transparent_union_function_pointer_call_compatible(
            parser->program, target_type, source->type) ||
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


bool minic_parser_apply_default_argument_promotion(MinicParser *parser,
                                                    MinicExpressionId *argument_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicType promoted_type;

    if (parser == NULL || argument_id == NULL) {
        return false;
    }
    source_id = *argument_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid default argument promotion source");
        return false;
    }

    if (minic_type_is_float(source->type)) {
        promoted_type = minic_type_double();
    } else if (minic_type_is_integer(source->type) || minic_type_is_enum(source->type)) {
        MinicType integer_source_type;

        integer_source_type = source->type;
        if (minic_type_is_enum(source->type) &&
            !minic_c0_type_effective_integer_type(
                parser->program, source->type, &integer_source_type)) {
            minic_parser_error(parser, "cannot resolve enum default argument promotion");
            return false;
        }
        if (!minic_target_info_integer_promotion(
                parser->target_info, integer_source_type, &promoted_type)) {
            minic_parser_error(parser, "cannot apply default integer argument promotion");
            return false;
        }
        if (minic_type_equal(promoted_type, source->type)) {
            return true;
        }
    } else {
        return true;
    }

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source->span;
    conversion.type = promoted_type;
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
        !minic_parser_parse_expression(parser, &argument_id, 0U) ||
        !minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
        return false;
    }
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
        if (!minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
            return false;
        }
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
    } else {
        if (!minic_parser_apply_default_argument_promotion(parser, &argument_id)) {
            return false;
        }
        if (!minic_c0_call_set_argument(call_expression, argument_index, argument_id)) {
            return false;
        }
        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL || !variadic_argument_type_supported(argument->type)) {
            minic_parser_error(parser, "unsupported variadic argument type");
            return false;
        }
    }
    if (argument_index >= callee->parameter_count && minic_type_is_record(argument->type) &&
               !minic_parser_require_complete_object_type(
                   parser, argument->type, "variadic record argument requires a complete type")) {
        return false;
    }
    return true;
}

bool minic_parser_expression_parse_call_arguments(MinicParser *parser,
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
        if (argument_count >= MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "call argument count exceeds implementation limit");
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
    MinicFixedRegisterBindingId fixed_register_binding_id;
    MinicEnumeratorId enumerator_id;
    MinicParserOrdinaryBinding nearest_binding;
    bool has_nearest_binding;
    bool builtin_handled;

    if (!minic_parser_try_builtin_primary_early(parser, expression_id,
            decay_array, &builtin_handled))
        return false;
    if (builtin_handled)
        return true;
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT ||
        parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        if (!parse_integer(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_FLOATING_CONSTANT) {
        if (!parse_floating(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL ||
        parser->current.kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
        if (!minic_parser_parse_string_literal(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (!minic_parser_try_builtin_primary_late(parser, expression_id,
            decay_array, &builtin_handled))
        return false;
    if (builtin_handled)
        return true;
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        /* GNU C spells the same lexical function name as
         * __func__, __FUNCTION__ and __PRETTY_FUNCTION__.
         * In C++ the last form has a richer signature; MiniC parses C. */
        if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__func__") ||
            minic_parser_current_identifier_lexeme_equals_cstr(parser, "__FUNCTION__") ||
            minic_parser_current_identifier_lexeme_equals_cstr(parser, "__PRETTY_FUNCTION__")) {
            name_span = parser->current.span;
            if (!minic_parser_get_predefined_function_name_object(parser, &global_object_id) ||
                !minic_parser_advance(parser) ||
                !parse_global_reference(parser, name_span, global_object_id, true, &primary_id)) {
                return false;
            }
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        name_span = parser->current.span;
        has_nearest_binding = minic_parser_find_nearest_ordinary_binding(
            parser, name_span, &nearest_binding);
        if (has_nearest_binding &&
            nearest_binding.type_alias_id != MINIC_TYPE_ALIAS_INVALID) {
            minic_parser_error(parser, "typedef name cannot be used as an expression");
            return false;
        }
        local_id = has_nearest_binding
            ? nearest_binding.local_id
            : minic_parser_find_local(parser, name_span);
        if (local_id != MINIC_LOCAL_INVALID) {
            if (!minic_parser_advance(parser) ||
                !parse_local_reference(parser, name_span, local_id, &primary_id)) {
                return false;
            }
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }

        function_id = has_nearest_binding
            ? MINIC_FUNCTION_INVALID : minic_parser_find_function(parser, name_span);
        if (!minic_parser_advance(parser)) {
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_LPAREN &&
            function_id != MINIC_FUNCTION_INVALID) {
            MinicSourcePosition call_end;
            const MinicFunction *callee;
            uint32_t call_overflow_scratch[MINIC_MAX_FUNCTION_PARAMETERS -
                                           MINIC_CALL_INLINE_ARGUMENTS];

            callee = minic_c0_program_function(parser->program, function_id);
            if (callee == NULL || callee->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                !minic_c0_program_set_function_referenced(parser->program, function_id, true) ||
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
            expression.value.call.overflow_arguments = call_overflow_scratch;
            if (!minic_parser_expression_parse_call_arguments(parser, &expression, callee)) {
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
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }

        global_object_id = has_nearest_binding
            ? nearest_binding.global_object_id
            : minic_parser_find_global_object(parser, name_span);
        fixed_register_binding_id = has_nearest_binding
            ? MINIC_FIXED_REGISTER_BINDING_INVALID
            : minic_parser_find_fixed_register_binding(parser, name_span);
        enumerator_id = has_nearest_binding
            ? nearest_binding.enumerator_id
            : minic_parser_find_enum_constant(parser, name_span);

        if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            if (!parse_global_reference(parser, name_span, global_object_id, true, &primary_id)) {
                return false;
            }
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (fixed_register_binding_id != MINIC_FIXED_REGISTER_BINDING_INVALID) {
            if (!parse_fixed_register_reference(
                    parser, name_span, fixed_register_binding_id, &primary_id)) {
                return false;
            }
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (function_id != MINIC_FUNCTION_INVALID) {
            if (!parse_function_reference(parser, name_span, function_id, &primary_id)) {
                return false;
            }
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
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
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        {
            const char *trace = getenv("CORE_FAST_TRACE");
            if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
                MinicGlobalObjectId entity_id;
                const MinicGlobalObject *entity;

                entity_id = minic_parser_find_global_object_entity(parser, name_span);
                entity = entity_id == MINIC_GLOBAL_OBJECT_INVALID
                             ? NULL
                             : minic_c0_program_global_object(parser->program, entity_id);
                {
                    size_t context_begin;
                    size_t context_end;
                    size_t context_length;

                    context_begin =
                        name_span.begin.offset > 32U ? name_span.begin.offset - 32U : 0U;
                    context_end = name_span.end.offset + 32U < parser->lexer.length
                                      ? name_span.end.offset + 32U
                                      : parser->lexer.length;
                    context_length = context_end - context_begin;
                    (void)fprintf(stderr,
                              "FRONTEND_LOOKUP_DETAIL name=%.*s span=%zu..%zu "
                              "current=%zu..%zu lexer_cursor=%zu raw=%.*s local=%llu function=%llu "
                              "global=%llu entity=%llu fixed=%llu enum=%llu globals=%zu "
                              "entity_block_scope_only=%d entity_extern=%d entity_tentative=%d\n",
                              (int)minic_parser_span_length(name_span),
                              parser->source + name_span.begin.offset,
                              name_span.begin.offset,
                              name_span.end.offset,
                              parser->current.span.begin.offset,
                              parser->current.span.end.offset,
                              parser->lexer.cursor,
                              (int)context_length,
                              parser->source + context_begin,
                              (unsigned long long)local_id,
                              (unsigned long long)function_id,
                              (unsigned long long)global_object_id,
                              (unsigned long long)entity_id,
                              (unsigned long long)fixed_register_binding_id,
                              (unsigned long long)enumerator_id,
                              parser->program->global_object_count,
                              entity != NULL && entity->is_block_scope_extern_only ? 1 : 0,
                              entity != NULL && entity->is_extern ? 1 : 0,
                              entity != NULL && entity->is_tentative ? 1 : 0);
                }
            }
        }
        minic_parser_error(parser, "use of undeclared local '%.*s'",
                           (int)minic_parser_span_length(name_span),
                           parser->source + name_span.begin.offset);
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
            return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (!minic_parser_expression_parse_internal(parser, &primary_id, 0U, decay_array)) {
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
                !minic_parser_expression_parse_internal(parser, &right_id, 0U, true)) {
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
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
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

static bool alignof_expression_alignment(MinicParser *parser,
                                         MinicExpressionId operand_id,
                                         const MinicExpression *operand,
                                         size_t *alignment) {
    const MinicDataLayout *layout;
    MinicArrayObjectInfo array_info;
    size_t size;

    if (parser == NULL || operand == NULL || alignment == NULL) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, operand->value.member.record_id);
        return minic_data_layout_record_field_alignment(
            layout, parser->program, record, operand->value.member.field_index, alignment);
    }
    if (operand->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, operand->value.global_object_id);
        if (object == NULL ||
            !minic_data_layout_global_object(layout, parser->program, object, &size, alignment) ||
            *alignment == 0U) {
            return false;
        }
        return true;
    }
    if (minic_c0_expression_array_object_info(parser->program, operand, &array_info)) {
        return minic_data_layout_type(
            layout, parser->program, array_info.element_type, &size, alignment);
    }
    (void)operand_id;
    return minic_data_layout_type(layout, parser->program, operand->type, &size, alignment);
}

static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicType measured_type;
    size_t measured_alignment;
    size_t measured_size;

    if (parser == NULL || expression_id == NULL || !current_is_alignof(parser)) {
        return false;
    }
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
            minic_parser_error(parser, "expected ')' after alignof type");
            return false;
        }
        end = parser->current.span.end;
        if (!minic_parser_advance(parser) ||
            !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                    parser->program,
                                    measured_type,
                                    &measured_size,
                                    &measured_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "alignof requires a supported type");
            }
            return false;
        }
        (void)measured_size;
    } else {
        MinicExpressionId operand_id;
        const MinicExpression *operand;

        if (!parse_unary(parser, &operand_id, false)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL ||
            !alignof_expression_alignment(parser, operand_id, operand, &measured_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "alignof requires an alignable expression");
            }
            return false;
        }
        end = operand->span.end;
    }
    if (measured_alignment > (size_t)INT64_MAX) {
        minic_parser_error(parser, "alignof result is not representable");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = (int64_t)measured_alignment;
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
        if (operand->kind == MINIC_EXPRESSION_LOCAL) {
            const MinicLocal *local;

            local = minic_c0_program_local(parser->program, operand->value.local_id);
            if (local != NULL && local->is_array &&
                local->dynamic_count_local_id != MINIC_LOCAL_INVALID) {
                MinicExpression count_lvalue;
                MinicExpression count_read;
                MinicExpression size_constant;
                MinicExpression product;
                MinicExpressionId count_lvalue_id;
                MinicExpressionId count_read_id;
                MinicExpressionId size_constant_id;
                size_t element_size;

                if (!minic_target_info_sizeof_type(
                        parser->target_info, parser->program, local->type, &element_size) ||
                    element_size == 0U || element_size > (size_t)INT64_MAX) {
                    minic_parser_error(parser, "sizeof variable length array element is unsupported");
                    return false;
                }
                end = operand->span.end;
                (void)memset(&count_lvalue, 0, sizeof(count_lvalue));
                count_lvalue.kind = MINIC_EXPRESSION_LOCAL;
                count_lvalue.span.begin = begin;
                count_lvalue.span.end = end;
                count_lvalue.type = minic_type_unsigned_long();
                count_lvalue.value_category = MINIC_VALUE_LVALUE;
                count_lvalue.value.local_id = local->dynamic_count_local_id;
                if (!minic_parser_add_expression(parser, &count_lvalue, &count_lvalue_id)) {
                    return false;
                }

                (void)memset(&count_read, 0, sizeof(count_read));
                count_read.kind = MINIC_EXPRESSION_LVALUE_READ;
                count_read.span = count_lvalue.span;
                count_read.type = minic_type_unsigned_long();
                count_read.value_category = MINIC_VALUE_RVALUE;
                count_read.value.unary.operand = count_lvalue_id;
                if (!minic_parser_add_expression(parser, &count_read, &count_read_id)) {
                    return false;
                }
                if (element_size == 1U) {
                    *expression_id = count_read_id;
                    return true;
                }

                (void)memset(&size_constant, 0, sizeof(size_constant));
                size_constant.kind = MINIC_EXPRESSION_INTEGER;
                size_constant.span = count_lvalue.span;
                size_constant.type = minic_type_unsigned_long();
                size_constant.value_category = MINIC_VALUE_RVALUE;
                size_constant.value.integer_value = (int64_t)element_size;
                if (!minic_parser_add_expression(
                        parser, &size_constant, &size_constant_id)) {
                    return false;
                }

                (void)memset(&product, 0, sizeof(product));
                product.kind = MINIC_EXPRESSION_BINARY;
                product.span = count_lvalue.span;
                product.type = minic_type_unsigned_long();
                product.value_category = MINIC_VALUE_RVALUE;
                product.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
                product.value.binary.left = count_read_id;
                product.value.binary.right = size_constant_id;
                return minic_parser_add_expression(parser, &product, expression_id);
            }
        }
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
        MinicStatement pending_label;

        if (parser->current_function == MINIC_FUNCTION_INVALID) {
            minic_parser_error(parser, "GNU label address requires a function label");
            return false;
        }
        statement_id = parser->program->statement_count;
        (void)memset(&pending_label, 0, sizeof(pending_label));
        pending_label.kind = MINIC_STATEMENT_LABEL;
        pending_label.span = name_span;
        pending_label.target_expression = MINIC_EXPRESSION_INVALID;
        pending_label.expression = MINIC_EXPRESSION_INVALID;
        pending_label.target_statement = statement_id;
        pending_label.then_block = MINIC_BLOCK_INVALID;
        pending_label.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_program_add_statement(parser->program, &pending_label, &statement_id)) {
            minic_parser_error(parser, "cannot reserve forward GNU label address");
            return false;
        }
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

    /* GNU __extension__ is a diagnostic-suppression prefix only.  It does
       not change the type, value category, or runtime behavior of its operand,
       so keep it out of AST/Core and continue through the ordinary unary/cast
       grammar. */
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__extension__")) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        return parse_unary(parser, expression_id, decay_array);
    }
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
        if (!parse_cast(parser, &result_id)) {
            return false;
        }
        return minic_parser_finish_value_expression(parser, result_id, decay_array, expression_id);
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
                !minic_c0_pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
                minic_parser_error(parser,
                                   "pointer update requires an arithmetic-compatible pointee type");
                return false;
            }
        } else if (!minic_type_is_integer(operand_expression->type) &&
                   !minic_type_is_float(operand_expression->type) &&
                   !minic_type_is_double(operand_expression->type) &&
                   !minic_type_is_long_double(operand_expression->type)) {
            minic_parser_error(parser,
                               "prefix update requires arithmetic or pointer lvalue");
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
        return minic_parser_finish_value_expression(parser, result_id, decay_array, expression_id);
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
    if (minic_type_is_double(operand_expression->type) ||
        minic_type_is_float(operand_expression->type) ||
        minic_type_is_long_double(operand_expression->type)) {
        if (operator_token.kind != MINIC_TOKEN_PLUS && operator_token.kind != MINIC_TOKEN_MINUS) {
            minic_parser_error(parser, "floating unary arithmetic requires '+' or '-'");
            return false;
        }
        expression.value.unary.operator_kind =
            operator_token.kind == MINIC_TOKEN_PLUS ? MINIC_UNARY_PLUS : MINIC_UNARY_NEGATE;
        if (!minic_type_unqualified(operand_expression->type, &expression.type)) {
            minic_parser_error(parser, "cannot form floating unary result type");
            return false;
        }
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary arithmetic requires an integer or floating operand");
        return false;
    }
    if (operator_token.kind == MINIC_TOKEN_TILDE) {
        expression.value.unary.operator_kind = MINIC_UNARY_BITWISE_NOT;
        if (!minic_target_info_integer_common_for_program(parser->target_info,
                                                          parser->program,
                                                          operand_expression->type,
                                                          operand_expression->type,
                                                          &expression.type)) {
            return false;
        }
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (operator_token.kind == MINIC_TOKEN_PLUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_PLUS;
        if (!minic_target_info_integer_common_for_program(parser->target_info,
                                                          parser->program,
                                                          operand_expression->type,
                                                          operand_expression->type,
                                                          &expression.type)) {
            return false;
        }
    } else {
        expression.value.unary.operator_kind = MINIC_UNARY_NEGATE;
        if (!minic_target_info_integer_common_for_program(parser->target_info,
                                                          parser->program,
                                                          operand_expression->type,
                                                          operand_expression->type,
                                                          &expression.type)) {
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

static bool normalize_float_binary_operands(MinicParser *parser,
                                                MinicExpressionId *left_id,
                                                MinicExpressionId *right_id,
                                                MinicTokenKind kind) {
    const MinicExpression *left;
    const MinicExpression *right;
    MinicExpression conversion;
    MinicExpressionId converted_id;
    bool eligible;

    if (parser == NULL || left_id == NULL || right_id == NULL ||
        (!binary_is_comparison(kind) && !binary_is_double_arithmetic(kind))) {
        return true;
    }
    left = minic_c0_program_expression(parser->program, *left_id);
    right = minic_c0_program_expression(parser->program, *right_id);
    if (left == NULL || right == NULL) {
        return false;
    }

    if (minic_type_is_long_double(left->type) ||
        minic_type_is_long_double(right->type)) {
        bool left_numeric =
            minic_type_is_integer(left->type) || minic_type_is_float(left->type) ||
            minic_type_is_double(left->type) || minic_type_is_long_double(left->type);
        bool right_numeric =
            minic_type_is_integer(right->type) || minic_type_is_float(right->type) ||
            minic_type_is_double(right->type) || minic_type_is_long_double(right->type);

        if (!left_numeric || !right_numeric) {
            return true;
        }
        if (!minic_type_is_long_double(left->type)) {
            (void)memset(&conversion, 0, sizeof(conversion));
            conversion.kind = MINIC_EXPRESSION_CAST;
            conversion.span = left->span;
            conversion.type = minic_type_long_double();
            conversion.value_category = MINIC_VALUE_RVALUE;
            conversion.value.unary.operand = *left_id;
            if (!minic_parser_add_expression(parser, &conversion, &converted_id)) return false;
            *left_id = converted_id;
        }
        right = minic_c0_program_expression(parser->program, *right_id);
        if (right == NULL) return false;
        if (!minic_type_is_long_double(right->type)) {
            (void)memset(&conversion, 0, sizeof(conversion));
            conversion.kind = MINIC_EXPRESSION_CAST;
            conversion.span = right->span;
            conversion.type = minic_type_long_double();
            conversion.value_category = MINIC_VALUE_RVALUE;
            conversion.value.unary.operand = *right_id;
            if (!minic_parser_add_expression(parser, &conversion, &converted_id)) return false;
            *right_id = converted_id;
        }
        return true;
    }

    /* Mixed float/integer arithmetic follows the ordinary arithmetic
       conversions by converting the integer operand to binary32.  Do this
       before the existing float/double bridge: widening the integer directly
       to double would change the required float-rounding semantics. */
    if (minic_type_is_float(left->type) && minic_type_is_integer(right->type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = right->span;
        conversion.type = minic_type_float();
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = *right_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        *right_id = converted_id;
        return true;
    }
    if (minic_type_is_integer(left->type) && minic_type_is_float(right->type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = left->span;
        conversion.type = minic_type_float();
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = *left_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        *left_id = converted_id;
        return true;
    }

    /* Comparing binary32 values after exact widening to binary64 preserves all
       IEEE-754 ordered/equality results, including NaNs, infinities and signed
       zero. Mixed float/double comparison and arithmetic use the ordinary C
       conversion to double. */
    eligible =
        (minic_type_is_float(left->type) &&
         (minic_type_is_float(right->type) || minic_type_is_double(right->type))) ||
        (minic_type_is_float(right->type) &&
         (minic_type_is_float(left->type) || minic_type_is_double(left->type)));
    if (!eligible) {
        return true;
    }
    if (binary_is_double_arithmetic(kind) && !binary_is_comparison(kind) &&
        !((minic_type_is_float(left->type) && minic_type_is_double(right->type)) ||
          (minic_type_is_double(left->type) && minic_type_is_float(right->type)))) {
        /* float op float remains binary32. This bridge only owns the ordinary
         * mixed float/double conversion to binary64. */
        return true;
    }

    if (minic_type_is_float(left->type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = left->span;
        conversion.type = minic_type_double();
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = *left_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        *left_id = converted_id;
    }

    /* Adding the left conversion may relocate Program expression storage. */
    right = minic_c0_program_expression(parser->program, *right_id);
    if (right == NULL) {
        return false;
    }
    if (minic_type_is_float(right->type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = right->span;
        conversion.type = minic_type_double();
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = *right_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        *right_id = converted_id;
    }
    return true;
}

static bool binary_result_type(const MinicTargetInfo *target,
                               const MinicC0Program *program,
                               MinicTokenKind kind,
                               MinicType left,
                               MinicType right,
                               MinicType *result) {
    MinicType pointer_type;
    MinicType pointee_type;
    bool has_double_operand;
    bool has_long_double_operand;
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
            return minic_target_info_integer_common_for_program(
                target, program, left, left, result);
        }
        return minic_target_info_integer_common_for_program(target, program, left, right, result);
    }
    if (((minic_type_is_float(left) && minic_type_is_float(right)) ||
         (minic_type_is_float(left) && minic_type_is_integer(right)) ||
         (minic_type_is_integer(left) && minic_type_is_float(right))) &&
        (binary_is_double_arithmetic(kind) || binary_is_comparison(kind))) {
        *result = binary_is_comparison(kind) ? minic_type_int() : minic_type_float();
        return true;
    }
    has_long_double_operand =
        minic_type_is_long_double(left) || minic_type_is_long_double(right);
    has_numeric_operands =
        (minic_type_is_integer(left) || minic_type_is_float(left) ||
         minic_type_is_double(left) || minic_type_is_long_double(left)) &&
        (minic_type_is_integer(right) || minic_type_is_float(right) ||
         minic_type_is_double(right) || minic_type_is_long_double(right));
    if (binary_is_comparison(kind) && has_long_double_operand && has_numeric_operands) {
        *result = minic_type_int();
        return true;
    }
    if (has_long_double_operand && has_numeric_operands && binary_is_double_arithmetic(kind)) {
        *result = minic_type_long_double();
        return true;
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
        minic_c0_pointer_difference_compatible(program, left, right)) {
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

bool minic_parser_expression_parse_internal(MinicParser *parser,
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
            !minic_parser_expression_parse_internal(parser, &right, precedence + 1U, true)) {
            return false;
        }
        if (!normalize_float_binary_operands(parser, &left, &right, token_kind)) {
            minic_parser_error(parser, "cannot normalize floating binary operands");
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
        } else if (!binary_result_type(parser->target_info,
                                       parser->program,
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
                !minic_parser_expression_parse_internal(parser, &when_false, 0U, true)) {
                return false;
            }
        } else if (!minic_parser_parse_full_expression(parser, &when_true) ||
                   !minic_parser_expect(
                       parser, MINIC_TOKEN_COLON, "expected ':' in conditional expression") ||
                   !minic_parser_expression_parse_internal(parser, &when_false, 0U, true)) {
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
                parser->program, parser->target_info, when_true, when_false, &conditional.type)) {
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
            !minic_parser_expression_parse_internal(parser, &value_id, 0U, true)) {
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
                !minic_c0_pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
                minic_parser_error(
                    parser,
                    "pointer compound assignment expression requires += or -= with an integer");
                return false;
            }
        } else if (minic_type_is_float(target_type) ||
                   minic_type_is_double(target_type) ||
                   minic_type_is_long_double(target_type)) {
            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT &&
                 compound_operator != MINIC_BINARY_MULTIPLY &&
                 compound_operator != MINIC_BINARY_DIVIDE) ||
                (!minic_type_is_integer(value_expression->type) &&
                 !minic_type_is_float(value_expression->type) &&
                 !minic_type_is_double(value_expression->type) &&
                 !minic_type_is_long_double(value_expression->type))) {
                minic_parser_error(parser,
                                   "floating compound assignment requires arithmetic operands");
                return false;
            }
        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target_type) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_target_info_integer_common_for_program(parser->target_info,
                                                              parser->program,
                                                              target_type,
                                                              value_expression->type,
                                                              &common_type)) {
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
            !minic_parser_expression_parse_internal(parser, &value_id, 0U, true)) {
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

                /* No implicit arbitrary pointer conversions: only a
                 * strictly identical single-level pointee with discarded
                 * const (GNU -Wdiscarded-qualifiers) may be materialized
                 * as an explicit diagnostic-compatible cast. */
                if (((minic_type_is_pointer(target_type) ||
                      minic_type_is_pointer(value_expression->type)) &&
                     !minic_type_gnu_discard_const_one_pointer_compatible(
                         target_type, value_expression->type)) ||
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

static bool parse_comma_expression_tail(MinicParser *parser,
                                        MinicExpressionId left,
                                        MinicExpressionId *expression_id,
                                        bool decay_array) {
    if (parser == NULL || expression_id == NULL ||
        minic_c0_program_expression(parser->program, left) == NULL) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicExpression sequence;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        if (!minic_parser_advance(parser) ||
            !minic_parser_expression_parse_internal(parser, &right, 0U, decay_array)) {
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

static bool
parse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId left;

    if (!minic_parser_expression_parse_internal(parser, &left, 0U, decay_array)) {
        return false;
    }
    return parse_comma_expression_tail(parser, left, expression_id, decay_array);
}

bool minic_parser_parse_expression_no_decay(MinicParser *parser, MinicExpressionId *expression_id) {
    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    return minic_parser_expression_parse_internal(parser, expression_id, 0U, false);
}

bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return minic_parser_expression_parse_internal(parser, expression_id, minimum_precedence, true);
}

bool minic_parser_parse_null_pointer_constant_expression(MinicParser *parser,
                                                         MinicType target_type) {
    MinicExpressionId expression_id;

    if (parser == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    return minic_c0_assignment_compatible(parser->program, target_type, expression_id) &&
           minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id);
}

bool minic_parser_parse_full_expression_tail(MinicParser *parser,
                                             MinicExpressionId left,
                                             MinicExpressionId *expression_id) {
    return parse_comma_expression_tail(parser, left, expression_id, true);
}

bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id) {
    return parse_comma_expression(parser, expression_id, true);
}
