#ifndef MINIC_TARGET_COMMON_CORE_MACHINE_ASM_H
#define MINIC_TARGET_COMMON_CORE_MACHINE_ASM_H

#include "core/core_ir.h"
#include "target/common/machine_builder.h"

/* The single Core->physical MachineIR source-ASM handoff. Only the C ingress
 * interprets GNU constraints; this layer copies its sealed target machine
 * form and Module-owned occurrence identity without parsing the template.
 * Target selection still owns physical bindings, implicit register effects
 * and control-flow edges. No Core pointer survives builder append. */
static inline bool minic_machine_builder_emit_core_source_asm(
    MinicMachineBuilder *builder, const MinicCoreInlineAsm *source,
    unsigned int target_effects,
    const MinicMachineOperand *bindings, size_t binding_count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count,
    bool terminates_block, bool permits_fallthrough) {
    MinicMachineSourceAsmInput input = {0};
    if (source == NULL || source->template_text == NULL ||
        source->template_length == SIZE_MAX ||
        source->source_asm_unique_id == 0U) return false;
    input.source_bytes = source->template_text;
    input.source_byte_length = source->template_length;
    input.effects = target_effects |
        (source->is_volatile ? MINIC_MACHINE_EFFECT_VOLATILE : 0U) |
        (source->has_memory_clobber
            ? MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U);
    input.bindings = bindings;
    input.binding_count = binding_count;
    input.implicit = implicit;
    input.implicit_count = implicit_count;
    input.terminates_block = terminates_block;
    input.permits_fallthrough = permits_fallthrough;
    input.target_machine_form_id = source->target_machine_form_id;
    input.target_machine_form_variant = source->target_machine_form_variant;
    input.has_unique_id = true;
    input.unique_id = source->source_asm_unique_id;
    return minic_machine_builder_emit_source_asm_typed(builder, &input);
}

#endif
