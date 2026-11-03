#include "target/loongarch64/simd_source_policy.h"
#include "target/loongarch64/simd_asm.h"
#include "target/loongarch64/simd_asm_syntax.h"

#include <stdio.h>
#include <string.h>

/* The original GNU constraints are only meaningful at source ingress.
 * Do not copy C rank, lvalue or AST expression matching into the Core
 * machine emitter; it consumes the sealed, typed operand plan instead. */
static bool loong_simd_constraint(const MinicInlineAsmOperand *op,
                                  const char *expected) {
    const size_t n = strlen(expected);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, expected, n) == 0;
}

static bool loong_simd_source_operand(
    const MinicC0Program *program, const MinicInlineAsmOperand *op,
    bool output, bool named, size_t index,
    MinicCoreStructuredInlineAsmOperandKind *kind) {
    const MinicExpression *expr;
    char expected_name[4];
    if (!program || !op || !kind) return false;
    if (named) {
        if (output || index >= 5U ||
            snprintf(expected_name, sizeof(expected_name), "v%zu", index + 1U) < 0 ||
            !op->name || op->name_length != 2U ||
            memcmp(op->name, expected_name, 2U) != 0)
            return false;
    } else if (op->name_length) return false;
    expr = minic_c0_program_expression(program, op->expression);
    if (!expr) return false;
    if (!output && named && loong_simd_constraint(op, "r") &&
        minic_type_is_pointer(expr->type)) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (expr->value_category != MINIC_VALUE_LVALUE ||
        (output && minic_type_is_const(expr->type))) return false;
    if (output && loong_simd_constraint(op, "+m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        return true;
    }
    if (output && loong_simd_constraint(op, "=m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        return true;
    }
    if (!output && !named && loong_simd_constraint(op, "m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        return true;
    }
    return false;
}

bool minic_loongarch64_simd_qualify_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    LoongSimdProgram parsed;
    size_t i;
    bool named;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->label_count ||
        source->register_clobber_count ||
        !minic_loongarch64_simd_parse(source->template_text, source->template_length,
                                      &parsed) || parsed.operand_count == 0U ||
        parsed.operand_count != source->output_count + source->input_count ||
        parsed.operand_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        (source->output_count && !source->outputs) ||
        (source->input_count && !source->inputs)) return false;
    named = strstr(source->template_text, "%[") != NULL;
    if (named) {
        if (source->output_count || source->input_count < 2U ||
            source->input_count > 5U || !source->has_memory_clobber ||
            source->clobber_count != 1U) return false;
    } else if (source->has_memory_clobber || source->clobber_count)
        return false;
    for (i = 0U; i < parsed.operand_count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *op = output
            ? &source->outputs[i] : &source->inputs[i-source->output_count];
        MinicCoreStructuredInlineAsmOperandKind role;
        const bool read = parsed.memory_read[i];
        const bool written = parsed.memory_written[i];
        if ((!read && !written) ||
            !loong_simd_source_operand(program, op, output, named,
                                       i, &role)) return false;
        if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            if (!named) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
            if (!read || written) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) {
            if (!written || read) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            if (!read || !written) return false;
        } else return false;
        contract->kinds[i] = role;
    }
    /* Every admitted operand above has a real typed load/store effect.
     * Named pointer forms carry a GNU `memory` clobber; numeric forms bind
     * exact read/write `m` C lvalues and preserve their volatile ordering. */
    contract->operand_count = parsed.operand_count;
    contract->implicit_memory_effect = named;
    return true;
}

/* Proof that GNU named operands normalized to positional operands without
 * changing any physical LSX/LASX operation, register or memory offset. */
static bool loong_simd_same_machine_effects(
    const LoongSimdProgram *original, const LoongSimdProgram *machine) {
    size_t i, j;
    if (!original || !machine || original->count != machine->count ||
        original->operand_count != machine->operand_count) return false;
    for (i = 0U; i < original->count; ++i) {
        const LoongSimdInstruction *a = &original->instructions[i];
        const LoongSimdInstruction *b = &machine->instructions[i];
        if (a->op != b->op || a->lasx != b->lasx ||
            a->operand_index != b->operand_index ||
            a->memory_offset != b->memory_offset) return false;
        for (j = 0U; j < 4U; ++j)
            if (a->registers[j] != b->registers[j]) return false;
    }
    return true;
}

bool minic_loongarch64_simd_seal_normalized(
    const MinicC0Program *program, const MinicInlineAsm *source,
    const char *normalized, size_t length,
    MinicTargetInlineAsmContract *contract) {
    LoongSimdProgram original, machine;
    MinicTargetInlineAsmContract admitted = {0};
    size_t i;
    if (program == NULL || source == NULL || normalized == NULL ||
        contract == NULL) return false;
    if (!minic_loongarch64_simd_parse(source->template_text, source->template_length,
                                      &original))
        return true; /* Another target-owned family, not SIMD. */
    if (contract->target_machine_form_id != 0U ||
        contract->target_machine_form_variant != 0U ||
        !minic_loongarch64_simd_qualify_source(program, source, &admitted) ||
        !minic_backend_capture_asm_operand_source(
            source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &admitted) ||
        !minic_loongarch64_simd_parse(normalized, length, &machine) ||
        !loong_simd_same_machine_effects(&original, &machine) ||
        admitted.operand_count != contract->operand_count ||
        admitted.implicit_memory_effect != contract->implicit_memory_effect ||
        admitted.effective_memory_effect != contract->effective_memory_effect)
        return false;
    for (i = 0U; i < admitted.operand_count; ++i)
        if (admitted.kinds[i] != contract->kinds[i]) return false;
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_SIMD;
    contract->target_machine_form_variant = machine.uses_lasx
        ? MINIC_LOONG_MACHINE_SIMD_LASX : MINIC_LOONG_MACHINE_SIMD_LSX;
    return true;
}
