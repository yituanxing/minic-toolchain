#include "frontend/parser_internal.h"

#include <string.h>

static bool add_local_lvalue_expression(
    MinicParser *parser,
    MinicLocalId local_id,
    MinicSourceSpan span,
    MinicExpressionId *expression_id)
{
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

static bool expression_is_modifiable_lvalue(
    const MinicExpression *expression)
{
    return expression != NULL &&
           expression->value_category == MINIC_VALUE_LVALUE &&
           !(minic_type_is_const(expression->type) &&
             !minic_type_is_pointer(expression->type));
}

static bool parse_local_declarator(
    MinicParser *parser,
    MinicType base_type)
{
    MinicLocal local;
    MinicLocalId local_id;
    MinicType declared_type;

    if (!minic_parser_parse_pointer_declarator(
            parser,
            base_type,
            &declared_type)) {
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
    if (minic_parser_find_local_in_current_scope(
            parser,
            local.name_span) != MINIC_LOCAL_INVALID) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(
                parser,
                &local.element_count)) {
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
                parser,
                local_id,
                local.name_span,
                &statement.target_expression) ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
            return false;
        }
        initializer = minic_c0_program_expression(
            parser->program,
            statement.expression);
        if (initializer == NULL ||
            !minic_type_assignment_compatible(local.type, initializer->type)) {
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

static bool parse_declaration(MinicParser *parser)
{
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

static bool parse_expression_or_assignment_statement(
    MinicParser *parser,
    bool allow_expression_statement)
{
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

    if (!minic_parser_parse_expression(
            parser,
            &statement.expression,
            0U)) {
        return false;
    }
    first_expression = minic_c0_program_expression(
        parser->program,
        statement.expression);
    if (first_expression == NULL) {
        minic_parser_error(parser, "invalid statement expression");
        return false;
    }
    first_type = first_expression->type;
    assignment_token = parser->current.kind;

    if (assignment_token != MINIC_TOKEN_EQUAL &&
        assignment_token != MINIC_TOKEN_CARET_EQUAL) {
        if (!allow_expression_statement) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span.end = first_expression->span.end;
        return minic_parser_expect(
                   parser,
                   MINIC_TOKEN_SEMICOLON,
                   "expected ';' after expression") &&
               minic_parser_add_statement(parser, &statement);
    }

    statement.kind = assignment_token == MINIC_TOKEN_CARET_EQUAL
        ? MINIC_STATEMENT_XOR_ASSIGN
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
    {
        const MinicExpression *assigned_expression;

        assigned_expression = minic_c0_program_expression(
            parser->program,
            statement.expression);
        if (statement.kind == MINIC_STATEMENT_XOR_ASSIGN) {
            MinicType common_type;

            if (assigned_expression == NULL ||
                !minic_type_is_integer(first_type) ||
                !minic_type_is_integer(assigned_expression->type) ||
                !minic_type_integer_common(
                    first_type,
                    assigned_expression->type,
                    &common_type)) {
                minic_parser_error(
                    parser,
                    "compound XOR assignment requires integer operands");
                return false;
            }
        } else if (assigned_expression == NULL ||
                   !minic_type_assignment_compatible(
                       first_type,
                       assigned_expression->type)) {
            minic_parser_error(parser, "assignment type does not match target type");
            return false;
        }
        statement.span.end = assigned_expression->span.end;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_compound_statement(MinicParser *parser)
{
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
        success = minic_parser_expect(
            parser,
            MINIC_TOKEN_RBRACE,
            "expected '}'");
    }

    minic_parser_end_scope(parser);
    return success;
}

static bool parse_branch(MinicParser *parser, MinicBlockId *block_id)
{
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

static bool parse_loop_branch(MinicParser *parser, MinicBlockId *block_id)
{
    bool success;

    parser->loop_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->loop_depth -= 1U;
    return success;
}

static bool expression_is_integer_condition(
    MinicParser *parser,
    MinicExpressionId expression_id)
{
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        minic_parser_error(parser, "condition requires an integer expression");
        return false;
    }
    return true;
}

static bool parse_if(MinicParser *parser)
{
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
        if (!minic_parser_advance(parser) ||
            !parse_branch(parser, &statement.else_block)) {
            return false;
        }
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_while(MinicParser *parser)
{
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
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

static bool build_prefix_update(
    MinicParser *parser,
    MinicStatementId *statement_id)
{
    MinicSourceSpan update_span;
    MinicSourceSpan name_span;
    MinicTokenKind update_kind;
    MinicBinaryOperator operator_kind;
    const char *name_error;
    const char *target_error;
    MinicLocalId local_id;
    const MinicLocal *local;
    MinicExpressionId target_id;
    MinicExpressionId value_id;
    MinicExpressionId one_id;
    MinicExpressionId updated_value_id;
    MinicExpression one;
    MinicExpression updated_value;
    MinicStatement statement;

    if (statement_id == NULL ||
        (parser->current.kind != MINIC_TOKEN_PLUS_PLUS &&
         parser->current.kind != MINIC_TOKEN_MINUS_MINUS)) {
        minic_parser_error(
            parser,
            "for update requires prefix increment or decrement");
        return false;
    }

    update_kind = parser->current.kind;
    operator_kind = update_kind == MINIC_TOKEN_PLUS_PLUS
        ? MINIC_BINARY_ADD
        : MINIC_BINARY_SUBTRACT;
    name_error = update_kind == MINIC_TOKEN_PLUS_PLUS
        ? "prefix increment requires a local name"
        : "prefix decrement requires a local name";
    target_error = update_kind == MINIC_TOKEN_PLUS_PLUS
        ? "prefix increment requires a modifiable integer local"
        : "prefix decrement requires a modifiable integer local";
    update_span = parser->current.span;

    if (!minic_parser_advance(parser) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "%s", name_error);
        return false;
    }

    name_span = parser->current.span;
    local_id = minic_parser_find_local(parser, name_span);
    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || local->element_count != 1U ||
        !minic_type_is_integer(local->type) ||
        (minic_type_is_const(local->type) &&
         !minic_type_is_pointer(local->type))) {
        minic_parser_error(parser, "%s", target_error);
        return false;
    }
    if (!add_local_lvalue_expression(
            parser,
            local_id,
            name_span,
            &target_id) ||
        !add_local_lvalue_expression(
            parser,
            local_id,
            name_span,
            &value_id)) {
        return false;
    }

    (void)memset(&one, 0, sizeof(one));
    one.kind = MINIC_EXPRESSION_INTEGER;
    one.span = update_span;
    one.type = minic_type_int();
    one.value_category = MINIC_VALUE_RVALUE;
    one.value.integer_value = 1;
    if (!minic_parser_add_expression(parser, &one, &one_id)) {
        return false;
    }

    (void)memset(&updated_value, 0, sizeof(updated_value));
    updated_value.kind = MINIC_EXPRESSION_BINARY;
    updated_value.span.begin = update_span.begin;
    updated_value.span.end = name_span.end;
    updated_value.value_category = MINIC_VALUE_RVALUE;
    updated_value.value.binary.operator_kind = operator_kind;
    updated_value.value.binary.left = value_id;
    updated_value.value.binary.right = one_id;
    if (!minic_type_integer_common(
            local->type,
            one.type,
            &updated_value.type) ||
        !minic_parser_add_expression(
            parser,
            &updated_value,
            &updated_value_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = update_span.begin;
    statement.span.end = name_span.end;
    statement.target_expression = target_id;
    statement.expression = updated_value_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(
            parser->program,
            &statement,
            statement_id)) {
        minic_parser_error(parser, "out of memory while building for update");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_for(MinicParser *parser)
{
    MinicStatement statement;
    MinicStatementId update_statement;
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
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER &&
        parser->current.kind != MINIC_TOKEN_STAR &&
        parser->current.kind != MINIC_TOKEN_LPAREN) {
        minic_parser_error(parser, "for initializer requires an assignment");
        return false;
    }
    if (!parse_expression_or_assignment_statement(parser, false)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_expression(
                   parser,
                   &statement.expression,
                   0U) ||
               !expression_is_integer_condition(parser, statement.expression) ||
               !minic_parser_expect(
                   parser,
                   MINIC_TOKEN_SEMICOLON,
                   "expected ';'")) {
        return false;
    }
    if (!build_prefix_update(parser, &update_statement) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_loop_branch(parser, &statement.then_block)) {
        return false;
    }
    if (!minic_c0_block_add_statement(
            parser->program,
            statement.then_block,
            update_statement)) {
        minic_parser_error(parser, "cannot append for-loop update");
        return false;
    }
    statement.span.begin = for_begin;
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_break(MinicParser *parser)
{
    MinicStatement statement;

    if (parser->loop_depth == 0U) {
        minic_parser_error(parser, "break statement requires an enclosing loop");
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
    return minic_parser_expect(
               parser,
               MINIC_TOKEN_SEMICOLON,
               "expected ';' after break") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_return(MinicParser *parser)
{
    const MinicFunction *function;
    MinicStatement statement;

    function = minic_c0_program_function(
        parser->program,
        parser->current_function);
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
        if (!minic_parser_parse_expression(
                parser,
                &statement.expression,
                0U)) {
            return false;
        }
        returned_expression = minic_c0_program_expression(
            parser->program,
            statement.expression);
        if (returned_expression == NULL ||
            !minic_type_assignment_compatible(
                function->return_type,
                returned_expression->type)) {
            minic_parser_error(
                parser,
                "return expression does not match function return type");
            return false;
        }
        statement.span.end = returned_expression->span.end;
        parser->program->return_expression = statement.expression;
    }

    return minic_parser_expect(
               parser,
               MINIC_TOKEN_SEMICOLON,
               "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_add_default_return(MinicParser *parser)
{
    const MinicFunction *function;
    MinicStatement statement;

    function = minic_c0_program_function(
        parser->program,
        parser->current_function);
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
        if (!minic_parser_add_expression(
                parser,
                &expression,
                &statement.expression)) {
            return false;
        }
        parser->program->return_expression = statement.expression;
    } else if (!minic_type_is_void(function->return_type)) {
        minic_parser_error(parser, "unsupported implicit return type");
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

static bool token_starts_local_declaration(const MinicParser *parser)
{
    switch (parser->current.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        return minic_parser_find_local(parser, parser->current.span) ==
                   MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, parser->current.span) !=
                   MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

static bool token_starts_expression(MinicTokenKind kind)
{
    return kind == MINIC_TOKEN_IDENTIFIER ||
           kind == MINIC_TOKEN_INTEGER_CONSTANT ||
           kind == MINIC_TOKEN_LPAREN ||
           kind == MINIC_TOKEN_PLUS ||
           kind == MINIC_TOKEN_MINUS ||
           kind == MINIC_TOKEN_BANG ||
           kind == MINIC_TOKEN_AMPERSAND ||
           kind == MINIC_TOKEN_STAR;
}

bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration)
{
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
    if (parser->current.kind == MINIC_TOKEN_KW_BREAK) {
        return parse_break(parser);
    }
    if (token_starts_local_declaration(parser)) {
        if (!allow_declaration) {
            minic_parser_error(
                parser,
                "a declaration requires a compound statement scope");
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
    minic_parser_error(
        parser,
        "expected compound, if, while, for, break, declaration, expression, return, or '}'");
    return false;
}
