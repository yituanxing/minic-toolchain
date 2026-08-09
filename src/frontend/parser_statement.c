#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdio.h>
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

static bool parse_zero_aggregate_initializer(MinicParser *parser,
                                             MinicSourceSpan *initializer_span) {
    MinicSourcePosition begin;
    bool saw_value;

    if (parser == NULL || initializer_span == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected aggregate zero initializer");
        return false;
    }
    begin = parser->current.span.begin;
    saw_value = false;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            if (!saw_value) {
                minic_parser_error(parser, "empty aggregate initializer is unsupported");
                return false;
            }
            initializer_span->begin = begin;
            initializer_span->end = parser->current.span.end;
            return minic_parser_advance(parser);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            MinicSourceSpan nested_span;

            if (!parse_zero_aggregate_initializer(parser, &nested_span)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
            int value;

            if (!minic_parser_parse_integer_value(parser, &value) || value != 0) {
                minic_parser_error(parser, "only all-zero aggregate initializers are supported");
                return false;
            }
        } else {
            minic_parser_error(parser, "only all-zero aggregate initializers are supported");
            return false;
        }
        saw_value = true;
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

static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {
    MinicLocal local;
    MinicLocalId local_id;
    MinicType declared_type;

    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
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
            MinicSourceSpan initializer_span;

            if (!add_local_lvalue_expression(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser) ||
                !parse_zero_aggregate_initializer(parser, &initializer_span) ||
                !add_zero_initialized_record_lvalue(parser, target_id, initializer_span)) {
                return false;
            }
            return true;
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

static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;

    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (minic_type_is_void(base_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }

    for (;;) {
        if (!parse_local_declarator(parser, base_type)) {
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
        MinicLocal local;
        MinicLocalId local_id;
        MinicStatement statement;
        const MinicExpression *initializer;

        if (!minic_type_is_const(declared_type) || parser->current.kind != MINIC_TOKEN_EQUAL) {
            minic_parser_error(parser,
                               "static local object currently requires a fixed array declarator");
            return false;
        }
        (void)memset(&local, 0, sizeof(local));
        local.name_span = name_span;
        local.type = declared_type;
        local.element_count = 1U;
        local.storage_offset = 0U;
        if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
            !minic_parser_bind_local(parser, name_span, local_id)) {
            minic_parser_error(parser, "cannot add static const scalar discovery local");
            return false;
        }

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = name_span.begin;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = MINIC_EXPRESSION_INVALID;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!add_local_lvalue_expression(
                parser, local_id, name_span, &statement.target_expression) ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !apply_assignment_conversion(parser, local.type, &statement.expression)) {
            return false;
        }
        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "static const scalar initializer type mismatch");
            return false;
        }
        statement.span.end = initializer->span.end;
        return minic_parser_add_statement(parser, &statement);
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        minic_parser_error(parser, "static local initializers are not supported yet");
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
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot add zero-initialized static local object");
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
    MinicExpression target_address;
    MinicExpression source_address;
    MinicExpressionId target_address_id;
    MinicExpressionId source_address_id;
    MinicType target_type;
    MinicType source_type;
    MinicSourceSpan target_span;
    MinicSourceSpan source_span;
    size_t field_index;

    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        source->value_category != MINIC_VALUE_LVALUE || !minic_type_is_record(target->type) ||
        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id) {
        minic_parser_error(parser, "record assignment requires matching record lvalues");
        return false;
    }

    /* Expression storage may realloc whenever a new expression is appended. Freeze every
       target/source property needed below before the first append and retain only stable IDs. */
    target_type = target->type;
    source_type = source->type;
    target_span = target->span;
    source_span = source->span;

    record = minic_c0_program_record(parser->program, target_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "record assignment requires a complete record");
        return false;
    }

    (void)memset(&target_address, 0, sizeof(target_address));
    target_address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    target_address.span = target_span;
    target_address.value_category = MINIC_VALUE_RVALUE;
    target_address.value.unary.operand = target_id;
    if (!minic_type_pointer_to(target_type, &target_address.type) ||
        !minic_parser_add_expression(parser, &target_address, &target_address_id)) {
        minic_parser_error(parser, "cannot form record assignment target address");
        return false;
    }

    (void)memset(&source_address, 0, sizeof(source_address));
    source_address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    source_address.span = source_span;
    source_address.value_category = MINIC_VALUE_RVALUE;
    source_address.value.unary.operand = source_id;
    if (!minic_type_pointer_to(source_type, &source_address.type) ||
        !minic_parser_add_expression(parser, &source_address, &source_address_id)) {
        minic_parser_error(parser, "cannot form record assignment source address");
        return false;
    }

    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicExpression target_member;
        MinicExpression source_member;
        MinicExpressionId target_member_id;
        MinicExpressionId source_member_id;
        MinicType target_member_type;
        MinicType source_member_type;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U) {
            minic_parser_error(parser, "record assignment with array members is unsupported");
            return false;
        }

        target_member_type = field->type;
        source_member_type = field->type;
        if ((minic_type_is_const(target_type) &&
             !minic_type_add_const(target_member_type, &target_member_type)) ||
            (minic_type_is_const(source_type) &&
             !minic_type_add_const(source_member_type, &source_member_type))) {
            minic_parser_error(parser, "cannot propagate const through record assignment");
            return false;
        }

        (void)memset(&target_member, 0, sizeof(target_member));
        target_member.kind = MINIC_EXPRESSION_MEMBER;
        target_member.span = span;
        target_member.type = target_member_type;
        target_member.value_category = MINIC_VALUE_LVALUE;
        target_member.value.member.base = target_address_id;
        target_member.value.member.record_id = target_type.record_id;
        target_member.value.member.field_index = field_index;
        if (!minic_parser_add_expression(parser, &target_member, &target_member_id)) {
            return false;
        }

        (void)memset(&source_member, 0, sizeof(source_member));
        source_member.kind = MINIC_EXPRESSION_MEMBER;
        source_member.span = span;
        source_member.type = source_member_type;
        source_member.value_category = MINIC_VALUE_LVALUE;
        source_member.value.member.base = source_address_id;
        source_member.value.member.record_id = source_type.record_id;
        source_member.value.member.field_index = field_index;
        if (!minic_parser_add_expression(parser, &source_member, &source_member_id)) {
            return false;
        }

        if (minic_type_is_record(field->type)) {
            if (!add_record_copy_assignments(parser, target_member_id, source_member_id, span)) {
                return false;
            }
        } else {
            MinicStatement assignment;

            if (!expression_is_modifiable_lvalue(
                    minic_c0_program_expression(parser->program, target_member_id)) ||
                !minic_c0_assignment_compatible(parser->program, field->type, source_member_id)) {
                minic_parser_error(parser, "record member cannot be copied");
                return false;
            }
            (void)memset(&assignment, 0, sizeof(assignment));
            assignment.kind = MINIC_STATEMENT_ASSIGN;
            assignment.span = span;
            assignment.target_expression = target_member_id;
            assignment.expression = source_member_id;
            assignment.target_statement = MINIC_STATEMENT_INVALID;
            assignment.then_block = MINIC_BLOCK_INVALID;
            assignment.else_block = MINIC_BLOCK_INVALID;
            if (!minic_parser_add_statement(parser, &assignment)) {
                return false;
            }
        }
    }
    return true;
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
    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression subtraction;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser, "compound subtraction assignment requires integer operands");
            return false;
        }
        (void)memset(&subtraction, 0, sizeof(subtraction));
        subtraction.kind = MINIC_EXPRESSION_BINARY;
        subtraction.span.begin = statement.span.begin;
        subtraction.span.end = right_expression->span.end;
        subtraction.type = common_type;
        subtraction.value_category = MINIC_VALUE_RVALUE;
        subtraction.value.binary.operator_kind = MINIC_BINARY_SUBTRACT;
        subtraction.value.binary.left = statement.target_expression;
        subtraction.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &subtraction, &statement.expression)) {
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
        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
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
        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
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
        !minic_parser_parse_expression(parser, &condition_id, 0U) ||
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
        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
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
                                        int *value) {
    const MinicExpression *expression;
    int left;
    int right;

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
            if (right < 0 || right >= 31 || left < 0) {
                return false;
            }
            *value = (int)((unsigned int)left << (unsigned int)right);
            return true;
        case MINIC_BINARY_SHIFT_RIGHT:
            if (right < 0 || right >= 31) {
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
    const MinicExpression *constant;
    MinicExpression folded_constant;
    MinicType constant_type;
    MinicSourceSpan constant_span;
    int value;
    size_t index;

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
        !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    constant = minic_c0_program_expression(parser->program, statement.expression);
    if (constant == NULL ||
        !case_integer_constant_value(parser->program, statement.expression, &value)) {
        minic_parser_error(parser, "case label currently requires one integer constant");
        return false;
    }
    constant_type = constant->type;
    constant_span = constant->span;
    for (index = 0U; index < context->case_count; ++index) {
        if (context->case_values[index] == value) {
            minic_parser_error(parser, "duplicate case value");
            return false;
        }
    }
    if (context->case_count >= MINIC_PARSER_MAX_SWITCH_CASES) {
        minic_parser_error(parser, "switch case count exceeds implementation limit");
        return false;
    }
    context->case_values[context->case_count] = value;
    context->case_count += 1U;

    (void)memset(&folded_constant, 0, sizeof(folded_constant));
    folded_constant.kind = MINIC_EXPRESSION_INTEGER;
    folded_constant.span = constant_span;
    folded_constant.type = constant_type;
    folded_constant.value_category = MINIC_VALUE_RVALUE;
    folded_constant.value.integer_value = value;
    if (!minic_parser_add_expression(parser, &folded_constant, &statement.expression)) {
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after case value")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
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

static bool add_general_prefix_for_update(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicExpressionId target_id,
                                          MinicTokenKind update_kind,
                                          MinicStatementId *statement_id) {
    const MinicExpression *target;
    MinicExpression update;
    MinicExpressionId update_id;
    MinicStatement statement;
    MinicType pointee_type;

    target = minic_c0_program_expression(parser->program, target_id);
    if (!expression_is_modifiable_lvalue(target) ||
        (!minic_type_is_integer(target->type) && !minic_type_is_pointer(target->type))) {
        minic_parser_error(parser, "prefix update requires a modifiable integer or pointer lvalue");
        return false;
    }
    if (minic_type_is_pointer(target->type) &&
        (!minic_type_pointee(target->type, &pointee_type) ||
         !minic_parser_require_complete_object_type(
             parser, pointee_type, "pointer update requires a complete object type"))) {
        return false;
    }

    (void)memset(&update, 0, sizeof(update));
    update.kind = MINIC_EXPRESSION_UNARY;
    update.span.begin = begin;
    update.span.end = target->span.end;
    update.type = target->type;
    update.value_category = MINIC_VALUE_RVALUE;
    update.value.unary.operator_kind = update_kind == MINIC_TOKEN_PLUS_PLUS
                                           ? MINIC_UNARY_POST_INCREMENT
                                           : MINIC_UNARY_POST_DECREMENT;
    update.value.unary.operand = target_id;
    if (!minic_parser_add_expression(parser, &update, &update_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span = update.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = update_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_c0_program_add_statement(parser->program, &statement, statement_id);
}

static bool parse_for_update(MinicParser *parser, MinicStatementId *statement_id) {
    MinicSourcePosition begin;
    MinicExpressionId first_id;
    const MinicExpression *first;
    MinicStatement statement;
    MinicTokenKind assignment_token;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_KW_VOID ||
            !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after void cast")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "for update only supports a discarded void cast here");
            }
            return false;
        }
    }

    if (parser->current.kind == MINIC_TOKEN_PLUS_PLUS ||
        parser->current.kind == MINIC_TOKEN_MINUS_MINUS) {
        MinicTokenKind update_kind;
        MinicExpressionId target_id;

        update_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &target_id, 0U)) {
            return false;
        }
        return add_general_prefix_for_update(parser, begin, target_id, update_kind, statement_id);
    }

    if (!minic_parser_parse_expression(parser, &first_id, 0U)) {
        return false;
    }
    first = minic_c0_program_expression(parser->program, first_id);
    if (first == NULL) {
        minic_parser_error(parser, "invalid for update expression");
        return false;
    }
    assignment_token = parser->current.kind;

    (void)memset(&statement, 0, sizeof(statement));
    statement.span.begin = begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = first_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (assignment_token == MINIC_TOKEN_EQUAL) {
        MinicType target_type;
        MinicSourceSpan target_span;
        const MinicExpression *assigned;

        if (!expression_is_modifiable_lvalue(first)) {
            minic_parser_error(parser, "assignment target must be a modifiable lvalue");
            return false;
        }
        target_type = first->type;
        target_span = first->span;
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.target_expression = first_id;
        statement.expression = MINIC_EXPRESSION_INVALID;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !apply_assignment_conversion(parser, target_type, &statement.expression)) {
            return false;
        }
        assigned = minic_c0_program_expression(parser->program, statement.expression);
        if (assigned == NULL ||
            !minic_c0_assignment_compatible(parser->program, target_type, statement.expression)) {
            minic_parser_error(parser, "assignment type does not match target type");
            return false;
        }
        statement.span.begin = target_span.begin;
        statement.span.end = assigned->span.end;
        return minic_c0_program_add_statement(parser->program, &statement, statement_id);
    }

    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span.end = first->span.end;
    return minic_c0_program_add_statement(parser->program, &statement, statement_id);
}

