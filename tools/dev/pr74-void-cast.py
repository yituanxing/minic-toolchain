#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    MINIC_EXPRESSION_BITCAST,
    MINIC_EXPRESSION_CONVERSION,
    MINIC_EXPRESSION_SUBSCRIPT,
""",
    """    MINIC_EXPRESSION_BITCAST,
    MINIC_EXPRESSION_CONVERSION,
    MINIC_EXPRESSION_DISCARD,
    MINIC_EXPRESSION_SUBSCRIPT,
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    if (operand == NULL ||
        (!minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
""",
    """    if (operand == NULL ||
        (!minic_type_is_void(target_type) &&
         !minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_LVALUE_READ:
""",
    """    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """static bool append_normalized_conversion(MinicC0Program *rewritten,
                                         const MinicExpression *cast_expression,
                                         MinicExpressionId mapped_operand,
                                         MinicExpressionId *normalized_id) {
""",
    """static bool append_normalized_discard(MinicC0Program *rewritten,
                                      const MinicExpression *cast_expression,
                                      MinicExpressionId mapped_operand,
                                      MinicExpressionId *normalized_id) {
    MinicExpression discard;

    (void)memset(&discard, 0, sizeof(discard));
    discard.kind = MINIC_EXPRESSION_DISCARD;
    discard.span = cast_expression->span;
    discard.type = minic_type_void();
    discard.value_category = MINIC_VALUE_RVALUE;
    discard.value.unary.operand = mapped_operand;
    return minic_c0_program_add_expression(rewritten, &discard, normalized_id);
}

static bool append_normalized_conversion(MinicC0Program *rewritten,
                                         const MinicExpression *cast_expression,
                                         MinicExpressionId mapped_operand,
                                         MinicExpressionId *normalized_id) {
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """    operand_expression = &rewritten->expressions[mapped_operand];

    if ((minic_type_is_double(cast_expression->type) &&
""",
    """    operand_expression = &rewritten->expressions[mapped_operand];

    if (minic_type_is_void(cast_expression->type)) {
        return append_normalized_discard(
            rewritten, cast_expression, mapped_operand, normalized_id);
    }

    if ((minic_type_is_double(cast_expression->type) &&
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_CAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_PARSED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               (minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
""",
    """    case MINIC_EXPRESSION_CAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_PARSED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               (minic_type_is_void(expression->type) ||
                minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_CONVERSION:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               ((minic_type_is_double(expression->type) &&
                 (minic_type_is_integer(operand->type) || minic_type_is_float(operand->type))) ||
                (minic_type_is_integer(expression->type) && minic_type_is_double(operand->type)));
    case MINIC_EXPRESSION_SUBSCRIPT:
""",
    """    case MINIC_EXPRESSION_CONVERSION:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               ((minic_type_is_double(expression->type) &&
                 (minic_type_is_integer(operand->type) || minic_type_is_float(operand->type))) ||
                (minic_type_is_integer(expression->type) && minic_type_is_double(operand->type)));
    case MINIC_EXPRESSION_DISCARD:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
    case MINIC_EXPRESSION_SUBSCRIPT:
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_BITCAST:
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_CONVERSION: {
""",
    """    case MINIC_EXPRESSION_BITCAST:
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_DISCARD:
        return minic_type_is_void(expression->type) &&
               minic_riscv64_emit_expression(
                   file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_CONVERSION: {
""",
)

print("staged explicit cast-to-void normalization as side-effect-preserving discard")
