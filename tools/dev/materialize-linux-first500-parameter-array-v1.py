#!/usr/bin/env python3
"""Materialize remaining parameter and inferred runtime record-array semantics."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {text.count(before)}")
    path.write_text(text.replace(before, after, 1))


function_parser = Path("src/frontend/parser_function.c")
replace_once(
    function_parser,
    """        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, true, &declarator)) {
""",
    """        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, false, &declarator)) {
""",
)
replace_once(
    function_parser,
    """        if (!is_function_pointer_parameter &&
            !adjust_function_parameter_type(parser, &parameter_type)) {
            return false;
        }
""",
    """        if (!adjust_function_parameter_type(parser, &parameter_type)) {
            return false;
        }
""",
)

statement_parser = Path("src/frontend/parser_statement.c")
replace_once(
    statement_parser,
    """    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;

        if ((!infer_count && initializer_count >= declared_count) ||
            initializer_count == SIZE_MAX) {
            minic_parser_error(parser, "too many local array initializers");
            return false;
        }
        if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
            !add_local_array_element_assignment(parser, local_id, initializer_count, value_id)) {
            return false;
        }
        initializer_count += 1U;
""",
    """    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;

        if ((!infer_count && initializer_count >= declared_count) ||
            initializer_count == SIZE_MAX) {
            minic_parser_error(parser, "too many local array initializers");
            return false;
        }
        if (minic_type_is_record(local->type)) {
            MinicExpressionId base_id;
            MinicExpressionId element_id;

            if (parser->current.kind != MINIC_TOKEN_LBRACE) {
                minic_parser_error(parser,
                                   "inferred runtime record array element requires braces");
                return false;
            }
            if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id) ||
                !add_array_object_element_lvalue(
                    parser, base_id, initializer_count, parser->current.span, &element_id) ||
                !minic_parser_parse_runtime_record_initializer(parser, element_id)) {
                return false;
            }
        } else if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                   !add_local_array_element_assignment(
                       parser, local_id, initializer_count, value_id)) {
            return false;
        }
        initializer_count += 1U;
""",
)
