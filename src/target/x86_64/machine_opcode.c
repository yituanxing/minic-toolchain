#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_target_state.h"
#include "target/x86_64/asm_contract.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum X86ImplicitKind {
    X86_IMPLICIT_NONE = 0,
    X86_IMPLICIT_FLAGS_DEF,
    X86_IMPLICIT_FLAGS_USE,
    X86_IMPLICIT_FLAGS_USE_DEF,
    X86_IMPLICIT_FLAGS_CLOBBER,
    X86_IMPLICIT_CL_FLAGS_DEF,
    X86_IMPLICIT_STACK_PUSH,
    X86_IMPLICIT_STACK_POP,
    X86_IMPLICIT_STACK_LEAVE,
    X86_IMPLICIT_STACK_RET,
    X86_IMPLICIT_MUL,
    X86_IMPLICIT_MUL32,
    X86_IMPLICIT_DIV,
    X86_IMPLICIT_CQTO,
    X86_IMPLICIT_REP_MOVSB,
    X86_IMPLICIT_CALL_SYSV,
    X86_IMPLICIT_CALL_MS,
    X86_IMPLICIT_CALL_UACCESS,
    X86_IMPLICIT_CALL_SYSV_THUNK_R11,
    X86_IMPLICIT_JMP_THUNK_RAX,
    X86_IMPLICIT_X87_STACK_PUSH,
    X86_IMPLICIT_X87_STACK_POP,
    X86_IMPLICIT_X87_STACK_UNARY,
    X86_IMPLICIT_X87_STACK_SWAP,
    X86_IMPLICIT_X87_STACK_BINARY_POP,
    X86_IMPLICIT_X87_STACK_COMPARE_POP
};

#define R MINIC_MACHINE_REGISTER_READ
#define W MINIC_MACHINE_REGISTER_WRITE
#define C MINIC_MACHINE_REGISTER_CLOBBER
#define MR MINIC_MACHINE_EFFECT_MEMORY_READ
#define MW MINIC_MACHINE_EFFECT_MEMORY_WRITE
#define BR MINIC_MACHINE_EFFECT_BRANCH
#define CA MINIC_MACHINE_EFFECT_CALL
#define RT MINIC_MACHINE_EFFECT_RETURN
#define BAR MINIC_MACHINE_EFFECT_COMPILER_BARRIER
#define BASE MINIC_X86_MACHINE_GENERATED_BASE
#define IBT MINIC_X86_MACHINE_GENERATED_IBT
#define SSE2 MINIC_X86_MACHINE_GENERATED_SSE2

/* The physical ISA descriptor is intentionally independent of a particular
 * Core operation or mutable instruction. It is never synthesized from an
 * instruction's own advertised effects or register clobbers. */
#define Z(op, word) [MINIC_X86_MI_##op] = { .mnemonic=word, .required_features=BASE }
#define RR(op, word, width1, width2, dst_access, impl) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RR, \
      .first_width_bits=width1, .second_width_bits=width2, \
      .first_access=R, .second_access=dst_access, .required_features=BASE, \
      .implicit_kind=impl }
#define IR(op, word, width, dst_access, impl) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_IR, \
      .second_width_bits=width, .second_access=dst_access, \
      .required_features=BASE, .implicit_kind=impl }
#define AR(op, word, width, impl) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_AR, \
      .second_width_bits=width, .second_access=W, .effects=MR, \
      .required_features=BASE, .implicit_kind=impl }
#define RA(op, word, width) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RA, \
      .first_width_bits=width, .first_access=R, .effects=MW, .required_features=BASE }
#define FR(op, word, width, base) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_FR, \
      .second_width_bits=width, .second_access=W, .effects=MR, \
      .frame_base=base, .required_features=BASE }
#define RF(op, word, width, base) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RF, \
      .first_width_bits=width, .first_access=R, .effects=MW, \
      .frame_base=base, .required_features=BASE }
#define SR(op, word, reloc_id, effect_bits) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_SR, \
      .second_width_bits=64U, .second_access=W, .effects=effect_bits, \
      .relocation=reloc_id, .required_features=BASE }
#define REG(op, word, width, access, impl) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_R, \
      .first_width_bits=width, .first_access=access, \
      .implicit_kind=impl, .required_features=BASE }
#define COND(op, word) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_B, \
      .effects=BR, .required_features=BASE, .implicit_kind=X86_IMPLICIT_FLAGS_USE, \
      .terminates_block=true, .permits_fallthrough=true }
#define SET(op, word) REG(op,word,8U,W,X86_IMPLICIT_FLAGS_USE)
#define BRANCH(op, word) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_B, \
      .effects=BR, .required_features=BASE, .terminates_block=true }
#define SYM(op, word, reloc_id, effect_bits, impl) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_S, \
      .relocation=reloc_id, .effects=effect_bits, .implicit_kind=impl, \
      .required_features=BASE, .terminates_block=(((effect_bits) & (BR|RT)) != 0U) }

#define XRR(op, word, first_bank_id, first_width, first_access_bits, \
            second_bank_id, second_width, second_access_bits, implicit_id) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RR, \
      .first_bank=first_bank_id, .first_width_bits=first_width, \
      .second_bank=second_bank_id, .second_width_bits=second_width, \
      .first_access=first_access_bits, .second_access=second_access_bits, \
      .required_features=SSE2, .implicit_kind=implicit_id }
#define XAR(op, word, effect_bits, access) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_AR, \
      .second_bank=MINIC_MACHINE_REGISTER_VECTOR, .second_width_bits=128, \
      .second_access=access, .effects=effect_bits, .required_features=SSE2 }
#define XRA(op, word, effect_bits) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RA, \
      .first_bank=MINIC_MACHINE_REGISTER_VECTOR, .first_width_bits=128, \
      .first_access=R, .effects=effect_bits, .required_features=SSE2 }
#define XFR(op, word, base_id) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_FR, \
      .second_bank=MINIC_MACHINE_REGISTER_VECTOR, .second_width_bits=128, \
      .second_access=W, .frame_base=base_id, .effects=MR, \
      .required_features=SSE2 }
#define XRF(op, word, base_id) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=MINIC_X86_MACHINE_FORMAT_RF, \
      .first_bank=MINIC_MACHINE_REGISTER_VECTOR, .first_width_bits=128, \
      .first_access=R, .frame_base=base_id, .effects=MW, \
      .required_features=SSE2 }
#define X87M(op, word, format_id, effect_bits, implicit_id, base_id, width) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .format=format_id, \
      .effects=effect_bits, .frame_base=base_id, .required_features=BASE, \
      .memory_width_bits=width, .implicit_kind=implicit_id }
#define X87Z(op, word, implicit_id) \
    [MINIC_X86_MI_##op] = { .mnemonic=word, .required_features=BASE, \
      .implicit_kind=implicit_id }

