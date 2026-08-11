#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool add_local_lvalue_expression(MinicParser *parser,
                                        MinicLocalId local_id,
                                        MinicSourceSpan span,
                                        MinicExpressionId *expression_id) {
    const MinicLocal *local;
    MinicExpression expression;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL) {
        minic_parser_error(parser, "invalid local assignment target");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LOCAL;
    expression.span = span;
    expression.type = local->type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.local_id = local_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool expression_is_modifiable_lvalue(const MinicExpression *expression) {
    return expression != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
           !minic_type_is_const(expression->type);
}

static bool apply_assignment_conversion(MinicParser *parser,
                                        MinicType target_type,
                                        MinicExpressionId *expression_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    source_id = *expression_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid assignment conversion source");
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, source_id)) {
        return true;
    }
    if (!minic_type_is_double(target_type) || !minic_type_is_integer(source->type)) {
        return true;
    }

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source->span;
    conversion.type = target_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, expression_id);
}

static bool add_zero_initialized_local_array(MinicParser *parser,
                                             MinicLocalId local_id,
                                             MinicSourceSpan initializer_span) {
    const MinicLocal *local;
    MinicExpression base;
    MinicExpression zero;
    MinicExpressionId base_id;
    MinicExpressionId zero_id;
    MinicExpressionId value_id;
    size_t index;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || local->element_count <= 1U || local->element_count > (size_t)INT_MAX) {
        minic_parser_error(parser, "unsupported local array zero initializer");
        return false;
    }

    (void)memset(&base, 0, sizeof(base));
    base.kind = MINIC_EXPRESSION_LOCAL;
    base.span = local->name_span;
    base.type = local->type;
    base.value_category = MINIC_VALUE_LVALUE;
    base.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &base, &base_id)) {
        return false;
    }

    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    if (!minic_parser_add_expression(parser, &zero, &zero_id)) {
        return false;
    }
    value_id = zero_id;
    if (!apply_assignment_conversion(parser, local->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, local->type, value_id)) {
        minic_parser_error(parser, "array zero initializer does not match element type");
        return false;
    }

    for (index = 0U; index < local->element_count; ++index) {
        MinicExpression index_expression;
        MinicExpression subscript;
        MinicExpressionId index_id;
        MinicExpressionId target_id;
        MinicStatement statement;

        (void)memset(&index_expression, 0, sizeof(index_expression));
        index_expression.kind = MINIC_EXPRESSION_INTEGER;
        index_expression.span = initializer_span;
        index_expression.type = minic_type_int();
        index_expression.value_category = MINIC_VALUE_RVALUE;
        index_expression.value.integer_value = (int)index;
        if (!minic_parser_add_expression(parser, &index_expression, &index_id)) {
            return false;
        }

        (void)memset(&subscript, 0, sizeof(subscript));
        subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
        subscript.span.begin = local->name_span.begin;
        subscript.span.end = initializer_span.end;
        subscript.type = local->type;
        subscript.value_category = MINIC_VALUE_LVALUE;
        subscript.value.subscript.base = base_id;
        subscript.value.subscript.index = index_id;
        if (!minic_parser_add_expression(parser, &subscript, &target_id)) {
            return false;
        }

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span = subscript.span;
        statement.target_expression = target_id;
        statement.expression = value_id;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }
    return true;
}

static bool parse_local_array_zero_initializer(MinicParser *parser,
                                               MinicLocalId local_id,
                                               MinicSourceSpan name_span) {
    MinicSourceSpan initializer_span;
    int value;

    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_LBRACE ||
        !minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
    initializer_span.begin = name_span.begin;
    if (!minic_parser_parse_integer_value(parser, &value) || value != 0 ||
        parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
    initializer_span.end = parser->current.span.end;
    return minic_parser_advance(parser) &&
           add_zero_initialized_local_array(parser, local_id, initializer_span);
}

static bool aggregate_expression_is_zero_constant(const MinicC0Program *program,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type)) {
        return aggregate_expression_is_zero_constant(program, expression->value.unary.operand);
    }
    return false;
}

static bool parse_zero_aggregate_initializer_contents(MinicParser *parser,
                                                      MinicSourcePosition begin,
                                                      MinicSourceSpan *initializer_span) {
    if (parser == NULL || initializer_span == NULL) {
        return false;
    }
    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            initializer_span->begin = begin;
            initializer_span->end = parser->current.span.end;
            return minic_parser_advance(parser);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            MinicSourcePosition nested_begin;
            MinicSourceSpan nested_span;

            nested_begin = parser->current.span.begin;
            if (!minic_parser_advance(parser) ||
                !parse_zero_aggregate_initializer_contents(parser, nested_begin, &nested_span)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !aggregate_expression_is_zero_constant(parser->program, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "only all-zero aggregate initializers are supported");
                }
                return false;
            }
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in aggregate initializer");
            return false;
        }
    }
}

static bool parse_zero_aggregate_initializer(MinicParser *parser,
                                             MinicSourceSpan *initializer_span) {
    MinicSourcePosition begin;

    if (parser == NULL || initializer_span == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected aggregate zero initializer");
        return false;
    }
    begin = parser->current.span.begin;
    return minic_parser_advance(parser) &&
           parse_zero_aggregate_initializer_contents(parser, begin, initializer_span);
}

static bool add_zero_assignment_to_lvalue(MinicParser *parser,
                                          MinicExpressionId target_id,
                                          MinicSourceSpan initializer_span) {
    const MinicExpression *target;
    MinicExpression zero;
    MinicExpressionId value_id;
    MinicStatement statement;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "invalid aggregate zero target");
        return false;
    }
    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    if (!minic_parser_add_expression(parser, &zero, &value_id) ||
        !apply_assignment_conversion(parser, target->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, target->type, value_id)) {
        minic_parser_error(parser, "aggregate zero initializer does not match member type");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = initializer_span;
    statement.target_expression = target_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
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
    size_t field_index;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "aggregate zero initializer requires a record lvalue");
        return false;
    }
    record = minic_c0_program_record(parser->program, base->type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "aggregate zero initializer requires a complete record");
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base->span;
    if (!minic_type_pointer_to(base->type, &address.type)) {
        minic_parser_error(parser, "record initializer address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }

    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicExpression member;
        MinicExpressionId member_id;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U) {
            minic_parser_error(parser,
                               "record array members in aggregate initialization are unsupported");
            return false;
        }
        (void)memset(&member, 0, sizeof(member));
        member.kind = MINIC_EXPRESSION_MEMBER;
        member.span = initializer_span;
        member.type = field->type;
        member.value_category = MINIC_VALUE_LVALUE;
        member.value.member.base = address_id;
        member.value.member.record_id = base->type.record_id;
        member.value.member.field_index = field_index;
        if (!minic_parser_add_expression(parser, &member, &member_id)) {
            return false;
        }
        if (minic_type_is_record(field->type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
    }
    return true;
}

static bool add_record_copy_assignments(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span);

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
    if (parser->current.kind != MINIC_TOKEN_DOT) {
        return parse_zero_aggregate_initializer_contents(
                   parser, initializer_span.begin, &initializer_span) &&
               add_zero_initialized_record_lvalue(parser, target_id, initializer_span);
    }
    if (!add_zero_initialized_record_lvalue(parser, target_id, zero_span)) {
        return false;
    }

    for (;;) {
        MinicExpressionId member_id;
        MinicExpressionId value_id;
        const MinicExpression *member;
        const MinicExpression *value;
        MinicStatement statement;

        if (parser->current.kind != MINIC_TOKEN_DOT ||
            !minic_parser_parse_direct_member(parser, target_id, &member_id) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after record designator") ||
            !minic_parser_parse_expression(parser, &value_id, 0U)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "expected designated record initializer");
            }
            return false;
        }
        member = minic_c0_program_expression(parser->program, member_id);
        if (member == NULL || member->value_category != MINIC_VALUE_LVALUE ||
            !apply_assignment_conversion(parser, member->type, &value_id) ||
            !minic_c0_assignment_compatible(parser->program, member->type, value_id)) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
        value = minic_c0_program_expression(parser->program, value_id);
        if (value == NULL) {
            minic_parser_error(parser, "invalid record designated initializer value");
            return false;
        }

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = member->span.begin;
        statement.span.end = value->span.end;
        statement.target_expression = member_id;
        statement.expression = value_id;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                initializer_span.end = parser->current.span.end;
                return minic_parser_advance(parser);
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in designated record initializer");
            return false;
        }
        initializer_span.end = parser->current.span.end;
        return minic_parser_advance(parser);
    }
}

static bool consume_local_object_attribute(MinicParser *parser,
                                           const MinicParsedAttribute *attribute,
                                           void *context) {
    const MinicAttributeDescriptor *descriptor;

    (void)context;
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU attribute on local object");
        return false;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "GNU attribute is not valid on a local object");
        return false;
    }
    if (attribute->has_arguments ||
        descriptor->semantic_class != MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        minic_parser_error(parser, "local object attribute semantics are not supported yet");
        return false;
    }
    return true;
}

static bool parse_local_object_attributes(MinicParser *parser) {
    return minic_parser_parse_gnu_attribute_lists(parser, consume_local_object_attribute, NULL);
}

