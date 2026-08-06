#include "frontend/parser_internal.h"

#include <string.h>

static bool postfix_element_type(
    const MinicParser *parser,
    MinicExpressionId base_id,
    MinicType *element_type)
{
    const MinicExpression *base;

    if (element_type == NULL) {
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL) {
        return false;
    }
    if (base->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, base->value.local_id);
        if (local != NULL && local->element_count > 1U) {
            *element_type = local->type;
            return true;
        }
    }
    if (minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            parser->program,
            base->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        *element_type = array_type->element_type;
        return true;
    }
    return minic_type_pointee(base->type, element_type);
}

static bool parse_one_subscript(
    MinicParser *parser,
    MinicExpressionId base_id,
    MinicExpressionId *expression_id)
{
    const MinicExpression *base;
    MinicSourceSpan base_span;
    MinicType element_type;
    MinicExpressionId index_id;
    const MinicExpression *index_expression;
    MinicSourcePosition subscript_end;
    MinicExpression subscript;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !postfix_element_type(parser, base_id, &element_type)) {
        minic_parser_error(parser, "subscript base must be an array or pointer");
        return false;
    }
    base_span = base->span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &index_id, 0U)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL ||
        !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "array index must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']'");
        return false;
    }
    subscript_end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&subscript, 0, sizeof(subscript));
    subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
    subscript.span.begin = base_span.begin;
    subscript.span.end = subscript_end;
    subscript.type = element_type;
    subscript.value_category = MINIC_VALUE_LVALUE;
    subscript.value.subscript.base = base_id;
    subscript.value.subscript.index = index_id;
    return minic_parser_add_expression(parser, &subscript, expression_id);
}

bool minic_parser_parse_postfix(
    MinicParser *parser,
    MinicExpressionId base_id,
    bool require_subscript,
    MinicExpressionId *expression_id)
{
    MinicExpressionId current;
    bool consumed_subscript;

    current = base_id;
    consumed_subscript = false;
    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_ARROW) {
            if (!minic_parser_parse_pointer_member(
                    parser,
                    current,
                    &current)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_one_subscript(parser, current, &current)) {
                return false;
            }
            consumed_subscript = true;
            continue;
        }
        break;
    }
    if (require_subscript && !consumed_subscript) {
        minic_parser_error(parser, "array object requires a subscript");
        return false;
    }
    *expression_id = current;
    return true;
}
