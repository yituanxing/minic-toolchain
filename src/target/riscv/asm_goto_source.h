#ifndef MINIC_TARGET_RISCV_ASM_GOTO_SOURCE_H
#define MINIC_TARGET_RISCV_ASM_GOTO_SOURCE_H

#include "target/backend_source.h"
#include "frontend/ast.h"

#include <string.h>

/* C ingress, not MachineIR selection, owns GNU operand-class admission.
 * Every accepted input must be a compile-time `i` witness, and every label
 * must be an actual C label. Core's common resolver still proves the exact
 * integer/symbol fact; no template spellings or C names are inferred here.
 * Generated RV32I/RV64I instruction selection is independent of this
 * source-origin GNU ASM permission. */
static inline bool minic_riscv_qualify_inline_asm_goto(
    const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source) {
    size_t i;
    if (invocation == NULL || program == NULL || source == NULL ||
        !source->is_goto || source->template_text == NULL ||
        source->template_length == 0U ||
        source->template_length > 1048576U ||
        source->output_count != 0U ||
        source->input_count == 0U || source->input_count > 3U ||
        source->inputs == NULL ||
        source->label_count == 0U || source->label_count > 2U ||
        source->labels == NULL || source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        source->has_memory_clobber) return false;
    for (i = 0U; i < source->input_count; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        const MinicExpression *expression = minic_c0_program_expression(
            program, input->expression);
        if (input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            input->constraint_text == NULL ||
            input->constraint_length != 1U ||
            input->constraint_text[0] != 'i' ||
            expression == NULL ||
            (!minic_type_is_integer(expression->type) &&
             !minic_type_is_pointer(expression->type))) return false;
    }
    for (i = 0U; i < source->label_count; ++i) {
        const MinicInlineAsmLabel *label = &source->labels[i];
        const MinicStatement *statement = minic_c0_program_statement(
            program, label->target_statement);
        if (statement == NULL || statement->kind != MINIC_STATEMENT_LABEL)
            return false;
    }
    return true;
}

#endif
