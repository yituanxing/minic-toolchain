#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()

marker = '''static const MinicInlineAsmOperand *operand_at(const MinicInlineAsm *inline_asm,
'''
helper = '''static bool constraint_matching_output(const MinicInlineAsm *inline_asm,
                                       const MinicInlineAsmOperand *operand,
                                       size_t *output_index) {
    unsigned char ch;
    size_t index;

    if (inline_asm == NULL || operand == NULL || output_index == NULL ||
        operand->constraint_text == NULL || operand->constraint_length != 1U) {
        return false;
    }
    ch = (unsigned char)operand->constraint_text[0];
    if (ch < '0' || ch > '9') {
        return false;
    }
    index = (size_t)(ch - '0');
    if (index >= inline_asm->output_count) {
        return false;
    }
    *output_index = index;
    return true;
}

static bool matching_output_is_register(const MinicInlineAsm *inline_asm,
                                        size_t output_index) {
    const MinicInlineAsmOperand *output;

    if (inline_asm == NULL || output_index >= inline_asm->output_count) {
        return false;
    }
    output = &inline_asm->outputs[output_index];
    return constraint_is(output, "=r") || constraint_is(output, "=&r") ||
           constraint_is(output, "+r");
}

'''
if helper not in text:
    if marker not in text:
        raise SystemExit("matching constraint helper anchor not found")
    text = text.replace(marker, helper + marker, 1)

old_validate_head = '''static bool validate_input(const MinicInlineAsm *inline_asm,
                           const MinicC0Program *program,
                           const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
        return false;
    }
'''
new_validate_head = '''static bool validate_input(const MinicInlineAsm *inline_asm,
                           const MinicC0Program *program,
                           const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;
    size_t matching_output_index;

    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
        return false;
    }
    if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
        if (!matching_output_is_register(inline_asm, matching_output_index)) {
            return false;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        return expression != NULL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
'''
if old_validate_head in text:
    text = text.replace(old_validate_head, new_validate_head, 1)
elif new_validate_head not in text:
    raise SystemExit("matching input validation anchor not found")

old_assign = '''        operand = operand_at(inline_asm, operand_index);
        if (operand == NULL) {
            return false;
        }
        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
'''
new_assign = '''        operand = operand_at(inline_asm, operand_index);
        if (operand == NULL) {
            return false;
        }
        if (operand_index >= inline_asm->output_count) {
            size_t matching_output_index;

            if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
                if (!matching_output_is_register(inline_asm, matching_output_index) ||
                    operand_registers[matching_output_index] == NULL) {
                    return false;
                }
                operand_registers[operand_index] = operand_registers[matching_output_index];
                continue;
            }
        }
        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
'''
if old_assign in text:
    text = text.replace(old_assign, new_assign, 1)
elif new_assign not in text:
    raise SystemExit("matching register assignment anchor not found")

path.write_text(text)
