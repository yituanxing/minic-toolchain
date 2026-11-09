#include "target/riscv/machine_opcode.h"
#include "target/riscv/machine_target_state.h"

#include "core/symbol_identity.h"
#include "core/core_ir.h"

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
    RV_OP(JALR_BLOCK, "jalr", REGISTER_BLOCK),
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
    RV_OP(UNREACHABLE, NULL, NONE),
    RV64_OP(FMV_X_W, "fmv.x.w", REGISTER_REGISTER),
    RV64_OP(FMV_W_X, "fmv.w.x", REGISTER_REGISTER),
    RV64_OP(FMV_X_D, "fmv.x.d", REGISTER_REGISTER),
    RV64_OP(FMV_D_X, "fmv.d.x", REGISTER_REGISTER),
    RV64_OP(FADD_S, "fadd.s", REGISTER_REGISTER_REGISTER),
    RV64_OP(FSUB_S, "fsub.s", REGISTER_REGISTER_REGISTER),
    RV64_OP(FMUL_S, "fmul.s", REGISTER_REGISTER_REGISTER),
    RV64_OP(FDIV_S, "fdiv.s", REGISTER_REGISTER_REGISTER),
    RV64_OP(FADD_D, "fadd.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FSUB_D, "fsub.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FMUL_D, "fmul.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FDIV_D, "fdiv.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FEQ_D, "feq.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FLT_D, "flt.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FLE_D, "fle.d", REGISTER_REGISTER_REGISTER),
    RV64_OP(FCVT_D_W, "fcvt.d.w", REGISTER_REGISTER),
    RV64_OP(FCVT_D_WU, "fcvt.d.wu", REGISTER_REGISTER),
    RV64_OP(FCVT_D_L, "fcvt.d.l", REGISTER_REGISTER),
    RV64_OP(FCVT_D_LU, "fcvt.d.lu", REGISTER_REGISTER),
    RV64_OP(FCVT_W_D_RTZ, "fcvt.w.d", REGISTER_REGISTER),
    RV64_OP(FCVT_WU_D_RTZ, "fcvt.wu.d", REGISTER_REGISTER),
    RV64_OP(FCVT_L_D_RTZ, "fcvt.l.d", REGISTER_REGISTER),
    RV64_OP(FCVT_LU_D_RTZ, "fcvt.lu.d", REGISTER_REGISTER),
    RV64_OP(FCVT_D_S, "fcvt.d.s", REGISTER_REGISTER),
    RV64_OP(FCVT_S_D, "fcvt.s.d", REGISTER_REGISTER),
    RV64_OP(FSGNJN_D, "fsgnjn.d", REGISTER_REGISTER_REGISTER)
};

#undef RV_OP
#undef RV64_OP

static uint32_t riscv_machine_fp_features(uint32_t opcode) {
    if (opcode < MINIC_RISCV_MACHINE_FMV_X_W ||
        opcode >= MINIC_RISCV_MACHINE_LAST) return 0U;
    if (opcode == MINIC_RISCV_MACHINE_FMV_X_W ||
        opcode == MINIC_RISCV_MACHINE_FMV_W_X ||
        (opcode >= MINIC_RISCV_MACHINE_FADD_S &&
         opcode <= MINIC_RISCV_MACHINE_FDIV_S))
        return MINIC_RISCV_FEATURE_F;
    return MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
}

/* Never infer a mixed-bank operand from its assembly spelling. The physical
 * opcode, not Core type or GNU serializer, owns all FPR/GPR bank choices. */