static bool
parse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {
    MinicLocal local;
    MinicLocalId local_id;
    MinicType declared_type;

    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected local name");
        return false;
    }

    local.name_span = parser->current.span;
    local.type = declared_type;
    local.element_count = 1U;
    local.storage_offset = 0U;
    local.is_array = false;
    local.is_register_storage = is_register_storage;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &local.element_count)) {
            return false;
        }
        local.is_array = true;
    }
    if (!parse_local_object_attributes(parser)) {
        return false;
    }
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "out of memory while adding local");
        return false;
    }
    if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        MinicStatement statement;
        const MinicExpression *initializer;

        if (local.element_count != 1U) {
            return parse_local_array_zero_initializer(parser, local_id, local.name_span);
        }
        if (minic_type_is_record(local.type)) {
            MinicExpressionId target_id;

            if (!add_local_lvalue_expression(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACE) {
                return minic_parser_parse_runtime_record_initializer(parser, target_id);
            } else {
                MinicExpressionId source_id;
                const MinicExpression *source;

                if (!minic_parser_parse_expression(parser, &source_id, 0U)) {
                    return false;
                }
                source = minic_c0_program_expression(parser->program, source_id);
                if (source == NULL ||
                    !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
                    !minic_type_is_record(source->type) ||
                    source->type.record_id != local.type.record_id) {
                    minic_parser_error(
                        parser, "record local initializer requires a matching record copy source");
                    return false;
                }
                return add_record_copy_assignments(parser, target_id, source_id, source->span);
            }
        }
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = local.name_span.begin;
        if (!add_local_lvalue_expression(
                parser, local_id, local.name_span, &statement.target_expression) ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !apply_assignment_conversion(parser, local.type, &statement.expression)) {
            return false;
        }
        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "initializer type does not match local type");
            return false;
        }
        statement.span.end = initializer->span.end;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }
    return true;
}

static bool current_identifier_is_auto_type(const MinicParser *parser) {
    return parser != NULL && parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           minic_parser_span_length(parser->current.span) == 11U &&
           memcmp(parser->source + parser->current.span.begin.offset, "__auto_type", 11U) == 0;
}

static bool parse_auto_type_local_declaration(MinicParser *parser) {
    const MinicExpression *initializer;
    MinicExpressionId initializer_id;
    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicSourcePosition begin;

    if (!current_identifier_is_auto_type(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declarator must be an identifier");
        }
        return false;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span = parser->current.span;
    local.element_count = 1U;
    local.storage_offset = 0U;
    local.is_array = false;
    local.is_register_storage = false;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_EQUAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declaration requires an initializer");
        }
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &initializer_id, 0U)) {
        return false;
    }
    initializer = minic_c0_program_expression(parser->program, initializer_id);
    if (initializer == NULL || minic_type_is_void(initializer->type) ||
        !minic_parser_require_complete_object_type(
            parser,
            initializer->type,
            "GNU __auto_type initializer must determine a complete object type")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "invalid GNU __auto_type initializer type");
        }
        return false;
    }
    local.type = initializer->type;

    /* GNU __auto_type deliberately keeps the new name out of scope while the
       initializer is parsed.  Bind it only after the initializer type is known. */
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !minic_parser_bind_local(parser, local.name_span, local_id) ||
        !add_local_lvalue_expression(parser, local_id, local.name_span, &target_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize GNU __auto_type local");
        }
        return false;
    }

    if (minic_type_is_record(local.type)) {
        if (!minic_c0_record_value_is_copy_source(parser->program, initializer_id) ||
            !add_record_copy_assignments(parser, target_id, initializer_id, initializer->span)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(
                    parser,
                    "GNU __auto_type record initializer requires a supported record copy source");
            }
            return false;
        }
    } else {
        MinicStatement statement;

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = begin;
        statement.span.end = initializer->span.end;
        statement.target_expression = target_id;
        statement.expression = initializer_id;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_assignment_compatible(parser->program, local.type, initializer_id) ||
            !minic_parser_add_statement(parser, &statement)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize GNU __auto_type local");
            }
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU __auto_type declaration");
}

static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;
    bool is_register_storage;

    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        if (!parse_local_declarator(parser, base_type, is_register_storage)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
}

static bool parse_block_scope_extern_function_declaration(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType parameter_types[16];
    MinicType return_type;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    size_t parameter_count;
    bool is_variadic;

    parameter_count = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_parse_gnu_prefix_function_attributes(parser, false, false) ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &return_type) ||
        !minic_parser_parse_gnu_function_attributes(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected function name in block-scope extern declarator");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_RPAREN,
                                 "expected ')' after block-scope extern function name")) {
            return false;
        }
    } else {
        minic_parser_error(parser, "expected block-scope extern function name");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !minic_parser_parse_gnu_function_attributes(parser)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(
            parser, "block-scope extern declaration must declare a function and end with ';'");
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic)) {
            minic_parser_error(parser, "conflicting block-scope extern function declaration");
            return false;
        }
    } else if (!minic_c0_program_add_function(parser->program,
                                              parser->source + name_span.begin.offset,
                                              minic_parser_span_length(name_span),
                                              parser->program->local_count,
                                              0U,
                                              MINIC_BLOCK_INVALID,
                                              &function_id) ||
               !minic_c0_program_set_function_signature(
                   parser->program, function_id, return_type, parameter_types, parameter_count) ||
               !minic_c0_program_set_function_internal(parser->program, function_id, false) ||
               !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
        minic_parser_error(parser, "out of memory while declaring block-scope extern function");
        return false;
    }

    return minic_parser_advance(parser);
}

static bool
parse_static_local_integer_constant(MinicParser *parser, const char *range_message, int *value) {
    int64_t parsed;

    if (parser == NULL || range_message == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "%s", range_message);
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_inferred_static_local_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span) {
    const MinicArrayType *literal_array;
    MinicGlobalObjectId object_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType object_type;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_type_is_char_integer(element_type)) {
            minic_parser_error(parser,
                               "string initializer requires a character static local array");
            return false;
        }
        if (!minic_parser_create_string_literal_object(
                parser, &object_id, &literal_type, &literal_span)) {
            return false;
        }
        literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
        if (literal_array == NULL || !minic_type_is_array(literal_type) ||
            !minic_c0_program_add_array_type(
                parser->program, element_type, literal_array->element_count, &object_type)) {
            minic_parser_error(parser, "cannot infer static local string array type");
            return false;
        }

        /* The literal helper owns decoding and byte initialization. Re-type that internal
           object to the declaration's qualified char element type, then bind the source-level
           static-local name to the same storage object. */
        parser->program->global_objects[object_id].type = object_type;
        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);
        return minic_parser_bind_static_local(parser, name_span, object_id);
    }

    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        char symbol_name[96];
        size_t initializer_count;
        int symbol_length;
        bool is_pointer_array;

        is_pointer_array = minic_type_is_pointer(element_type);
        if (!minic_type_is_integer(element_type) && !is_pointer_array) {
            minic_parser_error(
                parser,
                "brace-initialized inferred static array requires integer or pointer elements");
            return false;
        }
        symbol_length = snprintf(symbol_name,
                                 sizeof(symbol_name),
                                 "__minic_static_local_%zu_%zu",
                                 (size_t)parser->current_function,
                                 parser->program->global_object_count);
        if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type) ||
            !minic_c0_program_add_global_object(parser->program,
                                                symbol_name,
                                                (size_t)symbol_length,
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            (is_pointer_array &&
             !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) ||
            !minic_parser_advance(parser)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin inferred static local array");
            }
            return false;
        }

        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            if (is_pointer_array) {
                MinicGlobalObjectId target_id;
                bool has_relocation;

                target_id = MINIC_GLOBAL_OBJECT_INVALID;
                has_relocation = false;
                if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                    MinicSourceSpan string_span;
                    MinicType string_type;
                    MinicType source_pointer_type;
                    const MinicArrayType *string_array;

                    if (!minic_parser_create_string_literal_object(
                            parser, &target_id, &string_type, &string_span)) {
                        return false;
                    }
                    string_array =
                        minic_c0_program_array_type(parser->program, string_type.array_type_id);
                    if (string_array == NULL || !minic_type_is_array(string_type) ||
                        !minic_type_pointer_to(string_array->element_type, &source_pointer_type) ||
                        !minic_type_assignment_compatible(element_type, source_pointer_type)) {
                        minic_parser_error(
                            parser, "static local pointer array string initializer type mismatch");
                        return false;
                    }
                    (void)string_span;
                    has_relocation = true;
                } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
                    const MinicGlobalObject *target;
                    MinicType source_pointer_type;

                    target_id = minic_parser_find_global_object(parser, parser->current.span);
                    target = target_id == MINIC_GLOBAL_OBJECT_INVALID
                                 ? NULL
                                 : minic_c0_program_global_object(parser->program, target_id);
                    if (target == NULL) {
                        minic_parser_error(
                            parser,
                            "static local pointer array initializer requires a known object");
                        return false;
                    }
                    if (minic_type_is_array(target->type)) {
                        const MinicArrayType *target_array;

                        target_array = minic_c0_program_array_type(parser->program,
                                                                   target->type.array_type_id);
                        if (target_array == NULL ||
                            !minic_type_pointer_to(target_array->element_type,
                                                   &source_pointer_type)) {
                            minic_parser_error(
                                parser, "cannot decay static pointer array initializer object");
                            return false;
                        }
                    } else if (!minic_type_pointer_to(target->type, &source_pointer_type)) {
                        minic_parser_error(
                            parser,
                            "cannot take address of static pointer array initializer object");
                        return false;
                    }
                    if (!minic_type_assignment_compatible(element_type, source_pointer_type) ||
                        !minic_parser_advance(parser)) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(
                                parser,
                                "static local pointer array object initializer type mismatch");
                        }
                        return false;
                    }
                    has_relocation = true;
                } else {
                    int64_t parsed;

                    if (!minic_parser_parse_integer_constant_expression(parser, &parsed) ||
                        parsed != 0) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(
                                parser,
                                "static local pointer array scalar initializer must be null");
                        }
                        return false;
                    }
                }
                if (has_relocation &&
                    !minic_c0_global_object_add_object_relocation(
                        parser->program, object_id, initializer_count, target_id)) {
                    minic_parser_error(parser,
                                       "cannot record static local pointer array relocation");
                    return false;
                }
            } else {
                int value;

                if (!parse_static_local_integer_constant(
                        parser,
                        "static local array initializer is out of supported integer range",
                        &value) ||
                    !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "static local array requires an integer constant expression");
                    }
                    return false;
                }
            }
            initializer_count += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in static local array initializer");
                return false;
            }
        }
        if (initializer_count == 0U ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after static local array initializer") ||
            !minic_c0_program_complete_array_type(
                parser->program, object_type, initializer_count) ||
            !minic_parser_bind_static_local(parser, name_span, object_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot finalize inferred static local array");
            }
            return false;
        }
        return true;
    }

    minic_parser_error(parser,
                       "inferred static local array requires a string or brace initializer");
    return false;
}

