#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/aarch64/machine_lse_source_select.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/common/machine_source_asm_template.h"

#include <inttypes.h>
#include <string.h>

/* Original Linux kexec crash register image: all 31 GPRs, SP, synthetic
 * PSTATE and local-label PC. Source qualification proves the full record;
 * MachineIR independently verifies its exact ISA text and physical roles. */
static bool a64_kexec_crash_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_KEXEC_CRASH &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->operand_count == 3U && ins->operands != NULL &&
        ins->implicit_count == 0U && ins->effects == effects &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        aarch64_linux_kexec_crash_template(ins->source_asm_template,
            ins->source_asm_length);
}

static bool a64_kexec_crash_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const unsigned int regs[] = {9U, 10U, 0U};
    const MinicMachineOperand *op;
    (void)context;
    if (!a64_kexec_crash_form(fn, ins, NULL) || index >= 3U)
        return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL && op->asm_name == NULL &&
        strcmp(op->asm_constraint, index < 2U ? "=&r" : "r") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == regs[index] &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == (index < 2U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_kexec_crash_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const char *const regs[] = {"x9", "x10", "x0"};
    (void)labels;
    return modifier == 0U &&
        a64_kexec_crash_binding(fn, ins, index, context) &&
        (output == NULL || fputs(regs[index], output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_kexec_crash_policy = {
    a64_kexec_crash_form, a64_kexec_crash_binding,
    a64_kexec_crash_operand, NULL
};

/* This is a deliberately closed initial MachineIR source-ASM policy.  The
 * direct emitter's Linux descriptor/operand machinery must not be exposed as
 * arbitrary text in MIR: each nontrivial form needs its own physical binding
 * and exact template verifier before being admitted here.  A zero-operand
 * barrier/hint has no GNU substitution and can be represented immediately. */
static bool a64_machine_opaque_hint(const MinicMachineInstruction *instruction) {
    MinicCoreInlineAsm linux_bug = {0};
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
    if (aarch64_barrier_template(instruction->source_asm_template,
            instruction->source_asm_length) != NULL)
        return true;
    /* Original Linux local_daif_mask() carries a four-bit PSTATE update,
     * sometimes with the exact `// local_daif_mask` source annotation.
     * Source admission already proves this closed ISA form. MachineIR must
     * independently retain the exact zero-operand privileged instruction,
     * including its GNU memory clobber when present; a plain barrier/hint
     * recognizer cannot recognize the #0xf and annotated spellings. */
    if (instruction->source_asm_machine_form_id == 0U &&
        (instruction->effects ==
             (MINIC_MACHINE_EFFECT_SOURCE_ASM |
              MINIC_MACHINE_EFFECT_VOLATILE) ||
         instruction->effects ==
             (MINIC_MACHINE_EFFECT_SOURCE_ASM |
              MINIC_MACHINE_EFFECT_VOLATILE |
              MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) &&
        minic_aarch64_linux_daif_immediate_form(
            instruction->source_asm_template,
            instruction->source_asm_length) != 0U)
        return true;
    /* Linux 6.6 spectre_v4_enable_hw_mitigation emits exact PSTATE SSBS
     * .inst encodings as zero-operand volatile GNU source ASM, without a
     * memory clobber. C source admission already recognizes these four
     * privileged selectors; MachineIR separately seals the exact encoding. */
    if (instruction->source_asm_machine_form_id == 0U &&
        instruction->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                                 MINIC_MACHINE_EFFECT_VOLATILE) &&
        minic_aarch64_linux_pstate_imm_opcode(
            instruction->source_asm_template,
            instruction->source_asm_length) != 0U)
        return true;
    /* Cavium ThunderX's read-after-read workaround uses exactly four or
     * eight NOPs, with no operands or hidden side effects. */
    if (instruction->source_asm_machine_form_id == 0U &&
        instruction->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                                 MINIC_MACHINE_EFFECT_VOLATILE) &&
        minic_aarch64_linux_rept_nop_count(
            instruction->source_asm_template,
            instruction->source_asm_length) != 0U)
        return true;
    /* A Linux BUG_ON() is a *single*, self-contained zero-operand source
     * effect. Its paired numeric labels and ELF __bug_table relocations are
     * part of the same typed MachineIR instruction. Reuse the admission
     * recognizer; no arbitrary `.pushsection`/BRK string escapes here. */
    if (instruction->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                                 MINIC_MACHINE_EFFECT_VOLATILE) ||
        instruction->source_asm_machine_form_id != 0U)
        return false;
    linux_bug.is_volatile = true;
    linux_bug.template_text = instruction->source_asm_template;
    linux_bug.template_length = instruction->source_asm_length;
    return minic_multi_direct_linux_bug_asm(&linux_bug,
        "14470", "14471", "14472", "brk0x800", true);
}

/* Linux BUG/TRAP has one GNU I(imm) scalar input, but no physical
 * register, memory, label or target feature side effects. Validate the exact
 * ISA source and the complete typed imm16/intersection encoding independently
 * of C qualification. The regular compiler-generated BRK #0 remains separate. */
static bool a64_brk_source_form(const MinicMachineInstruction *ins) {
    const MinicMachineOperand *op;
    if (ins == NULL ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_BRK ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->operand_count != 1U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) ||
        !aarch64_linux_brk_template(ins->source_asm_template,
            ins->source_asm_length)) return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_INTEGER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, "I") == 0 &&
        aarch64_linux_brk_value(op->value.integer);
}

static bool a64_brk_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_brk_source_form(ins);
}

static bool a64_brk_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    return index == 0U && a64_brk_policy_form(fn, ins, context);
}

static bool a64_brk_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    return index == 0U && modifier == 0U &&
        a64_brk_policy_binding(fn, ins, index, context) &&
        (output == NULL ||
         fprintf(output, "#%" PRId64, ins->operands[0].value.integer) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_brk_policy = {
    a64_brk_policy_form,
    a64_brk_policy_binding,
    a64_brk_policy_operand,
    NULL
};

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
        (ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_WFXT &&
         ins->source_asm_machine_form_variant != 0U)) return false;
    switch (ins->source_asm_machine_form_id) {
    case AARCH64_MACHINE_ASM_WFXT:
        return ins->effects == (source | vol | barrier) &&
            ins->source_asm_machine_form_variant != 0U &&
            ins->source_asm_machine_form_variant ==
                aarch64_linux_wfxt_form(ins->source_asm_template,
                    ins->source_asm_length);
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

/* KVM's exact ALTERNATIVE_CB VA mask uses one live +r x9 and leaves its
 * section/callback/PREL32 directives with GNU GAS. Machine rejects a
 * partially substituted template or a different physical binding. */
static bool a64_kvm_va_mask_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_KVM_VA_MASK &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) &&
        ins->operand_count == 1U && ins->operands != NULL &&
        ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        aarch64_linux_kvm_va_mask_form(ins->source_asm_template,
            ins->source_asm_length);
}

static bool a64_kvm_va_mask_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    if (index != 0U || !a64_kvm_va_mask_form(fn, ins, context))
        return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL && strcmp(op->asm_constraint, "+r") == 0 &&
        op->asm_name == NULL &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == (MINIC_MACHINE_REGISTER_READ |
                                     MINIC_MACHINE_REGISTER_WRITE);
}

static bool a64_kvm_va_mask_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    return modifier == 0U &&
        a64_kvm_va_mask_binding(fn, ins, index, context) &&
        (output == NULL || fputs("x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_kvm_va_mask_policy = {
    a64_kvm_va_mask_form, a64_kvm_va_mask_binding,
    a64_kvm_va_mask_operand, NULL
};

/* XPACLRI's GNU +r transport is bound to physical x30: `hint #7` affects
 * LR itself, even though the template does not spell a %0 placeholder. */
static bool a64_xpaclri_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_XPACLRI &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) &&
        ins->operand_count == 1U && ins->operands != NULL &&
        ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        aarch64_linux_xpaclri_form(ins->source_asm_template,
            ins->source_asm_length);
}