static const MinicX86MachineOpcodeInfo x86_opcodes[MINIC_X86_MI_LAST] = {
    Z(NOP, "nop"), Z(PAUSE, "pause"),
    [MINIC_X86_MI_LFENCE] = { .mnemonic="lfence", .effects=BAR,
        .required_features=BASE },
    [MINIC_X86_MI_ENDBR64] = { .mnemonic="endbr64", .required_features=IBT },
    [MINIC_X86_MI_UD2] = { .mnemonic="ud2", .effects=BR,
        .terminates_block=true, .required_features=BASE },
    [MINIC_X86_MI_LEAVE] = { .mnemonic="leave", .implicit_kind=X86_IMPLICIT_STACK_LEAVE,
        .effects=MR, .required_features=BASE },
    [MINIC_X86_MI_RET] = { .mnemonic="ret", .implicit_kind=X86_IMPLICIT_STACK_RET,
        .effects=RT|MR, .terminates_block=true, .required_features=BASE },
    [MINIC_X86_MI_PUSH64_R] = { .mnemonic="pushq",
        .format=MINIC_X86_MACHINE_FORMAT_R, .first_width_bits=64, .first_access=R,
        .implicit_kind=X86_IMPLICIT_STACK_PUSH, .effects=MW, .required_features=BASE },
    [MINIC_X86_MI_POP64_R] = { .mnemonic="popq",
        .format=MINIC_X86_MACHINE_FORMAT_R, .first_width_bits=64, .first_access=W,
        .implicit_kind=X86_IMPLICIT_STACK_POP, .effects=MR, .required_features=BASE },

    RR(MOV8_RR,"movb",8,8,W,X86_IMPLICIT_NONE),
    RR(MOV16_RR,"movw",16,16,W,X86_IMPLICIT_NONE),
    RR(MOV32_RR,"movl",32,32,W,X86_IMPLICIT_NONE),
    RR(MOV64_RR,"movq",64,64,W,X86_IMPLICIT_NONE),
    IR(MOV8_IR,"movb",8,W,X86_IMPLICIT_NONE),
    IR(MOV16_IR,"movw",16,W,X86_IMPLICIT_NONE),
    IR(MOV32_IR,"movl",32,W,X86_IMPLICIT_NONE),
    IR(MOV64_IR,"movq",64,W,X86_IMPLICIT_NONE),
    IR(MOVABS64_IR,"movabsq",64,W,X86_IMPLICIT_NONE),
    AR(MOV8_AR,"movb",8,X86_IMPLICIT_NONE),
    AR(MOV16_AR,"movw",16,X86_IMPLICIT_NONE),
    AR(MOV32_AR,"movl",32,X86_IMPLICIT_NONE),
    AR(MOV64_AR,"movq",64,X86_IMPLICIT_NONE),
    RA(MOV8_RA,"movb",8), RA(MOV16_RA,"movw",16),
    RA(MOV32_RA,"movl",32), RA(MOV64_RA,"movq",64),
    FR(MOV8_FRBP_R,"movb",8,1), FR(MOV16_FRBP_R,"movw",16,1),
    FR(MOV32_FRBP_R,"movl",32,1), FR(MOV64_FRBP_R,"movq",64,1),
    FR(MOV8_FRSP_R,"movb",8,2), FR(MOV16_FRSP_R,"movw",16,2),
    FR(MOV32_FRSP_R,"movl",32,2), FR(MOV64_FRSP_R,"movq",64,2),
    RF(MOV8_R_FRBP,"movb",8,1), RF(MOV16_R_FRBP,"movw",16,1),
    RF(MOV32_R_FRBP,"movl",32,1), RF(MOV64_R_FRBP,"movq",64,1),
    RF(MOV8_R_FRSP,"movb",8,2), RF(MOV16_R_FRSP,"movw",16,2),
    RF(MOV32_R_FRSP,"movl",32,2), RF(MOV64_R_FRSP,"movq",64,2),
    RR(MOVSBQ_RR,"movsbq",8,64,W,X86_IMPLICIT_NONE),
    RR(MOVZBQ_RR,"movzbq",8,64,W,X86_IMPLICIT_NONE),
    RR(MOVSWQ_RR,"movswq",16,64,W,X86_IMPLICIT_NONE),
    RR(MOVZWQ_RR,"movzwq",16,64,W,X86_IMPLICIT_NONE),
    RR(MOVSLQ_RR,"movslq",32,64,W,X86_IMPLICIT_NONE),
    RR(MOVZBL_RR,"movzbl",8,32,W,X86_IMPLICIT_NONE),
    AR(MOVSBQ_AR,"movsbq",64,X86_IMPLICIT_NONE),
    AR(MOVZBQ_AR,"movzbq",64,X86_IMPLICIT_NONE),
    AR(MOVSWQ_AR,"movswq",64,X86_IMPLICIT_NONE),
    AR(MOVZWQ_AR,"movzwq",64,X86_IMPLICIT_NONE),
    AR(MOVSLQ_AR,"movslq",64,X86_IMPLICIT_NONE),
    [MINIC_X86_MI_LEA64_AR] = { .mnemonic="leaq",
        .format=MINIC_X86_MACHINE_FORMAT_AR, .second_width_bits=64,
        .second_access=W, .required_features=BASE },
    [MINIC_X86_MI_LEA64_FRBP_R] = { .mnemonic="leaq",
        .format=MINIC_X86_MACHINE_FORMAT_FR, .second_width_bits=64,
        .second_access=W, .frame_base=1, .required_features=BASE },
    [MINIC_X86_MI_LEA64_FRSP_R] = { .mnemonic="leaq",
        .format=MINIC_X86_MACHINE_FORMAT_FR, .second_width_bits=64,
        .second_access=W, .frame_base=2, .required_features=BASE },
    SR(LEA64_RIP_SYMBOL_R,"leaq",MINIC_X86_MACHINE_RELOC_PC32,0),
    SR(MOV64_GOTPCREL_R,"movq",MINIC_X86_MACHINE_RELOC_GOTPCREL,MR),
    SR(MOVABS64_SYMBOL_R,"movabsq",MINIC_X86_MACHINE_RELOC_ABSOLUTE,0),
    SR(MOV64_IMM32S_SYMBOL_R,"movq",MINIC_X86_MACHINE_RELOC_ABS32S,0),

    RR(ADD64_RR,"addq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(ADC64_RR,"adcq",64,64,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    RR(SUB64_RR,"subq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(SBB64_RR,"sbbq",64,64,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    RR(IMUL64_RR,"imulq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(AND64_RR,"andq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(OR64_RR,"orq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(XOR64_RR,"xorq",64,64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(CMP64_RR,"cmpq",64,64,R,X86_IMPLICIT_FLAGS_DEF),
    RR(TEST64_RR,"testq",64,64,R,X86_IMPLICIT_FLAGS_DEF),
    RR(ADD32_RR,"addl",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(ADC32_RR,"adcl",32,32,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    RR(SUB32_RR,"subl",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(SBB32_RR,"sbbl",32,32,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    RR(IMUL32_RR,"imull",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(AND32_RR,"andl",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(OR32_RR,"orl",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(XOR32_RR,"xorl",32,32,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(CMP32_RR,"cmpl",32,32,R,X86_IMPLICIT_FLAGS_DEF),
    RR(TEST32_RR,"testl",32,32,R,X86_IMPLICIT_FLAGS_DEF),
    IR(ADD64_IR,"addq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(ADC64_IR,"adcq",64,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    IR(SUB64_IR,"subq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(SBB64_IR,"sbbq",64,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    IR(AND64_IR,"andq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(OR64_IR,"orq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(XOR64_IR,"xorq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(CMP64_IR,"cmpq",64,R,X86_IMPLICIT_FLAGS_DEF),
    IR(TEST64_IR,"testq",64,R,X86_IMPLICIT_FLAGS_DEF),
    IR(ADD32_IR,"addl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(ADC32_IR,"adcl",32,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    IR(SUB32_IR,"subl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(SBB32_IR,"sbbl",32,R|W,X86_IMPLICIT_FLAGS_USE_DEF),
    IR(AND32_IR,"andl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(OR32_IR,"orl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(XOR32_IR,"xorl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    IR(CMP32_IR,"cmpl",32,R,X86_IMPLICIT_FLAGS_DEF),
    IR(TEST32_IR,"testl",32,R,X86_IMPLICIT_FLAGS_DEF),
    IR(SAR64_IR,"sarq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    REG(NEG64_R,"negq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    REG(NOT64_R,"notq",64,R|W,X86_IMPLICIT_NONE),
    REG(NEG32_R,"negl",32,R|W,X86_IMPLICIT_FLAGS_DEF),
    REG(NOT32_R,"notl",32,R|W,X86_IMPLICIT_NONE),
    REG(SHL64_CL_R,"shlq",64,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    REG(SHR64_CL_R,"shrq",64,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    REG(SAR64_CL_R,"sarq",64,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    REG(SHL32_CL_R,"shll",32,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    REG(SHR32_CL_R,"shrl",32,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    REG(SAR32_CL_R,"sarl",32,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    RR(SHLD64_CL_RR,"shldq",64,64,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    RR(SHRD64_CL_RR,"shrdq",64,64,R|W,X86_IMPLICIT_CL_FLAGS_DEF),
    RR(BSF64_RR,"bsfq",64,64,W,X86_IMPLICIT_FLAGS_DEF),
    RR(BSR64_RR,"bsrq",64,64,W,X86_IMPLICIT_FLAGS_DEF),
    RR(BSF32_RR,"bsfl",32,32,W,X86_IMPLICIT_FLAGS_DEF),
    RR(BSR32_RR,"bsrl",32,32,W,X86_IMPLICIT_FLAGS_DEF),
    [MINIC_X86_MI_CQTO] = { .mnemonic="cqto", .implicit_kind=X86_IMPLICIT_CQTO,
        .required_features=BASE },
    REG(MUL32_R,"mull",32,R,X86_IMPLICIT_MUL32),
    REG(MUL64_R,"mulq",64,R,X86_IMPLICIT_MUL),
    REG(IMUL64_R,"imulq",64,R,X86_IMPLICIT_MUL),
    REG(DIV64_R,"divq",64,R,X86_IMPLICIT_DIV),
    REG(IDIV64_R,"idivq",64,R,X86_IMPLICIT_DIV),
    [MINIC_X86_MI_REP_MOVSB] = { .mnemonic="rep movsb",
        .implicit_kind=X86_IMPLICIT_REP_MOVSB, .effects=MR|MW,
        .required_features=BASE },

    XRR(MOVD_GPR_XMM,"movd",MINIC_MACHINE_REGISTER_GENERAL,32,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,W,X86_IMPLICIT_NONE),
    XRR(MOVD_XMM_GPR,"movd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_GENERAL,32,W,X86_IMPLICIT_NONE),
    XRR(MOVQ_GPR_XMM,"movq",MINIC_MACHINE_REGISTER_GENERAL,64,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,W,X86_IMPLICIT_NONE),
    XRR(MOVQ_XMM_GPR,"movq",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_GENERAL,64,W,X86_IMPLICIT_NONE),
    XRR(ADDSS_XMM,"addss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(SUBSS_XMM,"subss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(MULSS_XMM,"mulss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(DIVSS_XMM,"divss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(ADDSD_XMM,"addsd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(SUBSD_XMM,"subsd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(MULSD_XMM,"mulsd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(DIVSD_XMM,"divsd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(UCOMISS_XMM,"ucomiss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R,X86_IMPLICIT_FLAGS_DEF),
    XRR(UCOMISD_XMM,"ucomisd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R,X86_IMPLICIT_FLAGS_DEF),
    XRR(CVTSI2SDQ_GPR_XMM,"cvtsi2sdq",MINIC_MACHINE_REGISTER_GENERAL,64,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(CVTTSD2SIQ_XMM_GPR,"cvttsd2siq",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_GENERAL,64,W,X86_IMPLICIT_NONE),
    XRR(CVTSS2SD_XMM,"cvtss2sd",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XRR(CVTSD2SS_XMM,"cvtsd2ss",MINIC_MACHINE_REGISTER_VECTOR,128,R,
        MINIC_MACHINE_REGISTER_VECTOR,128,R|W,X86_IMPLICIT_NONE),
    XAR(MOVDQU_ADDRESS_XMM,"movdqu",MR,W),
    XRA(MOVDQU_XMM_ADDRESS,"movdqu",MW),
    XFR(MOVDQU_FRAME_RBP_XMM,"movdqu",1),
    XRF(MOVDQU_XMM_FRAME_RBP,"movdqu",1),
    XFR(MOVDQU_FRAME_RSP_XMM,"movdqu",2),
    XRF(MOVDQU_XMM_FRAME_RSP,"movdqu",2),
    X87M(FLDT_ADDRESS,"fldt",MINIC_X86_MACHINE_FORMAT_A,MR,
        X86_IMPLICIT_X87_STACK_PUSH,0,80),
    X87M(FSTPT_ADDRESS,"fstpt",MINIC_X86_MACHINE_FORMAT_A,MW,
        X86_IMPLICIT_X87_STACK_POP,0,80),
    X87M(FLDT_FRAME_RBP,"fldt",MINIC_X86_MACHINE_FORMAT_F,MR,
        X86_IMPLICIT_X87_STACK_PUSH,1,80),
    X87M(FSTPT_FRAME_RBP,"fstpt",MINIC_X86_MACHINE_FORMAT_F,MW,
        X86_IMPLICIT_X87_STACK_POP,1,80),
    X87M(FLDS_FRAME_RBP,"flds",MINIC_X86_MACHINE_FORMAT_F,MR,
        X86_IMPLICIT_X87_STACK_PUSH,1,32),
    X87M(FLDL_FRAME_RBP,"fldl",MINIC_X86_MACHINE_FORMAT_F,MR,
        X86_IMPLICIT_X87_STACK_PUSH,1,64),
    X87M(FSTPS_FRAME_RBP,"fstps",MINIC_X86_MACHINE_FORMAT_F,MW,
        X86_IMPLICIT_X87_STACK_POP,1,32),
    X87M(FSTPL_FRAME_RBP,"fstpl",MINIC_X86_MACHINE_FORMAT_F,MW,
        X86_IMPLICIT_X87_STACK_POP,1,64),
    X87M(FILDL_FRAME_RBP,"fildl",MINIC_X86_MACHINE_FORMAT_F,MR,
        X86_IMPLICIT_X87_STACK_PUSH,1,32),
    X87M(FILDQ_FRAME_RBP,"fildq",MINIC_X86_MACHINE_FORMAT_F,MR,
        X86_IMPLICIT_X87_STACK_PUSH,1,64),
    X87M(FISTTPQ_FRAME_RSP,"fisttpq",MINIC_X86_MACHINE_FORMAT_F,MW,
        X86_IMPLICIT_X87_STACK_POP,2,64),
    X87M(FLDL_ADDRESS,"fldl",MINIC_X86_MACHINE_FORMAT_A,MR,
        X86_IMPLICIT_X87_STACK_PUSH,0,64),
    X87M(FILDQ_ADDRESS,"fildq",MINIC_X86_MACHINE_FORMAT_A,MR,
        X86_IMPLICIT_X87_STACK_PUSH,0,64),
    X87M(FISTTPQ_ADDRESS,"fisttpq",MINIC_X86_MACHINE_FORMAT_A,MW,
        X86_IMPLICIT_X87_STACK_POP,0,64),
    X87M(FADDL_ADDRESS,"faddl",MINIC_X86_MACHINE_FORMAT_A,MR,
        X86_IMPLICIT_X87_STACK_UNARY,0,64),
    X87Z(FCHS,"fchs",X86_IMPLICIT_X87_STACK_UNARY),
    X87Z(FXCH_ST1,"fxch",X86_IMPLICIT_X87_STACK_SWAP),
    X87Z(FUCOMIP_ST1,"fucomip",X86_IMPLICIT_X87_STACK_COMPARE_POP),
    X87Z(FSTP_ST0,"fstp",X86_IMPLICIT_X87_STACK_POP),
    X87Z(FADDP_ST1,"faddp",X86_IMPLICIT_X87_STACK_BINARY_POP),
    X87Z(FSUBRP_ST1,"fsubrp",X86_IMPLICIT_X87_STACK_BINARY_POP),
    X87Z(FMULP_ST1,"fmulp",X86_IMPLICIT_X87_STACK_BINARY_POP),
    X87Z(FDIVRP_ST1,"fdivrp",X86_IMPLICIT_X87_STACK_BINARY_POP),
    [MINIC_X86_MI_CLD] = { .mnemonic="cld", .required_features=BASE,
        .implicit_kind=X86_IMPLICIT_FLAGS_DEF },
    IR(BTS64_IR,"btsq",64,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(AND8_RR,"andb",8,8,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(OR8_RR,"orb",8,8,R|W,X86_IMPLICIT_FLAGS_DEF),
    RR(TEST8_RR,"testb",8,8,R,X86_IMPLICIT_FLAGS_DEF),

    /* GNU Linux emits the equal/ZF alias `sete` for hardware SETZ. */
    SET(SETZ8_R,"sete"), SET(SETNZ8_R,"setnz"),
    SET(SETB8_R,"setb"), SET(SETBE8_R,"setbe"),
    SET(SETL8_R,"setl"), SET(SETLE8_R,"setle"),
    SET(SETO8_R,"seto"), SET(SETC8_R,"setc"),
    SET(SETP8_R,"setp"), SET(SETNP8_R,"setnp"),
    SET(SETS8_R,"sets"), SET(SETNS8_R,"setns"),
    BRANCH(JMP_B,"jmp"),
    [MINIC_X86_MI_JMP_R] = { .mnemonic="jmp",
        .format=MINIC_X86_MACHINE_FORMAT_R, .first_width_bits=64,
        .first_access=R, .effects=BR, .required_features=BASE,
        .terminates_block=true, .indirect_successors=true },
    SYM(JMP_SYMBOL,"jmp",MINIC_X86_MACHINE_RELOC_PC32,BR,X86_IMPLICIT_NONE),
    SYM(JMP_RETURN_THUNK,"jmp",MINIC_X86_MACHINE_RELOC_PC32,RT,X86_IMPLICIT_NONE),
    SYM(JMP_INDIRECT_THUNK_RAX,"jmp",MINIC_X86_MACHINE_RELOC_PC32,
        BR,X86_IMPLICIT_JMP_THUNK_RAX),
    COND(JZ_B,"jz"), COND(JNZ_B,"jnz"),
    COND(JB_B,"jb"), COND(JBE_B,"jbe"),
    COND(JA_B,"ja"), COND(JAE_B,"jae"),
    COND(JL_B,"jl"), COND(JLE_B,"jle"),
    COND(JG_B,"jg"), COND(JGE_B,"jge"),
    COND(JO_B,"jo"), COND(JNO_B,"jno"),
    COND(JC_B,"jc"), COND(JNC_B,"jnc"),
    COND(JP_B,"jp"), COND(JNP_B,"jnp"),
    COND(JS_B,"js"), COND(JNS_B,"jns"),
    SYM(CALL_SYSV_SYMBOL,"call",MINIC_X86_MACHINE_RELOC_PLT32,
        CA|MR|MW,X86_IMPLICIT_CALL_SYSV),
    SYM(CALL_MS_SYMBOL,"call",MINIC_X86_MACHINE_RELOC_PLT32,
        CA|MR|MW,X86_IMPLICIT_CALL_MS),
    SYM(CALL_SYSV_LOCAL_SYMBOL,"call",MINIC_X86_MACHINE_RELOC_PC32,
        CA|MR|MW,X86_IMPLICIT_CALL_SYSV),
    SYM(CALL_MS_LOCAL_SYMBOL,"call",MINIC_X86_MACHINE_RELOC_PC32,
        CA|MR|MW,X86_IMPLICIT_CALL_MS),
    SYM(CALL_UACCESS_LOCAL_SYMBOL,"call",MINIC_X86_MACHINE_RELOC_PC32,
        CA|MR|MW,X86_IMPLICIT_CALL_UACCESS),
    SYM(CALL_INDIRECT_THUNK_R11,"call",MINIC_X86_MACHINE_RELOC_PC32,
        CA|MR|MW,X86_IMPLICIT_CALL_SYSV_THUNK_R11),
    [MINIC_X86_MI_CALL_SYSV_R] = { .mnemonic="call",
        .format=MINIC_X86_MACHINE_FORMAT_R, .first_width_bits=64,
        .first_access=R, .effects=CA|MR|MW, .required_features=BASE,
        .implicit_kind=X86_IMPLICIT_CALL_SYSV },
    [MINIC_X86_MI_CALL_MS_R] = { .mnemonic="call",
        .format=MINIC_X86_MACHINE_FORMAT_R, .first_width_bits=64,
        .first_access=R, .effects=CA|MR|MW, .required_features=BASE,
        .implicit_kind=X86_IMPLICIT_CALL_MS }
};

#undef Z
#undef RR
#undef IR
#undef AR
#undef RA
#undef FR
#undef RF
#undef SR
#undef REG
#undef COND
#undef SET
#undef BRANCH
#undef SYM
#undef XRR
#undef XAR
#undef XRA
#undef XFR
#undef XRF
#undef X87M
#undef X87Z

const MinicX86MachineOpcodeInfo *minic_x86_64_machine_opcode_info(
    uint32_t opcode) {
    if (opcode == 0U || opcode >= MINIC_X86_MI_LAST ||
        x86_opcodes[opcode].mnemonic == NULL) return NULL;
    return &x86_opcodes[opcode];
}

uint64_t minic_x86_64_machine_generated_features(
    const MinicTargetInfo *target) {
    uint64_t features = BASE;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64)
        return 0U;
    if ((minic_x86_64_target_codegen_options(target) & MINIC_X86_CODEGEN_NO_SSE) == 0U)
        features |= MINIC_X86_MACHINE_GENERATED_SSE2;
    if ((minic_x86_64_target_codegen_options(target) & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U)
        features |= IBT;
    return features;
}

bool minic_x86_64_machine_validate_features(const MinicTargetInfo *target,
    uint64_t features, void *context) {
    (void)context;
    return minic_x86_64_machine_generated_features(target) == features;
}

bool minic_x86_64_machine_validate_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention abi, void *context) {
    (void)context;
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_X86_64 &&
        (abi == MINIC_CALLING_CONVENTION_DEFAULT ||
         (abi == MINIC_CALLING_CONVENTION_MS_X64 &&
          (target->call_lowering_capabilities &
           MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR) != 0U));
}

bool minic_x86_64_machine_validate_frame_object(
    const MinicTargetInfo *target, const MinicMachineFunction *function,
    const MinicMachineFrameObject *object, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        function == NULL || object == NULL || !object->has_fixed_offset)
        return false;
    switch (object->area) {
    case MINIC_MACHINE_FRAME_LOCAL:
        if (object->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER)
            return object->fixed_offset < 0;
        if (object->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER)
            return !function->has_dynamic_stack && object->fixed_offset >= 0;
        return false;
    case MINIC_MACHINE_FRAME_INCOMING:
        /* This backend's prologue establishes RBP before any Core load.
         * ENTRY_SP needs a separate, explicitly modeled prologue offset. */
        return object->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER &&
               object->fixed_offset >= 16;
    case MINIC_MACHINE_FRAME_OUTGOING:
        return object->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER &&
               object->fixed_offset >= 0;
    default:
        return false;
    }
}

bool minic_x86_64_machine_validate_register(const MinicTargetInfo *target,
    const MinicMachineRegister *reg, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        reg == NULL) return false;
    switch (reg->bank) {
    case MINIC_MACHINE_REGISTER_GENERAL:
        return reg->number <= MINIC_X86_REG_RIP &&
            (reg->width_bits == 8U || reg->width_bits == 16U ||
             reg->width_bits == 32U || reg->width_bits == 64U) &&
            (reg->number != MINIC_X86_REG_RIP || reg->width_bits == 64U);
    case MINIC_MACHINE_REGISTER_VECTOR:
        return reg->number < 16U && reg->width_bits == 128U;
    case MINIC_MACHINE_REGISTER_FLOAT:
        return reg->number < 8U && reg->width_bits == 80U;
    case MINIC_MACHINE_REGISTER_FLAGS:
        return reg->number == 0U && reg->width_bits == 64U;
    case MINIC_MACHINE_REGISTER_SYSTEM:
        return reg->number <= MINIC_X86_SEG_SS && reg->width_bits == 16U;
    default:
        return false;
    }
}

bool minic_x86_64_machine_validate_address(const MinicTargetInfo *target,
    const MinicMachineAddress *address, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        address == NULL || address->target_mode != 0U ||
        address->displacement < INT32_MIN ||
        address->displacement > INT32_MAX ||
        !address->has_base ||
        (address->scale != 1U && address->scale != 2U &&
         address->scale != 4U && address->scale != 8U) ||
        (!address->has_index &&
         (address->scale != 1U ||
          address->index.bank != MINIC_MACHINE_REGISTER_INVALID ||
          address->index.number != 0U ||
          address->index.width_bits != 0U)) ||
        (!address->has_segment &&
         (address->segment.bank != MINIC_MACHINE_REGISTER_INVALID ||
          address->segment.number != 0U ||
          address->segment.width_bits != 0U))) return false;
    if (address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.width_bits != 64U ||
        address->base.number > MINIC_X86_REG_RIP) return false;
    if (address->has_index &&
        (address->index.bank != MINIC_MACHINE_REGISTER_GENERAL ||
         address->index.width_bits != 64U ||
         address->index.number >= MINIC_X86_REG_RIP ||
         address->index.number == MINIC_X86_REG_RSP ||
         address->base.number == MINIC_X86_REG_RIP)) return false;
    if (address->has_segment &&
        (address->segment.bank != MINIC_MACHINE_REGISTER_SYSTEM ||
         address->segment.width_bits != 16U ||
         (address->segment.number != MINIC_X86_SEG_GS &&
          address->segment.number != MINIC_X86_SEG_FS))) return false;
    if (address->has_symbol &&
        (address->symbol == NULL || address->symbol[0] == '\0' ||
         address->base.number != MINIC_X86_REG_RIP ||
         address->has_index || address->has_segment ||
         address->displacement != 0 ||
         (address->relocation != MINIC_X86_MACHINE_RELOC_PC32 &&
          address->relocation != MINIC_X86_MACHINE_RELOC_GOTPCREL) ||
         !minic_x86_64_machine_validate_relocation(target,address->symbol,
             address->relocation,address->addend,NULL))) return false;
    if (!address->has_symbol &&
        (address->symbol != NULL || address->relocation != 0U ||
         address->addend != 0)) return false;
    return true;
}

bool minic_x86_64_machine_validate_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t reloc, int64_t addend, void *context) {
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        symbol == NULL || symbol[0] == '\0') return false;
    switch (reloc) {
    case MINIC_X86_MACHINE_RELOC_ABSOLUTE:
        return addend == 0;
    case MINIC_X86_MACHINE_RELOC_ABS32S:
        /* Only the proven early non-PIC head path may use an absolute
         * signed-32 immediate symbol: GAS emits R_X86_64_32S. */
        return addend == 0 && !target->position_independent;
    case MINIC_X86_MACHINE_RELOC_PC32:
        return addend >= INT32_MIN && addend <= INT32_MAX;
    case MINIC_X86_MACHINE_RELOC_GOTPCREL:
    case MINIC_X86_MACHINE_RELOC_PLT32:
        return addend == 0;
    default:
        return false;
    }
}

static bool x86_add_effect(MinicMachineOpcodeContract *out,
    MinicMachineRegisterBank bank, uint32_t number, uint16_t width,
    unsigned int access) {
    MinicMachineRegisterEffect *entry;
    if (out->implicit_count >= MINIC_MACHINE_MAX_REGISTER_EFFECTS)
        return false;
    entry = &out->implicit[out->implicit_count++];
    entry->reg.bank = bank;
    entry->reg.number = number;
    entry->reg.width_bits = width;
    entry->access = access;
    return true;
}

static bool x86_flags(MinicMachineOpcodeContract *out, unsigned int access) {
    return x86_add_effect(out, MINIC_MACHINE_REGISTER_FLAGS, 0U, 64U, access);
}

static bool x86_gp(MinicMachineOpcodeContract *out, uint32_t number,
    uint16_t width, unsigned int access) {
    return x86_add_effect(out, MINIC_MACHINE_REGISTER_GENERAL,
        number, width, access);
}

static bool x86_append_implicit(MinicMachineOpcodeContract *out,
    unsigned int kind) {
    uint32_t i;
    switch (kind) {
    case X86_IMPLICIT_NONE: return true;
    case X86_IMPLICIT_FLAGS_DEF: return x86_flags(out,W);
    case X86_IMPLICIT_FLAGS_USE: return x86_flags(out,R);
    case X86_IMPLICIT_FLAGS_USE_DEF: return x86_flags(out,R|W);
    case X86_IMPLICIT_FLAGS_CLOBBER: return x86_flags(out,C);
    case X86_IMPLICIT_CL_FLAGS_DEF:
        return x86_gp(out,MINIC_X86_REG_RCX,8,R) && x86_flags(out,W);
    case X86_IMPLICIT_STACK_PUSH:
    case X86_IMPLICIT_STACK_POP:
    case X86_IMPLICIT_STACK_RET:
        return x86_gp(out,MINIC_X86_REG_RSP,64,R|W);
    case X86_IMPLICIT_STACK_LEAVE:
        return x86_gp(out,MINIC_X86_REG_RBP,64,R) &&
               x86_gp(out,MINIC_X86_REG_RSP,64,W) &&
               x86_gp(out,MINIC_X86_REG_RBP,64,W);
    case X86_IMPLICIT_MUL:
        return x86_gp(out,MINIC_X86_REG_RAX,64,R|W) &&
               x86_gp(out,MINIC_X86_REG_RDX,64,W) && x86_flags(out,W);
    case X86_IMPLICIT_MUL32:
        return x86_gp(out,MINIC_X86_REG_RAX,32,R|W) &&
               x86_gp(out,MINIC_X86_REG_RDX,32,W) && x86_flags(out,W);
    case X86_IMPLICIT_DIV:
        return x86_gp(out,MINIC_X86_REG_RAX,64,R|W) &&
               x86_gp(out,MINIC_X86_REG_RDX,64,R|W) && x86_flags(out,C);
    case X86_IMPLICIT_CQTO:
        return x86_gp(out,MINIC_X86_REG_RAX,64,R) &&
               x86_gp(out,MINIC_X86_REG_RDX,64,W);
    case X86_IMPLICIT_X87_STACK_PUSH:
    case X86_IMPLICIT_X87_STACK_POP:
    case X86_IMPLICIT_X87_STACK_BINARY_POP:
    case X86_IMPLICIT_X87_STACK_COMPARE_POP:
        /* x87 stack registers are addressed relative to TOP.  Loading or
         * popping shifts the logical names of all eight physical slots. */
        for (i = 0U; i < 8U; ++i)
            if (!x86_add_effect(out,MINIC_MACHINE_REGISTER_FLOAT,i,80,
                C|((kind != X86_IMPLICIT_X87_STACK_PUSH && i == 0U) ||
                   ((kind == X86_IMPLICIT_X87_STACK_BINARY_POP ||
                     kind == X86_IMPLICIT_X87_STACK_COMPARE_POP) && i == 1U)
                    ? R : 0U)))
                return false;
        return kind != X86_IMPLICIT_X87_STACK_COMPARE_POP || x86_flags(out,W);
    case X86_IMPLICIT_X87_STACK_UNARY:
        return x86_add_effect(out,MINIC_MACHINE_REGISTER_FLOAT,0U,80,R|W);
    case X86_IMPLICIT_X87_STACK_SWAP:
        return x86_add_effect(out,MINIC_MACHINE_REGISTER_FLOAT,0U,80,R|W) &&
               x86_add_effect(out,MINIC_MACHINE_REGISTER_FLOAT,1U,80,R|W);
    case X86_IMPLICIT_REP_MOVSB:
        return x86_gp(out,MINIC_X86_REG_RSI,64,R|W) &&
               x86_gp(out,MINIC_X86_REG_RDI,64,R|W) &&
               x86_gp(out,MINIC_X86_REG_RCX,64,R|W) && x86_flags(out,R);
    case X86_IMPLICIT_JMP_THUNK_RAX:
        return x86_gp(out,MINIC_X86_REG_RAX,64,R);
    case X86_IMPLICIT_CALL_UACCESS:
        /* Kernel uaccess helper ABI: one register carries the address,
         * another the payload, while RSP is live and RBX is clobbered by
         * put_user (conservatively model it for get_user too). */
        return x86_gp(out,MINIC_X86_REG_RSP,64,R|W) &&
            x86_gp(out,MINIC_X86_REG_RAX,64,R|W) &&
            x86_gp(out,MINIC_X86_REG_RCX,64,R|W) &&
            x86_gp(out,MINIC_X86_REG_RDX,64,C) &&
            x86_gp(out,MINIC_X86_REG_RBX,64,C) &&
            x86_flags(out,C);
    case X86_IMPLICIT_CALL_SYSV:
    case X86_IMPLICIT_CALL_MS:
    case X86_IMPLICIT_CALL_SYSV_THUNK_R11: {
        static const uint32_t sysv[] = {
            MINIC_X86_REG_RAX,MINIC_X86_REG_RCX,MINIC_X86_REG_RDX,
            MINIC_X86_REG_RSI,MINIC_X86_REG_RDI,MINIC_X86_REG_R8,
            MINIC_X86_REG_R9,MINIC_X86_REG_R10,MINIC_X86_REG_R11
        };
        static const uint32_t ms[] = {
            MINIC_X86_REG_RAX,MINIC_X86_REG_RCX,MINIC_X86_REG_RDX,
            MINIC_X86_REG_R8,MINIC_X86_REG_R9,
            MINIC_X86_REG_R10,MINIC_X86_REG_R11
        };
        const bool is_ms = kind == X86_IMPLICIT_CALL_MS;
        const bool thunk = kind == X86_IMPLICIT_CALL_SYSV_THUNK_R11;
        const uint32_t *clobbers = is_ms ? ms : sysv;
        const size_t count = is_ms ? sizeof(ms)/sizeof(ms[0])
                                   : sizeof(sysv)/sizeof(sysv[0]);
        if (!x86_gp(out,MINIC_X86_REG_RSP,64,R|W)) return false;
        for (i=0U;i<count;++i) {
            const uint32_t reg = clobbers[i];
            const bool arg = is_ms
                ? (reg == MINIC_X86_REG_RCX || reg == MINIC_X86_REG_RDX ||
                   reg == MINIC_X86_REG_R8 || reg == MINIC_X86_REG_R9)
                : (reg == MINIC_X86_REG_RAX || reg == MINIC_X86_REG_RDI ||
                   reg == MINIC_X86_REG_RSI || reg == MINIC_X86_REG_RDX ||
                   reg == MINIC_X86_REG_RCX || reg == MINIC_X86_REG_R8 ||
                   reg == MINIC_X86_REG_R9 ||
                   (thunk && reg == MINIC_X86_REG_R11));
            /* SysV RAX read conservatively covers variadic %al. A call
             * clobbers caller-saved registers regardless of arg count. */
            if (!x86_gp(out,reg,64,C|(arg?R:0U))) return false;
        }
        for (i=0U;i<(is_ms?6U:16U);++i)
            if (!x86_add_effect(out,MINIC_MACHINE_REGISTER_VECTOR,i,128,
                    C|(i<(is_ms?4U:8U)?R:0U)))
                return false;
        return x86_flags(out,C);
    }
    default: return false;
    }
}

bool minic_x86_64_machine_opcode_lookup(const MinicTargetInfo *target,
    uint32_t opcode, MinicMachineOpcodeContract *out, void *context) {
    const MinicX86MachineOpcodeInfo *info =
        minic_x86_64_machine_opcode_info(opcode);
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        info == NULL || out == NULL) return false;
    memset(out,0,sizeof(*out));
    switch (info->format) {
    case MINIC_X86_MACHINE_FORMAT_NONE: break;
    case MINIC_X86_MACHINE_FORMAT_RR:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[0]=info->first_access;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[1]=info->second_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_IR:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_INTEGER;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[1]=info->second_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_AR:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_ADDRESS;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[1]=info->second_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_RA:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[0]=info->first_access;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_ADDRESS;
        break;
    case MINIC_X86_MACHINE_FORMAT_FR:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_FRAME;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[1]=info->second_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_RF:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[0]=info->first_access;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_FRAME;
        break;
    case MINIC_X86_MACHINE_FORMAT_SR:
        out->operand_count=2U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_SYMBOL;
        out->operand_kinds[1]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[1]=info->second_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_R:
        out->operand_count=1U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_REGISTER;
        out->register_access[0]=info->first_access;
        break;
    case MINIC_X86_MACHINE_FORMAT_B:
        out->operand_count=1U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_BLOCK;
        break;
    case MINIC_X86_MACHINE_FORMAT_S:
        out->operand_count=1U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_SYMBOL;
        break;
    case MINIC_X86_MACHINE_FORMAT_A:
        out->operand_count=1U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_ADDRESS;
        break;
    case MINIC_X86_MACHINE_FORMAT_F:
        out->operand_count=1U;
        out->operand_kinds[0]=MINIC_MACHINE_OPERAND_FRAME;
        break;
    default: return false;
    }
    out->required_generated_features=info->required_features;
    out->required_effects=info->effects;
    /* Selected physical memory instructions may preserve a Core volatile
     * access as an additional ordering fact.  Volatile is not a second
     * instruction or a GNU text escape: the same validated load/store owns
     * both the physical memory effect and the compiler-side barrier. */
    out->permitted_effects=info->effects |
        ((info->effects & (MR|MW)) != 0U
            ? MINIC_MACHINE_EFFECT_VOLATILE : 0U);
    out->terminates_block=info->terminates_block;
    out->permits_fallthrough=info->permits_fallthrough;
    out->has_indirect_successors=info->indirect_successors;
    return x86_append_implicit(out,info->implicit_kind);
}

static bool x86_register_width(const MinicMachineOperand *operand,
    unsigned int width, MinicMachineRegisterBank required_bank) {
    return operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        operand->value.physical.reg.bank ==
            (required_bank == MINIC_MACHINE_REGISTER_INVALID
                 ? MINIC_MACHINE_REGISTER_GENERAL : required_bank) &&
        operand->value.physical.reg.number <
            (required_bank == MINIC_MACHINE_REGISTER_VECTOR ? 16U
             : required_bank == MINIC_MACHINE_REGISTER_FLOAT ? 8U
             : MINIC_X86_REG_RIP) &&
        operand->value.physical.reg.width_bits == width;
}

static bool x86_imm_fits(int64_t value, unsigned int bits) {
    switch (bits) {
    case 8U: return value >= -128 && value <= 255;
    case 16U: return value >= INT16_MIN && value <= UINT16_MAX;
    case 32U: return value >= INT32_MIN && value <= UINT32_MAX;
    case 64U: return true;
    default: return false;
    }
}

static bool x86_frame_encodable(const MinicMachineFunction *function,
    const MinicMachineOperand *operand,
    const MinicX86MachineOpcodeInfo *opcode, size_t access_bytes) {
    const MinicMachineFrameObject *obj;
    int64_t base,delta,effective;
    if (function == NULL || operand == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        operand->value.frame.index >= function->frame_object_count)
        return false;
    obj = &function->frame_objects[operand->value.frame.index];
    if ((opcode->frame_base == 1U &&
         obj->base != MINIC_MACHINE_FRAME_BASE_FRAME_POINTER) ||
        (opcode->frame_base == 2U &&
         obj->base != MINIC_MACHINE_FRAME_BASE_STACK_POINTER)) return false;
    if (!minic_x86_64_machine_validate_frame_object(function->target,
        function,obj,NULL) || obj->size_bytes > INT64_MAX) return false;
    base = obj->fixed_offset;
    delta = operand->value.frame.displacement;
    if (delta < 0 || (uint64_t)delta > obj->size_bytes ||
        access_bytes > obj->size_bytes - (size_t)delta ||
        (delta > 0 && base > INT64_MAX - delta) ||
        (delta < 0 && base < INT64_MIN - delta)) return false;
    effective = base + delta;
    return effective >= INT32_MIN && effective <= INT32_MAX;
}

bool minic_x86_64_machine_validate_selected(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    const MinicX86MachineOpcodeInfo *info;
    const MinicMachineOperand *first,*second;
    const MinicTargetInfo *target;
    (void)context;
    if (function == NULL ||
        (target=function->target) == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        instruction == NULL ||
        (info=minic_x86_64_machine_opcode_info(instruction->opcode)) == NULL)
        return false;
    first = instruction->operand_count>0U ? &instruction->operands[0] : NULL;
    second = instruction->operand_count>1U ? &instruction->operands[1] : NULL;
    if ((instruction->opcode == MINIC_X86_MI_CALL_MS_SYMBOL ||
         instruction->opcode == MINIC_X86_MI_CALL_MS_LOCAL_SYMBOL ||
         instruction->opcode == MINIC_X86_MI_CALL_MS_R) &&
        (target->call_lowering_capabilities &
         MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR) == 0U) return false;
    switch (info->format) {
    case MINIC_X86_MACHINE_FORMAT_NONE: return instruction->operand_count == 0U;
    case MINIC_X86_MACHINE_FORMAT_RR:
        return instruction->operand_count == 2U &&
            x86_register_width(first,info->first_width_bits,info->first_bank) &&
            x86_register_width(second,info->second_width_bits,info->second_bank);
    case MINIC_X86_MACHINE_FORMAT_IR: {
        int64_t value;
        const unsigned int bits =
            instruction->opcode == MINIC_X86_MI_MOVABS64_IR ? 64U :
            info->second_width_bits == 64U ? 32U : info->second_width_bits;
        if (instruction->operand_count != 2U ||
            first->kind != MINIC_MACHINE_OPERAND_INTEGER ||
            !x86_register_width(second,info->second_width_bits,info->second_bank)) return false;
        value = first->value.integer;
        if (instruction->opcode == MINIC_X86_MI_BTS64_IR)
            return value >= 0 && value <= 255;
        if (info->second_width_bits == 64U && bits == 32U)
            return value >= INT32_MIN && value <= INT32_MAX;
        return x86_imm_fits(value,bits);
    }
    case MINIC_X86_MACHINE_FORMAT_AR:
        return instruction->operand_count == 2U &&
            first->kind == MINIC_MACHINE_OPERAND_ADDRESS &&
            minic_x86_64_machine_validate_address(target,
                &first->value.address,NULL) &&
            x86_register_width(second,info->second_width_bits,info->second_bank) &&
            (instruction->opcode != MINIC_X86_MI_LEA64_AR ||
             !first->value.address.has_segment);
    case MINIC_X86_MACHINE_FORMAT_RA:
        return instruction->operand_count == 2U &&
            x86_register_width(first,info->first_width_bits,info->first_bank) &&
            second->kind == MINIC_MACHINE_OPERAND_ADDRESS &&
            minic_x86_64_machine_validate_address(target,
                &second->value.address,NULL);
    case MINIC_X86_MACHINE_FORMAT_FR:
    case MINIC_X86_MACHINE_FORMAT_RF:
        if (instruction->operand_count != 2U) return false;
        if (info->format == MINIC_X86_MACHINE_FORMAT_FR)
            return x86_register_width(second,info->second_width_bits,info->second_bank) &&
                x86_frame_encodable(function,first,info,
                    instruction->opcode == MINIC_X86_MI_LEA64_FRBP_R ||
                    instruction->opcode == MINIC_X86_MI_LEA64_FRSP_R
                    ? 0U : info->second_width_bits/8U);
        return x86_register_width(first,info->first_width_bits,info->first_bank) &&
            x86_frame_encodable(function,second,info,
                info->first_width_bits/8U);
    case MINIC_X86_MACHINE_FORMAT_SR:
        return instruction->operand_count == 2U &&
            first->kind == MINIC_MACHINE_OPERAND_SYMBOL &&
            first->value.symbol.relocation == info->relocation &&
            x86_register_width(second,64U,MINIC_MACHINE_REGISTER_GENERAL);
    case MINIC_X86_MACHINE_FORMAT_R:
        return instruction->operand_count == 1U &&
            x86_register_width(first,info->first_width_bits,info->first_bank) &&
            ((instruction->opcode != MINIC_X86_MI_JMP_R &&
              instruction->opcode != MINIC_X86_MI_CALL_SYSV_R &&
              instruction->opcode != MINIC_X86_MI_CALL_MS_R) ||
             (minic_x86_64_target_codegen_options(target) &
                 (MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK |
                  MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK)) == 0U);
    case MINIC_X86_MACHINE_FORMAT_B:
        return instruction->operand_count == 1U &&
            first->kind == MINIC_MACHINE_OPERAND_BLOCK;
    case MINIC_X86_MACHINE_FORMAT_S:
        if (instruction->operand_count != 1U ||
            first->kind != MINIC_MACHINE_OPERAND_SYMBOL ||
            first->value.symbol.name == NULL ||
            first->value.symbol.relocation != info->relocation)
            return false;
        if (instruction->opcode == MINIC_X86_MI_JMP_RETURN_THUNK)
            return (minic_x86_64_target_codegen_options(target) &
                MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK) != 0U &&
                strcmp(first->value.symbol.name,"__x86_return_thunk") == 0;
        if (instruction->opcode == MINIC_X86_MI_JMP_INDIRECT_THUNK_RAX)
            return (minic_x86_64_target_codegen_options(target) &
                MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U &&
                strcmp(first->value.symbol.name,"__x86_indirect_thunk_rax") == 0;
        if (instruction->opcode == MINIC_X86_MI_CALL_INDIRECT_THUNK_R11)
            return (minic_x86_64_target_codegen_options(target) &
                MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U &&
                strcmp(first->value.symbol.name,"__x86_indirect_thunk_r11") == 0;
        return true;
    case MINIC_X86_MACHINE_FORMAT_A:
        return instruction->operand_count == 1U &&
            first->kind == MINIC_MACHINE_OPERAND_ADDRESS &&
            minic_x86_64_machine_validate_address(target,
                &first->value.address,NULL);
    case MINIC_X86_MACHINE_FORMAT_F:
        return instruction->operand_count == 1U &&
            x86_frame_encodable(function,first,info,
                info->memory_width_bits/8U);
    default: return false;
    }
}

/* The two unsigned accumulator forms share a physical GNU register
 * contract.  The Source qualifier independently proved their C operands;
 * here we re-prove the exact hardware template, four physical bindings,
 * RDX status, and effects.  A forged FormId cannot authorize raw text. */
static bool x86_source_accumulator_math_contract(
    const MinicMachineInstruction *instruction) {
    const bool muldiv = instruction->source_asm_machine_form_id ==
        MINIC_X86_MACHINE_ASM_MULDIV_U64;
    const unsigned int regs[2][4] = {
        {MINIC_X86_REG_RAX, MINIC_X86_REG_RDX,
         MINIC_X86_REG_RAX, MINIC_X86_REG_R10},
        {MINIC_X86_REG_RAX, MINIC_X86_REG_RAX,
         MINIC_X86_REG_R10, MINIC_X86_REG_R11}
    };
    const char *const constraints[2][4] = {
        {"=a", "=d", "a", "rm"},
        {"=a", "a", "rm", "rm"}
    };
    size_t i;
    const unsigned int index = muldiv ? 1U : 0U;
    if (!muldiv && instruction->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_KASLR_MUL) return false;
    if (instruction->operand_count != 4U ||
        instruction->operands == NULL ||
        instruction->source_asm_terminates_block ||
        instruction->source_asm_permits_fallthrough ||
        instruction->effects !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             MINIC_MACHINE_EFFECT_VOLATILE) ||
        !minic_x86_64_typed_machine_form_matches_template(
            instruction->source_asm_machine_form_id,
            instruction->source_asm_machine_form_variant,
            instruction->source_asm_template,
            instruction->source_asm_length) ||
        instruction->implicit_count != (muldiv ? 1U : 0U) ||
        (muldiv && (instruction->implicit == NULL ||
            instruction->implicit[0].reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            instruction->implicit[0].reg.number != MINIC_X86_REG_RDX ||
            instruction->implicit[0].reg.width_bits != 64U ||
            instruction->implicit[0].access !=
                MINIC_MACHINE_REGISTER_CLOBBER))) return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicMachineOperand *operand = &instruction->operands[i];
        const unsigned int expected =
            i < (muldiv ? 1U : 2U) ? MINIC_MACHINE_REGISTER_WRITE :
                                         MINIC_MACHINE_REGISTER_READ;
        if (operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            operand->asm_constraint == NULL ||
            strcmp(operand->asm_constraint, constraints[index][i]) != 0 ||
            operand->value.physical.reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            operand->value.physical.reg.number != regs[index][i] ||
            operand->value.physical.reg.width_bits != 64U ||
            operand->value.physical.access != expected ||
            operand->asm_name != NULL) return false;
    }
    return true;
}

/* Exact physical bindings of GNU matched-register source forms. A source
 * form ID alone grants no permission: source syntax, operand alternatives,
 * effects, register widths, memory address and clobbers must all match the
 * sealed hardware execution. The Source qualifier independently checks C
 * lvalues and the original constraints; here only physical ISA is owned. */
static bool x86_source_matched_contract(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    static const unsigned int cpuid_regs[4] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RBX,
        MINIC_X86_REG_RCX, MINIC_X86_REG_RDX
    };
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const unsigned int mulacc_regs[7] = {
        MINIC_X86_REG_R8, MINIC_X86_REG_R9, MINIC_X86_REG_RCX,
        MINIC_X86_REG_R8, MINIC_X86_REG_R9, MINIC_X86_REG_RCX,
        MINIC_X86_REG_RAX
    };
    static const char *const mulacc_constraints[8] = {
        "=rm","=rm","=rm","0","1","2","a","m"
    };
    const bool cpuid = ins->source_asm_machine_form_id ==
        MINIC_X86_MACHINE_ASM_CPUID_MATCHED;
    const size_t count = cpuid ? ins->source_asm_machine_form_variant : 8U;
    const unsigned effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        (cpuid && count == 6U ? MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U) |
        (!cpuid ? MINIC_MACHINE_EFFECT_MEMORY_READ : 0U);
    size_t i;
    if (!cpuid && ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_MULACC_MATCHED) return false;
    if ((cpuid && count != 6U && count != 8U) ||
        (!cpuid && ins->source_asm_machine_form_variant != 1U) ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != effects ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,
            ins->source_asm_length) ||
        ins->implicit_count != (cpuid ? 0U : 2U) ||
        (cpuid && ins->implicit != NULL)) return false;
    if (!cpuid) {
        bool have_rdx = false, have_flags = false;
        if (ins->implicit == NULL) return false;
        for (i = 0U; i < 2U; ++i) {
            const MinicMachineRegisterEffect *effect = &ins->implicit[i];
            if (effect->access != MINIC_MACHINE_REGISTER_CLOBBER)
                return false;
            if (effect->reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
                effect->reg.number == MINIC_X86_REG_RDX &&
                effect->reg.width_bits == 64U && !have_rdx)
                have_rdx = true;
            else if (effect->reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
                     effect->reg.number == 0U &&
                     effect->reg.width_bits == 64U && !have_flags)
                have_flags = true;
            else return false;
        }
        if (!have_rdx || !have_flags) return false;
    }
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        unsigned int reg_number;
        const char *constraint;
        const unsigned int access = i < (cpuid ? 4U : 3U) ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
        if (op->asm_name != NULL || op->asm_constraint == NULL)
            return false;
        if (cpuid) {
            reg_number = i < 4U ? cpuid_regs[i] :
                cpuid_regs[count == 6U && i == 5U ? 2U : i-4U];
            constraint = i < 4U ? cpuid_out[i] :
                cpuid_in[count == 6U && i == 5U ? 2U : i-4U];
        } else {
            reg_number = i == 7U ? MINIC_X86_REG_R11 : mulacc_regs[i];
            constraint = mulacc_constraints[i];
        }
        if (strcmp(op->asm_constraint, constraint) != 0) return false;
        if (!cpuid && i == 7U) {
            const MinicMachineAddress *address;
            if (op->kind != MINIC_MACHINE_OPERAND_ADDRESS) return false;
            address = &op->value.address;
            if (!minic_x86_64_machine_validate_address(target, address, NULL) ||
                address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                address->base.number != MINIC_X86_REG_R11 ||
                address->base.width_bits != 64U ||
                address->has_index || address->has_segment ||
                address->has_symbol || address->displacement != 0)
                return false;
            continue;
        }
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != reg_number ||
            op->value.physical.reg.width_bits != (cpuid ? 32U : 64U) ||
            op->value.physical.access != access)
            return false;
    }
    return true;
}

/* The GNU matched-memory family is register transport around actual memory
 * traffic, not a Core instruction copied as assembler syntax. For 7/5/2
 * shapes it uses caller-scratch R8/R9/R10/RCX, reserving R11 for address
 * materialization after the source's entire carry chain. */
static bool x86_source_matched_memory_contract(
    const MinicMachineInstruction *ins) {
    static const unsigned int regs[4] = {
        MINIC_X86_REG_R8, MINIC_X86_REG_R9,
        MINIC_X86_REG_R10, MINIC_X86_REG_RCX
    };
    static const char *const ties[] = {"0","1","2"};
    const size_t count = ins->source_asm_machine_form_variant;
    const size_t noutputs = count == 7U ? 4U :
        count == 5U ? 3U : count == 2U ? 1U : 0U;
    size_t i;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_MATCHED_MEMORY || noutputs == 0U ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             MINIC_MACHINE_EFFECT_VOLATILE |
             MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
             MINIC_MACHINE_EFFECT_MEMORY_READ |
             MINIC_MACHINE_EFFECT_MEMORY_WRITE) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template, ins->source_asm_length) ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)
        return false;
    for (i = 0U; i < count; ++i) {
        const bool output = i < noutputs;
        const size_t reg_index = output ? i : i - noutputs;
        const MinicMachineOperand *operand = &ins->operands[i];
        const char *expected = output ? "=r" : ties[reg_index];
        if (reg_index >= 4U || operand->asm_name != NULL ||
            operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            operand->asm_constraint == NULL ||
            strcmp(operand->asm_constraint, expected) != 0 ||
            operand->value.physical.reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            operand->value.physical.reg.number != regs[reg_index] ||
            operand->value.physical.reg.width_bits != 64U ||
            operand->value.physical.access != (output ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

/* GNU bit scans use the implicit FLAGS producer and a fixed pair of physical
 * registers.  Source's five width/tied/read-write shapes remain distinct;
 * the verifier rejects a forged variant or a binding alias mismatch. */
static bool x86_source_bit_scan_contract(
    const MinicMachineInstruction *ins) {
    const unsigned int variant = ins->source_asm_machine_form_variant;
    const bool tied = variant == MINIC_X86_BIT_SCAN_BSF32_TIED ||
        variant == MINIC_X86_BIT_SCAN_BSR32_TIED;
    const bool readwrite = variant == MINIC_X86_BIT_SCAN_BSR64_READWRITE;
    const unsigned int width = tied ? 32U : 64U;
    const size_t count = tied ? 3U : 2U;
    const MinicMachineOperand *op;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED ||
        variant < MINIC_X86_BIT_SCAN_BSF64 ||
        variant > MINIC_X86_BIT_SCAN_BSR64_READWRITE ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id, variant,
            ins->source_asm_template, ins->source_asm_length) ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)
        return false;
    op = &ins->operands[0];
    if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        op->asm_name != NULL || op->asm_constraint == NULL ||
        strcmp(op->asm_constraint, readwrite ? "+r" : "=r") != 0 ||
        op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        op->value.physical.reg.number != MINIC_X86_REG_RAX ||
        op->value.physical.reg.width_bits != width ||
        op->value.physical.access != (readwrite ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_WRITE)) return false;
    op = &ins->operands[1];
    if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        op->asm_name != NULL || op->asm_constraint == NULL ||
        (strcmp(op->asm_constraint, "rm") != 0 &&
         strcmp(op->asm_constraint, "r") != 0) ||
        op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        op->value.physical.reg.number != MINIC_X86_REG_R10 ||
        op->value.physical.reg.width_bits != width ||
        op->value.physical.access != MINIC_MACHINE_REGISTER_READ)
        return false;
    if (tied) {
        op = &ins->operands[2];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint, "0") != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != MINIC_X86_REG_RAX ||
            op->value.physical.reg.width_bits != 32U ||
            op->value.physical.access != MINIC_MACHINE_REGISTER_READ)
            return false;
    }
    return true;
}

/* Linux's source-qualified prefetch alternative is an ELF patching protocol,
 * not unconditional PREFETCHW. Keep its exact two physical operands and
 * source-vetted template before allowing section/label serialization. */
static bool x86_source_prefetchw_alt_contract(
    const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    const MinicMachineOperand *imm,*mem;
    const MinicMachineAddress *address;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_PREFETCHW_ALT ||
        ins->source_asm_machine_form_variant != 1U ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_MEMORY_READ) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template, ins->source_asm_length) ||
        ins->implicit_count != 0U || ins->implicit != NULL)
        return false;
    imm = &ins->operands[0];
    mem = &ins->operands[1];
    if (imm->kind != MINIC_MACHINE_OPERAND_INTEGER ||
        imm->value.integer != 0 || imm->asm_name != NULL ||
        imm->asm_constraint == NULL ||
        strcmp(imm->asm_constraint,"i") != 0 ||
        mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_name != NULL || mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,"m") != 0)
        return false;
    address = &mem->value.address;
    return minic_x86_64_machine_validate_address(target,address,NULL) &&
        address->base.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        address->base.number == MINIC_X86_REG_R10 &&
        address->base.width_bits == 64U &&
        !address->has_index && !address->has_segment &&
        !address->has_symbol && address->displacement == 0;
}

/* CMPXCHG16B is one indivisible 128-bit hardware read/conditional write.
 * Its condition-code output, implicit flags, fixed four-register ABI and
 * patchable SMP/GS alternate section must all be proven together. No
 * source FormId alone authorizes section directives or a private CALL. */
static bool x86_source_cmpxchg128_contract(
    const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    static const unsigned int registers[4] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX,
        MINIC_X86_REG_RBX, MINIC_X86_REG_RCX
    };
    static const char *const constraints[4] = {"+a","+d","b","c"};
    const unsigned int variant = ins->source_asm_machine_form_variant;
    const bool alt = variant == 2U;
    const size_t at = alt ? 0U : 1U;
    const size_t count = alt ? 5U : 6U;
    const MinicMachineOperand *mem;
    const MinicMachineAddress *address;
    size_t i;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_CMPXCHG128 ||
        (variant != 1U && variant != 2U) ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
            MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE |
            (alt ? MINIC_MACHINE_EFFECT_CALL : 0U)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id, variant,
            ins->source_asm_template, ins->source_asm_length) ||
        ins->implicit_count != (alt ? 2U : 1U) ||
        ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER ||
        (alt && (ins->implicit[1].reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            ins->implicit[1].reg.number != MINIC_X86_REG_RSI ||
            ins->implicit[1].reg.width_bits != 64U ||
            ins->implicit[1].access != MINIC_MACHINE_REGISTER_CLOBBER)))
        return false;
    if (!alt) {
        const MinicMachineOperand *flags = &ins->operands[0];
        if (flags->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            flags->asm_name != NULL ||
            flags->asm_constraint == NULL ||
            strcmp(flags->asm_constraint,"=@cce") != 0 ||
            flags->value.physical.reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
            flags->value.physical.reg.number != 0U ||
            flags->value.physical.reg.width_bits != 64U ||
            flags->value.physical.access != MINIC_MACHINE_REGISTER_WRITE)
            return false;
    }
    mem = &ins->operands[at];
    if (mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,"+m") != 0 ||
        mem->asm_name == NULL ||
        strcmp(mem->asm_name,alt ? "var" : "ptr") != 0)
        return false;
    address = &mem->value.address;
    if (!minic_x86_64_machine_validate_address(target,address,NULL) ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.number != MINIC_X86_REG_R11 ||
        address->base.width_bits != 64U ||
        address->has_index || address->has_segment ||
        address->has_symbol || address->displacement != 0)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicMachineOperand *op = &ins->operands[at+i+1U];
        const unsigned int access = i < 2U ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_READ;
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraints[i]) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != registers[i] ||
            op->value.physical.reg.width_bits != 64U ||
            op->value.physical.access != access) return false;
    }
    return true;
}

/* Linux crash_setup_regs snapshots all sixteen architectural GPRs, flags
 * and RIP. The Source qualifier seals the precise GNU instruction and typed
 * variant, while this machine contract seals the captured register and the
 * distinct R10 scratch required when the source itself reads R11. */
static bool x86_source_crash_register_contract(
    const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    static const unsigned int gpr[16] = {
        MINIC_X86_REG_RBX, MINIC_X86_REG_RCX,
        MINIC_X86_REG_RDX, MINIC_X86_REG_RSI,
        MINIC_X86_REG_RDI, MINIC_X86_REG_RBP,
        MINIC_X86_REG_RAX, MINIC_X86_REG_RSP,
        MINIC_X86_REG_R8, MINIC_X86_REG_R9,
        MINIC_X86_REG_R10, MINIC_X86_REG_R11,
        MINIC_X86_REG_R12, MINIC_X86_REG_R13,
        MINIC_X86_REG_R14, MINIC_X86_REG_R15
    };
    const unsigned int variant = ins->source_asm_machine_form_variant;
    const bool memory_output = variant <= 17U;
    const MinicMachineOperand *operand;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_CRASH_REGISTER ||
        variant == 0U || variant > 18U ||
        ins->operand_count != 1U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (memory_output ?
            ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                MINIC_MACHINE_EFFECT_VOLATILE |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE) :
            (ins->effects != MINIC_MACHINE_EFFECT_SOURCE_ASM &&
             ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                 MINIC_MACHINE_EFFECT_VOLATILE))) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id, variant,
            ins->source_asm_template, ins->source_asm_length) ||
        ins->implicit_count != (memory_output ? 1U : 0U) ||
        (memory_output && ins->implicit == NULL) ||
        (!memory_output && ins->implicit != NULL)) return false;
    operand = &ins->operands[0];
    if (operand->asm_name != NULL || operand->asm_constraint == NULL ||
        strcmp(operand->asm_constraint,memory_output ? "=m" : "=r") != 0)
        return false;
    if (!memory_output) return
        operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        operand->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        operand->value.physical.reg.number == MINIC_X86_REG_RAX &&
        operand->value.physical.reg.width_bits == 64U &&
        operand->value.physical.access == MINIC_MACHINE_REGISTER_WRITE;
    {
        const MinicMachineAddress *address;
        const MinicMachineRegisterEffect *effect = &ins->implicit[0];
        if (operand->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
            effect->reg.bank != (variant == 17U ?
                MINIC_MACHINE_REGISTER_FLAGS : MINIC_MACHINE_REGISTER_GENERAL) ||
            effect->reg.number != (variant == 17U ? 0U : gpr[variant-1U]) ||
            effect->reg.width_bits != 64U ||
            effect->access != MINIC_MACHINE_REGISTER_READ) return false;
        address = &operand->value.address;
        return minic_x86_64_machine_validate_address(target,address,NULL) &&
            address->base.bank == MINIC_MACHINE_REGISTER_GENERAL &&
            address->base.number == (variant == 12U ?
                MINIC_X86_REG_R10 : MINIC_X86_REG_R11) &&
            address->base.width_bits == 64U &&
            !address->has_index && !address->has_segment &&
            !address->has_symbol && address->displacement == 0;
    }
}

/* GNU Linux futex type-17 fault recovery is an atomic ISA operation plus
 * PREL32 exception metadata. Variant 1 is cmpxchg, 2 xchg, 3 xadd, 4-6 the
 * OR/AND/XOR cmpxchg retry loops. Every variant traps into R8D (-EFAULT);
 * none may escape merely because its Source text mentions an extable macro. */
static bool x86_source_futex_extable_contract(
    const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    const unsigned int variant = ins->source_asm_machine_form_variant;
    const bool loop = variant >= 4U && variant <= 6U;
    const size_t count = loop ? 6U : 5U;
    const size_t output_count = loop ? 4U : 3U;
    const MinicMachineOperand *mem;
    const MinicMachineAddress *address;
    const unsigned int registers[6] = {
        variant == 1U ? MINIC_X86_REG_R8 : MINIC_X86_REG_RAX,
        variant == 1U ? MINIC_X86_REG_RAX : MINIC_X86_REG_R8,
        MINIC_X86_REG_R10,
        loop ? MINIC_X86_REG_R9 : variant == 1U ?
            MINIC_X86_REG_R9 : MINIC_X86_REG_RAX,
        loop ? MINIC_X86_REG_R11 : variant == 1U ?
            MINIC_X86_REG_RAX : MINIC_X86_REG_R8,
        MINIC_X86_REG_R8
    };
    const char *const constraints[6] = {
        variant == 1U ? "+r" : loop ? "=&a" : "=r",
        variant == 1U ? "=a" : loop ? "=&r" : "=r",
        "+m",
        loop ? "=&r" : variant == 1U ? "r" : "0",
        loop ? "r" : "1",
        "1"
    };
    size_t i;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_FUTEX_EXTABLE ||
        variant == 0U || variant > 6U ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,variant,
            ins->source_asm_template,ins->source_asm_length) ||
        ins->implicit_count != (variant == 2U ? 0U : 1U) ||
        (variant == 2U && ins->implicit != NULL) ||
        (variant != 2U && (ins->implicit == NULL ||
            ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
            ins->implicit[0].reg.number != 0U ||
            ins->implicit[0].reg.width_bits != 64U ||
            ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)))
        return false;
    mem = &ins->operands[2];
    if (mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_name != NULL || mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,"+m") != 0) return false;
    address = &mem->value.address;
    if (!minic_x86_64_machine_validate_address(target,address,NULL) ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.number != MINIC_X86_REG_R10 ||
        address->base.width_bits != 64U ||
        address->has_index || address->has_segment ||
        address->has_symbol || address->displacement != 0) return false;
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *operand = &ins->operands[i];
        const unsigned int access = i >= output_count ?
            MINIC_MACHINE_REGISTER_READ :
            i == 0U && variant == 1U ?
                MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_WRITE;
        if (i == 2U) continue;
        if (operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            operand->asm_name != NULL || operand->asm_constraint == NULL ||
            strcmp(operand->asm_constraint,constraints[i]) != 0 ||
            operand->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            operand->value.physical.reg.number != registers[i] ||
            operand->value.physical.reg.width_bits != 32U ||
            operand->value.physical.access != access)
            return false;
    }
    return true;
}

/* The five original Linux GS per-CPU tied-ALU operations each admit a
 * 32-bit and a 64-bit physical width. The source's `0` tied seed lives in
 * EAX/RAX; the GS segment address is not an ordinary process pointer. */
static bool x86_source_gs_tied_alu_contract(
    const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    const unsigned int form = ins->source_asm_machine_form_variant;
    const MinicMachineOperand *out,*mem,*tied;
    const MinicMachineAddress *address;
    unsigned int width;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_GS_TIED_ALU ||
        form < 1U || form > 5U ||
        ins->operand_count != 3U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                             MINIC_MACHINE_EFFECT_MEMORY_READ) &&
         ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                             MINIC_MACHINE_EFFECT_VOLATILE |
                             MINIC_MACHINE_EFFECT_MEMORY_READ)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,form,
            ins->source_asm_template,ins->source_asm_length) ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)
        return false;
    out = &ins->operands[0];
    mem = &ins->operands[1];
    tied = &ins->operands[2];
    if (out->kind != MINIC_MACHINE_OPERAND_REGISTER) return false;
    width = out->value.physical.reg.width_bits;
    if ((width != 32U && width != 64U) ||
        out->asm_name != NULL || out->asm_constraint == NULL ||
        strcmp(out->asm_constraint,"=r") != 0 ||
        out->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        out->value.physical.reg.number != MINIC_X86_REG_RAX ||
        out->value.physical.access != MINIC_MACHINE_REGISTER_WRITE ||
        mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_name != NULL || mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,"m") != 0 ||
        tied->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        tied->asm_name != NULL || tied->asm_constraint == NULL ||
        strcmp(tied->asm_constraint,"0") != 0 ||
        tied->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        tied->value.physical.reg.number != MINIC_X86_REG_RAX ||
        tied->value.physical.reg.width_bits != width ||
        tied->value.physical.access != MINIC_MACHINE_REGISTER_READ)
        return false;
    address = &mem->value.address;
    return minic_x86_64_machine_validate_address(target,address,NULL) &&
        address->base.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        address->base.number == MINIC_X86_REG_R10 &&
        address->base.width_bits == 64U &&
        address->has_segment &&
        address->segment.number == MINIC_X86_SEG_GS &&
        !address->has_index && !address->has_symbol &&
        address->displacement == 0;
}