static bool parse_static_local_record_initializer(MinicParser *parser,
                                                  MinicType declared_type,
                                                  MinicSourceSpan name_span) {
    char symbol_name[96];
    const MinicRecord *record;
    MinicGlobalObjectId object_id;
    size_t field_index;
    int symbol_length;

    record = minic_c0_program_record(parser->program, declared_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union) {
        minic_parser_error(parser,
                           "static local record initializer requires a complete struct type");
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local record symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LBRACE, "expected '{' in static record initializer")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static local record initializer");
        }
        return false;
    }

    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        int value;

        if (field_index >= record->field_count) {
            minic_parser_error(parser, "too many static local record initializers");
            return false;
        }
        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported static local record field initializer");
            return false;
        }

        value = 0;
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            MinicSourceSpan initializer_span;

            if (!minic_type_is_record(field->type) ||
                !parse_zero_aggregate_initializer(parser, &initializer_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "nested static record initializer must be all zero");
                }
                return false;
            }
        } else {
            if (!minic_type_is_integer(field->type) ||
                !parse_static_local_integer_constant(
                    parser,
                    "static record field constant is out of supported integer range",
                    &value)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "static record field requires an integer constant expression");
                }
                return false;
            }
        }
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "cannot record static local record initializer");
            return false;
        }
        field_index += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static record initializer");
            return false;
        }
    }
    while (field_index < record->field_count) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            minic_parser_error(parser, "cannot zero-fill static local record initializer");
            return false;
        }
        field_index += 1U;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static record initializer") ||
        !minic_parser_bind_static_local(parser, name_span, object_id)) {
        return false;
    }
    return true;
}

static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,
                                                         MinicType declared_type,
                                                         MinicSourceSpan name_span) {
    char symbol_name[96];
    MinicGlobalObjectId object_id;
    int symbol_length;

    if (parser == NULL ||
        !minic_parser_require_complete_object_type(
            parser,
            declared_type,
            "static local object without an initializer requires a complete object type")) {
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build implicit-zero static local symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_parser_bind_static_local(parser, name_span, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create implicit-zero static local storage");
        }
        return false;
    }
    return true;
}

static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {
    char symbol_name[96];
    MinicSourceSpan name_span;
    MinicType declared_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t bounds[8];
    size_t bound_count;
    size_t index;
    int symbol_length;

    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected static local name");
        return false;
    }
    name_span = parser->current.span;
    if (minic_parser_name_bound_in_current_scope(parser, name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    bound_count = 0U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_inferred_static_local_array(parser, declared_type, name_span);
        }
    }
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }
    if (bound_count == 0U) {
        char scalar_symbol_name[96];
        MinicGlobalObjectId scalar_object_id;
        int scalar_value;
        int scalar_symbol_length;

        if (parser->current.kind != MINIC_TOKEN_EQUAL) {
            return add_implicitly_zero_initialized_static_local(parser, declared_type, name_span);
        }
        if (minic_type_is_record(declared_type)) {
            return parse_static_local_record_initializer(parser, declared_type, name_span);
        }
        if (!minic_type_is_integer(declared_type) && !minic_type_is_pointer(declared_type)) {
            minic_parser_error(parser,
                               "static local scalar currently requires an integer or pointer type");
            return false;
        }

        scalar_symbol_length = snprintf(scalar_symbol_name,
                                        sizeof(scalar_symbol_name),
                                        "__minic_static_local_%zu_%zu",
                                        (size_t)parser->current_function,
                                        parser->program->global_object_count);
        if (scalar_symbol_length <= 0 ||
            (size_t)scalar_symbol_length >= sizeof(scalar_symbol_name)) {
            minic_parser_error(parser, "cannot build static local scalar symbol name");
            return false;
        }
        if (!minic_c0_program_add_global_object(parser->program,
                                                scalar_symbol_name,
                                                (size_t)scalar_symbol_length,
                                                declared_type,
                                                true,
                                                minic_type_is_const(declared_type),
                                                &scalar_object_id) ||
            !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static scalar")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin static local scalar initializer");
            }
            return false;
        }

        if (minic_type_is_pointer(declared_type)) {
            if (!minic_parser_parse_zero_pointer_constant(parser) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id) ||
                !minic_parser_bind_static_local(parser, name_span, scalar_object_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot finalize static local null pointer storage");
                }
                return false;
            }
            return true;
        }

        if (!parse_static_local_integer_constant(
                parser, "static local integer constant is out of supported range", &scalar_value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static local integer requires an integer constant expression");
            }
            return false;
        }
        if ((scalar_value == 0 &&
             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||
            (scalar_value != 0 && !minic_c0_global_object_add_initializer(
                                      parser->program, scalar_object_id, scalar_value)) ||
            !minic_parser_bind_static_local(parser, name_span, scalar_object_id)) {
            minic_parser_error(parser, "cannot finalize static local integer storage");
            return false;
        }
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL &&
        (bound_count != 1U || !minic_type_is_integer(declared_type))) {
        minic_parser_error(
            parser, "initialized static local array currently requires one integer dimension");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "static local array requires a complete element type")) {
        return false;
    }

    object_type = declared_type;
    for (index = bound_count; index > 0U; --index) {
        if (!minic_c0_program_add_array_type(
                parser->program, object_type, bounds[index - 1U], &object_type)) {
            minic_parser_error(parser, "out of memory while building static local array type");
            return false;
        }
    }

    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            object_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add static local array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        size_t initializer_count;

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in static local array initializer")) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed;

            if (!minic_parser_parse_integer_constant_expression(parser, &parsed)) {
                return false;
            }
            if (parsed < INT_MIN || parsed > INT_MAX) {
                minic_parser_error(
                    parser, "static local integer array initializer is out of supported range");
                return false;
            }
            if (initializer_count >= bounds[0]) {
                minic_parser_error(parser, "too many static local integer array initializers");
                return false;
            }
            if (!minic_c0_global_object_add_initializer(parser->program, object_id, (int)parsed)) {
                minic_parser_error(parser, "cannot record static local integer array initializer");
                return false;
            }
            initializer_count += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in static local array initializer");
                return false;
            }
        }
        if (initializer_count == 0U ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after static local array initializer")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static local integer array requires at least one initializer");
            }
            return false;
        }
    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot zero-initialize static local array object");
        return false;
    }
    return minic_parser_bind_static_local(parser, name_span, object_id);
}

static bool parse_static_local_declaration(MinicParser *parser) {
    MinicType base_type;

    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (minic_type_is_void(base_type)) {
        minic_parser_error(parser, "static local object cannot have void type");
        return false;
    }

    for (;;) {
        if (!parse_static_local_array_declarator(parser, base_type)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
}

static bool add_record_copy_assignments(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    MinicStatement statement;

    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id || minic_type_is_const(target->type)) {
        minic_parser_error(parser, "record assignment requires a matching record copy source");
        return false;
    }
    record = minic_c0_program_record(parser->program, target->type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "record assignment requires a complete record");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RECORD_COPY;
    statement.span = span;
    statement.target_expression = target_id;
    statement.expression = source_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool assignment_chain_expression_is_stable(const MinicParser *parser,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (parser == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        return local != NULL && !local->is_array;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        return assignment_chain_expression_is_stable(parser, expression->value.member.base);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return assignment_chain_expression_is_stable(parser, expression->value.unary.operand);
    }
    return false;
}

static bool assignment_chain_target_is_stable(const MinicParser *parser,
                                              const MinicExpression *target) {
    if (parser == NULL || target == NULL || !expression_is_modifiable_lvalue(target) ||
        minic_type_is_record(target->type)) {
        return false;
    }
    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, target->value.local_id);
        return local != NULL && !local->is_array;
    }
    if (target->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return true;
    }
    return target->kind == MINIC_EXPRESSION_MEMBER &&
           assignment_chain_expression_is_stable(parser, target->value.member.base);
}

