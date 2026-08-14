#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    """    if (minimum_precedence == 0U && (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
                                     parser->current.kind == MINIC_TOKEN_SLASH_EQUAL)) {
""",
    """    if (minimum_precedence == 0U &&
        (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_MINUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL)) {
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """        compound_operator =
            parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ? MINIC_BINARY_ADD : MINIC_BINARY_DIVIDE;
""",
    """        if (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL) {
            compound_operator = MINIC_BINARY_ADD;
        } else if (parser->current.kind == MINIC_TOKEN_MINUS_EQUAL) {
            compound_operator = MINIC_BINARY_SUBTRACT;
        } else {
            compound_operator = MINIC_BINARY_DIVIDE;
        }
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """            if (compound_operator != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value_expression->type) ||
""",
    """            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value_expression->type) ||
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE)) {
""",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE)) {
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """        if (minic_type_is_pointer(left->type)) {
            return expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(right->type);
        }
""",
    """        if (minic_type_is_pointer(left->type)) {
            return (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
                    expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&
                   minic_type_is_integer(right->type);
        }
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE) ||
""",
    """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE) ||
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """            if (expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value->type) ||
""",
    """            if ((expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
                 expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value->type) ||
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """                fprintf(file,
                        \"  ld t0, 8(sp)\\n\"
                        \"  add a0, t0, a0\\n\") < 0) {
""",
    """                fprintf(file,
                        \"  ld t0, 8(sp)\\n\"
                        \"  %s a0, t0, a0\\n\",
                        expression->value.binary.operator_kind == MINIC_BINARY_ADD ? \"add\" : \"sub\") < 0) {
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
            } else if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
                opcode = minic_type_is_long_integer(common_type) ? \"sub\" : \"subw\";
            } else if (minic_type_is_unsigned_integer(common_type)) {
                opcode = minic_type_is_long_integer(common_type) ? \"divu\" : \"divuw\";
            } else {
                opcode = minic_type_is_long_integer(common_type) ? \"div\" : \"divw\";
            }
""",
)

print("staged integer and pointer '-=' compound assignment expressions")
