#include "target/common/machine_source_asm_template.h"

#include <stdio.h>
#include <string.h>

static bool form(const MinicMachineFunction *fn,
                 const MinicMachineInstruction *ins, void *ctx) {
    (void)ctx;
    return fn->target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
        ins->source_asm_machine_form_id == 7U &&
        ins->source_asm_machine_form_variant == 0U;
}

static bool binding(const MinicMachineFunction *fn,
                    const MinicMachineInstruction *ins,
                    size_t id, void *ctx) {
    const MinicMachineOperand *op = &ins->operands[id];
    (void)fn;
    (void)ctx;
    if (id == 0U) return op->kind == MINIC_MACHINE_OPERAND_INTEGER &&
        op->asm_constraint != NULL && strcmp(op->asm_constraint, "i") == 0;
    if (id == 1U) return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL && strcmp(op->asm_constraint, "r") == 0;
    return id == 2U && op->kind == MINIC_MACHINE_OPERAND_BLOCK &&
        op->asm_constraint == NULL;
}

static bool operand(FILE *out, const MinicMachineFunction *fn,
                    const MinicMachineInstruction *ins,
                    const MinicMachineGNUAsmLabels *labels,
                    size_t id, unsigned char modifier, void *ctx) {
    (void)fn;
    (void)ctx;
    if (id == 0U) {
        if (modifier != 0U && modifier != 'n' && modifier != 'c')
            return false;
        if (out == NULL) return true;
        return fputs(modifier == 'n' ? "-5" : "5", out) >= 0;
    }
    if (id == 1U) {
        if (modifier != 'w' && modifier != 0U) return false;
        if (out == NULL) return true;
        return fputs(modifier == 'w' ? "w10" : "x10", out) >= 0;
    }
    if (id != 2U || modifier != 'l' ||
        (ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U)
        return false;
    if (out == NULL) return true;
    return labels != NULL && labels->names != NULL &&
        ins->operands[id].value.block_id < labels->count &&
        fputs(labels->names[ins->operands[id].value.block_id], out) >= 0;
}

static bool reject(const MinicMachineFunction *fn,
                   const MinicMachineInstruction *input,
                   const MinicMachineSourceAsmTemplatePolicy *policy,
                   const char *source, size_t size) {
    MinicMachineInstruction ins = *input;
    ins.source_asm_template = (char *)source;
    ins.source_asm_length = size;
    return !minic_machine_source_asm_template_preflight(fn, &ins, policy);
}

int main(void) {
    static const char source[] =
        "s %0,%[imm],%n0,%c0,%w[reg],%%,%=,%l[dest]\n";
    static const char expected[] =
        "s 5,5,-5,5,w10,%,123456,.Ltarget\n";
    static const char trailing[] = "nop %";
    static const char overflow[] = "nop %99999999999999999999999999999";
    static const char embedded_nul[] = {'n','o','p','\0','x'};
    static const char embedded_cr[] = {'n','o','p','\r','x'};
    const char *const names[] = {".Lentry", ".Ltarget"};
    MinicMachineGNUAsmLabels labels = {names, 2U};
    MinicTargetInfo target = {0};
    MinicMachineFunction function = {0};
    MinicMachineInstruction ins = {0};
    MinicMachineOperand operands[3] = {{0}};
    const MinicMachineSourceAsmTemplatePolicy policy = {
        form, binding, operand, NULL
    };
    FILE *out;
    char text[128] = {0};
    size_t got;
    target.architecture = MINIC_TARGET_ARCH_RISCV64;
    function.target = &target;
    operands[0].kind = MINIC_MACHINE_OPERAND_INTEGER;
    operands[0].value.integer = 5;
    operands[0].asm_constraint = "i";
    operands[0].asm_name = "imm";
    operands[1].kind = MINIC_MACHINE_OPERAND_REGISTER;
    operands[1].value.physical.reg = (MinicMachineRegister){10U,64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    operands[1].asm_constraint = "r";
    operands[1].asm_name = "reg";
    operands[2].kind = MINIC_MACHINE_OPERAND_BLOCK;
    operands[2].value.block_id = 1U;
    operands[2].asm_name = "dest";
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                  MINIC_MACHINE_EFFECT_BRANCH;
    ins.source_asm_template = (char *)source;
    ins.source_asm_length = sizeof(source) - 1U;
    ins.source_asm_machine_form_id = 7U;
    ins.source_asm_terminates_block = true;
    ins.has_source_asm_unique_id = true;
    ins.source_asm_unique_id = 123456U;
    ins.operands = operands;
    ins.operand_count = 3U;
    if (!minic_machine_source_asm_template_preflight(&function, &ins,
                                                     &policy)) return 1;
    out = tmpfile();
    if (out == NULL) return 1;
    if (!minic_machine_source_asm_template_emit(out, &function, &ins,
                                                &labels, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) {
        fclose(out);
        return 1;
    }
    got = fread(text, 1U, sizeof(text) - 1U, out);
    fclose(out);
    if (got != sizeof(expected) - 1U ||
        memcmp(text, expected, sizeof(expected) - 1U) != 0)
        return 1;
    /* A bad late goto-label binding must be rejected before any source
     * bytes reach the output stream. */
    {
        MinicMachineGNUAsmLabels missing = {names, 1U};
        out = tmpfile();
        if (out == NULL) return 1;
        if (minic_machine_source_asm_template_emit(out, &function, &ins,
                                                   &missing, &policy) ||
            fflush(out) != 0 || ftell(out) != 0L) {
            fclose(out);
            return 1;
        }
        fclose(out);
    }
    /* asm volatile("" ::: "memory") is a valid compiler barrier: an
     * empty template is admitted if the target has sealed its form. */
    {
        MinicMachineInstruction empty = ins;
        empty.source_asm_template = "";
        empty.source_asm_length = 0U;
        empty.operands = NULL;
        empty.operand_count = 0U;
        empty.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        empty.source_asm_terminates_block = false;
        if (!minic_machine_source_asm_template_preflight(&function, &empty,
                                                         &policy)) return 1;
        out = tmpfile();
        if (out == NULL) return 1;
        if (!minic_machine_source_asm_template_emit(out, &function, &empty,
                                                    &labels, &policy) ||
            fflush(out) != 0 || ftell(out) != 0L) {
            fclose(out);
            return 1;
        }
        fclose(out);
    }
    if (!reject(&function, &ins, &policy, "nop %q0", 7U) ||
        !reject(&function, &ins, &policy, "nop %l0", 7U) ||
        !reject(&function, &ins, &policy, "nop %[missing]", 14U) ||
        !reject(&function, &ins, &policy, trailing, sizeof(trailing) - 1U) ||
        !reject(&function, &ins, &policy, overflow, sizeof(overflow) - 1U) ||
        !reject(&function, &ins, &policy, embedded_nul,
                sizeof(embedded_nul)) ||
        !reject(&function, &ins, &policy, embedded_cr,
                sizeof(embedded_cr))) return 1;
    ins.has_source_asm_unique_id = false;
    ins.source_asm_unique_id = 0U;
    if (!reject(&function, &ins, &policy, source, sizeof(source) - 1U))
        return 1;
    ins.has_source_asm_unique_id = true;
    ins.source_asm_unique_id = 123456U;
    operands[1].asm_name = "imm";
    if (!reject(&function, &ins, &policy, source, sizeof(source) - 1U))
        return 1;
    operands[1].asm_name = "reg";
    ins.source_asm_terminates_block = false;
    if (!reject(&function, &ins, &policy, source, sizeof(source) - 1U))
        return 1;
    ins.source_asm_terminates_block = true;
    ins.source_asm_machine_form_id = 8U;
    if (!reject(&function, &ins, &policy, source, sizeof(source) - 1U))
        return 1;
    ins.source_asm_machine_form_id = 7U;
    operands[0].asm_constraint = "forged";
    if (!reject(&function, &ins, &policy, "nop", 3U))
        return 1;
    puts("MACHINE_SOURCE_ASM_TEMPLATE=PASS named=1 positional=1 unique=1 "
         "modifiers=1 goto=1 empty=1 missing_label=1 invalid=12");
    return 0;
}
