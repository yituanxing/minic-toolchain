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
    "src/frontend/type.c",
    """    if (minic_type_is_double(target) &&
        (minic_type_is_integer(source) || minic_type_is_float(source))) {
        return true;
    }
    return minic_type_is_pointer(target) && minic_type_is_pointer(source);
}
""",
    """    if (minic_type_is_double(target) &&
        (minic_type_is_integer(source) || minic_type_is_float(source))) {
        return true;
    }
    if ((minic_type_is_pointer(target) && minic_type_is_integer(source)) ||
        (minic_type_is_integer(target) && minic_type_is_pointer(source))) {
        return true;
    }
    return minic_type_is_pointer(target) && minic_type_is_pointer(source);
}
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """    if (minic_type_is_pointer(cast_expression->type) &&
        minic_type_is_pointer(operand_expression->type)) {
        return append_normalized_bitcast(rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if (minic_type_is_integer(cast_expression->type) &&
""",
    """    if ((minic_type_is_pointer(cast_expression->type) &&
         (minic_type_is_pointer(operand_expression->type) ||
          minic_type_is_integer(operand_expression->type))) ||
        (minic_type_is_integer(cast_expression->type) &&
         minic_type_is_pointer(operand_expression->type))) {
        return append_normalized_bitcast(rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if (minic_type_is_integer(cast_expression->type) &&
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_BITCAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_pointer(expression->type) &&
               ((minic_type_is_pointer(operand->type) &&
                 minic_type_cast_compatible(expression->type, operand->type)) ||
                expression_is_integer_zero(operand));
""",
    """    case MINIC_EXPRESSION_BITCAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_cast_compatible(expression->type, operand->type) &&
               ((minic_type_is_pointer(expression->type) &&
                 (minic_type_is_pointer(operand->type) || minic_type_is_integer(operand->type))) ||
                (minic_type_is_integer(expression->type) &&
                 minic_type_is_pointer(operand->type)));
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_BITCAST:
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.unary.operand);
""",
    """    case MINIC_EXPRESSION_BITCAST:
        if (!minic_riscv64_emit_expression(
                file, program, function, expression->value.unary.operand)) {
            return false;
        }
        if (minic_type_is_integer(expression->type)) {
            return minic_riscv64_emit_integer_conversion(file, expression->type, \"a0\");
        }
        return minic_type_is_pointer(expression->type);
""",
)

print("staged explicit pointer/integer casts without changing assignment compatibility")
