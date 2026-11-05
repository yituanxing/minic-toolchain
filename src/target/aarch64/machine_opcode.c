#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/asm_contract_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef enum A64Form {
    A64_FORM_RR, A64_FORM_RRR, A64_FORM_RRRR, A64_FORM_RRRI,
    A64_FORM_RI, A64_FORM_RII, A64_FORM_RRI,
    A64_FORM_CMP, A64_FORM_CMP_IMM, A64_FORM_CSET,
    A64_FORM_MEMORY, A64_FORM_ORDERED_MEMORY, A64_FORM_MEMORY_POST_BYTE,
    A64_FORM_FRAME, A64_FORM_PAIR,
    A64_FORM_ADRP, A64_FORM_ADD_LO12, A64_FORM_ADRP_BLOCK,
    A64_FORM_ADD_LO12_BLOCK, A64_FORM_GOT_LOAD,
    A64_FORM_BLOCK, A64_FORM_COND_BLOCK, A64_FORM_REG_BLOCK,
    A64_FORM_REG, A64_FORM_CALL_SYMBOL, A64_FORM_NONE,
    A64_FORM_FP_RR, A64_FORM_FP_RRR, A64_FORM_FP_CMP,
    A64_FORM_FP_CONVERT, A64_FORM_FP_FROM_GP, A64_FORM_GP_FROM_FP,
    A64_FORM_FP_MEMORY, A64_FORM_VEC_RRR
} A64Form;

typedef struct A64Spec {
    const char *name;
    A64Form form;
    unsigned int effects;
    unsigned int memory_width;
    bool writes_flags;
    bool reads_flags;
    bool writes_link;
    bool reads_link;
    bool ends_block;
    bool permits_fallthrough;
    bool indirect_successors;
    bool updates_dest;
} A64Spec;

#define R MINIC_MACHINE_EFFECT_MEMORY_READ
#define W MINIC_MACHINE_EFFECT_MEMORY_WRITE
#define B MINIC_MACHINE_EFFECT_BRANCH
#define C MINIC_MACHINE_EFFECT_CALL
#define T MINIC_MACHINE_EFFECT_RETURN
#define OP(op, mnemonic, shape, fx) [MINIC_A64_MACHINE_##op] = {mnemonic, A64_FORM_##shape, fx}