static bool parse_stable_assignment_chain_value(MinicParser *parser,
                                                MinicExpressionId target_id,
                                                MinicExpressionId *value_id) {
    const MinicExpression *target;
    const MinicExpression *assigned_expression;
    MinicExpression read;
    MinicExpressionId right_id;
    MinicStatement assignment;
    MinicSourceSpan target_span;
    MinicType target_type;

    if (parser == NULL || value_id == NULL || parser->current.kind != MINIC_TOKEN_EQUAL) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, target_id);
    if (!assignment_chain_target_is_stable(parser, target)) {
        minic_parser_error(parser,
                           "assignment expression currently requires a stable scalar lvalue");
        return false;
    }
    target_span = target->span;
    target_type = target->type;

    if (!minic_parser_advance(parser) || !minic_parser_parse_expression(parser, &right_id, 0U)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!parse_stable_assignment_chain_value(parser, right_id, &right_id)) {
            return false;
        }
    }
    if (!apply_assignment_conversion(parser, target_type, &right_id)) {
        return false;
    }
    assigned_expression = minic_c0_program_expression(parser->program, right_id);
    if (assigned_expression == NULL ||
        !minic_c0_assignment_compatible(parser->program, target_type, right_id)) {
        minic_parser_error(parser, "assignment expression type does not match target type");
        return false;
    }

    (void)memset(&assignment, 0, sizeof(assignment));
    assignment.kind = MINIC_STATEMENT_ASSIGN;
    assignment.span.begin = target_span.begin;
    assignment.span.end = assigned_expression->span.end;
    assignment.target_expression = target_id;
    assignment.expression = right_id;
    assignment.target_statement = MINIC_STATEMENT_INVALID;
    assignment.then_block = MINIC_BLOCK_INVALID;
    assignment.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &assignment)) {
        return false;
    }

    (void)memset(&read, 0, sizeof(read));
    read.kind = MINIC_EXPRESSION_LVALUE_READ;
    read.span = target_span;
    read.type = target_type;
    read.value_category = MINIC_VALUE_RVALUE;
    read.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &read, value_id);
}

static bool parse_expression_or_assignment_statement(MinicParser *parser,
                                                     bool allow_expression_statement) {
    MinicStatement statement;
    const MinicExpression *first_expression;
    MinicType first_type;
    MinicTokenKind assignment_token;

    (void)memset(&statement, 0, sizeof(statement));
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    first_expression = minic_c0_program_expression(parser->program, statement.expression);
    if (first_expression == NULL) {
        minic_parser_error(parser, "invalid statement expression");
        return false;
    }
    first_type = first_expression->type;
    assignment_token = parser->current.kind;

    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
        assignment_token != MINIC_TOKEN_PLUS_EQUAL && assignment_token != MINIC_TOKEN_MINUS_EQUAL &&
        assignment_token != MINIC_TOKEN_STAR_EQUAL &&
        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&
        assignment_token != MINIC_TOKEN_PIPE_EQUAL &&
        assignment_token != MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        if (!allow_expression_statement && first_expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span.end = first_expression->span.end;
        return minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after expression") &&
               minic_parser_add_statement(parser, &statement);
    }

    statement.kind = assignment_token == MINIC_TOKEN_CARET_EQUAL ? MINIC_STATEMENT_XOR_ASSIGN
                                                                 : MINIC_STATEMENT_ASSIGN;
    statement.target_expression = statement.expression;
    statement.expression = MINIC_EXPRESSION_INVALID;
    if (!expression_is_modifiable_lvalue(first_expression)) {
        minic_parser_error(parser, "assignment target must be a modifiable lvalue");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    if (assignment_token == MINIC_TOKEN_EQUAL && parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!parse_stable_assignment_chain_value(
                parser, statement.expression, &statement.expression)) {
            return false;
        }
    }
    if (assignment_token == MINIC_TOKEN_AMPERSAND_EQUAL ||
        assignment_token == MINIC_TOKEN_PIPE_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression bitwise;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser, "compound bitwise assignment requires integer operands");
            return false;
        }
        (void)memset(&bitwise, 0, sizeof(bitwise));
        bitwise.kind = MINIC_EXPRESSION_BINARY;
        bitwise.span.begin = statement.span.begin;
        bitwise.span.end = right_expression->span.end;
        bitwise.type = common_type;
        bitwise.value_category = MINIC_VALUE_RVALUE;
        bitwise.value.binary.operator_kind = assignment_token == MINIC_TOKEN_AMPERSAND_EQUAL
                                                 ? MINIC_BINARY_BITWISE_AND
                                                 : MINIC_BINARY_BITWISE_OR;
        bitwise.value.binary.left = statement.target_expression;
        bitwise.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &bitwise, &statement.expression)) {
            return false;
        }
    }
    if (assignment_token == MINIC_TOKEN_EQUAL && minic_type_is_record(first_type)) {
        const MinicExpression *source;

        source = minic_c0_program_expression(parser->program, statement.expression);
        if (source == NULL || !minic_type_is_record(source->type) ||
            source->type.record_id != first_type.record_id ||
            !add_record_copy_assignments(
                parser, statement.target_expression, statement.expression, source->span)) {
            return false;
        }
        return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
    }
    if (assignment_token == MINIC_TOKEN_PLUS_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression addition;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(
                parser,
                "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
        if (minic_type_is_pointer(first_type)) {
            MinicType pointee_type;

            if (!minic_type_pointee(first_type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
            common_type = first_type;
        } else if (!minic_type_is_integer(first_type) ||
                   !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(
                parser,
                "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
        (void)memset(&addition, 0, sizeof(addition));
        addition.kind = MINIC_EXPRESSION_BINARY;
        addition.span.begin = statement.span.begin;
        addition.span.end = right_expression->span.end;
        addition.type = common_type;
        addition.value_category = MINIC_VALUE_RVALUE;
        addition.value.binary.operator_kind = MINIC_BINARY_ADD;
        addition.value.binary.left = statement.target_expression;
        addition.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &addition, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL ||
               assignment_token == MINIC_TOKEN_STAR_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression arithmetic;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires pointer/integer or "
                                     "integer operands");
            return false;
        }
        if (assignment_token == MINIC_TOKEN_MINUS_EQUAL && minic_type_is_pointer(first_type)) {
            MinicType pointee_type;

            if (!minic_type_pointee(first_type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
            common_type = first_type;
        } else if (!minic_type_is_integer(first_type) ||
                   !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires pointer/integer or "
                                     "integer operands");
            return false;
        }
        (void)memset(&arithmetic, 0, sizeof(arithmetic));
        arithmetic.kind = MINIC_EXPRESSION_BINARY;
        arithmetic.span.begin = statement.span.begin;
        arithmetic.span.end = right_expression->span.end;
        arithmetic.type = common_type;
        arithmetic.value_category = MINIC_VALUE_RVALUE;
        arithmetic.value.binary.operator_kind = assignment_token == MINIC_TOKEN_STAR_EQUAL
                                                    ? MINIC_BINARY_MULTIPLY
                                                    : MINIC_BINARY_SUBTRACT;
        arithmetic.value.binary.left = statement.target_expression;
        arithmetic.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &arithmetic, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression shift;
        MinicExpressionId right_id;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser, "compound right shift assignment requires integer operands");
            return false;
        }
        (void)memset(&shift, 0, sizeof(shift));
        shift.kind = MINIC_EXPRESSION_BINARY;
        shift.span.begin = statement.span.begin;
        shift.span.end = right_expression->span.end;
        shift.value_category = MINIC_VALUE_RVALUE;
        shift.value.binary.operator_kind = MINIC_BINARY_SHIFT_RIGHT;
        shift.value.binary.left = statement.target_expression;
        shift.value.binary.right = right_id;
        if (!minic_type_integer_common(first_type, first_type, &shift.type) ||
            !minic_parser_add_expression(parser, &shift, &statement.expression)) {
            return false;
        }
    }
    if (statement.kind == MINIC_STATEMENT_ASSIGN &&
        !apply_assignment_conversion(parser, first_type, &statement.expression)) {
        return false;
    }
    {
        const MinicExpression *assigned_expression;

        assigned_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (statement.kind == MINIC_STATEMENT_XOR_ASSIGN) {
            MinicType common_type;

            if (assigned_expression == NULL || !minic_type_is_integer(first_type) ||
                !minic_type_is_integer(assigned_expression->type) ||
                !minic_type_integer_common(first_type, assigned_expression->type, &common_type)) {
                minic_parser_error(parser, "compound XOR assignment requires integer operands");
                return false;
            }
        } else if (assigned_expression == NULL ||
                   !minic_c0_assignment_compatible(
                       parser->program, first_type, statement.expression)) {
            minic_parser_error(parser, "assignment type does not match target type");
            return false;
        }
        statement.span.end = assigned_expression->span.end;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_compound_statement(MinicParser *parser) {
    bool success;

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected '{'");
        return false;
    }
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }

    success = minic_parser_advance(parser);
    while (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            success = false;
            break;
        }
        success = minic_parser_parse_statement(parser, true);
    }
    if (success) {
        success = minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'");
    }

    minic_parser_end_scope(parser);
    return success;
}

