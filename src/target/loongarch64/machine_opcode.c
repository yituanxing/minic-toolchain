#include "target/loongarch64/machine_opcode.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef enum LoongForm {
    L_FORM_RR, L_FORM_RI, L_FORM_RRR, L_FORM_RRI,
    L_FORM_MEMORY, L_FORM_FRAME, L_FORM_BLOCK, L_FORM_RBLOCK,
    L_FORM_RRBLOCK, L_FORM_SYMBOL, L_FORM_RSYMBOL,
    L_FORM_JIRL, L_FORM_NONE
} LoongForm;

typedef struct LoongSpec {
    const char *name;
    LoongForm form;
    unsigned int effects;
    unsigned int access_width;
    bool ends_block;
    bool permits_fallthrough;
    bool has_indirect_successors;
    bool writes_ra;
    bool reads_ra;
} LoongSpec;

#define RR(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RR, 0}
#define RRR(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RRR, 0}
#define RRI(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RRI, 0}
#define RI(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RI, 0}
#define LOAD(op, name, width) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_MEMORY, MINIC_MACHINE_EFFECT_MEMORY_READ, width}
#define STORE(op, name, width) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_MEMORY, MINIC_MACHINE_EFFECT_MEMORY_WRITE, width}
#define FRAME_LOAD(op, name, width) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_FRAME, MINIC_MACHINE_EFFECT_MEMORY_READ, width}
#define FRAME_STORE(op, name, width) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_FRAME, MINIC_MACHINE_EFFECT_MEMORY_WRITE, width}
#define RBLOCK(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RBLOCK, MINIC_MACHINE_EFFECT_BRANCH, .ends_block = true, .permits_fallthrough = true}
#define RRBLOCK(op, name) [MINIC_LOONG_MACHINE_##op] = {name, L_FORM_RRBLOCK, MINIC_MACHINE_EFFECT_BRANCH, .ends_block = true, .permits_fallthrough = true}