static const A64Spec a64_specs[MINIC_A64_MACHINE_NOP + 1U] = {
    OP(MOV, "mov", RR, 0), OP(MVN, "mvn", RR, 0),
    OP(NEG, "neg", RR, 0), OP(CLZ, "clz", RR, 0),
    OP(RBIT, "rbit", RR, 0), OP(SXTW, "sxtw", RR, 0),
    OP(UXTB, "uxtb", RR, 0), OP(UXTH, "uxth", RR, 0),
    OP(SXTB, "sxtb", RR, 0), OP(SXTH, "sxth", RR, 0),
    OP(MOVZ, "movz", RII, 0), OP(MOVN, "movn", RII, 0),
    [MINIC_A64_MACHINE_MOVK] = {"movk", A64_FORM_RII, 0, .updates_dest = true},
    OP(ADD, "add", RRR, 0), OP(SUB, "sub", RRR, 0),
    [MINIC_A64_MACHINE_ADDS] = {"adds", A64_FORM_RRR, 0, .writes_flags = true},
    [MINIC_A64_MACHINE_SUBS] = {"subs", A64_FORM_RRR, 0, .writes_flags = true},
    [MINIC_A64_MACHINE_ADC] = {"adc", A64_FORM_RRR, 0, .reads_flags = true},
    [MINIC_A64_MACHINE_SBC] = {"sbc", A64_FORM_RRR, 0, .reads_flags = true},
    OP(AND, "and", RRR, 0), OP(ORR, "orr", RRR, 0),
    OP(EOR, "eor", RRR, 0), OP(MUL, "mul", RRR, 0),
    OP(UDIV, "udiv", RRR, 0), OP(SDIV, "sdiv", RRR, 0),
    OP(LSL, "lsl", RRR, 0), OP(LSR, "lsr", RRR, 0),
    OP(ASR, "asr", RRR, 0), OP(MSUB, "msub", RRRR, 0),
    OP(MADD, "madd", RRRR, 0),
    OP(SMULH, "smulh", RRR, 0), OP(UMULH, "umulh", RRR, 0),
    OP(SMULL, "smull", RRR, 0), OP(UMULL, "umull", RRR, 0),
    OP(UXTW, "uxtw", RR, 0),
    [MINIC_A64_MACHINE_NEGS] = {"negs", A64_FORM_RR, 0,
        .writes_flags = true},
    [MINIC_A64_MACHINE_NGC] = {"ngc", A64_FORM_RR, 0,
        .reads_flags = true},
    OP(ADD_IMM, "add", RRI, 0), OP(SUB_IMM, "sub", RRI, 0),
    [MINIC_A64_MACHINE_ADDS_IMM] = {"adds", A64_FORM_RRI, 0, .writes_flags = true},
    [MINIC_A64_MACHINE_SUBS_IMM] = {"subs", A64_FORM_RRI, 0, .writes_flags = true},
    OP(AND_ALIGN_IMM, "and", RRI, 0),
    OP(ORR_LSL, "orr", RRRI, 0),
    OP(LSL_IMM, "lsl", RRI, 0), OP(LSR_IMM, "lsr", RRI, 0),
    OP(ASR_IMM, "asr", RRI, 0),
    [MINIC_A64_MACHINE_CMP] = {"cmp", A64_FORM_CMP, 0, .writes_flags = true},
    [MINIC_A64_MACHINE_CMP_IMM] = {"cmp", A64_FORM_CMP_IMM, 0, .writes_flags = true},
    [MINIC_A64_MACHINE_CSET] = {"cset", A64_FORM_CSET, 0, .reads_flags = true},
    [MINIC_A64_MACHINE_LDRB] = {"ldrb", A64_FORM_MEMORY, R, 1},
    [MINIC_A64_MACHINE_LDRH] = {"ldrh", A64_FORM_MEMORY, R, 2},
    [MINIC_A64_MACHINE_LDR_W] = {"ldr", A64_FORM_MEMORY, R, 4},
    [MINIC_A64_MACHINE_LDR_X] = {"ldr", A64_FORM_MEMORY, R, 8},
    [MINIC_A64_MACHINE_LDRSB] = {"ldrsb", A64_FORM_MEMORY, R, 1},
    [MINIC_A64_MACHINE_LDRSH] = {"ldrsh", A64_FORM_MEMORY, R, 2},
    [MINIC_A64_MACHINE_LDRSW] = {"ldrsw", A64_FORM_MEMORY, R, 4},
    [MINIC_A64_MACHINE_STRB] = {"strb", A64_FORM_MEMORY, W, 1},
    [MINIC_A64_MACHINE_STRH] = {"strh", A64_FORM_MEMORY, W, 2},
    [MINIC_A64_MACHINE_STR_W] = {"str", A64_FORM_MEMORY, W, 4},
    [MINIC_A64_MACHINE_STR_X] = {"str", A64_FORM_MEMORY, W, 8},
    [MINIC_A64_MACHINE_LDRB_POST] = {"ldrb", A64_FORM_MEMORY_POST_BYTE, R, 1},
    [MINIC_A64_MACHINE_STRB_POST] = {"strb", A64_FORM_MEMORY_POST_BYTE, W, 1},
    [MINIC_A64_MACHINE_LDARB] = {"ldarb", A64_FORM_ORDERED_MEMORY, R, 1},
    [MINIC_A64_MACHINE_LDARH] = {"ldarh", A64_FORM_ORDERED_MEMORY, R, 2},
    [MINIC_A64_MACHINE_LDAR_W] = {"ldar", A64_FORM_ORDERED_MEMORY, R, 4},
    [MINIC_A64_MACHINE_LDAR_X] = {"ldar", A64_FORM_ORDERED_MEMORY, R, 8},
    [MINIC_A64_MACHINE_STLRB] = {"stlrb", A64_FORM_ORDERED_MEMORY, W, 1},
    [MINIC_A64_MACHINE_STLRH] = {"stlrh", A64_FORM_ORDERED_MEMORY, W, 2},
    [MINIC_A64_MACHINE_STLR_W] = {"stlr", A64_FORM_ORDERED_MEMORY, W, 4},
    [MINIC_A64_MACHINE_STLR_X] = {"stlr", A64_FORM_ORDERED_MEMORY, W, 8},
    [MINIC_A64_MACHINE_LDR_FRAME_W] = {"ldr", A64_FORM_FRAME, R, 4},
    [MINIC_A64_MACHINE_LDR_FRAME_X] = {"ldr", A64_FORM_FRAME, R, 8},
    [MINIC_A64_MACHINE_STR_FRAME_W] = {"str", A64_FORM_FRAME, W, 4},
    [MINIC_A64_MACHINE_STR_FRAME_X] = {"str", A64_FORM_FRAME, W, 8},
    [MINIC_A64_MACHINE_STP_PRE_X] = {"stp", A64_FORM_PAIR, W, 8},
    [MINIC_A64_MACHINE_LDP_POST_X] = {"ldp", A64_FORM_PAIR, R, 8},
    [MINIC_A64_MACHINE_STP_OFFSET_X] = {"stp", A64_FORM_PAIR, W, 8},
    [MINIC_A64_MACHINE_LDP_OFFSET_X] = {"ldp", A64_FORM_PAIR, R, 8},
    OP(ADRP, "adrp", ADRP, 0), OP(ADD_LO12, "add", ADD_LO12, 0),
    OP(ADRP_BLOCK, "adrp", ADRP_BLOCK, 0),
    OP(ADD_LO12_BLOCK, "add", ADD_LO12_BLOCK, 0),
    [MINIC_A64_MACHINE_LDR_GOT_LO12] = {"ldr", A64_FORM_GOT_LOAD, R, 8},
    [MINIC_A64_MACHINE_B] = {"b", A64_FORM_BLOCK, B, .ends_block = true},
    [MINIC_A64_MACHINE_B_COND] = {"b", A64_FORM_COND_BLOCK, B,
        .reads_flags = true, .ends_block = true, .permits_fallthrough = true},
    [MINIC_A64_MACHINE_CBZ] = {"cbz", A64_FORM_REG_BLOCK, B,
        .ends_block = true, .permits_fallthrough = true},
    [MINIC_A64_MACHINE_CBNZ] = {"cbnz", A64_FORM_REG_BLOCK, B,
        .ends_block = true, .permits_fallthrough = true},
    [MINIC_A64_MACHINE_BR] = {"br", A64_FORM_REG, B,
        .ends_block = true, .indirect_successors = true},
    [MINIC_A64_MACHINE_BL] = {"bl", A64_FORM_CALL_SYMBOL, C,
        .writes_link = true},
    [MINIC_A64_MACHINE_BLR] = {"blr", A64_FORM_REG, C,
        .writes_link = true},
    [MINIC_A64_MACHINE_RET] = {"ret", A64_FORM_NONE, T,
        .reads_link = true, .ends_block = true},
    [MINIC_A64_MACHINE_BRK] = {"brk", A64_FORM_NONE, B,
        .ends_block = true},
    [MINIC_A64_MACHINE_FMOV] = {"fmov", A64_FORM_FP_RR, 0},
    [MINIC_A64_MACHINE_FNEG] = {"fneg", A64_FORM_FP_RR, 0},
    [MINIC_A64_MACHINE_FADD] = {"fadd", A64_FORM_FP_RRR, 0},
    [MINIC_A64_MACHINE_FSUB] = {"fsub", A64_FORM_FP_RRR, 0},
    [MINIC_A64_MACHINE_FMUL] = {"fmul", A64_FORM_FP_RRR, 0},
    [MINIC_A64_MACHINE_FDIV] = {"fdiv", A64_FORM_FP_RRR, 0},
    [MINIC_A64_MACHINE_FCMP] = {"fcmp", A64_FORM_FP_CMP, 0,
        .writes_flags = true},
    [MINIC_A64_MACHINE_FCVT] = {"fcvt", A64_FORM_FP_CONVERT, 0},
    [MINIC_A64_MACHINE_SCVTF] = {"scvtf", A64_FORM_FP_FROM_GP, 0},
    [MINIC_A64_MACHINE_UCVTF] = {"ucvtf", A64_FORM_FP_FROM_GP, 0},
    [MINIC_A64_MACHINE_FCVTZS] = {"fcvtzs", A64_FORM_GP_FROM_FP, 0},
    [MINIC_A64_MACHINE_FCVTZU] = {"fcvtzu", A64_FORM_GP_FROM_FP, 0},
    [MINIC_A64_MACHINE_FMOV_GP_FROM_FP] = {"fmov", A64_FORM_GP_FROM_FP, 0},
    [MINIC_A64_MACHINE_FMOV_FP_FROM_GP] = {"fmov", A64_FORM_FP_FROM_GP, 0},
    [MINIC_A64_MACHINE_LDR_FP_S] = {"ldr", A64_FORM_FP_MEMORY, R, 4},
    [MINIC_A64_MACHINE_LDR_FP_D] = {"ldr", A64_FORM_FP_MEMORY, R, 8},
    [MINIC_A64_MACHINE_LDR_FP_Q] = {"ldr", A64_FORM_FP_MEMORY, R, 16},
    [MINIC_A64_MACHINE_STR_FP_S] = {"str", A64_FORM_FP_MEMORY, W, 4},
    [MINIC_A64_MACHINE_STR_FP_D] = {"str", A64_FORM_FP_MEMORY, W, 8},
    [MINIC_A64_MACHINE_STR_FP_Q] = {"str", A64_FORM_FP_MEMORY, W, 16},
    [MINIC_A64_MACHINE_VECTOR_EOR_16B] = {"eor", A64_FORM_VEC_RRR, 0},
    OP(NOP, "nop", NONE, 0)
};

