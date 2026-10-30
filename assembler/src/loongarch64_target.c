#include "minias_internal.h"
#include "loongarch64_fixup_kinds.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Match RV's proven invariant: MC opcode describes the instruction family;
 * FixupKind describes an independently attached semantic relocation.
 * Never cast relocation identities to an instruction opcode. */
typedef enum {
    /* Encoded single-word instructions still retain their Target form ID. */
    LA_MC_FIXED = 0,
    LA_MC_PCALA_HI20,
    LA_MC_PCALA_LO12,
    LA_MC_PCREL_PSEUDO,
    LA_MC_GOT_PSEUDO,
    LA_MC_LI32_SEQUENCE,
    LA_MC_LI64_SEQUENCE,
    LA_MC_BRANCH16,
    LA_MC_BRANCH21,
    LA_MC_BRANCH26
} LaMCOpcode;

/* These are LoongArch-only register-bank and form identities. Common retains
 * their numeric values but never interprets or enumerates ISA-specific banks.
 * In particular, VR128 and XR256, FCC and FCSR are NOT interchangeable GPRs. */
typedef enum {
    LA_REG_BANK_NONE = 0,
    LA_REG_BANK_GPR,
    LA_REG_BANK_FPR,
    LA_REG_BANK_VR128,
    LA_REG_BANK_XR256,
    LA_REG_BANK_FCSR,
    LA_REG_BANK_FCC,
    LA_REG_BANK_SCR
} LaRegisterBank;

/* The ISA feature mask belongs to the LoongArch Target and is passed through
 * Common's opaque profile/state slots without adding global ISA knowledge. */
typedef enum {
    LA_FEATURE_LSX=1U<<0,
    LA_FEATURE_LASX=1U<<1
} LaISAFeature;
typedef struct {
    uint32_t features;
    uint32_t abi;
} LaTargetProfile;
typedef enum {
    LA_MN_BRANCH,
    LA_MN_VECTOR,
    LA_MN_SPECIAL,
    LA_MN_SCALAR,
    LA_MN_BITFIELD,
    LA_MN_ALSL,
    LA_MN_PRIVILEGED,
    LA_MN_LI,
    LA_MN_PCREL,
    LA_MN_GOT,
    LA_MN_BYTEPICK,
    LA_MN_JIRL,
    LA_MN_MOVE,
    LA_MN_PREFETCH,
    LA_MN_ADDU16I
} LaMnemonicFamily;
typedef enum {
    LA_BRANCH_CANONICAL,
    LA_BRANCH_ZERO_RIGHT,
    LA_BRANCH_ZERO_LEFT,
    LA_BRANCH_SWAP
} LaBranchAlias;
typedef struct {
    const char *name;
    LaMnemonicFamily family;
    unsigned index;
    uint32_t required_features;
    LaBranchAlias branch_alias;
} LaMnemonicEntry;
typedef struct {
    uint32_t features;
    LaMnemonicEntry *mnemonics;
    size_t mnemonic_count;
    bool *relaxable_sections;
    size_t relaxable_section_count;
} LaTargetState;

#define LA_COUNT(a) (sizeof(a)/sizeof((a)[0]))

static bool la_has_features(const MiniAs *as,uint32_t required) {
    const LaTargetState *state=(const LaTargetState *)as->target_state;
    /* Direct target API clients without a resolved profile preserve the
     * legacy instruction repertoire; explicit profiles are authoritative. */
    return state==NULL || (state->features&required)==required;
}

typedef enum {
    LA_FORM_NONE = 0,
    LA_FORM_PRIVILEGED = 1,
    LA_FORM_SCALAR = 0x1000,
    LA_FORM_VECTOR = 0x2000,
    LA_FORM_SPECIAL_REGISTER = 0x3000,
    LA_FORM_BRANCH = 0x4000,
    LA_FORM_LI_D = 0x5000,
    LA_FORM_LI_W = 0x5001,
    LA_FORM_PCREL = 0x5002,
    LA_FORM_GOT = 0x5004,
    LA_FORM_BITFIELD = 0x5003,
    LA_FORM_BYTEPICK = 0x5100,
    LA_FORM_ALSL = 0x5200,
    LA_FORM_JIRL = 0x5300,
    LA_FORM_MOVE = 0x5400,
    LA_FORM_PREFETCH = 0x5500,
    LA_FORM_ADDU16I = 0x5600,
    LA_FORM_PCALA_HI20 = 0x5700,
    LA_FORM_PCALA_LO12 = 0x5701
} LaFormKind;

typedef struct {
    const char *name;
    uint32_t opcode;
    unsigned immediate_bits;
} LaManualPattern;
static const LaManualPattern la_bitfield_patterns[] = {
    {"bstrins.w",UINT32_C(0x00600000),5U},
    {"bstrpick.w",UINT32_C(0x00608000),5U},
    {"bstrins.d",UINT32_C(0x00800000),6U},
    {"bstrpick.d",UINT32_C(0x00c00000),6U}
};
static const LaManualPattern la_alsl_patterns[] = {
    {"alsl.w",UINT32_C(0x00040000),2U},
    {"alsl.wu",UINT32_C(0x00060000),2U},
    {"alsl.d",UINT32_C(0x002c0000),2U}
};

static void la_set_register_bank(MiniAsMCOperand *operand,
                                 LaRegisterBank bank) {
    operand->reg_class = bank == LA_REG_BANK_FPR ? MINIAS_MC_REG_FPR :
        (bank == LA_REG_BANK_VR128 || bank == LA_REG_BANK_XR256) ?
        MINIAS_MC_REG_VECTOR : MINIAS_MC_REG_GPR;
    operand->target_reg_bank = (uint16_t)bank;
}

/* LoongArch64 LA64 basic integer target. GNU binutils tables own the reference
 * encodings; extensions and fixup relocation families are not guessed. */
static bool la_reg(const char *s,unsigned *r) {
    static const struct {const char *s;unsigned r;} aliases[]={
        {"zero",0U},{"ra",1U},{"tp",2U},{"sp",3U},
        {"a0",4U},{"a1",5U},{"a2",6U},{"a3",7U},
        {"a4",8U},{"a5",9U},{"a6",10U},{"a7",11U},
        {"t0",12U},{"t1",13U},{"t2",14U},{"t3",15U},
        {"t4",16U},{"t5",17U},{"t6",18U},{"t7",19U},
        {"t8",20U},{"u0",21U},{"fp",22U},{"s9",22U}
    };
    unsigned i;char *end;unsigned long val;
    if(*s!='$')return false;
    ++s;
    if(*s=='r' && isdigit((unsigned char)s[1])) {
        errno=0;val=strtoul(s+1,&end,10);
        if (!errno && !*end && val<=31U) {*r=(unsigned)val;return true;}
    }
    for(i=0U;i<sizeof(aliases)/sizeof(aliases[0]);++i)
        if(!strcmp(s,aliases[i].s)){*r=aliases[i].r;return true;}
    if(s[0]=='s' && isdigit((unsigned char)s[1])) {
        errno=0;val=strtoul(s+1,&end,10);
        if(!errno && !*end && val<=8U){*r=(unsigned)(23U+val);return true;}
    }
    return false;
}
static bool la_special_reg(const char *s,const char *prefix,
                           unsigned maximum,unsigned *reg) {
    char *end;unsigned long val;size_t len=strlen(prefix);
    if(strncmp(s,prefix,len)!=0 || !isdigit((unsigned char)s[len]))return false;
    errno=0;val=strtoul(s+len,&end,10);
    if(errno || *end || val>maximum)return false;
    *reg=(unsigned)val;return true;
}

/* All scalar floating-point instruction families share one psABI FPR
 * decoder; the retained typed register bank is still FPR. "$faN",
 * "$ftN", and "$fsN" are canonical ABI names for f0..f7,
 * f8..f23 and f24..f31 respectively, not additional hardware banks. */
static bool la_fpr_reg(const char *s,unsigned *reg) {
    unsigned ordinal;
    if(la_special_reg(s,"$f",31U,reg))return true;
    if(la_special_reg(s,"$fa",7U,&ordinal)) {
        *reg=ordinal;
        return true;
    }
    if(la_special_reg(s,"$ft",15U,&ordinal)) {
        *reg=8U+ordinal;
        return true;
    }
    if(la_special_reg(s,"$fs",7U,&ordinal)) {
        *reg=24U+ordinal;
        return true;
    }
    return false;
}
typedef struct {
    char fast[4][192];
    char *slots[4];
    char *overflow;
    unsigned count;
} LaOperandTokens;

/* Preserve a no-allocation path for the ordinary GCC instruction stream.
 * Oversized but lexically valid operands get a dynamic Target-owned buffer,
 * still tokenized by the ONE Common delimiter/quote grammar. Neither route
 * changes the ISA's four-operand maximum or SymbolExpr's name constraints. */
static bool la_split(const char *args,LaOperandTokens *tokens) {
    const size_t fast_capacity[4]={192U,192U,192U,192U};
    size_t capacities[4];
    size_t len,per_slot,i;
    for(i=0U;i<4U;++i)tokens->slots[i]=tokens->fast[i];
    if(minias_split_mc_operands(args,tokens->slots,fast_capacity,4U,
                                &tokens->count))return true;
    len=strlen(args);
    if(len<192U || len>SIZE_MAX/4U-1U)return false;
    per_slot=len+1U;
    tokens->overflow=malloc(per_slot*4U);
    if(tokens->overflow==NULL)return false;
    for(i=0U;i<4U;++i) {
        tokens->slots[i]=tokens->overflow+i*per_slot;
        capacities[i]=per_slot;
    }
    if(minias_split_mc_operands(args,tokens->slots,capacities,4U,
                                &tokens->count))return true;
    free(tokens->overflow);
    tokens->overflow=NULL;
    return false;
}


/* GNU LA relocation wrappers are typed operands, not raw machine bits.
 * For now support exactly %pc_hi20/%pc_lo12; GOT/TLS alternatives require
 * different psABI relocations and must fail closed. */
static bool la_reloc_expr(const char *value, const char *modifier,
                          MiniAsSymbolExpr *out) {
    size_t prefix = strlen(modifier), len = strlen(value);
    char inner[256];
    if (strncmp(value, modifier, prefix) != 0 ||
        value[prefix] != '(' || len <= prefix+2U ||
        value[len-1U] != ')' || len-prefix-2U >= sizeof(inner))
        return false;
    memcpy(inner,value+prefix+1U,len-prefix-2U);
    inner[len-prefix-2U]='\0';
    return minias_parse_symbol_addend(inner,out);
}

static bool la_symbol_operand(MiniAs *as, MiniAsMCInst *m, unsigned slot,
                              const MiniAsSymbolExpr *expr) {
    MiniAsSymbolRef *ref = malloc(sizeof(*ref));
    if (!ref) {
        minias_set_error(as,"out-of-memory:loongarch-symbol-mcinst");
        return false;
    }
    if(!minias_bind_symbol_ref(as,expr,ref)) {
        free(ref);
        return false;
    }
    m->operands[slot].kind=MINIAS_MC_BOUND_SYMBOL;
    m->operands[slot].bound_symbol=ref;
    return true;
}

