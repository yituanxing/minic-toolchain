#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/aarch64/machine_lse_source_select.h"
#include "target/common/machine_source_asm_template.h"

#include <string.h>

/* This is a deliberately closed initial MachineIR source-ASM policy.  The
 * direct emitter's Linux descriptor/operand machinery must not be exposed as
 * arbitrary text in MIR: each nontrivial form needs its own physical binding
 * and exact template verifier before being admitted here.  A zero-operand
 * barrier/hint has no GNU substitution and can be represented immediately. */
static bool a64_machine_opaque_hint(const MinicMachineInstruction *instruction) {
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
    /* Share the source qualifier's exact ISA recognizer. In particular,
     * original cache.h uses a literal TAB in `ic\tialluis`; preserving
     * this accepted GNU spelling must not trigger the old FILE* path. */
    return aarch64_barrier_template(instruction->source_asm_template,
        instruction->source_asm_length) != NULL;
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

/* Keep the original GNU source text for the three source-qualified one-GPR
 * families.  Their ISA fingerprints are the SAME pure Target predicates
 * used by C admission and the former direct emitter.  The physical x9
 * binding and %a0 address modifier are checked independently at MachineIR. */
static bool a64_one_gp_form(const MinicMachineInstruction *ins) {
    const unsigned int source = MINIC_MACHINE_EFFECT_SOURCE_ASM;
    const unsigned int vol = MINIC_MACHINE_EFFECT_VOLATILE;
    const unsigned int barrier = MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    const unsigned int memory = MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    const char *cache;
    if (ins == NULL || ins->operand_count != 1U ||
        ins->implicit_count != 0U || ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->source_asm_machine_form_variant != 0U) return false;
    switch (ins->source_asm_machine_form_id) {
    case AARCH64_MACHINE_ASM_AT_S1E1R:
        return ins->effects == (source | vol) &&
            aarch64_linux_at_s1e1r_template(ins->source_asm_template,
                ins->source_asm_length);
    case AARCH64_MACHINE_ASM_PREFETCH:
        return ins->effects == (source | vol) &&
            aarch64_linux_prefetch_template(ins->source_asm_template,
                ins->source_asm_length) != NULL;
    case AARCH64_MACHINE_ASM_CACHE_OP:
        cache = aarch64_linux_cache_op_form(ins->source_asm_template,
            ins->source_asm_length);
        return cache != NULL &&
            (ins->effects == (source | vol | barrier | memory) ||
             (aarch64_linux_efi_unclobbered_cache_form(cache) &&
              (ins->effects == (source | memory) ||
               ins->effects == (source | vol | memory))));
    default: return false;
    }
}

static bool a64_gp_source_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_one_gp_form(ins);
}

static bool a64_gp_source_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    const bool pref = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_PREFETCH;
    (void)context;
    if (!a64_gp_source_form(fn, ins, NULL) || index != 0U ||
        ins->operands == NULL) return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, pref ? "p" : "r") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == MINIC_MACHINE_REGISTER_READ;
}

static bool a64_gp_source_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const bool pref = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_PREFETCH;
    (void)labels;
    return modifier == (pref ? 'a' : 0U) &&
        a64_gp_source_binding(fn, ins, index, context) &&
        (output == NULL || fputs(pref ? "[x9]" : "x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_one_gp_policy = {
    a64_gp_source_form,
    a64_gp_source_binding,
    a64_gp_source_operand,
    NULL
};

/* All named and encoded Linux system-register accessors are single physical
 * x9 input/output effects. C admission has already sealed a finite tuple or
 * named-register grammar; MachineIR independently replays that exact target
 * recognizer and the original GNU placeholder, without an arbitrary ASM
 * string passthrough. Read outputs become one native x9 register definition;
 * their following typed store reaches the source lvalue. */
static bool a64_sysreg_source_form(const MinicMachineInstruction *ins) {
    uint32_t variant = 0U;
    const char *text;
    size_t len;
    bool valid = false;
    if (ins == NULL || ins->source_asm_template == NULL ||
        ins->operand_count != 1U || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE)) return false;
    text = ins->source_asm_template;
    len = ins->source_asm_length;
    switch (ins->source_asm_machine_form_id) {
    case AARCH64_MACHINE_ASM_MRS:
        valid = aarch64_linux_mrs_register(text, len) != NULL;
        break;
    case AARCH64_MACHINE_ASM_MSR:
        valid = aarch64_linux_msr_register(text, len) != NULL;
        break;
    case AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ:
        variant = aarch64_linux_encoded_sysreg_form(text, len);
        valid = variant != 0U;
        break;
    case AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE:
        variant = aarch64_linux_encoded_sysreg_write_form(text, len);
        valid = variant != 0U;
        break;
    default: return false;
    }
    return valid && ins->source_asm_machine_form_variant == variant;
}

static bool a64_sysreg_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_sysreg_source_form(ins);
}

static bool a64_sysreg_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *operand;
    const bool output = ins != NULL &&
        (ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_MRS ||
         ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ);
    const bool tegra = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_MSR &&
        aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length) != NULL &&
        strcmp(aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length), "s3_0_c15_c0_4") == 0;
    (void)context;
    if (!a64_sysreg_policy_form(fn, ins, NULL) || index != 0U ||
        ins->operands == NULL) return false;
    operand = &ins->operands[index];
    return operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        operand->asm_constraint != NULL &&
        strcmp(operand->asm_constraint, output ? "=r" : tegra ? "r" : "rZ") == 0 &&
        operand->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        operand->value.physical.reg.number == 9U &&
        operand->value.physical.reg.width_bits == 64U &&
        operand->value.physical.access == (output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_sysreg_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    const bool msr = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_MSR;
    const bool tegra = msr &&
        aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length) != NULL &&
        strcmp(aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length), "s3_0_c15_c0_4") == 0;
    return modifier == (ins != NULL &&
        (ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE ||
         (msr && !tegra)) ? 'x' : 0U) &&
        a64_sysreg_policy_binding(fn, ins, index, context) &&
        (output == NULL || fputs("x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_sysreg_policy = {
    a64_sysreg_policy_form,
    a64_sysreg_policy_binding,
    a64_sysreg_policy_operand,
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
             instruction, &a64_tlbi_template_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_one_gp_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_sysreg_policy) ||
         minic_aarch64_machine_lse_casp128_validate(&function, instruction) ||
         minic_aarch64_machine_lse_scalar_validate(&function, instruction) ||
         minic_aarch64_machine_lse_dec_positive_validate(&function, instruction));
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
    if (a64_one_gp_form(instruction))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_one_gp_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    if (a64_sysreg_source_form(instruction))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_sysreg_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    if (instruction->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_LSE_CASP128)
        return minic_aarch64_machine_lse_casp128_emit(output,
            function, instruction, labels);
    if (instruction->source_asm_machine_form_id == AARCH64_MACHINE_ASM_LSE)
        return minic_aarch64_machine_lse_scalar_emit(output,
            function, instruction, labels);
    if (instruction->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_LSE_DEC_POSITIVE)
        return minic_aarch64_machine_lse_dec_positive_emit(output,
            function, instruction, labels);
    return fwrite(instruction->source_asm_template, 1U,
                  instruction->source_asm_length, output) ==
               instruction->source_asm_length &&
        fputc('\n', output) != EOF;
}
