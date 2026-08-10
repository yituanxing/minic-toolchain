#include "target/riscv64/codegen_internal.h"

#include <stdint.h>
#include <string.h>

#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U

static const char *const minic_riscv64_inline_asm_registers[] = {
    "t0", "t1", "t3", "t4", "t5", "t6"};

static bool constraint_is(const MinicInlineAsmOperand *operand, const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool validate_output(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (constraint_is(operand, "+A")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;
    }
    if (constraint_is(operand, "=r")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
               expression->kind == MINIC_EXPRESSION_LOCAL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    return false;
}

static bool validate_input(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || !constraint_is(operand, "r")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}

static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {
    size_t index;

    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;

        ch = (unsigned char)inline_asm->template_text[index];
        if (ch != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            continue;
        }
        if (ch < '0' || ch > '9' || (size_t)(ch - '0') >= operand_count) {
            return false;
        }
    }
    return true;
}

static bool emit_template(FILE *file, const MinicInlineAsm *inline_asm) {
    size_t operand_count;
    size_t index;

    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (!template_operands_are_valid(inline_asm, operand_count) || fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;

        ch = (unsigned char)inline_asm->template_text[index];
        if (ch != '%') {
            if (fputc((int)ch, file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        {
            size_t operand_index;
            const MinicInlineAsmOperand *operand;
            const char *register_name;

            operand_index = (size_t)(ch - '0');
            register_name = minic_riscv64_inline_asm_registers[operand_index];
            if (operand_index < inline_asm->output_count) {
                operand = &inline_asm->outputs[operand_index];
                if (constraint_is(operand, "+A")) {
                    if (fprintf(file, "(%s)", register_name) < 0) {
                        return false;
                    }
                    continue;
                }
            }
            if (fputs(register_name, file) == EOF) {
                return false;
            }
        }
    }
    return fputc('\n', file) != EOF;
}

bool minic_riscv64_emit_inline_asm(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicStatement *statement) {
    const MinicInlineAsm *inline_asm;
    size_t operand_count;
    size_t temporary_size;
    size_t index;

    if (file == NULL || program == NULL || function == NULL || statement == NULL) {
        return false;
    }
    inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    if (inline_asm->output_count == 0U && inline_asm->input_count == 0U) {
        return fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
    }
    if (inline_asm->output_count == 1U && inline_asm->input_count == 0U &&
        inline_asm->template_length == 0U && constraint_is(&inline_asm->outputs[0], "+rm") &&
        inline_asm->outputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        return true;
    }

    if (inline_asm->output_count > SIZE_MAX - inline_asm->input_count) {
        return false;
    }
    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (operand_count == 0U || operand_count > MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS ||
        !template_operands_are_valid(inline_asm, operand_count)) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (!validate_output(program, &inline_asm->outputs[index])) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (!validate_input(program, &inline_asm->inputs[index])) {
            return false;
        }
    }

    if (operand_count > (SIZE_MAX - 15U) / 8U) {
        return false;
    }
    temporary_size = (operand_count * 8U + 15U) & ~(size_t)15U;
    if (!minic_riscv64_emit_stack_allocate(file, temporary_size)) {
        return false;
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;

        operand = &inline_asm->outputs[index];
        if (constraint_is(operand, "+A")) {
            if (!minic_riscv64_emit_lvalue_address(file, program, function, operand->expression) ||
                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {
                return false;
            }
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        const MinicInlineAsmOperand *operand;
        size_t operand_index;

        operand = &inline_asm->inputs[index];
        operand_index = inline_asm->output_count + index;
        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {
            return false;
        }
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (constraint_is(&inline_asm->outputs[index], "+A") &&
            !minic_riscv64_emit_sp_load64(
                file, minic_riscv64_inline_asm_registers[index], index * 8U)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        size_t operand_index;

        operand_index = inline_asm->output_count + index;
        if (!minic_riscv64_emit_sp_load64(
                file, minic_riscv64_inline_asm_registers[operand_index], operand_index * 8U)) {
            return false;
        }
    }
    if (!emit_template(file, inline_asm)) {
        return false;
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;
        const MinicExpression *expression;

        operand = &inline_asm->outputs[index];
        if (!constraint_is(operand, "=r")) {
            continue;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
            !minic_riscv64_emit_object_store_register(file,
                                                      program,
                                                      function,
                                                      expression->value.local_id,
                                                      minic_riscv64_inline_asm_registers[index])) {
            return false;
        }
    }
    return minic_riscv64_emit_stack_release(file, temporary_size);
}
