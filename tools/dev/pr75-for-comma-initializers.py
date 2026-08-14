#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
marker = "static bool token_starts_local_declaration(const MinicParser *parser);\n\nstatic bool parse_for(MinicParser *parser) {\n"
helper = r'''static bool token_starts_local_declaration(const MinicParser *parser);

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
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after for initializer") &&
           minic_parser_add_statement(parser, &statement);
}

static bool parse_for(MinicParser *parser) {
'''
if text.count(marker) != 1:
    raise SystemExit(f"for initializer helper anchor: expected 1 match, found {text.count(marker)}")
text = text.replace(marker, helper, 1)
old = '''    } else if (!parse_expression_or_assignment_statement(parser, true)) {
        return false;
    }
'''
new = '''    } else if (!parse_for_initializer_expression(parser)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"for initializer full-expression dispatch: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged comma-capable full expressions in for initializers")
