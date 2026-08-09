#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = r'''    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL ||
               assignment_token == MINIC_TOKEN_STAR_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression arithmetic;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires integer operands");
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
'''
new = r'''    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL ||
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
                                   : "compound subtraction assignment requires pointer/integer or integer operands");
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
                                   : "compound subtraction assignment requires pointer/integer or integer operands");
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
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected compound subtraction block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged pointer -= integer compound assignment")