bool minic_parser_parse_statement_expression(MinicParser *parser,
                                             MinicSourcePosition begin,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicBlock *block;
    const MinicExpression *result;
    const MinicStatement *last_statement;
    MinicBlockId block_id;
    MinicBlockId parent_block;
    MinicStatementId last_statement_id;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected '{' in GNU statement expression");
        }
        return false;
    }
    parent_block = parser->current_block;
    if (!minic_c0_program_add_block(parser->program, &block_id) ||
        !minic_parser_begin_scope(parser)) {
        minic_parser_error(parser, "cannot create GNU statement-expression scope");
        return false;
    }
    parser->current_block = block_id;
    success = minic_parser_advance(parser);
    while (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of GNU statement expression");
            success = false;
            break;
        }
        success = minic_parser_parse_statement(parser, true);
    }

    block = block_id < parser->program->block_count ? &parser->program->blocks[block_id] : NULL;
    last_statement = NULL;
    result = NULL;
    last_statement_id = MINIC_STATEMENT_INVALID;
    if (success && block == NULL) {
        minic_parser_error(parser, "invalid GNU statement-expression block");
        success = false;
    }
    if (success && block->statement_count != 0U) {
        last_statement_id = block->statements[block->statement_count - 1U];
        last_statement = minic_c0_program_statement(parser->program, last_statement_id);
        if (last_statement != NULL && last_statement->kind == MINIC_STATEMENT_EXPRESSION &&
            last_statement->expression != MINIC_EXPRESSION_INVALID) {
            result = minic_c0_program_expression(parser->program, last_statement->expression);
            if (result == NULL) {
                minic_parser_error(parser, "invalid GNU statement-expression result");
                success = false;
            }
        }
    }
    if (success) {
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_STATEMENT;
        expression.span.begin = begin;
        expression.span.end = parser->current.span.end;
        expression.type = result == NULL ? minic_type_void() : result->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.statement_expression.block = block_id;
        expression.value.statement_expression.result =
            result == NULL ? MINIC_EXPRESSION_INVALID : last_statement->expression;
        if (result != NULL) {
            block->statement_count -= 1U;
        }
        success = minic_parser_add_expression(parser, &expression, expression_id) &&
                  minic_parser_expect(
                      parser, MINIC_TOKEN_RBRACE, "expected '}' in GNU statement expression");
    }

    parser->current_block = parent_block;
    minic_parser_end_scope(parser);
    return success;
}

static bool parse_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicBlockId parent_block;
    bool success;

    parent_block = parser->current_block;
    if (!minic_c0_program_add_block(parser->program, block_id)) {
        minic_parser_error(parser, "out of memory while adding branch block");
        return false;
    }
    parser->current_block = *block_id;

    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        success = parse_compound_statement(parser);
    } else {
        success = minic_parser_parse_statement(parser, false);
    }

    parser->current_block = parent_block;
    return success;
}

static bool parse_loop_branch(MinicParser *parser, MinicBlockId *block_id) {
    bool success;

    parser->loop_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->loop_depth -= 1U;
    return success;
}

static bool parse_switch_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicParserSwitchContext *context;
    bool success;

    if (parser->switch_depth >= MINIC_PARSER_MAX_SWITCH_DEPTH) {
        minic_parser_error(parser, "switch nesting exceeds implementation limit");
        return false;
    }
    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));
    parser->switch_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->switch_depth -= 1U;
    return success;
}

static MinicParserSwitchContext *current_switch_context(MinicParser *parser) {
    if (parser->switch_depth == 0U) {
        return NULL;
    }
    return &parser->switch_contexts[parser->switch_depth - 1U];
}

static bool expression_is_integer_condition(MinicParser *parser, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        minic_parser_error(parser, "condition requires an integer or pointer expression");
        return false;
    }
    return true;
}

static bool expression_is_switch_selector(MinicParser *parser, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        minic_parser_error(parser, "switch selector requires an integer expression");
        return false;
    }
    return true;
}

static bool parse_if(MinicParser *parser) {
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_IF;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_branch(parser, &statement.then_block)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_KW_ELSE) {
        if (!minic_parser_advance(parser) || !parse_branch(parser, &statement.else_block)) {
            return false;
        }
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_internal_continue_label(MinicParser *parser,
                                        MinicSourceSpan span,
                                        MinicStatementId *statement_id) {
    MinicStatement label;

    if (statement_id == NULL) {
        return false;
    }
    (void)memset(&label, 0, sizeof(label));
    label.kind = MINIC_STATEMENT_LABEL;
    label.span = span;
    label.target_expression = MINIC_EXPRESSION_INVALID;
    label.expression = MINIC_EXPRESSION_INVALID;
    label.target_statement = MINIC_STATEMENT_INVALID;
    label.then_block = MINIC_BLOCK_INVALID;
    label.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(parser->program, &label, statement_id)) {
        minic_parser_error(parser, "out of memory while adding continue target");
        return false;
    }
    return true;
}