static bool a64_xpaclri_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    if (index != 0U || !a64_xpaclri_policy_form(fn, ins, context))
        return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL && strcmp(op->asm_constraint, "+r") == 0 &&
        op->asm_name == NULL &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 30U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == (MINIC_MACHINE_REGISTER_READ |
                                     MINIC_MACHINE_REGISTER_WRITE);
}

static bool a64_xpaclri_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    return modifier == 0U &&
        a64_xpaclri_policy_binding(fn, ins, index, context) &&
        (output == NULL || fputs("x30", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_xpaclri_policy = {
    a64_xpaclri_policy_form, a64_xpaclri_policy_binding,
    a64_xpaclri_policy_operand, NULL
};

/* The Qualcomm source form carries the old LR in one early-clobber x9
 * output while sixteen BL-to-next-instruction operations poison the return
 * predictor. Preserve the original .rept sequence, and reject any forged
 * register, effect or operand role before GNU template substitution. */
static bool a64_qcom_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_QCOM_RETURN_STACK &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) &&
        ins->operand_count == 1U && ins->operands != NULL &&
        ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        aarch64_linux_qcom_return_stack_form(ins->source_asm_template,
            ins->source_asm_length);
}

static bool a64_qcom_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    if (index != 0U || !a64_qcom_policy_form(fn, ins, context)) return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, "=&r") == 0 &&
        op->asm_name == NULL &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == MINIC_MACHINE_REGISTER_WRITE;
}

static bool a64_qcom_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    return modifier == 0U &&
        a64_qcom_policy_binding(fn, ins, index, context) &&
        (output == NULL || fputs("x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_qcom_policy = {
    a64_qcom_policy_form, a64_qcom_policy_binding,
    a64_qcom_policy_operand, NULL
};

/* Linux EL2 AT S1E1R fault recovery has five sealed physical roles. The
 * source contains the real __kvm_ex_table PREL32 pair, restored SPSR/ELR
 * instructions and a fixed -EFAULT immediate; no ordinary `at` reduction
 * is valid. */
static bool a64_kvm_at_fixup_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_KVM_AT_FIXUP &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
                         MINIC_MACHINE_EFFECT_MEMORY_READ |
                         MINIC_MACHINE_EFFECT_MEMORY_WRITE) &&
        ins->operand_count == 5U && ins->operands != NULL &&
        ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        aarch64_linux_kvm_at_fixup_template(ins->source_asm_template,
            ins->source_asm_length);
}

static bool a64_kvm_at_fixup_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    static const char *const constraints[] = {
        "+r", "=&r", "=&r", "r", "i"
    };
    const MinicMachineOperand *op;
    if (index >= 5U || !a64_kvm_at_fixup_form(fn, ins, context))
        return false;
    op = &ins->operands[index];
    if (op->asm_name != NULL || op->asm_constraint == NULL ||
        strcmp(op->asm_constraint, constraints[index]) != 0)
        return false;
    if (index == 4U)
        return op->kind == MINIC_MACHINE_OPERAND_INTEGER &&
            op->value.integer == -14;
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U + (unsigned int)index &&
        op->value.physical.reg.width_bits == (index == 0U ? 32U : 64U) &&
        op->value.physical.access == (index == 0U ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            index < 3U ? MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_READ);
}

static bool a64_kvm_at_fixup_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (!a64_kvm_at_fixup_binding(fn, ins, index, context)) return false;
    if (index == 4U) return modifier == 0U &&
        (output == NULL || fputs("#-14", output) >= 0);
    if (modifier != 0U && !(index == 0U && modifier == 'w'))
        return false;
    return output == NULL || fprintf(output,
        "%c%u", index == 0U ? 'w' : 'x', 9U + (unsigned int)index) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_kvm_at_fixup_policy = {
    a64_kvm_at_fixup_form, a64_kvm_at_fixup_binding,
    a64_kvm_at_fixup_operand, NULL
};

/* The physical source-ASM representation of Linux arm64 io.h's raw writes
 * and feature-91 acquire-read alternatives. Both source templates are
 * reclassified by the immutable ISA catalog before any GNU substitution.
 * Physical x9 carries the scalar and x10 the pointer: no Q-memory role or
 * legacy direct FILE* emitter is consulted. */
static bool a64_raw_io_form(const MinicMachineInstruction *ins) {
    const Aarch64RawIoRead *read;
    const Aarch64RawIoStore *store;
    const bool is_read = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_RAW_IO_READ;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        (!is_read ? MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U) |
        (is_read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                   MINIC_MACHINE_EFFECT_MEMORY_WRITE);
    if (ins == NULL || ins->source_asm_template == NULL ||
        ins->operand_count != 2U || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != effects) return false;
    read = aarch64_raw_io_read(ins->source_asm_template,
        ins->source_asm_length);
    store = aarch64_raw_io_store(ins->source_asm_template,
        ins->source_asm_length);
    if (is_read)
        return read != NULL && store == NULL &&
            ins->source_asm_machine_form_variant == read->width;
    return ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_RAW_IO_STORE &&
        store != NULL && read == NULL &&
        ins->source_asm_machine_form_variant == store->width;
}

static bool a64_raw_io_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_raw_io_form(ins);
}