#undef OP
#undef R
#undef W
#undef B
#undef C
#undef T

static const A64Spec *a64_spec(uint32_t opcode) {
    if (opcode == 0U || opcode >= sizeof(a64_specs) / sizeof(a64_specs[0]) ||
        a64_specs[opcode].name == NULL) return NULL;
    return &a64_specs[opcode];
}

static MinicMachineRegister a64_implicit(unsigned int number,
                                        MinicMachineRegisterBank bank,
                                        unsigned int width) {
    MinicMachineRegister result = {number, (uint16_t)width, bank};
    return result;
}

static void a64_add_implicit(MinicMachineOpcodeContract *contract,
    MinicMachineRegister reg, unsigned int access) {
    MinicMachineRegisterEffect *effect =
        &contract->implicit[contract->implicit_count++];
    effect->reg = reg;
    effect->access = access;
}

static bool a64_register(const MinicTargetInfo *target,
    const MinicMachineRegister *reg, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        reg == NULL) return false;
    switch (reg->bank) {
    case MINIC_MACHINE_REGISTER_GENERAL:
        return reg->number <= MINIC_A64_MACHINE_SP &&
            (reg->width_bits == 32U || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_FLOAT:
        return reg->number < 32U &&
            (reg->width_bits == 32U || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_VECTOR:
        return reg->number < 32U && reg->width_bits == 128U;
    case MINIC_MACHINE_REGISTER_FLAGS:
        return reg->number == 0U && reg->width_bits == 4U;
    default:
        return false;
    }
}

static bool a64_features(const MinicTargetInfo *target,
                         uint64_t features, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        (features == MINIC_A64_MACHINE_FEATURE_BASE ||
         features == (MINIC_A64_MACHINE_FEATURE_BASE |
                      MINIC_A64_MACHINE_FEATURE_FP_SIMD));
}

static bool a64_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t reloc, int64_t addend, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        symbol != NULL && symbol[0] != '\0' && addend == 0 &&
        reloc >= MINIC_A64_RELOC_DIRECT && reloc <= MINIC_A64_RELOC_GOT_LO12;
}

static bool a64_address(const MinicTargetInfo *target,
    const MinicMachineAddress *address, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        address == NULL || !address->has_base || address->has_index ||
        address->has_segment ||
        (address->scale != 0U && address->scale != 1U) ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.width_bits != 64U ||
        address->base.number > MINIC_A64_MACHINE_SP ||
        address->base.number == MINIC_A64_MACHINE_ZR) return false;
    switch (address->target_mode) {
    case MINIC_A64_ADDRESS_BASE_ONLY:
        return !address->has_symbol && address->displacement == 0;
    case MINIC_A64_ADDRESS_UNSIGNED:
        return !address->has_symbol && address->displacement >= 0 &&
            /* The independent selected opcode constrains the access size.
             * Q-register 128-bit loads allow unsigned 12-bit scaled offsets
             * up to 4095 * 16; rejecting them here would reject valid ISA. */
            address->displacement <= 65520;
    case MINIC_A64_ADDRESS_PRE_INDEX:
    case MINIC_A64_ADDRESS_POST_INDEX:
        return !address->has_symbol && address->displacement >= -512 &&
            address->displacement <= 504;
    case MINIC_A64_ADDRESS_PAIR_OFFSET:
        return !address->has_symbol && address->displacement >= -512 &&
            address->displacement <= 504;
    case MINIC_A64_ADDRESS_GOT_LO12:
        return address->has_symbol && address->symbol != NULL &&
            address->relocation == MINIC_A64_RELOC_GOT_LO12 &&
            address->addend == 0 && address->displacement == 0;
    default:
        return false;
    }
}

static bool a64_lookup(const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    const A64Spec *spec = a64_spec(opcode);
    size_t n = 0U;
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        contract == NULL || spec == NULL) return false;
    memset(contract, 0, sizeof(*contract));
#define REG(access) do { contract->operand_kinds[n] = MINIC_MACHINE_OPERAND_REGISTER; \
    contract->register_access[n++] = (access); } while (0)
#define ARG(kind) do { contract->operand_kinds[n++] = (kind); } while (0)
#define READ MINIC_MACHINE_REGISTER_READ
#define WRITE MINIC_MACHINE_REGISTER_WRITE
    switch (spec->form) {
    case A64_FORM_RR: REG(WRITE); REG(READ); break;
    case A64_FORM_RRR: REG(WRITE); REG(READ); REG(READ); break;
    case A64_FORM_RRRR: REG(WRITE); REG(READ); REG(READ); REG(READ); break;
    case A64_FORM_RRRI:
        REG(WRITE); REG(READ); REG(READ);
        ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_RI: REG(WRITE); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_RII:
        REG(spec->updates_dest ? READ | WRITE : WRITE);
        ARG(MINIC_MACHINE_OPERAND_INTEGER);
        ARG(MINIC_MACHINE_OPERAND_INTEGER);
        break;
    case A64_FORM_RRI:
        REG(WRITE); REG(READ); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_CMP: REG(READ); REG(READ); break;
    case A64_FORM_CMP_IMM: REG(READ); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_CSET: REG(WRITE); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_MEMORY:
    case A64_FORM_ORDERED_MEMORY:
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case A64_FORM_MEMORY_POST_BYTE:
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        REG(READ | WRITE); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case A64_FORM_FRAME:
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        ARG(MINIC_MACHINE_OPERAND_FRAME); break;
    case A64_FORM_PAIR:
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case A64_FORM_ADRP: REG(WRITE); ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case A64_FORM_ADD_LO12:
        REG(WRITE); REG(READ); ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case A64_FORM_ADRP_BLOCK:
        REG(WRITE); ARG(MINIC_MACHINE_OPERAND_BLOCK_ADDRESS); break;
    case A64_FORM_ADD_LO12_BLOCK:
        REG(WRITE); REG(READ);
        ARG(MINIC_MACHINE_OPERAND_BLOCK_ADDRESS); break;
    case A64_FORM_GOT_LOAD:
        REG(WRITE); ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case A64_FORM_FP_RR:
    case A64_FORM_FP_CONVERT:
    case A64_FORM_FP_FROM_GP:
    case A64_FORM_GP_FROM_FP:
        REG(WRITE); REG(READ); break;
    case A64_FORM_FP_RRR:
    case A64_FORM_VEC_RRR:
        REG(WRITE); REG(READ); REG(READ); break;
    case A64_FORM_FP_CMP: REG(READ); REG(READ); break;
    case A64_FORM_FP_MEMORY:
        REG((spec->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) ? WRITE : READ);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case A64_FORM_BLOCK: ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case A64_FORM_COND_BLOCK:
        ARG(MINIC_MACHINE_OPERAND_INTEGER);
        ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case A64_FORM_REG_BLOCK: REG(READ); ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case A64_FORM_REG: REG(READ); break;
    case A64_FORM_CALL_SYMBOL: ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case A64_FORM_NONE: break;
    default: return false;
    }