static bool parse_continue(MinicParser *parser) {
    MinicStatement statement;

    if (parser->loop_depth == 0U || parser->continue_target_statement == MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "continue statement requires an enclosing loop");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_GOTO;
    statement.span = parser->current.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = parser->continue_target_statement;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after continue") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_while(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId continue_label;
    MinicStatementId previous_continue_target;
    MinicSourceSpan while_span;
    bool success;

    while_span = parser->current.span;
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = while_span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !add_internal_continue_label(parser, while_span, &continue_label) ||
        !minic_c0_block_add_statement(parser->program, parser->current_block, continue_label)) {
        return false;
    }

    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    if (!success) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_do_while(MinicParser *parser) {
    MinicStatement statement;
    MinicStatement condition_check;
    MinicStatement break_statement;
    MinicExpression loop_true;
    MinicExpression negated_condition;
    MinicStatementId continue_label;
    MinicStatementId condition_check_id;
    MinicStatementId break_statement_id;
    MinicStatementId previous_continue_target;
    MinicBlockId break_block;
    MinicExpressionId condition_id;
    MinicExpressionId negated_condition_id;
    MinicSourceSpan do_span;
    bool success;

    do_span = parser->current.span;
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = do_span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    (void)memset(&loop_true, 0, sizeof(loop_true));
    loop_true.kind = MINIC_EXPRESSION_INTEGER;
    loop_true.span = do_span;
    loop_true.type = minic_type_int();
    loop_true.value_category = MINIC_VALUE_RVALUE;
    loop_true.value.integer_value = 1;
    if (!minic_parser_add_expression(parser, &loop_true, &statement.expression) ||
        !add_internal_continue_label(parser, do_span, &continue_label) ||
        !minic_parser_advance(parser)) {
        return false;
    }

    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    if (!success || parser->current.kind != MINIC_TOKEN_KW_WHILE ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, continue_label)) {
        if (success && parser->current.kind != MINIC_TOKEN_KW_WHILE) {
            minic_parser_error(parser, "expected 'while' after do body");
        }
        return false;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &condition_id) ||
        !expression_is_integer_condition(parser, condition_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after do-while condition") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after do-while")) {
        return false;
    }

    (void)memset(&negated_condition, 0, sizeof(negated_condition));
    negated_condition.kind = MINIC_EXPRESSION_UNARY;
    negated_condition.span = do_span;
    negated_condition.type = minic_type_int();
    negated_condition.value_category = MINIC_VALUE_RVALUE;
    negated_condition.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
    negated_condition.value.unary.operand = condition_id;
    if (!minic_parser_add_expression(parser, &negated_condition, &negated_condition_id)) {
        return false;
    }

    (void)memset(&break_statement, 0, sizeof(break_statement));
    break_statement.kind = MINIC_STATEMENT_BREAK;
    break_statement.span = do_span;
    break_statement.target_expression = MINIC_EXPRESSION_INVALID;
    break_statement.expression = MINIC_EXPRESSION_INVALID;
    break_statement.target_statement = MINIC_STATEMENT_INVALID;
    break_statement.then_block = MINIC_BLOCK_INVALID;
    break_statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_block(parser->program, &break_block) ||
        !minic_c0_program_add_statement(parser->program, &break_statement, &break_statement_id) ||
        !minic_c0_block_add_statement(parser->program, break_block, break_statement_id)) {
        minic_parser_error(parser, "cannot build do-while exit block");
        return false;
    }

    (void)memset(&condition_check, 0, sizeof(condition_check));
    condition_check.kind = MINIC_STATEMENT_IF;
    condition_check.span = do_span;
    condition_check.target_expression = MINIC_EXPRESSION_INVALID;
    condition_check.expression = negated_condition_id;
    condition_check.target_statement = MINIC_STATEMENT_INVALID;
    condition_check.then_block = break_block;
    condition_check.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(parser->program, &condition_check, &condition_check_id) ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, condition_check_id)) {
        minic_parser_error(parser, "cannot append do-while condition check");
        return false;
    }

    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_switch(MinicParser *parser) {
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_SWITCH;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_switch_selector(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_switch_branch(parser, &statement.then_block)) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool case_integer_constant_value(const MinicC0Program *program,
                                        MinicExpressionId expression_id,
                                        int64_t *value) {
    const MinicExpression *expression;
    int64_t left;
    int64_t right;

    if (program == NULL || value == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        *value = expression->value.integer_value;
        return true;
    case MINIC_EXPRESSION_UNARY:
        if (!case_integer_constant_value(program, expression->value.unary.operand, &left)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            *value = left;
            return true;
        case MINIC_UNARY_NEGATE:
            *value = -left;
            return true;
        case MINIC_UNARY_LOGICAL_NOT:
            *value = left == 0;
            return true;
        default:
            return false;
        }
    case MINIC_EXPRESSION_BINARY:
        if (!case_integer_constant_value(program, expression->value.binary.left, &left) ||
            !case_integer_constant_value(program, expression->value.binary.right, &right)) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            *value = left + right;
            return true;
        case MINIC_BINARY_SUBTRACT:
            *value = left - right;
            return true;
        case MINIC_BINARY_MULTIPLY:
            *value = left * right;
            return true;
        case MINIC_BINARY_DIVIDE:
            if (right == 0) {
                return false;
            }
            *value = left / right;
            return true;
        case MINIC_BINARY_REMAINDER:
            if (right == 0) {
                return false;
            }
            *value = left % right;
            return true;
        case MINIC_BINARY_SHIFT_LEFT:
            if (right < 0 || right >= 63 || left < 0) {
                return false;
            }
            *value = (int64_t)((uint64_t)left << (unsigned int)right);
            return true;
        case MINIC_BINARY_SHIFT_RIGHT:
            if (right < 0 || right >= 63) {
                return false;
            }
            *value = left >> (unsigned int)right;
            return true;
        case MINIC_BINARY_BITWISE_AND:
            *value = left & right;
            return true;
        case MINIC_BINARY_BITWISE_XOR:
            *value = left ^ right;
            return true;
        case MINIC_BINARY_BITWISE_OR:
            *value = left | right;
            return true;
        case MINIC_BINARY_EQUAL:
            *value = left == right;
            return true;
        case MINIC_BINARY_NOT_EQUAL:
            *value = left != right;
            return true;
        case MINIC_BINARY_LESS:
            *value = left < right;
            return true;
        case MINIC_BINARY_LESS_EQUAL:
            *value = left <= right;
            return true;
        case MINIC_BINARY_GREATER:
            *value = left > right;
            return true;
        case MINIC_BINARY_GREATER_EQUAL:
            *value = left >= right;
            return true;
        case MINIC_BINARY_LOGICAL_AND:
            *value = left != 0 && right != 0;
            return true;
        case MINIC_BINARY_LOGICAL_OR:
            *value = left != 0 || right != 0;
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

static bool parse_case(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;
    const MinicExpression *lower_constant;
    const MinicExpression *upper_constant;
    MinicExpression folded_constant;
    MinicExpressionId lower_expression_id;
    MinicExpressionId upper_expression_id;
    MinicType constant_type;
    MinicSourceSpan constant_span;
    int64_t lower_value;
    int64_t upper_value;
    int64_t candidate;
    size_t range_count;
    size_t index;
    bool is_range;

    context = current_switch_context(parser);
    if (context == NULL) {
        minic_parser_error(parser, "case label requires an enclosing switch");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_CASE;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &lower_expression_id, 0U)) {
        return false;
    }
    lower_constant = minic_c0_program_expression(parser->program, lower_expression_id);
    if (lower_constant == NULL ||
        !case_integer_constant_value(parser->program, lower_expression_id, &lower_value)) {
        minic_parser_error(parser, "case label currently requires an integer constant expression");
        return false;
    }
    constant_type = lower_constant->type;
    constant_span = lower_constant->span;
    upper_value = lower_value;
    upper_expression_id = MINIC_EXPRESSION_INVALID;
    is_range = parser->current.kind == MINIC_TOKEN_ELLIPSIS;
    if (is_range) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &upper_expression_id, 0U)) {
            return false;
        }
        upper_constant = minic_c0_program_expression(parser->program, upper_expression_id);
        if (upper_constant == NULL ||
            !case_integer_constant_value(parser->program, upper_expression_id, &upper_value)) {
            minic_parser_error(parser,
                               "GNU case range upper bound must be an integer constant expression");
            return false;
        }
        if (upper_value < lower_value) {
            minic_parser_error(parser, "GNU case range upper bound is below lower bound");
            return false;
        }
        constant_span.end = upper_constant->span.end;
    }

    range_count = 0U;
    candidate = lower_value;
    for (;;) {
        if (context->case_count + range_count >= MINIC_PARSER_MAX_SWITCH_CASES) {
            minic_parser_error(parser, "switch case count exceeds implementation limit");
            return false;
        }
        for (index = 0U; index < context->case_count; ++index) {
            if (context->case_values[index] == candidate) {
                minic_parser_error(parser, "duplicate case value");
                return false;
            }
        }
        range_count += 1U;
        if (candidate == upper_value) {
            break;
        }
        candidate += 1;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after case value")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;

    candidate = lower_value;
    for (;;) {
        MinicStatement case_statement;

        (void)memset(&folded_constant, 0, sizeof(folded_constant));
        folded_constant.kind = MINIC_EXPRESSION_INTEGER;
        folded_constant.span = constant_span;
        folded_constant.type = constant_type;
        folded_constant.value_category = MINIC_VALUE_RVALUE;
        folded_constant.value.integer_value = candidate;

        case_statement = statement;
        if (!minic_parser_add_expression(parser, &folded_constant, &case_statement.expression) ||
            !minic_parser_add_statement(parser, &case_statement)) {
            return false;
        }
        context->case_values[context->case_count] = candidate;
        context->case_count += 1U;
        if (candidate == upper_value) {
            break;
        }
        candidate += 1;
    }
    return true;
}

static bool parse_default(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;

    context = current_switch_context(parser);
    if (context == NULL) {
        minic_parser_error(parser, "default label requires an enclosing switch");
        return false;
    }
    if (context->has_default) {
        minic_parser_error(parser, "duplicate default label");
        return false;
    }
    context->has_default = true;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_DEFAULT;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after default")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_for_update(MinicParser *parser, MinicStatementId *statement_id) {
    const MinicExpression *expression;
    MinicStatement statement;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, statement.expression);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid for update expression");
        return false;
    }
    statement.span = expression->span;
    return minic_c0_program_add_statement(parser->program, &statement, statement_id);
}

static bool token_starts_local_declaration(const MinicParser *parser);

static bool parse_for_initializer_expression(MinicParser *parser) {
    MinicStatement statement;
    const MinicExpression *expression;

    if (parser == NULL) {
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, statement.expression);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid for initializer expression");
        return false;
    }
    statement.span.end = expression->span.end;
    return minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after for initializer") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_for(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId update_statement;
    MinicStatementId continue_label;
    MinicStatementId previous_continue_target;
    bool has_update;
    MinicSourceSpan for_span;
    bool success;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    for_span = parser->current.span;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('")) {
        return false;
    }
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (token_starts_local_declaration(parser)) {
        if (!parse_declaration(parser)) {
            return false;
        }
    } else if (!parse_for_initializer_expression(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_full_expression(parser, &statement.expression) ||
               !expression_is_integer_condition(parser, statement.expression) ||
               !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'")) {
        return false;
    }

    has_update = false;
    update_statement = MINIC_STATEMENT_INVALID;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (!parse_for_update(parser, &update_statement)) {
            return false;
        }
        has_update = true;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !add_internal_continue_label(parser, for_span, &continue_label)) {
        return false;
    }
    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    if (!success ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, continue_label)) {
        if (success) {
            minic_parser_error(parser, "cannot append for-loop continue target");
        }
        return false;
    }
    if (has_update &&
        !minic_c0_block_add_statement(parser->program, statement.then_block, update_statement)) {
        minic_parser_error(parser, "cannot append for-loop update");
        return false;
    }
    statement.span.begin = for_span.begin;
    statement.span.end = parser->current.span.begin;
    success = minic_parser_add_statement(parser, &statement);
    minic_parser_end_scope(parser);
    return success;
}

static bool ensure_function_label_context(MinicParser *parser) {
    if (parser->current_function == MINIC_FUNCTION_INVALID) {
        minic_parser_error(parser, "goto/label statement outside a function");
        return false;
    }
    if (!parser->label_context_initialized ||
        parser->label_context_function != parser->current_function) {
        parser->label_context_initialized = true;
        parser->label_context_function = parser->current_function;
        parser->function_statement_begin = parser->program->statement_count;
    }
    return true;
}

static bool identifier_equals(const MinicParser *parser,
                              MinicSourceSpan span,
                              const char *text,
                              size_t text_length) {
    return minic_parser_span_length(span) == text_length &&
           memcmp(parser->source + span.begin.offset, text, text_length) == 0;
}

