#!/usr/bin/env python3
"""Stage M48: Core one-register-output inline asm plus compiler barriers."""

from __future__ import annotations

from pathlib import Path


FILES = {
    "ir_h": Path("src/core/core_ir.h"),
    "ir_c": Path("src/core/core_ir.c"),
    "lower": Path("src/core/core_lower.c"),
    "codegen": Path("src/target/riscv64/core_codegen.c"),
}
MARKER = "MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    texts = {name: path.read_text() for name, path in FILES.items()}
    marker_state = {name: MARKER in text for name, text in texts.items()}
    if all(marker_state.values()):
        print("M48 inline-asm output lowering already applied")
        return 0
    if any(marker_state.values()):
        raise SystemExit(f"partial M48 state: {marker_state}")

    texts["ir_h"] = replace_once(
        texts["ir_h"],
        """    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_CALL\n""",
        """    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,\n    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,\n    MINIC_CORE_INSTRUCTION_CALL\n""",
        "core_ir.h instruction kinds",
    )

    texts["ir_c"] = replace_once(
        texts["ir_c"],
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (instruction->result != MINIC_CORE_VALUE_INVALID ||\n            !minic_type_is_void(instruction->type) ||\n            instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n    case MINIC_CORE_INSTRUCTION_CALL: {\n""",
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (instruction->result != MINIC_CORE_VALUE_INVALID ||\n            !minic_type_is_void(instruction->type) ||\n            instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            (!minic_type_is_integer(instruction->type) &&\n             !minic_type_is_pointer(instruction->type)) ||\n            instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n               inline_asm->is_volatile;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n        return instruction->result == MINIC_CORE_VALUE_INVALID &&\n               minic_type_is_void(instruction->type);\n    case MINIC_CORE_INSTRUCTION_CALL: {\n""",
        "core_ir.c verifier",
    )
    texts["ir_c"] = replace_once(
        texts["ir_c"],
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return fprintf(output,\n                       \"  asm.opaque id=%\" PRIu32 \"%s%s\\n\",\n                       instruction->value.inline_asm_id,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n    case MINIC_CORE_INSTRUCTION_CALL: {\n""",
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return fprintf(output,\n                       \"  asm.opaque id=%\" PRIu32 \"%s%s\\n\",\n                       instruction->value.inline_asm_id,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {\n        const MinicCoreInlineAsm *inline_asm;\n\n        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {\n            return false;\n        }\n        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = asm.register_output id=%\" PRIu32 \"%s%s\\n\",\n                       instruction->result,\n                       instruction->value.inline_asm_id,\n                       inline_asm->is_volatile ? \" volatile\" : \"\",\n                       inline_asm->has_memory_clobber ? \" memory\" : \"\") >= 0;\n    }\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n        return fprintf(output, \"  compiler.barrier\\n\") >= 0;\n    case MINIC_CORE_INSTRUCTION_CALL: {\n""",
        "core_ir.c dump",
    )

    texts["lower"] = replace_once(
        texts["lower"],
        """    if (local->is_array || local->is_register_storage ||\n        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n""",
        """    if (local->is_array ||\n        minic_c0_program_local_fixed_register_binding(context->body->program, local_id) != NULL ||\n        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n""",
        "core_lower.c ordinary register locals",
    )
    texts["lower"] = replace_once(
        texts["lower"],
        """    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);\n    if (source == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&\n""",
        """    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);\n    if (source == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n\n    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length == 0U && source->output_count == 0U &&\n        source->input_count == 0U && source->label_count == 0U &&\n        source->register_clobber_count == 0U && source->has_memory_clobber &&\n        source->clobber_count == 1U) {\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;\n        instruction.span = statement->span;\n        instruction.type = minic_type_void();\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        return minic_core_function_append_effect_instruction(\n                   context->function, context->block_id, &instruction)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n\n    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->outputs != NULL &&\n        source->output_count == 1U && source->input_count == 0U &&\n        source->label_count == 0U && source->register_clobber_count == 0U &&\n        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {\n        const MinicInlineAsmOperand *output;\n        const MinicExpression *output_expression;\n        const MinicLocal *local;\n        MinicCoreValueId address_id;\n        MinicCoreValueId output_value;\n        MinicType output_type;\n        bool register_constraint;\n\n        output = &source->outputs[0];\n        output_expression = minic_c0_program_expression(context->body->program, output->expression);\n        register_constraint =\n            output->constraint_text != NULL &&\n            ((output->constraint_length == 2U &&\n              memcmp(output->constraint_text, \"=r\", 2U) == 0) ||\n             (output->constraint_length == 3U &&\n              memcmp(output->constraint_text, \"=&r\", 3U) == 0));\n        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY && register_constraint &&\n            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&\n            output_expression->value_category == MINIC_VALUE_LVALUE &&\n            !minic_type_is_const(output_expression->type) &&\n            !minic_type_is_volatile(output_expression->type) &&\n            minic_type_unqualified(output_expression->type, &output_type) &&\n            core_memory_scalar_type(output_type)) {\n            local = minic_c0_program_local(\n                context->body->program, output_expression->value.local_id);\n            if (local == NULL) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            if (!local->is_array &&\n                minic_c0_program_local_fixed_register_binding(\n                    context->body->program, output_expression->value.local_id) == NULL &&\n                minic_type_equal(local->type, output_expression->type)) {\n                if (!minic_core_function_add_opaque_inline_asm(context->function,\n                                                               source->template_text,\n                                                               source->template_length,\n                                                               source->is_volatile,\n                                                               source->has_memory_clobber,\n                                                               &inline_asm_id)) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                (void)memset(&instruction, 0, sizeof(instruction));\n                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;\n                instruction.span = statement->span;\n                instruction.type = output_type;\n                instruction.result = MINIC_CORE_VALUE_INVALID;\n                instruction.value.inline_asm_id = inline_asm_id;\n                if (!minic_core_function_append_value_instruction(\n                        context->function, context->block_id, &instruction, &output_value)) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {\n                    return MINIC_CORE_LOWER_ERROR;\n                }\n                (void)memset(&instruction, 0, sizeof(instruction));\n                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;\n                instruction.span = statement->span;\n                instruction.type = minic_type_void();\n                instruction.result = MINIC_CORE_VALUE_INVALID;\n                instruction.value.store.address = address_id;\n                instruction.value.store.stored_value = output_value;\n                instruction.value.store.is_volatile = false;\n                return minic_core_function_append_effect_instruction(\n                           context->function, context->block_id, &instruction)\n                           ? MINIC_CORE_LOWER_OK\n                           : MINIC_CORE_LOWER_ERROR;\n            }\n        }\n    }\n\n    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&\n""",
        "core_lower.c inline asm output/barrier",
    )

    texts["codegen"] = replace_once(
        texts["codegen"],
        """static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,\n                                             const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n\n    if (function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||\n        instruction->value.inline_asm_id >= function->inline_asm_count) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n           inline_asm->is_volatile;\n}\n\nstatic bool core_instruction_supported(const MinicC0Program *program,\n""",
        """static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,\n                                             const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n\n    if (function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||\n        instruction->value.inline_asm_id >= function->inline_asm_count) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&\n           inline_asm->is_volatile;\n}\n\nstatic bool core_register_output_inline_asm_supported(\n    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    size_t index;\n\n    if (function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM ||\n        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||\n        instruction->value.inline_asm_id >= function->inline_asm_count) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||\n        !inline_asm->is_volatile) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            continue;\n        }\n        if (index + 1U >= inline_asm->template_length ||\n            (inline_asm->template_text[index + 1U] != '%' &&\n             inline_asm->template_text[index + 1U] != '0')) {\n            return false;\n        }\n        index += 1U;\n    }\n    return true;\n}\n\nstatic bool core_instruction_supported(const MinicC0Program *program,\n""",
        "core_codegen.c support helper",
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return core_opaque_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_CALL:\n""",
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return core_opaque_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return core_register_output_inline_asm_supported(function, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n        return true;\n    case MINIC_CORE_INSTRUCTION_CALL:\n""",
        "core_codegen.c support switch",
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """static bool emit_instruction(FILE *file,\n                             const MinicC0Program *program,\n""",
        """static bool emit_register_output_inline_asm(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicCoreFunction *function,\n    const MinicRiscv64CoreFrame *frame,\n    const MinicCoreInstruction *instruction) {\n    const MinicCoreInlineAsm *inline_asm;\n    size_t index;\n\n    if (file == NULL || frame == NULL ||\n        !core_register_output_inline_asm_supported(function, instruction)) {\n        return false;\n    }\n    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];\n    if (fprintf(file, \"  \") < 0) {\n        return false;\n    }\n    for (index = 0U; index < inline_asm->template_length; ++index) {\n        if (inline_asm->template_text[index] != '%') {\n            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {\n                return false;\n            }\n            continue;\n        }\n        index += 1U;\n        if (inline_asm->template_text[index] == '%') {\n            if (fputc('%', file) == EOF) {\n                return false;\n            }\n        } else if (inline_asm->template_text[index] == '0') {\n            if (fprintf(file, \"t0\") < 0) {\n                return false;\n            }\n        } else {\n            return false;\n        }\n    }\n    if (fputc('\\n', file) == EOF ||\n        (minic_type_is_integer(instruction->type) &&\n         !minic_riscv64_emit_integer_conversion_for_program(\n             file, program, instruction->type, \"t0\"))) {\n        return false;\n    }\n    return store_core_value(file, frame, instruction->result, \"t0\");\n}\n\nstatic bool emit_instruction(FILE *file,\n                             const MinicC0Program *program,\n""",
        "core_codegen.c output emitter",
    )
    texts["codegen"] = replace_once(
        texts["codegen"],
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return emit_opaque_inline_asm(file, function, instruction);\n    case MINIC_CORE_INSTRUCTION_CALL:\n""",
        """    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return emit_opaque_inline_asm(file, function, instruction);\n    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:\n        return emit_register_output_inline_asm(file, program, function, frame, instruction);\n    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:\n        return true;\n    case MINIC_CORE_INSTRUCTION_CALL:\n""",
        "core_codegen.c emission switch",
    )

    for name, path in FILES.items():
        path.write_text(texts[name])
    print("M48 inline-asm output lowering applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
