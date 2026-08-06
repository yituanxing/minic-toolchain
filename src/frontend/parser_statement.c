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

static bool parse_assignment(MinicParser *parser)
{
    MinicStatement statement;
    const MinicExpression *target_expression;
    const MinicExpression *assigned_expression;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = parser->current.span.begin;
    if (!minic_parser_parse_expression(
            parser,
            &statement.target_expression,
            0U)) {
        return false;
    }
    target_expression = minic_c0_program_expression(
        parser->program,
        statement.target_expression);
    if (!expression_is_modifiable_lvalue(target_expression)) {
        minic_parser_error(parser, "assignment target must be a modifiable lvalue");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    target_expression = minic_c0_program_expression(
        parser->program,
        statement.target_expression);
    assigned_expression = minic_c0_program_expression(
        parser->program,
        statement.expression);
    if (!expression_is_modifiable_lvalue(target_expression) ||
        assigned_expression == NULL ||
        !minic_type_assignment_compatible(
            target_expression->type,
            assigned_expression->type)) {
        minic_parser_error(parser, "assignment type does not match target type");
        return false;
    }
    statement.span.end = assigned_expression->span.end;
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
        !parse_branch(parser, &statement.then_block)) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool build_prefix_increment(
    MinicParser *parser,
    MinicStatementId *statement_id)
{
    MinicSourceSpan increment_span;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    const MinicLocal *local;
    MinicExpressionId target_id;
    MinicExpressionId value_id;
    MinicExpressionId one_id;
    MinicExpressionId sum_id;
    MinicExpression one;
    MinicExpression sum;
    MinicStatement statement;

    if (statement_id == NULL ||
        parser->current.kind != MINIC_TOKEN_PLUS_PLUS) {
        minic_parser_error(parser, "for update requires prefix increment");
        return false;
    }
    increment_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "prefix increment requires a local name");
        return false;
    }

    name_span = parser->current.span;
    local_id = minic_parser_find_local(parser, name_span);
    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || local->element_count != 1U ||
        !minic_type_is_integer(local->type) ||
        (minic_type_is_const(local->type) &&
         !minic_type_is_pointer(local->type))) {
        minic_parser_error(parser, "prefix increment requires a modifiable integer local");
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
    one.span = increment_span;
    one.type = minic_type_int();
    one.value_category = MINIC_VALUE_RVALUE;
    one.value.integer_value = 1;
    if (!minic_parser_add_expression(parser, &one, &one_id)) {
        return false;
    }

    (void)memset(&sum, 0, sizeof(sum));
    sum.kind = MINIC_EXPRESSION_BINARY;
    sum.span.begin = increment_span.begin;
    sum.span.end = name_span.end;
    sum.value_category = MINIC_VALUE_RVALUE;
    sum.value.binary.operator_kind = MINIC_BINARY_ADD;
    sum.value.binary.left = value_id;
    sum.value.binary.right = one_id;
    if (!minic_type_integer_common(local->type, one.type, &sum.type) ||
        !minic_parser_add_expression(parser, &sum, &sum_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = increment_span.begin;
    statement.span.end = name_span.end;
    statement.target_expression = target_id;
    statement.expression = sum_id;
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
    if (!parse_assignment(parser) ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") ||
        !build_prefix_increment(parser, &update_statement) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
    if (!parse_branch(parser, &statement.then_block)) {
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
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER ||
        parser->current.kind == MINIC_TOKEN_STAR ||
        parser->current.kind == MINIC_TOKEN_LPAREN) {
        return parse_assignment(parser);
    }
    minic_parser_error(
        parser,
        "expected compound, if, while, for, declaration, assignment, return, or '}'");
    return false;
}