static bool current_identifier_is_goto(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           identifier_equals(parser, parser->current.span, "goto", 4U);
}

static MinicStatementId find_function_label(MinicParser *parser, MinicSourceSpan name_span) {
    size_t statement_index;

    for (statement_index = parser->function_statement_begin;
         statement_index < parser->program->statement_count;
         ++statement_index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(parser->program, statement_index);
        if (statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&
            !minic_parser_statement_is_local_label(parser, statement_index) &&
            minic_parser_span_equals(parser, statement->span, name_span)) {
            return statement_index;
        }
    }
    return MINIC_STATEMENT_INVALID;
}

MinicStatementId minic_parser_find_label_statement(MinicParser *parser, MinicSourceSpan name_span) {
    MinicStatementId local_label;

    local_label = minic_parser_find_local_label(parser, name_span);
    return local_label != MINIC_STATEMENT_INVALID ? local_label
                                                  : find_function_label(parser, name_span);
}

static bool current_identifier_is_local_label_keyword(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           identifier_equals(parser, parser->current.span, "__label__", 9U);
}

static bool parse_gnu_local_label_declaration(MinicParser *parser) {
    if (!current_identifier_is_local_label_keyword(parser) || !minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        MinicStatement label;
        MinicStatementId statement_id;
        MinicSourceSpan name_span;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected label name after __label__");
            return false;
        }
        name_span = parser->current.span;
        (void)memset(&label, 0, sizeof(label));
        label.kind = MINIC_STATEMENT_LABEL;
        label.span = name_span;
        label.target_expression = MINIC_EXPRESSION_INVALID;
        label.expression = MINIC_EXPRESSION_INVALID;
        label.target_statement = MINIC_STATEMENT_INVALID;
        label.then_block = MINIC_BLOCK_INVALID;
        label.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_program_add_statement(parser->program, &label, &statement_id) ||
            !minic_parser_declare_local_label(parser, name_span, statement_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU local label declaration");
}

static bool parse_goto(MinicParser *parser) {
    MinicStatement statement;
    MinicSourceSpan name_span;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_GOTO;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected label name after goto");
        return false;
    }
    name_span = parser->current.span;
    statement.span = name_span;
    statement.target_statement = minic_parser_find_label_statement(parser, name_span);
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after goto")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

static bool identifier_starts_label(MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicLexer lookahead;
    MinicToken token;

    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER || current_identifier_is_goto(parser)) {
        return false;
    }
    lookahead = parser->lexer;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    return minic_lexer_next(&lookahead, &token, &diagnostic) && token.kind == MINIC_TOKEN_COLON;
}

static void resolve_pending_inline_asm_labels(MinicParser *parser,
                                              MinicSourceSpan name_span,
                                              MinicStatementId label_statement_id) {
    size_t statement_index;
    size_t name_length;
    const char *name;

    if (parser == NULL || label_statement_id == MINIC_STATEMENT_INVALID) {
        return;
    }
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    for (statement_index = parser->function_statement_begin; statement_index < label_statement_id;
         ++statement_index) {
        const MinicStatement *statement;
        MinicInlineAsm *inline_asm;
        size_t label_index;

        statement = minic_c0_program_statement(parser->program, statement_index);
        if (statement == NULL || statement->kind != MINIC_STATEMENT_INLINE_ASM ||
            statement->inline_asm_id >= parser->program->inline_asm_count) {
            continue;
        }
        inline_asm = &parser->program->inline_asms[statement->inline_asm_id];
        for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
            MinicInlineAsmLabel *label;

            label = &inline_asm->labels[label_index];
            if (label->target_statement == MINIC_STATEMENT_INVALID &&
                label->name_length == name_length && memcmp(label->name, name, name_length) == 0) {
                label->target_statement = label_statement_id;
            }
        }
    }
}

static bool parse_label(MinicParser *parser, bool allow_declaration) {
    MinicStatement statement;
    MinicSourceSpan name_span;
    MinicStatementId label_statement_id;
    MinicStatementId local_label_id;
    size_t statement_index;
    bool is_local_label;

    name_span = parser->current.span;
    local_label_id = minic_parser_find_local_label(parser, name_span);
    is_local_label = local_label_id != MINIC_STATEMENT_INVALID;
    if (!is_local_label && find_function_label(parser, name_span) != MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "duplicate label definition");
        return false;
    }

    if (is_local_label) {
        MinicStatement *local_label;

        if (!minic_parser_define_local_label(parser, name_span, &label_statement_id)) {
            return false;
        }
        local_label = &parser->program->statements[label_statement_id];
        local_label->span = name_span;
    } else {
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_LABEL;
        statement.span = name_span;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = MINIC_EXPRESSION_INVALID;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        label_statement_id = parser->program->statement_count;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after label")) {
        return false;
    }
    if (is_local_label) {
        if (!minic_c0_block_add_statement(
                parser->program, parser->current_block, label_statement_id)) {
            minic_parser_error(parser, "cannot materialize GNU local label definition");
            return false;
        }
    } else if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    if (!is_local_label) {
        for (statement_index = parser->function_statement_begin;
             statement_index < label_statement_id;
             ++statement_index) {
            MinicStatement *pending;

            pending = &parser->program->statements[statement_index];
            if (pending->kind == MINIC_STATEMENT_GOTO &&
                pending->target_statement == MINIC_STATEMENT_INVALID &&
                minic_parser_span_equals(parser, pending->span, name_span)) {
                pending->target_statement = label_statement_id;
            }
        }
        resolve_pending_inline_asm_labels(parser, name_span, label_statement_id);
    }

    if (parser->current.kind == MINIC_TOKEN_RBRACE || parser->current.kind == MINIC_TOKEN_EOF) {
        minic_parser_error(parser, "label must be followed by a statement");
        return false;
    }
    return minic_parser_parse_statement(parser, allow_declaration);
}

static bool parse_break(MinicParser *parser) {
    MinicStatement statement;

    if (parser->loop_depth == 0U && parser->switch_depth == 0U) {
        minic_parser_error(parser, "break statement requires an enclosing loop or switch");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_BREAK;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    statement.span.end = parser->current.span.end;
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after break") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_return(MinicParser *parser) {
    const MinicFunction *function;
    MinicStatement statement;

    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL) {
        minic_parser_error(parser, "return statement outside a function");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (minic_type_is_void(function->return_type)) {
        if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser, "void function cannot return a value");
            return false;
        }
        statement.span.end = parser->current.span.end;
    } else {
        const MinicExpression *returned_expression;

        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser, "non-void function requires a return value");
            return false;
        }
        if (!minic_parser_parse_expression(parser, &statement.expression, 0U)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            MinicStatement assignment;
            const MinicExpression *assigned_expression;
            const MinicExpression *target;
            MinicExpressionId target_id;

            target_id = statement.expression;
            target = minic_c0_program_expression(parser->program, target_id);
            if (!expression_is_modifiable_lvalue(target)) {
                minic_parser_error(parser, "assignment target must be a modifiable lvalue");
                return false;
            }
            (void)memset(&assignment, 0, sizeof(assignment));
            assignment.kind = MINIC_STATEMENT_ASSIGN;
            assignment.span.begin = target->span.begin;
            assignment.target_expression = target_id;
            assignment.then_block = MINIC_BLOCK_INVALID;
            assignment.else_block = MINIC_BLOCK_INVALID;
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_expression(parser, &assignment.expression, 0U) ||
                !apply_assignment_conversion(parser, target->type, &assignment.expression)) {
                return false;
            }
            assigned_expression =
                minic_c0_program_expression(parser->program, assignment.expression);
            if (assigned_expression == NULL ||
                !minic_c0_assignment_compatible(
                    parser->program, target->type, assignment.expression)) {
                minic_parser_error(parser, "assignment type does not match target type");
                return false;
            }
            assignment.span.end = assigned_expression->span.end;
            if (!minic_parser_add_statement(parser, &assignment)) {
                return false;
            }
            statement.expression = target_id;
        }
        if (!apply_assignment_conversion(parser, function->return_type, &statement.expression)) {
            return false;
        }
        returned_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (returned_expression == NULL || !minic_c0_assignment_compatible(parser->program,
                                                                           function->return_type,
                                                                           statement.expression)) {
            minic_parser_error(parser, "return expression does not match function return type");
            return false;
        }
        statement.span.end = returned_expression->span.end;
        parser->program->return_expression = statement.expression;
    }

    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_add_default_return(MinicParser *parser) {
    const MinicFunction *function;
    MinicStatement statement;

    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL) {
        minic_parser_error(parser, "internal error: no active function");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = parser->current.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;

    if (minic_type_is_integer(function->return_type)) {
        MinicExpression expression;

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_INTEGER;
        expression.span = parser->current.span;
        expression.type = function->return_type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.integer_value = 0;
        if (!minic_parser_add_expression(parser, &expression, &statement.expression)) {
            return false;
        }
        parser->program->return_expression = statement.expression;
    } else if (!minic_type_is_void(function->return_type)) {
        minic_parser_error(parser, "unsupported implicit return type");
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

static bool inline_asm_identifier_is(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool current_is_gnu_asm(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "asm") || inline_asm_identifier_is(parser, "__asm") ||
           inline_asm_identifier_is(parser, "__asm__");
}

static bool current_is_gnu_goto(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "goto");
}

static bool current_is_gnu_volatile(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
           inline_asm_identifier_is(parser, "__volatile") ||
           inline_asm_identifier_is(parser, "__volatile__");
}

static bool
parse_gnu_inline_asm_operand_name(MinicParser *parser, const char **name, size_t *name_length) {
    if (parser == NULL || name == NULL || name_length == NULL) {
        return false;
    }
    *name = NULL;
    *name_length = 0U;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return true;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected GNU asm operand name after '['");
        return false;
    }
    *name = parser->source + parser->current.span.begin.offset;
    *name_length = minic_parser_span_length(parser->current.span);
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' after GNU asm operand name")) {
        return false;
    }
    return true;
}

