#!/usr/bin/env python3
"""Lower empty nonvolatile tied-register inline asm as a scalar copy."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M77_EMPTY_TIED_ASM_COPY"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M77 empty tied asm copy already applied")
        return 0

    anchor = '''    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&\n        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&\n        source->register_clobber_count == 0U && !source->has_memory_clobber) {\n'''
    replacement = '''    /* M77_EMPTY_TIED_ASM_COPY: an empty, nonvolatile GNU asm with one\n       register output tied to input 0 carries no target instruction semantics.\n       It preserves the input register bit-pattern in the output. Model that\n       target-neutrally as scalar bitcast/copy plus the output store. */\n    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length == 0U && source->outputs != NULL && source->inputs != NULL &&\n        source->output_count == 1U && source->input_count == 1U && source->label_count == 0U &&\n        source->clobber_count == 0U && source->register_clobber_count == 0U &&\n        !source->has_memory_clobber) {\n        const MinicInlineAsmOperand *input = &source->inputs[0];\n        const MinicInlineAsmOperand *output = &source->outputs[0];\n        const MinicExpression *input_expression;\n        const MinicExpression *output_expression;\n        MinicCoreInstruction store;\n        MinicCoreLowerStatus status;\n        MinicCoreValueId input_value;\n        MinicCoreValueId output_address;\n        MinicCoreValueId output_value;\n        MinicType input_type;\n        MinicType output_type;\n\n        input_expression =\n            minic_c0_program_expression(context->body->program, input->expression);\n        output_expression =\n            minic_c0_program_expression(context->body->program, output->expression);\n        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&\n            core_inline_asm_register_output_constraint(output) &&\n            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&\n            core_inline_asm_constraint_is(input, "0") && output_expression != NULL &&\n            output_expression->value_category == MINIC_VALUE_LVALUE &&\n            !minic_type_is_const(output_expression->type) &&\n            minic_type_unqualified(output_expression->type, &output_type) &&\n            core_memory_scalar_type(output_type) && input_expression != NULL &&\n            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {\n            status = lower_expression(context, input->expression, &input_value);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            status = append_scalar_bitcast(\n                context, statement->span, output_type, input_value, &output_value);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            status = lower_address(context, output->expression, &output_address);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            (void)memset(&store, 0, sizeof(store));\n            store.kind = MINIC_CORE_INSTRUCTION_STORE;\n            store.span = statement->span;\n            store.type = minic_type_void();\n            store.result = MINIC_CORE_VALUE_INVALID;\n            store.value.store.address = output_address;\n            store.value.store.stored_value = output_value;\n            store.value.store.is_volatile = minic_type_is_volatile(output_expression->type);\n            return minic_core_function_append_effect_instruction(\n                       context->function, context->block_id, &store)\n                       ? MINIC_CORE_LOWER_OK\n                       : MINIC_CORE_LOWER_ERROR;\n        }\n    }\n\n    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&\n        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&\n        source->register_clobber_count == 0U && !source->has_memory_clobber) {\n'''
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"M77 anchor count={count}")
    PATH.write_text(text.replace(anchor, replacement, 1))
    print("M77 empty tied asm scalar copy applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