/* Linux _THIS_IP_ is not an absolute symbol constant: the architectural
 * instruction computes RIP at the source CALLSITE. Its single GNU =r output
 * is selected into RAX before the ordinary typed store into the C lvalue. */
static bool x86_source_ip_here_contract(const MinicMachineInstruction *ins) {
    const MinicMachineOperand *output;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_IP_HERE ||
        ins->source_asm_machine_form_variant != 1U ||
        ins->operand_count != 1U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (ins->effects != MINIC_MACHINE_EFFECT_SOURCE_ASM &&
         ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             MINIC_MACHINE_EFFECT_VOLATILE)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length) ||
        ins->implicit_count != 0U || ins->implicit != NULL)
        return false;
    output = &ins->operands[0];
    return output->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        output->asm_name == NULL && output->asm_constraint != NULL &&
        strcmp(output->asm_constraint,"=r") == 0 &&
        output->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        output->value.physical.reg.number == MINIC_X86_REG_RAX &&
        output->value.physical.reg.width_bits == 64U &&
        output->value.physical.access == MINIC_MACHINE_REGISTER_WRITE;
}

/* Privileged Linux MSR instructions have exact source-qualified templates,
 * but the machine verifier still independently proves the physical ECX,
 * EAX, EDX and safe-error bindings before GNU is allowed to emit bytes. */
