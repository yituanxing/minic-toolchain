#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# Batch G: reuse the existing generic structured-inline-asm Core instruction
# for GNU outputless asm with two scalar register inputs plus a memory clobber.
# Core already models scalar input bindings and the RV64 emitter already has two
# scalar input scratch registers; only the lowerer/target acceptance tiers were
# narrower than the IR.
path = "src/core/core_lower.c"
anchor = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
block = '''    /* BATCH_G_TWO_SCALAR_OUTPUTLESS_ASM: outputless GNU asm is effectively
       volatile (Batch F). Reuse the generic structured operand model when the
       statement has two scalar register inputs and a memory clobber. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < 2U; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression = minic_c0_program_expression(
                context->body->program, operand->expression);
            MinicType input_type;
            bool register_constraint;

            register_constraint =
                operand->constraint_text != NULL &&
                ((operand->constraint_length == 1U &&
                  memcmp(operand->constraint_text, "r", 1U) == 0) ||
                 (operand->constraint_length == 2U &&
                  memcmp(operand->constraint_text, "rK", 2U) == 0));
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !register_constraint || input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = numeric_template_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              true,
                                                              &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 2U;
            for (input_index = 0U; input_index < 2U; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                status = lower_expression(context, source->inputs[input_index].expression,
                                          &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

'''
replace_once(path, anchor, block + anchor)

path = "src/target/riscv64/core_codegen.c"
old = '''        instruction->value.structured_inline_asm.operand_count < 3U ||
        instruction->value.structured_inline_asm.operand_count > 5U) {
'''
new = '''        instruction->value.structured_inline_asm.operand_count == 0U ||
        instruction->value.structured_inline_asm.operand_count > 5U) {
'''
replace_once(path, old, new)

old = '''    if (register_outputs != 2U || memory_readwrites != 1U || scalar_inputs > 2U ||
        scalar_inputs + 3U != instruction->value.structured_inline_asm.operand_count) {
        return false;
    }
'''
new = '''    if (!((register_outputs == 2U && memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count) ||
          (register_outputs == 0U && memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U))) {
        return false;
    }
'''
replace_once(path, old, new)

print("CORE_BATCH_G_PATCHED two-scalar outputless structured asm")
