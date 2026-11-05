#include "target/riscv/machine_gnu_asm.h"

#include "core/symbol_identity.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool riscv_machine_register_printable(const MinicMachineFunction *fn,
                                             const MinicMachineOperand *operand) {
    return operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
           minic_riscv_machine_validate_register(fn->target,
               &operand->value.physical.reg, NULL);
}

static bool riscv_machine_block_label(const MinicMachineFunction *fn,
                                      const MinicMachineGNUAsmLabels *labels,
                                      const MinicMachineOperand *operand,
                                      const char **result) {
    if (fn == NULL || labels == NULL || operand == NULL || result == NULL ||
        (operand->kind != MINIC_MACHINE_OPERAND_BLOCK &&
         operand->kind != MINIC_MACHINE_OPERAND_BLOCK_ADDRESS) ||
        operand->value.block_id >= fn->block_count ||
        operand->value.block_id >= labels->count || labels->names == NULL ||
        labels->names[operand->value.block_id] == NULL ||
        !minic_core_assembler_symbol_lexically_safe(
            labels->names[operand->value.block_id])) return false;
    *result = labels->names[operand->value.block_id];
    return true;
}

static bool riscv_machine_symbol(const MinicMachineFunction *fn,
                                 const MinicMachineOperand *operand,
                                 const char **result) {
    if (fn == NULL || operand == NULL || result == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_SYMBOL ||
        !minic_riscv_machine_validate_relocation(fn->target,
             operand->value.symbol.name, operand->value.symbol.relocation,
             operand->value.symbol.addend, NULL)) return false;
    *result = operand->value.symbol.name;
    return true;
}

/* Operand values are verified before output; this emitter uses only the
 * target's fixed mnemonic table and numeric physical register identities.
 * It cannot consume a pre-rendered assembly statement. */
