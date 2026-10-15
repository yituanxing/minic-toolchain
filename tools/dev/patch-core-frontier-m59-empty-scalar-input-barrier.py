from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M59_EMPTY_SCALAR_INPUT_BARRIER'
if marker in text:
    print('M59 empty scalar-input barrier already applied')
    raise SystemExit(0)

anchor = '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M59 anchor count={text.count(anchor)}')

block = '''    /* M59_EMPTY_SCALAR_INPUT_BARRIER: GNU barrier_data() is an empty
       volatile asm with one scalar register input and a memory clobber. The
       operand must still be evaluated, but an empty target template needs no
       target instruction. Represent the ordering effect with the existing
       target-neutral compiler barrier rather than inventing an empty opaque
       asm encoding. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId discarded_input;
        MinicCoreLowerStatus input_status;
        MinicType input_type;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            (core_inline_asm_constraint_is(input, "r") ||
             core_inline_asm_constraint_is(input, "rK")) &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &discarded_input);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (discarded_input >= context->function->value_count ||
                !minic_type_equal(context->function->values[discarded_input].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

'''
text = text.replace(anchor, block + anchor, 1)
path.write_text(text)
print('M59 empty scalar-input barrier applied')