static const LoongSpec loong_specs[MINIC_LOONG_MACHINE_NOP + 1U] = {
    RR(MOVE, "move"), RI(LI_D, "li.d"),
    RRR(ADD_D, "add.d"), RRR(ADD_W, "add.w"),
    RRR(SUB_D, "sub.d"), RRR(SUB_W, "sub.w"),
    RRR(AND, "and"), RRR(OR, "or"), RRR(XOR, "xor"),
    RRR(NOR, "nor"), RRR(SLT, "slt"), RRR(SLTU, "sltu"),
    RRR(MUL_D, "mul.d"), RRR(MUL_W, "mul.w"),
    RRR(MULH_D, "mulh.d"), RRR(MULH_DU, "mulh.du"),
    RRR(DIV_D, "div.d"), RRR(DIV_DU, "div.du"),
    RRR(DIV_W, "div.w"), RRR(DIV_WU, "div.wu"),
    RRR(MOD_D, "mod.d"), RRR(MOD_DU, "mod.du"),
    RRR(MOD_W, "mod.w"), RRR(MOD_WU, "mod.wu"),
    RRR(SLL_D, "sll.d"), RRR(SRL_D, "srl.d"), RRR(SRA_D, "sra.d"),
    RRR(SLL_W, "sll.w"), RRR(SRL_W, "srl.w"), RRR(SRA_W, "sra.w"),
    RRI(ADDI_D, "addi.d"), RRI(ADDI_W, "addi.w"),
    RRI(ANDI, "andi"), RRI(ORI, "ori"), RRI(XORI, "xori"),
    RRI(SLTI, "slti"), RRI(SLTUI, "sltui"),
    RRI(SLLI_D, "slli.d"), RRI(SRLI_D, "srli.d"),
    RRI(SRAI_D, "srai.d"), RRI(SLLI_W, "slli.w"),
    RRI(SRLI_W, "srli.w"), RRI(SRAI_W, "srai.w"),
    RI(LU12I_W, "lu12i.w"), RI(LU32I_D, "lu32i.d"),
    RRI(LU52I_D, "lu52i.d"),
    LOAD(LD_B, "ld.b", 1), LOAD(LD_BU, "ld.bu", 1),
    LOAD(LD_H, "ld.h", 2), LOAD(LD_HU, "ld.hu", 2),
    LOAD(LD_W, "ld.w", 4), LOAD(LD_WU, "ld.wu", 4),
    LOAD(LD_D, "ld.d", 8),
    STORE(ST_B, "st.b", 1), STORE(ST_H, "st.h", 2),
    STORE(ST_W, "st.w", 4), STORE(ST_D, "st.d", 8),
    LOAD(LDPTR_W, "ldptr.w", 4), LOAD(LDPTR_D, "ldptr.d", 8),
    STORE(STPTR_W, "stptr.w", 4), STORE(STPTR_D, "stptr.d", 8),
    FRAME_LOAD(LD_FRAME_W, "ld.w", 4),
    FRAME_LOAD(LD_FRAME_D, "ld.d", 8),
    FRAME_STORE(ST_FRAME_W, "st.w", 4),
    FRAME_STORE(ST_FRAME_D, "st.d", 8),
    RI(PCADDI, "pcaddi"),
    [MINIC_LOONG_MACHINE_LA_GLOBAL] = {"la.global", L_FORM_RSYMBOL, 0},
    [MINIC_LOONG_MACHINE_B] = {"b", L_FORM_BLOCK,
        MINIC_MACHINE_EFFECT_BRANCH, .ends_block = true},
    RBLOCK(BEQZ, "beqz"), RBLOCK(BNEZ, "bnez"),
    RRBLOCK(BEQ, "beq"), RRBLOCK(BNE, "bne"),
    RRBLOCK(BLT, "blt"), RRBLOCK(BLTU, "bltu"),
    RRBLOCK(BGE, "bge"), RRBLOCK(BGEU, "bgeu"),
    [MINIC_LOONG_MACHINE_BL] = {"bl", L_FORM_SYMBOL,
        MINIC_MACHINE_EFFECT_CALL, .writes_ra = true},
    [MINIC_LOONG_MACHINE_JIRL_CALL] = {"jirl", L_FORM_JIRL,
        MINIC_MACHINE_EFFECT_CALL, .writes_ra = true},
    [MINIC_LOONG_MACHINE_JIRL_BRANCH] = {"jirl", L_FORM_JIRL,
        MINIC_MACHINE_EFFECT_BRANCH, .ends_block = true,
        .has_indirect_successors = true},
    [MINIC_LOONG_MACHINE_RET] = {"ret", L_FORM_NONE,
        MINIC_MACHINE_EFFECT_RETURN, .reads_ra = true,
        .ends_block = true},
    [MINIC_LOONG_MACHINE_BREAK] = {"break", L_FORM_NONE,
        MINIC_MACHINE_EFFECT_BRANCH, .ends_block = true},
    [MINIC_LOONG_MACHINE_NOP] = {"nop", L_FORM_NONE, 0}
};

#undef RR
#undef RRR
#undef RRI
#undef RI
#undef LOAD
#undef STORE
#undef FRAME_LOAD
#undef FRAME_STORE
#undef RBLOCK
#undef RRBLOCK

static const LoongSpec *loong_spec(uint32_t opcode) {
    if (opcode == 0U || opcode >= sizeof(loong_specs) / sizeof(loong_specs[0]) ||
        loong_specs[opcode].name == NULL) return NULL;
    return &loong_specs[opcode];
}

static bool loong_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention calling_convention, void *context) {
    (void)context;
    return target != NULL &&
        target->architecture == MINIC_TARGET_ARCH_LOONGARCH64 &&
        calling_convention == MINIC_CALLING_CONVENTION_DEFAULT;
}

