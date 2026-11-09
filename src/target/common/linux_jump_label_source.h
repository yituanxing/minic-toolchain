#ifndef MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_SOURCE_H
#define MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_SOURCE_H

#include "frontend/ast.h"
#include "target/common/linux_jump_label_asm.h"

/* C/GNU inline-asm admission happens before Core publication. The native
 * Linux static-key emitter must only see the sealed Core form and must not
 * pull the C AST through its shared machine-side grammar header. */
static inline bool minic_multi_qualify_linux_jump_label_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicMultiLinuxJumpLabelForm form) {
    const MinicInlineAsmOperand *key;
    const MinicInlineAsmLabel *label;
    const MinicExpression *expression;
    if (form == NULL || program == NULL || source == NULL ||
        !source->is_goto || !source->is_volatile ||
        source->output_count != 0U || source->input_count != 1U ||
        source->inputs == NULL || source->label_count != 1U ||
        source->labels == NULL || source->clobber_count != 0U ||
        source->register_clobber_count != 0U || source->has_memory_clobber ||
        form(source->template_text, source->template_length) == 0U)
        return false;
    key = &source->inputs[0];
    label = &source->labels[0];
    expression = minic_c0_program_expression(program, key->expression);
    return key->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
           key->constraint_text != NULL && key->constraint_length == 1U &&
           key->constraint_text[0] == 'i' && expression != NULL &&
           minic_type_is_pointer(expression->type) &&
           label->name != NULL && label->name_length == 5U &&
           memcmp(label->name, "l_yes", 5U) == 0 &&
           label->target_statement != MINIC_STATEMENT_INVALID;
}

#endif
