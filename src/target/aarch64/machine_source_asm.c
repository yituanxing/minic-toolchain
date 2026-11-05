#include "target/aarch64/machine_source_asm.h"

#include <string.h>

/* This is a deliberately closed initial MachineIR source-ASM policy.  The
 * direct emitter's Linux descriptor/operand machinery must not be exposed as
 * arbitrary text in MIR: each nontrivial form needs its own physical binding
 * and exact template verifier before being admitted here.  A zero-operand
 * barrier/hint has no GNU substitution and can be represented immediately. */
static bool a64_machine_opaque_hint(const MinicMachineInstruction *instruction) {
    static const char *const allowed[] = {
        "nop", "yield", "sev", "sevl", "wfe", "wfi", "isb",
        "dsb sy", "dsb st", "dsb ish", "dsb ishst", "dsb ld",
        "dmb sy", "dmb ish", "dmb ishld", "dmb ishst",
        "dmb osh", "dmb oshld", "dmb oshst", "ic iallu", "ic ialluis"
    };
    size_t i;
    if (instruction == NULL || instruction->source_asm_template == NULL ||
        instruction->source_asm_length == 0U ||
        instruction->source_asm_machine_form_id != 0U ||
        instruction->source_asm_machine_form_variant != 0U ||
        instruction->operand_count != 0U ||
        instruction->implicit_count != 0U ||
        instruction->source_asm_terminates_block ||
        instruction->source_asm_permits_fallthrough ||
        (instruction->effects & (MINIC_MACHINE_EFFECT_CALL |
                                 MINIC_MACHINE_EFFECT_BRANCH |
                                 MINIC_MACHINE_EFFECT_RETURN)) != 0U)
        return false;
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        const size_t length = strlen(allowed[i]);
        if (instruction->source_asm_length == length &&
            memcmp(instruction->source_asm_template, allowed[i], length) == 0)
            return true;
    }
    return false;
}

bool minic_aarch64_machine_source_asm_validate(
    const MinicTargetInfo *target, const MinicMachineInstruction *instruction,
    void *context) {
    (void)context;
    return target != NULL &&
        target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        instruction != NULL &&
        instruction->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        (instruction->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) != 0U &&
        a64_machine_opaque_hint(instruction);
}

bool minic_aarch64_machine_source_asm_emit(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    (void)labels;
    if (output == NULL || function == NULL ||
        !minic_aarch64_machine_source_asm_validate(
            function->target, instruction, context))
        return false;
    return fwrite(instruction->source_asm_template, 1U,
                  instruction->source_asm_length, output) ==
               instruction->source_asm_length &&
        fputc('\n', output) != EOF;
}
