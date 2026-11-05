#include "target/riscv/machine_opcode.h"

#include "core/symbol_identity.h"

#include <limits.h>
#include <string.h>

/* The generated-instruction policy is intentionally independent of the
 * source-ASM -march policy. Base I is always available; no M/A/F/D/C opcode
 * is inferred from a spelling or from the target's accepted extensions. */
typedef struct RiscvMachineDescription {
    const char *mnemonic;
    MinicRiscvMachineForm form;
    bool rv64_only;
} RiscvMachineDescription;

#define RV_OP(name, spelling, shape) \
    [MINIC_RISCV_MACHINE_##name] = {spelling, MINIC_RISCV_MACHINE_FORM_##shape, false}
#define RV64_OP(name, spelling, shape) \
    [MINIC_RISCV_MACHINE_##name] = {spelling, MINIC_RISCV_MACHINE_FORM_##shape, true}

static const RiscvMachineDescription riscv_machine_descriptions[
    MINIC_RISCV_MACHINE_LAST] = {
    RV_OP(LUI, "lui", REGISTER_IMMEDIATE),
    RV_OP(AUIPC, "auipc", REGISTER_IMMEDIATE),
    RV_OP(JAL_BLOCK, "jal", BLOCK),
    RV_OP(JALR_BRANCH, "jalr", REGISTER_IMMEDIATE),
    RV_OP(JALR_CALL, "jalr", REGISTER_IMMEDIATE),
    RV_OP(BEQ, "beq", REGISTER_REGISTER_BLOCK),
    RV_OP(BNE, "bne", REGISTER_REGISTER_BLOCK),
    RV_OP(BLT, "blt", REGISTER_REGISTER_BLOCK),
    RV_OP(BGE, "bge", REGISTER_REGISTER_BLOCK),
    RV_OP(BLTU, "bltu", REGISTER_REGISTER_BLOCK),
    RV_OP(BGEU, "bgeu", REGISTER_REGISTER_BLOCK),
    RV_OP(LB, "lb", LOAD),
    RV_OP(LH, "lh", LOAD),
    RV_OP(LW, "lw", LOAD),
    RV_OP(LBU, "lbu", LOAD),
    RV_OP(LHU, "lhu", LOAD),
    RV_OP(LB_FRAME, "lb", LOAD_FRAME),
    RV_OP(LH_FRAME, "lh", LOAD_FRAME),
    RV_OP(LW_FRAME, "lw", LOAD_FRAME),
    RV_OP(LBU_FRAME, "lbu", LOAD_FRAME),
    RV_OP(LHU_FRAME, "lhu", LOAD_FRAME),
    RV_OP(SB, "sb", STORE),
    RV_OP(SH, "sh", STORE),
    RV_OP(SW, "sw", STORE),
    RV_OP(SB_FRAME, "sb", STORE_FRAME),
    RV_OP(SH_FRAME, "sh", STORE_FRAME),
    RV_OP(SW_FRAME, "sw", STORE_FRAME),
    RV_OP(ADDI, "addi", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(SLTI, "slti", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(SLTIU, "sltiu", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(XORI, "xori", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(ORI, "ori", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(ANDI, "andi", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(SLLI, "slli", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(SRLI, "srli", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(SRAI, "srai", REGISTER_REGISTER_IMMEDIATE),
    RV_OP(ADD, "add", REGISTER_REGISTER_REGISTER),
    RV_OP(SUB, "sub", REGISTER_REGISTER_REGISTER),
    RV_OP(SLL, "sll", REGISTER_REGISTER_REGISTER),
    RV_OP(SLT, "slt", REGISTER_REGISTER_REGISTER),
    RV_OP(SLTU, "sltu", REGISTER_REGISTER_REGISTER),
    RV_OP(XOR, "xor", REGISTER_REGISTER_REGISTER),
    RV_OP(SRL, "srl", REGISTER_REGISTER_REGISTER),
    RV_OP(SRA, "sra", REGISTER_REGISTER_REGISTER),
    RV_OP(OR, "or", REGISTER_REGISTER_REGISTER),
    RV_OP(AND, "and", REGISTER_REGISTER_REGISTER),
    RV_OP(FENCE, "fence", NONE),
    RV_OP(FENCE_MASK, "fence", FENCE_MASK),
    RV_OP(ECALL, "ecall", NONE),
    RV_OP(EBREAK, "ebreak", NONE),
    RV64_OP(LWU, "lwu", LOAD),
    RV64_OP(LD, "ld", LOAD),
    RV64_OP(SD, "sd", STORE),
    RV64_OP(LWU_FRAME, "lwu", LOAD_FRAME),
    RV64_OP(LD_FRAME, "ld", LOAD_FRAME),
    RV64_OP(SD_FRAME, "sd", STORE_FRAME),
    RV64_OP(ADDIW, "addiw", REGISTER_REGISTER_IMMEDIATE),
    RV64_OP(SLLIW, "slliw", REGISTER_REGISTER_IMMEDIATE),
    RV64_OP(SRLIW, "srliw", REGISTER_REGISTER_IMMEDIATE),
    RV64_OP(SRAIW, "sraiw", REGISTER_REGISTER_IMMEDIATE),
    RV64_OP(ADDW, "addw", REGISTER_REGISTER_REGISTER),
    RV64_OP(SUBW, "subw", REGISTER_REGISTER_REGISTER),
    RV64_OP(SLLW, "sllw", REGISTER_REGISTER_REGISTER),
    RV64_OP(SRLW, "srlw", REGISTER_REGISTER_REGISTER),
    RV64_OP(SRAW, "sraw", REGISTER_REGISTER_REGISTER),
    RV_OP(LI, "li", REGISTER_IMMEDIATE),
    RV_OP(MV, "mv", REGISTER_REGISTER),
    RV_OP(NOT, "not", REGISTER_REGISTER),
    RV_OP(NEG, "neg", REGISTER_REGISTER),
    RV_OP(SEQZ, "seqz", REGISTER_REGISTER),
    RV_OP(SNEZ, "snez", REGISTER_REGISTER),
    RV64_OP(SEXT_W, "sext.w", REGISTER_REGISTER),
    RV64_OP(ZEXT_W, "zext.w", REGISTER_REGISTER),
    RV_OP(BEQZ, "beqz", REGISTER_BLOCK),
    RV_OP(BNEZ, "bnez", REGISTER_BLOCK),
    RV_OP(J_BLOCK, "j", BLOCK),
    RV_OP(RET, "ret", NONE),
    RV_OP(LA_BLOCK, "lla", REGISTER_BLOCK),
    RV_OP(LA_PCREL, "lla", REGISTER_SYMBOL),
    RV_OP(LA_GOT, "la", REGISTER_SYMBOL),
    RV_OP(CALL_DIRECT, "call", SYMBOL),
    RV_OP(TAIL_DIRECT, "tail", SYMBOL),
    RV_OP(CALL_INDIRECT, "jalr", REGISTER),
    RV_OP(BARRIER, NULL, NONE),
    RV_OP(UNREACHABLE, NULL, NONE)
};

#undef RV_OP
#undef RV64_OP

static bool riscv_machine_target(const MinicTargetInfo *target, unsigned *xlen) {
    if (target == NULL || xlen == NULL) return false;
    if (target->architecture == MINIC_TARGET_ARCH_RISCV32 &&
        target->riscv_isa.xlen == 32U &&
        target->riscv_isa.abi == MINIC_RISCV_ABI_ILP32) {
        *xlen = 32U;
        return true;
    }
    if (target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
        target->riscv_isa.xlen == 64U &&
        (target->riscv_isa.abi == MINIC_RISCV_ABI_LP64 ||
         target->riscv_isa.abi == MINIC_RISCV_ABI_LP64D)) {
        *xlen = 64U;
        return true;
    }
    return false;
}

bool minic_riscv_machine_opcode_info(const MinicTargetInfo *target,
                                      uint32_t opcode,
                                      MinicRiscvMachineOpcodeInfo *info) {
    const RiscvMachineDescription *desc;
    unsigned xlen;
    if (info == NULL || !riscv_machine_target(target, &xlen) ||
        opcode == MINIC_RISCV_MACHINE_INVALID ||
        opcode >= MINIC_RISCV_MACHINE_LAST) return false;
    desc = &riscv_machine_descriptions[opcode];
    if (desc->form == MINIC_RISCV_MACHINE_FORM_INVALID ||
        (desc->rv64_only && xlen != 64U)) return false;
    info->mnemonic = desc->mnemonic;
    info->form = desc->form;
    info->rv64_only = desc->rv64_only;
    return true;
}

bool minic_riscv_machine_validate_generated_features(
    const MinicTargetInfo *target, uint64_t generated_features, void *context) {
    unsigned xlen;
    (void)context;
    return riscv_machine_target(target, &xlen) &&
           generated_features == (uint64_t)target->riscv_isa.codegen_features;
}

bool minic_riscv_machine_validate_function_abi(
    const MinicTargetInfo *target,
    MinicFunctionCallingConvention calling_convention, void *context) {
    unsigned xlen;
    (void)context;
    return riscv_machine_target(target, &xlen) &&
           calling_convention == MINIC_CALLING_CONVENTION_DEFAULT;
}

bool minic_riscv_machine_validate_frame_object(
    const MinicTargetInfo *target, const MinicMachineFunction *fn,
    const MinicMachineFrameObject *object, void *context) {
    unsigned xlen;
    (void)context;
    if (!riscv_machine_target(target, &xlen) || fn == NULL ||
        object == NULL || !object->has_fixed_offset ||
        object->area > MINIC_MACHINE_FRAME_OUTGOING ||
        object->base > MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER ||
        fn->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT ||
        fn->frame_alignment < 16U ||
        (xlen == 32U &&
         (fn->frame_size > INT32_MAX ||
          object->fixed_offset > INT32_MAX ||
          object->fixed_offset < INT32_MIN))) return false;
    if ((object->area == MINIC_MACHINE_FRAME_OUTGOING &&
         object->base != MINIC_MACHINE_FRAME_BASE_STACK_POINTER) ||
        (object->area == MINIC_MACHINE_FRAME_INCOMING &&
         object->base != MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER) ||
        (object->area == MINIC_MACHINE_FRAME_LOCAL &&
         object->base != (fn->has_dynamic_stack
            ? MINIC_MACHINE_FRAME_BASE_FRAME_POINTER
            : MINIC_MACHINE_FRAME_BASE_STACK_POINTER))) return false;
    /* Outgoing call arguments reside in the current function-owned frame.
     * Incoming argument references use an ABI entry-SP anchor, which can
     * differ from SP after alloca/over-alignment; do not conflate the two. */
    if (object->area == MINIC_MACHINE_FRAME_OUTGOING) {
        if (object->fixed_offset < 0 ||
            (uint64_t)object->fixed_offset > (uint64_t)fn->frame_size ||
            object->size_bytes > fn->frame_size -
                 (size_t)object->fixed_offset) return false;
    } else if (object->area == MINIC_MACHINE_FRAME_INCOMING &&
               object->fixed_offset < 0) {
        return false;
    }
    return true;
}

bool minic_riscv_machine_validate_register(
    const MinicTargetInfo *target, const MinicMachineRegister *reg,
    void *context) {
    unsigned xlen;
    (void)context;
    return reg != NULL && riscv_machine_target(target, &xlen) &&
           reg->bank == MINIC_MACHINE_REGISTER_GENERAL &&
           reg->number < 32U && reg->width_bits == xlen;
}

bool minic_riscv_machine_validate_address(
    const MinicTargetInfo *target, const MinicMachineAddress *address,
    void *context) {
    return address != NULL &&
           address->target_mode == MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT &&
           address->has_base && !address->has_index &&
           !address->has_segment && !address->has_symbol &&
           address->scale == 1U &&
           address->displacement >= -2048 &&
           address->displacement <= 2047 &&
           minic_riscv_machine_validate_register(target, &address->base, context);
}

bool minic_riscv_machine_validate_relocation(
    const MinicTargetInfo *target, const char *symbol,
    uint32_t relocation, int64_t addend, void *context) {
    unsigned xlen;
    (void)context;
    return riscv_machine_target(target, &xlen) &&
           minic_core_assembler_symbol_lexically_safe(symbol) &&
           addend == 0 &&
           (relocation == MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL ||
            relocation == MINIC_RISCV_MACHINE_RELOC_GOT ||
            relocation == MINIC_RISCV_MACHINE_RELOC_CALL);
}

static void register_effect(MinicMachineRegisterEffect *effect, unsigned id,
                            unsigned xlen, unsigned access) {
    effect->reg.number = id;
    effect->reg.width_bits = (uint16_t)xlen;
    effect->reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    effect->access = access;
}

/* Physical effects are specified by the target, never self-attested by an
 * incoming MachineInstruction. A standard call clobbers every psABI
 * caller-saved integer register; it reads all potential argument registers
 * conservatively without changing existing argument assignment. */
static void call_effects(MinicMachineOpcodeContract *contract, unsigned xlen,
                         bool tail) {
    static const unsigned volatile_ids[] = {
        5U, 6U, 7U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U,
        28U, 29U, 30U, 31U
    };
    size_t i;
    register_effect(&contract->implicit[contract->implicit_count++], 1U,
                    xlen, tail ? MINIC_MACHINE_REGISTER_READ :
                    (MINIC_MACHINE_REGISTER_WRITE | MINIC_MACHINE_REGISTER_CLOBBER));
    for (i = 0U; i < sizeof(volatile_ids) / sizeof(volatile_ids[0]); ++i) {
        const unsigned id = volatile_ids[i];
        const unsigned access = MINIC_MACHINE_REGISTER_WRITE |
                                MINIC_MACHINE_REGISTER_CLOBBER |
                                (id >= 10U && id <= 17U ?
                                 MINIC_MACHINE_REGISTER_READ : 0U);
        register_effect(&contract->implicit[contract->implicit_count++],
                        id, xlen, access);
    }
}

static void operand(MinicMachineOpcodeContract *contract, size_t index,
                    MinicMachineOperandKind kind, unsigned access) {
    contract->operand_kinds[index] = kind;
    contract->register_access[index] = access;
}

bool minic_riscv_machine_lookup_opcode_contract(
    const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    MinicRiscvMachineOpcodeInfo info;
    unsigned xlen;
    const unsigned read = MINIC_MACHINE_REGISTER_READ;
    const unsigned write = MINIC_MACHINE_REGISTER_WRITE;
    const unsigned memread = MINIC_MACHINE_EFFECT_MEMORY_READ;
    const unsigned memwrite = MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    const unsigned barrier = MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    const unsigned branch = MINIC_MACHINE_EFFECT_BRANCH;
    const unsigned call = MINIC_MACHINE_EFFECT_CALL;
    (void)context;
    if (contract == NULL || !riscv_machine_target(target, &xlen) ||
        !minic_riscv_machine_opcode_info(target, opcode, &info)) return false;
    memset(contract, 0, sizeof(*contract));
    switch (info.form) {
    case MINIC_RISCV_MACHINE_FORM_NONE:
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER:
        contract->operand_count = 1U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER,
                opcode == MINIC_RISCV_MACHINE_CALL_INDIRECT ? read : write);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER, write);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_REGISTER, read);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_IMMEDIATE:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER,
                opcode == MINIC_RISCV_MACHINE_JALR_BRANCH ||
                opcode == MINIC_RISCV_MACHINE_JALR_CALL ? read : write);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_INTEGER, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_IMMEDIATE:
        contract->operand_count = 3U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER, write);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_REGISTER, read);
        operand(contract, 2U, MINIC_MACHINE_OPERAND_INTEGER, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_REGISTER:
        contract->operand_count = 3U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER, write);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_REGISTER, read);
        operand(contract, 2U, MINIC_MACHINE_OPERAND_REGISTER, read);
        break;
    case MINIC_RISCV_MACHINE_FORM_LOAD:
    case MINIC_RISCV_MACHINE_FORM_STORE:
    case MINIC_RISCV_MACHINE_FORM_LOAD_FRAME:
    case MINIC_RISCV_MACHINE_FORM_STORE_FRAME:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER,
                info.form == MINIC_RISCV_MACHINE_FORM_LOAD ||
                info.form == MINIC_RISCV_MACHINE_FORM_LOAD_FRAME
                    ? write : read);
        operand(contract, 1U,
                info.form == MINIC_RISCV_MACHINE_FORM_LOAD_FRAME ||
                info.form == MINIC_RISCV_MACHINE_FORM_STORE_FRAME
                    ? MINIC_MACHINE_OPERAND_FRAME
                    : MINIC_MACHINE_OPERAND_ADDRESS, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK:
        contract->operand_count = 3U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER, read);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_REGISTER, read);
        operand(contract, 2U, MINIC_MACHINE_OPERAND_BLOCK, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER,
                opcode == MINIC_RISCV_MACHINE_LA_BLOCK ? write : read);
        operand(contract, 1U, opcode == MINIC_RISCV_MACHINE_LA_BLOCK
                            ? MINIC_MACHINE_OPERAND_BLOCK_ADDRESS
                            : MINIC_MACHINE_OPERAND_BLOCK, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_BLOCK:
        contract->operand_count = 1U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_BLOCK, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_SYMBOL:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_REGISTER, write);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_SYMBOL, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_SYMBOL:
        contract->operand_count = 1U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_SYMBOL, 0U);
        break;
    case MINIC_RISCV_MACHINE_FORM_FENCE_MASK:
        contract->operand_count = 2U;
        operand(contract, 0U, MINIC_MACHINE_OPERAND_INTEGER, 0U);
        operand(contract, 1U, MINIC_MACHINE_OPERAND_INTEGER, 0U);
        break;
    default:
        return false;
    }
    switch (opcode) {
    case MINIC_RISCV_MACHINE_LB:
    case MINIC_RISCV_MACHINE_LH:
    case MINIC_RISCV_MACHINE_LW:
    case MINIC_RISCV_MACHINE_LBU:
    case MINIC_RISCV_MACHINE_LHU:
    case MINIC_RISCV_MACHINE_LWU:
    case MINIC_RISCV_MACHINE_LD:
    case MINIC_RISCV_MACHINE_LB_FRAME:
    case MINIC_RISCV_MACHINE_LH_FRAME:
    case MINIC_RISCV_MACHINE_LW_FRAME:
    case MINIC_RISCV_MACHINE_LBU_FRAME:
    case MINIC_RISCV_MACHINE_LHU_FRAME:
    case MINIC_RISCV_MACHINE_LWU_FRAME:
    case MINIC_RISCV_MACHINE_LD_FRAME:
        contract->required_effects = memread;
        contract->permitted_effects = memread | MINIC_MACHINE_EFFECT_VOLATILE;
        break;
    case MINIC_RISCV_MACHINE_SB:
    case MINIC_RISCV_MACHINE_SH:
    case MINIC_RISCV_MACHINE_SW:
    case MINIC_RISCV_MACHINE_SD:
    case MINIC_RISCV_MACHINE_SB_FRAME:
    case MINIC_RISCV_MACHINE_SH_FRAME:
    case MINIC_RISCV_MACHINE_SW_FRAME:
    case MINIC_RISCV_MACHINE_SD_FRAME:
        contract->required_effects = memwrite;
        contract->permitted_effects = memwrite | MINIC_MACHINE_EFFECT_VOLATILE;
        break;
    case MINIC_RISCV_MACHINE_BEQ:
    case MINIC_RISCV_MACHINE_BNE:
    case MINIC_RISCV_MACHINE_BLT:
    case MINIC_RISCV_MACHINE_BGE:
    case MINIC_RISCV_MACHINE_BLTU:
    case MINIC_RISCV_MACHINE_BGEU:
    case MINIC_RISCV_MACHINE_BEQZ:
    case MINIC_RISCV_MACHINE_BNEZ:
        contract->required_effects = branch;
        contract->permitted_effects = branch;
        contract->terminates_block = true;
        contract->permits_fallthrough = true;
        break;
    case MINIC_RISCV_MACHINE_JAL_BLOCK:
    case MINIC_RISCV_MACHINE_J_BLOCK:
    case MINIC_RISCV_MACHINE_JALR_BRANCH:
    case MINIC_RISCV_MACHINE_EBREAK:
    case MINIC_RISCV_MACHINE_UNREACHABLE:
        contract->required_effects = branch;
        contract->permitted_effects = branch;
        contract->terminates_block = true;
        contract->has_indirect_successors =
            opcode == MINIC_RISCV_MACHINE_JALR_BRANCH;
        break;
    case MINIC_RISCV_MACHINE_CALL_DIRECT:
    case MINIC_RISCV_MACHINE_CALL_INDIRECT:
    case MINIC_RISCV_MACHINE_JALR_CALL:
        contract->required_effects = call | memread | memwrite;
        contract->permitted_effects = call | memread | memwrite | barrier;
        call_effects(contract, xlen, false);
        break;
    case MINIC_RISCV_MACHINE_TAIL_DIRECT:
        contract->required_effects = call | branch | memread | memwrite;
        contract->permitted_effects = call | branch | memread | memwrite | barrier;
        contract->terminates_block = true;
        call_effects(contract, xlen, true);
        break;
    case MINIC_RISCV_MACHINE_RET:
        contract->required_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->permitted_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->terminates_block = true;
        register_effect(&contract->implicit[0], 1U, xlen,
                        MINIC_MACHINE_REGISTER_READ);
        contract->implicit_count = 1U;
        break;
    case MINIC_RISCV_MACHINE_FENCE:
    case MINIC_RISCV_MACHINE_FENCE_MASK:
    case MINIC_RISCV_MACHINE_ECALL:
    case MINIC_RISCV_MACHINE_BARRIER:
        contract->required_effects = barrier;
        contract->permitted_effects = barrier | memread | memwrite;
        break;
    default:
        break;
    }
    return true;
}

