#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

marker = "static bool parse_zero_aggregate_initializer(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit("unexpected aggregate-zero parser marker")

helper = r'''static bool aggregate_expression_is_zero_constant(const MinicC0Program *program,
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

'''
text = text.replace(marker, helper + marker, 1)

old = r'''        } else if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
            int value;

            if (!minic_parser_parse_integer_value(parser, &value) || value != 0) {
                minic_parser_error(parser, "only all-zero aggregate initializers are supported");
                return false;
            }
        } else {
            minic_parser_error(parser, "only all-zero aggregate initializers are supported");
            return false;
        }
'''
new = r'''        } else {
            MinicExpressionId value_id;

            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !aggregate_expression_is_zero_constant(parser->program, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "only all-zero aggregate initializers are supported");
                }
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected aggregate-zero element parser")
path.write_text(text.replace(old, new, 1))
print("staged pointer null constants in all-zero aggregate initializers")
