#ifndef MINIC_CORE_CORE_LOWER_ASM_INTERNAL_H
#define MINIC_CORE_CORE_LOWER_ASM_INTERNAL_H

#include "core/core_lower_internal.h"

/* Private AST inline-asm operand normalization → Core effect admission seam.
 * Lowering probes deliberately retain their original ordered dispatch:
 * the first probe that claims an input owns any Core mutations. */

bool core_inline_asm_constraint_is(const MinicInlineAsmOperand *operand,
    const char *text);

bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand);

bool core_inline_asm_specialize_immediates(const MinicCoreLowerContext *context,
    const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out);

bool core_inline_asm_specialize_register_output_immediates(
    const MinicCoreLowerContext *context,
    const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out);

bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out);

MinicCoreLowerStatus core_asm_try_atomic_and_immediate_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

MinicCoreLowerStatus core_asm_try_mixed_memory_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

MinicCoreLowerStatus core_asm_try_readwrite_and_output_specialization(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

MinicCoreLowerStatus core_asm_try_empty_barrier_scalar_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

#endif
