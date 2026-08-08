#include "frontend/parser_internal.h"

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
            minic_parser_error(parser, "array initializers are not supported yet");
            return false;
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
        minic_parser_error(parser,
                           "static local object currently requires a fixed array declarator");
        return false;
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

    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL) {
        if (!allow_expression_statement) {
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

static bool parse_while(MinicParser *parser) {
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_loop_branch(parser, &statement.then_block)) {
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

static bool parse_case(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;
    const MinicExpression *constant;
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
    if (constant == NULL || constant->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(constant->type)) {
        minic_parser_error(parser, "case label currently requires one integer constant");
        return false;
    }
    value = constant->value.integer_value;
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

static bool add_for_update_statement(MinicParser *parser,
                                     MinicSourcePosition begin,
                                     MinicSourcePosition end,
                                     MinicLocalId local_id,
                                     MinicTokenKind update_kind,
                                     MinicStatementId *statement_id) {
    const MinicLocal *local;
    MinicExpressionId target_id;
    MinicExpressionId value_id;
    MinicExpressionId one;
    MinicExpressionId updated_value_id;
    MinicExpression one;
    MinicExpression updated_value;
    MinicStatement statement;
    MinicSourceSpan name_span;
    MinicType pointee_type;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || local->element_count != 1U || minic_type_is_const(local->type) ||
        (!minic_type_is_integer(local->type) && !minic_type_is_pointer(local->type))) {
        minic_parser_error(parser, "for update requires a modifiable integer or pointer local");
        return false;
    }
    if (minic_type_is_pointer(local->type) &&
        (!minic_type_pointee(local->type, &pointee_type) ||
         !minic_parser_require_complete_object_type(
             parser, pointee_type, "pointer update requires a complete object type"))) {
        return false;
    }

    name_span.begin = begin;
    name_span.end = end;
    if (!add_local_lvalue_expression(parser, local_id, name_span, &target_id) ||
        !add_local_lvalue_expression(parser, local_id, name_span, &value_id)) {
        return false;
    }

    (void)memset(&one, 0, sizeof(one));
    one.kind = MINIC_EXPRESSION_INTEGER;
    one.span = name_span;
    one.type = minic_type_int();
    one.value_category = MINIC_VALUE_RVALUE;
    one.value.integer_value = 1;
    if (!minic_parser_add_expression(parser, &one, &one_id)) {
        return false;
    }

    (void)memset(&updated_value, 0, sizeof(updated_value));
    updated_value.kind = MINIC_EXPRESSION_BINARY;
    updated_value.span = name_span;
    updated_value.value_category = MINIC_VALUE_RVALUE;
    updated_value.value.binary.operator_kind =
        update_kind == MINIC_TOKEN_PLUS_PLUS ? MINIC_BINARY_ADD : MINIC_BINARY_SUBTRACT;
    updated_value.value.binary.left = value_id;
    updated_value.value.binary.right = one_id;
    if (minic_type_is_pointer(local->type)) {
        updated_value.type = local->type;
    } else if (!minic_type_integer_common(local->type, one.type, &updated_value.type)) {
        return false;
    }
    if (!minic_parser_add_expression(parser, &updated_value, &updated_value_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = name_span;
    statement.target_expression = target_id;
    statement.expression = updated_value_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(parser->program, &statement, statement_id)) {
        minic_parser_error(parser, "out of memory while building for update");
        return false;
    }
    return true;
}

static bool parse_for_update(MinicParser *parser, MinicStatementId *statement_id) {
    MinicSourcePosition begin;
    MinicSourceSpan name_span;
    MinicTokenKind update_kind;
    MinicLocalId local_id;
    bool prefix;

    if (statement_id == NULL) {
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

    prefix = parser->current.kind == MINIC_TOKEN_PLUS_PLUS ||
             parser->current.kind == MINIC_TOKEN_MINUS_MINUS;
    if (prefix) {
        update_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "prefix update requires a local name");
            return false;
        }
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "for update requires a local increment or decrement");
            return false;
        }
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_PLUS_PLUS &&
            parser->current.kind != MINIC_TOKEN_MINUS_MINUS) {
            minic_parser_error(parser, "postfix update requires '++' or '--'");
            return false;
        }
        update_kind = parser->current.kind;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (local_id == MINIC_LOCAL_INVALID) {
        minic_parser_error(parser, "for update requires a local name");
        return false;
    }
    return add_for_update_statement(
        parser, begin, name_span.end, local_id, update_kind, statement_id);
}

static bool parse_for(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId updates[8];
    size_t update_count;
    size_t update_index;
    MinicSourcePosition for_begin;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    for_begin = parser->current.span.begin;

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
        !parse_loop_branch(parser, &statement.then_block)) {
        return false;
    }
    for (update_index = 0U; update_index < update_count; ++update_index) {
        if (!minic_c0_block_add_statement(
                parser->program, statement.then_block, updates[update_index])) {
            minic_parser_error(parser, "cannot append for-loop update");
            return false;
        }
    }
    statement.span.begin = for_begin;
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
        if (!minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !apply_assignment_conversion(parser, function->return_type, &statement.expression)) {
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
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return parse_compound_statement(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_IF) {
        return parse_if(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_WHILE) {
        return parse_while(parser);
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
