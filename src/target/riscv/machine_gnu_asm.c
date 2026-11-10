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

/* The spelling is an output-only presentation of a verified physical bank:
 * GNU aliases preserve the established RV64D ABI and arithmetic probes. */
static const char *riscv_machine_abi_register_name(
    const MinicMachineRegister *reg) {
    static const char *const gpr_names[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
    };
    static const char *const fpr_names[32] = {
        "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
        "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
        "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
        "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
    };
    if (reg == NULL || reg->number >= 32U) return NULL;
    if (reg->bank == MINIC_MACHINE_REGISTER_GENERAL)
        return gpr_names[reg->number];
    if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT)
        return fpr_names[reg->number];
    return NULL;
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

/* A selected physical instruction is already fully verified before this
 * callback.  Keep its presentation in a bounded, local byte builder: unlike
 * fprintf, appending a mnemonic, physical register or integer does not parse
 * an instruction-specific printf format on every selected instruction.  The
 * builder flushes on long symbols, so no assembler identifier is truncated. */
typedef struct RiscvSelectedText {
    FILE *output;
    char bytes[256];
    size_t count;
} RiscvSelectedText;

static bool riscv_text_flush(RiscvSelectedText *text) {
    if (text->count != 0U &&
        fwrite(text->bytes, 1U, text->count, text->output) != text->count)
        return false;
    text->count = 0U;
    return true;
}

static bool riscv_text_bytes(RiscvSelectedText *text,
                             const char *bytes, size_t length) {
    /* Almost every selected opcode fits in the remaining local buffer.
     * Keep that path to a bounds comparison plus one memcpy; only uncommon
     * long symbols need the streaming loop. */
    if (length <= sizeof(text->bytes) - text->count) {
        memcpy(text->bytes + text->count, bytes, length);
        text->count += length;
        return true;
    }
    while (length != 0U) {
        size_t available = sizeof(text->bytes) - text->count;
        size_t size;
        if (available == 0U) {
            if (!riscv_text_flush(text)) return false;
            available = sizeof(text->bytes);
        }
        size = length < available ? length : available;
        memcpy(text->bytes + text->count, bytes, size);
        text->count += size;
        bytes += size;
        length -= size;
    }
    return true;
}

static bool riscv_text_string(RiscvSelectedText *text, const char *value) {
    return value != NULL && riscv_text_bytes(text, value, strlen(value));
}

