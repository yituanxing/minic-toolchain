#ifndef MINIC_CORE_CORE_LOWER_ASM_INTERNAL_H
#define MINIC_CORE_CORE_LOWER_ASM_INTERNAL_H

#include "core/core_lower_internal.h"
#include "target/backend.h"

/* Shared GNU asm input-normalization bounds: consumed by operand decoding,
 * tracing and CSR structured-effect lowering across separate C files. */
#define MINIC_CORE_IMMEDIATE_ASM_LIMIT 8U
#define MINIC_CORE_IMMEDIATE_TEXT_LIMIT 64U

/* One typed GNU operand-plan publication path for both target-qualified
 * and target-neutral structured forms. No ISA interpretation is done here. */
bool core_asm_publish_operand_plan(MinicCoreInstruction *instruction,
    MinicCoreInlineAsm *stored, const MinicAsmOperandPlan *plan);

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

/* Operand normalization -> structured Core effect admission.
 * These interfaces are private to GNU inline-asm lowering, not Core IR API. */
bool core_inline_asm_fixed_binding_id(const MinicC0Program *program,
                                                   const MinicExpression *expression,
                                                   size_t *binding_id);

bool core_inline_asm_immediate_text(
    const MinicCoreLowerContext *context,
    const MinicInlineAsmOperand *operand,
    char *integer_text,
    size_t integer_capacity,
    const char **text_out,
    size_t *length_out);

bool core_inline_asm_single_label_goto_supported(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source);

bool core_inline_asm_single_label_goto_numeric_template(
    const MinicInlineAsm *source, char **template_out, size_t *template_length_out);

bool core_inline_asm_single_label_goto_bake_input(
    const char *template_text,
    size_t template_length,
    const char *replacement,
    size_t replacement_length,
    char **template_out,
    size_t *template_length_out);

bool core_inline_asm_expression_is_local(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicLocalId local_id);

MinicCoreLowerStatus core_asm_try_target_register_contract(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

MinicCoreLowerStatus core_asm_try_immediate_output_csr(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

MinicCoreLowerStatus core_asm_try_structured_roles_and_asm_goto(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled);

#endif