static bool parse_gnu_inline_asm_output(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
    MinicInlineAsmOperandAccess access;
    MinicSourceSpan constraint_span;
    const char *name;
    char *constraint;
    size_t constraint_length;
    size_t name_length;

    constraint = NULL;
    constraint_length = 0U;
    name = NULL;
    name_length = 0U;
    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||
        !minic_parser_parse_string_text(
            parser, &constraint, &constraint_length, &constraint_span)) {
        free(constraint);
        return false;
    }
    if (constraint_length == 0U || (constraint[0] != '+' && constraint[0] != '=')) {
        free(constraint);
        minic_parser_error(parser, "GNU asm output constraint must begin with '+' or '='");
        return false;
    }
    access = constraint[0] == '+' ? MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                  : MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before GNU asm output expression") ||
        !minic_parser_parse_expression_no_decay(parser, &operand_id) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm output expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (operand_expression == NULL || operand_expression->value_category != MINIC_VALUE_LVALUE) {
        free(constraint);
        minic_parser_error(parser, "GNU asm output operand requires an lvalue");
        return false;
    }
    if (!minic_c0_program_add_inline_asm_output(parser->program,
                                                inline_asm_id,
                                                name,
                                                name_length,
                                                constraint,
                                                constraint_length,
                                                operand_id,
                                                access)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm output operand");
        return false;
    }
    free(constraint);
    return true;
}

static bool parse_gnu_inline_asm_input(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
    MinicSourceSpan constraint_span;
    const char *name;
    char *constraint;
    size_t constraint_length;
    size_t name_length;

    constraint = NULL;
    constraint_length = 0U;
    name = NULL;
    name_length = 0U;
    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||
        !minic_parser_parse_string_text(
            parser, &constraint, &constraint_length, &constraint_span)) {
        free(constraint);
        return false;
    }
    if (constraint_length == 0U || constraint[0] == '+' || constraint[0] == '=') {
        free(constraint);
        minic_parser_error(parser, "GNU asm input constraint must describe a read-only operand");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before GNU asm input expression") ||
        !minic_parser_parse_expression(parser, &operand_id, 0U) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm input expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (operand_expression == NULL || (!minic_type_is_integer(operand_expression->type) &&
                                       !minic_type_is_pointer(operand_expression->type))) {
        free(constraint);
        minic_parser_error(parser,
                           "GNU asm input operand currently requires an integer or pointer");
        return false;
    }
    if (!minic_c0_program_add_inline_asm_input(parser->program,
                                               inline_asm_id,
                                               name,
                                               name_length,
                                               constraint,
                                               constraint_length,
                                               operand_id)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm input operand");
        return false;
    }
    free(constraint);
    return true;
}

static bool parse_gnu_inline_asm_label(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    MinicSourceSpan name_span;
    MinicStatementId target_statement;
    const char *name;
    size_t name_length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected GNU asm goto label name");
        }
        return false;
    }
    name_span = parser->current.span;
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    target_statement = minic_parser_find_label_statement(parser, name_span);
    return minic_c0_program_add_inline_asm_label(
               parser->program, inline_asm_id, name, name_length, target_statement) &&
           minic_parser_advance(parser);
}

static bool parse_gnu_inline_asm_statement(MinicParser *parser) {
    MinicStatement statement;
    MinicInlineAsmId inline_asm_id;
    MinicSourcePosition begin;
    MinicSourceSpan template_span;
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool is_goto;
    bool has_memory_clobber;

    if (!current_is_gnu_asm(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    template_text = NULL;
    template_length = 0U;
    is_volatile = false;
    is_goto = false;
    has_memory_clobber = false;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        if (current_is_gnu_volatile(parser)) {
            if (is_volatile) {
                minic_parser_error(parser, "duplicate volatile qualifier on GNU asm");
                return false;
            }
            is_volatile = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (current_is_gnu_goto(parser)) {
            if (is_goto) {
                minic_parser_error(parser, "duplicate goto qualifier on GNU asm");
                return false;
            }
            is_goto = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        break;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU asm") ||
        !minic_parser_parse_string_text(parser, &template_text, &template_length, &template_span)) {
        free(template_text);
        return false;
    }
    if (!minic_c0_program_add_inline_asm(
            parser->program, template_text, template_length, is_volatile, false, &inline_asm_id) ||
        !minic_c0_program_set_inline_asm_goto(parser->program, inline_asm_id, is_goto)) {
        free(template_text);
        minic_parser_error(parser, "cannot store GNU inline assembly");
        return false;
    }
    free(template_text);

    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_output(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_input(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            char *clobber;
            size_t clobber_length;
            MinicSourceSpan clobber_span;

            clobber = NULL;
            clobber_length = 0U;
            if (!minic_parser_parse_string_text(parser, &clobber, &clobber_length, &clobber_span)) {
                free(clobber);
                return false;
            }
            if (clobber_length == 6U && memcmp(clobber, "memory", 6U) == 0) {
                has_memory_clobber = true;
            } else {
                free(clobber);
                minic_parser_error(parser,
                                   "GNU asm register clobbers require TargetConstraint support");
                return false;
            }
            free(clobber);
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!is_goto) {
            minic_parser_error(parser, "GNU asm label operands require asm goto");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_label(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (is_goto) {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(parser->program, inline_asm_id);
        if (inline_asm == NULL || inline_asm->output_count != 0U || inline_asm->label_count == 0U) {
            minic_parser_error(parser,
                               "GNU asm goto currently requires no outputs and at least one label");
            return false;
        }
    }
    if (!minic_c0_program_set_inline_asm_memory_clobber(
            parser->program, inline_asm_id, has_memory_clobber)) {
        minic_parser_error(parser, "cannot finalize GNU inline assembly metadata");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_INLINE_ASM;
    statement.span.begin = begin;
    statement.span.end = parser->current.span.end;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.inline_asm_id = inline_asm_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU asm")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL &&
           minic_parser_token_starts_declaration_specifiers(parser, parser->current);
}

static bool token_starts_expression(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_IDENTIFIER || kind == MINIC_TOKEN_INTEGER_CONSTANT ||
           kind == MINIC_TOKEN_LPAREN || kind == MINIC_TOKEN_PLUS || kind == MINIC_TOKEN_MINUS ||
           kind == MINIC_TOKEN_BANG || kind == MINIC_TOKEN_AMPERSAND ||
           kind == MINIC_TOKEN_AMPERSAND_AMPERSAND || kind == MINIC_TOKEN_STAR;
}

bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration) {
    if (!ensure_function_label_context(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_advance(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return parse_compound_statement(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {
        if (!allow_declaration) {
            minic_parser_error(parser, "_Static_assert requires a declaration scope");
            return false;
        }
        return minic_parser_parse_static_assert_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_IF) {
        return parse_if(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_WHILE) {
        return parse_while(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_DO) {
        return parse_do_while(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_FOR) {
        return parse_for(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SWITCH) {
        return parse_switch(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_CASE) {
        return parse_case(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_DEFAULT) {
        return parse_default(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_BREAK) {
        return parse_break(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_CONTINUE) {
        return parse_continue(parser);
    }
    if (current_is_gnu_asm(parser)) {
        return parse_gnu_inline_asm_statement(parser);
    }
    if (current_identifier_is_local_label_keyword(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "GNU local label requires a compound statement scope");
            return false;
        }
        return parse_gnu_local_label_declaration(parser);
    }
    if (current_identifier_is_goto(parser)) {
        return parse_goto(parser);
    }
    if (identifier_starts_label(parser)) {
        return parse_label(parser, allow_declaration);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_EXTERN ||
        (parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
         identifier_equals(parser, parser->current.span, "__attribute__", 13U))) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_block_scope_extern_function_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_static_local_declaration(parser);
    }
    if (current_identifier_is_auto_type(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "GNU __auto_type requires a compound statement scope");
            return false;
        }
        return parse_auto_type_local_declaration(parser);
    }
    if (token_starts_local_declaration(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_RETURN) {
        return parse_return(parser);
    }
    if (token_starts_expression(parser->current.kind)) {
        return parse_expression_or_assignment_statement(parser, true);
    }
    minic_parser_error(parser,
                       "expected compound, if, while, for, switch, case/default, break, "
                       "goto/label, declaration, expression, return, or '}'");
    return false;
}
