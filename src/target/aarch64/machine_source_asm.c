#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/aarch64/machine_lse_source_select.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/common/machine_source_asm_template.h"

#include <inttypes.h>
#include <string.h>

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
        if (*digits == '\0') return false;
        for (; *digits; ++digits)
            if (*digits < '0' || *digits > '9') return false;
        return true;
    }
    if (kind == 1U)
        return value->kind == MINIC_MACHINE_OPERAND_SYMBOL &&
            value->asm_name == NULL &&
            value->value.symbol.relocation ==
                MINIC_A64_RELOC_SOURCE_IMMEDIATE &&
            (value->value.symbol.addend == 0 ||
             value->value.symbol.addend == 1);
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
            fprintf(output, "%s+1", op->value.symbol.name) >= 0;
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
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        (cmpwait ? MINIC_MACHINE_EFFECT_VOLATILE |
                   MINIC_MACHINE_EFFECT_MEMORY_READ |
                   MINIC_MACHINE_EFFECT_MEMORY_WRITE :
         dep ? MINIC_MACHINE_EFFECT_VOLATILE |
               MINIC_MACHINE_EFFECT_COMPILER_BARRIER :
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
         a64_machine_pstate_alternative(instruction) ||
         a64_machine_extra_source_form(instruction) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_source_goto_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_tlbi_template_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_one_gp_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_raw_io_policy) ||
         minic_machine_source_asm_template_preflight(&function,
             instruction, &a64_sysreg_policy) ||
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