static bool riscv_machine_emit_selected(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    MinicRiscvMachineOpcodeInfo desc;
    const MinicMachineOperand *operand;
    const char *branch_label = NULL;
    const char *symbol = NULL;
    unsigned r0 = 0U, r1 = 0U, r2 = 0U;
    (void)context;
    if (output == NULL || fn == NULL || ins == NULL ||
        !minic_riscv_machine_opcode_info(fn->target, ins->opcode, &desc) ||
        !minic_riscv_machine_validate_selected_instruction(fn, ins, NULL))
        return false;
    operand = ins->operands;
    if (ins->operand_count != 0U && operand == NULL) return false;
    if (ins->operand_count > 0U &&
        operand[0].kind == MINIC_MACHINE_OPERAND_REGISTER) {
        if (!riscv_machine_register_printable(fn, &operand[0])) return false;
        r0 = operand[0].value.physical.reg.number;
    }
    if (ins->operand_count > 1U &&
        operand[1].kind == MINIC_MACHINE_OPERAND_REGISTER) {
        if (!riscv_machine_register_printable(fn, &operand[1])) return false;
        r1 = operand[1].value.physical.reg.number;
    }
    if (ins->operand_count > 2U &&
        operand[2].kind == MINIC_MACHINE_OPERAND_REGISTER) {
        if (!riscv_machine_register_printable(fn, &operand[2])) return false;
        r2 = operand[2].value.physical.reg.number;
    }
    switch (desc.form) {
    case MINIC_RISCV_MACHINE_FORM_NONE:
        if (ins->opcode == MINIC_RISCV_MACHINE_BARRIER ||
            ins->opcode == MINIC_RISCV_MACHINE_UNREACHABLE) return true;
        return desc.mnemonic != NULL &&
               fprintf(output, "  %s\n", desc.mnemonic) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER:
        if (ins->opcode == MINIC_RISCV_MACHINE_CALL_INDIRECT)
            return fprintf(output, "  jalr ra, 0(x%u)\n", r0) >= 0;
        return false;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER:
        return desc.mnemonic != NULL &&
               fprintf(output, "  %s x%u, x%u\n", desc.mnemonic, r0, r1) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_IMMEDIATE:
        if (ins->opcode == MINIC_RISCV_MACHINE_JALR_BRANCH ||
            ins->opcode == MINIC_RISCV_MACHINE_JALR_CALL) {
            return fprintf(output, "  jalr %s, %" PRId64 "(x%u)\n",
                           ins->opcode == MINIC_RISCV_MACHINE_JALR_CALL
                               ? "ra" : "zero",
                           operand[1].value.integer, r0) >= 0;
        }
        return desc.mnemonic != NULL &&
               fprintf(output, "  %s x%u, %" PRId64 "\n",
                       desc.mnemonic, r0, operand[1].value.integer) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_IMMEDIATE:
        return desc.mnemonic != NULL &&
               fprintf(output, "  %s x%u, x%u, %" PRId64 "\n",
                       desc.mnemonic, r0, r1,
                       operand[2].value.integer) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_REGISTER:
        return desc.mnemonic != NULL &&
               fprintf(output, "  %s x%u, x%u, x%u\n",
                       desc.mnemonic, r0, r1, r2) >= 0;
    case MINIC_RISCV_MACHINE_FORM_LOAD:
    case MINIC_RISCV_MACHINE_FORM_STORE: {
        const MinicMachineAddress *address = &operand[1].value.address;
        if (operand[1].kind != MINIC_MACHINE_OPERAND_ADDRESS ||
            !minic_riscv_machine_validate_address(fn->target, address, NULL))
            return false;
        return fprintf(output, "  %s x%u, %" PRId64 "(x%u)\n",
                       desc.mnemonic, r0, address->displacement,
                       address->base.number) >= 0;
    }
    case MINIC_RISCV_MACHINE_FORM_LOAD_FRAME:
    case MINIC_RISCV_MACHINE_FORM_STORE_FRAME: {
        unsigned frame_base;
        int64_t frame_displacement;
        if (!minic_riscv_machine_resolve_frame_access(
                fn, ins->opcode, &operand[1],
                &frame_base, &frame_displacement)) return false;
        return fprintf(output, "  %s x%u, %" PRId64 "(x%u)\n",
                       desc.mnemonic, r0, frame_displacement,
                       frame_base) >= 0;
    }
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK:
        return riscv_machine_block_label(fn, labels, &operand[2], &branch_label) &&
               fprintf(output, "  %s x%u, x%u, %s\n",
                       desc.mnemonic, r0, r1, branch_label) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK:
        return riscv_machine_block_label(fn, labels, &operand[1], &branch_label) &&
               fprintf(output, "  %s x%u, %s\n", desc.mnemonic,
                       r0, branch_label) >= 0;
    case MINIC_RISCV_MACHINE_FORM_BLOCK:
        return riscv_machine_block_label(fn, labels, &operand[0], &branch_label) &&
               (ins->opcode == MINIC_RISCV_MACHINE_JAL_BLOCK
                 ? fprintf(output, "  jal zero, %s\n", branch_label)
                 : fprintf(output, "  %s %s\n", desc.mnemonic,
                           branch_label)) >= 0;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_SYMBOL:
        if (!riscv_machine_symbol(fn, &operand[1], &symbol)) return false;
        if (ins->opcode == MINIC_RISCV_MACHINE_LA_GOT)
            return fprintf(output,
                           "  .option push\n"
                           "  .option pic\n"
                           "  la x%u, %s\n"
                           "  .option pop\n", r0, symbol) >= 0;
        return fprintf(output, "  lla x%u, %s\n", r0, symbol) >= 0;
    case MINIC_RISCV_MACHINE_FORM_SYMBOL:
        return riscv_machine_symbol(fn, &operand[0], &symbol) &&
               fprintf(output, "  %s %s\n", desc.mnemonic, symbol) >= 0;
    case MINIC_RISCV_MACHINE_FORM_FENCE_MASK: {
        char predecessor[5], successor[5];
        static const unsigned masks[] = {8U, 4U, 2U, 1U};
        static const char chars[] = {'i', 'o', 'r', 'w'};
        size_t i, count;
        count = 0U;
        for (i = 0U; i < 4U; ++i)
            if (((unsigned)operand[0].value.integer & masks[i]) != 0U)
                predecessor[count++] = chars[i];
        if (count == 0U) predecessor[count++] = '0';
        predecessor[count] = '\0';
        count = 0U;
        for (i = 0U; i < 4U; ++i)
            if (((unsigned)operand[1].value.integer & masks[i]) != 0U)
                successor[count++] = chars[i];
        if (count == 0U) successor[count++] = '0';
        successor[count] = '\0';
        return fprintf(output, "  fence %s, %s\n",
                       predecessor, successor) >= 0;
    }
    default:
        return false;
    }
}

static bool riscv_machine_reject_source_asm(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins,
    void *context) {
    (void)target;
    (void)ins;
    (void)context;
    /* Source-owned GNU asm requires the later complete physical binding and
     * goto/constraint emitter. Never pretend its template is selected code. */
    return false;
}

bool minic_riscv_machine_gnu_asm_policy(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *policy) {
    MinicRiscvMachineOpcodeInfo probe;
    if (policy == NULL || !minic_riscv_machine_opcode_info(
            target, MINIC_RISCV_MACHINE_ADD, &probe)) return false;
    memset(policy, 0, sizeof(*policy));
    policy->verify.lookup_opcode = minic_riscv_machine_lookup_opcode_contract;
    policy->verify.validate_selected_instruction =
        minic_riscv_machine_validate_selected_instruction;
    policy->verify.validate_generated_features =
        minic_riscv_machine_validate_generated_features;
    policy->verify.validate_function_abi =
        minic_riscv_machine_validate_function_abi;
    policy->verify.validate_frame_object =
        minic_riscv_machine_validate_frame_object;
    policy->verify.validate_register = minic_riscv_machine_validate_register;
    policy->verify.validate_address = minic_riscv_machine_validate_address;
    policy->verify.validate_relocation = minic_riscv_machine_validate_relocation;
    policy->verify.validate_source_asm = riscv_machine_reject_source_asm;
    policy->emit_selected = riscv_machine_emit_selected;
    /* Fail closed until an actual source-ASM binding serializer has been
     * migrated. The common whole-function gate rejects SOURCE_ASM. */
    policy->emit_source_asm = NULL;
    return true;
}
