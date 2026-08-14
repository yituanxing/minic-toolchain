#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
start = text.find("static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {")
end = text.find("\nstatic bool local_array_without_array_type", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate parse_primary")
body = text[start:end]
arm_start = body.rfind("    if (parser->current.kind == MINIC_TOKEN_LPAREN) {")
end_marker = "        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n"
arm_end = body.find(end_marker, arm_start)
if arm_start < 0 or arm_end < 0:
    raise SystemExit("cannot locate staged parenthesized primary arm")
arm_end += len(end_marker)

replacement = r'''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicSourcePosition begin;

        begin = parser->current.span.begin;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            if (!minic_parser_parse_statement_expression(parser, begin, &primary_id) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after GNU statement expression") ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        if (!parse_expression_internal(parser, &primary_id, 0U, decay_array)) {
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_COMMA) {
            const MinicExpression *left_expression;
            const MinicExpression *right_expression;
            MinicExpression comma_expression;
            MinicExpressionId right_id;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL || !minic_parser_advance(parser) ||
                !parse_expression_internal(parser, &right_id, 0U, true)) {
                return false;
            }
            right_expression = minic_c0_program_expression(parser->program, right_id);
            if (right_expression == NULL) {
                minic_parser_error(parser, "invalid comma expression operand");
                return false;
            }
            (void)memset(&comma_expression, 0, sizeof(comma_expression));
            comma_expression.kind = MINIC_EXPRESSION_BINARY;
            comma_expression.span.begin = left_expression->span.begin;
            comma_expression.span.end = right_expression->span.end;
            comma_expression.type = right_expression->type;
            comma_expression.value_category = MINIC_VALUE_RVALUE;
            comma_expression.value.binary.operator_kind = MINIC_BINARY_COMMA;
            comma_expression.value.binary.left = primary_id;
            comma_expression.value.binary.right = right_id;
            if (!minic_parser_add_expression(parser, &comma_expression, &primary_id)) {
                return false;
            }
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
body = body[:arm_start] + replacement + body[arm_end:]
path.write_text(text[:start] + body + text[end:])
print("restored parenthesized comma sequencing while retaining GNU statement expressions")
