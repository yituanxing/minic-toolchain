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
    """bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,\n                                            MinicType target_type,\n                                            MinicExpressionId source_expression_id,\n                                            MinicType *result);\n""",
    """bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,\n                                            MinicType target_type,\n                                            MinicExpressionId source_expression_id,\n                                            MinicType *result);\nbool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,\n                                                  const MinicTargetInfo *target,\n                                                  MinicType source_type,\n                                                  MinicType destination_type);\n""",
)

replace_once(
    "src/frontend/expression_semantics.c",
    """bool minic_c0_conditional_result_type(const MinicC0Program *program,\n                                      const MinicTargetInfo *target,\n                                      MinicExpressionId when_true_expression_id,\n                                      MinicExpressionId when_false_expression_id,\n                                      MinicType *result) {\n""",
    """bool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,\n                                                  const MinicTargetInfo *target,\n                                                  MinicType source_type,\n                                                  MinicType destination_type) {\n    unsigned int source_bits;\n    unsigned int destination_bits;\n    bool source_signed;\n    bool destination_signed;\n\n    if (program == NULL || target == NULL || !minic_type_is_integer(source_type) ||\n        !minic_type_is_integer(destination_type) ||\n        !minic_target_info_integer_width(target, program, source_type, &source_bits) ||\n        !minic_target_info_integer_width(\n            target, program, destination_type, &destination_bits) ||\n        source_bits == 0U || destination_bits == 0U) {\n        return false;\n    }\n    if (minic_type_is_bool_integer(source_type)) {\n        return destination_bits >= 1U;\n    }\n    source_signed = minic_type_is_signed_integer(source_type);\n    destination_signed = minic_type_is_signed_integer(destination_type);\n    if (source_signed) {\n        return destination_signed && destination_bits >= source_bits;\n    }\n    if (!destination_signed) {\n        return destination_bits >= source_bits;\n    }\n    return destination_bits > source_bits;\n}\n\nbool minic_c0_conditional_result_type(const MinicC0Program *program,\n                                      const MinicTargetInfo *target,\n                                      MinicExpressionId when_true_expression_id,\n                                      MinicExpressionId when_false_expression_id,\n                                      MinicType *result) {\n""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    if (left == NULL || right == NULL || result_pointer == NULL ||\n        !minic_type_pointee(result_pointer->type, &result_type) ||\n        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type)) {\n        minic_parser_error(parser,\n                           \"overflow builtin currently requires matching non-bool integer operands \"\n                           \"and result pointee\");\n        return false;\n    }\n""",
    """    if (left == NULL || right == NULL || result_pointer == NULL ||\n        !minic_type_pointee(result_pointer->type, &result_type) ||\n        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            parser->program, parser->target_info, left->type, result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            parser->program, parser->target_info, right->type, result_type)) {\n        minic_parser_error(\n            parser,\n            \"overflow builtin currently requires integer operand ranges representable by the \"\n            \"non-bool result pointee type\");\n        return false;\n    }\n""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """               minic_type_pointee(result_pointer->type, &result_type) &&\n               minic_type_is_integer(result_type) && !minic_type_is_bool_integer(result_type) &&\n               minic_type_equal(overflow_left->type, result_type) &&\n               minic_type_equal(overflow_right->type, result_type);\n""",
    """               minic_type_pointee(result_pointer->type, &result_type) &&\n               minic_type_is_integer(result_type) && !minic_type_is_bool_integer(result_type) &&\n               minic_c0_integer_range_representable_in_type(\n                   program, target, overflow_left->type, result_type) &&\n               minic_c0_integer_range_representable_in_type(\n                   program, target, overflow_right->type, result_type);\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """#include \"target/riscv64/codegen_internal.h\"\n#include \"target/data_layout.h\"\n""",
    """#include \"target/riscv64/codegen_internal.h\"\n#include \"frontend/expression_semantics.h\"\n#include \"target/data_layout.h\"\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    if (left == NULL || right == NULL || result_pointer == NULL ||\n        !minic_type_pointee(result_pointer->type, &result_type) ||\n        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type) ||\n        !minic_riscv64_type_layout(program, result_type, &result_size, &result_alignment) ||\n        result_size == 0U || result_size > 8U) {\n""",
    """    if (left == NULL || right == NULL || result_pointer == NULL ||\n        !minic_type_pointee(result_pointer->type, &result_type) ||\n        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            program, minic_default_target_info(), left->type, result_type) ||\n        !minic_c0_integer_range_representable_in_type(\n            program, minic_default_target_info(), right->type, result_type) ||\n        !minic_riscv64_type_layout(program, result_type, &result_size, &result_alignment) ||\n        result_size == 0U || result_size > 8U) {\n""",
)

path = root / "src/frontend/expression_semantics.c"
if not path.exists():
    raise SystemExit("expression semantics file missing")
