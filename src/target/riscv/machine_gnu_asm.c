#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_target_state.h"

#include "core/symbol_identity.h"

#include <inttypes.h>
#include <ctype.h>
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
        if (ins->opcode == MINIC_RISCV_MACHINE_JALR_BLOCK)
            return riscv_machine_block_label(fn, labels, &operand[1],
                                              &branch_label) &&
                   fprintf(output, "  jalr zero, 0(x%u)\n", r0) >= 0;
        return riscv_machine_block_label(fn, labels, &operand[1], &branch_label) &&
               fprintf(output, "  %s x%u, %s\n", desc.mnemonic,
                       r0, branch_label) >= 0;
    case MINIC_RISCV_MACHINE_FORM_BLOCK:
        if (!riscv_machine_block_label(fn, labels, &operand[0], &branch_label))
            return false;
        if (ins->opcode == MINIC_RISCV_MACHINE_JAL_BLOCK)
            return fprintf(output, "  jal zero, %s\n", branch_label) >= 0;
        return fprintf(output, "  %s %s\n", desc.mnemonic, branch_label) >= 0;
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

/* A register/constant/memory operand here was selected by the Core inline
 * asm binder, not by the compiler's normal instruction selector. The source
 * template remains owned source data, while physical bindings are checked
 * against a deliberately small, explicit GNU RISC-V constraint grammar. */
static bool riscv_asm_constraint(const MinicTargetInfo *target,
                                const MinicMachineInstruction *ins,
                                const MinicMachineOperand *operand) {
    const char *constraint;
    unsigned xlen;
    size_t i = 0U;
    bool output = false, inout = false;
    char role;
    if (target == NULL || ins == NULL || operand == NULL ||
        operand->asm_constraint == NULL ||
        (xlen = minic_riscv_target_isa(target).xlen) == 0U) return false;
    constraint = operand->asm_constraint;
    if (constraint[i] == '=' || constraint[i] == '+') {
        output = constraint[i] == '=';
        inout = constraint[i] == '+';
        ++i;
    }
    if (constraint[i] == '&') {
        if (!output) return false;
        ++i;
    }
    role = constraint[i];
    if (role == '\0') return false;
    /* Source-owned GNU rJ accepts either a general register or immediate
     * zero.  The generated-ISA policy never consumes this source grammar. */
    if (role == 'r' && constraint[i + 1U] == 'J' &&
        constraint[i + 2U] == '\0') {
        if (output || inout) return false;
        if (operand->kind == MINIC_MACHINE_OPERAND_INTEGER)
            return operand->value.integer == 0;
        role = 'r';
    } else if (constraint[i + 1U] != '\0') {
        return false;
    }
    if (output || inout) {
        if (role != 'r' && role != 'm' && role != 'g' && role != 'A')
            return false;
    }
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER: {
        const unsigned access = operand->value.physical.access;
        if (role != 'r' && role != 'g') return false;
        if (!minic_riscv_machine_validate_register(
                target, &operand->value.physical.reg, NULL) ||
            (output || inout ?
             (access & MINIC_MACHINE_REGISTER_WRITE) == 0U :
             (access & MINIC_MACHINE_REGISTER_READ) == 0U) ||
            (inout && (access & MINIC_MACHINE_REGISTER_READ) == 0U) ||
            ((output || inout) && operand->value.physical.reg.number == 0U) ||
            operand->value.physical.reg.width_bits != xlen)
            return false;
        return true;
    }
    case MINIC_MACHINE_OPERAND_ADDRESS:
        return (role == 'm' || role == 'g' || role == 'A') &&
               (role != 'A' || operand->value.address.displacement == 0) &&
               ((output || inout) ?
                (ins->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U :
                true) &&
               ((!output || inout) ?
                (ins->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) != 0U :
                true) &&
               minic_riscv_machine_validate_address(
                   target, &operand->value.address, NULL);
    case MINIC_MACHINE_OPERAND_INTEGER:
        return !output && !inout &&
               (role == 'i' || role == 'n' || role == 'g');
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return !output && !inout && (role == 'i' || role == 'g') &&
               minic_riscv_machine_validate_relocation(
                   target, operand->value.symbol.name,
                   operand->value.symbol.relocation,
                   operand->value.symbol.addend, NULL);
    default:
        return false;
    }
}

