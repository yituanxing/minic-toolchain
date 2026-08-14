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
    """    if (minic_type_is_double(left) && minic_type_is_double(right) &&
        binary_is_double_arithmetic(kind)) {
        *result = minic_type_double();
        return true;
    }
""",
    """    if (has_double_operand && has_numeric_operands && binary_is_double_arithmetic(kind)) {
        *result = minic_type_double();
        return true;
    }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    if (minic_type_is_double(left->type) && minic_type_is_double(right->type) &&
        binary_is_double_arithmetic(expression->value.binary.operator_kind)) {
        return minic_type_is_double(expression->type);
    }
""",
    """    if ((minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
        (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
        (minic_type_is_double(left->type) || minic_type_is_double(right->type)) &&
        binary_is_double_arithmetic(expression->value.binary.operator_kind)) {
        return minic_type_is_double(expression->type);
    }
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """static bool minic_riscv64_emit_double_binary(FILE *file, MinicBinaryOperator operator_kind) {
    const char *instruction;

    switch (operator_kind) {
""",
    """static bool minic_riscv64_emit_double_binary(FILE *file,
                                             MinicBinaryOperator operator_kind,
                                             MinicType left_type,
                                             MinicType right_type) {
    const char *instruction;

    switch (operator_kind) {
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    return fprintf(file,
                   \"  fmv.d.x ft0, t0\\n\"
                   \"  fmv.d.x ft1, a0\\n\"
                   \"  %s ft0, ft0, ft1\\n\"
                   \"  fmv.x.d a0, ft0\\n\",
                   instruction) >= 0;
}
""",
    """    if (minic_type_is_double(left_type)) {
        if (fprintf(file, \"  fmv.d.x ft0, t0\\n\") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, left_type, \"t0\", \"ft0\")) {
        return false;
    }
    if (minic_type_is_double(right_type)) {
        if (fprintf(file, \"  fmv.d.x ft1, a0\\n\") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, right_type, \"a0\", \"ft1\")) {
        return false;
    }
    return fprintf(file,
                   \"  %s ft0, ft0, ft1\\n\"
                   \"  fmv.x.d a0, ft0\\n\",
                   instruction) >= 0;
}
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        if (minic_type_is_double(left->type) && minic_type_is_double(right->type) &&
            minic_type_is_double(expression->type)) {
            return minic_riscv64_emit_double_binary(file, expression->value.binary.operator_kind);
        }
""",
    """        if (minic_type_is_double(expression->type) &&
            (minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
            (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
            (minic_type_is_double(left->type) || minic_type_is_double(right->type))) {
            return minic_riscv64_emit_double_binary(file,
                                                    expression->value.binary.operator_kind,
                                                    left->type,
                                                    right->type);
        }
""",
)

print("staged mixed integer/double arithmetic with RV64 operand conversion")