typedef struct {
    const char *name;
    unsigned operands;
    uint32_t opcode;
    LaMCOpcode kind;
    LaRegisterBank first_bank;
} LaBranchPattern;
static const LaBranchPattern la_branch_patterns[]={
    {"b",1U,UINT32_C(0x50000000),LA_MC_BRANCH26,LA_REG_BANK_NONE},
    {"bl",1U,UINT32_C(0x54000000),LA_MC_BRANCH26,LA_REG_BANK_NONE},
    {"beqz",2U,UINT32_C(0x40000000),LA_MC_BRANCH21,LA_REG_BANK_GPR},
    {"bnez",2U,UINT32_C(0x44000000),LA_MC_BRANCH21,LA_REG_BANK_GPR},
    {"beq",3U,UINT32_C(0x58000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    {"bne",3U,UINT32_C(0x5c000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    {"blt",3U,UINT32_C(0x60000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    {"bge",3U,UINT32_C(0x64000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    {"bltu",3U,UINT32_C(0x68000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    {"bgeu",3U,UINT32_C(0x6c000000),LA_MC_BRANCH16,LA_REG_BANK_GPR},
    /* GNU opcodes/loongarch-opc.c float_jmp: FCC lives in bits 5..7;
     * bit 8 distinguishes BCNEZ from BCEQZ. Both use the existing B21
     * relocation, layout and short-branch resolution machinery. */
    {"bceqz",2U,UINT32_C(0x48000000),LA_MC_BRANCH21,LA_REG_BANK_FCC},
    {"bcnez",2U,UINT32_C(0x48000100),LA_MC_BRANCH21,LA_REG_BANK_FCC}
};

static bool la_branch_instruction(MiniAs *as, MiniAsMCInst *m,
                                  char *t[4],unsigned count,
                                  unsigned branch_index,LaBranchAlias alias) {
    unsigned rj, rd, slot;
    const LaBranchPattern *b;
    const bool zero_alias=alias==LA_BRANCH_ZERO_RIGHT ||
                          alias==LA_BRANCH_ZERO_LEFT;
    const bool swap=alias==LA_BRANCH_SWAP;
    MiniAsSymbolExpr expr;
    /* GAS pseudo comparisons reuse the existing B16 encoding and its
     * Common RV-style SymbolExpr/ELF fixup. Do not duplicate a branch ISA. */
    if(zero_alias) {
        /* GNU psABI BxxZ aliases share the one existing typed B16,
         * relocation and local-branch encode path, not a second encoder. */
        bool zero_first=alias==LA_BRANCH_ZERO_LEFT;
        if(count!=2U)return false;
        memcpy(t[2],t[1],strlen(t[1])+1U);
        if(zero_first) {
            memcpy(t[1],t[0],strlen(t[0])+1U);
            memcpy(t[0],"$zero",6U);
        } else memcpy(t[1],"$zero",6U);
        count=3U;
    }
    if(alias>LA_BRANCH_SWAP)return false;
    if(branch_index>=LA_COUNT(la_branch_patterns))return false;
    b=&la_branch_patterns[branch_index];
    if(count==b->operands) {
        /* GCC emits `bl %plt(function)` and the original BusyBox .S sources
         * also emit `b %plt(function)`. GNU GAS uses R_LARCH_B26 with the
         * unadorned symbol in both cases. Preserve the typed SymbolRef and
         * branch fixup; `%plt(...)` is not a part of the symbol's name. */
        if (!(minias_parse_symbol_addend(t[count-1U],&expr) ||
              (branch_index<=1U &&
               la_reloc_expr(t[count-1U],"%plt",&expr))) ||
            (expr.addend&3)!=0)
            return false;
        rj=0U;rd=0U;
        if(count>=2U &&
           !(b->first_bank==LA_REG_BANK_FCC ?
             la_special_reg(t[0],"$fcc",7U,&rj) :
             la_reg(t[0],&rj)))return false;
        if(count==3U && !la_reg(t[1],&rd))return false;
        if(swap && count==3U) {unsigned tmp=rj;rj=rd;rd=tmp;}
        slot=count-1U;
        if (!la_symbol_operand(as,m,slot,&expr))return false;
        m->opcode=(unsigned)b->kind;
        m->target_form=(uint32_t)LA_FORM_BRANCH+branch_index;
        m->count=count;
        if(count>=2U) {m->operands[0].kind=MINIAS_MC_REGISTER;
                       m->operands[0].reg=rj;
                       la_set_register_bank(&m->operands[0],b->first_bank);}
        if(count==3U) {m->operands[1].kind=MINIAS_MC_REGISTER;
                       m->operands[1].reg=rd;
                       la_set_register_bank(&m->operands[1],LA_REG_BANK_GPR);}
        return true;
    }
    return false;
}

/* GNU binutils gas/config/tc-loongarch.c assember_macro_helper:
 * exact sign-extension reduction for LI.D.  The macro is a semantic
 * Target pseudo containing only a 64-bit immediate; Common only measures
 * and places a typed instruction fragment.  This is not an arbitrary
 * fixed four-word guess: GNU emits from one to four words depending on
 * the sign-extension relationships of its 12/20/20/12-bit groups. */
static unsigned la_li_d_words(uint64_t bits,unsigned rd,uint32_t out[4]) {
    const uint32_t lo=(uint32_t)bits,hi=(uint32_t)(bits>>32U);
    const unsigned top=(unsigned)(hi>>20U)&4095U;
    const unsigned mid=(unsigned)(hi&UINT32_C(0xfffff));
    const unsigned lowhi=(unsigned)(lo>>12U)&UINT32_C(0xfffff);
    const unsigned lowlo=(unsigned)(lo&4095U);
    const unsigned zeros=
        (((hi&UINT32_C(0xfff00000))==0U)?8U:0U)|
        (((hi&UINT32_C(0x000fffff))==0U)?4U:0U)|
        (((lo&UINT32_C(0xfffff000))==0U)?2U:0U)|
        (((lo&UINT32_C(0x00000fff))==0U)?1U:0U);
    const unsigned ones=
        (((hi&UINT32_C(0xfff00000))==UINT32_C(0xfff00000))?8U:0U)|
        (((hi&UINT32_C(0x000fffff))==UINT32_C(0x000fffff))?4U:0U)|
        (((lo&UINT32_C(0xfffff000))==UINT32_C(0xfffff000))?2U:0U)|
        (((lo&UINT32_C(0x00000fff))==UINT32_C(0x00000fff))?1U:0U);
    const unsigned signs=
        ((hi&UINT32_C(0x80000000))?8U:0U)|
        ((hi&UINT32_C(0x00080000))?4U:0U)|
        ((lo&UINT32_C(0x80000000))?2U:0U)|
        ((lo&UINT32_C(0x00000800))?1U:0U);
    const unsigned part=(zeros^ones)&(zeros^(signs<<1U));
    unsigned n=0U;
    if(part==7U) {
        out[n++]=UINT32_C(0x03000000)|(top<<10U)|rd;
        return n;
    }
    if((zeros&3U)==2U) {
        out[n++]=UINT32_C(0x03800000)|(lowlo<<10U)|rd;
    } else {
        switch(part&3U) {
            case 0U:
                out[n++]=UINT32_C(0x14000000)|(lowhi<<5U)|rd;
                out[n++]=UINT32_C(0x03800000)|
                         (lowlo<<10U)|(rd<<5U)|rd;
                break;
            case 1U:
                out[n++]=UINT32_C(0x14000000)|(lowhi<<5U)|rd;
                break;
            case 2U:
                out[n++]=UINT32_C(0x02800000)|(lowlo<<10U)|rd;
                break;
            default:
                out[n++]=UINT32_C(0x00150000)|rd;
                break;
        }
    }
    switch(part>>2U) {
        case 0U:
            out[n++]=UINT32_C(0x16000000)|(mid<<5U)|rd;
            out[n++]=UINT32_C(0x03000000)|(top<<10U)|(rd<<5U)|rd;
            break;
        case 1U:
            out[n++]=UINT32_C(0x03000000)|(top<<10U)|(rd<<5U)|rd;
            break;
        case 2U:
            out[n++]=UINT32_C(0x16000000)|(mid<<5U)|rd;
            break;
        default:break;
    }
    return n;
}

/* All variable-width LoongArch pseudos share one immutable lowering plan.
 * Fixups are relative to the emitted word, and refer to retained semantic
 * operands; neither sizing nor emission has to re-lower an instruction. */
typedef struct {
    uint32_t tag;
    unsigned count;
    uint32_t words[4];
    unsigned fixup_count;
    struct {
        unsigned word;
        unsigned symbol_operand;
        MiniAsFixupKind kind;
        bool absolute_zero;
    } fixups[4];
} LaLoweringPlan;

enum { LA_LOWERING_PLAN_TAG=0x4c41504cU };

static bool la_valid_lowering_plan(const MiniAsMCInst *inst) {
    const LaLoweringPlan *plan=(const LaLoweringPlan *)inst->target_payload;
    return plan!=NULL && plan->tag==LA_LOWERING_PLAN_TAG &&
        plan->count>0U && plan->count<=4U && plan->fixup_count<=4U &&
        ((inst->opcode==LA_MC_LI32_SEQUENCE &&
          inst->target_form==LA_FORM_LI_W && plan->count<=2U &&
          plan->fixup_count==0U) ||
         (inst->opcode==LA_MC_LI64_SEQUENCE &&
          inst->target_form==LA_FORM_LI_D && plan->fixup_count==0U) ||
         (inst->opcode==LA_MC_PCREL_PSEUDO &&
          inst->target_form==LA_FORM_PCREL && plan->fixup_count==4U) ||
         (inst->opcode==LA_MC_GOT_PSEUDO &&
          inst->target_form==LA_FORM_GOT && plan->fixup_count==4U));
}

static bool la_attach_lowering_plan(MiniAs *as,MiniAsMCInst *inst,
                                    const LaLoweringPlan *source) {
    LaLoweringPlan *plan=malloc(sizeof(*plan));
    if(plan==NULL) {
        minias_set_error(as,"out-of-memory:loongarch-lowering-plan");
        return false;
    }
    *plan=*source;
    plan->tag=LA_LOWERING_PLAN_TAG;
    if(plan->count==0U || plan->count>4U || plan->fixup_count>4U) {
        free(plan);
        minias_set_error(as,"invalid-loongarch-lowering-plan");
        return false;
    }
    inst->target_payload=plan;
    inst->dispose_target_payload=free;
    return true;
}

static bool la_plan_li_d(MiniAs *as,MiniAsMCInst *inst,
                         uint64_t bits,unsigned destination) {
    LaLoweringPlan plan={0};
    plan.count=la_li_d_words(bits,destination,plan.words);
    inst->target_form=LA_FORM_LI_D;
    return la_attach_lowering_plan(as,inst,&plan);
}

static bool la_plan_li_w(MiniAs *as,MiniAsMCInst *inst,
                         uint32_t value,unsigned destination) {
    LaLoweringPlan plan={0};
    /* GAS accepts every 32-bit immediate and first sign-extends its low
     * word before running the same 12/20/20/12 reduction as LI.D.  That
     * reduction emits at most two words for a sign-extended 32-bit value;
     * reusing it also covers negative non-4K multiples such as -2049. */
    uint64_t canonical=(uint64_t)value;
    if((value&UINT32_C(0x80000000))!=0U)
        canonical|=UINT64_C(0xffffffff00000000);
    plan.count=la_li_d_words(canonical,destination,plan.words);
    if(plan.count==0U || plan.count>2U) {
        minias_set_error(as,"invalid-loongarch-liw-reduction");
        return false;
    }
    inst->target_form=LA_FORM_LI_W;
    return la_attach_lowering_plan(as,inst,&plan);
}

static bool la_plan_pcrel(MiniAs *as,MiniAsMCInst *inst,
                           unsigned destination) {
    LaLoweringPlan plan={0};
    plan.count=2U;
    plan.words[0]=UINT32_C(0x1a000000)|destination;
    plan.words[1]=UINT32_C(0x02c00000)|
        (destination<<5U)|destination;
    /* GNU GAS defaults to linker relaxation for the one-register
     * la.local/la.pcrel macro. Every PCALA relocation is immediately
     * accompanied by R_LARCH_RELAX against the ELF zero symbol. This is
     * part of the input object's semantic link contract. */
    plan.fixup_count=4U;
    plan.fixups[0].kind=MINIAS_FIXUP_LARCH_PCALA_HI20;
    plan.fixups[0].symbol_operand=1U;
    plan.fixups[1].kind=MINIAS_FIXUP_LARCH_RELAX;
    plan.fixups[1].absolute_zero=true;
    plan.fixups[2].word=1U;
    plan.fixups[2].kind=MINIAS_FIXUP_LARCH_PCALA_LO12;
    plan.fixups[2].symbol_operand=1U;
    plan.fixups[3].word=1U;
    plan.fixups[3].kind=MINIAS_FIXUP_LARCH_RELAX;
    plan.fixups[3].absolute_zero=true;
    inst->target_form=LA_FORM_PCREL;
    return la_attach_lowering_plan(as,inst,&plan);
}

/* Under GNU's ordinary LP64D code model, la.global is a GOT-address
 * materialization: the first word addresses the GOT page, and the second
 * loads the pointer from that GOT entry. It is NOT an alias of la.local;
 * the instruction and relocation types differ despite sharing the same
 * two-word fragment and optional-linker-relaxation policy. */
static bool la_plan_global_got(MiniAs *as,MiniAsMCInst *inst,
                               unsigned destination) {
    LaLoweringPlan plan={0};
    plan.count=2U;
    plan.words[0]=UINT32_C(0x1a000000)|destination;
    plan.words[1]=UINT32_C(0x28c00000)|
        (destination<<5U)|destination;
    plan.fixup_count=4U;
    plan.fixups[0].kind=MINIAS_FIXUP_LARCH_GOT_PC_HI20;
    plan.fixups[0].symbol_operand=1U;
    plan.fixups[1].kind=MINIAS_FIXUP_LARCH_RELAX;
    plan.fixups[1].absolute_zero=true;
    plan.fixups[2].word=1U;
    plan.fixups[2].kind=MINIAS_FIXUP_LARCH_GOT_PC_LO12;
    plan.fixups[2].symbol_operand=1U;
    plan.fixups[3].word=1U;
    plan.fixups[3].kind=MINIAS_FIXUP_LARCH_RELAX;
    plan.fixups[3].absolute_zero=true;
    inst->target_form=LA_FORM_GOT;
    return la_attach_lowering_plan(as,inst,&plan);
}

/* One LoongArch-owned operand grammar for LSX and LASX.  The two vector
 * extensions have different register classes and opcode tables, but share
 * their architectural field layouts.  Keeping the format description next
 * to the opcode makes their signed immediates and lane limits explicit, and
 * prevents every new instruction from introducing another private parser.
 * None of these target register classes enter Common MC or another ISA. */
typedef enum {
    LA_ARG_GPR, LA_ARG_VR, LA_ARG_XR, LA_ARG_IMM
} LaOperandClass;
typedef enum {
    LA_VECTOR_VVV, LA_VECTOR_XXX,
    LA_VECTOR_VVI, LA_VECTOR_XXI,
    LA_VECTOR_GVI, LA_VECTOR_GXI,
    LA_VECTOR_VGI, LA_VECTOR_XGI,
    LA_VECTOR_XX, LA_VECTOR_VVVV, LA_VECTOR_XXXX
} LaVectorShape;
typedef struct {
    unsigned count;
    LaOperandClass classes[4];
    unsigned shifts[4];
} LaVectorLayout;
typedef struct {
    const char *name;
    uint32_t opcode;
    LaVectorShape shape;
    int64_t immediate_min;
    int64_t immediate_max;
    unsigned immediate_bits;
} LaVectorPattern;

static const LaVectorLayout la_vector_layouts[] = {
    [LA_VECTOR_VVV]  ={3U,{LA_ARG_VR,LA_ARG_VR,LA_ARG_VR},{0U,5U,10U}},
    [LA_VECTOR_XXX]  ={3U,{LA_ARG_XR,LA_ARG_XR,LA_ARG_XR},{0U,5U,10U}},
    [LA_VECTOR_VVI]  ={3U,{LA_ARG_VR,LA_ARG_VR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_XXI]  ={3U,{LA_ARG_XR,LA_ARG_XR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_GVI]  ={3U,{LA_ARG_GPR,LA_ARG_VR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_GXI]  ={3U,{LA_ARG_GPR,LA_ARG_XR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_VGI]  ={3U,{LA_ARG_VR,LA_ARG_GPR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_XGI]  ={3U,{LA_ARG_XR,LA_ARG_GPR,LA_ARG_IMM},{0U,5U,10U}},
    [LA_VECTOR_XX]   ={2U,{LA_ARG_XR,LA_ARG_XR},{0U,5U}},
    [LA_VECTOR_VVVV] ={4U,{LA_ARG_VR,LA_ARG_VR,LA_ARG_VR,LA_ARG_VR},
                          {0U,5U,10U,15U}},
    [LA_VECTOR_XXXX] ={4U,{LA_ARG_XR,LA_ARG_XR,LA_ARG_XR,LA_ARG_XR},
                          {0U,5U,10U,15U}}
};

/* Each bound belongs to the concrete instruction, not the format: b/h/w/d
 * lane counts and shift counts must never be inferred from ISA prefixes. */
static const LaVectorPattern la_vector_patterns[] = {
    {"vld",0x2c000000U,LA_VECTOR_VGI,-2048,2047,12U},
    {"vst",0x2c400000U,LA_VECTOR_VGI,-2048,2047,12U},
    {"xvld",0x2c800000U,LA_VECTOR_XGI,-2048,2047,12U},
    {"xvst",0x2cc00000U,LA_VECTOR_XGI,-2048,2047,12U},
    {"vand.v",0x71260000U,LA_VECTOR_VVV,0,0,0U},
    {"vor.v",0x71268000U,LA_VECTOR_VVV,0,0,0U},
    {"vxor.v",0x71270000U,LA_VECTOR_VVV,0,0,0U},
    {"vnor.v",0x71278000U,LA_VECTOR_VVV,0,0,0U},
    {"vandn.v",0x71280000U,LA_VECTOR_VVV,0,0,0U},
    {"vorn.v",0x71288000U,LA_VECTOR_VVV,0,0,0U},
    {"xvand.v",0x75260000U,LA_VECTOR_XXX,0,0,0U},
    {"xvor.v",0x75268000U,LA_VECTOR_XXX,0,0,0U},
    {"xvxor.v",0x75270000U,LA_VECTOR_XXX,0,0,0U},
    {"xvnor.v",0x75278000U,LA_VECTOR_XXX,0,0,0U},
    {"xvandn.v",0x75280000U,LA_VECTOR_XXX,0,0,0U},
    {"xvorn.v",0x75288000U,LA_VECTOR_XXX,0,0,0U},
    {"vandi.b",0x73d00000U,LA_VECTOR_VVI,0,255,8U},
    {"vori.b",0x73d40000U,LA_VECTOR_VVI,0,255,8U},
    {"vxori.b",0x73d80000U,LA_VECTOR_VVI,0,255,8U},
    {"vnori.b",0x73dc0000U,LA_VECTOR_VVI,0,255,8U},
    {"xvandi.b",0x77d00000U,LA_VECTOR_XXI,0,255,8U},
    {"xvori.b",0x77d40000U,LA_VECTOR_XXI,0,255,8U},
    {"xvxori.b",0x77d80000U,LA_VECTOR_XXI,0,255,8U},
    {"xvnori.b",0x77dc0000U,LA_VECTOR_XXI,0,255,8U},
    {"vslti.b",0x72860000U,LA_VECTOR_VVI,-16,15,5U},
    {"vslti.h",0x72868000U,LA_VECTOR_VVI,-16,15,5U},
    {"vslti.w",0x72870000U,LA_VECTOR_VVI,-16,15,5U},
    {"vslti.d",0x72878000U,LA_VECTOR_VVI,-16,15,5U},
    {"xvslti.b",0x76860000U,LA_VECTOR_XXI,-16,15,5U},
    {"xvslti.h",0x76868000U,LA_VECTOR_XXI,-16,15,5U},
    {"xvslti.w",0x76870000U,LA_VECTOR_XXI,-16,15,5U},
    {"xvslti.d",0x76878000U,LA_VECTOR_XXI,-16,15,5U},
    {"vslli.b",0x732c2000U,LA_VECTOR_VVI,0,7,3U},
    {"vslli.h",0x732c4000U,LA_VECTOR_VVI,0,15,4U},
    {"vslli.w",0x732c8000U,LA_VECTOR_VVI,0,31,5U},
    {"vslli.d",0x732d0000U,LA_VECTOR_VVI,0,63,6U},
    {"vsrli.b",0x73302000U,LA_VECTOR_VVI,0,7,3U},
    {"vsrli.h",0x73304000U,LA_VECTOR_VVI,0,15,4U},
    {"vsrli.w",0x73308000U,LA_VECTOR_VVI,0,31,5U},
    {"vsrli.d",0x73310000U,LA_VECTOR_VVI,0,63,6U},
    {"vsrai.b",0x73342000U,LA_VECTOR_VVI,0,7,3U},
    {"vsrai.h",0x73344000U,LA_VECTOR_VVI,0,15,4U},
    {"vsrai.w",0x73348000U,LA_VECTOR_VVI,0,31,5U},
    {"vsrai.d",0x73350000U,LA_VECTOR_VVI,0,63,6U},
    {"xvslli.b",0x772c2000U,LA_VECTOR_XXI,0,7,3U},
    {"xvslli.h",0x772c4000U,LA_VECTOR_XXI,0,15,4U},
    {"xvslli.w",0x772c8000U,LA_VECTOR_XXI,0,31,5U},
    {"xvslli.d",0x772d0000U,LA_VECTOR_XXI,0,63,6U},
    {"xvsrli.b",0x77302000U,LA_VECTOR_XXI,0,7,3U},
    {"xvsrli.h",0x77304000U,LA_VECTOR_XXI,0,15,4U},
    {"xvsrli.w",0x77308000U,LA_VECTOR_XXI,0,31,5U},
    {"xvsrli.d",0x77310000U,LA_VECTOR_XXI,0,63,6U},
    {"vpickve2gr.b",0x72ef8000U,LA_VECTOR_GVI,0,15,4U},
    {"vpickve2gr.h",0x72efc000U,LA_VECTOR_GVI,0,7,3U},
    {"vpickve2gr.w",0x72efe000U,LA_VECTOR_GVI,0,3,2U},
    {"vpickve2gr.d",0x72eff000U,LA_VECTOR_GVI,0,1,1U},
    {"vpickve2gr.bu",0x72f38000U,LA_VECTOR_GVI,0,15,4U},
    {"vpickve2gr.hu",0x72f3c000U,LA_VECTOR_GVI,0,7,3U},
    {"vpickve2gr.wu",0x72f3e000U,LA_VECTOR_GVI,0,3,2U},
    {"vpickve2gr.du",0x72f3f000U,LA_VECTOR_GVI,0,1,1U},
    {"xvpickve2gr.w",0x76efc000U,LA_VECTOR_GXI,0,7,3U},
    {"xvpickve2gr.d",0x76efe000U,LA_VECTOR_GXI,0,3,2U},
    {"xvpickve2gr.wu",0x76f3c000U,LA_VECTOR_GXI,0,7,3U},
    {"xvpickve2gr.du",0x76f3e000U,LA_VECTOR_GXI,0,3,2U},
    {"vinsgr2vr.b",0x72eb8000U,LA_VECTOR_VGI,0,15,4U},
    {"vinsgr2vr.h",0x72ebc000U,LA_VECTOR_VGI,0,7,3U},
    {"vinsgr2vr.w",0x72ebe000U,LA_VECTOR_VGI,0,3,2U},
    {"vinsgr2vr.d",0x72ebf000U,LA_VECTOR_VGI,0,1,1U},
    {"xvinsgr2vr.w",0x76ebc000U,LA_VECTOR_XGI,0,7,3U},
    {"xvinsgr2vr.d",0x76ebe000U,LA_VECTOR_XGI,0,3,2U},
    {"xvpermi.w",0x77e40000U,LA_VECTOR_XXI,0,255,8U},
    {"xvpermi.d",0x77e80000U,LA_VECTOR_XXI,0,255,8U},
    {"xvpermi.q",0x77ec0000U,LA_VECTOR_XXI,0,255,8U},
    {"xvreplve0.b",0x77070000U,LA_VECTOR_XX,0,0,0U},
    {"xvreplve0.h",0x77078000U,LA_VECTOR_XX,0,0,0U},
    {"xvreplve0.w",0x7707c000U,LA_VECTOR_XX,0,0,0U},
    {"xvreplve0.d",0x7707e000U,LA_VECTOR_XX,0,0,0U},
    {"xvreplve0.q",0x7707f000U,LA_VECTOR_XX,0,0,0U},
    {"vshuf.b",0x0d500000U,LA_VECTOR_VVVV,0,0,0U},
    {"vbitsel.v",0x0d100000U,LA_VECTOR_VVVV,0,0,0U},
    {"xvshuf.b",0x0d600000U,LA_VECTOR_XXXX,0,0,0U},
    {"xvbitsel.v",0x0d200000U,LA_VECTOR_XXXX,0,0,0U}
};

static bool la_parse_vector_pattern(MiniAsMCInst *m,char *args[4],
                                    unsigned count,unsigned index) {
    const LaVectorPattern *pattern;
    const LaVectorLayout *layout;
    unsigned arg;
    if(index>=LA_COUNT(la_vector_patterns))return false;
    pattern=&la_vector_patterns[index];
    layout=&la_vector_layouts[pattern->shape];
    if(count!=layout->count)return false;
    for(arg=0U;arg<count;++arg) {
        LaOperandClass cls=layout->classes[arg];
        unsigned reg=0U;
        int64_t value=0;
        bool valid=false;
        if(cls==LA_ARG_GPR)valid=la_reg(args[arg],&reg);
        else if(cls==LA_ARG_VR)valid=la_special_reg(args[arg],"$vr",31U,&reg);
        else if(cls==LA_ARG_XR)valid=la_special_reg(args[arg],"$xr",31U,&reg);
        else {
            valid=minias_eval_i64_expression(args[arg],&value) &&
                  value>=pattern->immediate_min &&
                  value<=pattern->immediate_max;
        }
        if(!valid)return false;
        if(cls==LA_ARG_IMM) {
            if(pattern->immediate_bits==0U ||
               pattern->immediate_bits>12U)return false;
            m->operands[arg].kind=MINIAS_MC_IMMEDIATE;
            m->operands[arg].immediate=(uint64_t)value;
        } else {
            m->operands[arg].kind=MINIAS_MC_REGISTER;
            m->operands[arg].reg=reg;
            la_set_register_bank(&m->operands[arg],
                cls==LA_ARG_GPR ? LA_REG_BANK_GPR :
                cls==LA_ARG_VR ? LA_REG_BANK_VR128 : LA_REG_BANK_XR256);
        }
    }
    m->count=count;
    m->target_form=(uint32_t)LA_FORM_VECTOR+index;
    return true;
}

/* Populate semantic MC operands even for fixed-width scalar words. The ELF
 * encoder happens to need only the stored word today, but a target-specific
 * analysis/verification pass must not have to reparse source text to recover
 * an instruction's register and signed-immediate operands. */
static void la_capture_gpr_operand(MiniAsMCInst *m,unsigned slot,unsigned reg) {
    m->operands[slot].kind=MINIAS_MC_REGISTER;
    m->operands[slot].reg=reg;
    la_set_register_bank(&m->operands[slot],LA_REG_BANK_GPR);
}
static void la_capture_imm_operand(MiniAsMCInst *m,unsigned slot,int64_t value) {
    m->operands[slot].kind=MINIAS_MC_IMMEDIATE;
    m->operands[slot].immediate=(uint64_t)value;
}
static void la_capture_rrr(MiniAsMCInst *m,unsigned rd,unsigned rj,unsigned rk) {
    la_capture_gpr_operand(m,0U,rd);
    la_capture_gpr_operand(m,1U,rj);
    la_capture_gpr_operand(m,2U,rk);
}
static void la_capture_rri(MiniAsMCInst *m,unsigned rd,unsigned rj,int64_t imm) {
    la_capture_gpr_operand(m,0U,rd);
    la_capture_gpr_operand(m,1U,rj);
    la_capture_imm_operand(m,2U,imm);
}

/* FPR, FCC and FCSR are three different target register banks; CSR, TLB and
 * immediate-bearing system opcodes also have target-private operand grammar.
 * One descriptor keeps the ISA bank, range and bit placement inseparable from
 * the opcode. Common MC still owns only the generic typed operand lifecycle.
 * FCC/FCSR bank identity is validated here until Common has a generic way of
 * describing condition/control registers without encoding one ISA into it. */
typedef enum {
    LA_BANK_GPR, LA_BANK_FPR, LA_BANK_FCSR, LA_BANK_FCC, LA_BANK_SCR,
    LA_BANK_SIGNED12, LA_BANK_UNSIGNED5, LA_BANK_UNSIGNED8,
    LA_BANK_UNSIGNED14, LA_BANK_UNSIGNED15
} LaRegisterOperandBank;
typedef struct {
    const char *name;
    uint32_t opcode;
    unsigned count;
    LaRegisterOperandBank banks[4];
    unsigned shifts[4];
    unsigned min_register[4];
} LaSpecialRegisterPattern;

#define LA_FIELDS_2  {0U,5U},{0U}
#define LA_FIELDS_3  {0U,5U,10U},{0U}
/* LoongArch scalar FP uses exactly the same indexed form encoder and typed
 * register ownership as the existing FCSR/FCC instructions.  These records
 * follow GNU binutils opcodes/loongarch-opc.c f0/f5/f10/f15 and c0 formats;
 * only the Target's format table changes when the instruction set grows. */
#define LA_FPR2(n,o) {n,o,2U,{LA_BANK_FPR,LA_BANK_FPR},LA_FIELDS_2}
#define LA_FPR3(n,o) {n,o,3U,{LA_BANK_FPR,LA_BANK_FPR,LA_BANK_FPR},LA_FIELDS_3}
#define LA_FCC3(n,o) {n,o,3U,{LA_BANK_FCC,LA_BANK_FPR,LA_BANK_FPR},LA_FIELDS_3}
/* GNU comparison aliases Gt/Ge/Ugt/Uge reverse the two FPR fields; retain
 * that swap in the descriptor's shifts instead of creating another parser. */
#define LA_FCC3_SWAP(n,o) {n,o,3U,{LA_BANK_FCC,LA_BANK_FPR,LA_BANK_FPR},{0U,10U,5U},{0U}}
#define LA_FPR4(n,o) {n,o,4U,{LA_BANK_FPR,LA_BANK_FPR,LA_BANK_FPR,LA_BANK_FPR},{0U,5U,10U,15U},{0U}}
static const LaSpecialRegisterPattern la_special_register_patterns[] = {
    {"movgr2fr.d",0x0114a800U,2U,{LA_BANK_FPR,LA_BANK_GPR},LA_FIELDS_2},
    {"movgr2fr.w",0x0114a400U,2U,{LA_BANK_FPR,LA_BANK_GPR},LA_FIELDS_2},
    {"movfr2gr.d",0x0114b800U,2U,{LA_BANK_GPR,LA_BANK_FPR},LA_FIELDS_2},
    {"movfr2gr.s",0x0114b400U,2U,{LA_BANK_GPR,LA_BANK_FPR},LA_FIELDS_2},
    {"movfrh2gr.s",0x0114bc00U,2U,{LA_BANK_GPR,LA_BANK_FPR},LA_FIELDS_2},
    {"movgr2frh.w",0x0114ac00U,2U,{LA_BANK_FPR,LA_BANK_GPR},LA_FIELDS_2},
    {"movfcsr2gr",0x0114c800U,2U,{LA_BANK_GPR,LA_BANK_FCSR},LA_FIELDS_2},
    {"movgr2fcsr",0x0114c000U,2U,{LA_BANK_FCSR,LA_BANK_GPR},LA_FIELDS_2},
    /* GNU binutils loongarch-opc.c: LBT SCR is a distinct 2-bit bank.
     * 0x00000c00 r0:5,cr5:2; 0x00000800 cr0:2,r5:5.
     * The former is Linux's real AS_HAS_LBT_EXTENSION capability probe. */
    {"movscr2gr",0x00000c00U,2U,{LA_BANK_GPR,LA_BANK_SCR},LA_FIELDS_2},
    {"movgr2scr",0x00000800U,2U,{LA_BANK_SCR,LA_BANK_GPR},LA_FIELDS_2},
    {"movcf2gr",0x0114dc00U,2U,{LA_BANK_GPR,LA_BANK_FCC},LA_FIELDS_2},
    {"movgr2cf",0x0114d800U,2U,{LA_BANK_FCC,LA_BANK_GPR},LA_FIELDS_2},
    {"movcf2fr",0x0114d400U,2U,{LA_BANK_FPR,LA_BANK_FCC},LA_FIELDS_2},
    {"movfr2cf",0x0114d000U,2U,{LA_BANK_FCC,LA_BANK_FPR},LA_FIELDS_2},
    LA_FPR3("fadd.s",0x01008000U),
    LA_FPR3("fsub.s",0x01028000U),
    LA_FPR3("fmul.s",0x01048000U),
    LA_FPR3("fdiv.s",0x01068000U),
    LA_FPR3("fmax.s",0x01088000U),
    LA_FPR3("fmin.s",0x010a8000U),
    LA_FPR3("fmaxa.s",0x010c8000U),
    LA_FPR3("fmina.s",0x010e8000U),
    LA_FPR3("fscaleb.s",0x01108000U),
    LA_FPR3("fcopysign.s",0x01128000U),
    LA_FPR3("fadd.d",0x01010000U),
    LA_FPR3("fsub.d",0x01030000U),
    LA_FPR3("fmul.d",0x01050000U),
    LA_FPR3("fdiv.d",0x01070000U),
    LA_FPR3("fmax.d",0x01090000U),
    LA_FPR3("fmin.d",0x010b0000U),
    LA_FPR3("fmaxa.d",0x010d0000U),
    LA_FPR3("fmina.d",0x010f0000U),
    LA_FPR3("fscaleb.d",0x01110000U),
    LA_FPR3("fcopysign.d",0x01130000U),
    LA_FPR2("fabs.s",0x01140400U),
    LA_FPR2("fneg.s",0x01141400U),
    LA_FPR2("flogb.s",0x01142400U),
    LA_FPR2("fclass.s",0x01143400U),
    LA_FPR2("fsqrt.s",0x01144400U),
    LA_FPR2("frecip.s",0x01145400U),
    LA_FPR2("frsqrt.s",0x01146400U),
    LA_FPR2("frecipe.s",0x01147400U),
    LA_FPR2("frsqrte.s",0x01148400U),
    LA_FPR2("fmov.s",0x01149400U),
    LA_FPR2("fabs.d",0x01140800U),
    LA_FPR2("fneg.d",0x01141800U),
    LA_FPR2("flogb.d",0x01142800U),
    LA_FPR2("fclass.d",0x01143800U),
    LA_FPR2("fsqrt.d",0x01144800U),
    LA_FPR2("frecip.d",0x01145800U),
    LA_FPR2("frsqrt.d",0x01146800U),
    LA_FPR2("frecipe.d",0x01147800U),
    LA_FPR2("frsqrte.d",0x01148800U),
    LA_FPR2("fmov.d",0x01149800U),
    LA_FPR2("fcvt.s.d",0x01191800U),
    LA_FPR2("fcvt.d.s",0x01192400U),
    LA_FPR2("ftintrm.w.s",0x011a0400U),
    LA_FPR2("ftintrm.l.s",0x011a2400U),
    LA_FPR2("ftintrp.w.s",0x011a4400U),
    LA_FPR2("ftintrp.l.s",0x011a6400U),
    LA_FPR2("ftintrz.w.s",0x011a8400U),
    LA_FPR2("ftintrz.l.s",0x011aa400U),
    LA_FPR2("ftintrne.w.s",0x011ac400U),
    LA_FPR2("ftintrne.l.s",0x011ae400U),
    LA_FPR2("ftint.w.s",0x011b0400U),
    LA_FPR2("ftint.l.s",0x011b2400U),
    LA_FPR2("ffint.s.w",0x011d1000U),
    LA_FPR2("ffint.s.l",0x011d1800U),
    LA_FPR2("frint.s",0x011e4400U),
    LA_FPR2("ftintrm.w.d",0x011a0800U),
    LA_FPR2("ftintrm.l.d",0x011a2800U),
    LA_FPR2("ftintrp.w.d",0x011a4800U),
    LA_FPR2("ftintrp.l.d",0x011a6800U),
    LA_FPR2("ftintrz.w.d",0x011a8800U),
    LA_FPR2("ftintrz.l.d",0x011aa800U),
    LA_FPR2("ftintrne.w.d",0x011ac800U),
    LA_FPR2("ftintrne.l.d",0x011ae800U),
    LA_FPR2("ftint.w.d",0x011b0800U),
    LA_FPR2("ftint.l.d",0x011b2800U),
    LA_FPR2("ffint.d.w",0x011d2000U),
    LA_FPR2("ffint.d.l",0x011d2800U),
    LA_FPR2("frint.d",0x011e4800U),
    LA_FPR4("fmadd.d",0x08200000U),
    LA_FPR4("fmsub.d",0x08600000U),
    LA_FPR4("fnmadd.d",0x08a00000U),
    LA_FPR4("fnmsub.d",0x08e00000U),
    LA_FPR4("fmadd.s",0x08100000U),
    LA_FPR4("fmsub.s",0x08500000U),
    LA_FPR4("fnmadd.s",0x08900000U),
    LA_FPR4("fnmsub.s",0x08d00000U),
    {"fsel",0x0d000000U,4U,
      {LA_BANK_FPR,LA_BANK_FPR,LA_BANK_FPR,LA_BANK_FCC},
      {0U,5U,10U,15U},{0U}},
    LA_FCC3("fcmp.caf.d",0x0c200000U),
    LA_FCC3("fcmp.saf.d",0x0c208000U),
    LA_FCC3("fcmp.clt.d",0x0c210000U),
    LA_FCC3("fcmp.slt.d",0x0c218000U),
    LA_FCC3_SWAP("fcmp.sgt.d",0x0c218000U),
    LA_FCC3("fcmp.ceq.d",0x0c220000U),
    LA_FCC3("fcmp.seq.d",0x0c228000U),
    LA_FCC3("fcmp.cle.d",0x0c230000U),
    LA_FCC3("fcmp.sle.d",0x0c238000U),
    LA_FCC3_SWAP("fcmp.sge.d",0x0c238000U),
    LA_FCC3("fcmp.cun.d",0x0c240000U),
    LA_FCC3("fcmp.sun.d",0x0c248000U),
    LA_FCC3("fcmp.cult.d",0x0c250000U),
    LA_FCC3_SWAP("fcmp.cugt.d",0x0c250000U),
    LA_FCC3("fcmp.sult.d",0x0c258000U),
    LA_FCC3("fcmp.cueq.d",0x0c260000U),
    LA_FCC3("fcmp.sueq.d",0x0c268000U),
    LA_FCC3("fcmp.cule.d",0x0c270000U),
    LA_FCC3_SWAP("fcmp.cuge.d",0x0c270000U),
    LA_FCC3("fcmp.sule.d",0x0c278000U),
    LA_FCC3("fcmp.cne.d",0x0c280000U),
    LA_FCC3("fcmp.sne.d",0x0c288000U),
    LA_FCC3("fcmp.cor.d",0x0c2a0000U),
    LA_FCC3("fcmp.sor.d",0x0c2a8000U),
    LA_FCC3("fcmp.cune.d",0x0c2c0000U),
    LA_FCC3("fcmp.sune.d",0x0c2c8000U),
    LA_FCC3("fcmp.caf.s",0x0c100000U),
    LA_FCC3("fcmp.saf.s",0x0c108000U),
    LA_FCC3("fcmp.clt.s",0x0c110000U),
    LA_FCC3("fcmp.slt.s",0x0c118000U),
    LA_FCC3_SWAP("fcmp.sgt.s",0x0c118000U),
    LA_FCC3("fcmp.ceq.s",0x0c120000U),
    LA_FCC3("fcmp.seq.s",0x0c128000U),
    LA_FCC3("fcmp.cle.s",0x0c130000U),
    LA_FCC3("fcmp.sle.s",0x0c138000U),
    LA_FCC3_SWAP("fcmp.sge.s",0x0c138000U),
    LA_FCC3("fcmp.cun.s",0x0c140000U),
    LA_FCC3("fcmp.sun.s",0x0c148000U),
    LA_FCC3("fcmp.cult.s",0x0c150000U),
    LA_FCC3_SWAP("fcmp.cugt.s",0x0c150000U),
    LA_FCC3("fcmp.sult.s",0x0c158000U),
    LA_FCC3("fcmp.cueq.s",0x0c160000U),
    LA_FCC3("fcmp.sueq.s",0x0c168000U),
    LA_FCC3("fcmp.cule.s",0x0c170000U),
    LA_FCC3_SWAP("fcmp.cuge.s",0x0c170000U),
    LA_FCC3("fcmp.sule.s",0x0c178000U),
    LA_FCC3("fcmp.cne.s",0x0c180000U),
    LA_FCC3("fcmp.sne.s",0x0c188000U),
    LA_FCC3("fcmp.cor.s",0x0c1a0000U),
    LA_FCC3("fcmp.sor.s",0x0c1a8000U),
    LA_FCC3("fcmp.cune.s",0x0c1c0000U),
    LA_FCC3("fcmp.sune.s",0x0c1c8000U),
    {"fld.s",0x2b000000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fst.s",0x2b400000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fld.d",0x2b800000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fst.d",0x2bc00000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    /* Register-indexed scalar FP memory instructions use a GPR byte
     * offset, not the SIGNED12 immediate of FLD/FST. One typed register
     * descriptor covers the whole original GNU LoongArch LP64 family. */
    {"fldx.s",0x38300000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fldx.d",0x38340000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstx.s",0x38380000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstx.d",0x383c0000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fldgt.s",0x38740000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fldgt.d",0x38748000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fldle.s",0x38750000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fldle.d",0x38758000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstgt.s",0x38760000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstgt.d",0x38768000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstle.s",0x38770000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"fstle.d",0x38778000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"lddir",0x06400000U,3U,{LA_BANK_GPR,LA_BANK_GPR,LA_BANK_UNSIGNED8},LA_FIELDS_3},
    {"ldpte",0x06440000U,2U,{LA_BANK_GPR,LA_BANK_UNSIGNED8},{5U,10U},{0U}},
    {"invtlb",0x06498000U,3U,{LA_BANK_UNSIGNED5,LA_BANK_GPR,LA_BANK_GPR},LA_FIELDS_3},
    {"cacop",0x06000000U,3U,{LA_BANK_UNSIGNED5,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"csrrd",0x04000000U,2U,{LA_BANK_GPR,LA_BANK_UNSIGNED14},{0U,10U},{0U}},
    {"csrwr",0x04000020U,2U,{LA_BANK_GPR,LA_BANK_UNSIGNED14},{0U,10U},{0U}},
    {"csrxchg",0x04000000U,3U,{LA_BANK_GPR,LA_BANK_GPR,LA_BANK_UNSIGNED14},{0U,5U,10U},{0U,2U}},
    {"break",0x002a0000U,1U,{LA_BANK_UNSIGNED15},{0U},{0U}},
    {"syscall",0x002b0000U,1U,{LA_BANK_UNSIGNED15},{0U},{0U}},
    {"dbar",0x38720000U,1U,{LA_BANK_UNSIGNED15},{0U},{0U}},
    {"ibar",0x38728000U,1U,{LA_BANK_UNSIGNED15},{0U},{0U}},
    {"idle",0x06488000U,1U,{LA_BANK_UNSIGNED15},{0U},{0U}}
};
#undef LA_FPR2
#undef LA_FPR3
#undef LA_FCC3
#undef LA_FCC3_SWAP
#undef LA_FPR4
#undef LA_FIELDS_2
#undef LA_FIELDS_3

static bool la_parse_special_register_pattern(MiniAsMCInst *m,
                                               char *args[4],unsigned count,
                                               unsigned index) {
    const LaSpecialRegisterPattern *rule;
    unsigned arg;
    if(index>=LA_COUNT(la_special_register_patterns))return false;
    rule=&la_special_register_patterns[index];
    if(count!=rule->count)return false;
    for(arg=0U;arg<count;++arg) {
        unsigned reg=0U;
        int64_t imm=0;
        bool valid=false, immediate=false;
        unsigned width=0U;
        switch(rule->banks[arg]) {
            case LA_BANK_GPR:valid=la_reg(args[arg],&reg);break;
            case LA_BANK_FPR:
                valid=la_fpr_reg(args[arg],&reg);break;
            case LA_BANK_FCSR:
                /* Linux's GCC LoongArch backend writes numeric $r0 for the
                 * FCSR0 operand of movfcsr2gr/movgr2fcsr.  GAS accepts this
                 * register spelling even though the hardware operand is the
                 * *FCSR bank*, not a GPR.  Accept only FCSR indices 0..3;
                 * preserve the typed bank and reject $r4..$r31. */
                valid=la_special_reg(args[arg],"$fcsr",3U,&reg) ||
                      la_special_reg(args[arg],"$r",3U,&reg);
                break;
            case LA_BANK_FCC:
                valid=la_special_reg(args[arg],"$fcc",7U,&reg);break;
            case LA_BANK_SCR:
                valid=la_special_reg(args[arg],"$scr",3U,&reg);break;
            case LA_BANK_SIGNED12:
                valid=minias_eval_i64_expression(args[arg],&imm) &&
                      imm>=-2048 && imm<=2047;
                immediate=true;width=12U;
                break;
            case LA_BANK_UNSIGNED5:
            case LA_BANK_UNSIGNED8:
            case LA_BANK_UNSIGNED14:
            case LA_BANK_UNSIGNED15: {
                uint64_t uimm;
                width=rule->banks[arg]==LA_BANK_UNSIGNED5?5U:
                      rule->banks[arg]==LA_BANK_UNSIGNED8?8U:
                      rule->banks[arg]==LA_BANK_UNSIGNED14?14U:15U;
                valid=minias_eval_u64_expression(args[arg],&uimm) &&
                      uimm<(UINT64_C(1)<<width);
                if(valid)imm=(int64_t)uimm;
                immediate=true;
                break;
            }
        }
        if(!valid || (!immediate && reg<rule->min_register[arg]))
            return false;
        if(immediate) {
            la_capture_imm_operand(m,arg,imm);
        } else {
            la_capture_gpr_operand(m,arg,reg);
            la_set_register_bank(&m->operands[arg],
                rule->banks[arg]==LA_BANK_FPR ? LA_REG_BANK_FPR :
                rule->banks[arg]==LA_BANK_FCSR ? LA_REG_BANK_FCSR :
                rule->banks[arg]==LA_BANK_FCC ? LA_REG_BANK_FCC :
                rule->banks[arg]==LA_BANK_SCR ? LA_REG_BANK_SCR :
                LA_REG_BANK_GPR);
        }
    }
    m->count=count;
    m->target_form=(uint32_t)LA_FORM_SPECIAL_REGISTER+index;
    return true;
}

/* One target-owned scalar grammar.  A scalar opcode is an ISA encoding plus
 * an operand form, not another control-flow arm in the assembler's parser.
 * Keep this local to LoongArch; neither Common MC nor AArch64 should know
 * how an LA register or immediate occupies its instruction word. */
typedef enum {
    LA_SCALAR_GPR3, LA_SCALAR_GPR2, LA_SCALAR_IMM12,
    LA_SCALAR_UIMM12, LA_SCALAR_SHIFT5, LA_SCALAR_SHIFT6,
    LA_SCALAR_PTR14, LA_SCALAR_UPPER20, LA_SCALAR_ATOMIC
} LaScalarShape;
typedef struct {
    const char *name;
    uint32_t opcode;
    LaScalarShape shape;
} LaScalarPattern;

static const LaScalarPattern la_scalar_patterns[] = {
    {"add.d",0x00108000U,LA_SCALAR_GPR3},
    {"sub.d",0x00118000U,LA_SCALAR_GPR3},
    {"add.w",0x00100000U,LA_SCALAR_GPR3},
    {"sub.w",0x00110000U,LA_SCALAR_GPR3},
    {"and",0x00148000U,LA_SCALAR_GPR3},
    {"or",0x00150000U,LA_SCALAR_GPR3},
    {"xor",0x00158000U,LA_SCALAR_GPR3},
    {"slt",0x00120000U,LA_SCALAR_GPR3},
    {"sltu",0x00128000U,LA_SCALAR_GPR3},
    {"maskeqz",0x00130000U,LA_SCALAR_GPR3},
    {"masknez",0x00138000U,LA_SCALAR_GPR3},
    {"nor",0x00140000U,LA_SCALAR_GPR3},
    {"orn",0x00160000U,LA_SCALAR_GPR3},
    {"andn",0x00168000U,LA_SCALAR_GPR3},
    {"sll.w",0x00170000U,LA_SCALAR_GPR3},
    {"srl.w",0x00178000U,LA_SCALAR_GPR3},
    {"sra.w",0x00180000U,LA_SCALAR_GPR3},
    {"sll.d",0x00188000U,LA_SCALAR_GPR3},
    {"srl.d",0x00190000U,LA_SCALAR_GPR3},
    {"sra.d",0x00198000U,LA_SCALAR_GPR3},
    {"rotr.w",0x001b0000U,LA_SCALAR_GPR3},
    {"rotr.d",0x001b8000U,LA_SCALAR_GPR3},
    {"mul.w",0x001c0000U,LA_SCALAR_GPR3},
    {"mul.d",0x001d8000U,LA_SCALAR_GPR3},
    {"mulh.w",0x001c8000U,LA_SCALAR_GPR3},
    {"mulh.wu",0x001d0000U,LA_SCALAR_GPR3},
    {"mulh.d",0x001e0000U,LA_SCALAR_GPR3},
    {"mulh.du",0x001e8000U,LA_SCALAR_GPR3},
    {"div.w",0x00200000U,LA_SCALAR_GPR3},
    {"mod.w",0x00208000U,LA_SCALAR_GPR3},
    {"div.wu",0x00210000U,LA_SCALAR_GPR3},
    {"mod.wu",0x00218000U,LA_SCALAR_GPR3},
    {"div.d",0x00220000U,LA_SCALAR_GPR3},
    {"mod.d",0x00228000U,LA_SCALAR_GPR3},
    {"div.du",0x00230000U,LA_SCALAR_GPR3},
    {"mod.du",0x00238000U,LA_SCALAR_GPR3},
    {"ldx.b",0x38000000U,LA_SCALAR_GPR3},
    {"ldx.h",0x38040000U,LA_SCALAR_GPR3},
    {"ldx.w",0x38080000U,LA_SCALAR_GPR3},
    {"ldx.d",0x380c0000U,LA_SCALAR_GPR3},
    {"stx.b",0x38100000U,LA_SCALAR_GPR3},
    {"stx.h",0x38140000U,LA_SCALAR_GPR3},
    {"stx.w",0x38180000U,LA_SCALAR_GPR3},
    {"stx.d",0x381c0000U,LA_SCALAR_GPR3},
    {"ldx.bu",0x38200000U,LA_SCALAR_GPR3},
    {"ldx.hu",0x38240000U,LA_SCALAR_GPR3},
    {"ldx.wu",0x38280000U,LA_SCALAR_GPR3},
    {"addi.d",0x02c00000U,LA_SCALAR_IMM12},
    {"addi.w",0x02800000U,LA_SCALAR_IMM12},
    {"slti",0x02000000U,LA_SCALAR_IMM12},
    {"sltui",0x02400000U,LA_SCALAR_IMM12},
    {"lu52i.d",0x03000000U,LA_SCALAR_IMM12},
    {"ld.d",0x28c00000U,LA_SCALAR_IMM12},
    {"st.d",0x29c00000U,LA_SCALAR_IMM12},
    {"ld.w",0x28800000U,LA_SCALAR_IMM12},
    {"st.w",0x29800000U,LA_SCALAR_IMM12},
    {"ld.b",0x28000000U,LA_SCALAR_IMM12},
    {"ld.h",0x28400000U,LA_SCALAR_IMM12},
    {"st.b",0x29000000U,LA_SCALAR_IMM12},
    {"st.h",0x29400000U,LA_SCALAR_IMM12},
    {"ld.bu",0x2a000000U,LA_SCALAR_IMM12},
    {"ld.hu",0x2a400000U,LA_SCALAR_IMM12},
    {"ld.wu",0x2a800000U,LA_SCALAR_IMM12},
    {"ll.w",0x20000000U,LA_SCALAR_PTR14},
    {"sc.w",0x21000000U,LA_SCALAR_PTR14},
    {"ll.d",0x22000000U,LA_SCALAR_PTR14},
    {"sc.d",0x23000000U,LA_SCALAR_PTR14},
    {"ldptr.w",0x24000000U,LA_SCALAR_PTR14},
    {"stptr.w",0x25000000U,LA_SCALAR_PTR14},
    {"ldptr.d",0x26000000U,LA_SCALAR_PTR14},
    {"stptr.d",0x27000000U,LA_SCALAR_PTR14},
    {"andi",0x03400000U,LA_SCALAR_UIMM12},
    {"ori",0x03800000U,LA_SCALAR_UIMM12},
    {"xori",0x03c00000U,LA_SCALAR_UIMM12},
    {"slli.w",0x00408000U,LA_SCALAR_SHIFT5},
    {"slli.d",0x00410000U,LA_SCALAR_SHIFT6},
    {"srli.w",0x00448000U,LA_SCALAR_SHIFT5},
    {"srli.d",0x00450000U,LA_SCALAR_SHIFT6},
    {"srai.w",0x00488000U,LA_SCALAR_SHIFT5},
    {"srai.d",0x00490000U,LA_SCALAR_SHIFT6},
    {"rotri.w",0x004c8000U,LA_SCALAR_SHIFT5},
    {"rotri.d",0x004d0000U,LA_SCALAR_SHIFT6},
    {"lu12i.w",0x14000000U,LA_SCALAR_UPPER20},
    {"lu32i.d",0x16000000U,LA_SCALAR_UPPER20},
    {"pcaddi",0x18000000U,LA_SCALAR_UPPER20},
    {"pcalau12i",0x1a000000U,LA_SCALAR_UPPER20},
    {"pcaddu12i",0x1c000000U,LA_SCALAR_UPPER20},
    {"pcaddu18i",0x1e000000U,LA_SCALAR_UPPER20},
    {"clo.w",0x00001000U,LA_SCALAR_GPR2},
    {"clz.w",0x00001400U,LA_SCALAR_GPR2},
    {"cto.w",0x00001800U,LA_SCALAR_GPR2},
    {"ctz.w",0x00001c00U,LA_SCALAR_GPR2},
    {"clo.d",0x00002000U,LA_SCALAR_GPR2},
    {"clz.d",0x00002400U,LA_SCALAR_GPR2},
    {"cto.d",0x00002800U,LA_SCALAR_GPR2},
    {"ctz.d",0x00002c00U,LA_SCALAR_GPR2},
    {"revb.2h",0x00003000U,LA_SCALAR_GPR2},
    {"revb.4h",0x00003400U,LA_SCALAR_GPR2},
    {"revb.2w",0x00003800U,LA_SCALAR_GPR2},
    {"revb.d",0x00003c00U,LA_SCALAR_GPR2},
    {"revh.2w",0x00004000U,LA_SCALAR_GPR2},
    {"revh.d",0x00004400U,LA_SCALAR_GPR2},
    {"bitrev.4b",0x00004800U,LA_SCALAR_GPR2},
    {"bitrev.8b",0x00004c00U,LA_SCALAR_GPR2},
    {"bitrev.w",0x00005000U,LA_SCALAR_GPR2},
    {"bitrev.d",0x00005400U,LA_SCALAR_GPR2},
    {"ext.w.h",0x00005800U,LA_SCALAR_GPR2},
    {"ext.w.b",0x00005c00U,LA_SCALAR_GPR2},
    {"rdtimel.w",0x00006000U,LA_SCALAR_GPR2},
    {"rdtimeh.w",0x00006400U,LA_SCALAR_GPR2},
    {"rdtime.d",0x00006800U,LA_SCALAR_GPR2},
    {"cpucfg",0x00006c00U,LA_SCALAR_GPR2},
    {"iocsrrd.b",0x06480000U,LA_SCALAR_GPR2},
    {"iocsrrd.h",0x06480400U,LA_SCALAR_GPR2},
    {"iocsrrd.w",0x06480800U,LA_SCALAR_GPR2},
    {"iocsrrd.d",0x06480c00U,LA_SCALAR_GPR2},
    {"iocsrwr.b",0x06481000U,LA_SCALAR_GPR2},
    {"iocsrwr.h",0x06481400U,LA_SCALAR_GPR2},
    {"iocsrwr.w",0x06481800U,LA_SCALAR_GPR2},
    {"iocsrwr.d",0x06481c00U,LA_SCALAR_GPR2},
    {"amswap.w",0x38600000U,LA_SCALAR_ATOMIC},
    {"amswap.d",0x38608000U,LA_SCALAR_ATOMIC},
    {"amadd.w",0x38610000U,LA_SCALAR_ATOMIC},
    {"amadd.d",0x38618000U,LA_SCALAR_ATOMIC},
    {"amand.w",0x38620000U,LA_SCALAR_ATOMIC},
    {"amand.d",0x38628000U,LA_SCALAR_ATOMIC},
    {"amor.w",0x38630000U,LA_SCALAR_ATOMIC},
    {"amor.d",0x38638000U,LA_SCALAR_ATOMIC},
    {"amxor.w",0x38640000U,LA_SCALAR_ATOMIC},
    {"amxor.d",0x38648000U,LA_SCALAR_ATOMIC},
    {"ammax.w",0x38650000U,LA_SCALAR_ATOMIC},
    {"ammax.d",0x38658000U,LA_SCALAR_ATOMIC},
    {"ammin.w",0x38660000U,LA_SCALAR_ATOMIC},
    {"ammin.d",0x38668000U,LA_SCALAR_ATOMIC},
    {"ammax.wu",0x38670000U,LA_SCALAR_ATOMIC},
    {"ammax.du",0x38678000U,LA_SCALAR_ATOMIC},
    {"ammin.wu",0x38680000U,LA_SCALAR_ATOMIC},
    {"ammin.du",0x38688000U,LA_SCALAR_ATOMIC},
    {"amswap_db.w",0x38690000U,LA_SCALAR_ATOMIC},
    {"amswap_db.d",0x38698000U,LA_SCALAR_ATOMIC},
    {"amadd_db.w",0x386a0000U,LA_SCALAR_ATOMIC},
    {"amadd_db.d",0x386a8000U,LA_SCALAR_ATOMIC},
    {"amand_db.w",0x386b0000U,LA_SCALAR_ATOMIC},
    {"amand_db.d",0x386b8000U,LA_SCALAR_ATOMIC},
    {"amor_db.w",0x386c0000U,LA_SCALAR_ATOMIC},
    {"amor_db.d",0x386c8000U,LA_SCALAR_ATOMIC},
    {"amxor_db.w",0x386d0000U,LA_SCALAR_ATOMIC},
    {"amxor_db.d",0x386d8000U,LA_SCALAR_ATOMIC}
};

static bool la_parse_scalar_pattern(MiniAsMCInst *m,char *args[4],
                                    unsigned count,unsigned index) {
    const LaScalarPattern *rule;
    unsigned rd=0U,rj=0U,rk=0U,need;
    int64_t value=0;
    if(index>=LA_COUNT(la_scalar_patterns))return false;
    rule=&la_scalar_patterns[index];
    need=(rule->shape==LA_SCALAR_GPR2 ||
          rule->shape==LA_SCALAR_UPPER20)?2U:3U;
    if(rule->shape==LA_SCALAR_ATOMIC && count==4U) {
        uint64_t zero;
        /* GCC's four-argument pseudo is the same instruction with an
         * explicit reserved u0:0 field, not a second atomic family. */
        if(!minias_eval_u64_expression(args[3],&zero) || zero!=0U)
            return false;
        need=4U;
    }
    if(count!=need || !la_reg(args[0],&rd) ||
       (rule->shape!=LA_SCALAR_UPPER20 && !la_reg(args[1],&rj)))
        return false;
    if(rule->shape==LA_SCALAR_GPR2) {
        la_capture_gpr_operand(m,0U,rd);
        la_capture_gpr_operand(m,1U,rj);
    } else if(rule->shape==LA_SCALAR_GPR3 ||
              rule->shape==LA_SCALAR_ATOMIC) {
        if(!la_reg(args[2],&rk))return false;
        /* Atomics swap assembly operand 1/2 field placement relative to
         * ordinary three-register instructions. */
        la_capture_rrr(m,rd,rj,rk);
        if(need==4U)la_capture_imm_operand(m,3U,0);
    } else {
        if(rule->shape==LA_SCALAR_UPPER20) {
            if(!minias_eval_i64_expression(args[1],&value) ||
               value<-524288 || value>524287)return false;
            la_capture_gpr_operand(m,0U,rd);
            la_capture_imm_operand(m,1U,value);
        } else {
            if(!minias_eval_i64_expression(args[2],&value))return false;
            switch(rule->shape) {
                case LA_SCALAR_IMM12:
                    if(value<-2048 || value>2047)return false;
                    break;
                case LA_SCALAR_UIMM12:
                    if(value<0 || value>4095)return false;
                    break;
                case LA_SCALAR_SHIFT5:
                    if(value<0 || value>31)return false;
                    break;
                case LA_SCALAR_SHIFT6:
                    if(value<0 || value>63)return false;
                    break;
                case LA_SCALAR_PTR14:
                    if(value<-32768 || value>32764 || value%4!=0)
                        return false;
                    break;
                default:return false;
            }
            la_capture_rri(m,rd,rj,value);
        }
    }
    m->count=need;
    m->target_form=(uint32_t)LA_FORM_SCALAR+index;
    return true;
}

/* Table-backed encoders consume semantic MC operands, not the parser's
 * machine-word cache.  Both the descriptor ID and register-bank ID remain
 * private to LoongArch; Common is agnostic about the ISA's form layout. */
static bool la_encoded_reg(const MiniAsMCOperand *operand,
                           LaRegisterBank bank,uint32_t *value) {
    if(operand->kind!=MINIAS_MC_REGISTER ||
       operand->target_reg_bank!=(uint16_t)bank || operand->reg>31U)
        return false;
    *value=operand->reg;
    return true;
}

static bool la_encoded_imm(const MiniAsMCOperand *operand,
                           unsigned bits,uint32_t *value) {
    if(operand->kind!=MINIAS_MC_IMMEDIATE || bits==0U || bits>20U)
        return false;
    *value=(uint32_t)operand->immediate & ((UINT32_C(1)<<bits)-1U);
    return true;
}

static bool la_materialize_table_word(const MiniAsMCInst *m,
                                      uint32_t *word,bool *recognized) {
    uint32_t form=m->target_form;
    unsigned i;
    *recognized=true;
    if(form>=LA_FORM_VECTOR && form<LA_FORM_SPECIAL_REGISTER) {
        size_t idx=(size_t)(form-LA_FORM_VECTOR);
        const LaVectorPattern *pattern;
        const LaVectorLayout *layout;
        if(idx>=sizeof(la_vector_patterns)/sizeof(la_vector_patterns[0]))
            return false;
        pattern=&la_vector_patterns[idx];
        layout=&la_vector_layouts[pattern->shape];
        if(m->count!=layout->count)return false;
        *word=pattern->opcode;
        for(i=0U;i<layout->count;++i) {
            uint32_t value;
            LaOperandClass cls=layout->classes[i];
            if(cls==LA_ARG_IMM) {
                if(!la_encoded_imm(&m->operands[i],
                                   pattern->immediate_bits,&value))return false;
            } else if(!la_encoded_reg(&m->operands[i],
                         cls==LA_ARG_GPR?LA_REG_BANK_GPR:
                         cls==LA_ARG_VR?LA_REG_BANK_VR128:LA_REG_BANK_XR256,
                         &value))return false;
            *word|=value<<layout->shifts[i];
        }
        return true;
    }
    if(form>=LA_FORM_SPECIAL_REGISTER && form<LA_FORM_BRANCH) {
        size_t idx=(size_t)(form-LA_FORM_SPECIAL_REGISTER);
        const LaSpecialRegisterPattern *rule;
        if(idx>=sizeof(la_special_register_patterns)/
                sizeof(la_special_register_patterns[0]))return false;
        rule=&la_special_register_patterns[idx];
        if(m->count!=rule->count)return false;
        *word=rule->opcode;
        for(i=0U;i<rule->count;++i) {
            LaRegisterOperandBank bank=rule->banks[i];
            uint32_t value;
            if(bank>=LA_BANK_SIGNED12) {
                unsigned bits=bank==LA_BANK_SIGNED12?12U:
                    bank==LA_BANK_UNSIGNED5?5U:
                    bank==LA_BANK_UNSIGNED8?8U:
                    bank==LA_BANK_UNSIGNED14?14U:15U;
                if(!la_encoded_imm(&m->operands[i],bits,&value))return false;
            } else if(!la_encoded_reg(&m->operands[i],
                         bank==LA_BANK_GPR?LA_REG_BANK_GPR:
                         bank==LA_BANK_FPR?LA_REG_BANK_FPR:
                         bank==LA_BANK_FCSR?LA_REG_BANK_FCSR:
                         bank==LA_BANK_SCR?LA_REG_BANK_SCR:LA_REG_BANK_FCC,
                         &value) || value<rule->min_register[i])return false;
            *word|=value<<rule->shifts[i];
        }
        return true;
    }
    if(form>=LA_FORM_SCALAR && form<LA_FORM_VECTOR) {
        size_t idx=(size_t)(form-LA_FORM_SCALAR);
        const LaScalarPattern *rule;
        uint32_t rd,rj=0U,rk=0U,imm=0U;
        unsigned bits=0U;
        if(idx>=sizeof(la_scalar_patterns)/sizeof(la_scalar_patterns[0]))
            return false;
        rule=&la_scalar_patterns[idx];
        if(!la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd))
            return false;
        if(rule->shape==LA_SCALAR_UPPER20) {
            if(m->count!=2U ||
               !la_encoded_imm(&m->operands[1],20U,&imm))return false;
            *word=rule->opcode|(imm<<5U)|rd;
            return true;
        }
        if(!la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj))
            return false;
        if(rule->shape==LA_SCALAR_GPR2) {
            if(m->count!=2U)return false;
            *word=rule->opcode|(rj<<5U)|rd;
            return true;
        }
        if(rule->shape==LA_SCALAR_GPR3 ||
           rule->shape==LA_SCALAR_ATOMIC) {
            if(!la_encoded_reg(&m->operands[2],LA_REG_BANK_GPR,&rk) ||
               (rule->shape==LA_SCALAR_GPR3 && m->count!=3U) ||
               (rule->shape==LA_SCALAR_ATOMIC &&
                m->count!=3U && m->count!=4U))return false;
            if(m->count==4U && (m->operands[3].kind!=MINIAS_MC_IMMEDIATE ||
                                 m->operands[3].immediate!=0U))return false;
            *word=rule->opcode|rd|
                 (rule->shape==LA_SCALAR_ATOMIC ?
                  (rj<<10U)|(rk<<5U):(rk<<10U)|(rj<<5U));
            return true;
        }
        if(m->count!=3U)return false;
        bits=rule->shape==LA_SCALAR_SHIFT5?5U:
             rule->shape==LA_SCALAR_SHIFT6?6U:
             rule->shape==LA_SCALAR_PTR14?14U:12U;
        if(rule->shape==LA_SCALAR_PTR14) {
            int64_t displacement=(int64_t)m->operands[2].immediate;
            if(m->operands[2].kind!=MINIAS_MC_IMMEDIATE ||
               displacement%4!=0)return false;
            imm=(uint32_t)(displacement/4)&UINT32_C(0x3fff);
        } else if(!la_encoded_imm(&m->operands[2],bits,&imm))return false;
        *word=rule->opcode|(imm<<10U)|(rj<<5U)|rd;
        return true;
    }
    *recognized=false;
    return true;
}

/* The remaining compact instruction families retain their semantic operands
 * and a private Form ID. The parser records only an opcode template; this
 * encoder is the sole owner of field placement for every one-word family. */
static bool la_materialize_manual_word(const MiniAsMCInst *m,
                                       uint32_t *word,bool *recognized) {
    uint32_t form=m->target_form,rd=0U,rj=0U,rk=0U,immediate=0U;
    int64_t signed_value;
    *recognized=true;
    if(form>=LA_FORM_PRIVILEGED && form<LA_FORM_SCALAR) {
        if(m->count!=0U ||
           !((form>=LA_FORM_PRIVILEGED &&
              form<LA_FORM_PRIVILEGED+7U) ||
             form==LA_FORM_PRIVILEGED+16U ||
             form==LA_FORM_PRIVILEGED+17U))return false;
        *word=m->encoding;
        return true;
    }
    if(form>=LA_FORM_BRANCH && form<LA_FORM_LI_D) {
        size_t index=(size_t)(form-LA_FORM_BRANCH);
        const LaBranchPattern *branch;
        if(index>=sizeof(la_branch_patterns)/sizeof(la_branch_patterns[0]))
            return false;
        branch=&la_branch_patterns[index];
        if(m->count!=branch->operands || m->opcode!=(unsigned)branch->kind ||
           m->operands[m->count-1U].kind!=MINIAS_MC_BOUND_SYMBOL)
            return false;
        *word=branch->opcode;
        if(m->count>=2U) {
            if(!la_encoded_reg(&m->operands[0],branch->first_bank,&rj))
                return false;
            *word|=rj<<5U;
        }
        if(m->count==3U) {
            if(!la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rd))
                return false;
            *word|=rd;
        }
        return true;
    }
    if(form>=LA_FORM_BITFIELD &&
       form<LA_FORM_BITFIELD+sizeof(la_bitfield_patterns)/
            sizeof(la_bitfield_patterns[0])) {
        size_t index=(size_t)(form-LA_FORM_BITFIELD);
        const LaManualPattern *pattern=&la_bitfield_patterns[index];
        if(m->count!=4U ||
           !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd) ||
           !la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj) ||
           !la_encoded_imm(&m->operands[2],pattern->immediate_bits,&immediate) ||
           m->operands[3].kind!=MINIAS_MC_IMMEDIATE ||
           m->operands[3].immediate>immediate)return false;
        *word=pattern->opcode|(immediate<<16U)|
            ((uint32_t)m->operands[3].immediate<<10U)|(rj<<5U)|rd;
        return true;
    }
    if(form==LA_FORM_BYTEPICK || form==LA_FORM_BYTEPICK+1U ||
       (form>=LA_FORM_ALSL && form<LA_FORM_ALSL+3U)) {
        uint64_t value;
        if(m->count!=4U ||
           !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd) ||
           !la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj) ||
           !la_encoded_reg(&m->operands[2],LA_REG_BANK_GPR,&rk) ||
           m->operands[3].kind!=MINIAS_MC_IMMEDIATE)return false;
        value=m->operands[3].immediate;
        if(form==LA_FORM_BYTEPICK || form==LA_FORM_BYTEPICK+1U) {
            unsigned wide=form==LA_FORM_BYTEPICK+1U;
            if(value>=(wide?8U:4U))return false;
            *word=(wide?UINT32_C(0x000c0000):UINT32_C(0x00080000))|
                ((uint32_t)value<<15U)|(rk<<10U)|(rj<<5U)|rd;
        } else {
            size_t index=(size_t)(form-LA_FORM_ALSL);
            if(value<1U || value>4U)return false;
            *word=la_alsl_patterns[index].opcode|
                (((uint32_t)value-1U)<<15U)|(rk<<10U)|(rj<<5U)|rd;
        }
        return true;
    }
    if(form==LA_FORM_JIRL || form==LA_FORM_JIRL+1U) {
        if(form==LA_FORM_JIRL) {
            if(m->count!=1U ||
               !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rj))
                return false;
            *word=UINT32_C(0x4c000000)|(rj<<5U);
        } else {
            if(m->count!=3U ||
               !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd) ||
               !la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj) ||
               m->operands[2].kind!=MINIAS_MC_IMMEDIATE)return false;
            signed_value=(int64_t)m->operands[2].immediate;
            if(signed_value<-131072 || signed_value>131068 ||
               signed_value%4!=0)return false;
            *word=UINT32_C(0x4c000000)|
                (((uint32_t)(signed_value/4)&UINT32_C(0xffff))<<10U)|
                (rj<<5U)|rd;
        }
        return true;
    }
    if(form==LA_FORM_MOVE || form==LA_FORM_ADDU16I) {
        if(m->count!=(form==LA_FORM_MOVE?2U:3U) ||
           !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd) ||
           !la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj))return false;
        if(form==LA_FORM_MOVE) {
            *word=UINT32_C(0x00150000)|(rj<<5U)|rd;
        } else {
            if(!la_encoded_imm(&m->operands[2],16U,&immediate))return false;
            *word=UINT32_C(0x10000000)|(immediate<<10U)|(rj<<5U)|rd;
        }
        return true;
    }
    if(form==LA_FORM_PREFETCH || form==LA_FORM_PREFETCH+1U) {
        if(m->count!=3U ||
           !la_encoded_imm(&m->operands[0],5U,&immediate) ||
           m->operands[0].immediate>31U ||
           !la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj))return false;
        if(form==LA_FORM_PREFETCH) {
            uint32_t displacement;
            if(!la_encoded_imm(&m->operands[2],12U,&displacement))
                return false;
            *word=UINT32_C(0x2ac00000)|(displacement<<10U)|
                (rj<<5U)|immediate;
        } else {
            if(!la_encoded_reg(&m->operands[2],LA_REG_BANK_GPR,&rk))
                return false;
            *word=UINT32_C(0x382c0000)|(rk<<10U)|
                (rj<<5U)|immediate;
        }
        return true;
    }
    if(form==LA_FORM_PCALA_HI20 || form==LA_FORM_PCALA_LO12) {
        unsigned symbol=form==LA_FORM_PCALA_HI20?1U:2U;
        if(m->count!=symbol+1U ||
           m->opcode!=(form==LA_FORM_PCALA_HI20 ?
                      LA_MC_PCALA_HI20:LA_MC_PCALA_LO12) ||
           !la_encoded_reg(&m->operands[0],LA_REG_BANK_GPR,&rd) ||
           m->operands[symbol].kind!=MINIAS_MC_BOUND_SYMBOL)return false;
        *word=m->encoding|rd;
        if(form==LA_FORM_PCALA_LO12) {
            if(!la_encoded_reg(&m->operands[1],LA_REG_BANK_GPR,&rj))
                return false;
            *word|=rj<<5U;
        }
        return true;
    }
    *recognized=false;
    return true;
}

/* The index contains identities, never ISA operand/encoding rules. LSX,
 * LASX, scalar and special-register descriptors remain independently owned;
 * adding one mnemonic means adding one descriptor, not editing a second
 * per-family dispatch chain. Branch aliases point to canonical B16 forms. */
typedef struct {
    const char *name;
    LaMnemonicFamily family;
    unsigned index;
    LaBranchAlias branch_alias;
} LaBuiltinMnemonic;
static const struct {const char *name;uint32_t opcode;} la_privileged[] = {
    {"tlbclr",UINT32_C(0x06482000)},
    {"tlbflush",UINT32_C(0x06482400)},
    {"tlbsrch",UINT32_C(0x06482800)},
    {"tlbrd",UINT32_C(0x06482c00)},
    {"tlbwr",UINT32_C(0x06483000)},
    {"tlbfill",UINT32_C(0x06483400)},
    {"ertn",UINT32_C(0x06483800)},
    {"nop",UINT32_C(0x03400000)},
    {"ret",UINT32_C(0x4c000020)}
};
static const LaBuiltinMnemonic la_builtin_mnemonics[]={
    {"bltz",LA_MN_BRANCH,6U,LA_BRANCH_ZERO_RIGHT},
    {"bgtz",LA_MN_BRANCH,6U,LA_BRANCH_ZERO_LEFT},
    {"bgez",LA_MN_BRANCH,7U,LA_BRANCH_ZERO_RIGHT},
    {"blez",LA_MN_BRANCH,7U,LA_BRANCH_ZERO_LEFT},
    {"bgt",LA_MN_BRANCH,6U,LA_BRANCH_SWAP},
    {"ble",LA_MN_BRANCH,7U,LA_BRANCH_SWAP},
    {"bgtu",LA_MN_BRANCH,8U,LA_BRANCH_SWAP},
    {"bleu",LA_MN_BRANCH,9U,LA_BRANCH_SWAP},
    {"li.w",LA_MN_LI,0U,LA_BRANCH_CANONICAL},
    {"li.d",LA_MN_LI,1U,LA_BRANCH_CANONICAL},
    {"la.pcrel",LA_MN_PCREL,0U,LA_BRANCH_CANONICAL},
    /* GNU's ordinary LP64 model lowers two-operand la.local through the
     * same pcalau12i/addi.d relocation pair as la.pcrel. */
    {"la.local",LA_MN_PCREL,0U,LA_BRANCH_CANONICAL},
    {"la.global",LA_MN_GOT,0U,LA_BRANCH_CANONICAL},
    {"bytepick.w",LA_MN_BYTEPICK,0U,LA_BRANCH_CANONICAL},
    {"bytepick.d",LA_MN_BYTEPICK,1U,LA_BRANCH_CANONICAL},
    {"jr",LA_MN_JIRL,0U,LA_BRANCH_CANONICAL},
    {"jirl",LA_MN_JIRL,1U,LA_BRANCH_CANONICAL},
    {"move",LA_MN_MOVE,0U,LA_BRANCH_CANONICAL},
    {"preld",LA_MN_PREFETCH,0U,LA_BRANCH_CANONICAL},
    {"preldx",LA_MN_PREFETCH,1U,LA_BRANCH_CANONICAL},
    {"addu16i.d",LA_MN_ADDU16I,0U,LA_BRANCH_CANONICAL}
};

static int la_compare_mnemonic(const void *lhs,const void *rhs) {
    const LaMnemonicEntry *a=(const LaMnemonicEntry *)lhs;
    const LaMnemonicEntry *b=(const LaMnemonicEntry *)rhs;
    return strcmp(a->name,b->name);
}

static bool la_build_mnemonic_index(MiniAs *as,LaTargetState *state) {
    size_t i,j,n=0U;
    const size_t capacity=LA_COUNT(la_branch_patterns)+
        LA_COUNT(la_vector_patterns)+LA_COUNT(la_special_register_patterns)+
        LA_COUNT(la_scalar_patterns)+LA_COUNT(la_bitfield_patterns)+
        LA_COUNT(la_alsl_patterns)+LA_COUNT(la_builtin_mnemonics)+
        LA_COUNT(la_privileged);
    LaMnemonicEntry *entries=calloc(capacity,sizeof(*entries));
    if(entries==NULL) {
        minias_set_error(as,"out-of-memory:loongarch-mnemonic-index");
        return false;
    }
    for(i=0U;i<LA_COUNT(la_branch_patterns);++i)
        entries[n++]=(LaMnemonicEntry){la_branch_patterns[i].name,
                                      LA_MN_BRANCH,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    for(i=0U;i<LA_COUNT(la_vector_patterns);++i) {
        const LaVectorPattern *pattern=&la_vector_patterns[i];
        const LaVectorLayout *layout=&la_vector_layouts[pattern->shape];
        uint32_t features=0U;
        for(j=0U;j<layout->count;++j) {
            if(layout->classes[j]==LA_ARG_XR)features|=LA_FEATURE_LASX;
            if(layout->classes[j]==LA_ARG_VR)features|=LA_FEATURE_LSX;
        }
        entries[n++]=(LaMnemonicEntry){pattern->name,LA_MN_VECTOR,
                                      (unsigned)i,features,
                                      LA_BRANCH_CANONICAL};
    }
    for(i=0U;i<LA_COUNT(la_special_register_patterns);++i)
        entries[n++]=(LaMnemonicEntry){la_special_register_patterns[i].name,
                                      LA_MN_SPECIAL,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    for(i=0U;i<LA_COUNT(la_scalar_patterns);++i)
        entries[n++]=(LaMnemonicEntry){la_scalar_patterns[i].name,
                                      LA_MN_SCALAR,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    for(i=0U;i<LA_COUNT(la_bitfield_patterns);++i)
        entries[n++]=(LaMnemonicEntry){la_bitfield_patterns[i].name,
                                      LA_MN_BITFIELD,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    for(i=0U;i<LA_COUNT(la_alsl_patterns);++i)
        entries[n++]=(LaMnemonicEntry){la_alsl_patterns[i].name,
                                      LA_MN_ALSL,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    for(i=0U;i<LA_COUNT(la_builtin_mnemonics);++i)
        entries[n++]=(LaMnemonicEntry){la_builtin_mnemonics[i].name,
                                      la_builtin_mnemonics[i].family,
                                      la_builtin_mnemonics[i].index,0U,
                                      la_builtin_mnemonics[i].branch_alias};
    /* Privileged names and their machine words share one ISA descriptor.
     * The index must never contain a second handwritten name inventory. */
    for(i=0U;i<LA_COUNT(la_privileged);++i)
        entries[n++]=(LaMnemonicEntry){la_privileged[i].name,
                                      LA_MN_PRIVILEGED,(unsigned)i,0U,
                                      LA_BRANCH_CANONICAL};
    qsort(entries,n,sizeof(*entries),la_compare_mnemonic);
    for(i=1U;i<n;++i)
        if(strcmp(entries[i-1U].name,entries[i].name)==0) {
            minias_set_error(as,"duplicate-loongarch-mnemonic:%s",
                             entries[i].name);
            free(entries);
            return false;
        }
    state->mnemonics=entries;
    state->mnemonic_count=n;
    return true;
}

static const LaMnemonicEntry *la_find_mnemonic(const MiniAs *as,
                                               const char *name) {
    const LaTargetState *state=(const LaTargetState *)as->target_state;
    size_t lo=0U,hi;
    if(state==NULL || state->mnemonics==NULL)return NULL;
    hi=state->mnemonic_count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2U;
        int cmp=strcmp(state->mnemonics[mid].name,name);
        if(cmp==0)return &state->mnemonics[mid];
        if(cmp<0)lo=mid+1U;
        else hi=mid;
    }
    return NULL;
}

/* LI.W and LI.D share a typed mnemonic family, but LI.D is ALWAYS
 * lowered through GNU GAS's exact 12/20/20/12 sign-extension reduction.
 * Direct LI.W single-word forms retain their established GNU encodings. */
static bool la_parse_li(MiniAs *as,MiniAsMCInst *m,char *args[4],
                        unsigned count,bool li_d) {
    unsigned rd;
    uint64_t bits;
    if(count!=2U || !la_reg(args[0],&rd))return false;
    if(!minias_eval_u64_expression(args[1],&bits))return false;
    if(li_d) {
        if(!la_plan_li_d(as,m,bits,rd))return false;
        m->opcode=LA_MC_LI64_SEQUENCE;
    } else {
        uint32_t upper=(uint32_t)(bits>>32U);
        /* GNU GAS allows either zero or all-ones high half, then narrows
         * to the low word.  Values requiring other high bits overflow. */
        if(upper!=0U && upper!=UINT32_MAX)return false;
        if(!la_plan_li_w(as,m,(uint32_t)bits,rd))return false;
        m->opcode=LA_MC_LI32_SEQUENCE;
    }
    m->count=2U;
    la_capture_gpr_operand(m,0U,rd);
    m->operands[1].kind=MINIAS_MC_IMMEDIATE;
    m->operands[1].immediate=bits;
    return true;
}

static bool la_parse(MiniAs *as,MiniAsStmt *s) {
    MiniAsMCInst *m=minias_mcinst_alloc(4U);
    LaOperandTokens tokens={0};
    char **t=tokens.slots;
    const LaMnemonicEntry *mnemonic;
    unsigned n,i,rd,rj,rk;
    if(!m){minias_set_error(as,"out-of-memory:loongarch64-mcinst");return false;}
    mnemonic=la_find_mnemonic(as,s->op);
    if(mnemonic==NULL ||
       !la_has_features(as,mnemonic->required_features))goto fail;
    if(mnemonic->family==LA_MN_PRIVILEGED) {
        i=mnemonic->index;
        if(*s->args || i>=LA_COUNT(la_privileged))goto fail;
        m->encoding=la_privileged[i].opcode;
        m->target_form=(uint32_t)LA_FORM_PRIVILEGED+
            (uint32_t)(i<7U?i:i+9U);
        goto done;
    }
    if(!la_split(s->args,&tokens))goto fail;
    n=tokens.count;
    if(mnemonic->family==LA_MN_BRANCH) {
        if(la_branch_instruction(as,m,t,n,mnemonic->index,
                                 mnemonic->branch_alias))goto done;
        goto fail;
    }
    if(mnemonic->family==LA_MN_VECTOR) {
        if(la_parse_vector_pattern(m,t,n,mnemonic->index))goto done;
        goto fail;
    }
    if((mnemonic->family==LA_MN_PCREL ||
        mnemonic->family==LA_MN_GOT) &&
       n==2U && la_reg(t[0],&rd)) {
        MiniAsSymbolExpr expr;
        if(!minias_parse_symbol_addend(t[1],&expr) ||
           !la_symbol_operand(as,m,1U,&expr))goto fail;
        m->opcode=mnemonic->family==LA_MN_GOT ?
            LA_MC_GOT_PSEUDO : LA_MC_PCREL_PSEUDO;
        m->count=2U;
        la_capture_gpr_operand(m,0U,rd);
        if(!(mnemonic->family==LA_MN_GOT ?
             la_plan_global_got(as,m,rd) : la_plan_pcrel(as,m,rd)))goto fail;
        goto done;
    }
    if(mnemonic->family==LA_MN_PCREL ||
       mnemonic->family==LA_MN_GOT)goto fail;
    if(mnemonic->family==LA_MN_SPECIAL) {
        if(la_parse_special_register_pattern(m,t,n,mnemonic->index))goto done;
        goto fail;
    }
    if(mnemonic->family==LA_MN_LI) {
        if(la_parse_li(as,m,t,n,mnemonic->index==1U))goto done;
        goto fail;
    }
    if(mnemonic->family==LA_MN_BITFIELD) {
        const LaManualPattern *pattern=&la_bitfield_patterns[mnemonic->index];
        uint64_t msb,lsb;
        if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
           minias_eval_u64_expression(t[2],&msb) &&
           minias_eval_u64_expression(t[3],&lsb) &&
           msb<(UINT64_C(1)<<pattern->immediate_bits) && lsb<=msb) {
            m->count=4U;
            m->target_form=LA_FORM_BITFIELD+mnemonic->index;
            la_capture_gpr_operand(m,0U,rd);
            la_capture_gpr_operand(m,1U,rj);
            la_capture_imm_operand(m,2U,(int64_t)msb);
            la_capture_imm_operand(m,3U,(int64_t)lsb);
            goto done;
        }
        goto fail;
    }
    if(mnemonic->family==LA_MN_BYTEPICK) {
        uint64_t offset;
        bool wide=mnemonic->index==1U;
        if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
           la_reg(t[2],&rk) && minias_eval_u64_expression(t[3],&offset) &&
           offset<(wide?8U:4U)) {
            m->count=4U;
            m->target_form=LA_FORM_BYTEPICK+(wide?1U:0U);
            la_capture_rrr(m,rd,rj,rk);
            la_capture_imm_operand(m,3U,(int64_t)offset);
            goto done;
        }
        goto fail;
    }
    /* GNU LA opcode tables: sa2 lives in bits 16:15 and the source
     * syntax specifies sa2+1 (one through four), not the encoded 0..3.
     * Index/shift-add is a single target-local MCInst, not Common logic. */
    if(mnemonic->family==LA_MN_ALSL) {
        uint64_t shift;
        if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
           la_reg(t[2],&rk) && minias_eval_u64_expression(t[3],&shift) &&
           shift>=1U && shift<=4U) {
            m->count=4U;
            m->target_form=LA_FORM_ALSL+mnemonic->index;
            la_capture_rrr(m,rd,rj,rk);
            la_capture_imm_operand(m,3U,(int64_t)shift);
            goto done;
        }
        goto fail;
    }
    /* LoongArch jr register == jirl $zero,$reg,0.  No relocation. */
    if(mnemonic->family==LA_MN_JIRL && mnemonic->index==0U &&
       n==1U && la_reg(t[0],&rj)) {
        m->count=1U;
        m->target_form=LA_FORM_JIRL;
        la_capture_gpr_operand(m,0U,rj);
        goto done;
    }
    /* JIRL is a signed 16-bit displacement in 4-byte units. The JR alias
     * above is a canonical zero-link JIRL, not a second instruction family. */
    if(mnemonic->family==LA_MN_JIRL && mnemonic->index==1U && n==3U &&
       la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        int64_t delta;
        if(minias_eval_i64_expression(t[2],&delta) &&
           delta>=-131072 && delta<=131068 && delta%4==0) {
            m->count=3U;
            m->target_form=LA_FORM_JIRL+1U;
            la_capture_rri(m,rd,rj,delta);
            goto done;
        }
    }
    if(mnemonic->family==LA_MN_JIRL)goto fail;
    if(mnemonic->family==LA_MN_MOVE && n==2U &&
        la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        m->count=2U;
        m->target_form=LA_FORM_MOVE;
        la_capture_gpr_operand(m,0U,rd);
        la_capture_gpr_operand(m,1U,rj);
        goto done;
    }
    if(mnemonic->family==LA_MN_MOVE)goto fail;
    /* LoongArch PREFETCH has a u5 cache hint, a base register and either
     * a signed 12-bit displacement or an index register. Both variants
     * use the exact GNU opcodes and shared typed MCInst/ELF pipeline. */
    if(mnemonic->family==LA_MN_PREFETCH && n==3U) {
        uint64_t hint;
        int64_t displacement;
        if(!minias_eval_u64_expression(t[0],&hint) || hint>31U ||
           !la_reg(t[1],&rj))goto fail;
        if(mnemonic->index==0U) {
            m->target_form=LA_FORM_PREFETCH;
            if(!minias_eval_i64_expression(t[2],&displacement) ||
               displacement<-2048 || displacement>2047)goto fail;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=(uint64_t)displacement;
        }else {
            m->target_form=LA_FORM_PREFETCH+1U;
            if(!la_reg(t[2],&rk))goto fail;
            la_capture_gpr_operand(m,2U,rk);
        }
        m->count=3U;
        m->operands[0].kind=MINIAS_MC_IMMEDIATE;
        m->operands[0].immediate=hint;
        la_capture_gpr_operand(m,1U,rj);
        goto done;
    }
    if(mnemonic->family==LA_MN_PREFETCH)goto fail;
    /* Signed si16 GOT/LDptr address arithmetic, no guessed relocations. */
    if(mnemonic->family==LA_MN_ADDU16I && n==3U) {
        int64_t imm;
        if(!la_reg(t[0],&rd) || !la_reg(t[1],&rj) ||
           !minias_eval_i64_expression(t[2],&imm) ||
           imm<-32768 || imm>32767)goto fail;
        m->count=3U;
        m->target_form=LA_FORM_ADDU16I;
        la_capture_rri(m,rd,rj,imm);
        goto done;
    }
    if(mnemonic->family==LA_MN_ADDU16I)goto fail;
    if(mnemonic->family==LA_MN_SCALAR) {
        const LaScalarPattern *rule=&la_scalar_patterns[mnemonic->index];
        MiniAsSymbolExpr expr;
        /* The SAME scalar mnemonic may have a PCALA relocation operand
         * instead of a numeric immediate. The indexed descriptor chooses
         * the field layout; the modifier chooses the relocation kind. */
        if(n==2U && rule->shape==LA_SCALAR_UPPER20 &&
           rule->opcode==UINT32_C(0x1a000000) &&
           la_reg(t[0],&rd) &&
           la_reloc_expr(t[1],"%pc_hi20",&expr)) {
            if(!la_symbol_operand(as,m,1U,&expr))goto fail;
            m->opcode=LA_MC_PCALA_HI20;
            m->target_form=LA_FORM_PCALA_HI20;
            m->count=2U;
            m->encoding=rule->opcode;
            la_capture_gpr_operand(m,0U,rd);
            goto done;
        }
        if(n==3U && rule->shape==LA_SCALAR_IMM12 &&
           la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
           la_reloc_expr(t[2],"%pc_lo12",&expr)) {
            if(!la_symbol_operand(as,m,2U,&expr))goto fail;
            m->opcode=LA_MC_PCALA_LO12;
            m->target_form=LA_FORM_PCALA_LO12;
            m->count=3U;
            m->encoding=rule->opcode;
            la_capture_gpr_operand(m,0U,rd);
            la_capture_gpr_operand(m,1U,rj);
            goto done;
        }
        if(la_parse_scalar_pattern(m,t,n,mnemonic->index))goto done;
        goto fail;
    }
fail:
    minias_set_error(as,"unsupported-loongarch64-operands:%s:%s:line=%zu",
                     s->op,s->args,s->line);
    free(tokens.overflow);
    minias_mcinst_destroy(m);return false;
done:
    if(m->target_form==LA_FORM_NONE) {
        minias_set_error(as,"missing-loongarch-target-form:line=%zu",s->line);
        free(tokens.overflow);
        minias_mcinst_destroy(m);
        return false;
    }
    for(i=0U;i<m->count;++i)
        if(m->operands[i].kind==MINIAS_MC_REGISTER &&
           m->operands[i].target_reg_bank==LA_REG_BANK_NONE) {
            minias_set_error(as,"missing-loongarch-register-bank:line=%zu",
                             s->line);
            free(tokens.overflow);
            minias_mcinst_destroy(m);
            return false;
        }
    free(tokens.overflow);
    s->mcinst=m;return true;
}

/* Branch decisions are re-evaluated from the retained SymbolRef after each
 * Common layout pass.  Weak definitions are link-time replaceable even when
 * they happen to be in this Section: GAS deliberately does not resolve them
 * as fixed local branch displacements. */
typedef struct {
    bool local;
    bool short_legal;
    int64_t displacement;
} LaBranchPlan;

static bool la_resolve_branch_plan(MiniAs *as,const MiniAsStmt *s,
                                   LaBranchPlan *plan) {
    const MiniAsMCInst *m=s->mcinst;
    const MiniAsSymbolRef *ref;
    const MiniAsSymbol *target;
    int64_t diff,limit;
    unsigned slot;
    memset(plan,0,sizeof(*plan));
    if(m==NULL || (m->opcode!=LA_MC_BRANCH16 &&
                   m->opcode!=LA_MC_BRANCH21 &&
                   m->opcode!=LA_MC_BRANCH26) || m->count==0U) {
        minias_set_error(as,"invalid-loongarch-branch-plan");
        return false;
    }
    slot=m->count-1U;
    if(m->operands[slot].kind!=MINIAS_MC_BOUND_SYMBOL ||
       (ref=m->operands[slot].bound_symbol)==NULL) {
        minias_set_error(as,"invalid-loongarch-bound-branch");
        return false;
    }
    target=minias_symbol_ref_lookup(as,*ref);
    if(target==NULL || !target->defined || target->section!=s->section ||
       target->bind==MINIAS_STB_WEAK)return true;
    if(target->value>INT64_MAX || s->offset>INT64_MAX) {
        minias_set_error(as,"loong-branch-offset-overflow");
        return false;
    }
    diff=(int64_t)target->value-(int64_t)s->offset;
    if((ref->addend>0 && diff>INT64_MAX-ref->addend) ||
       (ref->addend<0 && diff<INT64_MIN-ref->addend)) {
        minias_set_error(as,"loong-branch-addend-overflow");
        return false;
    }
    diff+=ref->addend;
    plan->local=true;
    plan->displacement=diff;
    limit=m->opcode==LA_MC_BRANCH16 ? (INT64_C(1)<<17) :
          m->opcode==LA_MC_BRANCH21 ? (INT64_C(1)<<22) :
                                        (INT64_C(1)<<27);
    plan->short_legal=(diff&3)==0 && diff>=-limit && diff<limit;
    return true;
}

/* GNU GAS must keep same-section branch displacements as linker fixups when
 * PCALA/GOT macros in that Section may shrink during linker relaxation. A
 * branch at the beginning of the Section can depend on a later such macro,
 * so the condition is computed from the complete retained statement graph,
 * not the current source prefix. Cache once for O(n) total compilation. */
static bool la_section_is_linker_relaxable(MiniAs *as,int section,
                                           bool *relaxable) {
    LaTargetState *state=(LaTargetState *)as->target_state;
    size_t i;
    if(state==NULL || section<0 ||
       (size_t)section>=as->section_count)return false;
    if(state->relaxable_sections==NULL ||
       state->relaxable_section_count!=as->section_count) {
        bool *sections=calloc(as->section_count,sizeof(*sections));
        if(sections==NULL) {
            minias_set_error(as,"out-of-memory:loongarch-relaxable-sections");
            return false;
        }
        for(i=0U;i<as->stmt_count;++i) {
            const MiniAsStmt *stmt=&as->stmts[i];
            if(stmt->section>=0 &&
               (size_t)stmt->section<as->section_count &&
               stmt->mcinst!=NULL &&
               (stmt->mcinst->opcode==LA_MC_PCREL_PSEUDO ||
                stmt->mcinst->opcode==LA_MC_GOT_PSEUDO))
                sections[(size_t)stmt->section]=true;
        }
        free(state->relaxable_sections);
        state->relaxable_sections=sections;
        state->relaxable_section_count=as->section_count;
    }
    *relaxable=state->relaxable_sections[(size_t)section];
    return true;
}

static bool la_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    if(!s->mcinst){minias_set_error(as,"missing-la-mcinst");return false;}
    if(s->mcinst->target_payload!=NULL) {
        const LaLoweringPlan *plan=
            (const LaLoweringPlan *)s->mcinst->target_payload;
        if(!la_valid_lowering_plan(s->mcinst)) {
            minias_set_error(as,"invalid-loongarch-lowering-plan");
            return false;
        }
        *size=plan->count*4U;
        return true;
    }
    if(s->mcinst->opcode==LA_MC_PCREL_PSEUDO ||
       s->mcinst->opcode==LA_MC_GOT_PSEUDO ||
       s->mcinst->opcode==LA_MC_LI32_SEQUENCE ||
       s->mcinst->opcode==LA_MC_LI64_SEQUENCE) {
        minias_set_error(as,"missing-loongarch-lowering-plan");
        return false;
    }
    *size=4U;
    return true;
}
static bool la_encode(MiniAs *as,const MiniAsStmt *s) {
    unsigned char bytes[4];
    const MiniAsMCInst *m=s->mcinst;
    uint32_t code;
    unsigned i, slot=0U;
    MiniAsFixupKind kind=MINIAS_FIXUP_NONE;
    const MiniAsSymbolRef *expr=NULL;
    bool table_form=false,manual_form=false;
    if(!m){minias_set_error(as,"missing-la-mcinst");return false;}
    code=m->encoding;
    if(m->target_payload!=NULL) {
        const LaLoweringPlan *plan=
            (const LaLoweringPlan *)m->target_payload;
        unsigned j,k;
        unsigned char body[16];
        if(!la_valid_lowering_plan(m) || s->size!=4U*plan->count) {
            minias_set_error(as,"invalid-la-lowering-layout:line=%zu",s->line);
            return false;
        }
        for(j=0U;j<plan->count;++j)
            for(i=0U;i<4U;++i)
                body[4U*j+i]=(unsigned char)(plan->words[j]>>(8U*i));
        if(!minias_emit_instruction_bytes(as,s->section,body,
                                          4U*plan->count))return false;
        for(k=0U;k<plan->fixup_count;++k) {
            const unsigned index=plan->fixups[k].symbol_operand;
            const unsigned word_index=plan->fixups[k].word;
            const MiniAsSymbolRef *symbol;
            if(plan->fixups[k].absolute_zero) {
                if(word_index>=plan->count ||
                   plan->fixups[k].kind!=MINIAS_FIXUP_LARCH_RELAX ||
                   !minias_encode_add_fixup_abs0(as,s->section,
                       s->offset+word_index*4U,
                       plan->fixups[k].kind,4U))return false;
                continue;
            }
            if(index>=m->count || word_index>=plan->count ||
               m->operands[index].kind!=MINIAS_MC_BOUND_SYMBOL ||
               m->operands[index].bound_symbol==NULL) {
                minias_set_error(as,"invalid-la-lowering-fixup:line=%zu",
                                 s->line);
                return false;
            }
            symbol=m->operands[index].bound_symbol;
            if(!minias_encode_add_fixup_ref(as,s->section,
                 s->offset+word_index*4U,plan->fixups[k].kind,4U,
                 *symbol,false))return false;
        }
        return true;
    }
    if(!la_materialize_table_word(m,&code,&table_form)) {
        minias_set_error(as,"invalid-loongarch-typed-form:line=%zu",s->line);
        return false;
    }
    if(!table_form &&
       !la_materialize_manual_word(m,&code,&manual_form)) {
        minias_set_error(as,"invalid-loongarch-manual-form:line=%zu",s->line);
        return false;
    }
    if(!table_form && !manual_form) {
        minias_set_error(as,"missing-loongarch-encoder-form:line=%zu",s->line);
        return false;
    }
    if(table_form && m->opcode!=LA_MC_FIXED) {
        minias_set_error(as,"loongarch-typed-form-opcode-mismatch");
        return false;
    }
    switch(m->opcode) {
        case LA_MC_PCALA_HI20:
        case LA_MC_PCALA_LO12:
            kind=m->opcode==LA_MC_PCALA_HI20 ?
                 MINIAS_FIXUP_LARCH_PCALA_HI20 :
                 MINIAS_FIXUP_LARCH_PCALA_LO12;
            slot=m->count-1U;
            break;
        case LA_MC_BRANCH16:
        case LA_MC_BRANCH21:
        case LA_MC_BRANCH26: {
            LaBranchPlan branch;
            bool linker_relaxable;
            kind=m->opcode==LA_MC_BRANCH16 ?
                 MINIAS_FIXUP_LARCH_B16 :
                 m->opcode==LA_MC_BRANCH21 ?
                 MINIAS_FIXUP_LARCH_B21 : MINIAS_FIXUP_LARCH_B26;
            slot=m->count-1U;
            if(!la_resolve_branch_plan(as,s,&branch))return false;
            if(!la_section_is_linker_relaxable(as,s->section,
                                               &linker_relaxable))return false;
            if(s->size!=4U || (branch.local && !branch.short_legal)) {
                minias_set_error(as,"loong-branch-out-of-range:%s:line=%zu",
                                 s->args,s->line);
                return false;
            }
            /* GNU GAS materializes the resolved displacement in the input
             * instruction even when linker relaxation requires retaining a
             * B16/B21/B26 relocation. Both pieces of state matter: the
             * raw .text oracle sees the immediate, and the final linker
             * needs the relocation when PCALA/GOT pairs may later shrink. */
            if(branch.local) {
                uint32_t off=(uint32_t)(branch.displacement/4);
                code|=kind==MINIAS_FIXUP_LARCH_B16 ?
                    ((off&UINT32_C(0xffff))<<10U) :
                    kind==MINIAS_FIXUP_LARCH_B21 ?
                    ((off&UINT32_C(0xffff))<<10U)|
                        ((off>>16U)&UINT32_C(0x1f)) :
                    ((off&UINT32_C(0xffff))<<10U)|
                        ((off>>16U)&UINT32_C(0x3ff));
                if(!linker_relaxable)kind=MINIAS_FIXUP_NONE;
            }
            break;
        }
        default: break;
    }
    for(i=0U;i<4U;++i)bytes[i]=(unsigned char)(code>>(8U*i));
    if(!minias_emit_instruction_bytes(as,s->section,bytes,sizeof(bytes)))
        return false;
    if(kind==MINIAS_FIXUP_NONE)return true;
    if(m->operands[slot].kind!=MINIAS_MC_BOUND_SYMBOL ||
       m->operands[slot].bound_symbol==NULL) {
        minias_set_error(as,"invalid-loongarch-bound-fixup");
        return false;
    }
    expr=m->operands[slot].bound_symbol;
    return minias_encode_add_fixup_ref(as,s->section,s->offset,kind,4U,
                                       *expr,false);
}
static bool la_align(MiniAs *as,const MiniAsStmt *stmt,size_t size) {
    static const unsigned char nop[4]={0x00,0x00,0x40,0x03};
    int section=stmt->section;
    if(size%4U){minias_set_error(as,"unalignable-loongarch-code-gap");return false;}
    while(size){if(!minias_emit_instruction_bytes(as,section,nop,4U))return false;size-=4U;}
    return true;
}
static bool la_profile(const char *march,const char *mabi,
                       MiniAsAssemblyConfig *cfg,char *err,size_t cap) {
    LaTargetProfile profile;
    uint32_t abi;
    if(march && strcmp(march,"loongarch64"))goto bad;
    if(!mabi || !strcmp(mabi,"lp64d"))abi=3U;
    else if(!strcmp(mabi,"lp64f"))abi=2U;
    else if(!strcmp(mabi,"lp64s"))abi=1U;
    else goto bad;
    memset(cfg,0,sizeof(*cfg));
    cfg->target=minias_target_loongarch64();
    cfg->elf_flags=UINT32_C(0x40)|abi; /* psABI v1 + precise LP64 FPU ABI */
    /* Keep the existing accepted -march spellings and extension behavior.
     * Once a target-owned directive/policy exposes narrower feature modes,
     * this profile becomes its single capability source. */
    profile.features=LA_FEATURE_LSX|LA_FEATURE_LASX;
    profile.abi=abi;
    if(sizeof(profile)>sizeof(cfg->target_profile))goto bad;
    memcpy(cfg->target_profile,&profile,sizeof(profile));
    cfg->target_profile_size=sizeof(profile);
    return true;
bad:
    if(cap)snprintf(err,cap,"unsupported-loongarch64-isa-abi");
    return false;
}
static bool la_init_target_state(MiniAs *as,
                                  const MiniAsAssemblyConfig *cfg) {
    LaTargetState *state=calloc(1U,sizeof(*state));
    LaTargetProfile profile;
    if(state==NULL) {
        minias_set_error(as,"out-of-memory:loongarch-target-state");
        return false;
    }
    state->features=LA_FEATURE_LSX|LA_FEATURE_LASX;
    if(cfg->target_profile_size!=0U) {
        if(cfg->target_profile_size!=sizeof(profile)) {
            free(state);
            minias_set_error(as,"invalid-loongarch-target-profile");
            return false;
        }
        memcpy(&profile,cfg->target_profile,sizeof(profile));
        if((profile.features &
            ~(uint32_t)(LA_FEATURE_LSX|LA_FEATURE_LASX))!=0U ||
           profile.abi<1U || profile.abi>3U ||
           cfg->elf_flags!=(UINT32_C(0x40)|profile.abi)) {
            free(state);
            minias_set_error(as,"loongarch-target-profile-abi-features");
            return false;
        }
        state->features=profile.features;
    }
    if(!la_build_mnemonic_index(as,state)) {
        free(state);
        return false;
    }
    as->target_state=state;
    return true;
}
static void la_destroy_target_state(MiniAs *as) {
    LaTargetState *state=(LaTargetState *)as->target_state;
    if(state!=NULL) {
        free(state->mnemonics);
        free(state->relaxable_sections);
        free(state);
    }
    as->target_state=NULL;
}
static bool la_validate(MiniAs *as) {
    size_t i;
    /* GNU GAS accepts raw byte payloads in executable sections and does not
     * implicitly pad odd-position instruction fragments.  Its ELF section
     * alignment is a directive/section property, not a reason to rewrite
     * source offsets here (Ubuntu 24 LoongArch GAS emits .text align=1 for
     * the real CFI short-relax instruction fixture).  Enforce only the
     * target MCInst's fixed-width encoding contract. */
    for(i=0U;i<as->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION)continue;
        if(stmt->section<0 ||
           (size_t)stmt->section>=as->section_count ||
           stmt->size==0U || (stmt->size&3U)!=0U) {
            minias_set_error(as,"invalid-loongarch-instruction-width:line=%zu",
                             stmt->line);
            return false;
        }
    }
    return true;
}
/* Target-specific ELF ABI forms also constitute the semantic capabilities.
 * In particular, only ADD/SUB/ABS admit 64-bit data on LoongArch. */
static const struct {
    MiniAsFixupKind kind;
    unsigned width;
    uint32_t relocation;
} la_fixup_forms[] = {
    {MINIAS_FIXUP_ABS,4U,1U}, /* R_LARCH_32 */
    {MINIAS_FIXUP_ABS,8U,2U}, /* R_LARCH_64 */
    {MINIAS_FIXUP_PREL32,4U,99U}, /* R_LARCH_32_PCREL */
    {MINIAS_FIXUP_ADD,1U,47U}, /* R_LARCH_ADD8 */
    {MINIAS_FIXUP_ADD,2U,48U}, /* R_LARCH_ADD16 */
    {MINIAS_FIXUP_ADD,3U,49U}, /* R_LARCH_ADD24 */
    {MINIAS_FIXUP_ADD,4U,50U},
    {MINIAS_FIXUP_ADD,8U,51U},
    {MINIAS_FIXUP_SUB,1U,52U}, /* R_LARCH_SUB8 */
    {MINIAS_FIXUP_SUB,2U,53U}, /* R_LARCH_SUB16 */
    {MINIAS_FIXUP_SUB,3U,54U}, /* R_LARCH_SUB24 */
    {MINIAS_FIXUP_SUB,4U,55U},
    {MINIAS_FIXUP_SUB,8U,56U},
    {MINIAS_FIXUP_CFI_ADD6,1U,105U}, /* R_LARCH_ADD6 */
    {MINIAS_FIXUP_CFI_SUB6,1U,106U}, /* R_LARCH_SUB6 */
    /* Variable-length ULEB fields are linker-adjusted in place.  The
     * generic MCExpr owner retains the symbolic pair; LoongArch owns its
     * relocation numbers and the zero-width field descriptor. */
    {MINIAS_FIXUP_LEB_ADD,0U,107U}, /* R_LARCH_ADD_ULEB128 */
    {MINIAS_FIXUP_LEB_SUB,0U,108U}, /* R_LARCH_SUB_ULEB128 */
    {MINIAS_FIXUP_LARCH_PCALA_HI20,4U,71U},
    {MINIAS_FIXUP_LARCH_PCALA_LO12,4U,72U},
    {MINIAS_FIXUP_LARCH_GOT_PC_HI20,4U,75U},
    {MINIAS_FIXUP_LARCH_GOT_PC_LO12,4U,76U},
    {MINIAS_FIXUP_LARCH_B16,4U,64U},
    {MINIAS_FIXUP_LARCH_B21,4U,65U},
    {MINIAS_FIXUP_LARCH_B26,4U,66U},
    {MINIAS_FIXUP_LARCH_RELAX,4U,100U}
};

static bool la_lookup_fixup_form(MiniAsFixupKind kind,unsigned width,
                                 uint32_t *relocation) {
    size_t i;
    for(i=0U;i<sizeof(la_fixup_forms)/sizeof(la_fixup_forms[0]);++i) {
        if(la_fixup_forms[i].kind!=kind || la_fixup_forms[i].width!=width)
            continue;
        if(relocation!=NULL)*relocation=la_fixup_forms[i].relocation;
        return true;
    }
    return false;
}
static bool la_supports_fixup(MiniAsFixupKind kind,unsigned width) {
    return la_lookup_fixup_form(kind,width,NULL);
}
static uint32_t la_reloc(MiniAsFixupKind kind,unsigned width) {
    uint32_t relocation=0U;
    (void)la_lookup_fixup_form(kind,width,&relocation);
    return relocation;
}
static bool la_cfi_linker_relaxation_marker(MiniAsFixupKind kind) {
    return kind==MINIAS_FIXUP_LARCH_RELAX;
}
/* GNU LoongArch retains the original local label in linker-relaxable ULEB
 * ADD/SUB pairs.  Folding `.Lend` to STT_SECTION + offset loses the symbol
 * identity that the linker/debug consumers must observe.  Keep the target
 * ABI decision here instead of special-casing ELF's generic symbol planner. */
static bool la_allow_local_section_symbol_fixup(const MiniAsFixup *fixup,
                                                const MiniAsSymbol *symbol) {
    (void)symbol;
    return fixup->kind!=MINIAS_FIXUP_LEB_ADD &&
           fixup->kind!=MINIAS_FIXUP_LEB_SUB;
}
static const MiniAsTargetOps ops={
    .name="loongarch64",.comment_prefix='#',.elf_machine=258U,
    /* GNU LoongArch GAS 2.42 (Linux cross toolchain) unconditionally keeps
     * symbolic ULEB differences as ADD/SUB pairs in relax mode.  The newer
     * interval-sensitive policy remains available in Common mode 1. */
    .leb128_reloc_mode=3U,
    .resolve_profile=la_profile,.parse_mcinst=la_parse,
    .init_target_state=la_init_target_state,
    .destroy_target_state=la_destroy_target_state,
    .measure_mcinst=la_measure,.encode=la_encode,
    .append_code_alignment=la_align,.supports_fixup=la_supports_fixup,
    .fixup_relocation_type=la_reloc,
    .validate_output=la_validate,
    .cfi_implicit_eh_frame=true,
    .allow_local_section_symbol_fixup=la_allow_local_section_symbol_fixup,
    .emit_all_section_symbols=true,
    .cfi_code_align=1U,
    .cfi_data_align=-4,
    .cfi_return_reg=1U,
    .cfi_stack_reg=3U,
    .cfi_address_size=8U,
    .cfi_eh_fde_alignment=4U,
    .cfi_eh_final_alignment=8U,
    .cfi_initial_cfa_register_only=true,
    .cfi_eh_range_relocations=true,
    .cfi_advance_reloc_mode=1U,
    .synthetic_expr_anchors_hidden=true,
    .cfi_linker_relaxation_marker=la_cfi_linker_relaxation_marker,
    .default_exec_alignment=1U,
    .cfi_anchors_hidden=true
};
const MiniAsTargetOps *minias_target_loongarch64(void){return &ops;}