static bool immediate_range(int64_t immediate, int64_t minimum,
                            int64_t maximum) {
    return immediate >= minimum && immediate <= maximum;
}

static unsigned riscv_memory_access_bytes(uint32_t opcode) {
    switch (opcode) {
    case MINIC_RISCV_MACHINE_LB_FRAME:
    case MINIC_RISCV_MACHINE_LBU_FRAME:
    case MINIC_RISCV_MACHINE_SB_FRAME:
        return 1U;
    case MINIC_RISCV_MACHINE_LH_FRAME:
    case MINIC_RISCV_MACHINE_LHU_FRAME:
    case MINIC_RISCV_MACHINE_SH_FRAME:
        return 2U;
    case MINIC_RISCV_MACHINE_LW_FRAME:
    case MINIC_RISCV_MACHINE_LWU_FRAME:
    case MINIC_RISCV_MACHINE_SW_FRAME:
        return 4U;
    case MINIC_RISCV_MACHINE_LD_FRAME:
    case MINIC_RISCV_MACHINE_SD_FRAME:
        return 8U;
    default:
        return 0U;
    }
}

bool minic_riscv_machine_resolve_frame_access(
    const MinicMachineFunction *fn, uint32_t opcode,
    const MinicMachineOperand *frame_operand,
    unsigned *base_register, int64_t *immediate) {
    const MinicMachineFrameObject *object;
    const unsigned bytes = riscv_memory_access_bytes(opcode);
    const int64_t displacement = frame_operand != NULL &&
        frame_operand->kind == MINIC_MACHINE_OPERAND_FRAME
            ? frame_operand->value.frame.displacement : -1;
    int64_t offset;
    unsigned xlen;
    if (fn == NULL || !riscv_machine_target(fn->target, &xlen) ||
        frame_operand == NULL ||
        frame_operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        frame_operand->value.frame.index >= fn->frame_object_count ||
        fn->frame_objects == NULL || base_register == NULL ||
        immediate == NULL || bytes == 0U) return false;
    object = &fn->frame_objects[frame_operand->value.frame.index];
    /* Incoming arguments need the entry-SP anchor, which is not generally
     * the current SP after stack realignment/alloca. The builder must use
     * explicit, selected address materialization for that case. */
    if (!object->has_fixed_offset ||
        object->area == MINIC_MACHINE_FRAME_INCOMING ||
        object->size_bytes < bytes ||
        displacement < 0 ||
        (uint64_t)displacement > object->size_bytes - bytes ||
        object->fixed_offset > INT64_MAX - displacement)
        return false;
    offset = object->fixed_offset + displacement;
    if (!immediate_range(offset, -2048, 2047) ||
        offset % (int64_t)bytes != 0) return false;
    *base_register = object->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER
                     ? 8U : 2U;
    *immediate = offset;
    return true;
}

