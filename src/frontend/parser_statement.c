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

static bool parse_declaration(MinicParser *parser)
{
    MinicLocal local;
    MinicLocalId local_id;
    MinicType declared_type;

    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_KW_INT,
            "expected keyword 'int'")) {
        return false;
    }
    declared_type = minic_type_int();
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        if (!minic_type_pointer_to(declared_type, &declared_type) ||
            !minic_parser_advance(parser)) {
            minic_parser_error(parser, "pointer declarator depth is unsupported");
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected local name");
        return false;
    }

    local.name_span = parser->current.span;
    local.type = declared_type;
    local.storage_offset = 0U;
    if (minic_parser_find_local_in_current_scope(
            parser,
            local.name_span) != MINIC_LOCAL_INVALID) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "out of memory while adding local");
        return false;
    }
    if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        MinicStatement statement;
        const MinicExpression *initializer;

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
            !minic_type_equal(local.type, initializer->type)) {
            minic_parser_error(parser, "initializer type does not match local type");
            return false;
        }
        statement.span.end = initializer->span.end;
        if (!minic_parser_add_statement(parser, &statement)) {
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
    if (target_expression == NULL ||
        target_expression->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "assignment target must be an lvalue");
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
    if (target_expression == NULL || assigned_expression == NULL ||
        target_expression->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(target_expression->type, assigned_expression->type)) {
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
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_branch(parser, &statement.then_block)) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_return(MinicParser *parser)
{
    MinicStatement statement;
    const MinicExpression *returned_expression;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    returned_expression = minic_c0_program_expression(
        parser->program,
        statement.expression);
    if (returned_expression == NULL ||
        !minic_type_is_integer(returned_expression->type)) {
        minic_parser_error(parser, "return expression must have int type");
        return false;
    }
    statement.span.end = returned_expression->span.end;
    if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") ||
        !minic_parser_add_statement(parser, &statement)) {
        return false;
    }
    parser->program->return_expression = statement.expression;
    return true;
}

bool minic_parser_add_default_return(MinicParser *parser)
{
    MinicExpression expression;
    MinicStatement statement;

    (void)memset(&expression, 0, sizeof(expression));
    (void)memset(&statement, 0, sizeof(statement));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = 0;
    if (!minic_parser_add_expression(
            parser,
            &expression,
            &statement.expression)) {
        return false;
    }
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = parser->current.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    parser->program->return_expression = statement.expression;
    return minic_parser_add_statement(parser, &statement);
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
    if (parser->current.kind == MINIC_TOKEN_KW_INT) {
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
        "expected compound, if, while, declaration, assignment, return, or '}'");
    return false;
}