/* Physical proof for the original Linux checksum CF loop and SMP XADD.
 * Both have exact Source-owned grammar, but independent machine proofs here
 * rule out forged register widths, ties, lock metadata and memory effects. */

/* CF-sensitive checksum operands are independently sealed before the GNU
 * serializer prints any ADD/ADC instruction.  MachineIR has no C types:
 * only the exact physical register, memory, ties and FLAGS effects remain. */
static bool x86_source_checksum_carry_contract(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins) {
    static const unsigned int tcp_regs[5] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R10, MINIC_X86_REG_R11,
        MINIC_X86_REG_R8, MINIC_X86_REG_RAX
    };
    static const unsigned int partial_regs[5] = {
        MINIC_X86_REG_R10, MINIC_X86_REG_R11, MINIC_X86_REG_R8,
        MINIC_X86_REG_R9, MINIC_X86_REG_RCX
    };
    const unsigned int form_id = ins->source_asm_machine_form_id;
    const bool partial = form_id == MINIC_X86_MACHINE_ASM_CSUM_PARTIAL;
    const unsigned int form = partial ?
        minic_x86_64_linux_csum_partial_form(ins->source_asm_template,
            ins->source_asm_length) : 0U;
    const unsigned int bits = partial ? 64U : 32U;
    const size_t count = partial ? (form == 1U || form == 6U ? 2U :
        form == 2U ? 6U : 3U) :
        form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD ? 5U : 3U;
    size_t i;
    if (form_id != MINIC_X86_MACHINE_ASM_ADD32_CARRY &&
        form_id != MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD &&
        form_id != MINIC_X86_MACHINE_ASM_CHECKSUM && !partial)
        return false;
    if ((partial && (form == 0U || form > 6U)) ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        !minic_x86_64_typed_machine_form_matches_template(
            form_id,0U,ins->source_asm_template,ins->source_asm_length) ||
        (ins->effects &
            ~(unsigned int)MINIC_MACHINE_EFFECT_COMPILER_BARRIER) !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             (partial && form >= 2U && form <= 5U ?
                 MINIC_MACHINE_EFFECT_MEMORY_READ : 0U)) ||
        ((ins->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U &&
            !(partial && form >= 2U && form <= 5U)) ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        unsigned int reg = MINIC_X86_REG_RAX;
        const char *constraint;
        const bool memory = partial &&
            ((form == 2U && i > 0U) ||
             (form >= 3U && form <= 5U && i == 2U));
        if (partial) {
            reg = i == 0U ? MINIC_X86_REG_RAX :
                form == 2U ? partial_regs[i-1U] :
                i == 1U ? MINIC_X86_REG_R10 : MINIC_X86_REG_R11;
            constraint = i == 0U ? "+r" : memory ? "m" : "r";
        } else if (form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD) {
            reg = tcp_regs[i];
            constraint = i == 0U ? "=r" : i == 4U ? "0" : "g";
        } else if (form_id == MINIC_X86_MACHINE_ASM_ADD32_CARRY) {
            reg = i == 2U ? MINIC_X86_REG_R10 : MINIC_X86_REG_RAX;
            constraint = i == 0U ? "=r" : i == 1U ? "0" : "rm";
        } else {
            reg = i == 1U ? MINIC_X86_REG_R10 : MINIC_X86_REG_RAX;
            constraint = i == 0U ? "=r" : i == 1U ? "r" : "0";
        }
        if (op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraint) != 0)
            return false;
        if (memory) {
            const MinicMachineAddress *address = &op->value.address;
            if (op->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
                !minic_x86_64_machine_validate_address(target,address,NULL) ||
                !address->has_base ||
                address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                address->base.number != reg ||
                address->base.width_bits != 64U ||
                address->has_index || address->has_segment ||
                address->has_symbol || address->displacement != 0)
                return false;
        } else if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != reg ||
            op->value.physical.reg.width_bits != bits ||
            op->value.physical.access != (i == 0U ?
                partial ? MINIC_MACHINE_REGISTER_READ |
                    MINIC_MACHINE_REGISTER_WRITE :
                    MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}


