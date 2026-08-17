#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(relative_path: str, old: str, new: str) -> None:
    path = root / relative_path
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative_path}: anchor count={count}")
    path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/expression_semantics.h",
    """bool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,\n                                                  const MinicTargetInfo *target,\n                                                  MinicType source_type,\n                                                  MinicType destination_type);\n""",
    """bool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,\n                                                  const MinicTargetInfo *target,\n                                                  MinicType source_type,\n                                                  MinicType destination_type);\nbool minic_c0_integer_expression_representable_in_type(const MinicC0Program *program,\n                                                       const MinicTargetInfo *target,\n                                                       MinicExpressionId expression_id,\n                                                       MinicType destination_type);\n""",
)

replace_once(
    "src/frontend/expression_semantics.c",
    """#include \"frontend/expression_semantics.h\"\n""",
    """#include \"frontend/expression_semantics.h\"\n#include \"frontend/const_eval.h\"\n\n#include <limits.h>\n""",
)

replace_once(
    "src/frontend/expression_semantics.c",
    """bool minic_c0_conditional_result_type(const MinicC0Program *program,\n                                      const MinicTargetInfo *target,\n                                      MinicExpressionId when_true_expression_id,\n                                      MinicExpressionId when_false_expression_id,\n                                      MinicType *result) {\n""",
    """static bool integer_constant_representable_in_type(const MinicC0Program *program,\n                                                   const MinicTargetInfo *target,\n                                                   const MinicConstValue *value,\n                                                   MinicType destination_type) {\n    int64_t signed_value;\n    unsigned int destination_bits;\n\n    if (program == NULL || target == NULL || value == NULL ||\n        !minic_type_is_integer(value->type) || !minic_type_is_integer(destination_type) ||\n        !minic_const_value_as_int64(program, target, value, &signed_value) ||\n        !minic_target_info_integer_width(\n            target, program, destination_type, &destination_bits) ||\n        destination_bits == 0U) {\n        return false;\n    }\n    if (minic_type_is_bool_integer(destination_type)) {\n        return signed_value == 0 || signed_value == 1;\n    }\n    if (minic_type_is_signed_integer(destination_type)) {\n        int64_t minimum;\n        int64_t maximum;\n\n        if (destination_bits >= 64U) {\n            return true;\n        }\n        minimum = -(INT64_C(1) << (destination_bits - 1U));\n        maximum = (INT64_C(1) << (destination_bits - 1U)) - INT64_C(1);\n        return signed_value >= minimum && signed_value <= maximum;\n    }\n    if (signed_value < 0) {\n        return false;\n    }\n    if (destination_bits >= 63U) {\n        return true;\n    }\n    return (uint64_t)signed_value <=\n           (UINT64_C(1) << destination_bits) - UINT64_C(1);\n}\n\nbool minic_c0_integer_expression_representable_in_type(const MinicC0Program *program,\n                                                       const MinicTargetInfo *target,\n                                                       MinicExpressionId expression_id,\n                                                       MinicType destination_type) {\n    const MinicExpression *expression;\n    MinicConstValue constant;\n\n    expression = minic_c0_program_expression(program, expression_id);\n    if (expression == NULL || !minic_type_is_integer(expression->type) ||\n        !minic_type_is_integer(destination_type)) {\n        return false;\n    }\n    if (minic_c0_integer_range_representable_in_type(\n            program, target, expression->type, destination_type)) {\n        return true;\n    }\n    return minic_const_eval_integer(program, target, expression_id, &constant) &&\n           integer_constant_representable_in_type(\n               program, target, &constant, destination_type);\n}\n\nbool minic_c0_conditional_result_type(const MinicC0Program *program,\n                                      const MinicTargetInfo *target,\n                                      MinicExpressionId when_true_expression_id,\n                                      MinicExpressionId when_false_expression_id,\n                                      MinicType *result) {\n""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """        !minic_c0_integer_range_representable_in_type(\n            parser->program, parser->target_info, left->type, result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            parser->program, parser->target_info, right->type, result_type)) {\n""",
    """        !minic_c0_integer_expression_representable_in_type(\n            parser->program, parser->target_info, left_id, result_type) ||\n        !minic_c0_integer_expression_representable_in_type(\n            parser->program, parser->target_info, right_id, result_type)) {\n""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """               minic_c0_integer_range_representable_in_type(\n                   program, target, overflow_left->type, result_type) &&\n               minic_c0_integer_range_representable_in_type(\n                   program, target, overflow_right->type, result_type);\n""",
    """               minic_c0_integer_expression_representable_in_type(\n                   program, target, expression->value.overflow.left, result_type) &&\n               minic_c0_integer_expression_representable_in_type(\n                   program, target, expression->value.overflow.right, result_type);\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        !minic_c0_integer_range_representable_in_type(\n            program, minic_default_target_info(), left->type, result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            program, minic_default_target_info(), right->type, result_type) ||\n""",
    """        !minic_c0_integer_expression_representable_in_type(\n            program,\n            minic_default_target_info(),\n            expression->value.overflow.left,\n            result_type) ||\n        !minic_c0_integer_expression_representable_in_type(\n            program,\n            minic_default_target_info(),\n            expression->value.overflow.right,\n            result_type) ||\n""",
)