#undef REG
#undef ARG
#undef READ
#undef WRITE
    contract->operand_count = n;
    contract->required_generated_features = MINIC_A64_MACHINE_FEATURE_BASE;
    if (spec->form == A64_FORM_FP_RR || spec->form == A64_FORM_FP_RRR ||
        spec->form == A64_FORM_FP_CMP || spec->form == A64_FORM_FP_CONVERT ||
        spec->form == A64_FORM_FP_FROM_GP || spec->form == A64_FORM_GP_FROM_FP ||
        spec->form == A64_FORM_FP_MEMORY || spec->form == A64_FORM_VEC_RRR)
        contract->required_generated_features |= MINIC_A64_MACHINE_FEATURE_FP_SIMD;
    contract->required_effects = spec->effects;
    contract->permitted_effects = spec->effects;
    if ((spec->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE)) != 0U)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_VOLATILE;
    /* Compiler GNU `memory` clobber on a sealed acquire/release instruction
     * is a semantic ordering property of that SAME selected hardware op. */
    if (spec->form == A64_FORM_ORDERED_MEMORY)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    if (spec->effects == MINIC_MACHINE_EFFECT_CALL)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_MEMORY_READ |
                                       MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    contract->terminates_block = spec->ends_block;
    contract->permits_fallthrough = spec->permits_fallthrough;
    contract->has_indirect_successors = spec->indirect_successors;
    if (spec->writes_flags || spec->reads_flags) {
        MinicMachineRegisterEffect *effect =
            &contract->implicit[contract->implicit_count++];
        effect->reg = a64_implicit(0U, MINIC_MACHINE_REGISTER_FLAGS, 4U);
        effect->access = spec->writes_flags ? MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_READ;
    }
    if (spec->writes_link) {
        unsigned int reg;
        /* AAPCS64 caller-saved GPRs; x18 is platform-defined and must not
         * be invented as a scratch register.  Argument banks explicitly
         * carry use+clobber, not just the call opcode side-effect bit. */
        for (reg = 0U; reg <= 17U; ++reg)
            a64_add_implicit(contract,
                a64_implicit(reg, MINIC_MACHINE_REGISTER_GENERAL, 64U),
                MINIC_MACHINE_REGISTER_CLOBBER |
                    (reg <= 8U ? MINIC_MACHINE_REGISTER_READ : 0U));
        for (reg = 0U; reg < 32U; ++reg)
            if (reg < 8U || reg >= 16U)
                a64_add_implicit(contract,
                    a64_implicit(reg, MINIC_MACHINE_REGISTER_VECTOR, 128U),
                    MINIC_MACHINE_REGISTER_CLOBBER |
                        (reg < 8U ? MINIC_MACHINE_REGISTER_READ : 0U));
        a64_add_implicit(contract,
            a64_implicit(30U, MINIC_MACHINE_REGISTER_GENERAL, 64U),
            MINIC_MACHINE_REGISTER_WRITE);
    } else if (spec->reads_link) {
        a64_add_implicit(contract,
            a64_implicit(30U, MINIC_MACHINE_REGISTER_GENERAL, 64U),
            MINIC_MACHINE_REGISTER_READ);
    }
    if (spec->form == A64_FORM_PAIR &&
        (opcode == MINIC_A64_MACHINE_STP_PRE_X ||
         opcode == MINIC_A64_MACHINE_LDP_POST_X)) {
        MinicMachineRegisterEffect *effect =
            &contract->implicit[contract->implicit_count++];
        effect->reg = a64_implicit(MINIC_A64_MACHINE_SP,
            MINIC_MACHINE_REGISTER_GENERAL, 64U);
        effect->access = MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    }
    return true;
}

static bool a64_reg_gp(const MinicMachineOperand *operand) {
    return operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        operand->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL;
}

static bool a64_reg_bank(const MinicMachineOperand *operand,
    MinicMachineRegisterBank bank) {
    return operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        operand->value.physical.reg.bank == bank;
}

static bool a64_same_width(const MinicMachineOperand *a,
                           const MinicMachineOperand *b) {
    return a->value.physical.reg.width_bits == b->value.physical.reg.width_bits;
}

static bool a64_frame_offset(const MinicMachineFunction *fn,
    const MinicMachineOperand *operand, unsigned int width,
    unsigned int *base, int64_t *offset) {
    const MinicMachineFrameObject *object;
    int64_t displacement;
    if (fn == NULL || operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        operand->value.frame.index >= fn->frame_object_count ||
        width == 0U) return false;
    object = &fn->frame_objects[operand->value.frame.index];
    displacement = operand->value.frame.displacement;
    if (!object->has_fixed_offset ||
        (displacement > 0 && object->fixed_offset > INT64_MAX - displacement) ||
        (displacement < 0 && object->fixed_offset < INT64_MIN - displacement))
        return false;
    *offset = object->fixed_offset + displacement;
    *base = object->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER
        ? MINIC_A64_MACHINE_SP
        : object->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER ? 19U : UINT_MAX;
    return *base != UINT_MAX && *offset >= 0 && *offset <= 4095 &&
        (*offset % (int64_t)width) == 0;
}

