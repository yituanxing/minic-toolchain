#!/usr/bin/env python3
"""Stage M47: add generic Core lowering/codegen for GNU fixed-register reads."""

from __future__ import annotations

from pathlib import Path


FILES = {
    "ir_h": Path("src/core/core_ir.h"),
    "ir_c": Path("src/core/core_ir.c"),
    "lower": Path("src/core/core_lower.c"),
    "codegen": Path("src/target/riscv64/core_codegen.c"),
}
MARKER = "MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    texts = {name: path.read_text() for name, path in FILES.items()}
    marker_state = {name: MARKER in text for name, text in texts.items()}
    if all(marker_state.values()):
        print("M47 fixed-register Core lowering already applied")
        return 0
    if any(marker_state.values()):
        raise SystemExit(f"partial M47 state: {marker_state}")

    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,\n    MINIC_CORE_INSTRUCTION_PARAMETER,\n    MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT,\n""",
        """    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,\n    MINIC_CORE_INSTRUCTION_PARAMETER,\n    MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ,\n    MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT,\n""",
        "core_ir.h instruction kind",
    )
    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """        MinicCoreValueId operand;\n        size_t parameter_index;\n        struct {\n            size_t parameter_index;\n""",
        """        MinicCoreValueId operand;\n        size_t parameter_index;\n        size_t fixed_register_binding_id;\n        struct {\n            size_t parameter_index;\n""",
        "core_ir.h instruction payload",
    )

    texts["ir_c"] = replace_once(
        texts["ir_c"],
        """    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.parameter_index < function->parameter_count &&\n               minic_type_equal(function->parameter_types[instruction->value.parameter_index],\n                                instruction->type);\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        """    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.parameter_index < function->parameter_count &&\n               minic_type_equal(function->parameter_types[instruction->value.parameter_index],\n                                instruction->type);\n    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.fixed_register_binding_id != SIZE_MAX &&\n               (minic_type_is_integer(instruction->type) ||\n                minic_type_is_pointer(instruction->type));\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        "core_ir.c verifier",
    )
    texts["ir_c"] = replace_once(
        texts["ir_c"],
        """    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = parameter %zu\\n\",\n                       instruction->result,\n                       instruction->value.parameter_index) >= 0;\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        """    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = parameter %zu\\n\",\n                       instruction->result,\n                       instruction->value.parameter_index) >= 0;\n    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = fixed.register.read binding=%zu\\n\",\n                       instruction->result,\n                       instruction->value.fixed_register_binding_id) >= 0;\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        "core_ir.c dump",
    )

    texts["lower"] = replace_once(
        texts["lower"],
        """    if (expression->kind == MINIC_EXPRESSION_CALL) {\n        return lower_direct_call(context, expression, value_id);\n    }\n    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n""",
        """    if (expression->kind == MINIC_EXPRESSION_CALL) {\n        return lower_direct_call(context, expression, value_id);\n    }\n    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {\n        const MinicFixedRegisterBinding *binding;\n\n        binding = minic_c0_program_fixed_register_binding(\n            context->body->program, expression->value.fixed_register_binding_id);\n        if (binding == NULL || binding->register_name == NULL ||\n            binding->register_name_length == 0U) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        if (!core_memory_scalar_type(binding->type) ||\n            !minic_type_equal(binding->type, expression->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;\n        instruction.span = expression->span;\n        instruction.type = expression->type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.fixed_register_binding_id =\n            expression->value.fixed_register_binding_id;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n""",
        "core_lower.c fixed-register lowering",
    )

    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_STORE:\n        return true;\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n""",
        """    case MINIC_CORE_INSTRUCTION_STORE:\n        return true;\n    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {\n        const MinicFixedRegisterBinding *binding;\n\n        if (program == NULL) {\n            return false;\n        }\n        binding = minic_c0_program_fixed_register_binding(\n            program, instruction->value.fixed_register_binding_id);\n        return binding != NULL && binding->register_name != NULL &&\n               binding->register_name_length != 0U && core_scalar_type(binding->type) &&\n               minic_type_equal(binding->type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n""",
        "core_codegen.c support",
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        """    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {\n        const MinicFixedRegisterBinding *binding;\n\n        if (program == NULL) {\n            return false;\n        }\n        binding = minic_c0_program_fixed_register_binding(\n            program, instruction->value.fixed_register_binding_id);\n        if (binding == NULL || binding->register_name == NULL ||\n            binding->register_name_length == 0U || !core_scalar_type(binding->type) ||\n            !minic_type_equal(binding->type, instruction->type) ||\n            fprintf(file, \"  mv t0, %s\\n\", binding->register_name) < 0) {\n            return false;\n        }\n        if (minic_type_is_integer(instruction->type) &&\n            !minic_riscv64_emit_integer_conversion_for_program(\n                file, program, instruction->type, \"t0\")) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    }\n    case MINIC_CORE_INSTRUCTION_PARAMETER:\n        return emit_parameter(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:\n""",
        "core_codegen.c emission",
    )

    for name, path in FILES.items():
        path.write_text(texts[name])
    print("M47 fixed-register Core lowering applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
