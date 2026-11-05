#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/common/machine_source_asm_template.h"

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
        (instruction->source_asm_machine_form_id != 0U &&
         instruction->source_asm_machine_form_id !=
             AARCH64_MACHINE_ASM_BARRIER) ||
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

/* Original Linux TLBI 6.6.143 is a sealed GNU source-ASM instruction. The
 * `.arch armv8.5-a` feature directive and TLBI remain source-owned; GNU GAS
 * assembles them. Only the validated rZ physical input is substituted. */
static bool a64_machine_tlbi_form(const MinicMachineInstruction *ins) {
    const unsigned int form = ins != NULL ? aarch64_linux_tlbi_form(
        ins->source_asm_template, ins->source_asm_length) : 0U;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    return ins != NULL && form != 0U &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_TLBI &&
        ins->source_asm_machine_form_variant == form &&
        ins->operand_count ==
            (form <= AARCH64_LINUX_TLBI_ADDR_FORM_COUNT ? 1U : 0U) &&
        ins->implicit_count == 0U && ins->effects == effects &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough;
}

static bool a64_tlbi_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_machine_tlbi_form(ins);
}

static bool a64_tlbi_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t id, void *context) {
    const MinicMachineOperand *op;
    (void)context;
    if (!a64_tlbi_policy_form(fn, ins, NULL) || id != 0U ||
        ins->operand_count != 1U || ins->operands == NULL) return false;
    op = &ins->operands[id];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, "rZ") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == MINIC_MACHINE_REGISTER_READ;
}

static bool a64_tlbi_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t id,
    unsigned char modifier, void *context) {
    (void)labels;
    return modifier == 'x' &&
        a64_tlbi_policy_binding(fn, ins, id, context) &&
        (output == NULL || fputs("x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_tlbi_template_policy = {
    a64_tlbi_policy_form,
    a64_tlbi_policy_binding,
    a64_tlbi_policy_operand,
    NULL
};

bool minic_aarch64_machine_source_asm_validate(
    const MinicTargetInfo *target, const MinicMachineInstruction *instruction,
    void *context) {
    MinicMachineFunction function = {0};
    (void)context;
    if (target == NULL || instruction == NULL) return false;
    function.target = target;
    return target != NULL &&
        target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        instruction != NULL &&
        instruction->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        (instruction->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) != 0U &&
        (a64_machine_opaque_hint(instruction) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_tlbi_template_policy));
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
    if (a64_machine_tlbi_form(instruction))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_tlbi_template_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    return fwrite(instruction->source_asm_template, 1U,
                  instruction->source_asm_length, output) ==
               instruction->source_asm_length &&
        fputc('\n', output) != EOF;
}