static bool loong_frame_object(const MinicTargetInfo *target,
    const MinicMachineFunction *function,
    const MinicMachineFrameObject *frame, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
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

static bool loong_features(const MinicTargetInfo *target,
    uint64_t features, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_LOONGARCH64 &&
        features == (target->loongarch64_soft_float_abi
            ? MINIC_LOONG_MACHINE_FEATURE_BASE
            : (MINIC_LOONG_MACHINE_FEATURE_BASE | MINIC_LOONG_MACHINE_FEATURE_FP));
}

static bool loong_register(const MinicTargetInfo *target,
    const MinicMachineRegister *reg, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        reg == NULL) return false;
    switch (reg->bank) {
    case MINIC_MACHINE_REGISTER_GENERAL:
        return reg->number <= 31U &&
            (reg->width_bits == 32U || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_FLOAT:
        return !target->loongarch64_soft_float_abi && reg->number <= 31U &&
            (reg->width_bits == 32U || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_FLAGS:
        return !target->loongarch64_soft_float_abi && reg->number < 8U &&
            reg->width_bits == 1U;
    default:
        return false;
    }
}

static bool loong_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t reloc, int64_t addend, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_LOONGARCH64 &&
        symbol != NULL && symbol[0] != '\0' && addend == 0 &&
        reloc >= MINIC_LOONG_RELOC_DIRECT && reloc <= MINIC_LOONG_RELOC_PC_LO12;
}

static bool loong_address(const MinicTargetInfo *target,
    const MinicMachineAddress *address, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        address == NULL || !address->has_base || address->has_index ||
        address->has_segment || address->has_symbol ||
        (address->scale != 0U && address->scale != 1U) ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.width_bits != 64U || address->base.number > 31U) return false;
    if (address->target_mode == MINIC_LOONG_ADDRESS_IMM12)
        return address->displacement >= -2048 && address->displacement <= 2047;
    if (address->target_mode == MINIC_LOONG_ADDRESS_PTR14)
        return address->displacement >= -32768 &&
            address->displacement <= 32764 &&
            (address->displacement & 3) == 0;
    return false;
}

static bool loong_lookup(const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    const LoongSpec *spec = loong_spec(opcode);
    size_t n = 0U;
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        contract == NULL || spec == NULL) return false;
    memset(contract, 0, sizeof(*contract));
#define REG(access) do { contract->operand_kinds[n] = MINIC_MACHINE_OPERAND_REGISTER; \
    contract->register_access[n++] = (access); } while (0)
#define ARG(kind) do { contract->operand_kinds[n++] = (kind); } while (0)
#define RD MINIC_MACHINE_REGISTER_READ
#define WR MINIC_MACHINE_REGISTER_WRITE
    switch (spec->form) {
    case L_FORM_RR: REG(WR); REG(RD); break;
    case L_FORM_RI: REG(WR); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_RRR: REG(WR); REG(RD); REG(RD); break;
    case L_FORM_RRI: REG(WR); REG(RD); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_MEMORY:
        REG(spec->effects == MINIC_MACHINE_EFFECT_MEMORY_READ ? WR : RD);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case L_FORM_FRAME:
        REG(spec->effects == MINIC_MACHINE_EFFECT_MEMORY_READ ? WR : RD);
        ARG(MINIC_MACHINE_OPERAND_FRAME); break;
    case L_FORM_BLOCK: ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case L_FORM_RBLOCK: REG(RD); ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case L_FORM_RRBLOCK:
        REG(RD); REG(RD); ARG(MINIC_MACHINE_OPERAND_BLOCK); break;
    case L_FORM_SYMBOL: ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case L_FORM_RSYMBOL: REG(WR); ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case L_FORM_JIRL:
        REG(RD); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_NONE: break;
    default: return false;
    }