static bool a64_legal_selected(const MinicMachineFunction *function,
    const MinicMachineInstruction *ins, void *context) {
    const MinicTargetInfo *target = function != NULL ? function->target : NULL;
    const A64Spec *spec;
    const MinicMachineOperand *ops;
    size_t index;
    unsigned int frame_base;
    int64_t frame_offset;
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ins == NULL || (spec = a64_spec(ins->opcode)) == NULL ||
        ins->operand_count > 16U) return false;
    ops = ins->operands;
    if (spec->form != A64_FORM_FP_RR &&
        spec->form != A64_FORM_FP_RRR &&
        spec->form != A64_FORM_FP_CMP &&
        spec->form != A64_FORM_FP_CONVERT &&
        spec->form != A64_FORM_FP_FROM_GP &&
        spec->form != A64_FORM_GP_FROM_FP &&
        spec->form != A64_FORM_FP_MEMORY &&
        spec->form != A64_FORM_VEC_RRR)
        for (index = 0U; index < ins->operand_count; ++index)
            if (ops[index].kind == MINIC_MACHINE_OPERAND_REGISTER &&
                !a64_reg_gp(&ops[index])) return false;
    switch (spec->form) {
    case A64_FORM_RR:
        return ins->opcode == MINIC_A64_MACHINE_SXTW ||
               ins->opcode == MINIC_A64_MACHINE_UXTW
            ? ops[0].value.physical.reg.width_bits == 64U &&
              ops[1].value.physical.reg.width_bits == 32U
            : a64_same_width(&ops[0], &ops[1]);
    case A64_FORM_RRR:
    case A64_FORM_RRRR:
    case A64_FORM_RRRI:
    case A64_FORM_RRI:
        for (index = 1U; index < ins->operand_count; ++index)
            if (ops[index].kind == MINIC_MACHINE_OPERAND_REGISTER &&
                !a64_same_width(&ops[0], &ops[index]) &&
                !((ins->opcode == MINIC_A64_MACHINE_SMULL ||
                   ins->opcode == MINIC_A64_MACHINE_UMULL) &&
                  ops[0].value.physical.reg.width_bits == 64U &&
                  ops[index].value.physical.reg.width_bits == 32U)) return false;
        for (index = 0U; index < ins->operand_count; ++index)
            if (ops[index].kind == MINIC_MACHINE_OPERAND_REGISTER &&
                ops[index].value.physical.reg.number == MINIC_A64_MACHINE_SP &&
                !((ins->opcode == MINIC_A64_MACHINE_ADD ||
                   ins->opcode == MINIC_A64_MACHINE_SUB ||
                   ins->opcode == MINIC_A64_MACHINE_ADD_IMM ||
                   ins->opcode == MINIC_A64_MACHINE_SUB_IMM) &&
                  index <= 1U)) return false;
        if (ins->opcode == MINIC_A64_MACHINE_MSUB &&
            ops[0].value.physical.reg.number == MINIC_A64_MACHINE_SP) return false;
        if (spec->form == A64_FORM_RRRI)
            return ops[3].value.integer >= 0 &&
                ops[3].value.integer <
                    ops[0].value.physical.reg.width_bits;
        if (spec->form != A64_FORM_RRI) return true;
        if (ins->opcode == MINIC_A64_MACHINE_ADD_IMM ||
            ins->opcode == MINIC_A64_MACHINE_SUB_IMM ||
            ins->opcode == MINIC_A64_MACHINE_ADDS_IMM ||
            ins->opcode == MINIC_A64_MACHINE_SUBS_IMM)
            return ops[2].value.integer >= 0 && ops[2].value.integer <= 4095;
        if (ins->opcode == MINIC_A64_MACHINE_AND_ALIGN_IMM) {
            const int64_t imm = ops[2].value.integer;
            if (imm > -16 || imm < -256) return false;
            const uint64_t alignment = (uint64_t)(-imm);
            return (alignment & (alignment - 1U)) == 0U;
        }
        return ops[2].value.integer >= 0 &&
            ops[2].value.integer < ops[0].value.physical.reg.width_bits;
    case A64_FORM_RII:
        return ops[1].value.integer >= 0 && ops[1].value.integer <= 65535 &&
            ops[2].value.integer >= 0 &&
            (ops[2].value.integer % 16) == 0 &&
            ops[2].value.integer < ops[0].value.physical.reg.width_bits &&
            ops[0].value.physical.reg.number != MINIC_A64_MACHINE_SP;
    case A64_FORM_CMP:
        return a64_same_width(&ops[0], &ops[1]);
    case A64_FORM_CMP_IMM:
        return ops[1].value.integer >= 0 && ops[1].value.integer <= 4095;
    case A64_FORM_CSET:
        return ops[1].value.integer >= 0 && ops[1].value.integer <= 13;
    case A64_FORM_MEMORY: {
        const MinicMachineAddress *a = &ops[1].value.address;
        const bool load_signed = ins->opcode == MINIC_A64_MACHINE_LDRSB ||
                                 ins->opcode == MINIC_A64_MACHINE_LDRSH ||
                                 ins->opcode == MINIC_A64_MACHINE_LDRSW;
        const unsigned int bits = ops[0].value.physical.reg.width_bits;
        if ((a->target_mode != MINIC_A64_ADDRESS_UNSIGNED &&
             a->target_mode != MINIC_A64_ADDRESS_BASE_ONLY) ||
            a->has_symbol ||
            a->displacement < 0 ||
            a->displacement > (int64_t)(4095U * spec->memory_width) ||
            (a->displacement % (int64_t)spec->memory_width) != 0 ||
            ops[0].value.physical.reg.number == MINIC_A64_MACHINE_SP)
            return false;
        return bits == spec->memory_width * 8U ||
            ((spec->memory_width == 1U || spec->memory_width == 2U) &&
             bits == 32U) || (load_signed && bits == 64U);
    }
    case A64_FORM_ORDERED_MEMORY:
        return ops[1].value.address.target_mode == MINIC_A64_ADDRESS_UNSIGNED &&
            !ops[1].value.address.has_symbol &&
            ops[1].value.address.displacement == 0 &&
            ops[0].value.physical.reg.number != MINIC_A64_MACHINE_SP &&
            ops[0].value.physical.reg.width_bits ==
                (spec->memory_width == 8U ? 64U : 32U);
    case A64_FORM_MEMORY_POST_BYTE:
        return ops[0].value.physical.reg.width_bits == 32U &&
            ops[0].value.physical.reg.number < 31U &&
            ops[1].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.number <= MINIC_A64_MACHINE_SP &&
            ops[1].value.physical.reg.number != MINIC_A64_MACHINE_ZR &&
            ops[0].value.physical.reg.number !=
                ops[1].value.physical.reg.number &&
            ops[2].value.integer >= -256 && ops[2].value.integer <= 255;
    case A64_FORM_FRAME:
        /* The owner has to resolve an entry-SP-dependent reference into
         * explicit selected loads/address materializations first. */
        return ops[0].value.physical.reg.width_bits == spec->memory_width * 8U &&
            ops[0].value.physical.reg.number != MINIC_A64_MACHINE_SP &&
            a64_frame_offset(function, &ops[1], spec->memory_width,
                &frame_base, &frame_offset);
    case A64_FORM_PAIR: {
        const MinicMachineAddress *a = &ops[2].value.address;
        const bool offset_pair =
            ins->opcode == MINIC_A64_MACHINE_STP_OFFSET_X ||
            ins->opcode == MINIC_A64_MACHINE_LDP_OFFSET_X;
        return ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.width_bits == 64U &&
            a->has_base && a->base.number <= MINIC_A64_MACHINE_SP &&
            a->base.number != MINIC_A64_MACHINE_ZR &&
            (offset_pair || a->base.number == MINIC_A64_MACHINE_SP) &&
            a->base.width_bits == 64U && !a->has_symbol &&
            a->displacement >= -512 && a->displacement <= 504 &&
            (a->displacement % 8) == 0 &&
            a->target_mode == (offset_pair ? MINIC_A64_ADDRESS_PAIR_OFFSET :
                ins->opcode == MINIC_A64_MACHINE_STP_PRE_X
                    ? MINIC_A64_ADDRESS_PRE_INDEX : MINIC_A64_ADDRESS_POST_INDEX);
    }
    case A64_FORM_ADRP:
        return ops[0].value.physical.reg.width_bits == 64U &&
            (ops[1].value.symbol.relocation == MINIC_A64_RELOC_PAGE ||
             ops[1].value.symbol.relocation == MINIC_A64_RELOC_GOT_PAGE);
    case A64_FORM_ADRP_BLOCK:
        return ops[0].value.physical.reg.width_bits == 64U;
    case A64_FORM_ADD_LO12:
        return ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.width_bits == 64U &&
            ops[2].value.symbol.relocation == MINIC_A64_RELOC_LO12;
    case A64_FORM_ADD_LO12_BLOCK:
        return ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.width_bits == 64U;
    case A64_FORM_GOT_LOAD:
        return ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.address.target_mode == MINIC_A64_ADDRESS_GOT_LO12;
    case A64_FORM_FP_RR:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_same_width(&ops[0], &ops[1]);
    case A64_FORM_FP_RRR:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[2], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_same_width(&ops[0], &ops[1]) &&
            a64_same_width(&ops[0], &ops[2]);
    case A64_FORM_FP_CMP:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_same_width(&ops[0], &ops[1]);
    case A64_FORM_FP_CONVERT:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_FLOAT) &&
            !a64_same_width(&ops[0], &ops[1]);
    case A64_FORM_FP_FROM_GP:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_FLOAT) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_GENERAL) &&
            ops[1].value.physical.reg.number != MINIC_A64_MACHINE_SP &&
            (ins->opcode != MINIC_A64_MACHINE_FMOV_FP_FROM_GP ||
             a64_same_width(&ops[0], &ops[1]));
    case A64_FORM_GP_FROM_FP:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_GENERAL) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_FLOAT) &&
            ops[0].value.physical.reg.number != MINIC_A64_MACHINE_SP &&
            (ins->opcode != MINIC_A64_MACHINE_FMOV_GP_FROM_FP ||
             a64_same_width(&ops[0], &ops[1]));
    case A64_FORM_FP_MEMORY: {
        const MinicMachineAddress *a = &ops[1].value.address;
        const MinicMachineRegisterBank bank = spec->memory_width == 16U
            ? MINIC_MACHINE_REGISTER_VECTOR : MINIC_MACHINE_REGISTER_FLOAT;
        return a64_reg_bank(&ops[0], bank) &&
            ops[0].value.physical.reg.width_bits == spec->memory_width * 8U &&
            a->target_mode == MINIC_A64_ADDRESS_UNSIGNED && !a->has_symbol &&
            a->displacement >= 0 &&
            a->displacement <= (int64_t)(4095U * spec->memory_width) &&
            (a->displacement % (int64_t)spec->memory_width) == 0;
    }
    case A64_FORM_VEC_RRR:
        return a64_reg_bank(&ops[0], MINIC_MACHINE_REGISTER_VECTOR) &&
            a64_reg_bank(&ops[1], MINIC_MACHINE_REGISTER_VECTOR) &&
            a64_reg_bank(&ops[2], MINIC_MACHINE_REGISTER_VECTOR) &&
            a64_same_width(&ops[0], &ops[1]) &&
            a64_same_width(&ops[0], &ops[2]);
    case A64_FORM_COND_BLOCK:
        return ops[0].value.integer >= 0 && ops[0].value.integer <= 13;
    case A64_FORM_CALL_SYMBOL:
        return ops[0].value.symbol.relocation == MINIC_A64_RELOC_DIRECT;
    case A64_FORM_REG_BLOCK:
    case A64_FORM_REG:
        return ops[0].value.physical.reg.number != MINIC_A64_MACHINE_SP &&
            (ops[0].value.physical.reg.width_bits == 64U ||
             spec->form == A64_FORM_REG_BLOCK);
    case A64_FORM_BLOCK:
    case A64_FORM_NONE:
        return true;
    default:
        return false;
    }
}