static bool a64_raw_io_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    const bool read = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_RAW_IO_READ;
    (void)context;
    if (!a64_raw_io_policy_form(fn, ins, NULL) || index >= 2U ||
        ins->operands == NULL) return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL && op->asm_name == NULL &&
        strcmp(op->asm_constraint, index == 0U ?
            (read ? "=r" : "rZ") : "r") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U + index &&
        op->value.physical.reg.width_bits ==
            (index == 1U || ins->source_asm_machine_form_variant == 8U ?
                64U : 32U) &&
        op->value.physical.access == (read && index == 0U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_raw_io_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const unsigned char expected = index == 0U ?
        (ins->source_asm_machine_form_variant == 8U ?
            /* GNU raw64 uses %x0; the feature-91 read64 uses %0. */
            ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_RAW_IO_READ ?
                0U : 'x' : 'w') : 0U;
    (void)labels;
    return a64_raw_io_policy_binding(fn, ins, index, context) &&
        modifier == expected &&
        (output == NULL || fputs(index == 1U ? "x10" :
            ins->source_asm_machine_form_variant == 8U ? "x9" : "w9",
            output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_raw_io_policy = {
    a64_raw_io_policy_form,
    a64_raw_io_policy_binding,
    a64_raw_io_policy_operand,
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
    Aarch64LinuxVheSysregAlternative alternative = {0};
    const char *text;
    size_t len;
    bool valid = false;
    if (ins == NULL || ins->source_asm_template == NULL ||
        ins->operand_count != 1U || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         (ins->source_asm_machine_form_id ==
                            AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE ?
                            MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U)))
        return false;
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
    case AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE:
        /* Both original .irp/.macro arms and their feature-47/50
         * .altinstructions relocation are recognized as one sealed form. */
        valid = aarch64_linux_vhe_sysreg_alternative_form(text, len,
            &alternative);
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
    Aarch64LinuxVheSysregAlternative alternative = {0};
    const bool vhe = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE;
    const bool output = ins != NULL &&
        (ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_MRS ||
         ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ ||
         (vhe && aarch64_linux_vhe_sysreg_alternative_form(
             ins->source_asm_template, ins->source_asm_length,
             &alternative) && alternative.is_read));
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
        (!vhe || operand->asm_name == NULL) &&
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
    Aarch64LinuxVheSysregAlternative alternative = {0};
    const bool vhe_write = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE &&
        aarch64_linux_vhe_sysreg_alternative_form(
            ins->source_asm_template, ins->source_asm_length,
            &alternative) && !alternative.is_read;
    const bool tegra = msr &&
        aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length) != NULL &&
        strcmp(aarch64_linux_msr_register(ins->source_asm_template,
            ins->source_asm_length), "s3_0_c15_c0_4") == 0;
    return modifier == (ins != NULL &&
        (ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE ||
         vhe_write || (msr && !tegra)) ? 'x' : 0U) &&
        a64_sysreg_policy_binding(fn, ins, index, context) &&
        (output == NULL || fputs("x9", output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_sysreg_policy = {
    a64_sysreg_policy_form,
    a64_sysreg_policy_binding,
    a64_sysreg_policy_operand,
    NULL
};

/* The Linux original .irp/.equ MRS_S grammar is sealed upstream. Resolve
 * only its two admitted encodings: RNDR and RNDRRS write x9 then consume
 * their NZCV with CSET w10. The implicit flag clobber is physical MachineIR
 * state, never a free-form source ASM shortcut. */
static bool a64_rng_sysreg_form(const MinicMachineInstruction *ins) {
    size_t i;
    unsigned int variant;
    if (ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_RNG_SYSREG ||
        (variant = aarch64_linux_rng_sysreg_form(ins->source_asm_template,
            ins->source_asm_length)) == 0U ||
        ins->source_asm_machine_form_variant != variant ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 4U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint, "=r") != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != 9U + (unsigned int)i ||
            op->value.physical.reg.width_bits != (i == 0U ? 64U : 32U) ||
            op->value.physical.access != MINIC_MACHINE_REGISTER_WRITE)
            return false;
    }
    return true;
}

/* Type-2 Linux user cache-fault recovery requires two PREL32 relocations
 * and a status-register number in the exception table. Recheck both sealed
 * roles before admitting the hardware operation or writing its table. */
static const char *a64_user_cache_fault_form(
    const MinicMachineInstruction *ins) {
    const MinicMachineOperand *status, *addr;
    const char *opcode;
    if (ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_USER_CACHE_FAULT ||
        ins->source_asm_machine_form_variant != 0U ||
        (opcode = aarch64_linux_user_cache_fault_form(
            ins->source_asm_template, ins->source_asm_length)) == NULL ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
                         MINIC_MACHINE_EFFECT_MEMORY_READ |
                         MINIC_MACHINE_EFFECT_MEMORY_WRITE) ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return NULL;
    status = &ins->operands[0];
    addr = &ins->operands[1];
    if (status->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        status->asm_name != NULL || status->asm_constraint == NULL ||
        strcmp(status->asm_constraint, "=r") != 0 ||
        status->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        status->value.physical.reg.number != 9U ||
        status->value.physical.reg.width_bits != 32U ||
        status->value.physical.access != MINIC_MACHINE_REGISTER_WRITE ||
        addr->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        addr->asm_name != NULL || addr->asm_constraint == NULL ||
        strcmp(addr->asm_constraint, "r") != 0 ||
        addr->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        addr->value.physical.reg.number != 10U ||
        addr->value.physical.reg.width_bits != 64U ||
        addr->value.physical.access != MINIC_MACHINE_REGISTER_READ)
        return NULL;
    return opcode;
}

/* The full original Linux user-access template is one faulting machine
 * effect plus its associated GNU __ex_table fixup.  x9 carries the errno
 * read/write operand, x10 the user scalar, and x11 the user pointer.  The
 * C lvalue writeback is a separate typed STRB/STRH/STR instruction selected
 * by MachineCore; it must never inherit x10's 64-bit register width. */
static bool a64_uaccess_form(const MinicMachineInstruction *ins) {
    Aarch64UaccessForm form;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    return ins != NULL && ins->source_asm_template != NULL &&
        aarch64_linux_uaccess_form(ins->source_asm_template,
            ins->source_asm_length, &form) &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_UACCESS &&
        ins->source_asm_machine_form_variant ==
            (((unsigned int)form.is_load << 16) |
             ((unsigned int)form.width << 8) | form.exception_type) &&
        ins->operand_count == 3U && ins->implicit_count == 0U &&
        ins->effects == (effects |
            (form.is_load ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                            MINIC_MACHINE_EFFECT_MEMORY_WRITE)) &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough;
}

static bool a64_uaccess_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        a64_uaccess_form(ins);
}

static bool a64_uaccess_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    Aarch64UaccessForm form;
    const MinicMachineOperand *op;
    const unsigned int access = index == 0U ?
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
        index == 1U ? MINIC_MACHINE_REGISTER_WRITE :
                      MINIC_MACHINE_REGISTER_READ;
    (void)context;
    if (!a64_uaccess_policy_form(fn, ins, NULL) || index >= 3U ||
        ins->operands == NULL ||
        !aarch64_linux_uaccess_form(ins->source_asm_template,
            ins->source_asm_length, &form)) return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, index == 0U ? "+r" :
            index == 1U ? (form.is_load ? "=r" : "rZ") : "r") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U + index &&
        op->value.physical.reg.width_bits ==
            (index == 0U || (index == 1U && form.width < 8U) ? 32U : 64U) &&
        op->value.physical.access ==
            (index == 1U && !form.is_load ?
                MINIC_MACHINE_REGISTER_READ : access);
}

static bool a64_uaccess_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const char *word = index == 0U ? "w9" :
        index == 1U ? "w10" : "w11";
    const char *wide = index == 0U ? "x9" :
        index == 1U ? "x10" : "x11";
    (void)labels;
    return a64_uaccess_policy_binding(fn, ins, index, context) &&
        ((index == 0U && modifier == 'w') ||
         (index == 1U && (modifier == 'w' || modifier == 'x')) ||
         (index == 2U && modifier == 0U)) &&
        (output == NULL || fputs(modifier == 'w' ? word : wide, output) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_uaccess_policy = {
    a64_uaccess_policy_form,
    a64_uaccess_policy_binding,
    a64_uaccess_policy_operand,
    NULL
};


/* Original Linux jump_label (PREL32/PREL64) and capability alternatives
 * (feature-38 normal/callback PREL32) are exact, control-flow-bearing source
 * fragments. The Core-created GNU immediate and C label are independently
 * checked as typed Machine operands before the original section text emits. */
static unsigned int a64_source_goto_kind(const MinicMachineInstruction *ins) {
    if (ins == NULL || ins->source_asm_template == NULL) return 0U;
    if (aarch64_linux_jump_label_form(ins->source_asm_template,
            ins->source_asm_length) != 0U) return 1U;
    if (aarch64_linux_cap_goto_text(ins->source_asm_template,
            ins->source_asm_length, true, false)) return 2U;
    if (aarch64_linux_cap_goto_text(ins->source_asm_template,
            ins->source_asm_length, true, true)) return 3U;
    if (aarch64_linux_cap_goto_text(ins->source_asm_template,
            ins->source_asm_length, false, false)) return 4U;
    if (aarch64_linux_cap_goto_text(ins->source_asm_template,
            ins->source_asm_length, false, true)) return 5U;
    return 0U;
}

static bool a64_source_goto_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    const unsigned int kind = a64_source_goto_kind(ins);
    const MinicMachineOperand *value, *label;
    const bool baked = kind == 4U || kind == 5U;
    const bool deferred = baked && ins != NULL &&
        strstr(ins->source_asm_template, "%0") != NULL;
    const char *const name = kind == 3U || kind == 5U ?
        "l_no" : "l_yes";
    (void)context;
    if (fn == NULL || fn->target == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        kind == 0U || ins->source_asm_machine_form_id != 0U ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->operand_count != (deferred ? 2U : baked ? 1U : 2U) ||
        ins->operands == NULL ||
        ins->implicit_count != 0U ||
        !ins->source_asm_terminates_block ||
        !ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_BRANCH)) return false;
    label = &ins->operands[baked && !deferred ? 0U : 1U];
    if (label->kind != MINIC_MACHINE_OPERAND_BLOCK ||
        label->asm_constraint != NULL ||
        label->asm_name == NULL || strcmp(label->asm_name, name) != 0)
        return false;
    if (baked && !deferred) return true;
    value = &ins->operands[0];
    if (value->asm_constraint == NULL ||
        strcmp(value->asm_constraint, "i") != 0)
        return false;
    if (deferred) {
        static const char prefix[] = "__minic_deferred_asm_immediate_";
        const char *const symbol = value->kind ==
            MINIC_MACHINE_OPERAND_SYMBOL ?
            value->value.symbol.name : NULL;
        const unsigned char *digits;
        if (value->kind != MINIC_MACHINE_OPERAND_SYMBOL ||
            value->asm_name != NULL ||
            value->value.symbol.relocation !=
                MINIC_A64_RELOC_SOURCE_IMMEDIATE ||
            value->value.symbol.addend != 0 || symbol == NULL ||
            strncmp(symbol, prefix, sizeof(prefix) - 1U) != 0)
            return false;
        digits = (const unsigned char *)(symbol + sizeof(prefix) - 1U);
        /* Core emits the exact typed deferred witness as
         * __minic_deferred_asm_immediate_<ordinal>_0. Reject other suffixes
         * instead of admitting an arbitrary assembly-time symbol. */
        if (*digits < '0' || *digits > '9' ||
            (*digits == '0' && digits[1] != '_')) return false;
        do { ++digits; } while (*digits >= '0' && *digits <= '9');
        return digits[0] == '_' && digits[1] == '0' &&
            digits[2] == '\0';
    }
    if (kind == 1U)
        return value->kind == MINIC_MACHINE_OPERAND_SYMBOL &&
            value->asm_name == NULL &&
            value->value.symbol.relocation ==
                MINIC_A64_RELOC_SOURCE_IMMEDIATE &&
            aarch64_linux_jump_key_addend(value->value.symbol.addend);
    return value->kind == MINIC_MACHINE_OPERAND_INTEGER &&
        value->asm_name != NULL &&
        strcmp(value->asm_name, "cpucap") == 0 &&
        value->value.integer >= 0 && value->value.integer < (1 << 15);
}

