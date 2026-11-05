#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_target_state.h"

#include <limits.h>
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
    X86_IMPLICIT_DIV,
    X86_IMPLICIT_CQTO,
    X86_IMPLICIT_REP_MOVSB,
    X86_IMPLICIT_CALL_SYSV,
    X86_IMPLICIT_CALL_MS,
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

    SET(SETZ8_R,"setz"), SET(SETNZ8_R,"setnz"),
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
    out->permitted_effects=info->effects;
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

/* This first x86 ISA policy deliberately does not certify a Linux inline
 * assembly template. Source qualification/operand materialization will be
 * attached only when that entire target-specific path has been migrated. */
bool minic_x86_64_machine_validate_source_asm(const MinicTargetInfo *target,
    const MinicMachineInstruction *instruction, void *context) {
    (void)target; (void)instruction; (void)context;
    return false;
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