static bool parse_for(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId updates[8];
    MinicStatementId continue_label;
    MinicStatementId previous_continue_target;
    size_t update_count;
    size_t update_index;
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
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!parse_expression_or_assignment_statement(parser, false)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_expression(parser, &statement.expression, 0U) ||
               !expression_is_integer_condition(parser, statement.expression) ||
               !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'")) {
        return false;
    }

    update_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (update_count >= sizeof(updates) / sizeof(updates[0])) {
            minic_parser_error(parser, "for update supports at most eight comma-separated items");
            return false;
        }
        if (!parse_for_update(parser, &updates[update_count])) {
            return false;
        }
        update_count += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RPAREN) {
            minic_parser_error(parser, "expected ',' or ')' after for update");
            return false;
        }
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
    for (update_index = 0U; update_index < update_count; ++update_index) {
        if (!minic_c0_block_add_statement(
                parser->program, statement.then_block, updates[update_index])) {
            minic_parser_error(parser, "cannot append for-loop update");
            return false;
        }
    }
    statement.span.begin = for_span.begin;
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
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
            minic_parser_span_equals(parser, statement->span, name_span)) {
            return statement_index;
        }
    }
    return MINIC_STATEMENT_INVALID;
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
    statement.target_statement = find_function_label(parser, name_span);
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