static bool a64_gp_text(const MinicMachineRegister *reg,
                        char *buffer, size_t size) {
    const char letter = reg->width_bits == 32U ? 'w' : 'x';
    if (reg->bank != MINIC_MACHINE_REGISTER_GENERAL ||
        (reg->width_bits != 32U && reg->width_bits != 64U)) return false;
    if (reg->number == MINIC_A64_MACHINE_SP)
        return snprintf(buffer, size, "%s", letter == 'x' ? "sp" : "wsp") > 0;
    if (reg->number == MINIC_A64_MACHINE_ZR)
        return snprintf(buffer, size, "%s", letter == 'x' ? "xzr" : "wzr") > 0;
    return reg->number <= 30U &&
        snprintf(buffer, size, "%c%u", letter, reg->number) > 0;
}

static bool a64_fp_text(const MinicMachineRegister *reg,
                        char *buffer, size_t size) {
    if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT &&
        reg->number < 32U &&
        (reg->width_bits == 32U || reg->width_bits == 64U))
        return snprintf(buffer, size, "%c%u",
            reg->width_bits == 32U ? 's' : 'd', reg->number) > 0;
    if (reg->bank == MINIC_MACHINE_REGISTER_VECTOR &&
        reg->number < 32U && reg->width_bits == 128U)
        return snprintf(buffer, size, "q%u", reg->number) > 0;
    return false;
}

