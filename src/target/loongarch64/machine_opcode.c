#include "target/loongarch64/machine_opcode.h"
#include "target/loongarch64/machine_target_state.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/simd_asm.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef enum LoongForm {
    L_FORM_RR, L_FORM_RI, L_FORM_RRR, L_FORM_RRI,
    L_FORM_MEMORY, L_FORM_FRAME, L_FORM_BLOCK, L_FORM_RBLOCK,
    L_FORM_RRBLOCK, L_FORM_SYMBOL, L_FORM_RSYMBOL,
    L_FORM_RSYMBOL_LOCAL, L_FORM_RBLOCK_ADDRESS,
    L_FORM_JIRL, L_FORM_NONE,
    L_FORM_RRII,
    L_FORM_FP_RR, L_FORM_FP_RRR, L_FORM_FP_CMP,
    L_FORM_GP_FROM_FCC, L_FORM_GP_FROM_FP, L_FORM_FP_FROM_GP,
    L_FORM_FP_MEMORY,
    L_FORM_VEC_MEMORY, L_FORM_VEC_RR, L_FORM_VEC_RRR,
    L_FORM_VEC_RRRR, L_FORM_VEC_RRI,
    /* Privileged/native target builtin forms.  These preserve the sealed
     * call's $a0/$a1 physical convention without emitting a BL. */
    L_FORM_BUILTIN_RR_WR,
    L_FORM_BUILTIN_RR_RD,
    L_FORM_BUILTIN_RRR,
    L_FORM_BUILTIN_CSR_RD,
    L_FORM_BUILTIN_CSR_RW,
    L_FORM_BUILTIN_CSR_XCHG
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
#define BUILTIN(op, name, form) [MINIC_LOONG_MACHINE_##op] = {name, form, MINIC_MACHINE_EFFECT_CALL}