#undef REG
#undef ARG
#undef RD
#undef WR
    contract->operand_count = n;
    contract->required_generated_features = MINIC_LOONG_MACHINE_FEATURE_BASE;
    contract->required_effects = spec->effects;
    contract->permitted_effects = spec->effects;
    if ((spec->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE)) != 0U)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_VOLATILE;
    if (spec->effects == MINIC_MACHINE_EFFECT_CALL)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_MEMORY_READ |
                                       MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    contract->terminates_block = spec->ends_block;
    contract->permits_fallthrough = spec->permits_fallthrough;
    contract->has_indirect_successors = spec->has_indirect_successors;
    if (spec->writes_ra) {
        unsigned int reg;
        for (reg = 4U; reg <= 20U; ++reg) {
            MinicMachineRegisterEffect *effect =
                &contract->implicit[contract->implicit_count++];
            effect->reg = (MinicMachineRegister){reg, 64U,
                MINIC_MACHINE_REGISTER_GENERAL};
            effect->access = MINIC_MACHINE_REGISTER_CLOBBER |
                (reg <= 11U ? MINIC_MACHINE_REGISTER_READ : 0U);
        }
        if (!target->loongarch64_soft_float_abi)
            for (reg = 0U; reg <= 23U; ++reg) {
                MinicMachineRegisterEffect *effect =
                    &contract->implicit[contract->implicit_count++];
                effect->reg = (MinicMachineRegister){reg, 64U,
                    MINIC_MACHINE_REGISTER_FLOAT};
                effect->access = MINIC_MACHINE_REGISTER_CLOBBER |
                    (reg < 8U ? MINIC_MACHINE_REGISTER_READ : 0U);
            }
        contract->implicit[contract->implicit_count++] =
            (MinicMachineRegisterEffect){
                {1U, 64U, MINIC_MACHINE_REGISTER_GENERAL},
                MINIC_MACHINE_REGISTER_WRITE};
    } else if (spec->reads_ra) {
        contract->implicit[contract->implicit_count++] =
            (MinicMachineRegisterEffect){
                {1U, 64U, MINIC_MACHINE_REGISTER_GENERAL},
                MINIC_MACHINE_REGISTER_READ};
    }
    return true;
}

static bool loong_frame_offset(const MinicMachineFunction *fn,
    const MinicMachineOperand *operand,
    unsigned int *base, int64_t *offset) {
    const MinicMachineFrameObject *object;
    int64_t delta;
    if (fn == NULL || operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        operand->value.frame.index >= fn->frame_object_count) return false;
    object = &fn->frame_objects[operand->value.frame.index];
    delta = operand->value.frame.displacement;
    if (!object->has_fixed_offset ||
        (delta > 0 && object->fixed_offset > INT64_MAX - delta) ||
        (delta < 0 && object->fixed_offset < INT64_MIN - delta)) return false;
    *offset = object->fixed_offset + delta;
    *base = object->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER
        ? 3U : object->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER
            ? 23U : UINT_MAX;
    return *base != UINT_MAX && *offset >= -2048 && *offset <= 2047;
}