static bool a64_source_goto_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    (void)context;
    return index < 2U && a64_source_goto_policy_form(fn, ins, NULL);
}

static bool a64_source_goto_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *op;
    const unsigned int kind = a64_source_goto_kind(ins);
    const bool baked = kind == 4U || kind == 5U;
    const bool deferred = baked && ins != NULL &&
        strstr(ins->source_asm_template, "%0") != NULL;
    (void)context;
    if (!a64_source_goto_policy_binding(fn, ins, index, NULL))
        return false;
    op = &ins->operands[index];
    if (index == (baked && !deferred ? 0U : 1U)) {
        if (modifier != 'l') return false;
        if (output == NULL) return true;
        return labels != NULL && labels->names != NULL &&
            op->value.block_id < labels->count &&
            labels->names[op->value.block_id] != NULL &&
            fputs(labels->names[op->value.block_id], output) >= 0;
    }
    if (deferred) {
        if (modifier != 0U) return false;
        return output == NULL ||
            fputs(op->value.symbol.name, output) >= 0;
    }
    if (kind == 1U) {
        if (modifier != 'c') return false;
        if (output == NULL) return true;
        return op->value.symbol.addend == 0 ?
            fprintf(output, "%s", op->value.symbol.name) >= 0 :
            fprintf(output, "%s+%" PRId64, op->value.symbol.name,
                op->value.symbol.addend) >= 0;
    }
    if (modifier != 0U) return false;
    return output == NULL ||
        fprintf(output, "%" PRId64, op->value.integer) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_source_goto_policy = {
    a64_source_goto_policy_form,
    a64_source_goto_policy_binding,
    a64_source_goto_policy_operand,
    NULL
};


/* KVM nVHE ticket lock/unlock: exact original Linux ALTERNATIVE source
 * remains intact (feature33 PREL32 entries, numbered labels and LSE
 * directives). Only two source-sealed physical forms are legal; every
 * early-clobber result has its own register distinct from the live memory
 * address carriers. */
static bool a64_hyp_spin_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    const Aarch64LinuxHypSpinForm form = ins != NULL ?
        aarch64_linux_hyp_spin_form(ins->source_asm_template,
            ins->source_asm_length) : AARCH64_LINUX_HYP_SPIN_NONE;
    const size_t count = form == AARCH64_LINUX_HYP_SPIN_LOCK ? 5U : 2U;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_HYP_SPIN &&
        form != AARCH64_LINUX_HYP_SPIN_NONE &&
        ins->source_asm_machine_form_variant == (unsigned int)form &&
        ins->operand_count == count && ins->operands != NULL &&
        ins->implicit_count == 0U && ins->effects == effects &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough;
}

