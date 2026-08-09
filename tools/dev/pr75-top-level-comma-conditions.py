#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
''',
    '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
bool minic_parser_parse_full_expression(MinicParser *parser,
                                        MinicExpressionId *expression_id);
''',
    "full-expression declaration",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return parse_expression_internal(parser, expression_id, minimum_precedence, true);
}
''',
    '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return parse_expression_internal(parser, expression_id, minimum_precedence, true);
}

bool minic_parser_parse_full_expression(MinicParser *parser,
                                        MinicExpressionId *expression_id) {
    return parse_comma_expression(parser, expression_id, true);
}
''',
    "full-expression implementation",
)

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = '''        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
        !expression_is_integer_condition(parser, statement.expression) ||'''
new = '''        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_integer_condition(parser, statement.expression) ||'''
count = text.count(old)
if count != 2:
    raise SystemExit(f"if/while full-expression conditions: expected 2 matches, found {count}")
text = text.replace(old, new)

old = '''        !minic_parser_parse_expression(parser, &condition_id, 0U) ||
        !expression_is_integer_condition(parser, condition_id) ||'''
new = '''        !minic_parser_parse_full_expression(parser, &condition_id) ||
        !expression_is_integer_condition(parser, condition_id) ||'''
if text.count(old) != 1:
    raise SystemExit("do-while full-expression condition: expected 1 match")
text = text.replace(old, new, 1)

old = '''        !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
        !expression_is_switch_selector(parser, statement.expression) ||'''
new = '''        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_switch_selector(parser, statement.expression) ||'''
if text.count(old) != 1:
    raise SystemExit("switch full-expression condition: expected 1 match")
text = text.replace(old, new, 1)

old = '''    } else if (!minic_parser_parse_expression(parser, &statement.expression, 0U) ||
               !expression_is_integer_condition(parser, statement.expression) ||'''
new = '''    } else if (!minic_parser_parse_full_expression(parser, &statement.expression) ||
               !expression_is_integer_condition(parser, statement.expression) ||'''
if text.count(old) != 1:
    raise SystemExit("for full-expression condition: expected 1 match")
text = text.replace(old, new, 1)
path.write_text(text)

print("staged top-level comma operators in statement conditions")
