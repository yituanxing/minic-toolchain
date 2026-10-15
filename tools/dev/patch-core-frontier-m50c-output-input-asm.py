#!/usr/bin/env python3
"""Stage M50c: lower one register output plus one scalar input inline asm."""

from pathlib import Path

FILES = {
    "ir_h": Path("src/core/core_ir.h"),
    "ir_c": Path("src/core/core_ir.c"),
    "lower": Path("src/core/core_lower.c"),
    "codegen": Path("src/target/riscv64/core_codegen.c"),
}
MARKER = "MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    texts = {name: path.read_text() for name, path in FILES.items()}
    state = {name: MARKER in text for name, text in texts.items()}
    if all(state.values()):
        print("M50c register-output/input inline asm already applied")
        return 0
    if any(state.values()):
        raise SystemExit(f"partial M50c state: {state}")

    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,\n""",
        """    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,\n""",
        "core_ir.h instruction kind",
    )
    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """        MinicCoreInlineAsmId inline_asm_id;\n        struct {\n            MinicCoreInlineAsmId inline_asm_id;\n            MinicCoreValueId operand;\n        } scalar_input_inline_asm;\n""",
        """        MinicCoreInlineAsmId inline_asm_id;\n        struct {\n            MinicCoreInlineAsmId inline_asm_id;\n            MinicCoreValueId operand;\n        } register_output_input_inline_asm;\n        struct {\n            MinicCoreInlineAsmId inline_asm_id;\n            MinicCoreValueId operand;\n        } scalar_input_inline_asm;\n""",
        "core_ir.h payload",
    )

    ir_verify_anchor = """    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreValueId operand;\n"""
    ir_verify_case = """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreValueId operand;\n\n        operand = instruction->value.register_output_input_inline_asm.operand;\n        if (!instruction_result_is_valid(function, instruction) ||\n            (!minic_type_is_integer(instruction->type) &&\n             !minic_type_is_pointer(instruction->type)) ||\n            operand >= function->value_count || !available_values[operand] ||\n            (!minic_type_is_integer(function->values[operand].type) &&\n             !minic_type_is_pointer(function->values[operand].type)) ||\n            instruction->value.register_output_input_inline_asm.inline_asm_id >=\n                function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[\n            instruction->value.register_output_input_inline_asm.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n"""
    texts["ir_c"] = replace_once(
        texts["ir_c"], ir_verify_anchor, ir_verify_case + ir_verify_anchor, "core_ir.c verifier"
    )

    ir_dump_anchor = """    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreInlineAsmId inline_asm_id;\n"""
    ir_dump_case = """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreInlineAsmId inline_asm_id;\n\n        inline_asm_id = instruction->value.register_output_input_inline_asm.inline_asm_id;\n        if (function == NULL || inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[inline_asm_id];\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = asm.register_output_input id=%\" PRIu32\n                       \" %%%\" PRIu32 \"%s%s\\n\",\n                       instruction->result,\n                       inline_asm_id,\n                       instruction->value.register_output_input_inline_asm.operand,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n"""
    texts["ir_c"] = replace_once(
        texts["ir_c"], ir_dump_anchor, ir_dump_case + ir_dump_anchor, "core_ir.c dump"
    )

    lower_anchor = """    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->outputs != NULL &&\n        source->output_count == 1U && source->input_count == 0U &&\n"""
    lower_case = """    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&\n        source->output_count == 1U && source->input_count == 1U &&\n        source->label_count == 0U && source->register_clobber_count == 0U &&\n        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {\n        const MinicInlineAsmOperand *input;\n        const MinicInlineAsmOperand *output;\n        const MinicExpression *input_expression;\n        const MinicExpression *output_expression;\n        const MinicLocal *local;\n        MinicCoreValueId address_id;\n        MinicCoreValueId input_value;\n        MinicCoreValueId output_value;\n        MinicCoreLowerStatus status;\n        MinicType input_type;\n        MinicType output_type;\n        bool input_register_constraint;\n        bool output_register_constraint;\n\n        output = &source->outputs[0];\n        input = &source->inputs[0];\n        output_expression = minic_c0_program_expression(context->body->program, output->expression);\n        input_expression = minic_c0_program_expression(context->body->program, input->expression);\n        output_register_constraint =\n            output->constraint_text != NULL &&\n            ((output->constraint_length == 2U &&\n              memcmp(output->constraint_text, \"=r\", 2U) == 0) ||\n             (output->constraint_length == 3U &&\n              memcmp(output->constraint_text, \"=&r\", 3U) == 0));\n        input_register_constraint =\n            input->constraint_text != NULL &&\n            ((input->constraint_length == 1U && memcmp(input->constraint_text, \"r\", 1U) == 0) ||\n             (input->constraint_length == 2U && memcmp(input->constraint_text, \"rK\", 2U) == 0));\n        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&\n            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&\n            output_register_constraint && input_register_constraint &&\n            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&\n            output_expression->value_category == MINIC_VALUE_LVALUE &&\n            !minic_type_is_const(output_expression->type) &&\n            !minic_type_is_volatile(output_expression->type) &&\n            minic_type_unqualified(output_expression->type, &output_type) &&\n            core_memory_scalar_type(output_type) && input_expression != NULL &&\n            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {\n            local = minic_c0_program_local(\n                context->body->program, output_expression->value.local_id);\n            if (local == NULL) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!local->is_array &&\n                minic_c0_program_local_fixed_register_binding(\n                    context->body->program, output_expression->value.local_id) == NULL &&\n                minic_type_equal(local->type, output_expression->type)) {\n                status = lower_expression(context, input->expression, &input_value);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n                if (input_value >= context->function->value_count ||\n                    !minic_type_equal(context->function->values[input_value].type, input_type)) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                if (!minic_core_function_add_opaque_inline_asm(context->function,\n                                                               source->template_text,\n                                                               source->template_length,\n                                                               source->is_volatile,\n                                                               source->has_memory_clobber,\n                                                               &inline_asm_id)) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                (void)memset(&instruction, 0, sizeof(instruction));\n                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM;\n                instruction.span = statement->span;\n                instruction.type = output_type;\n                instruction.result = MINIC_CORE_VALUE_INVALID;\n                instruction.value.register_output_input_inline_asm.inline_asm_id = inline_asm_id;\n                instruction.value.register_output_input_inline_asm.operand = input_value;\n                if (!minic_core_function_append_value_instruction(\n                        context->function, context->block_id, &instruction, &output_value)) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                (void)memset(&instruction, 0, sizeof(instruction));\n                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;\n                instruction.span = statement->span;\n                instruction.type = minic_type_void();\n                instruction.result = MINIC_CORE_VALUE_INVALID;\n                instruction.value.store.address = address_id;\n                instruction.value.store.stored_value = output_value;\n                instruction.value.store.is_volatile = false;\n                return minic_core_function_append_effect_instruction(\n                           context->function, context->block_id, &instruction)\n                           ? MINIC_CORE_LOWER_OK\n                           : MINIC_CORE_LOWER_ERROR;\n            }\n        }\n    }\n\n"""
    texts["lower"] = replace_once(
        texts["lower"], lower_anchor, lower_case + lower_anchor, "core_lower.c output/input asm"
    )

    support_anchor = """static bool core_scalar_input_inline_asm_supported(\n    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {\n"""
    support_helper = """static bool core_register_output_input_inline_asm_supported(\n    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    MinicCoreValueId operand;\n    size_t index;\n\n    if (function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||\n        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||\n        instruction->value.register_output_input_inline_asm.inline_asm_id >=\n            function->inline_asm_count) {\n        return false;\n    }\n    operand = instruction->value.register_output_input_inline_asm.operand;\n    if (operand >= function->value_count ||\n        (!minic_type_is_integer(function->values[operand].type) &&\n         !minic_type_is_pointer(function->values[operand].type))) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[\n        instruction->value.register_output_input_inline_asm.inline_asm_id];\n    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||\n        !inline_asm->is_volatile) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            continue;\n        }\n        if (index + 1U >= inline_asm->template_length ||\n            (inline_asm->template_text[index + 1U] != '%' &&\n             inline_asm->template_text[index + 1U] != '0' &&\n             inline_asm->template_text[index + 1U] != '1')) {\n            return false;\n        }\n        index += 1U;\n    }\n    return true;\n}\n\n"""
    texts["codegen"] = replace_once(
        texts["codegen"], support_anchor, support_helper + support_anchor, "core_codegen.c helper"
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return core_register_output_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n""",
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return core_register_output_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:\n        return core_register_output_input_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n""",
        "core_codegen.c supported switch",
    )

    emit_anchor = """static bool emit_scalar_input_inline_asm(\n    FILE *file,\n"""
    emit_helper = """static bool emit_register_output_input_inline_asm(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicCoreFunction *function,\n    const MinicRiscv64CoreFrame *frame,\n    const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    size_t index;\n\n    if (file == NULL || frame == NULL ||\n        !core_register_output_input_inline_asm_supported(function, instruction) ||\n        !load_core_value(\n            file, frame, instruction->value.register_output_input_inline_asm.operand, \"t1\")) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[\n        instruction->value.register_output_input_inline_asm.inline_asm_id];\n    if (fprintf(file, \"  \") < 0) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {\n                return false;\n            }\n            continue;\n        }\n        index += 1U;\n        if (inline_asm->template_text[index] == '%') {\n            if (fputc('%', file) == EOF) {\n                return false;\n            }\n        } else if (inline_asm->template_text[index] == '0') {\n            if (fprintf(file, \"t0\") < 0) {\n                return false;\n            }\n        } else if (inline_asm->template_text[index] == '1') {\n            if (fprintf(file, \"t1\") < 0) {\n                return false;\n            }\n        } else {\n            return false;\n        }\n    }\n    if (fputc('\\n', file) == EOF ||\n        (minic_type_is_integer(instruction->type) &&\n         !minic_riscv64_emit_integer_conversion_for_program(\n             file, program, instruction->type, \"t0\"))) {\n        return false;\n    }\n    return store_core_value(file, frame, instruction->result, \"t0\");\n}\n\n"""
    texts["codegen"] = replace_once(
        texts["codegen"], emit_anchor, emit_helper + emit_anchor, "core_codegen.c emitter"
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return emit_register_output_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n""",
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return emit_register_output_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:\n        return emit_register_output_input_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n""",
        "core_codegen.c emit switch",
    )

    for name, path in FILES.items():
        path.write_text(texts[name])
    print("M50c register-output/input inline asm applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