static bool a64_hyp_spin_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const bool lock = ins != NULL &&
        ins->source_asm_machine_form_variant ==
            AARCH64_LINUX_HYP_SPIN_LOCK;
    const size_t count = lock ? 5U : 2U;
    const unsigned int number = lock ? 9U + (unsigned int)index :
        (index == 0U ? 12U : 9U);
    const bool output = lock ? index < 3U : index == 1U;
    const char *constraint = lock ?
        (index < 3U ? "=&r" : index == 3U ? "+Q" : "Q") :
        (index == 0U ? "=Q" : "=&r");
    const MinicMachineOperand *op;
    (void)context;
    if (!a64_hyp_spin_form(fn, ins, NULL) || index >= count ||
        ins->operands == NULL) return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, constraint) == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == number &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == (output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_hyp_spin_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const bool lock = ins != NULL &&
        ins->source_asm_machine_form_variant ==
            AARCH64_LINUX_HYP_SPIN_LOCK;
    const bool memory = lock ? index >= 3U : index == 0U;
    const unsigned int number = lock ? 9U + (unsigned int)index :
        (index == 0U ? 12U : 9U);
    (void)labels;
    if (!a64_hyp_spin_binding(fn, ins, index, context)) return false;
    if (memory) return modifier == 0U &&
        (output == NULL || fprintf(output, "[x%u]", number) >= 0);
    return modifier == 'w' &&
        (output == NULL || fprintf(output, "w%u", number) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_hyp_spin_policy = {
    a64_hyp_spin_form, a64_hyp_spin_binding, a64_hyp_spin_operand, NULL
};

/* Linux arm64's exact 16-form CMPXCHG retry loop: five typed operands,
 * source-owned memory ordering, exclusive status and GNU local labels. */
static bool a64_cmpxchg_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    unsigned int form, effects;
    (void)context;
    if (fn == NULL || fn->target == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_CMPXCHG ||
        ins->source_asm_template == NULL ||
        ins->operand_count != 5U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return false;
    form = aarch64_linux_cmpxchg_form(ins->source_asm_template,
        ins->source_asm_length, true);
    if (!form) form = aarch64_linux_cmpxchg_form(ins->source_asm_template,
        ins->source_asm_length, false);
    effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    return form != 0U && ins->source_asm_machine_form_variant == form &&
        ins->effects == effects;
}

static bool a64_cmpxchg_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    static const unsigned int regs[5] = {13U, 9U, 10U, 11U, 12U};
    static const char *const names[5] = {
        "tmp", "oldval", "v", "old", "new"
    };
    const MinicMachineOperand *op;
    const size_t width = aarch64_linux_cmpxchg_width(
        ins != NULL ? ins->source_asm_machine_form_variant : 0U);
    const char *constraint;
    (void)context;
    if (!a64_cmpxchg_form(fn, ins, NULL) ||
        index >= 5U || ins->operands == NULL) return false;
    constraint = index < 2U ? "=&r" : index == 2U ? "+Q" :
        index == 3U ? (width == 8U ? "Lr" : "Kr") : "r";
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name != NULL &&
        strcmp(op->asm_name, names[index]) == 0 &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, constraint) == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == regs[index] &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == (index < 2U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_cmpxchg_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    static const unsigned int regs[5] = {13U, 9U, 10U, 11U, 12U};
    const size_t width = aarch64_linux_cmpxchg_width(
        ins != NULL ? ins->source_asm_machine_form_variant : 0U);
    (void)labels;
    if (!a64_cmpxchg_binding(fn, ins, index, context))
        return false;
    if (index == 2U) return modifier == 0U &&
        (output == NULL || fputs("[x10]", output) >= 0);
    if ((width == 8U && modifier != 0U &&
         !(index == 0U && modifier == 'w')) ||
        (width != 8U && modifier != 'w'))
        return false;
    return output == NULL || fprintf(output, "%c%u",
        width == 8U && modifier != 'w' ? 'x' : 'w', regs[index]) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_cmpxchg_policy = {
    a64_cmpxchg_form, a64_cmpxchg_binding, a64_cmpxchg_operand, NULL
};

/* The six frozen Linux arm64 LL/SC or UACCESS families are ISA-owned
 * source templates. Core only carries scalar/memory roles; Machine seals
 * exact physical GPRs, GNU constraints, true memory effects and imm bits. */
typedef struct {
    unsigned int regs[5];
    const char *constraints[5], *names[5];
    bool outputs[5], immediate;
    size_t count, memory;
} A64AtomicPhysicalForm;

static bool a64_atomic_physical_form(
    const MinicMachineInstruction *ins, A64AtomicPhysicalForm *p) {
    Aarch64AtomicForm a = {0};
    Aarch64PerCpuForm cpu = {0};
    Aarch64XchgForm x = {0};
    const char *in = NULL;
    size_t width = 0U, i;
    unsigned int mode = 0U, variant, id;
    const unsigned int mem_effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    if (ins == NULL || p == NULL ||
        ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_template == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->implicit_count != 0U) return false;
    memset(p, 0, sizeof(*p));
    id = ins->source_asm_machine_form_id;
    if (id == AARCH64_MACHINE_ASM_UACCESS_BIC) {
        if (!aarch64_linux_uaccess_bic_template(ins->source_asm_template,
                ins->source_asm_length) ||
            ins->source_asm_machine_form_variant != 0U ||
            ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                             MINIC_MACHINE_EFFECT_VOLATILE))
            return false;
        p->count = 3U;
        p->regs[0] = 9U; p->regs[1] = 10U;
        p->constraints[0] = "=r";
        p->constraints[1] = "r";
        p->constraints[2] = "i";
        p->outputs[0] = true; p->immediate = true;
    } else {
        switch (id) {
        case AARCH64_MACHINE_ASM_ATOMIC_RMW:
            if (!aarch64_atomic_form(ins->source_asm_template,
                    ins->source_asm_length, false, &a)) return false;
            width = a.width; mode = a.mode;
            in = width == 8U ? a.op->rmw_constraint : a.op->fetch32_constraint;
            break;
        case AARCH64_MACHINE_ASM_ATOMIC_FETCH:
            if (!aarch64_atomic_form(ins->source_asm_template,
                    ins->source_asm_length, true, &a)) return false;
            width = a.width; mode = a.mode;
            in = width == 8U ? a.op->fetch64_constraint : a.op->fetch32_constraint;
            break;
        case AARCH64_MACHINE_ASM_ATOMIC_RETURN:
            if (!aarch64_atomic_return_form(ins->source_asm_template,
                    ins->source_asm_length, &a)) return false;
            width = a.width; mode = a.mode; in = a.op->rmw_constraint;
            break;
        case AARCH64_MACHINE_ASM_XCHG:
            if (!aarch64_linux_xchg_form(ins->source_asm_template,
                    ins->source_asm_length, &x)) return false;
            width = x.width; mode = x.mode; in = "r";
            break;
        case AARCH64_MACHINE_ASM_PERCPU_RMW:
            if (!aarch64_linux_percpu_rmw_form(ins->source_asm_template,
                    ins->source_asm_length, &cpu) || cpu.op == NULL)
                return false;
            for (i = 0U; i < 6U; ++i)
                if (cpu.op == &aarch64_atomic_operations[i]) break;
            if (i == 6U) return false;
            width = cpu.width; mode = (unsigned int)i; in = "r";
            break;
        default: return false;
        }
        variant = ((unsigned int)width << 8) | mode;
        if ((width != 1U && width != 2U && width != 4U && width != 8U) ||
            (id != AARCH64_MACHINE_ASM_XCHG &&
             id != AARCH64_MACHINE_ASM_PERCPU_RMW &&
             width != 4U && width != 8U) ||
            ins->source_asm_machine_form_variant != variant ||
            ins->effects != mem_effects || in == NULL)
            return false;
        if (id == AARCH64_MACHINE_ASM_ATOMIC_FETCH) {
            static const unsigned int regs[] = {9U,13U,12U,10U,11U};
            p->count = 5U; p->memory = 3U;
            for (i=0U;i<5U;++i)p->regs[i]=regs[i];
            for (i=0U;i<3U;++i) {
                p->constraints[i]="=&r";p->outputs[i]=true;
            }
            p->constraints[3]="+Q";p->constraints[4]=in;
        } else {
            static const unsigned int regs[] = {9U,12U,10U,11U};
            p->count=4U;p->memory=2U;
            for(i=0U;i<4U;++i)p->regs[i]=regs[i];
            if(id==AARCH64_MACHINE_ASM_PERCPU_RMW) {
                p->regs[0]=12U;p->regs[1]=9U;
                if(strstr(ins->source_asm_template,"%w[loop]")!=NULL ||
                   strstr(ins->source_asm_template,"%[loop]")!=NULL) {
                    p->names[0]="loop";
                    p->names[1]=cpu.returns_new?"ret":"tmp";
                    p->names[2]="ptr";p->names[3]="val";
                }
            }
            p->outputs[0]=true;p->outputs[1]=true;
            p->constraints[0]="=&r";p->constraints[1]="=&r";
            p->constraints[2]="+Q";p->constraints[3]=in;
        }
    }
    return ins->operand_count==p->count && ins->operands!=NULL;
}

static bool a64_atomic_policy_form(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    void *context) {
    A64AtomicPhysicalForm p;
    (void)context;
    return fn!=NULL && fn->target!=NULL &&
        fn->target->architecture==MINIC_TARGET_ARCH_AARCH64 &&
        a64_atomic_physical_form(ins,&p);
}

static bool a64_atomic_policy_binding(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    size_t index, void *context) {
    A64AtomicPhysicalForm p;
    const MinicMachineOperand *op;
    if (!a64_atomic_policy_form(fn,ins,context) ||
        !a64_atomic_physical_form(ins,&p) || index>=p.count) return false;
    op=&ins->operands[index];
    if(op->asm_constraint==NULL ||
       strcmp(op->asm_constraint,p.constraints[index])!=0 ||
       (op->asm_name==NULL)!=(p.names[index]==NULL) ||
       (op->asm_name!=NULL &&
        strcmp(op->asm_name,p.names[index])!=0)) return false;
    if(p.immediate && index==2U)
        return op->kind==MINIC_MACHINE_OPERAND_INTEGER &&
            op->value.integer==(INT64_C(1)<<55);
    return op->kind==MINIC_MACHINE_OPERAND_REGISTER &&
        op->value.physical.reg.bank==MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number==p.regs[index] &&
        op->value.physical.reg.width_bits==64U &&
        op->value.physical.access==(p.outputs[index]?
            MINIC_MACHINE_REGISTER_WRITE:MINIC_MACHINE_REGISTER_READ);
}

static bool a64_atomic_policy_operand(FILE *out,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    A64AtomicPhysicalForm p;
    (void)labels;
    if (!a64_atomic_policy_binding(fn,ins,index,context) ||
        !a64_atomic_physical_form(ins,&p)) return false;
    if(p.immediate && index==2U)
        return modifier==0U && (out==NULL ||
            fprintf(out,"#0x%" PRIx64,
                (uint64_t)ins->operands[index].value.integer)>=0);
    if(!p.immediate && index==p.memory)
        return modifier==0U &&
            (out==NULL || fputs("[x10]",out)>=0);
    return (modifier==0U || modifier=='w') &&
        (out==NULL || fprintf(out,"%c%u",
            modifier=='w'?'w':'x',p.regs[index])>=0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_atomic_source_policy={
    a64_atomic_policy_form,
    a64_atomic_policy_binding,
    a64_atomic_policy_operand,
    NULL
};


/* Original Linux 6.6 arm64 per-CPU TPIDR_EL1/EL2 alternatives. The
 * target seals the complete two-PREL32 feature-47 patch record. */
static bool a64_percpu_alt_form(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    void *context) {
    const bool read = ins != NULL &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_PERCPU_ALTERNATIVE;
    const bool write = ins != NULL &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_PERCPU_WRITE_ALTERNATIVE;
    const unsigned int read_effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_MEMORY_READ;
    const unsigned int write_effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        (read || write) &&
        ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        ins->operand_count == (read ? 2U : 1U) &&
        ins->operands != NULL &&
        ins->effects == (read ? read_effects : write_effects) &&
        (read ? aarch64_linux_percpu_alternative_template(
                    ins->source_asm_template, ins->source_asm_length) :
                aarch64_linux_percpu_write_alternative_template(
                    ins->source_asm_template, ins->source_asm_length));
}
static bool a64_percpu_alt_binding(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    size_t index, void *context) {
    const bool read = ins != NULL &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_PERCPU_ALTERNATIVE;
    const MinicMachineOperand *op;
    const char *constraint;
    if (!a64_percpu_alt_form(fn, ins, context) ||
        index >= ins->operand_count) return false;
    constraint = read ? (index == 0U ? "=r" : "Q") : "r";
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, constraint) == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == (read && index == 1U ? 10U : 9U) &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access ==
            (read && index == 0U ?
                MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_READ);
}
static bool a64_percpu_alt_operand(
    FILE *output, const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const bool read = ins->source_asm_machine_form_id ==
        AARCH64_MACHINE_ASM_PERCPU_ALTERNATIVE;
    (void)labels;
    if (!a64_percpu_alt_binding(fn, ins, index, context) ||
        modifier != 0U) return false;
    if (output == NULL) return true;
    if (read && index == 1U) return fputs("[x10]", output) >= 0;
    return fputs("x9", output) >= 0;
}
static const MinicMachineSourceAsmTemplatePolicy a64_percpu_alt_policy = {
    a64_percpu_alt_form, a64_percpu_alt_binding,
    a64_percpu_alt_operand, NULL
};
static bool a64_percpu_alt_emit(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins) {
    const bool read = ins->source_asm_machine_form_id ==
        AARCH64_MACHINE_ASM_PERCPU_ALTERNATIVE;
    if (!minic_machine_source_asm_template_preflight(
            fn, ins, &a64_percpu_alt_policy))
        return false;
    /* Source recognizers prove each original token, including both .org
     * equal-length assertions. Resolve the sole %0 to physical x9. */
    if (fputs(
            "  .if 1 == 1\n"
            "661:\n", output) == EOF ||
        fputs(read ? "  mrs x9, tpidr_el1\n" :
                     "  msr tpidr_el1, x9\n", output) == EOF ||
        fputs(
            "662:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .word 661b - .\n"
            "  .word 663f - .\n"
            "  .hword 47\n"
            "  .byte 662b-661b\n"
            "  .byte 664f-663f\n"
            "  .popsection\n"
            "  .subsection 1\n"
            "663:\n", output) == EOF ||
        fputs(read ? "  mrs x9, tpidr_el2\n" :
                     "  msr tpidr_el2, x9\n", output) == EOF)
        return false;
    return fputs(
        "664:\n"
        "  .org . - (664b-663b) + (662b-661b)\n"
        "  .org . - (662b-661b) + (664b-663b)\n"
        "  .previous\n"
        "  .endif\n", output) != EOF;
}

static bool a64_machine_pstate_alternative(
    const MinicMachineInstruction *ins) {
    const Aarch64LinuxPstateAlternative form = ins != NULL &&
        ins->source_asm_template != NULL ?
        aarch64_linux_pstate_alternative(ins->source_asm_template,
            ins->source_asm_length) :
        (Aarch64LinuxPstateAlternative){0};
    return form.cap != 0U &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_PSTATE_ALTERNATIVE &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->operand_count == 0U && ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_COMPILER_BARRIER);
}

/* Independent physical Machine proof for Linux's five supported
 * ALTERNATIVE(BARRIER) variants. Their .altinstructions and .org length
 * assertions must survive verbatim and cannot become an opaque NOP. */
static bool a64_machine_barrier_alternative(
    const MinicMachineInstruction *ins) {
    return ins != NULL && ins->source_asm_template != NULL &&
        ins->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_BARRIER_ALTERNATIVE &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
        ins->operand_count == 0U && ins->implicit_count == 0U &&
        !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_COMPILER_BARRIER) &&
        aarch64_linux_barrier_alternative(ins->source_asm_template,
            ins->source_asm_length) != NULL;
}

