#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}: {old[:140]!r}")
    return text.replace(old, new, 1)


def rewrite_region(path: str, start_marker: str, end_marker: str, edits) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    region = text[start:end]
    for old, new, label in edits:
        region = replace_once(region, old, new, f"{path}:{label}")
    target.write_text(text[:start] + region + text[end:])


# Parser: extend the already-staged += expression block, and only that block.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old_start = "    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_PLUS_EQUAL) {\n"
new_start = """    if (minimum_precedence == 0U &&
        (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL)) {
"""
text = replace_once(text, old_start, new_start, "parser compound start")
path.write_text(text)
rewrite_region(
    "src/frontend/parser_expression.c",
    new_start,
    "    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {\n",
    [
        (
            """        MinicSourceSpan target_span;
        MinicType target_type;

        target_expression = minic_c0_program_expression(parser->program, left);
""",
            """        MinicSourceSpan target_span;
        MinicType target_type;
        MinicBinaryOperator compound_operator;

        compound_operator = parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ? MINIC_BINARY_ADD
                                                                           : MINIC_BINARY_DIVIDE;
        target_expression = minic_c0_program_expression(parser->program, left);
""",
            "operator capture",
        ),
        (
            """            if (!minic_type_is_integer(value_expression->type) ||
""",
            """            if (compound_operator != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value_expression->type) ||
""",
            "pointer add-only",
        ),
        (
            "        assignment.value.binary.operator_kind = MINIC_BINARY_ADD;\n",
            "        assignment.value.binary.operator_kind = compound_operator;\n",
            "AST operator",
        ),
    ],
)

# Verifier: modify only the COMPOUND_ASSIGNMENT case.
rewrite_region(
    "src/frontend/ast_verifier.c",
    "    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {\n",
    "    case MINIC_EXPRESSION_UNARY: {\n",
    [
        (
            """            expression->value.binary.operator_kind != MINIC_BINARY_ADD) {
""",
            """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE)) {
""",
            "allowed operators",
        ),
        (
            """        if (minic_type_is_pointer(left->type)) {
            return minic_type_is_integer(right->type);
        }
""",
            """        if (minic_type_is_pointer(left->type)) {
            return expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(right->type);
        }
""",
            "pointer add-only",
        ),
    ],
)

# Backend: modify only the COMPOUND_ASSIGNMENT lowering.
rewrite_region(
    "src/target/riscv64/codegen_expression.c",
    "    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {\n",
    "    case MINIC_EXPRESSION_UNARY:\n",
    [
        (
            """            expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
""",
            """            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE) ||
""",
            "allowed operators",
        ),
        (
            """            if (!minic_type_is_integer(value->type) ||
""",
            """            if (expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value->type) ||
""",
            "pointer add-only",
        ),
        (
            """        } else {
            MinicType common_type;

""",
            """        } else {
            MinicType common_type;
            const char *opcode;

""",
            "opcode local",
        ),
        (
            """                !minic_riscv64_emit_normalize_integer(file, common_type, "a0") ||
                fprintf(file,
                        "  ld t0, 8(sp)\\n"
                        "  %s a0, t0, a0\\n",
                        minic_type_is_long_integer(common_type) ? "add" : "addw") < 0 ||
                !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
""",
            """                !minic_riscv64_emit_normalize_integer(file, common_type, "a0")) {
                return false;
            }
            if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
                opcode = minic_type_is_long_integer(common_type) ? "add" : "addw";
            } else if (minic_type_is_unsigned_integer(common_type)) {
                opcode = minic_type_is_long_integer(common_type) ? "divu" : "divuw";
            } else {
                opcode = minic_type_is_long_integer(common_type) ? "div" : "divw";
            }
            if (fprintf(file,
                        "  ld t0, 8(sp)\\n"
                        "  %s a0, t0, a0\\n",
                        opcode) < 0 ||
                !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
""",
            "integer lowering",
        ),
    ],
)

print("staged /= compound assignment expression semantics")