bool minic_riscv_machine_validate_selected_instruction(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    void *context) {
    MinicRiscvMachineOpcodeInfo info;
    MinicMachineOpcodeContract contract;
    unsigned xlen;
    int64_t immediate;
    size_t index;
    const MinicTargetInfo *target = fn != NULL ? fn->target : NULL;
    (void)context;
    if (ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        !riscv_machine_target(target, &xlen) ||
        !minic_riscv_machine_opcode_info(target, ins->opcode, &info) ||
        !minic_riscv_machine_lookup_opcode_contract(target, ins->opcode,
                                                      &contract, context) ||
        ins->operand_count != contract.operand_count ||
        (ins->operand_count != 0U && ins->operands == NULL))
        return false;
    for (index = 0U; index < ins->operand_count; ++index)
        if (ins->operands[index].kind == MINIC_MACHINE_OPERAND_ADDRESS &&
            !minic_riscv_machine_validate_address(
                target, &ins->operands[index].value.address, context))
            return false;
    if (info.form == MINIC_RISCV_MACHINE_FORM_LOAD_FRAME ||
        info.form == MINIC_RISCV_MACHINE_FORM_STORE_FRAME) {
        unsigned base_register;
        int64_t displacement;
        return minic_riscv_machine_resolve_frame_access(
            fn, ins->opcode, &ins->operands[1],
            &base_register, &displacement);
    }
    switch (ins->opcode) {
    case MINIC_RISCV_MACHINE_LUI:
    case MINIC_RISCV_MACHINE_AUIPC:
        return immediate_range(ins->operands[1].value.integer,
                               -524288, 1048575);
    case MINIC_RISCV_MACHINE_ADDI:
    case MINIC_RISCV_MACHINE_SLTI:
    case MINIC_RISCV_MACHINE_SLTIU:
    case MINIC_RISCV_MACHINE_XORI:
    case MINIC_RISCV_MACHINE_ORI:
    case MINIC_RISCV_MACHINE_ANDI:
    case MINIC_RISCV_MACHINE_ADDIW:
        return immediate_range(ins->operands[2].value.integer,
                               -2048, 2047);
    case MINIC_RISCV_MACHINE_SLLI:
    case MINIC_RISCV_MACHINE_SRLI:
    case MINIC_RISCV_MACHINE_SRAI:
        return immediate_range(ins->operands[2].value.integer,
                               0, (int64_t)xlen - 1);
    case MINIC_RISCV_MACHINE_SLLIW:
    case MINIC_RISCV_MACHINE_SRLIW:
    case MINIC_RISCV_MACHINE_SRAIW:
        return immediate_range(ins->operands[2].value.integer, 0, 31);
    case MINIC_RISCV_MACHINE_JALR_BRANCH:
    case MINIC_RISCV_MACHINE_JALR_CALL:
        return immediate_range(ins->operands[1].value.integer,
                               -2048, 2047);
    case MINIC_RISCV_MACHINE_LI:
        immediate = ins->operands[1].value.integer;
        return xlen == 64U ||
               immediate_range(immediate, INT32_MIN, UINT32_MAX);
    case MINIC_RISCV_MACHINE_FENCE_MASK:
        return immediate_range(ins->operands[0].value.integer, 0, 15) &&
               immediate_range(ins->operands[1].value.integer, 0, 15);
    case MINIC_RISCV_MACHINE_LA_PCREL:
        return ins->operands[1].value.symbol.relocation ==
                   MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL;
    case MINIC_RISCV_MACHINE_LA_GOT:
        return ins->operands[1].value.symbol.relocation ==
                   MINIC_RISCV_MACHINE_RELOC_GOT;
    case MINIC_RISCV_MACHINE_CALL_DIRECT:
    case MINIC_RISCV_MACHINE_TAIL_DIRECT:
        return ins->operands[0].value.symbol.relocation ==
                   MINIC_RISCV_MACHINE_RELOC_CALL;
    default:
        return true;
    }
}