static bool riscv_machine_fp_operand_bank(uint32_t opcode, size_t index,
                                         MinicMachineRegisterBank *bank) {
    bool destination_is_gpr = false;
    bool source_is_gpr = false;
    bool three_registers;
    if (bank == NULL || riscv_machine_fp_features(opcode) == 0U ||
        index >= 3U) return false;
    switch (opcode) {
    case MINIC_RISCV_MACHINE_FMV_X_W:
    case MINIC_RISCV_MACHINE_FMV_X_D:
    case MINIC_RISCV_MACHINE_FCVT_W_D_RTZ:
    case MINIC_RISCV_MACHINE_FCVT_WU_D_RTZ:
    case MINIC_RISCV_MACHINE_FCVT_L_D_RTZ:
    case MINIC_RISCV_MACHINE_FCVT_LU_D_RTZ:
    case MINIC_RISCV_MACHINE_FEQ_D:
    case MINIC_RISCV_MACHINE_FLT_D:
    case MINIC_RISCV_MACHINE_FLE_D:
        destination_is_gpr = true;
        break;
    case MINIC_RISCV_MACHINE_FMV_W_X:
    case MINIC_RISCV_MACHINE_FMV_D_X:
    case MINIC_RISCV_MACHINE_FCVT_D_W:
    case MINIC_RISCV_MACHINE_FCVT_D_WU:
    case MINIC_RISCV_MACHINE_FCVT_D_L:
    case MINIC_RISCV_MACHINE_FCVT_D_LU:
        source_is_gpr = true;
        break;
    default:
        break;
    }
    three_registers =
        (opcode >= MINIC_RISCV_MACHINE_FADD_S &&
         opcode <= MINIC_RISCV_MACHINE_FLE_D) ||
        opcode == MINIC_RISCV_MACHINE_FSGNJN_D;
    if (index >= (three_registers ? 3U : 2U)) return false;
    *bank = (index == 0U && destination_is_gpr) ||
            (index == 1U && source_is_gpr)
        ? MINIC_MACHINE_REGISTER_GENERAL : MINIC_MACHINE_REGISTER_FLOAT;
    return true;
}

static bool riscv_machine_target(const MinicTargetInfo *target, unsigned *xlen) {
    MinicRiscvIsa isa;
    if (target == NULL || xlen == NULL) return false;
    if (!minic_riscv_target_isa_load(target, &isa)) return false;
    if (target->architecture == MINIC_TARGET_ARCH_RISCV32 &&
        isa.xlen == 32U &&
        isa.abi == MINIC_RISCV_ABI_ILP32) {
        *xlen = 32U;
        return true;
    }
    if (target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
        isa.xlen == 64U &&
        (isa.abi == MINIC_RISCV_ABI_LP64 ||
         isa.abi == MINIC_RISCV_ABI_LP64D)) {
        *xlen = 64U;
        return true;
    }
    return false;
}

bool minic_riscv_machine_opcode_info(const MinicTargetInfo *target,
                                      uint32_t opcode,
                                      MinicRiscvMachineOpcodeInfo *info) {
    const RiscvMachineDescription *desc;
    MinicRiscvIsa isa;
    unsigned xlen;
    if (info == NULL || !riscv_machine_target(target, &xlen) ||
        opcode == MINIC_RISCV_MACHINE_INVALID ||
        opcode >= MINIC_RISCV_MACHINE_LAST) return false;
    desc = &riscv_machine_descriptions[opcode];
    if (desc->form == MINIC_RISCV_MACHINE_FORM_INVALID ||
        (desc->rv64_only && xlen != 64U)) return false;
    if (riscv_machine_fp_features(opcode) != 0U) {
        const uint32_t required = riscv_machine_fp_features(opcode);
        if (!minic_riscv_target_isa_load(target, &isa) ||
            isa.abi != MINIC_RISCV_ABI_LP64D ||
            !minic_riscv_codegen_has(&isa, required)) return false;
    }
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
           generated_features == (uint64_t)minic_riscv_target_isa(target).codegen_features;
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
    MinicRiscvIsa isa;
    unsigned xlen;
    (void)context;
    if (reg == NULL || !riscv_machine_target(target, &xlen) ||
        reg->number >= 32U || reg->width_bits != xlen) return false;
    if (reg->bank == MINIC_MACHINE_REGISTER_GENERAL) return true;
    return reg->bank == MINIC_MACHINE_REGISTER_FLOAT && xlen == 64U &&
        minic_riscv_target_isa_load(target, &isa) &&
        isa.abi == MINIC_RISCV_ABI_LP64D &&
        minic_riscv_codegen_has(&isa, MINIC_RISCV_FEATURE_F |
                                   MINIC_RISCV_FEATURE_D);
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
           address->base.bank == MINIC_MACHINE_REGISTER_GENERAL &&
           minic_riscv_machine_validate_register(target, &address->base, context);
}

