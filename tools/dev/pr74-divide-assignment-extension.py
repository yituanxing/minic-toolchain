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
    """    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_PLUS_EQUAL) {
        const MinicExpression *target_expression;
""",
    """    if (minimum_precedence == 0U &&
        (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL)) {
        const MinicExpression *target_expression;
""",
)
replace_once(
    "src/frontend/parser_expression.c",
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
)
replace_once(
    "src/frontend/parser_expression.c",
    """        if (minic_type_is_pointer(target_type)) {
            MinicType pointee_type;

            if (!minic_type_is_integer(value_expression->type) ||
""",
    """        if (minic_type_is_pointer(target_type)) {
            MinicType pointee_type;

            if (compound_operator != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value_expression->type) ||
""",
)
replace_once(
    "src/frontend/parser_expression.c",
    """        assignment.value.binary.operator_kind = MINIC_BINARY_ADD;
        assignment.value.binary.left = left;
""",
    """        assignment.value.binary.operator_kind = compound_operator;
        assignment.value.binary.left = left;
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type) ||
            expression->value.binary.operator_kind != MINIC_BINARY_ADD) {
""",
    """            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type) ||
            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE)) {
""",
)
replace_once(
    "src/frontend/ast_verifier.c",
    """        if (minic_type_is_pointer(left->type)) {
            return minic_type_is_integer(right->type);
        }
""",
    """        if (minic_type_is_pointer(left->type)) {
            return expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(right->type);
        }
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
            !minic_type_equal(expression->type, target->type) ||
""",
    """        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
             expression->value.binary.operator_kind != MINIC_BINARY_DIVIDE) ||
            !minic_type_equal(expression->type, target->type) ||
""",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if (!minic_type_is_integer(value->type) ||
""",
    """        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if (expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
                !minic_type_is_integer(value->type) ||
""",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
""",
    """        } else {
            MinicType common_type;
            const char *opcode;

            if (!minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
""",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
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
)

print("staged /= compound assignment expression semantics")
