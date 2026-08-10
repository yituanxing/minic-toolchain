#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()

helper_anchor = (
    "static bool parse_builtin_constant_p(MinicParser *parser, "
    "MinicExpressionId *expression_id) {\n"
)
if text.count(helper_anchor) != 1:
    raise SystemExit("object-size helper insertion anchor mismatch")

helpers = r'''static bool object_extent_direct_object(const MinicParser *parser,
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

        object = minic_c0_program_global_object(
            parser->program, expression->value.global_object_id);
        return object != NULL &&
               minic_target_info_sizeof_type(
                   parser->target_info, parser->program, object->type, extent);
    }
    return false;
}

static bool object_extent_exact_start(const MinicParser *parser,
                                      MinicExpressionId pointer_id,
                                      size_t *extent) {
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

static bool parse_builtin_object_size(MinicParser *parser,
                                      MinicExpressionId *expression_id) {
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
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_object_size") ||
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

'''
text = text.replace(helper_anchor, helpers + helper_anchor, 1)

dispatch_anchor = '    if (generic_token_text_equals(parser, "__builtin_add_overflow")) {'
if text.count(dispatch_anchor) != 1:
    raise SystemExit("object-size dispatch insertion anchor mismatch")

dispatch = r'''    if (generic_token_text_equals(parser, "__builtin_object_size")) {
        if (!parse_builtin_object_size(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
text = text.replace(dispatch_anchor, dispatch + dispatch_anchor, 1)
path.write_text(text)