/* Local XADD, GS XADD and both GS CMPXCHG forms share a fixed accumulator
 * and R10 memory address, but their memory segments and GNU constraints are
 * distinct.  This independent proof rejects all untagged source templates. */
static bool x86_source_gs_atomic_extended_contract(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins) {
    const MinicX86GsAtomicPlan plan = minic_x86_64_gs_atomic_plan(
        ins->source_asm_template,ins->source_asm_length);
    const bool cmp = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG ||
        plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    const bool retry = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    const bool gs = plan.kind != MINIC_X86_GS_ATOMIC_XADD_LOCAL;
    const unsigned int bits = (unsigned int)(plan.width*8U);
    const unsigned int base_effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    const size_t count = cmp ? 3U : 2U;
    const MinicMachineOperand *acc,*mem;
    const MinicMachineAddress *address;
    if (plan.kind != MINIC_X86_GS_ATOMIC_XADD_LOCAL &&
        plan.kind != MINIC_X86_GS_ATOMIC_XADD_PERCPU &&
        plan.kind != MINIC_X86_GS_ATOMIC_CMPXCHG &&
        plan.kind != MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY)
        return false;
    if ((bits != 8U && bits != 16U && bits != 32U && bits != 64U) ||
        (plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL && bits < 32U) ||
        ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_GS_ATOMIC ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,0U,
            ins->source_asm_template,ins->source_asm_length) ||
        (ins->effects & ~(unsigned int)
            (MINIC_MACHINE_EFFECT_VOLATILE |
             MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) != base_effects ||
        (plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL &&
            (ins->effects & (MINIC_MACHINE_EFFECT_VOLATILE |
                MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) !=
                    (MINIC_MACHINE_EFFECT_VOLATILE |
                     MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) ||
        (cmp && (ins->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) == 0U) ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)
        return false;
    acc = &ins->operands[0];
    mem = &ins->operands[1];
    if (acc->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        acc->asm_name != NULL || acc->asm_constraint == NULL ||
        strcmp(acc->asm_constraint,retry ? "=&a" : cmp ? "+a" :
            plan.kind == MINIC_X86_GS_ATOMIC_XADD_PERCPU && bits == 8U ?
                "+q" : "+r") != 0 ||
        acc->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        acc->value.physical.reg.number != MINIC_X86_REG_RAX ||
        acc->value.physical.reg.width_bits != bits ||
        acc->value.physical.access != (retry ?
            MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE) ||
        mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_name != NULL || mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,"+m") != 0)
        return false;
    address = &mem->value.address;
    if (!minic_x86_64_machine_validate_address(target,address,NULL) ||
        !address->has_base ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.number != MINIC_X86_REG_R10 ||
        address->base.width_bits != 64U ||
        address->has_index || address->has_symbol ||
        address->displacement != 0 ||
        address->has_segment != gs ||
        (gs && (address->segment.bank != MINIC_MACHINE_REGISTER_SYSTEM ||
                address->segment.number != MINIC_X86_SEG_GS)))
        return false;
    if (cmp) {
        const MinicMachineOperand *value = &ins->operands[2];
        if (value->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            value->asm_name != NULL || value->asm_constraint == NULL ||
            strcmp(value->asm_constraint,bits == 8U ? "q" : "r") != 0 ||
            value->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            value->value.physical.reg.number != MINIC_X86_REG_R11 ||
            value->value.physical.reg.width_bits != bits ||
            value->value.physical.access != MINIC_MACHINE_REGISTER_READ)
            return false;
    }
    return true;
}

static bool x86_source_carry_xadd_contract(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    const bool csum = ins->source_asm_machine_form_id ==
        MINIC_X86_MACHINE_ASM_IP_FAST_CSUM;
    const MinicX86GsAtomicPlan plan = csum ?
        (MinicX86GsAtomicPlan){0} : minic_x86_64_gs_atomic_plan(
            ins->source_asm_template,ins->source_asm_length);
    static const unsigned int csum_regs[5] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R9, MINIC_X86_REG_R11,
        MINIC_X86_REG_R9, MINIC_X86_REG_R11
    };
    static const unsigned int csum_bits[5] = {32U,64U,32U,64U,32U};
    static const char *const csum_constraints[5] = {"=r","=r","=r","1","2"};
    const unsigned int bits = (unsigned int)(plan.width*8U);
    const size_t count = csum ? 5U : 2U;
    size_t i;
    if ((csum ? !minic_x86_64_ip_fast_csum_template(
            ins->source_asm_template,ins->source_asm_length) :
            plan.kind != MINIC_X86_GS_ATOMIC_XADD_SMP) ||
        (!csum && bits != 8U && bits != 16U &&
            bits != 32U && bits != 64U) ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        /* Keep the Source-owned volatile bit for ip_fast_csum. GNU
         * permits both asm and asm volatile for this fixed checksum
         * template; the Core source emitter copies that bit unchanged.
         * GS SMP lock XADD, by contrast, is always volatile. */
        (ins->effects & ~(unsigned int)MINIC_MACHINE_EFFECT_VOLATILE) !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             MINIC_MACHINE_EFFECT_MEMORY_READ |
             MINIC_MACHINE_EFFECT_MEMORY_WRITE |
             MINIC_MACHINE_EFFECT_COMPILER_BARRIER) ||
        (!csum &&
         (ins->effects & MINIC_MACHINE_EFFECT_VOLATILE) == 0U) ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length)) return false;
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->asm_name != NULL || op->asm_constraint == NULL) return false;
        if (csum) {
            if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
                strcmp(op->asm_constraint,csum_constraints[i]) != 0 ||
                op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                op->value.physical.reg.number != csum_regs[i] ||
                op->value.physical.reg.width_bits != csum_bits[i] ||
                op->value.physical.access != (i < 3U ?
                    MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
                return false;
        } else if (i == 0U) {
            if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
                strcmp(op->asm_constraint,bits == 8U ? "+q" : "+r") != 0 ||
                op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                op->value.physical.reg.number != MINIC_X86_REG_RAX ||
                op->value.physical.reg.width_bits != bits ||
                op->value.physical.access !=
                    (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE))
                return false;
        } else {
            const MinicMachineAddress *address = &op->value.address;
            if (op->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
                strcmp(op->asm_constraint,"+m") != 0 ||
                !minic_x86_64_machine_validate_address(target,address,NULL) ||
                !address->has_base ||
                address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
                address->base.number != MINIC_X86_REG_R10 ||
                address->base.width_bits != 64U ||
                address->has_index || address->has_segment ||
                address->has_symbol || address->displacement != 0)
                return false;
        }
    }
    return true;
}