static const char *a64_condition(int64_t id) {
    static const char *const names[] = {"eq", "ne", "hs", "lo", "mi", "pl",
        "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};
    return id >= 0 && (size_t)id < sizeof(names) / sizeof(names[0])
        ? names[id] : NULL;
}

static bool a64_block_name(const MinicMachineOperand *op,
    const MinicMachineGNUAsmLabels *labels, const char **out) {
    if ((op->kind != MINIC_MACHINE_OPERAND_BLOCK &&
         op->kind != MINIC_MACHINE_OPERAND_BLOCK_ADDRESS) || labels == NULL ||
        op->value.block_id >= labels->count || labels->names == NULL ||
        labels->names[op->value.block_id] == NULL) return false;
    *out = labels->names[op->value.block_id];
    return true;
}

static bool a64_serialize(FILE *out, const MinicMachineFunction *function,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    const A64Spec *spec = a64_spec(ins->opcode);
    const MinicMachineOperand *o = ins->operands;
    char a[20], b[20], c[20], d[20], base[20];
    const char *label;
    unsigned int frame_base;
    int64_t frame_offset;
    (void)context;
    if (out == NULL || function == NULL || spec == NULL ||
        !a64_legal_selected(function, ins, NULL)) return false;
    switch (spec->form) {
    case A64_FORM_RR:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case A64_FORM_RRR:
    case A64_FORM_RRRR:
    case A64_FORM_RRRI:
        if (!a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) ||
            !a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) ||
            !a64_gp_text(&o[2].value.physical.reg, c, sizeof(c))) return false;
        if (spec->form == A64_FORM_RRR)
            return fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
        if (spec->form == A64_FORM_RRRI)
            return fprintf(out, "  %s %s, %s, %s, lsl #%" PRId64 "\n",
                spec->name, a, b, c, o[3].value.integer) >= 0;
        return a64_gp_text(&o[3].value.physical.reg, d, sizeof(d)) &&
            fprintf(out, "  %s %s, %s, %s, %s\n", spec->name, a, b, c, d) >= 0;
    case A64_FORM_RII:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  %s %s, #%" PRId64 ", lsl #%" PRId64 "\n",
                spec->name, a, o[1].value.integer, o[2].value.integer) >= 0;
    case A64_FORM_RRI:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, #%" PRId64 "\n",
                spec->name, a, b, o[2].value.integer) >= 0;
    case A64_FORM_CMP:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  cmp %s, %s\n", a, b) >= 0;
    case A64_FORM_CMP_IMM:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  cmp %s, #%" PRId64 "\n", a, o[1].value.integer) >= 0;
    case A64_FORM_CSET:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  cset %s, %s\n", a, a64_condition(o[1].value.integer)) >= 0;
    case A64_FORM_MEMORY:
    case A64_FORM_GOT_LOAD: {
        const MinicMachineAddress *addr = &o[1].value.address;
        if (!a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) ||
            !a64_gp_text(&addr->base, base, sizeof(base))) return false;
        if (addr->has_symbol)
            return fprintf(out, "  %s %s, [%s, :got_lo12:%s]\n",
                spec->name, a, base, addr->symbol) >= 0;
        /* Keep the no-offset spelling a target-verified physical address
         * mode. Other zero-displacement accesses retain their established
         * base+zero golden output. */
        if (addr->target_mode == MINIC_A64_ADDRESS_BASE_ONLY)
            return fprintf(out, "  %s %s, [%s]\n",
                spec->name, a, base) >= 0;
        return fprintf(out, "  %s %s, [%s, #%" PRId64 "]\n",
            spec->name, a, base, addr->displacement) >= 0;
    }
    case A64_FORM_ORDERED_MEMORY: {
        const MinicMachineAddress *addr = &o[1].value.address;
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&addr->base, base, sizeof(base)) &&
            fprintf(out, "  %s %s, [%s]\n", spec->name, a, base) >= 0;
    }
    case A64_FORM_MEMORY_POST_BYTE:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, base, sizeof(base)) &&
            fprintf(out, "  %s %s, [%s], #%" PRId64 "\n",
                spec->name, a, base, o[2].value.integer) >= 0;
    case A64_FORM_FP_RR:
    case A64_FORM_FP_CONVERT:
    case A64_FORM_FP_CMP:
        return a64_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case A64_FORM_FP_RRR:
        return a64_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            a64_fp_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case A64_FORM_GP_FROM_FP:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case A64_FORM_FP_FROM_GP:
        return a64_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case A64_FORM_FP_MEMORY: {
        const MinicMachineAddress *addr = &o[1].value.address;
        return a64_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&addr->base, base, sizeof(base)) &&
            fprintf(out, "  %s %s, [%s, #%" PRId64 "]\n",
                spec->name, a, base, addr->displacement) >= 0;
    }
    case A64_FORM_VEC_RRR:
        return snprintf(a, sizeof(a), "v%u.16b", o[0].value.physical.reg.number) > 0 &&
            snprintf(b, sizeof(b), "v%u.16b", o[1].value.physical.reg.number) > 0 &&
            snprintf(c, sizeof(c), "v%u.16b", o[2].value.physical.reg.number) > 0 &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case A64_FORM_FRAME:
        if (!a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) ||
            !a64_frame_offset(function, &o[1], spec->memory_width,
                &frame_base, &frame_offset)) return false;
        return fprintf(out, "  %s %s, [%s, #%" PRId64 "]\n",
            spec->name, a, frame_base == MINIC_A64_MACHINE_SP ? "sp" : "x19",
            frame_offset) >= 0;
    case A64_FORM_PAIR:
        if (!a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) ||
            !a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) ||
            !a64_gp_text(&o[2].value.address.base, base, sizeof(base)))
            return false;
        if (ins->opcode == MINIC_A64_MACHINE_STP_PRE_X)
            return fprintf(out, "  stp %s, %s, [%s, #%" PRId64 "]!\n",
                a, b, base, o[2].value.address.displacement) >= 0;
        if (ins->opcode == MINIC_A64_MACHINE_LDP_POST_X)
            return fprintf(out, "  ldp %s, %s, [%s], #%" PRId64 "\n",
                a, b, base, o[2].value.address.displacement) >= 0;
        return fprintf(out, "  %s %s, %s, [%s, #%" PRId64 "]\n",
            spec->name, a, b, base, o[2].value.address.displacement) >= 0;
    case A64_FORM_ADRP:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  adrp %s, %s%s\n", a,
                o[1].value.symbol.relocation == MINIC_A64_RELOC_GOT_PAGE
                    ? ":got:" : "", o[1].value.symbol.name) >= 0;
    case A64_FORM_ADRP_BLOCK:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_block_name(&o[1], labels, &label) &&
            fprintf(out, "  adrp %s, %s\n", a, label) >= 0;
    case A64_FORM_ADD_LO12:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  add %s, %s, :lo12:%s\n", a, b,
                o[2].value.symbol.name) >= 0;
    case A64_FORM_ADD_LO12_BLOCK:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            a64_block_name(&o[2], labels, &label) &&
            fprintf(out, "  add %s, %s, :lo12:%s\n", a, b, label) >= 0;
    case A64_FORM_BLOCK:
        return a64_block_name(&o[0], labels, &label) &&
            fprintf(out, "  b %s\n", label) >= 0;
    case A64_FORM_COND_BLOCK:
        return a64_block_name(&o[1], labels, &label) &&
            fprintf(out, "  b.%s %s\n", a64_condition(o[0].value.integer),
                label) >= 0;
    case A64_FORM_REG_BLOCK:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            a64_block_name(&o[1], labels, &label) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, label) >= 0;
    case A64_FORM_REG:
        return a64_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  %s %s\n", spec->name, a) >= 0;
    case A64_FORM_CALL_SYMBOL:
        return fprintf(out, "  bl %s\n", o[0].value.symbol.name) >= 0;
    case A64_FORM_NONE:
        return fprintf(out, ins->opcode == MINIC_A64_MACHINE_BRK
            ? "  brk #0\n" : "  %s\n", spec->name) >= 0;
    case A64_FORM_RI:
    default:
        return false;
    }
}

