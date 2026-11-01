#ifndef MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_ASM_H
#define MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_ASM_H

#include "target/backend.h"
#include <string.h>

/* The two LP64 targets share the Linux static-key *source and Core*
 * admission grammar, not the target-specific branch opcode or relocation
 * modifier.  Each target supplies its exact __jump_table template suffix
 * and remains the sole owner of generated ELF/assembly instructions. */
typedef unsigned int (*MinicMultiLinuxJumpLabelForm)(const char *, size_t);

static inline unsigned int minic_multi_linux_jump_label_form(
    const char *text, size_t length, const char *record_template) {
    static const char *const prefixes[] = { "1:nop", "1:b%l[l_yes]" };
    char compact[256];
    size_t i, n = 0U;
    if (text == NULL || record_template == NULL ||
        length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        const size_t a = strlen(prefixes[i]), b = strlen(record_template);
        if (n == a + b && memcmp(compact, prefixes[i], a) == 0 &&
            memcmp(compact + a, record_template, b) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}

/* An ELF symbol plus at most one signed decimal byte addend.  Do not admit
 * arbitrary GAS expressions or percent/label directives from inline ASM. */
static inline bool minic_multi_linux_jump_key_relocation(const char *key) {
    size_t i, length;
    if (key == NULL) return false;
    length = strlen(key);
    if (length == 0U || length > 128U ||
        !((key[0] >= 'a' && key[0] <= 'z') ||
          (key[0] >= 'A' && key[0] <= 'Z') ||
          key[0] == '_' || key[0] == '.')) return false;
    for (i = 1U; i < length; ++i) {
        const char ch = key[i];
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '.' || ch == '$') continue;
        if ((ch == '+' || ch == '-') && i + 1U < length) {
            for (++i; i < length; ++i)
                if (key[i] < '0' || key[i] > '9') return false;
            return true;
        }
        return false;
    }
    return true;
}

static inline bool minic_multi_qualify_linux_jump_label_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicMultiLinuxJumpLabelForm form) {
    const MinicInlineAsmOperand *key;
    const MinicInlineAsmLabel *label;
    const MinicExpression *expression;
    if (form == NULL || program == NULL || source == NULL ||
        !source->is_goto || source->is_volatile ||
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

static inline bool minic_multi_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source, MinicMultiLinuxJumpLabelForm form) {
    return form != NULL && source != NULL && source->is_goto &&
           source->is_volatile &&
           source->goto_target != MINIC_CORE_BLOCK_INVALID &&
           source->goto_second_target == MINIC_CORE_BLOCK_INVALID &&
           !source->has_memory_clobber &&
           source->register_clobber_count == 0U &&
           source->source_inline_asm_id == SIZE_MAX &&
           source->resolved_immediate_count == 1U &&
           minic_multi_linux_jump_key_relocation(
               source->resolved_immediates[0]) &&
           form(source->template_text, source->template_length) != 0U;
}

#endif
