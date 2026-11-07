#ifndef MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_ASM_H
#define MINIC_TARGET_COMMON_LINUX_JUMP_LABEL_ASM_H

#include "core/core_ir.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* The two LP64 targets share the Linux static-key template and Core
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

/* Target-owned GNU/ELF serializer. The Core witness is already a verified
 * symbol plus byte displacement; no GNU immediate or relocation expression
 * is parsed after source admission. */
static inline bool minic_multi_linux_jump_relocation_text(
    const MinicCoreInlineAsmImmediate *immediate,
    char *buffer, size_t capacity) {
    int written;
    if (buffer == NULL || capacity == 0U ||
        !minic_core_inline_asm_relocation_valid(immediate)) return false;
    if (immediate->byte_addend == 0)
        written = snprintf(buffer, capacity, "%s", immediate->symbol);
    else
        written = snprintf(buffer, capacity,
            immediate->byte_addend > 0 ? "%s+%" PRId64 : "%s%" PRId64,
            immediate->symbol, immediate->byte_addend);
    return written > 0 && (size_t)written < capacity && written <= 128;
}

static inline bool minic_multi_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source, MinicMultiLinuxJumpLabelForm form) {
    return form != NULL && source != NULL && source->is_goto &&
           source->is_volatile &&
           source->goto_target != MINIC_CORE_BLOCK_INVALID &&
           source->goto_second_target == MINIC_CORE_BLOCK_INVALID &&
           !source->has_memory_clobber &&
           source->register_clobber_count == 0U &&
           source->deferred_immediate_symbol == NULL &&
           source->resolved_immediate_count == 1U &&
           minic_core_inline_asm_relocation_valid(
               &source->resolved_immediates[0]) &&
           form(source->template_text, source->template_length) != 0U;
}

#endif
