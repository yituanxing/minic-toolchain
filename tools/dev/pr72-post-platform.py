#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL ||
            target_expression->value_category != MINIC_VALUE_LVALUE ||
""",
    """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression != NULL && minic_type_is_record(target_expression->type)) {
            /* Record assignment already has statement-level recursive copy lowering.
               Leave '=' unconsumed so that path can handle standalone record copies. */
            *expression_id = left;
            return true;
        }
        if (target_expression == NULL ||
            target_expression->value_category != MINIC_VALUE_LVALUE ||
""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """        assignment_token != MINIC_TOKEN_PLUS_EQUAL &&
        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&
""",
    """        assignment_token != MINIC_TOKEN_PLUS_EQUAL &&
        assignment_token != MINIC_TOKEN_MINUS_EQUAL &&
        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&
""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
""",
    """    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL) {
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
""",
)

print("staged record-assignment routing and subtract assignment lowering")