static bool loong_legal_selected(const MinicMachineFunction *function,
    const MinicMachineInstruction *ins, void *context) {
    const MinicTargetInfo *target = function != NULL ? function->target : NULL;
    const LoongSpec *spec;
    const MinicMachineOperand *ops;
    size_t i;
    int64_t imm;
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        ins == NULL || (spec = loong_spec(ins->opcode)) == NULL) return false;
    ops = ins->operands;
    for (i = 0U; i < ins->operand_count; ++i)
        if (ops[i].kind == MINIC_MACHINE_OPERAND_REGISTER &&
            (ops[i].value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
             ops[i].value.physical.reg.width_bits != 64U)) return false;
    switch (spec->form) {
    case L_FORM_RRI:
        imm = ops[2].value.integer;
        if (ins->opcode == MINIC_LOONG_MACHINE_ANDI ||
            ins->opcode == MINIC_LOONG_MACHINE_ORI ||
            ins->opcode == MINIC_LOONG_MACHINE_XORI)
            return imm >= 0 && imm <= 4095;
        if (ins->opcode == MINIC_LOONG_MACHINE_SLLI_D ||
            ins->opcode == MINIC_LOONG_MACHINE_SRLI_D ||
            ins->opcode == MINIC_LOONG_MACHINE_SRAI_D)
            return imm >= 0 && imm <= 63;
        if (ins->opcode == MINIC_LOONG_MACHINE_SLLI_W ||
            ins->opcode == MINIC_LOONG_MACHINE_SRLI_W ||
            ins->opcode == MINIC_LOONG_MACHINE_SRAI_W)
            return imm >= 0 && imm <= 31;
        if (ins->opcode == MINIC_LOONG_MACHINE_LU52I_D)
            return imm >= 0 && imm <= 4095;
        return imm >= -2048 && imm <= 2047;
    case L_FORM_RI:
        imm = ops[1].value.integer;
        if (ins->opcode == MINIC_LOONG_MACHINE_LU12I_W ||
            ins->opcode == MINIC_LOONG_MACHINE_LU32I_D)
            return imm >= -(INT64_C(1) << 19) && imm < (INT64_C(1) << 19);
        if (ins->opcode == MINIC_LOONG_MACHINE_PCADDI)
            return imm >= -(INT64_C(1) << 19) && imm < (INT64_C(1) << 19);
        return true;
    case L_FORM_MEMORY:
        return ops[1].value.address.target_mode ==
            ((ins->opcode >= MINIC_LOONG_MACHINE_LDPTR_W &&
              ins->opcode <= MINIC_LOONG_MACHINE_STPTR_D)
                ? MINIC_LOONG_ADDRESS_PTR14 : MINIC_LOONG_ADDRESS_IMM12) &&
            (spec->access_width <= 4U ||
             (ops[1].value.address.displacement % 8) == 0 ||
             ops[1].value.address.target_mode == MINIC_LOONG_ADDRESS_IMM12);
    case L_FORM_FRAME: {
        unsigned int frame_base;
        int64_t frame_offset;
        return loong_frame_offset(function, &ops[1],
            &frame_base, &frame_offset);
    }
    case L_FORM_SYMBOL:
        return ops[0].value.symbol.relocation == MINIC_LOONG_RELOC_DIRECT;
    case L_FORM_RSYMBOL:
        return ops[1].value.symbol.relocation == MINIC_LOONG_RELOC_GLOBAL;
    case L_FORM_JIRL:
        return ops[1].value.integer >= -32768 &&
            ops[1].value.integer <= 32764 &&
            (ops[1].value.integer % 4) == 0;
    case L_FORM_RR:
    case L_FORM_RRR:
    case L_FORM_RBLOCK:
    case L_FORM_RRBLOCK:
    case L_FORM_BLOCK:
    case L_FORM_NONE:
        return true;
    default:
        return false;
    }
}

/* Hardware numbers: $zero=r0, $ra=r1, $tp=r2, $sp=r3,
 * $a0..$a7=r4..r11, $t0..$t8=r12..r20, $s0..$s8=r23..r31. */
static bool loong_gp_text(const MinicMachineRegister *reg,
    char *buffer, size_t capacity) {
    if (reg == NULL || reg->bank != MINIC_MACHINE_REGISTER_GENERAL ||
        reg->number >= 32U || reg->width_bits != 64U) return false;
    if (reg->number == 0U) return snprintf(buffer, capacity, "$zero") > 0;
    if (reg->number == 1U) return snprintf(buffer, capacity, "$ra") > 0;
    if (reg->number == 2U) return snprintf(buffer, capacity, "$tp") > 0;
    if (reg->number == 3U) return snprintf(buffer, capacity, "$sp") > 0;
    if (reg->number <= 11U)
        return snprintf(buffer, capacity, "$a%u", reg->number - 4U) > 0;
    if (reg->number <= 20U)
        return snprintf(buffer, capacity, "$t%u", reg->number - 12U) > 0;
    if (reg->number == 21U) return snprintf(buffer, capacity, "$r21") > 0;
    if (reg->number == 22U) return snprintf(buffer, capacity, "$fp") > 0;
    return snprintf(buffer, capacity, "$s%u", reg->number - 23U) > 0;
}

