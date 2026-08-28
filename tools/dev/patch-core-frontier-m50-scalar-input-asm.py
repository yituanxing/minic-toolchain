#!/usr/bin/env python3
"""Stage M50: lower one scalar-input GNU inline asm operand through Core."""

from __future__ import annotations

from pathlib import Path

FILES = {
    "ir_h": Path("src/core/core_ir.h"),
    "ir_c": Path("src/core/core_ir.c"),
    "lower": Path("src/core/core_lower.c"),
    "codegen": Path("src/target/riscv64/core_codegen.c"),
}
MARKER = "MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    texts = {name: path.read_text() for name, path in FILES.items()}
    marker_state = {name: MARKER in text for name, text in texts.items()}
    if all(marker_state.values()):
        print("M50 scalar-input inline asm already applied")
        return 0
    if any(marker_state.values()):
        raise SystemExit(f"partial M50 state: {marker_state}")

    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n""",
        """    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n""",
        "core_ir.h instruction kind",
    )
    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """        MinicCoreInlineAsmId inline_asm_id;\n        struct {\n            MinicCoreCalleeId callee_id;\n""",
        """        MinicCoreInlineAsmId inline_asm_id;\n        struct {\n            MinicCoreInlineAsmId inline_asm_id;\n            MinicCoreValueId operand;\n        } scalar_input_inline_asm;\n        struct {\n            MinicCoreCalleeId callee_id;\n""",
        "core_ir.h scalar input payload",
    )

    verifier_anchor = """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            (!minic_type_is_integer(instruction->type) &&\n             !minic_type_is_pointer(instruction->type)) ||\n            instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n"""
    verifier_replacement = verifier_anchor.replace(
        "    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n",
        """    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreValueId operand;\n\n        operand = instruction->value.scalar_input_inline_asm.operand;\n        if (instruction->result != MINIC_CORE_VALUE_INVALID ||\n            !minic_type_is_void(instruction->type) || operand >= function->value_count ||\n            !available_values[operand] ||\n            (!minic_type_is_integer(function->values[operand].type) &&\n             !minic_type_is_pointer(function->values[operand].type)) ||\n            instruction->value.scalar_input_inline_asm.inline_asm_id >=\n                function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[\n            instruction->value.scalar_input_inline_asm.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
    )
    texts["ir_c"] = replace_once(
        texts["ir_c"], verifier_anchor, verifier_replacement, "core_ir.c verifier"
    )

    dump_anchor = """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = asm.register_output id=%\" PRIu32 \"%s%s\\n\",\n                       instruction->result,\n                       instruction->value.inline_asm_id,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n"""
    dump_replacement = dump_anchor.replace(
        "    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n",
        """    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n        MinicCoreInlineAsmId inline_asm_id;\n\n        inline_asm_id = instruction->value.scalar_input_inline_asm.inline_asm_id;\n        if (function == NULL || inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[inline_asm_id];\n        return fprintf(output,\n                       \"  asm.scalar_input id=%\" PRIu32 \" %%%\" PRIu32 \"%s%s\\n\",\n                       inline_asm_id,\n                       instruction->value.scalar_input_inline_asm.operand,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
    )
    texts["ir_c"] = replace_once(texts["ir_c"], dump_anchor, dump_replacement, "core_ir.c dump")

    lower_anchor = """    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||\n        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||\n        source->label_count != 0U || source->register_clobber_count != 0U) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n"""
    lower_block = """    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&\n        source->input_count == 1U && source->label_count == 0U &&\n        source->register_clobber_count == 0U &&\n        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {\n        const MinicInlineAsmOperand *input;\n        const MinicExpression *input_expression;\n        MinicCoreValueId input_value;\n        MinicCoreLowerStatus input_status;\n        MinicType input_type;\n        bool register_constraint;\n\n        input = &source->inputs[0];\n        input_expression = minic_c0_program_expression(context->body->program, input->expression);\n        register_constraint =\n            input->constraint_text != NULL &&\n            ((input->constraint_length == 1U &&\n              memcmp(input->constraint_text, \"r\", 1U) == 0) ||\n             (input->constraint_length == 2U &&\n              memcmp(input->constraint_text, \"rK\", 2U) == 0));\n        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY && register_constraint &&\n            input_expression != NULL &&\n            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {\n            input_status = lower_expression(context, input->expression, &input_value);\n            if (input_status != MINIC_CORE_LOWER_OK) {\n                return input_status;\n            }\n            if (input_value >= context->function->value_count ||\n                !minic_type_equal(context->function->values[input_value].type, input_type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!minic_core_function_add_opaque_inline_asm(context->function,\n                                                           source->template_text,\n                                                           source->template_length,\n                                                           source->is_volatile,\n                                                           source->has_memory_clobber,\n                                                           &inline_asm_id)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            (void)memset(&instruction, 0, sizeof(instruction));\n            instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM;\n            instruction.span = statement->span;\n            instruction.type = minic_type_void();\n            instruction.result = MINIC_CORE_VALUE_INVALID;\n            instruction.value.scalar_input_inline_asm.inline_asm_id = inline_asm_id;\n            instruction.value.scalar_input_inline_asm.operand = input_value;\n            return minic_core_function_append_effect_instruction(\n                       context->function, context->block_id, &instruction)\n                       ? MINIC_CORE_LOWER_OK\n                       : MINIC_CORE_LOWER_ERROR;\n        }\n    }\n\n"""
    texts["lower"] = replace_once(
        texts["lower"], lower_anchor, lower_block + lower_anchor, "core_lower.c scalar input asm"
    )

    support_anchor = """static bool core_instruction_supported(const MinicC0Program *program,\n                                       const MinicCoreFunction *function,\n                                       const MinicCoreInstruction *instruction) {\n"""
    support_helper = """static bool core_scalar_input_inline_asm_supported(\n    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    MinicCoreValueId operand;\n    size_t index;\n\n    if (function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM ||\n        instruction->result != MINIC_CORE_VALUE_INVALID ||\n        !minic_type_is_void(instruction->type) ||\n        instruction->value.scalar_input_inline_asm.inline_asm_id >= function->inline_asm_count) {\n        return false;\n    }\n    operand = instruction->value.scalar_input_inline_asm.operand;\n    if (operand >= function->value_count ||\n        (!minic_type_is_integer(function->values[operand].type) &&\n         !minic_type_is_pointer(function->values[operand].type))) {\n        return false;\n    }\n    inline_asm =\n        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];\n    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||\n        !inline_asm->is_volatile) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            continue;\n        }\n        if (index + 1U >= inline_asm->template_length ||\n            (inline_asm->template_text[index + 1U] != '%' &&\n             inline_asm->template_text[index + 1U] != '0')) {\n            return false;\n        }\n        index += 1U;\n    }\n    return true;\n}\n\n"""
    texts["codegen"] = replace_once(
        texts["codegen"], support_anchor, support_helper + support_anchor, "core_codegen.c support helper"
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return core_register_output_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return core_register_output_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n        return core_scalar_input_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
        "core_codegen.c supported switch",
    )

    emit_anchor = """static bool emit_instruction(FILE *file,\n                             const MinicC0Program *program,\n"""
    emit_helper = """static bool emit_scalar_input_inline_asm(\n    FILE *file,\n    const MinicCoreFunction *function,\n    const MinicRiscv64CoreFrame *frame,\n    const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    size_t index;\n\n    if (file == NULL || frame == NULL ||\n        !core_scalar_input_inline_asm_supported(function, instruction) ||\n        !load_core_value(file, frame, instruction->value.scalar_input_inline_asm.operand, \"t0\")) {\n        return false;\n    }\n    inline_asm =\n        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];\n    if (fprintf(file, \"  \") < 0) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {\n                return false;\n            }\n            continue;\n        }\n        index += 1U;\n        if (inline_asm->template_text[index] == '%') {\n            if (fputc('%', file) == EOF) {\n                return false;\n            }\n        } else if (inline_asm->template_text[index] == '0') {\n            if (fprintf(file, \"t0\") < 0) {\n                return false;\n            }\n        } else {\n            return false;\n        }\n    }\n    return fputc('\\n', file) != EOF;\n}\n\n"""
    texts["codegen"] = replace_once(
        texts["codegen"], emit_anchor, emit_helper + emit_anchor, "core_codegen.c scalar input emitter"
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return emit_register_output_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
        """    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return emit_register_output_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:\n        return emit_scalar_input_inline_asm(file, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n""",
        "core_codegen.c emit switch",
    )

    for name, path in FILES.items():
        path.write_text(texts[name])
    print("M50 scalar-input inline asm applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