/* Counter SOURCE_ASM carries two real register results, not an integer
 * approximation. Validate the exact 652-byte ordered alternative grammar
 * before the template's ELF patch sections can ever reach the GNU printer. */
static bool x86_source_hweight_contract(const MinicMachineInstruction *ins) {
    static const unsigned int registers[2] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDI
    };
    static const char *const constraints[2] = {"=a","D"};
    const unsigned int bytes = minic_x86_64_hweight_alternative_form(
        ins->source_asm_template,ins->source_asm_length);
    const unsigned int bits = bytes * 8U;
    size_t i;
    if ((bytes != 4U && bytes != 8U) ||
        ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_HWEIGHT ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != MINIC_MACHINE_EFFECT_SOURCE_ASM ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraints[i]) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != registers[i] ||
            op->value.physical.reg.width_bits != bits ||
            op->value.physical.access != (i == 0U ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

/* Finite Linux MOV MMIO and XCHG family.  Source qualification owns the
 * C type and the exact GNU text; MachineIR independently proves hardware
 * addressing, scalar width, result transport, barrier and FLAGS effects. */
static bool x86_source_mmio_or_xchg_contract(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins) {
    const uint32_t form_id = ins->source_asm_machine_form_id;
    const bool reading = form_id == MINIC_X86_MACHINE_ASM_MMIO_READ;
    const bool writing = form_id == MINIC_X86_MACHINE_ASM_MMIO_WRITE;
    const bool exchange = form_id == MINIC_X86_MACHINE_ASM_ATOMIC_XCHG;
    const unsigned int width = reading ?
        minic_x86_64_linux_mmio_read_form(
            ins->source_asm_template,ins->source_asm_length) :
        writing ? minic_x86_64_linux_mmio_write_form(
            ins->source_asm_template,ins->source_asm_length) :
        minic_x86_64_linux_atomic_xchg_form(
            ins->source_asm_template,ins->source_asm_length);
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        (reading || exchange ? MINIC_MACHINE_EFFECT_MEMORY_READ : 0U) |
        (writing || exchange ? MINIC_MACHINE_EFFECT_MEMORY_WRITE : 0U);
    const MinicMachineOperand *reg,*mem;
    const MinicMachineAddress *address;
    const char *constraint;
    if ((!reading && !writing && !exchange) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        ins->source_asm_machine_form_variant != width ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (exchange ? ins->effects !=
            (effects | MINIC_MACHINE_EFFECT_COMPILER_BARRIER) :
            (ins->effects != effects &&
             ins->effects != (effects | MINIC_MACHINE_EFFECT_COMPILER_BARRIER))) ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->implicit_count != (exchange ? 1U : 0U) ||
        (!exchange && ins->implicit != NULL) ||
        !minic_x86_64_typed_machine_form_matches_template(
            form_id,ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    reg = &ins->operands[0];
    mem = &ins->operands[1];
    if (reading) {
        constraint = width == 1U &&
            minic_x86_64_compact_asm_equals(
                ins->source_asm_template,ins->source_asm_length,
                "movb%1,%%al") ? "=a" : (width == 1U ? "=q" : "=r");
    } else constraint = exchange ?
        (width == 1U ? "+q" : "+r") :
        (width == 1U ? "q" : "r");
    if (reg->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        reg->asm_name != NULL || reg->asm_constraint == NULL ||
        strcmp(reg->asm_constraint,constraint) != 0 ||
        reg->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        reg->value.physical.reg.number != MINIC_X86_REG_RAX ||
        reg->value.physical.reg.width_bits != width*8U ||
        reg->value.physical.access != (exchange ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            reading ? MINIC_MACHINE_REGISTER_WRITE :
                      MINIC_MACHINE_REGISTER_READ) ||
        mem->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        mem->asm_name != NULL || mem->asm_constraint == NULL ||
        strcmp(mem->asm_constraint,exchange ? "+m" : "m") != 0)
        return false;
    address = &mem->value.address;
    if (!minic_x86_64_machine_validate_address(target,address,NULL) ||
        !address->has_base ||
        address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
        address->base.number != MINIC_X86_REG_R10 ||
        address->base.width_bits != 64U ||
        address->has_index || address->has_segment ||
        address->has_symbol || address->displacement != 0)
        return false;
    if (exchange) {
        const MinicMachineRegisterEffect *flags = &ins->implicit[0];
        return flags->reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
            flags->reg.number == 0U && flags->reg.width_bits == 64U &&
            flags->access == MINIC_MACHINE_REGISTER_CLOBBER;
    }
    return true;
}

static bool x86_source_counter_contract(const MinicMachineInstruction *ins) {
    static const unsigned int registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const char *const constraints[3] = {"=a","=d","c"};
    const unsigned int form = minic_x86_64_linux_counter_form(
        ins->source_asm_template,ins->source_asm_length);
    const bool has_input = form == 2U || form == 5U;
    const bool clobbers_rcx = form == 3U || form == 4U;
    const size_t count = has_input ? 3U : 2U;
    const unsigned int expected_effects =
        MINIC_MACHINE_EFFECT_SOURCE_ASM | MINIC_MACHINE_EFFECT_VOLATILE;
    unsigned int width;
    size_t i;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_COUNTER ||
        ins->source_asm_machine_form_variant != 0U ||
        form < 1U || form > 5U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != expected_effects ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->implicit_count != (clobbers_rcx ? 1U : 0U) ||
        (!clobbers_rcx && ins->implicit != NULL) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    width = ins->operands[0].value.physical.reg.width_bits;
    if (ins->operands[0].kind != MINIC_MACHINE_OPERAND_REGISTER ||
        (width != 32U && width != 64U) ||
        (clobbers_rcx &&
         (ins->implicit == NULL ||
          ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
          ins->implicit[0].reg.number != MINIC_X86_REG_RCX ||
          ins->implicit[0].reg.width_bits != 32U ||
          ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraints[i]) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != registers[i] ||
            op->value.physical.reg.width_bits != (i < 2U ? width : 32U) ||
            op->value.physical.access != (i < 2U ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

/* Exact Linux PKRU byte forms have three fixed 32-bit GPR operands;
 * neither form modifies FLAGS or directly reads/writes a C memory operand.
 * The verifier independently rejects a missing, wider or swapped binding. */
/* Real Linux RDRAND/RDSEED returns status in CF and data in a 64-bit GPR.
 * The status output is a FLAGS register, never a fabricated data register. */
/* The x87 FDIV probe resets and consumes the entire hardware floating
 * stack. All three C objects are exact typed memory operands, not GPR
 * stand-ins; GNU output must retain FINIT/FWAIT and FISTP rounding. */
static bool x86_source_fdiv_bug_contract(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins) {
    static const unsigned int registers[3] = {
        MINIC_X86_REG_R10, MINIC_X86_REG_R11, MINIC_X86_REG_RAX
    };
    size_t i;
    if (ins->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_FDIV_BUG &&
        getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL) {
        fprintf(stderr,
            "x86 fdiv machine: variant=%u operands=%zu implicit=%zu"
            " effects=%u expected=%u terminates=%u fallthrough=%u"
            " template_ok=%u\n",
            ins->source_asm_machine_form_variant,
            ins->operand_count,ins->implicit_count,ins->effects,
            (unsigned int)(MINIC_MACHINE_EFFECT_SOURCE_ASM |
                MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                MINIC_MACHINE_EFFECT_COMPILER_BARRIER),
            (unsigned int)ins->source_asm_terminates_block,
            (unsigned int)ins->source_asm_permits_fallthrough,
            (unsigned int)minic_x86_64_typed_machine_form_matches_template(
                ins->source_asm_machine_form_id,
                ins->source_asm_machine_form_variant,
                ins->source_asm_template,ins->source_asm_length));
        if (ins->operands != NULL) {
            size_t diag;
            for (diag = 0U; diag < ins->operand_count && diag < 3U; ++diag) {
                const MinicMachineOperand *op = &ins->operands[diag];
                fprintf(stderr,
                    "x86 fdiv machine operand%zu: kind=%u"
                    " asm=%s name=%s base=%u width=%u"
                    " disp=%lld index=%u segment=%u symbol=%u\n",
                    diag,(unsigned int)op->kind,
                    op->asm_constraint ? op->asm_constraint : "<null>",
                    op->asm_name ? op->asm_name : "<null>",
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        op->value.address.base.number : 0U,
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        op->value.address.base.width_bits : 0U,
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        (long long)op->value.address.displacement : 0LL,
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        (unsigned int)op->value.address.has_index : 0U,
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        (unsigned int)op->value.address.has_segment : 0U,
                    op->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
                        (unsigned int)op->value.address.has_symbol : 0U);
            }
        }
        if (ins->implicit != NULL && ins->implicit_count != 0U)
            fprintf(stderr,
                "x86 fdiv machine implicit0: bank=%u reg=%u width=%u access=%u\n",
                (unsigned int)ins->implicit[0].reg.bank,
                ins->implicit[0].reg.number,
                ins->implicit[0].reg.width_bits,
                ins->implicit[0].access);
    }
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_FDIV_BUG ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->operand_count != 3U || ins->operands == NULL ||
        ins->implicit_count != 8U || ins->implicit == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (ins->effects & ~(unsigned int)MINIC_MACHINE_EFFECT_VOLATILE) !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
             MINIC_MACHINE_EFFECT_MEMORY_READ |
             MINIC_MACHINE_EFFECT_MEMORY_WRITE |
             MINIC_MACHINE_EFFECT_COMPILER_BARRIER) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        const MinicMachineAddress *address = &op->value.address;
        if (op->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
            op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,i == 0U ? "=m" : "m") != 0 ||
            op->asm_name != NULL ||
            !minic_x86_64_machine_validate_address(target,address,NULL) ||
            !address->has_base ||
            address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            address->base.number != registers[i] ||
            address->base.width_bits != 64U ||
            address->has_index || address->has_segment ||
            address->has_symbol || address->displacement != 0)
            return false;
    }
    for (i = 0U; i < 8U; ++i) {
        const MinicMachineRegisterEffect *op = &ins->implicit[i];
        if (op->reg.bank != MINIC_MACHINE_REGISTER_FLOAT ||
            op->reg.number != i || op->reg.width_bits != 80U ||
            op->access != MINIC_MACHINE_REGISTER_CLOBBER)
            return false;
    }
    return true;
}

static bool x86_source_random_seed_contract(
    const MinicMachineInstruction *ins) {
    const unsigned int form = minic_x86_64_linux_random_seed_form(
        ins->source_asm_template,ins->source_asm_length);
    const bool named = ins->source_asm_length > 8U &&
        ins->source_asm_template[7] == '%' &&
        ins->source_asm_template[8] == '[';
    const MinicMachineOperand *flags, *data;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_RANDOM_SEED ||
        (form != 1U && form != 2U) ||
        ins->source_asm_machine_form_variant != form ||
        ins->operand_count != 2U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE) ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
        ins->implicit[0].reg.number != 0U ||
        ins->implicit[0].reg.width_bits != 64U ||
        ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    flags = &ins->operands[0];
    data = &ins->operands[1];
    return flags->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        flags->asm_name == NULL && flags->asm_constraint != NULL &&
        strcmp(flags->asm_constraint,"=@ccc") == 0 &&
        flags->value.physical.reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
        flags->value.physical.reg.number == 0U &&
        flags->value.physical.reg.width_bits == 64U &&
        flags->value.physical.access == MINIC_MACHINE_REGISTER_WRITE &&
        data->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        data->asm_constraint != NULL &&
        strcmp(data->asm_constraint,"=r") == 0 &&
        (named ? (data->asm_name != NULL &&
            strcmp(data->asm_name,"out") == 0) : data->asm_name == NULL) &&
        data->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        data->value.physical.reg.number == MINIC_X86_REG_RAX &&
        data->value.physical.reg.width_bits == 64U &&
        data->value.physical.access == MINIC_MACHINE_REGISTER_WRITE;
}


/* A sealed 32-bit MXCSR control-word transfer has exactly one true memory
 * operand (R10), with independently checked read/write and compiler effects. */
static bool x86_source_mxcsr_contract(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins) {
    const unsigned int form = minic_x86_64_linux_mxcsr_form(
        ins->source_asm_template,ins->source_asm_length);
    const bool reading = form == 1U;
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        (reading ? MINIC_MACHINE_EFFECT_MEMORY_READ :
            MINIC_MACHINE_EFFECT_MEMORY_WRITE);
    const MinicMachineOperand *op;
    const MinicMachineAddress *addr;
    const bool named = memchr(ins->source_asm_template,'[',
        ins->source_asm_length) != NULL;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_MXCSR ||
        (form != 1U && form != 2U) ||
        ins->source_asm_machine_form_variant != form ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != effects ||
        ins->operand_count != 1U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    op = &ins->operands[0];
    if (op->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
        op->asm_constraint == NULL ||
        strcmp(op->asm_constraint,reading ? "m" : "=m") != 0 ||
        (named ? op->asm_name == NULL ||
            strcmp(op->asm_name,"val") != 0 : op->asm_name != NULL))
        return false;
    addr = &op->value.address;
    return minic_x86_64_machine_validate_address(target,addr,NULL) &&
        addr->has_base &&
        addr->base.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        addr->base.number == MINIC_X86_REG_R10 &&
        addr->base.width_bits == 64U &&
        !addr->has_index && !addr->has_segment &&
        !addr->has_symbol && addr->displacement == 0;
}

/* The three ORC register values are snapshots of hardware state, not
 * address operands or aliases. Reject all forged widths/roles/effects. */
static bool x86_source_unwind_context_contract(
    const MinicMachineInstruction *ins) {
    static const unsigned int output_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R10, MINIC_X86_REG_R11
    };
    size_t i;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE) ||
        ins->operand_count != 3U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,"=r") != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != output_registers[i] ||
            op->value.physical.reg.width_bits != 64U ||
            op->value.physical.access != MINIC_MACHINE_REGISTER_WRITE)
            return false;
    }
    return true;
}

