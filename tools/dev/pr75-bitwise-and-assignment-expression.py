#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    """    if (minimum_precedence == 0U && (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_SLASH_EQUAL)) {
""",
    """    if (minimum_precedence == 0U &&
        (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL ||
         parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL)) {
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """        compound_operator =
            parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ? MINIC_BINARY_ADD : MINIC_BINARY_DIVIDE;
""",
    """        if (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL) {
            compound_operator = MINIC_BINARY_ADD;
        } else if (parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL) {
            compound_operator = MINIC_BINARY_BITWISE_AND;
        } else {
            compound_operator = MINIC_BINARY_DIVIDE;
        }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE)) {
""",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE &&
             expression->value.binary.operator_kind != MINIC_BINARY_BITWISE_AND)) {
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE) ||
""",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE &&
             expression->value.binary.operator_kind != MINIC_BINARY_BITWISE_AND) ||
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """            if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
                opcode = minic_type_is_long_integer(common_type) ? \"add\" : \"addw\";
            } else if (minic_type_is_unsigned_integer(common_type)) {
                opcode = minic_type_is_long_integer(common_type) ? \"divu\" : \"divuw\";
            } else {
                opcode = minic_type_is_long_integer(common_type) ? \"div\" : \"divw\";
            }
""",
    """            if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
                opcode = minic_type_is_long_integer(common_type) ? \"add\" : \"addw\";
            } else if (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
                opcode = \"and\";
            } else if (minic_type_is_unsigned_integer(common_type)) {
                opcode = minic_type_is_long_integer(common_type) ? \"divu\" : \"divuw\";
            } else {
                opcode = minic_type_is_long_integer(common_type) ? \"div\" : \"divw\";
            }
""",
)

print("staged integer '&=' compound assignment expressions")