static bool a64_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention calling_convention, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_AARCH64 &&
        calling_convention == MINIC_CALLING_CONVENTION_DEFAULT;
}

static bool a64_frame_object(const MinicTargetInfo *target,
    const MinicMachineFunction *function,
    const MinicMachineFrameObject *frame, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        function == NULL || frame == NULL || function->frame_alignment < 16U ||
        frame->base == MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER) return false;
    if (frame->area == MINIC_MACHINE_FRAME_OUTGOING)
        return frame->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER;
    if (function->has_dynamic_stack && frame->area == MINIC_MACHINE_FRAME_LOCAL)
        return frame->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER;
    if (!function->has_dynamic_stack &&
        frame->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER)
        return false;
    return frame->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER ||
        frame->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER;
}

/* A selected opcode cannot declare arbitrary Linux ASM provenance. The
 * target-owned proof binds the sealed source form to the exact physical
 * opcode, width and compulsory memory/volatility effects. The selected
 * output store for RBIT also requires an actual predecessor RBIT with the
 * identical Core origin: a fabricated STR alone cannot claim that form. */
static bool a64_selected_source_asm(const MinicMachineFunction *function,
    const MinicMachineInstruction *ins, void *context) {
    const uint32_t id = ins->source_asm_machine_form_id;
    const uint32_t variant = ins->source_asm_machine_form_variant;
    const uint32_t op = ins->opcode;
    const unsigned int required = MINIC_MACHINE_EFFECT_VOLATILE;
    size_t i, j;
    (void)context;
    if (function == NULL || function->target == NULL ||
        function->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        !ins->has_selected_source_asm_seal ||
        ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        (ins->effects & required) != required) return false;
    if (id == AARCH64_MACHINE_ASM_LDAR || id == AARCH64_MACHINE_ASM_STLR) {
        const unsigned int effects = required |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
            (id == AARCH64_MACHINE_ASM_LDAR ?
                MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE);
        const uint32_t opcode = variant == 1U ?
                (id == AARCH64_MACHINE_ASM_LDAR ?
                    MINIC_A64_MACHINE_LDARB : MINIC_A64_MACHINE_STLRB) :
            variant == 2U ?
                (id == AARCH64_MACHINE_ASM_LDAR ?
                    MINIC_A64_MACHINE_LDARH : MINIC_A64_MACHINE_STLRH) :
            variant == 4U ?
                (id == AARCH64_MACHINE_ASM_LDAR ?
                    MINIC_A64_MACHINE_LDAR_W : MINIC_A64_MACHINE_STLR_W) :
            variant == 8U ?
                (id == AARCH64_MACHINE_ASM_LDAR ?
                    MINIC_A64_MACHINE_LDAR_X : MINIC_A64_MACHINE_STLR_X) : 0U;
        return opcode != 0U && op == opcode && ins->effects == effects;
    }
    if (id != AARCH64_MACHINE_ASM_BIT_REVERSE || variant != 0U ||
        op != MINIC_A64_MACHINE_STR_W ||
        ins->effects != (required | MINIC_MACHINE_EFFECT_MEMORY_WRITE))
        return false;
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *candidate =
                &block->instructions[j];
            size_t k;
            if (candidate != ins) continue;
            for (k = 0U; k < j; ++k) {
                const MinicMachineInstruction *before =
                    &block->instructions[k];
                if (before->kind == MINIC_MACHINE_INSTRUCTION_SELECTED &&
                    before->opcode == MINIC_A64_MACHINE_RBIT &&
                    before->has_core_origin && ins->has_core_origin &&
                    before->core_instruction_id == ins->core_instruction_id &&
                    before->core_block_id == ins->core_block_id)
                    return true;
            }
            return false;
        }
    }
    return false;
}

bool minic_aarch64_machine_opcode_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *destination) {
    static const MinicMachineGNUAsmPolicy policy = {
        .verify = {
            .lookup_opcode = a64_lookup,
            .validate_generated_features = a64_features,
            .validate_register = a64_register,
            .validate_address = a64_address,
            .validate_relocation = a64_relocation,
            /* No source-ASM bypass until the complete sealed Core ASM
             * admission/constraint translator is integrated. */
            .validate_source_asm = NULL,
            .validate_function_abi = a64_abi,
            .validate_frame_object = a64_frame_object,
            .validate_selected_instruction = a64_legal_selected,
            .validate_selected_source_asm = a64_selected_source_asm
        },
        .emit_selected = a64_serialize,
        .emit_source_asm = NULL,
        .context = NULL
    };
    if (destination == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_AARCH64) return false;
    *destination = policy;
    return true;
}