static bool riscv_asm_name(const char *name) {
    const unsigned char *it = (const unsigned char *)name;
    if (it == NULL || !(*it == '_' || isalpha(*it))) return false;
    for (++it; *it != '\0'; ++it)
        if (*it != '_' && !isalnum(*it)) return false;
    return true;
}

static bool riscv_source_asm_operand(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels,
    size_t id, bool label_operand, unsigned char modifier) {
    const MinicMachineOperand *operand;
    if (ins == NULL || id >= ins->operand_count || ins->operands == NULL)
        return false;
    operand = &ins->operands[id];
    if (label_operand) {
        const char *label;
        if (operand->kind != MINIC_MACHINE_OPERAND_BLOCK) return false;
        /* Common verification checks the target ID and CFG successor.
         * The source preflight callback has no GNU label table yet. */
        if (output == NULL) return true;
        return riscv_machine_block_label(function, labels, operand, &label) &&
               fputs(label, output) >= 0;
    }
    if (!riscv_asm_constraint(function->target, ins, operand)) return false;
    if (modifier == 'n' &&
        operand->kind != MINIC_MACHINE_OPERAND_INTEGER) return false;
    if (output == NULL) return true;
    if (modifier == 'z' &&
        operand->kind == MINIC_MACHINE_OPERAND_INTEGER &&
        operand->value.integer == 0) return fputs("zero", output) >= 0;
    if (modifier == 'n') {
        const int64_t value = operand->value.integer;
        if (value > 0) return fprintf(output, "-%" PRId64, value) >= 0;
        return fprintf(output, "%" PRIu64, -(uint64_t)value) >= 0;
    }
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER:
        return fprintf(output, "x%u",
                       operand->value.physical.reg.number) >= 0;
    case MINIC_MACHINE_OPERAND_ADDRESS:
        if (strchr(operand->asm_constraint, 'A') != NULL)
            return fprintf(output, "(x%u)",
                           operand->value.address.base.number) >= 0;
        return fprintf(output, "%" PRId64 "(x%u)",
                       operand->value.address.displacement,
                       operand->value.address.base.number) >= 0;
    case MINIC_MACHINE_OPERAND_INTEGER:
        return fprintf(output, "%" PRId64,
                       operand->value.integer) >= 0;
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return fputs(operand->value.symbol.name, output) >= 0;
    default:
        return false;
    }
}

/* One parser for verifier and serializer: any source placeholder accepted by
 * preflight will materialize from exactly the same typed machine operand.
 * Unlike compiler-selected instructions, only genuine SOURCE_ASM may reach
 * this path; we never interpret the source text as selected instruction IR. */