static bool riscv_text_unsigned(RiscvSelectedText *text, uint64_t value) {
    char digits[20];
    char *begin = digits + sizeof(digits);
    do {
        *--begin = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    return riscv_text_bytes(text, begin,
        (size_t)(digits + sizeof(digits) - begin));
}

static bool riscv_text_signed(RiscvSelectedText *text, int64_t value) {
    if (value >= 0) return riscv_text_unsigned(text, (uint64_t)value);
    return riscv_text_string(text, "-") &&
        riscv_text_unsigned(text, UINT64_C(0) - (uint64_t)value);
}

static bool riscv_text_register(RiscvSelectedText *text, unsigned reg) {
    char name[3];
    if (reg >= 32U) return false;
    name[0] = 'x';
    if (reg < 10U) {
        name[1] = (char)('0' + reg);
        return riscv_text_bytes(text, name, 2U);
    }
    name[1] = (char)('0' + reg / 10U);
    name[2] = (char)('0' + reg % 10U);
    return riscv_text_bytes(text, name, 3U);
}

static bool riscv_text_mnemonic(RiscvSelectedText *text, const char *mnemonic) {
    return riscv_text_string(text, "  ") &&
        riscv_text_string(text, mnemonic);
}

static bool riscv_text_end(RiscvSelectedText *text) {
    return riscv_text_string(text, "\n") && riscv_text_flush(text);
}

/* Operand values are verified before output; this emitter uses only the
 * target's fixed mnemonic table and numeric physical register identities.
 * It cannot consume a pre-rendered assembly statement. */
static bool riscv_machine_emit_selected(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    MinicRiscvMachineOpcodeInfo desc;
    RiscvSelectedText text = { .output = output, .count = 0U };
    const MinicMachineOperand *operand;
    const char *branch_label = NULL;
    const char *symbol = NULL;
    unsigned r0 = 0U, r1 = 0U, r2 = 0U;
    (void)context;
    /* The sealed prepared body has already verified this instruction through
     * policy->verify.validate_selected_instruction. Revalidating while printing
     * duplicates expensive branch-location scans and cannot strengthen a
     * MachineFunction that must remain unchanged through emission. */
    if (output == NULL || fn == NULL || ins == NULL ||
        !minic_riscv_machine_opcode_info(fn->target, ins->opcode, &desc))
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
    if (ins->opcode >= MINIC_RISCV_MACHINE_FMV_X_W &&
        ins->opcode < MINIC_RISCV_MACHINE_LAST) {
        const char *rnames[3] = {NULL, NULL, NULL};
        size_t i;
        if (desc.mnemonic == NULL ||
            (ins->operand_count != 2U && ins->operand_count != 3U))
            return false;
        for (i = 0U; i < ins->operand_count; ++i) {
            rnames[i] = riscv_machine_abi_register_name(
                &operand[i].value.physical.reg);
            if (rnames[i] == NULL) return false;
        }
        if (!riscv_text_mnemonic(&text, desc.mnemonic) ||
            !riscv_text_string(&text, " ") ||
            !riscv_text_string(&text, rnames[0]) ||
            !riscv_text_string(&text, ", ") ||
            !riscv_text_string(&text, rnames[1])) return false;
        if (ins->operand_count == 3U)
            return riscv_text_string(&text, ", ") &&
                riscv_text_string(&text, rnames[2]) && riscv_text_end(&text);
        if (ins->opcode >= MINIC_RISCV_MACHINE_FCVT_W_D_RTZ &&
            ins->opcode <= MINIC_RISCV_MACHINE_FCVT_LU_D_RTZ)
            return riscv_text_string(&text, ", rtz") && riscv_text_end(&text);
        return riscv_text_end(&text);
    }
    switch (desc.form) {
    case MINIC_RISCV_MACHINE_FORM_NONE:
        if (ins->opcode == MINIC_RISCV_MACHINE_BARRIER ||
            ins->opcode == MINIC_RISCV_MACHINE_UNREACHABLE) return true;
        return desc.mnemonic != NULL &&
            riscv_text_mnemonic(&text, desc.mnemonic) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER:
        if (ins->opcode == MINIC_RISCV_MACHINE_CALL_INDIRECT)
            return riscv_text_string(&text, "  jalr ra, 0(") &&
                riscv_text_register(&text, r0) &&
                riscv_text_string(&text, ")") && riscv_text_end(&text);
        return false;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER:
        return desc.mnemonic != NULL &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_register(&text, r1) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER_IMMEDIATE:
        if (ins->opcode == MINIC_RISCV_MACHINE_JALR_BRANCH ||
            ins->opcode == MINIC_RISCV_MACHINE_JALR_CALL) {
            return riscv_text_string(&text, "  jalr ") &&
                riscv_text_string(&text,
                    ins->opcode == MINIC_RISCV_MACHINE_JALR_CALL
                        ? "ra" : "zero") &&
                riscv_text_string(&text, ", ") &&
                riscv_text_signed(&text, operand[1].value.integer) &&
                riscv_text_string(&text, "(") &&
                riscv_text_register(&text, r0) &&
                riscv_text_string(&text, ")") && riscv_text_end(&text);
        }
        return desc.mnemonic != NULL &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_signed(&text, operand[1].value.integer) &&
            riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_IMMEDIATE:
        return desc.mnemonic != NULL &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_register(&text, r1) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_signed(&text, operand[2].value.integer) &&
            riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_REGISTER:
        return desc.mnemonic != NULL &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_register(&text, r1) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_register(&text, r2) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_LOAD:
    case MINIC_RISCV_MACHINE_FORM_STORE: {
        const MinicMachineAddress *address = &operand[1].value.address;
        if (operand[1].kind != MINIC_MACHINE_OPERAND_ADDRESS ||
            !minic_riscv_machine_validate_address(fn->target, address, NULL))
            return false;
        return riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_signed(&text, address->displacement) &&
            riscv_text_string(&text, "(") &&
            riscv_text_register(&text, address->base.number) &&
            riscv_text_string(&text, ")") && riscv_text_end(&text);
    }
    case MINIC_RISCV_MACHINE_FORM_LOAD_FRAME:
    case MINIC_RISCV_MACHINE_FORM_STORE_FRAME: {
        unsigned frame_base;
        int64_t frame_displacement;
        if (!minic_riscv_machine_resolve_frame_access(
                fn, ins->opcode, &operand[1],
                &frame_base, &frame_displacement)) return false;
        return riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_signed(&text, frame_displacement) &&
            riscv_text_string(&text, "(") &&
            riscv_text_register(&text, frame_base) &&
            riscv_text_string(&text, ")") && riscv_text_end(&text);
    }
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK:
        return riscv_machine_block_label(fn, labels, &operand[2], &branch_label) &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_register(&text, r1) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_string(&text, branch_label) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK:
        if (ins->opcode == MINIC_RISCV_MACHINE_JALR_BLOCK)
            return riscv_machine_block_label(fn, labels, &operand[1],
                                              &branch_label) &&
                riscv_text_string(&text, "  jalr zero, 0(") &&
                riscv_text_register(&text, r0) &&
                riscv_text_string(&text, ")") && riscv_text_end(&text);
        return riscv_machine_block_label(fn, labels, &operand[1], &branch_label) &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_string(&text, branch_label) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_BLOCK:
        if (!riscv_machine_block_label(fn, labels, &operand[0], &branch_label))
            return false;
        if (ins->opcode == MINIC_RISCV_MACHINE_JAL_BLOCK)
            return riscv_text_string(&text, "  jal zero, ") &&
                riscv_text_string(&text, branch_label) && riscv_text_end(&text);
        return riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_string(&text, branch_label) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_REGISTER_SYMBOL:
        if (!riscv_machine_symbol(fn, &operand[1], &symbol)) return false;
        if (ins->opcode == MINIC_RISCV_MACHINE_LA_GOT)
            return riscv_text_string(&text,
                       "  .option push\n  .option pic\n  la ") &&
                riscv_text_register(&text, r0) &&
                riscv_text_string(&text, ", ") &&
                riscv_text_string(&text, symbol) &&
                riscv_text_string(&text, "\n  .option pop") &&
                riscv_text_end(&text);
        return riscv_text_string(&text, "  lla ") &&
            riscv_text_register(&text, r0) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_string(&text, symbol) && riscv_text_end(&text);
    case MINIC_RISCV_MACHINE_FORM_SYMBOL:
        return riscv_machine_symbol(fn, &operand[0], &symbol) &&
            riscv_text_mnemonic(&text, desc.mnemonic) &&
            riscv_text_string(&text, " ") &&
            riscv_text_string(&text, symbol) && riscv_text_end(&text);
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
        return riscv_text_string(&text, "  fence ") &&
            riscv_text_string(&text, predecessor) &&
            riscv_text_string(&text, ", ") &&
            riscv_text_string(&text, successor) && riscv_text_end(&text);
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
        /* GNU +&r is an early-clobber tied output: it must retain the
         * initial register value as well as forbid overlap with inputs. */
        if (!output && !inout) return false;
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
        if (operand->value.physical.reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            !minic_riscv_machine_validate_register(
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
        if (operand->value.symbol.relocation !=
            MINIC_RISCV_MACHINE_RELOC_SOURCE_IMMEDIATE) return false;
        if (operand->value.symbol.addend == 0)
            return fputs(operand->value.symbol.name, output) >= 0;
        return fprintf(output,
            operand->value.symbol.addend > 0 ? "%s+%" PRId64 : "%s%" PRId64,
            operand->value.symbol.name,
            operand->value.symbol.addend) >= 0;
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
    policy->section_type_marker = MINIC_MACHINE_GNU_SECTION_TYPE_AT;
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