static bool loong_block_name(const MinicMachineOperand *operand,
    const MinicMachineGNUAsmLabels *labels, const char **name) {
    if (operand->kind != MINIC_MACHINE_OPERAND_BLOCK || labels == NULL ||
        operand->value.block_id >= labels->count || labels->names == NULL ||
        labels->names[operand->value.block_id] == NULL) return false;
    *name = labels->names[operand->value.block_id];
    return true;
}

static bool loong_serialize(FILE *out, const MinicMachineFunction *function,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    const LoongSpec *spec = loong_spec(ins->opcode);
    const MinicMachineOperand *o = ins->operands;
    char a[20], b[20], c[20];
    unsigned int base_id;
    int64_t offset;
    const char *label;
    (void)context;
    if (out == NULL || function == NULL || spec == NULL ||
        !loong_legal_selected(function, ins, NULL)) return false;
    switch (spec->form) {
    case L_FORM_RR:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_RI:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  %s %s, %" PRId64 "\n",
                spec->name, a, o[1].value.integer) >= 0;
    case L_FORM_RRR:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_gp_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case L_FORM_RRI:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n",
                spec->name, a, b, o[2].value.integer) >= 0;
    case L_FORM_MEMORY:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.address.base, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n", spec->name,
                a, b, o[1].value.address.displacement) >= 0;
    case L_FORM_FRAME:
        if (!loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) ||
            !loong_frame_offset(function, &o[1], &base_id, &offset)) return false;
        return fprintf(out, "  %s %s, %s, %" PRId64 "\n", spec->name,
            a, base_id == 3U ? "$sp" : "$s0", offset) >= 0;
    case L_FORM_BLOCK:
        return loong_block_name(&o[0], labels, &label) &&
            fprintf(out, "  b %s\n", label) >= 0;
    case L_FORM_RBLOCK:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_block_name(&o[1], labels, &label) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, label) >= 0;
    case L_FORM_RRBLOCK:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_block_name(&o[2], labels, &label) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, label) >= 0;
    case L_FORM_SYMBOL:
        return fprintf(out, "  bl %s\n", o[0].value.symbol.name) >= 0;
    case L_FORM_RSYMBOL:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  la.global %s, %s\n", a,
                o[1].value.symbol.name) >= 0;
    case L_FORM_JIRL:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  jirl %s, %s, %" PRId64 "\n",
                ins->opcode == MINIC_LOONG_MACHINE_JIRL_CALL ? "$ra" : "$zero",
                a, o[1].value.integer) >= 0;
    case L_FORM_NONE:
        if (ins->opcode == MINIC_LOONG_MACHINE_BREAK)
            return fprintf(out, "  break 0\n") >= 0;
        return fprintf(out, "  %s\n", spec->name) >= 0;
    default:
        return false;
    }
}

bool minic_loongarch64_machine_opcode_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *destination) {
    static const MinicMachineGNUAsmPolicy policy = {
        .verify = {
            .lookup_opcode = loong_lookup,
            .validate_generated_features = loong_features,
            .validate_register = loong_register,
            .validate_address = loong_address,
            .validate_relocation = loong_relocation,
            .validate_source_asm = NULL,
            .validate_function_abi = loong_abi,
            .validate_frame_object = loong_frame_object,
            .validate_selected_instruction = loong_legal_selected
        },
        .emit_selected = loong_serialize,
        .emit_source_asm = NULL,
        .context = NULL
    };
    if (target == NULL || destination == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64) return false;
    *destination = policy;
    return true;
}
