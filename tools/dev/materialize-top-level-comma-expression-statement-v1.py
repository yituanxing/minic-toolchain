from pathlib import Path

root = Path(__file__).resolve().parents[2]

expr_path = root / "src/frontend/parser_expression.c"
text = expr_path.read_text()
old = r'''static bool
parse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId left;

    if (!parse_expression_internal(parser, &left, 0U, decay_array)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicExpression sequence;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &right, 0U, decay_array)) {
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
'''
new = r'''static bool parse_comma_expression_tail(MinicParser *parser,
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
            !parse_expression_internal(parser, &right, 0U, decay_array)) {
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

    if (!parse_expression_internal(parser, &left, 0U, decay_array)) {
        return false;
    }
    return parse_comma_expression_tail(parser, left, expression_id, decay_array);
}
'''
if text.count(old) != 1:
    raise SystemExit("canonical comma owner shape changed")
text = text.replace(old, new)
old = r'''bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id) {
    return parse_comma_expression(parser, expression_id, true);
}
'''
new = r'''bool minic_parser_parse_full_expression_tail(MinicParser *parser,
                                             MinicExpressionId left,
                                             MinicExpressionId *expression_id) {
    return parse_comma_expression_tail(parser, left, expression_id, true);
}

bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id) {
    return parse_comma_expression(parser, expression_id, true);
}
'''
if text.count(old) != 1:
    raise SystemExit("full-expression export shape changed")
expr_path.write_text(text.replace(old, new))

header_path = root / "src/frontend/parser_internal.h"
text = header_path.read_text()
old = '''bool minic_parser_parse_expression_no_decay(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id);
'''
new = '''bool minic_parser_parse_expression_no_decay(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_full_expression_tail(MinicParser *parser,
                                             MinicExpressionId left,
                                             MinicExpressionId *expression_id);
bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id);
'''
if text.count(old) != 1:
    raise SystemExit("parser internal expression declarations changed")
header_path.write_text(text.replace(old, new))

statement_path = root / "src/frontend/parser_statement.c"
text = statement_path.read_text()
old = r'''    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
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
'''
new = r'''    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
        assignment_token != MINIC_TOKEN_PLUS_EQUAL && assignment_token != MINIC_TOKEN_MINUS_EQUAL &&
        assignment_token != MINIC_TOKEN_STAR_EQUAL &&
        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&
        assignment_token != MINIC_TOKEN_PIPE_EQUAL &&
        assignment_token != MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        const MinicExpression *full_expression;

        if (!minic_parser_parse_full_expression_tail(
                parser, statement.expression, &statement.expression)) {
            return false;
        }
        full_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (full_expression == NULL) {
            minic_parser_error(parser, "invalid full expression statement");
            return false;
        }
        if (!allow_expression_statement && full_expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span.end = full_expression->span.end;
        return minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after expression") &&
               minic_parser_add_statement(parser, &statement);
    }
'''
if text.count(old) != 1:
    raise SystemExit("expression statement dispatch shape changed")
statement_path.write_text(text.replace(old, new))

fixture_path = root / "tests/compiler/c0/comma_operator.c"
text = fixture_path.read_text()
append = r'''
int comma_expression_statement(void)
{
    int value = 0;

    (void)(value += 1), (void)(value += 2), (void)(value += 4);
    return value;
}
'''
if "int comma_expression_statement(void)" in text:
    raise SystemExit("comma expression statement fixture already present")
fixture_path.write_text(text + append)

runner_path = root / "tests/compiler/c0/run-comma-operator.sh"
text = runner_path.read_text()
old = '''grep -F 'comma_conditions:' "$work/comma_operator.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if void-left=1 assignment-side-effect=1 result=right'
'''
new = '''grep -F 'comma_conditions:' "$work/comma_operator.s" >/dev/null
grep -F 'comma_expression_statement:' "$work/comma_operator.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
'''
if text.count(old) != 1:
    raise SystemExit("comma operator runner shape changed")
runner_path.write_text(text.replace(old, new))