static bool x86_source_pkru_contract(const MinicMachineInstruction *ins) {
    static const unsigned int read_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const unsigned int write_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RCX, MINIC_X86_REG_RDX
    };
    static const char *const read_constraints[3] = {"=a", "=d", "c"};
    static const char *const write_constraints[3] = {"a", "c", "d"};
    const unsigned int form = minic_x86_64_linux_pkru_form(
        ins->source_asm_template,ins->source_asm_length);
    const unsigned int *registers = form == 1U ?
        read_registers : write_registers;
    const char *const *constraints = form == 1U ?
        read_constraints : write_constraints;
    const size_t outputs = form == 1U ? 2U : 0U;
    size_t i;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_PKRU ||
        form < 1U || form > 2U ||
        ins->source_asm_machine_form_variant != form ||
        ins->operand_count != 3U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name != NULL || op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraints[i]) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != registers[i] ||
            op->value.physical.reg.width_bits != 32U ||
            op->value.physical.access != (i < outputs ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ))
            return false;
    }
    return true;
}

static bool x86_source_msr_contract(const MinicMachineInstruction *ins) {
    unsigned int registers[4] = {0};
    const char *constraints[4] = {NULL};
    const unsigned int form = minic_x86_64_linux_msr_form(
        ins->source_asm_template,ins->source_asm_length);
    const bool read = form == 1U || form == 3U || form == 5U;
    const bool safe = form == 3U || form == 4U;
    const size_t outputs = safe ? (read ? 3U : 1U) : (read ? 2U : 0U);
    const size_t count = safe ? 4U : 3U;
    unsigned int width = 32U;
    size_t i;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_MSR ||
        ins->source_asm_machine_form_variant != 0U ||
        form == 0U || form > 6U ||
        ins->operand_count != count || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            (read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE) |
            (read ? 0U : MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) ||
        ins->implicit_count != (safe ? 1U : 0U) ||
        (safe && (ins->implicit == NULL ||
            ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
            ins->implicit[0].reg.number != 0U ||
            ins->implicit[0].reg.width_bits != 64U ||
            ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)) ||
        (!safe && ins->implicit != NULL) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    if (safe && read) {
        registers[0] = MINIC_X86_REG_R11;
        registers[1] = MINIC_X86_REG_RAX;
        registers[2] = MINIC_X86_REG_RDX;
        registers[3] = MINIC_X86_REG_RCX;
        constraints[0] = "=r";
        constraints[1] = "=a";
        constraints[2] = "=d";
        constraints[3] = "c";
    } else if (safe) {
        registers[0] = registers[2] = MINIC_X86_REG_RAX;
        registers[1] = MINIC_X86_REG_RCX;
        registers[3] = MINIC_X86_REG_RDX;
        constraints[0] = "=a";
        constraints[1] = "c";
        constraints[2] = "0";
        constraints[3] = "d";
    } else if (read) {
        registers[0] = MINIC_X86_REG_RAX;
        registers[1] = MINIC_X86_REG_RDX;
        registers[2] = MINIC_X86_REG_RCX;
        constraints[0] = "=a";
        constraints[1] = "=d";
        constraints[2] = "c";
        width = ins->operands[0].value.physical.reg.width_bits;
        if ((width != 32U && width != 64U) ||
            ins->operands[1].value.physical.reg.width_bits != width)
            return false;
    } else {
        registers[0] = MINIC_X86_REG_RCX;
        registers[1] = MINIC_X86_REG_RAX;
        registers[2] = MINIC_X86_REG_RDX;
        constraints[0] = "c";
        constraints[1] = "a";
        constraints[2] = "d";
    }
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *operand = &ins->operands[i];
        const unsigned int bits = read && !safe && i < 2U ? width :
            safe && read && (i == 1U || i == 2U) ? 64U : 32U;
        const unsigned int access = i < outputs ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
        if (operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            operand->asm_constraint == NULL ||
            strcmp(operand->asm_constraint,constraints[i]) != 0 ||
            (operand->asm_name != NULL &&
                (!safe || i != 0U || strcmp(operand->asm_name,"err") != 0)) ||
            (safe && i == 0U && (operand->asm_name == NULL ||
                strcmp(operand->asm_name,"err") != 0)) ||
            operand->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            operand->value.physical.reg.number != registers[i] ||
            operand->value.physical.reg.width_bits != bits ||
            operand->value.physical.access != access)
            return false;
    }
    return true;
}

