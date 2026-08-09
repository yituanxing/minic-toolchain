#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_in_region(path: str,
                      start_marker: str,
                      end_marker: str,
                      old: str,
                      new: str,
                      label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: cannot locate region")
    region = text[start:end]
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match in region, found {count}")
    region = region.replace(old, new, 1)
    target.write_text(text[:start] + region + text[end:])


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

statement_path = "src/frontend/parser_statement.c"
old_call = "minic_parser_parse_expression(parser, &statement.expression, 0U)"
new_call = "minic_parser_parse_full_expression(parser, &statement.expression)"

replace_in_region(statement_path,
                  "static bool parse_if(MinicParser *parser) {\n",
                  "static bool add_internal_continue_label(",
                  old_call,
                  new_call,
                  "if full expression")
replace_in_region(statement_path,
                  "static bool parse_while(MinicParser *parser) {\n",
                  "static bool parse_do_while(",
                  old_call,
                  new_call,
                  "while full expression")
replace_in_region(statement_path,
                  "static bool parse_switch(MinicParser *parser) {\n",
                  "static bool case_integer_constant_value(",
                  old_call,
                  new_call,
                  "switch full expression")
replace_in_region(statement_path,
                  "static bool parse_for(MinicParser *parser) {\n",
                  "static bool ensure_function_label_context(",
                  old_call,
                  new_call,
                  "for condition full expression")
replace_in_region(statement_path,
                  "static bool parse_do_while(MinicParser *parser) {\n",
                  "static bool parse_switch(",
                  "minic_parser_parse_expression(parser, &condition_id, 0U)",
                  "minic_parser_parse_full_expression(parser, &condition_id)",
                  "do-while full expression")

# Subscript grammar also accepts a full expression. Keep function arguments on the
# assignment-expression parser so their commas remain argument separators.
replace_in_region("src/frontend/parser_postfix.c",
                  "static bool parse_one_subscript(MinicParser *parser,\n",
                  "static const MinicFunctionType *indirect_callee_type(",
                  "minic_parser_parse_expression(parser, &index_id, 0U)",
                  "minic_parser_parse_full_expression(parser, &index_id)",
                  "subscript full expression")

print("staged full comma expressions in control conditions and subscripts")
