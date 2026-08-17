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
    "src/target/riscv64/codegen_internal.h",
    """bool minic_riscv64_emit_record_return_value(FILE *file,\n                                            const MinicC0Program *program,\n                                            const MinicFunction *function,\n                                            const MinicRiscv64FunctionLayout *function_layout,\n                                            MinicExpressionId source_id,\n                                            size_t result_pointer_offset);\n""",
    """bool minic_riscv64_emit_record_return_value(FILE *file,\n                                            const MinicC0Program *program,\n                                            const MinicFunction *function,\n                                            const MinicRiscv64FunctionLayout *function_layout,\n                                            MinicExpressionId source_id,\n                                            size_t result_pointer_offset);\nbool minic_riscv64_emit_small_record_return_value(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicExpressionId source_id,\n    size_t slot_count);\n""",
)

# Keep the temporary materializer aligned with the canonical copy-source predicate.
# Assignment expressions are record producers too: evaluating one leaves the written
# target address in a0, which can then be copied into the shared temporary shape.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    if (minic_c0_record_value_is_address_backed(program, expression_id)) {\n        if (!minic_riscv64_emit_address_backed_record_value(\n                file, program, function, function_layout, expression_id) ||\n""",
    """    if (minic_c0_record_value_is_address_backed(program, expression_id) ||\n        expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {\n        if (!minic_riscv64_emit_address_backed_record_value(\n                file, program, function, function_layout, expression_id) ||\n""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """bool minic_riscv64_emit_record_copy_value(FILE *file,\n                                          const MinicC0Program *program,\n""",
    """bool minic_riscv64_emit_small_record_return_value(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicExpressionId source_id,\n    size_t slot_count) {\n    const MinicExpression *source;\n    size_t storage_size;\n    size_t temporary_size;\n\n    source = minic_c0_program_expression(program, source_id);\n    if (source == NULL || !minic_type_is_record(source->type) ||\n        !minic_c0_record_value_is_copy_source(program, source_id) || slot_count == 0U ||\n        slot_count > 2U ||\n        !minic_riscv64_type_layout(program, source->type, &storage_size, &temporary_size) ||\n        storage_size == 0U || storage_size > 16U || storage_size > SIZE_MAX - 15U) {\n        return false;\n    }\n    temporary_size = (storage_size + 15U) & ~(size_t)15U;\n    if (!minic_riscv64_emit_record_value_temporary(\n            file, program, function, function_layout, source_id, storage_size, temporary_size) ||\n        fprintf(file, \"  mv t0, sp\\n\") < 0 ||\n        !minic_riscv64_emit_integer_aggregate_load_chunk(\n            file, program, source->type, 0U, \"a0\", \"t0\") ||\n        (slot_count == 2U &&\n         !minic_riscv64_emit_integer_aggregate_load_chunk(\n             file, program, source->type, 1U, \"a1\", \"t0\"))) {\n        return false;\n    }\n    return minic_riscv64_emit_stack_release(file, temporary_size);\n}\n\nbool minic_riscv64_emit_record_copy_value(FILE *file,\n                                          const MinicC0Program *program,\n""",
)

replace_once(
    "src/target/riscv64/codegen_statement.c",
    """                } else if (value->kind != MINIC_EXPRESSION_CALL ||\n                           !minic_riscv64_emit_expression(\n                               file, program, function, function_layout, statement->expression)) {\n                    return false;\n                }\n""",
    """                } else if (value->kind == MINIC_EXPRESSION_CALL) {\n                    if (!minic_riscv64_emit_expression(\n                            file, program, function, function_layout, statement->expression)) {\n                        return false;\n                    }\n                } else if (!minic_c0_record_value_is_copy_source(\n                               program, statement->expression) ||\n                           !minic_riscv64_emit_small_record_return_value(\n                               file,\n                               program,\n                               function,\n                               function_layout,\n                               statement->expression,\n                               return_value.slot_count)) {\n                    return false;\n                }\n""",
)
