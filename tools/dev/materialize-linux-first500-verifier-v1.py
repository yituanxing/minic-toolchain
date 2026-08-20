#!/usr/bin/env python3
"""Converge GNU pointer-update and zero-length-array verifier ownership once."""
from pathlib import Path
import runpy


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


postfix = Path("src/frontend/parser_postfix.c")
replace_once(
    postfix,
    """    if (minic_type_is_pointer(operand_type)) {
        if (!minic_type_pointee(operand_type, &pointee_type) ||
            !minic_parser_require_complete_object_type(
                parser, pointee_type, \"pointer update requires a complete object type\")) {
            return false;
        }
""",
    """    if (minic_type_is_pointer(operand_type)) {
        if (!minic_type_pointee(operand_type, &pointee_type) ||
            !minic_c0_pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
            minic_parser_error(parser,
                               \"pointer update requires an arithmetic-compatible pointee type\");
            return false;
        }
""",
)

expression = Path("src/frontend/parser_expression.c")
replace_once(
    expression,
    """        if (minic_type_is_pointer(operand_expression->type)) {
            if (!minic_type_pointee(operand_expression->type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, \"pointer update requires a complete object type\")) {
                return false;
            }
""",
    """        if (minic_type_is_pointer(operand_expression->type)) {
            if (!minic_type_pointee(operand_expression->type, &pointee_type) ||
                !minic_c0_pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
                minic_parser_error(parser,
                                   \"pointer update requires an arithmetic-compatible pointee type\");
                return false;
            }
""",
)

verifier = Path("src/frontend/ast_verifier.c")
replace_once(
    verifier,
    """            return minic_type_is_pointer(operand->type) &&
                   minic_type_pointee(operand->type, &pointee_type) &&
                   minic_c0_type_is_complete_object(program, pointee_type);
""",
    """            return minic_type_is_pointer(operand->type) &&
                   minic_type_pointee(operand->type, &pointee_type) &&
                   minic_c0_pointer_arithmetic_pointee_allowed(program, pointee_type);
""",
)
replace_once(
    verifier,
    """        if ((array_type->element_count == 0U && !array_type->is_query_materialized &&

             !incomplete_array_has_semantic_owner(program, index)) ||
""",
    """        if ((array_type->element_count == 0U && !array_type->is_zero_length &&
             !array_type->is_query_materialized &&
             !incomplete_array_has_semantic_owner(program, index)) ||
""",
)

runpy.run_path("tools/dev/materialize-linux-first500-nested-designator-v1.py", run_name="__main__")