static bool riscv_source_asm_expand(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels) {
    size_t i;
    if (function == NULL || ins == NULL ||
        ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_template == NULL || ins->source_asm_length == 0U ||
        ins->source_asm_length > 1048576U ||
        (ins->operand_count != 0U && ins->operands == NULL)) return false;
    if (function->target != NULL &&
        function->target->architecture == MINIC_TARGET_ARCH_RISCV64) {
        static const char *const reserved[] = {
            "# MINIC_RV64I_ASM_BEGIN", "# MINIC_RV64I_ASM_END"
        };
        size_t marker;
        /* The ISA auditor treats these comments as source-origin trust
         * delimiters. Neither literal GNU source asm nor a placeholder may
         * forge them; enforce the same sealed constraint as the old emitter. */
        for (marker = 0U; marker < sizeof(reserved) / sizeof(reserved[0]);
             ++marker) {
            const size_t len = strlen(reserved[marker]);
            if (len > ins->source_asm_length) continue;
            for (i = 0U; i <= ins->source_asm_length - len; ++i)
                if (memcmp(ins->source_asm_template + i,
                        reserved[marker], len) == 0) return false;
        }
    }
    for (i = 0U; i < ins->operand_count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        size_t previous;
        if (op->asm_name != NULL && !riscv_asm_name(op->asm_name))
            return false;
        if (op->asm_name != NULL)
            for (previous = 0U; previous < i; ++previous)
                if (ins->operands[previous].asm_name != NULL &&
                    strcmp(op->asm_name,
                           ins->operands[previous].asm_name) == 0)
                    return false;
        if (op->kind == MINIC_MACHINE_OPERAND_BLOCK) {
            if (op->asm_constraint != NULL ||
                (ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U ||
                !ins->source_asm_terminates_block) return false;
        } else if (!riscv_asm_constraint(function->target, ins, op)) {
            return false;
        }
    }
    for (i = 0U; i < ins->source_asm_length; ++i) {
        const unsigned char ch =
            (unsigned char)ins->source_asm_template[i];
        if (ch == '\0' || ch == '\r') return false;
        if (ch != '%') {
            if (output != NULL && fputc(ch, output) == EOF) return false;
            continue;
        }
        if (++i >= ins->source_asm_length) return false;
        if (ins->source_asm_template[i] == '%') {
            if (output != NULL && fputc('%', output) == EOF) return false;
            continue;
        }
        {
            bool label_operand = false;
            size_t id = SIZE_MAX;
            const unsigned char modifier =
                (unsigned char)ins->source_asm_template[i];
            if (modifier == 'l') {
                label_operand = true;
                if (++i >= ins->source_asm_length) return false;
            } else if (modifier == 'c' || modifier == 'n' ||
                       modifier == 'z') {
                if (++i >= ins->source_asm_length) return false;
            }
            if (ins->source_asm_template[i] == '[') {
                size_t name_start = i + 1U, name_end;
                size_t j;
                for (name_end = name_start;
                     name_end < ins->source_asm_length &&
                     ins->source_asm_template[name_end] != ']';
                     ++name_end) {}
                if (name_end == ins->source_asm_length ||
                    name_end == name_start) return false;
                for (j = 0U; j < ins->operand_count; ++j) {
                    const char *name = ins->operands[j].asm_name;
                    if (name != NULL &&
                        strlen(name) == name_end - name_start &&
                        memcmp(name, ins->source_asm_template + name_start,
                               name_end - name_start) == 0) {
                        if (id != SIZE_MAX) return false;
                        id = j;
                    }
                }
                i = name_end;
            } else if (ins->source_asm_template[i] >= '0' &&
                       ins->source_asm_template[i] <= '9') {
                id = 0U;
                do {
                    unsigned digit =
                        (unsigned)(ins->source_asm_template[i] - '0');
                    if (id > (SIZE_MAX - digit) / 10U) return false;
                    id = id * 10U + digit;
                    if (i + 1U == ins->source_asm_length ||
                        ins->source_asm_template[i + 1U] < '0' ||
                        ins->source_asm_template[i + 1U] > '9') break;
                    ++i;
                } while (true);
            } else {
                return false;
            }
            if (id == SIZE_MAX || id >= ins->operand_count ||
                (modifier == 'c' &&
                 ins->operands[id].kind != MINIC_MACHINE_OPERAND_INTEGER) ||
                !riscv_source_asm_operand(output, function, ins,
                    labels, id, label_operand, modifier)) return false;
        }
    }
    return true;
}

static bool riscv_machine_validate_source_asm(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins,
    void *context) {
    MinicMachineFunction probe = {0};
    (void)context;
    if (target == NULL || ins == NULL) return false;
    probe.target = target;
    /* Validation of binding kinds, placeholders and target registers needs no
     * labels. Actual BLOCK indices are checked by the common verifier. */
    return riscv_source_asm_expand(NULL, &probe, ins, NULL);
}

static bool riscv_machine_emit_source_asm(FILE *output,
    const MinicMachineFunction *function, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    const bool rv64 = function != NULL && function->target != NULL &&
        function->target->architecture == MINIC_TARGET_ARCH_RISCV64;
    (void)context;
    if (output == NULL || function == NULL || labels == NULL ||
        !riscv_source_asm_expand(NULL, function, ins, labels) ||
        fputs(rv64 ? "# MINIC_RV64I_ASM_BEGIN\n  " :
            "# MINIC_SOURCE_INLINE_ASM_BEGIN\n  ", output) < 0 ||
        !riscv_source_asm_expand(output, function, ins, labels)) return false;
    return fputs(rv64 ? "\n# MINIC_RV64I_ASM_END\n" :
        "\n# MINIC_SOURCE_INLINE_ASM_END\n", output) >= 0;
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
    policy->verify.validate_source_asm = riscv_machine_validate_source_asm;
    policy->emit_selected = riscv_machine_emit_selected;
    policy->emit_source_asm = riscv_machine_emit_source_asm;
    return true;
}