/* SMCCC has no textual GNU substitution operands, but its ABI is not
 * operand-free: the already sealed original Linux source binds four returns
 * to x0..x3 and one to eight arguments to x0..x7. Revalidate this complete
 * typed physical contract on MachineIR before serializing either SMC/HVC. */
static bool a64_machine_smccc_form(const MinicMachineInstruction *ins) {
    size_t i;
    unsigned int variant;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    if (ins == NULL ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_SMCCC ||
        (variant = aarch64_linux_smccc_form(ins->source_asm_template,
            ins->source_asm_length)) == 0U ||
        ins->source_asm_machine_form_variant != variant ||
        ins->effects != effects ||
        ins->operand_count < 5U || ins->operand_count > 12U ||
        ins->operands == NULL || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough)
        return false;
    for (i = 0U; i < ins->operand_count; ++i) {
        const MinicMachineOperand *operand = &ins->operands[i];
        const bool output = i < 4U;
        if (operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            operand->asm_name != NULL || operand->asm_constraint == NULL ||
            strcmp(operand->asm_constraint, output ? "=r" : "r") != 0 ||
            operand->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            operand->value.physical.reg.number != (output ? i : i - 4U) ||
            operand->value.physical.reg.width_bits != 64U ||
            operand->value.physical.access != (output ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

static bool a64_machine_vdso_svc_form(const MinicMachineInstruction *ins) {
    static const unsigned int regs[] = {0U, 0U, 1U, 8U};
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    size_t i;
    if (ins == NULL ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_SVC ||
        !aarch64_linux_svc_form(ins->source_asm_template,
            ins->source_asm_length) ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->effects != effects || ins->operand_count != 4U ||
        ins->operands == NULL || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        const bool output = i == 0U;
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint, output ? "=r" : "r") != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.width_bits != 64U ||
            op->value.physical.reg.number != regs[i] ||
            op->value.physical.access != (output ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

static bool a64_machine_vdso_timer_form(const MinicMachineInstruction *ins) {
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE;
    const MinicMachineOperand *op;
    unsigned int variant;
    if (ins == NULL ||
        ins->source_asm_machine_form_id !=
            AARCH64_MACHINE_ASM_TIMER_ALTERNATIVE ||
        (variant = aarch64_linux_timer_alternative_form(
            ins->source_asm_template, ins->source_asm_length)) == 0U ||
        ins->source_asm_machine_form_variant != variant ||
        (ins->effects != effects &&
         ins->effects != (effects | MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) ||
        ins->operand_count != 1U ||
        ins->operands == NULL || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return false;
    op = &ins->operands[0];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, "=r") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == 9U &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == MINIC_MACHINE_REGISTER_WRITE;
}

static bool a64_machine_vdso_counter_ordering_form(
    const MinicMachineInstruction *ins) {
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ;
    size_t i;
    if (ins == NULL || ins->source_asm_machine_form_id !=
            AARCH64_MACHINE_ASM_COUNTER_ORDERING ||
        !aarch64_linux_counter_ordering_form(ins->source_asm_template,
            ins->source_asm_length) ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->effects != effects || ins->operand_count != 2U ||
        ins->operands == NULL || ins->implicit_count != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint, i == 0U ? "=r" : "r") != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.width_bits != 64U ||
            op->value.physical.reg.number != 9U + i ||
            op->value.physical.access != (i == 0U ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

/* Three exact Linux source-ASM families: exclusive compare/wait, MMIO
 * dependency ordering and fault-recoverable unaligned zero-pad. Never accept
 * arbitrary GNU template bytes or unbound compiler-selected registers. */
static bool a64_machine_extra_source_form(const MinicMachineInstruction *ins) {
    const bool cmpwait = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_CMPWAIT;
    const bool dep = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_IO_DEPENDENCY;
    const bool zeropad = ins != NULL &&
        ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_ZEROPAD;
    const unsigned int width = cmpwait ? aarch64_linux_cmpwait_form(
        ins->source_asm_template,ins->source_asm_length) : 0U;
    const size_t count = dep ? 2U : 3U;
    /* Core records both explicit GNU "memory" and typed Q/+Q hardware
     * memory effects under its effective-memory contract. The common Core
     * source-ASM builder preserves that conservative compiler barrier for
     * all three exact target-qualified forms. */
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        (cmpwait ? MINIC_MACHINE_EFFECT_VOLATILE |
                   MINIC_MACHINE_EFFECT_MEMORY_READ |
                   MINIC_MACHINE_EFFECT_MEMORY_WRITE :
         dep ? MINIC_MACHINE_EFFECT_VOLATILE :
               MINIC_MACHINE_EFFECT_VOLATILE |
               MINIC_MACHINE_EFFECT_MEMORY_READ);
    size_t i;
    if (!(cmpwait || dep || zeropad) ||
        (cmpwait && (width != 1U && width != 2U &&
                     width != 4U && width != 8U)) ||
        (dep && !aarch64_linux_io_dependency_form(
            ins->source_asm_template,ins->source_asm_length)) ||
        (zeropad && !aarch64_linux_zeropad_form(
            ins->source_asm_template,ins->source_asm_length)) ||
        ins->source_asm_machine_form_variant != (cmpwait ? width : 0U) ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != effects ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->implicit_count != 0U)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        const char *constraint = i == 0U ? (dep ? "=r" : "=&r") :
            (cmpwait && i == 1U ? "+Q" :
             zeropad && i == 2U ? "Q" : "r");
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraint) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != 9U + i ||
            op->value.physical.reg.width_bits != 64U ||
            op->value.physical.access != (i == 0U ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

/* AArch64 SHA3's four GNU `w` operands are vector registers, not GPRs.
 * Keep the complete original `.arch_extension` and `eor3` text; the common
 * GNU template expander replaces only %0..%3, leaving `.16b` intact. */
static bool a64_sha3_eor3_policy_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    uint32_t form = 0U, variant = 0U;
    (void)context;
    return fn != NULL && fn->target != NULL &&
        fn->target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        ins != NULL && ins->source_asm_machine_form_id == AARCH64_MACHINE_ASM_SHA3_EOR3 &&
        ins->source_asm_machine_form_variant == 0U &&
        ins->source_asm_template != NULL &&
        aarch64_machine_asm_identify(ins->source_asm_template,
            ins->source_asm_length, true, &form, &variant) &&
        form == AARCH64_MACHINE_ASM_SHA3_EOR3 && variant == 0U &&
        ins->effects == (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) &&
        ins->operand_count == 4U && ins->operands != NULL &&
        ins->implicit_count == 0U && !ins->source_asm_terminates_block &&
        !ins->source_asm_permits_fallthrough;
}

static bool a64_sha3_eor3_policy_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    if (!a64_sha3_eor3_policy_form(fn, ins, context) || index >= 4U)
        return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_name == NULL && op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, index == 0U ? "=w" : "w") == 0 &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_VECTOR &&
        op->value.physical.reg.number == 16U + index &&
        op->value.physical.reg.width_bits == 128U &&
        op->value.physical.access == (index == 0U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
}

static bool a64_sha3_eor3_policy_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    return modifier == 0U && a64_sha3_eor3_policy_binding(fn, ins, index, context) &&
        (output == NULL || fprintf(output, "v%u", 16U + (unsigned int)index) >= 0);
}

static const MinicMachineSourceAsmTemplatePolicy a64_sha3_eor3_policy = {
    a64_sha3_eor3_policy_form,
    a64_sha3_eor3_policy_binding,
    a64_sha3_eor3_policy_operand,
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
              instruction, &a64_sha3_eor3_policy) ||
          minic_machine_source_asm_template_preflight(&function,
              instruction, &a64_brk_policy) ||
          minic_machine_source_asm_template_preflight(&function,
              instruction, &a64_kexec_crash_policy) ||
         a64_machine_pstate_alternative(instruction) ||
         a64_machine_barrier_alternative(instruction) ||
         a64_machine_smccc_form(instruction) ||
         a64_machine_vdso_svc_form(instruction) ||
         a64_machine_vdso_timer_form(instruction) ||
         a64_machine_vdso_counter_ordering_form(instruction) ||
         a64_machine_extra_source_form(instruction) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_hyp_spin_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_cmpxchg_policy) ||
          minic_machine_source_asm_template_preflight(&function,
              instruction, &a64_atomic_source_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_percpu_alt_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_source_goto_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_tlbi_template_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_one_gp_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_kvm_va_mask_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_xpaclri_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_qcom_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_kvm_at_fixup_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_raw_io_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_sysreg_policy) ||
         a64_rng_sysreg_form(instruction) ||
         a64_user_cache_fault_form(instruction) != NULL ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_uaccess_policy) ||
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
    if (instruction->source_asm_machine_form_id == AARCH64_MACHINE_ASM_SHA3_EOR3)
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_sha3_eor3_policy) &&
            fputc('\n', output) != EOF;
    if (a64_qcom_policy_form(function, instruction, NULL))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_qcom_policy) &&
            fputc('\n', output) != EOF;
    if (a64_kvm_at_fixup_form(function, instruction, NULL))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_kvm_at_fixup_policy) &&
            fputc('\n', output) != EOF;
    if (a64_machine_smccc_form(instruction))
        return fputs(instruction->source_asm_machine_form_variant == 1U ?
            "  hvc #0\n" : "  smc #0\n", output) != EOF;
    if (a64_rng_sysreg_form(instruction)) {
        const unsigned int variant =
            instruction->source_asm_machine_form_variant;
        return fprintf(output, "  .inst 0x%08x\n  cset w10, ne\n",
            (unsigned)(UINT32_C(0xd53b2409) | ((variant - 1U) << 5))) >= 0;
    }
    if (a64_user_cache_fault_form(instruction) != NULL) {
        const char *operation = a64_user_cache_fault_form(instruction);
        return fprintf(output,
            "1: %s, x10\n"
            "  mov w9, #0\n"
            "2:\n"
            ".pushsection __ex_table,\"a\"\n"
            "  .align 2\n"
            "  .long (1b - .)\n"
            "  .long (2b - .)\n"
            "  .short 2\n"
            "  .short %u\n"
            ".popsection\n",
            operation, (unsigned)(9U | (31U << 5))) >= 0;
    }
    if (a64_machine_vdso_svc_form(instruction))
        return fputs("  svc #0\n", output) != EOF;
    if (a64_machine_vdso_timer_form(instruction)) {
        const unsigned int variant =
            instruction->source_asm_machine_form_variant;
        /* S3_3_C14_C0_{1,2} -> S3_3_C14_C0_{5,6}. Both original GNU
         * branches are exactly sealed; only resolve their physical Rt=x9
         * and preserve feature-19 PREL32 patch metadata and .org lengths. */
        const uint32_t mrs_base = UINT32_C(0xd5300000) |
            (3U << 19) | (3U << 16) | (14U << 12) | 9U;
        return fprintf(output,
            ".if 1 == 1\n"
            "661:\n"
            "  isb\n"
            "  .inst 0x%08x\n"
            "662:\n"
            ".pushsection .altinstructions,\"a\"\n"
            "  .word 661b - .\n"
            "  .word 663f - .\n"
            "  .hword 19\n"
            "  .byte 662b-661b\n"
            "  .byte 664f-663f\n"
            ".popsection\n"
            ".subsection 1\n"
            "663:\n"
            "  nop\n"
            "  .inst 0x%08x\n"
            "664:\n"
            "  .org . - (664b-663b) + (662b-661b)\n"
            "  .org . - (662b-661b) + (664b-663b)\n"
            ".previous\n"
            ".endif\n",
            (unsigned)(mrs_base | (variant << 5)),
            (unsigned)(mrs_base | ((4U + variant) << 5))) >= 0;
    }
    if (a64_machine_vdso_counter_ordering_form(instruction))
        return fputs("  eor x9, x10, x10\n"
                     "  add x9, sp, x9\n"
                     "  ldr xzr, [x9]\n", output) != EOF;
    if (instruction->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE) {
        Aarch64LinuxVheSysregAlternative pair = {0};
        /* Original feature-47/50 GNU alternatives have exactly two
         * validated MRS_S/MSR_S encodings. Resolve the macro-owned Rt to
         * the physical x9 binding and preserve the Linux patch metadata,
         * PREL32 relocations and both equal-length instruction arms. */
        if (!a64_sysreg_source_form(instruction) ||
            !a64_sysreg_policy_binding(function, instruction, 0U, NULL) ||
            !aarch64_linux_vhe_sysreg_alternative_form(
                instruction->source_asm_template,
                instruction->source_asm_length, &pair)) return false;
        return fprintf(output,
            ".if 1 == 1\n"
            "661:\n"
            "  .inst 0x%08x\n"
            "662:\n"
            ".pushsection .altinstructions,\"a\"\n"
            "  .word 661b - .\n"
            "  .word 663f - .\n"
            "  .hword %u\n"
            "  .byte 662b-661b\n"
            "  .byte 664f-663f\n"
            ".popsection\n"
            ".subsection 1\n"
            "663:\n"
            "  .inst 0x%08x\n"
            "664:\n"
            "  .org . - (664b-663b) + (662b-661b)\n"
            "  .org . - (662b-661b) + (664b-663b)\n"
            ".previous\n"
            ".endif\n",
            (unsigned)((pair.old_opcode & ~31U) | 9U),
            pair.capability,
            (unsigned)((pair.new_opcode & ~31U) | 9U)) >= 0;
    }
    if (a64_machine_extra_source_form(instruction)) {
        if (instruction->source_asm_machine_form_id ==
                AARCH64_MACHINE_ASM_IO_DEPENDENCY)
            return fputs("  eor x9, x10, x10\n"
                         "  cbnz x9, .\n",output) != EOF;
        if (instruction->source_asm_machine_form_id ==
                AARCH64_MACHINE_ASM_ZEROPAD)
            return fputs(
                "1: ldr x9, [x11]\n"
                "2:\n"
                "  .pushsection __ex_table, \"a\"\n"
                "  .align 2\n"
                "  .long (1b - .)\n"
                "  .long (2b - .)\n"
                "  .short 4\n"
                "  .short ((9 << 0) | (10 << 5))\n"
                "  .popsection\n",output) != EOF;
        {
            const unsigned int width =
                instruction->source_asm_machine_form_variant;
            const char *load = width == 1U ? "ldxrb" :
                width == 2U ? "ldxrh" : "ldxr";
            const char *reg = width == 8U ? "x" : "w";
            return fprintf(output,
                "  sevl\n"
                "  wfe\n"
                "  %s %s9, [x10]\n"
                "  eor %s9, %s9, %s11\n"
                "  cbnz %s9, 1f\n"
                "  wfe\n"
                "1:\n",
                load,reg,reg,reg,reg,reg) >= 0;
        }
    }
    if (a64_source_goto_kind(instruction) != 0U) {
        const unsigned int kind = a64_source_goto_kind(instruction);
        const bool deferred = (kind == 4U || kind == 5U) &&
            strstr(instruction->source_asm_template, "%0") != NULL;
        if (deferred &&
            (instruction->operand_count != 2U ||
             instruction->operands == NULL ||
             fprintf(output,
                 "# MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                 "  .extern %s\n",
                 instruction->operands[0].value.symbol.name) < 0))
            return false;
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_source_goto_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    }
    if (instruction->source_asm_machine_form_id ==
            AARCH64_MACHINE_ASM_KEXEC_CRASH) {
        size_t i;
        /* Canonical GNU spelling of the already-proved 675-byte hardware
         * template. Resolve #16*N and physical %0/%1/%2 explicitly so a
         * snapshot verifier can assert zero instructions between STP pairs.
         * The only removed source bytes are whitespace and a C comment. */
        if (!minic_machine_source_asm_template_preflight(function,
                instruction, &a64_kexec_crash_policy))
            return false;
        for (i = 0U; i < 15U; ++i)
            if (fprintf(output, "  stp x%zu, x%zu, [x0, #%zu]\n",
                    i * 2U, i * 2U + 1U, i * 16U) < 0)
                return false;
        return fputs(
            "  mov x9, sp\n"
            "  stp x30, x9, [x0, #240]\n"
            "  mrs x9, CurrentEL\n"
            "  mrs x10, SPSEL\n"
            "  orr x9, x9, x10\n"
            "  mrs x10, DAIF\n"
            "  orr x9, x9, x10\n"
            "  mrs x10, NZCV\n"
            "  orr x9, x9, x10\n"
            "  adr x10, 1f\n"
            "1: stp x10, x9, [x0, #256]\n", output) != EOF;
    }
    if (a64_brk_source_form(instruction))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_brk_policy) &&
            fputc('\n', output) != EOF;
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
    if (a64_kvm_va_mask_form(function, instruction, NULL))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_kvm_va_mask_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    if (a64_xpaclri_policy_form(function, instruction, NULL))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_xpaclri_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    if (a64_raw_io_form(instruction))
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_raw_io_policy) &&
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
    if (a64_uaccess_form(instruction)) {
        Aarch64UaccessForm form;
        /* The exact original GNU .irp register table was admitted and
         * preflighted above.  Its only observable expansion in the sealed
         * form is one type-2/type-3 exception fixup that mentions physical
         * x9 and x10 (or xzr for a store). Emit its canonical, already
         * resolved GNU directives; this retains relocation identity while
         * making the width-specific native writeback auditable. */
        return aarch64_linux_uaccess_form(
                instruction->source_asm_template,
                instruction->source_asm_length, &form) &&
            minic_machine_source_asm_template_preflight(function,
                instruction, &a64_uaccess_policy) &&
            fprintf(output,
                "1: %s, [x11]\n"
                "2:\n"
                "  .pushsection __ex_table,\"a\"\n"
                "  .align 2\n"
                "  .long (1b - .)\n"
                "  .long (2b - .)\n"
                "  .short %u\n"
                "  .short ((9 << 0) | (%u << 5))\n"
                "  .popsection\n",
                form.mnemonic, form.exception_type,
                form.is_load ? 10U : 31U) >= 0;
    }
    if (instruction->source_asm_machine_form_id == AARCH64_MACHINE_ASM_HYP_SPIN)
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_hyp_spin_policy) &&
            ((instruction->source_asm_length != 0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length - 1U] == '\n') ||
             fputc('\n', output) != EOF);
    if (a64_percpu_alt_form(function, instruction, NULL))
        return a64_percpu_alt_emit(output, function, instruction);
    if(a64_atomic_policy_form(function,instruction,NULL))
        return minic_machine_source_asm_template_emit(output,function,
            instruction,labels,&a64_atomic_source_policy) &&
            ((instruction->source_asm_length!=0U &&
              instruction->source_asm_template[
                  instruction->source_asm_length-1U]=='\n') ||
             fputc('\n',output)!=EOF);
    if (instruction->source_asm_machine_form_id == AARCH64_MACHINE_ASM_CMPXCHG)
        return minic_machine_source_asm_template_emit(output, function,
            instruction, labels, &a64_cmpxchg_policy) &&
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