static bool x86_source_percpu_gs_contract(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins) {
    unsigned int operation;
    size_t width, count, i;
    const bool stable = minic_x86_64_asm_percpu_stable_width(
        ins->source_asm_template,ins->source_asm_length) != 0U;
    const MinicMachineOperand *memory;
    const MinicMachineAddress *address;
    if (ins->source_asm_machine_form_id != MINIC_X86_MACHINE_ASM_PERCPU_GS ||
        ins->source_asm_machine_form_variant != 0U ||
        !minic_x86_64_linux_percpu_form(ins->source_asm_template,
            ins->source_asm_length,&operation,&width) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ((ins->effects & ~(unsigned int)MINIC_MACHINE_EFFECT_VOLATILE) !=
            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
            (operation == 0U ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                operation == 1U ? MINIC_MACHINE_EFFECT_MEMORY_WRITE :
                    MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE) |
            (operation == 0U ? 0U :
                MINIC_MACHINE_EFFECT_COMPILER_BARRIER))) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length) ||
        ins->implicit_count != (operation >= 2U ? 1U : 0U) ||
        (operation >= 2U && (ins->implicit == NULL ||
            ins->implicit[0].reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
            ins->implicit[0].reg.number != 0U ||
            ins->implicit[0].reg.width_bits != 64U ||
            ins->implicit[0].access != MINIC_MACHINE_REGISTER_CLOBBER)) ||
        (operation < 2U && ins->implicit != NULL)) return false;
    count = operation == 2U || operation == 3U ? 1U : 2U;
    if (ins->operand_count != count || ins->operands == NULL) return false;
    memory = &ins->operands[operation == 0U ? 1U : 0U];
    if (stable) {
        if (operation != 0U ||
            memory->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            memory->asm_name == NULL ||
            strcmp(memory->asm_name,"var") != 0 ||
            memory->asm_constraint == NULL ||
            strcmp(memory->asm_constraint,"p") != 0 ||
            memory->value.physical.reg.bank !=
                MINIC_MACHINE_REGISTER_GENERAL ||
            memory->value.physical.reg.number != MINIC_X86_REG_R10 ||
            memory->value.physical.reg.width_bits != 64U ||
            memory->value.physical.access != MINIC_MACHINE_REGISTER_READ)
            return false;
    } else {
        if (memory->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
            memory->asm_name == NULL ||
            strcmp(memory->asm_name,"var") != 0 ||
            memory->asm_constraint == NULL ||
            strcmp(memory->asm_constraint,
                operation == 0U ? "m" : "+m") != 0)
            return false;
        address = &memory->value.address;
        if (!minic_x86_64_machine_validate_address(target,address,NULL) ||
            !address->has_base ||
            address->base.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            address->base.number != MINIC_X86_REG_R10 ||
            address->base.width_bits != 64U ||
            address->has_index || address->has_segment ||
            address->has_symbol || address->displacement != 0)
            return false;
    }
    for (i = 0U; i < count; ++i) {
        const MinicMachineOperand *op = &ins->operands[i];
        const unsigned int access = operation == 0U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
        const char *constraint = operation == 0U ?
            (width == 1U ? "=q" : "=r") :
            (width == 1U ? "qi" : width == 8U ? "re" : "ri");
        if (op == memory) continue;
        if (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
            op->asm_name == NULL || strcmp(op->asm_name,"val") != 0 ||
            op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,constraint) != 0 ||
            op->value.physical.reg.bank != MINIC_MACHINE_REGISTER_GENERAL ||
            op->value.physical.reg.number != MINIC_X86_REG_RAX ||
            op->value.physical.reg.width_bits != width*8U ||
            op->value.physical.access != access)
            return false;
    }
    return true;
}

/* The two GNU Linux BUG_TABLE source spellings have identical ELF semantics
 * but different operand-reference names. `%c` prints the relocation *symbol*
 * and three source-proven integer constants without a `$` prefix. The ELF
 * reference is PC-relative from `.long symbol - .`, never ABS64. */
static bool x86_source_bug_c_immediate_contract(
    const MinicMachineInstruction *ins) {
    static const char *const names[4] = {"path","ln","fl","sz"};
    const unsigned int variant = ins->source_asm_machine_form_variant;
    size_t index;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE ||
        (variant != 1U && variant != 2U) ||
        ins->operand_count != 4U || ins->operands == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE) ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,variant,
            ins->source_asm_template,ins->source_asm_length))
        return false;
    for (index = 0U; index < 4U; ++index) {
        const MinicMachineOperand *op = &ins->operands[index];
        if (op->asm_constraint == NULL ||
            strcmp(op->asm_constraint,"i") != 0 ||
            (variant == 1U ? op->asm_name != NULL :
                (op->asm_name == NULL ||
                 strcmp(op->asm_name,names[index]) != 0)))
            return false;
        if (index == 0U) {
            if (op->kind != MINIC_MACHINE_OPERAND_SYMBOL ||
                op->value.symbol.name == NULL ||
                op->value.symbol.relocation !=
                    MINIC_X86_MACHINE_RELOC_PC32)
                return false;
        } else if (op->kind != MINIC_MACHINE_OPERAND_INTEGER)
            return false;
    }
    /* GNU __bug_table uses a 16-bit line and flags field, plus 12-byte
     * record layout. These checked physical values are not emitted as
     * arbitrary assembler source or taken from runtime SSA registers. */
    return ins->operands[1].value.integer >= 0 &&
        ins->operands[1].value.integer <= UINT16_MAX &&
        ins->operands[2].value.integer >= 0 &&
        ins->operands[2].value.integer <= UINT16_MAX &&
        ins->operands[3].value.integer == 12;
}

static bool x86_source_nop_const_i_contract(
    const MinicMachineInstruction *ins) {
    const MinicMachineOperand *input;
    if (ins->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_NOP_CONST_I ||
        ins->source_asm_machine_form_variant != 1U ||
        ins->operand_count != 1U || ins->operands == NULL ||
        ins->implicit_count != 0U || ins->implicit != NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                        MINIC_MACHINE_EFFECT_VOLATILE) ||
        !minic_x86_64_typed_machine_form_matches_template(
            ins->source_asm_machine_form_id,
            ins->source_asm_machine_form_variant,
            ins->source_asm_template,ins->source_asm_length)) return false;
    input = &ins->operands[0];
    return input->kind == MINIC_MACHINE_OPERAND_INTEGER &&
        input->asm_constraint != NULL &&
        strcmp(input->asm_constraint,"i") == 0 &&
        input->asm_name == NULL;
}

/* Machine ownership of the complete zero-operand Linux ordering family.
 * Every form has an exact Source-validated variant. No other source syntax
 * can pass this physical proof, even if its textual mnemonic looks similar. */
bool minic_x86_64_machine_validate_source_asm(const MinicTargetInfo *target,
    const MinicMachineInstruction *instruction, void *context) {
    unsigned int form;
    unsigned int expected;
    (void)context;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        instruction == NULL ||
        instruction->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        !instruction->has_core_origin ||
        !instruction->has_source_asm_unique_id ||
        instruction->source_asm_unique_id == 0U ||
        instruction->source_asm_template == NULL ||
        instruction->source_asm_terminates_block ||
        instruction->source_asm_permits_fallthrough)
        return false;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULDIV_U64 ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_KASLR_MUL)
        return x86_source_accumulator_math_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CPUID_MATCHED ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULACC_MATCHED)
        return x86_source_matched_contract(target, instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MATCHED_MEMORY)
        return x86_source_matched_memory_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED)
        return x86_source_bit_scan_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PREFETCHW_ALT)
        return x86_source_prefetchw_alt_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CMPXCHG128)
        return x86_source_cmpxchg128_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CRASH_REGISTER)
        return x86_source_crash_register_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_FUTEX_EXTABLE)
        return x86_source_futex_extable_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GS_TIED_ALU)
        return x86_source_gs_tied_alu_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_IP_HERE)
        return x86_source_ip_here_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_FDIV_BUG)
        return x86_source_fdiv_bug_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_RANDOM_SEED)
        return x86_source_random_seed_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PKRU)
        return x86_source_pkru_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MXCSR)
        return x86_source_mxcsr_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT)
        return x86_source_unwind_context_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MSR)
        return x86_source_msr_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_HWEIGHT)
        return x86_source_hweight_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_COUNTER)
        return x86_source_counter_contract(instruction);
    if (instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_MMIO_READ ||
        instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_MMIO_WRITE ||
        instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_ATOMIC_XCHG)
        return x86_source_mmio_or_xchg_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_ADD32_CARRY ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CHECKSUM ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CSUM_PARTIAL)
        return x86_source_checksum_carry_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GS_ATOMIC &&
        minic_x86_64_gs_atomic_plan(instruction->source_asm_template,
            instruction->source_asm_length).kind !=
                MINIC_X86_GS_ATOMIC_XADD_SMP)
        return x86_source_gs_atomic_extended_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_IP_FAST_CSUM ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GS_ATOMIC)
        return x86_source_carry_xadd_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PERCPU_GS)
        return x86_source_percpu_gs_contract(target,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE)
        return x86_source_bug_c_immediate_contract(instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_NOP_CONST_I)
        return x86_source_nop_const_i_contract(instruction);
    if (instruction->source_asm_machine_form_id == 0U &&
        instruction->source_asm_machine_form_variant == 0U &&
        instruction->source_asm_length == 0U)
        return instruction->operand_count == 0U &&
            instruction->operands == NULL &&
            instruction->effects ==
                (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                 MINIC_MACHINE_EFFECT_VOLATILE) &&
            instruction->implicit_count == 0U &&
            instruction->implicit == NULL;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PUT_USER_CALL ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GET_USER_CALL) {
        const bool put = instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PUT_USER_CALL;
        form = minic_x86_64_linux_uaccess_core_form_exact(
            instruction->source_asm_template,
            instruction->source_asm_length, put);
        return instruction->operand_count == 0U &&
            instruction->operands == NULL &&
            instruction->source_asm_machine_form_variant == form &&
            (put ? form == 0x200U || form == 0x800U :
                   form == 3U || form == 4U) &&
            instruction->implicit_count == 0U &&
            instruction->implicit == NULL &&
            instruction->effects ==
                (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                 MINIC_MACHINE_EFFECT_VOLATILE |
                 MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
                 MINIC_MACHINE_EFFECT_MEMORY_READ |
                 MINIC_MACHINE_EFFECT_MEMORY_WRITE);
    }
    if (instruction->operands != NULL || instruction->operand_count != 0U ||
        instruction->source_asm_machine_form_id !=
            MINIC_X86_MACHINE_ASM_ZERO_BARRIER ||
        (form = minic_x86_64_zero_barrier_form_exact(
            instruction->source_asm_template,
            instruction->source_asm_length)) == 0U ||
        instruction->source_asm_machine_form_variant != form)
        return false;
    expected = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE;
    if (form <= 4U) expected |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    if (form == 1U) {
        const MinicMachineRegisterEffect *flags;
        expected |= MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE;
        if (instruction->implicit_count != 1U ||
            (flags = instruction->implicit) == NULL ||
            flags->reg.bank != MINIC_MACHINE_REGISTER_FLAGS ||
            flags->reg.number != 0U || flags->reg.width_bits != 64U ||
            flags->access != MINIC_MACHINE_REGISTER_CLOBBER) return false;
    } else if (instruction->implicit_count != 0U ||
               instruction->implicit != NULL) return false;
    return instruction->effects == expected;
}

#undef R
#undef W
#undef C
#undef MR
#undef MW
#undef BR
#undef CA
#undef RT
#undef BAR
#undef BASE
#undef IBT
#undef SSE2