bool minic_riscv_machine_validate_relocation(
    const MinicTargetInfo *target, const char *symbol,
    uint32_t relocation, int64_t addend, void *context) {
    unsigned xlen;
    MinicCoreInlineAsmImmediate immediate = {0};
    (void)context;
    if (!riscv_machine_target(target, &xlen) ||
        !minic_core_assembler_symbol_lexically_safe(symbol)) return false;
    if (relocation == MINIC_RISCV_MACHINE_RELOC_SOURCE_IMMEDIATE) {
        immediate.kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL;
        immediate.symbol = (char *)symbol;
        immediate.byte_addend = addend;
        return minic_core_inline_asm_relocation_valid(&immediate);
    }
    return addend == 0 &&
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
static void float_register_effect(MinicMachineRegisterEffect *effect,
                                  unsigned id, unsigned access) {
    effect->reg.number = id;
    effect->reg.width_bits = 64U;
    effect->reg.bank = MINIC_MACHINE_REGISTER_FLOAT;
    effect->access = access;
}

/* Physical effects are specified by the target, never self-attested by an
 * incoming MachineInstruction. A standard call clobbers every psABI
 * caller-saved integer register; it reads all potential argument registers
 * conservatively without changing existing argument assignment. */
static void call_effects(MinicMachineOpcodeContract *contract,
                         const MinicTargetInfo *target, unsigned xlen,
                         bool tail) {
    static const unsigned volatile_ids[] = {
        5U, 6U, 7U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U,
        28U, 29U, 30U, 31U
    };
    static const unsigned fp_volatile_ids[] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U,
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
    if (xlen == 64U && minic_riscv_target_isa(target).abi ==
        MINIC_RISCV_ABI_LP64D) {
        for (i = 0U; i < sizeof(fp_volatile_ids) /
                         sizeof(fp_volatile_ids[0]); ++i) {
            const unsigned id = fp_volatile_ids[i];
            const unsigned access = MINIC_MACHINE_REGISTER_WRITE |
                MINIC_MACHINE_REGISTER_CLOBBER |
                (id >= 10U && id <= 17U ?
                    MINIC_MACHINE_REGISTER_READ : 0U);
            float_register_effect(
                &contract->implicit[contract->implicit_count++], id, access);
        }
    }
}

static void operand(MinicMachineOpcodeContract *contract, size_t index,
                    MinicMachineOperandKind kind, unsigned access) {
    contract->operand_kinds[index] = kind;
    contract->register_access[index] = access;
}

/* Caller already resolved this opcode against the immutable per-invocation
 * target.  Reuse that resolution when the strict selected-instruction verifier
 * also needs the contract: looking it up a second time used to repeat the
 * TargetInfo ISA snapshot and feature admission for every emitted instruction.
 * The public entry point still validates arbitrary external callers. */
static bool riscv_machine_fill_opcode_contract(
    const MinicTargetInfo *target, uint32_t opcode,
    const MinicRiscvMachineOpcodeInfo *info, unsigned xlen,
    MinicMachineOpcodeContract *contract) {
    const unsigned read = MINIC_MACHINE_REGISTER_READ;
    const unsigned write = MINIC_MACHINE_REGISTER_WRITE;
    const unsigned memread = MINIC_MACHINE_EFFECT_MEMORY_READ;
    const unsigned memwrite = MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    const unsigned barrier = MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    const unsigned branch = MINIC_MACHINE_EFFECT_BRANCH;
    const unsigned call = MINIC_MACHINE_EFFECT_CALL;
    if (contract == NULL || info == NULL) return false;
    memset(contract, 0, sizeof(*contract));
    if (riscv_machine_fp_features(opcode) != 0U &&
        minic_riscv_target_isa(target).strict_isa)
        contract->required_generated_features =
            riscv_machine_fp_features(opcode);
    switch (info->form) {
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
                info->form == MINIC_RISCV_MACHINE_FORM_LOAD ||
                info->form == MINIC_RISCV_MACHINE_FORM_LOAD_FRAME
                    ? write : read);
        operand(contract, 1U,
                info->form == MINIC_RISCV_MACHINE_FORM_LOAD_FRAME ||
                info->form == MINIC_RISCV_MACHINE_FORM_STORE_FRAME
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
    case MINIC_RISCV_MACHINE_JALR_BLOCK:
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
        call_effects(contract, target, xlen, false);
        break;
    case MINIC_RISCV_MACHINE_TAIL_DIRECT:
        contract->required_effects = call | branch | memread | memwrite;
        contract->permitted_effects = call | branch | memread | memwrite | barrier;
        contract->terminates_block = true;
        call_effects(contract, target, xlen, true);
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

bool minic_riscv_machine_lookup_opcode_contract(
    const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    MinicRiscvMachineOpcodeInfo info;
    unsigned xlen;
    (void)context;
    if (contract == NULL ||
        !minic_riscv_machine_opcode_info(target, opcode, &info)) return false;
    /* opcode_info already verified the exact XLEN/ABI machine state. */
    xlen = target->architecture == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    return riscv_machine_fill_opcode_contract(target, opcode, &info, xlen,
                                              contract);
}

/* The selected-instruction validator independently checks ISA admission,
 * immediates, frame references and real CFG edges. Its only use of a freshly
 * reconstructed opcode contract was the operand count; the generic physical
 * MachineIR verifier owns the full register/effect contract check. Keep a
 * separate, exhaustive form arity check here instead of rebuilding the same
 * full contract twice per selected instruction. */
static bool riscv_machine_form_operand_count(MinicRiscvMachineForm form,
                                             size_t *operand_count) {
    if (operand_count == NULL) return false;
    switch (form) {
    case MINIC_RISCV_MACHINE_FORM_NONE:
        *operand_count = 0U;
        return true;
    case MINIC_RISCV_MACHINE_FORM_REGISTER:
    case MINIC_RISCV_MACHINE_FORM_BLOCK:
    case MINIC_RISCV_MACHINE_FORM_SYMBOL:
        *operand_count = 1U;
        return true;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER:
    case MINIC_RISCV_MACHINE_FORM_REGISTER_IMMEDIATE:
    case MINIC_RISCV_MACHINE_FORM_LOAD:
    case MINIC_RISCV_MACHINE_FORM_STORE:
    case MINIC_RISCV_MACHINE_FORM_LOAD_FRAME:
    case MINIC_RISCV_MACHINE_FORM_STORE_FRAME:
    case MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK:
    case MINIC_RISCV_MACHINE_FORM_REGISTER_SYMBOL:
    case MINIC_RISCV_MACHINE_FORM_FENCE_MASK:
        *operand_count = 2U;
        return true;
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_IMMEDIATE:
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_REGISTER:
    case MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK:
        *operand_count = 3U;
        return true;
    default:
        return false;
    }
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

/* Do not allow a caller to claim an arbitrary physical CFG edge for a
 * register-indirect jump. The immediately preceding typed LA_BLOCK must
 * materialize precisely that destination into precisely that register.
 * This keeps the legacy far-jump `lla; jalr` structure explicit in MIR,
 * subject to the whole-function verifier before any GNU assembly output. */
static bool riscv_machine_far_branch_pair_valid(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *jump) {
    size_t block_id;
    if (function == NULL || jump == NULL || jump->operand_count != 2U ||
        jump->operands == NULL || function->blocks == NULL)
        return false;
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        const MinicMachineBlock *block = &function->blocks[block_id];
        size_t instruction_id;
        for (instruction_id = 1U;
             instruction_id < block->instruction_count; ++instruction_id) {
            const MinicMachineInstruction *current =
                &block->instructions[instruction_id];
            const MinicMachineInstruction *previous =
                &block->instructions[instruction_id - 1U];
            if (current != jump) continue;
            if (previous->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
                previous->opcode != MINIC_RISCV_MACHINE_LA_BLOCK ||
                previous->operand_count != 2U || previous->operands == NULL ||
                previous->operands[0].kind != MINIC_MACHINE_OPERAND_REGISTER ||
                previous->operands[1].kind !=
                    MINIC_MACHINE_OPERAND_BLOCK_ADDRESS ||
                jump->operands[0].kind != MINIC_MACHINE_OPERAND_REGISTER ||
                jump->operands[1].kind != MINIC_MACHINE_OPERAND_BLOCK)
                return false;
            return previous->operands[0].value.physical.reg.number ==
                       jump->operands[0].value.physical.reg.number &&
                   previous->operands[0].value.physical.reg.width_bits ==
                       jump->operands[0].value.physical.reg.width_bits &&
                   previous->operands[1].value.block_id ==
                       jump->operands[1].value.block_id;
        }
    }
    return false;
}

/* Direct RISC-V conditional branches have a signed 4KiB reach; JAL has a
 * signed 1MiB reach. GNU relaxation cannot legalize an arbitrary Core CFG
 * edge after the fact. In the current thin MIR (which has no byte layout
 * pass), accept only a provably adjacent block or a next-next block separated
 * by at most 16 already-selected base-I operations, never source ASM. Every
 * other edge must use target-owned auxiliary CFG blocks and explicit typed
 * LA_BLOCK/JALR_BLOCK far materialization before whole-function verify. */
static bool riscv_machine_short_branch_safe(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *branch, size_t target_block) {
    size_t block_id;
    if (function == NULL || function->blocks == NULL || branch == NULL ||
        target_block >= function->block_count) return false;
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        const MinicMachineBlock *block = &function->blocks[block_id];
        size_t i;
        for (i = 0U; i < block->instruction_count; ++i) {
            if (&block->instructions[i] != branch) continue;
            if (target_block == block_id + 1U) return true;
            if (target_block != block_id + 2U) return false;
            {
                const MinicMachineBlock *between =
                    &function->blocks[block_id + 1U];
                size_t j;
                if (between->instruction_count > 16U) return false;
                for (j = 0U; j < between->instruction_count; ++j) {
                    const MinicMachineInstruction *ins =
                        &between->instructions[j];
                    if (ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED &&
                        ins->kind != MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL)
                        return false;
                }
                return true;
            }
        }
    }
    return false;
}

bool minic_riscv_machine_validate_selected_instruction(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    void *context) {
    MinicRiscvMachineOpcodeInfo info;
    size_t operand_count;
    unsigned xlen;
    int64_t immediate;
    size_t index;
    const MinicTargetInfo *target = fn != NULL ? fn->target : NULL;
    (void)context;
    if (ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        !minic_riscv_machine_opcode_info(target, ins->opcode, &info) ||
        !riscv_machine_form_operand_count(info.form, &operand_count) ||
        ins->operand_count != operand_count ||
        (ins->operand_count != 0U && ins->operands == NULL))
        return false;
    xlen = target->architecture == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    if (riscv_machine_fp_features(ins->opcode) != 0U) {
        for (index = 0U; index < ins->operand_count; ++index) {
            MinicMachineRegisterBank required_bank;
            if (!riscv_machine_fp_operand_bank(ins->opcode, index,
                    &required_bank) ||
                ins->operands[index].kind != MINIC_MACHINE_OPERAND_REGISTER ||
                ins->operands[index].value.physical.reg.bank != required_bank)
                return false;
        }
    }
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
    if (info.form == MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK ||
        (info.form == MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK &&
         ins->opcode != MINIC_RISCV_MACHINE_LA_BLOCK &&
         ins->opcode != MINIC_RISCV_MACHINE_JALR_BLOCK) ||
        info.form == MINIC_RISCV_MACHINE_FORM_BLOCK) {
        const size_t branch_index = info.form ==
            MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK ? 2U :
            info.form == MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK ? 1U : 0U;
        if (!riscv_machine_short_branch_safe(fn, ins,
                ins->operands[branch_index].value.block_id)) return false;
    }
    switch (ins->opcode) {
    case MINIC_RISCV_MACHINE_JALR_BLOCK:
        return riscv_machine_far_branch_pair_valid(fn, ins);
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