static bool parse_label(MinicParser *parser, bool allow_declaration) {
    MinicStatement statement;
    MinicSourceSpan name_span;
    MinicStatementId label_statement_id;
    size_t statement_index;

    name_span = parser->current.span;
    if (find_function_label(parser, name_span) != MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "duplicate label definition");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_LABEL;
    statement.span = name_span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after label")) {
        return false;
    }
    label_statement_id = parser->program->statement_count;
    if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    for (statement_index = parser->function_statement_begin; statement_index < label_statement_id;
         ++statement_index) {
        MinicStatement *pending;

        pending = &parser->program->statements[statement_index];
        if (pending->kind == MINIC_STATEMENT_GOTO &&
            pending->target_statement == MINIC_STATEMENT_INVALID &&
            minic_parser_span_equals(parser, pending->span, name_span)) {
            pending->target_statement = label_statement_id;
        }
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

static bool token_starts_local_declaration(const MinicParser *parser) {
    switch (parser->current.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        return !minic_parser_name_bound(parser, parser->current.span) &&
               minic_parser_find_type_alias(parser, parser->current.span) !=
                   MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

static bool token_starts_expression(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_IDENTIFIER || kind == MINIC_TOKEN_INTEGER_CONSTANT ||
           kind == MINIC_TOKEN_LPAREN || kind == MINIC_TOKEN_PLUS || kind == MINIC_TOKEN_MINUS ||
           kind == MINIC_TOKEN_BANG || kind == MINIC_TOKEN_AMPERSAND || kind == MINIC_TOKEN_STAR;
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
    if (current_identifier_is_goto(parser)) {
        return parse_goto(parser);
    }
    if (identifier_starts_label(parser)) {
        return parse_label(parser, allow_declaration);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_static_local_declaration(parser);
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