static const LoongSpec loong_specs[MINIC_LOONG_MACHINE_XVREPLVE0_Q + 1U] = {
    RR(MOVE, "move"), RI(LI_D, "li.d"),
    RI(LI_W, "li.w"),
    RR(CLZ_D, "clz.d"), RR(CLZ_W, "clz.w"),
    RR(CTZ_D, "ctz.d"), RR(CTZ_W, "ctz.w"),
    [MINIC_LOONG_MACHINE_BSTRPICK_D] = {"bstrpick.d", L_FORM_RRII, 0},
    [MINIC_LOONG_MACHINE_BSTRPICK_W] = {"bstrpick.w", L_FORM_RRII, 0},
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
    [MINIC_LOONG_MACHINE_LA_LOCAL] = {"la.local", L_FORM_RSYMBOL_LOCAL, 0},
    [MINIC_LOONG_MACHINE_LA_BLOCK_ADDRESS] = {
        "la.local", L_FORM_RBLOCK_ADDRESS, 0},
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
    [MINIC_LOONG_MACHINE_FADD_D] = {"fadd.d", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FSUB_D] = {"fsub.d", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FMUL_D] = {"fmul.d", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FDIV_D] = {"fdiv.d", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FNEG_D] = {"fneg.d", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FADD_S] = {"fadd.s", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FSUB_S] = {"fsub.s", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FMUL_S] = {"fmul.s", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FDIV_S] = {"fdiv.s", L_FORM_FP_RRR, 0},
    [MINIC_LOONG_MACHINE_FCMP_CEQ_D] = {"fcmp.ceq.d", L_FORM_FP_CMP, 0},
    [MINIC_LOONG_MACHINE_FCMP_CLT_D] = {"fcmp.clt.d", L_FORM_FP_CMP, 0},
    [MINIC_LOONG_MACHINE_FCMP_CLE_D] = {"fcmp.cle.d", L_FORM_FP_CMP, 0},
    [MINIC_LOONG_MACHINE_FCVT_D_S] = {"fcvt.d.s", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FCVT_S_D] = {"fcvt.s.d", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_MOVCF2GR] = {"movcf2gr", L_FORM_GP_FROM_FCC, 0},
    [MINIC_LOONG_MACHINE_MOVGR2FR_D] = {"movgr2fr.d", L_FORM_FP_FROM_GP, 0},
    [MINIC_LOONG_MACHINE_MOVGR2FR_W] = {"movgr2fr.w", L_FORM_FP_FROM_GP, 0},
    [MINIC_LOONG_MACHINE_MOVFR2GR_D] = {"movfr2gr.d", L_FORM_GP_FROM_FP, 0},
    [MINIC_LOONG_MACHINE_MOVFR2GR_S] = {"movfr2gr.s", L_FORM_GP_FROM_FP, 0},
    [MINIC_LOONG_MACHINE_FFINT_D_L] = {"ffint.d.l", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FFINT_D_W] = {"ffint.d.w", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FTINTRZ_L_D] = {"ftintrz.l.d", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FTINTRZ_W_D] = {"ftintrz.w.d", L_FORM_FP_RR, 0},
    [MINIC_LOONG_MACHINE_FLD_S] = {"fld.s", L_FORM_FP_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_READ, 4U},
    [MINIC_LOONG_MACHINE_FLD_D] = {"fld.d", L_FORM_FP_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_READ, 8U},
    [MINIC_LOONG_MACHINE_FST_S] = {"fst.s", L_FORM_FP_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, 4U},
    [MINIC_LOONG_MACHINE_FST_D] = {"fst.d", L_FORM_FP_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, 8U},
    [MINIC_LOONG_MACHINE_NOP] = {"nop", L_FORM_NONE, 0},
    BUILTIN(CPUCFG, "cpucfg", L_FORM_BUILTIN_RR_WR),
    BUILTIN(ASRTLE_D, "asrtle.d", L_FORM_BUILTIN_RR_RD),
    BUILTIN(ASRTGT_D, "asrtgt.d", L_FORM_BUILTIN_RR_RD),
    BUILTIN(CRC_W_B_W, "crc.w.b.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRC_W_H_W, "crc.w.h.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRC_W_W_W, "crc.w.w.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRC_W_D_W, "crc.w.d.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRCC_W_B_W, "crcc.w.b.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRCC_W_H_W, "crcc.w.h.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRCC_W_W_W, "crcc.w.w.w", L_FORM_BUILTIN_RRR),
    BUILTIN(CRCC_W_D_W, "crcc.w.d.w", L_FORM_BUILTIN_RRR),
    BUILTIN(IOCSRRD_B, "iocsrrd.b", L_FORM_BUILTIN_RR_WR),
    BUILTIN(IOCSRRD_H, "iocsrrd.h", L_FORM_BUILTIN_RR_WR),
    BUILTIN(IOCSRRD_W, "iocsrrd.w", L_FORM_BUILTIN_RR_WR),
    BUILTIN(IOCSRRD_D, "iocsrrd.d", L_FORM_BUILTIN_RR_WR),
    BUILTIN(IOCSRWR_B, "iocsrwr.b", L_FORM_BUILTIN_RR_RD),
    BUILTIN(IOCSRWR_H, "iocsrwr.h", L_FORM_BUILTIN_RR_RD),
    BUILTIN(IOCSRWR_W, "iocsrwr.w", L_FORM_BUILTIN_RR_RD),
    BUILTIN(IOCSRWR_D, "iocsrwr.d", L_FORM_BUILTIN_RR_RD),
    BUILTIN(CSRRD, "csrrd", L_FORM_BUILTIN_CSR_RD),
    BUILTIN(CSRWR, "csrwr", L_FORM_BUILTIN_CSR_RW),
    BUILTIN(CSRXCHG, "csrxchg", L_FORM_BUILTIN_CSR_XCHG),
    [MINIC_LOONG_MACHINE_VLD] = {"vld", L_FORM_VEC_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_READ, 16U},
    [MINIC_LOONG_MACHINE_VST] = {"vst", L_FORM_VEC_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, 16U},
    [MINIC_LOONG_MACHINE_VXOR_V] = {"vxor.v", L_FORM_VEC_RRR, 0U, 16U},
    [MINIC_LOONG_MACHINE_VANDI_B] = {"vandi.b", L_FORM_VEC_RRI, 0U, 16U},
    [MINIC_LOONG_MACHINE_VORI_B] = {"vori.b", L_FORM_VEC_RRI, 0U, 16U},
    [MINIC_LOONG_MACHINE_VSLTI_B] = {"vslti.b", L_FORM_VEC_RRI, 0U, 16U},
    [MINIC_LOONG_MACHINE_VSLLI_B] = {"vslli.b", L_FORM_VEC_RRI, 0U, 16U},
    [MINIC_LOONG_MACHINE_VSRLI_B] = {"vsrli.b", L_FORM_VEC_RRI, 0U, 16U},
    [MINIC_LOONG_MACHINE_VSHUF_B] = {"vshuf.b", L_FORM_VEC_RRRR, 0U, 16U},
    [MINIC_LOONG_MACHINE_XVLD] = {"xvld", L_FORM_VEC_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_READ, 32U},
    [MINIC_LOONG_MACHINE_XVST] = {"xvst", L_FORM_VEC_MEMORY,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, 32U},
    [MINIC_LOONG_MACHINE_XVXOR_V] = {"xvxor.v", L_FORM_VEC_RRR, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVANDI_B] = {"xvandi.b", L_FORM_VEC_RRI, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVORI_B] = {"xvori.b", L_FORM_VEC_RRI, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVSLTI_B] = {"xvslti.b", L_FORM_VEC_RRI, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVSLLI_B] = {"xvslli.b", L_FORM_VEC_RRI, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVSRLI_B] = {"xvsrli.b", L_FORM_VEC_RRI, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVSHUF_B] = {"xvshuf.b", L_FORM_VEC_RRRR, 0U, 32U},
    [MINIC_LOONG_MACHINE_XVREPLVE0_Q] = {"xvreplve0.q", L_FORM_VEC_RR,
        0U, 32U}
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
#undef BUILTIN

static const LoongSpec *loong_spec(uint32_t opcode) {
    if (opcode == 0U || opcode >= sizeof(loong_specs) / sizeof(loong_specs[0]) ||
        loong_specs[opcode].name == NULL) return NULL;
    return &loong_specs[opcode];
}

bool minic_loongarch64_machine_builtin_opcode(
    const char *mnemonic, uint32_t *opcode) {
    uint32_t candidate;
    if (mnemonic == NULL || opcode == NULL) return false;
    for (candidate = MINIC_LOONG_MACHINE_CPUCFG;
         candidate <= MINIC_LOONG_MACHINE_CSRXCHG; ++candidate) {
        const LoongSpec *spec = loong_spec(candidate);
        if (spec != NULL && strcmp(spec->name, mnemonic) == 0) {
            *opcode = candidate;
            return true;
        }
    }
    return false;
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
        features == (minic_loongarch64_target_soft_float_abi(target)
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
        return !minic_loongarch64_target_soft_float_abi(target) && reg->number <= 31U &&
            (reg->width_bits == 32U || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_VECTOR:
        /* Explicit source LSX/LASX uses the vector file even when the
         * kernel's C calling convention is soft-float LP64S. */
        return reg->number < 32U &&
            (reg->width_bits == 128U || reg->width_bits == 256U);
    case MINIC_MACHINE_REGISTER_FLAGS:
        return !minic_loongarch64_target_soft_float_abi(target) && reg->number < 8U &&
            reg->width_bits == 1U;
    default:
        return false;
    }
}

static bool loong_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t reloc, int64_t addend, void *context) {
    (void)context;
    if (target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        symbol == NULL || symbol[0] == '\0') return false;
    if (reloc == MINIC_LOONG_RELOC_JUMP_LABEL_KEY)
        return minic_loongarch64_jump_key_relocation_valid(symbol, addend);
    return addend == 0 && reloc >= MINIC_LOONG_RELOC_DIRECT &&
        reloc <= MINIC_LOONG_RELOC_PC_LO12;
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
#define RW (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE)
    switch (spec->form) {
    case L_FORM_RR: REG(WR); REG(RD); break;
    case L_FORM_BUILTIN_RR_WR: REG(WR); REG(RD); break;
    case L_FORM_BUILTIN_RR_RD: REG(RD); REG(RD); break;
    case L_FORM_BUILTIN_RRR: REG(WR); REG(RD); REG(RD); break;
    case L_FORM_BUILTIN_CSR_RD: REG(WR); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_BUILTIN_CSR_RW: REG(RW); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_BUILTIN_CSR_XCHG:
        REG(RW); REG(RD); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_RRII:
        REG(WR); REG(RD);
        ARG(MINIC_MACHINE_OPERAND_INTEGER);
        ARG(MINIC_MACHINE_OPERAND_INTEGER);
        break;
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
    case L_FORM_RSYMBOL:
    case L_FORM_RSYMBOL_LOCAL:
        REG(WR); ARG(MINIC_MACHINE_OPERAND_SYMBOL); break;
    case L_FORM_RBLOCK_ADDRESS:
        REG(WR); ARG(MINIC_MACHINE_OPERAND_BLOCK_ADDRESS); break;
    case L_FORM_JIRL:
        REG(RD); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_FP_RR:
    case L_FORM_GP_FROM_FCC:
    case L_FORM_GP_FROM_FP:
    case L_FORM_FP_FROM_GP:
        REG(WR); REG(RD); break;
    case L_FORM_FP_RRR:
    case L_FORM_FP_CMP:
        REG(WR); REG(RD); REG(RD); break;
    case L_FORM_FP_MEMORY:
        REG(spec->effects == MINIC_MACHINE_EFFECT_MEMORY_READ ? WR : RD);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case L_FORM_VEC_MEMORY:
        REG(spec->effects == MINIC_MACHINE_EFFECT_MEMORY_READ ? WR : RD);
        ARG(MINIC_MACHINE_OPERAND_ADDRESS); break;
    case L_FORM_VEC_RR: REG(WR); REG(RD); break;
    case L_FORM_VEC_RRR: REG(WR); REG(RD); REG(RD); break;
    case L_FORM_VEC_RRRR: REG(WR); REG(RD); REG(RD); REG(RD); break;
    case L_FORM_VEC_RRI:
        REG(WR); REG(RD); ARG(MINIC_MACHINE_OPERAND_INTEGER); break;
    case L_FORM_NONE: break;
    default: return false;
    }
#undef REG
#undef ARG
#undef RD
#undef WR
#undef RW
    contract->operand_count = n;
    contract->required_generated_features = MINIC_LOONG_MACHINE_FEATURE_BASE;
    if (spec->form == L_FORM_FP_RR || spec->form == L_FORM_FP_RRR ||
        spec->form == L_FORM_FP_CMP || spec->form == L_FORM_GP_FROM_FCC ||
        spec->form == L_FORM_GP_FROM_FP || spec->form == L_FORM_FP_FROM_GP ||
        spec->form == L_FORM_FP_MEMORY)
        contract->required_generated_features |= MINIC_LOONG_MACHINE_FEATURE_FP;
    contract->required_effects = spec->effects;
    contract->permitted_effects = spec->effects;
    if ((spec->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE)) != 0U)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_VOLATILE;
    if (spec->form == L_FORM_VEC_MEMORY || spec->form == L_FORM_VEC_RR ||
        spec->form == L_FORM_VEC_RRR || spec->form == L_FORM_VEC_RRRR ||
        spec->form == L_FORM_VEC_RRI)
        contract->permitted_effects |= MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
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
        if (!minic_loongarch64_target_soft_float_abi(target))
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
    if (spec->form != L_FORM_FP_RR && spec->form != L_FORM_FP_RRR &&
        spec->form != L_FORM_FP_CMP && spec->form != L_FORM_GP_FROM_FCC &&
        spec->form != L_FORM_GP_FROM_FP && spec->form != L_FORM_FP_FROM_GP &&
        spec->form != L_FORM_FP_MEMORY &&
        spec->form != L_FORM_VEC_MEMORY && spec->form != L_FORM_VEC_RR &&
        spec->form != L_FORM_VEC_RRR && spec->form != L_FORM_VEC_RRRR &&
        spec->form != L_FORM_VEC_RRI)
        for (i = 0U; i < ins->operand_count; ++i)
            if (ops[i].kind == MINIC_MACHINE_OPERAND_REGISTER &&
                (ops[i].value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                 ops[i].value.physical.reg.width_bits != 64U)) return false;
    switch (spec->form) {
    case L_FORM_BUILTIN_RR_WR:
        /* CPUCFG/IOCSRRD read a0 and replace a0.  Repeated register operands
         * are intentional and carry different access contracts. */
        return ops[0].value.physical.reg.number == 4U &&
            ops[1].value.physical.reg.number == 4U;
    case L_FORM_BUILTIN_RR_RD:
        /* ASRT and IOCSRWR do not write their source registers. */
        return ops[0].value.physical.reg.number == 4U &&
            ops[1].value.physical.reg.number == 5U;
    case L_FORM_BUILTIN_RRR:
        return ops[0].value.physical.reg.number == 4U &&
            ops[1].value.physical.reg.number == 4U &&
            ops[2].value.physical.reg.number == 5U;
    case L_FORM_BUILTIN_CSR_RD:
    case L_FORM_BUILTIN_CSR_RW:
        return ops[0].value.physical.reg.number == 4U &&
            ops[1].value.integer >= 0 && ops[1].value.integer <= 16383;
    case L_FORM_BUILTIN_CSR_XCHG:
        return ops[0].value.physical.reg.number == 4U &&
            ops[1].value.physical.reg.number == 5U &&
            ops[2].value.integer >= 0 && ops[2].value.integer <= 16383;
    case L_FORM_RRII:
        return ops[3].value.integer >= 0 &&
            ops[2].value.integer >= ops[3].value.integer &&
            ops[2].value.integer <
                (ins->opcode == MINIC_LOONG_MACHINE_BSTRPICK_D ? 64 : 32);
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
    case L_FORM_RSYMBOL_LOCAL:
        return ops[1].value.symbol.relocation == MINIC_LOONG_RELOC_DIRECT;
    case L_FORM_RBLOCK_ADDRESS:
        return true;
    case L_FORM_JIRL:
        return ops[1].value.integer >= -32768 &&
            ops[1].value.integer <= 32764 &&
            (ops[1].value.integer % 4) == 0;
    case L_FORM_FP_RR: {
        unsigned int out_width = 64U, src_width = 64U;
        if (ins->opcode == MINIC_LOONG_MACHINE_FCVT_D_S ||
            ins->opcode == MINIC_LOONG_MACHINE_FFINT_D_W) src_width = 32U;
        if (ins->opcode == MINIC_LOONG_MACHINE_FCVT_S_D ||
            ins->opcode == MINIC_LOONG_MACHINE_FTINTRZ_W_D) out_width = 32U;
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[1].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[0].value.physical.reg.width_bits == out_width &&
            ops[1].value.physical.reg.width_bits == src_width;
    }
    case L_FORM_FP_RRR: {
        const unsigned int bits =
            ins->opcode == MINIC_LOONG_MACHINE_FADD_S ||
            ins->opcode == MINIC_LOONG_MACHINE_FSUB_S ||
            ins->opcode == MINIC_LOONG_MACHINE_FMUL_S ||
            ins->opcode == MINIC_LOONG_MACHINE_FDIV_S ? 32U : 64U;
        for (i = 0U; i < 3U; ++i)
            if (ops[i].value.physical.reg.bank != MINIC_MACHINE_REGISTER_FLOAT ||
                ops[i].value.physical.reg.width_bits != bits) return false;
        return true;
    }
    case L_FORM_FP_CMP:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
            ops[0].value.physical.reg.width_bits == 1U &&
            ops[1].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[1].value.physical.reg.width_bits == 64U &&
            ops[2].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[2].value.physical.reg.width_bits == 64U;
    case L_FORM_GP_FROM_FCC:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
            ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
            ops[1].value.physical.reg.width_bits == 1U;
    case L_FORM_GP_FROM_FP:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
            ops[0].value.physical.reg.width_bits == 64U &&
            ops[1].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[1].value.physical.reg.width_bits ==
                (ins->opcode == MINIC_LOONG_MACHINE_MOVFR2GR_S ? 32U : 64U);
    case L_FORM_FP_FROM_GP:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[0].value.physical.reg.width_bits ==
                (ins->opcode == MINIC_LOONG_MACHINE_MOVGR2FR_W ? 32U : 64U) &&
            ops[1].value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
            ops[1].value.physical.reg.width_bits == 64U;
    case L_FORM_FP_MEMORY:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            ops[0].value.physical.reg.width_bits == spec->access_width * 8U &&
            ops[1].value.address.target_mode == MINIC_LOONG_ADDRESS_IMM12;
    case L_FORM_VEC_MEMORY:
        return ops[0].value.physical.reg.bank == MINIC_MACHINE_REGISTER_VECTOR &&
            ops[0].value.physical.reg.width_bits == spec->access_width * 8U &&
            ops[1].value.address.target_mode == MINIC_LOONG_ADDRESS_IMM12 &&
            (ops[1].value.address.displacement == 0 ||
             ops[1].value.address.displacement == 16 ||
             ops[1].value.address.displacement == 32 ||
             ops[1].value.address.displacement == 48);
    case L_FORM_VEC_RR:
    case L_FORM_VEC_RRR:
    case L_FORM_VEC_RRRR:
    case L_FORM_VEC_RRI: {
        const size_t count = spec->form == L_FORM_VEC_RR ? 2U :
            spec->form == L_FORM_VEC_RRRR ? 4U :
            spec->form == L_FORM_VEC_RRI ? 2U : 3U;
        for (i = 0U; i < count; ++i)
            if (ops[i].value.physical.reg.bank != MINIC_MACHINE_REGISTER_VECTOR ||
                ops[i].value.physical.reg.width_bits != spec->access_width * 8U)
                return false;
        if (spec->form != L_FORM_VEC_RRI) return true;
        imm = ops[2].value.integer;
        if (ins->opcode == MINIC_LOONG_MACHINE_VANDI_B ||
            ins->opcode == MINIC_LOONG_MACHINE_XVANDI_B)
            return imm == 0x0f || imm == 0x1d;
        if (ins->opcode == MINIC_LOONG_MACHINE_VSLLI_B ||
            ins->opcode == MINIC_LOONG_MACHINE_XVSLLI_B) return imm == 1;
        if (ins->opcode == MINIC_LOONG_MACHINE_VSRLI_B ||
            ins->opcode == MINIC_LOONG_MACHINE_XVSRLI_B) return imm == 4;
        return imm == 0;
    }
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

static bool loong_fp_text(const MinicMachineRegister *reg,
    char *buffer, size_t capacity) {
    if (reg == NULL || reg->bank != MINIC_MACHINE_REGISTER_FLOAT ||
        reg->number >= 32U ||
        (reg->width_bits != 32U && reg->width_bits != 64U)) return false;
    if (reg->number <= 7U)
        return snprintf(buffer, capacity, "$fa%u", reg->number) > 0;
    if (reg->number <= 23U)
        return snprintf(buffer, capacity, "$ft%u", reg->number - 8U) > 0;
    return snprintf(buffer, capacity, "$fs%u", reg->number - 24U) > 0;
}

static bool loong_fcc_text(const MinicMachineRegister *reg,
    char *buffer, size_t capacity) {
    return reg != NULL && reg->bank == MINIC_MACHINE_REGISTER_FLAGS &&
        reg->number < 8U && reg->width_bits == 1U &&
        snprintf(buffer, capacity, "$fcc%u", reg->number) > 0;
}

static bool loong_vec_text(const MinicMachineRegister *reg,
    char *buffer, size_t capacity) {
    return reg != NULL && reg->bank == MINIC_MACHINE_REGISTER_VECTOR &&
        reg->number < 32U &&
        (reg->width_bits == 128U || reg->width_bits == 256U) &&
        snprintf(buffer, capacity, "$%s%u",
            reg->width_bits == 128U ? "vr" : "xr", reg->number) > 0;
}

static bool loong_block_name(const MinicMachineOperand *operand,
    const MinicMachineGNUAsmLabels *labels, const char **name) {
    if ((operand->kind != MINIC_MACHINE_OPERAND_BLOCK &&
         operand->kind != MINIC_MACHINE_OPERAND_BLOCK_ADDRESS) || labels == NULL ||
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
    char a[20], b[20], c[20], d[20];
    unsigned int base_id;
    int64_t offset;
    const char *label;
    (void)context;
    if (out == NULL || function == NULL || spec == NULL ||
        !loong_legal_selected(function, ins, NULL)) return false;
    switch (spec->form) {
    case L_FORM_BUILTIN_RR_WR:
    case L_FORM_BUILTIN_RR_RD:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_BUILTIN_RRR:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_gp_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case L_FORM_BUILTIN_CSR_RD:
    case L_FORM_BUILTIN_CSR_RW:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  %s %s, %" PRId64 "\n",
                spec->name, a, o[1].value.integer) >= 0;
    case L_FORM_BUILTIN_CSR_XCHG:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n",
                spec->name, a, b, o[2].value.integer) >= 0;
    case L_FORM_RRII:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 ", %" PRId64 "\n",
                spec->name, a, b, o[2].value.integer, o[3].value.integer) >= 0;
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
    case L_FORM_FP_RR:
        return loong_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_FP_RRR:
        return loong_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_fp_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case L_FORM_FP_CMP:
        return loong_fcc_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_fp_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case L_FORM_GP_FROM_FCC:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_fcc_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_GP_FROM_FP:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_fp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_FP_FROM_GP:
        return loong_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_FP_MEMORY:
        return loong_fp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.address.base, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n", spec->name,
                a, b, o[1].value.address.displacement) >= 0;
    case L_FORM_VEC_MEMORY:
        return loong_vec_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_gp_text(&o[1].value.address.base, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n", spec->name,
                a, b, o[1].value.address.displacement) >= 0;
    case L_FORM_VEC_RR:
        return loong_vec_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_vec_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s\n", spec->name, a, b) >= 0;
    case L_FORM_VEC_RRR:
        return loong_vec_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_vec_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_vec_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            fprintf(out, "  %s %s, %s, %s\n", spec->name, a, b, c) >= 0;
    case L_FORM_VEC_RRRR:
        return loong_vec_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_vec_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            loong_vec_text(&o[2].value.physical.reg, c, sizeof(c)) &&
            loong_vec_text(&o[3].value.physical.reg, d, sizeof(d)) &&
            fprintf(out, "  %s %s, %s, %s, %s\n", spec->name,
                a, b, c, d) >= 0;
    case L_FORM_VEC_RRI:
        return loong_vec_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_vec_text(&o[1].value.physical.reg, b, sizeof(b)) &&
            fprintf(out, "  %s %s, %s, %" PRId64 "\n", spec->name,
                a, b, o[2].value.integer) >= 0;
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
    case L_FORM_RSYMBOL_LOCAL:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            fprintf(out, "  la.local %s, %s\n", a,
                o[1].value.symbol.name) >= 0;
    case L_FORM_RBLOCK_ADDRESS:
        return loong_gp_text(&o[0].value.physical.reg, a, sizeof(a)) &&
            loong_block_name(&o[1], labels, &label) &&
            fprintf(out, "  la.local %s, %s\n", a, label) >= 0;
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

static bool loong_selected_source_asm(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *ins, void *context) {
    const LoongSpec *spec;
    (void)context;
    if (function == NULL || function->target == NULL ||
        function->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        ins == NULL || !ins->has_selected_source_asm_seal ||
        ins->source_asm_machine_form_id != MINIC_LOONG_MACHINE_ASM_SIMD ||
        ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        ins->source_asm_template != NULL || ins->source_asm_length != 0U ||
        (spec = loong_spec(ins->opcode)) == NULL ||
        (spec->form != L_FORM_VEC_MEMORY &&
         spec->form != L_FORM_VEC_RR &&
         spec->form != L_FORM_VEC_RRR &&
         spec->form != L_FORM_VEC_RRRR &&
         spec->form != L_FORM_VEC_RRI))
        return false;
    return ins->source_asm_machine_form_variant ==
        (spec->access_width == 32U ? MINIC_LOONG_MACHINE_SIMD_LASX
                                    : MINIC_LOONG_MACHINE_SIMD_LSX);
}

bool minic_loongarch64_machine_opcode_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *destination) {
    static const MinicMachineGNUAsmPolicy policy = {
        .section_type_marker = MINIC_MACHINE_GNU_SECTION_TYPE_AT,
        .verify = {
            .lookup_opcode = loong_lookup,
            .validate_generated_features = loong_features,
            .validate_register = loong_register,
            .validate_address = loong_address,
            .validate_relocation = loong_relocation,
            .validate_source_asm = NULL,
            .validate_function_abi = loong_abi,
            .validate_frame_object = loong_frame_object,
            .validate_selected_instruction = loong_legal_selected,
            .validate_selected_source_asm = loong_selected_source_asm
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
