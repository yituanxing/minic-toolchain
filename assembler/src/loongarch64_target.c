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
    LA_MC_RAW = 0,
    LA_MC_PCALA_HI20,
    LA_MC_PCALA_LO12,
    LA_MC_PCREL_PSEUDO,
    LA_MC_LI32_PAIR,
    LA_MC_LI64_SEQUENCE,
    LA_MC_BRANCH16,
    LA_MC_BRANCH21,
    LA_MC_BRANCH26
} LaMCOpcode;

/* LoongArch64 LA64 basic integer target. GNU binutils tables own the reference
 * encodings; extensions and fixup relocation families are not guessed. */
static bool la_reg(const char *s,unsigned *r) {
    static const struct {const char *s;unsigned r;} aliases[]={
        {"zero",0U},{"ra",1U},{"tp",2U},{"sp",3U},
        {"a0",4U},{"a1",5U},{"a2",6U},{"a3",7U},
        {"a4",8U},{"a5",9U},{"a6",10U},{"a7",11U},
        {"t0",12U},{"t1",13U},{"t2",14U},{"t3",15U},
        {"t4",16U},{"t5",17U},{"t6",18U},{"t7",19U},
        {"t8",20U},{"u0",21U},{"fp",22U}
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
static bool la_split(const char *args,char t[4][192],unsigned *n) {
    char *slots[4]={t[0],t[1],t[2],t[3]};
    const size_t capacity[4]={sizeof(t[0]),sizeof(t[1]),
                              sizeof(t[2]),sizeof(t[3])};
    return minias_split_mc_operands(args,slots,capacity,4U,n);
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
    MiniAsSymbolExpr *copy = malloc(sizeof(*copy));
    if (!copy) {
        minias_set_error(as,"out-of-memory:loongarch-symbol-mcinst");
        return false;
    }
    *copy=*expr;
    m->operands[slot].kind=MINIAS_MC_SYMBOL;
    m->operands[slot].symbol=copy;
    return true;
}

static bool la_branch_instruction(MiniAs *as, MiniAsMCInst *m,
                                  const char *op, char t[4][192],
                                  unsigned count) {
    struct BranchKind {const char *name;unsigned operands;uint32_t base;
                       LaMCOpcode opcode;};
    static const struct BranchKind branches[]={
        {"b",1U,UINT32_C(0x50000000),LA_MC_BRANCH26},
        {"bl",1U,UINT32_C(0x54000000),LA_MC_BRANCH26},
        {"beqz",2U,UINT32_C(0x40000000),LA_MC_BRANCH21},
        {"bnez",2U,UINT32_C(0x44000000),LA_MC_BRANCH21},
        {"beq",3U,UINT32_C(0x58000000),LA_MC_BRANCH16},
        {"bne",3U,UINT32_C(0x5c000000),LA_MC_BRANCH16},
        {"blt",3U,UINT32_C(0x60000000),LA_MC_BRANCH16},
        {"bge",3U,UINT32_C(0x64000000),LA_MC_BRANCH16},
        {"bltu",3U,UINT32_C(0x68000000),LA_MC_BRANCH16},
        {"bgeu",3U,UINT32_C(0x6c000000),LA_MC_BRANCH16}
    };
    unsigned i, rj, rd, slot;
    const char *base_op=op;
    bool swap=false;
    MiniAsSymbolExpr expr;
    /* GAS pseudo comparisons reuse the existing B16 encoding and its
     * Common RV-style SymbolExpr/ELF fixup. Do not duplicate a branch ISA. */
    if(count==2U &&
       (!strcmp(op,"bltz") || !strcmp(op,"bgtz") ||
        !strcmp(op,"bgez") || !strcmp(op,"blez"))) {
        /* GNU psABI BxxZ aliases share the one existing typed B16,
         * relocation and local-branch encode path, not a second encoder. */
        bool zero_first=!strcmp(op,"bgtz") || !strcmp(op,"blez");
        memcpy(t[2],t[1],strlen(t[1])+1U);
        if(zero_first) {
            memcpy(t[1],t[0],strlen(t[0])+1U);
            memcpy(t[0],"$zero",6U);
        } else memcpy(t[1],"$zero",6U);
        base_op=(!strcmp(op,"bltz") || !strcmp(op,"bgtz"))?"blt":"bge";
        count=3U;
    }
    if(!strcmp(op,"bgtu")) {base_op="bltu";swap=true;}
    else if(!strcmp(op,"bleu")) {base_op="bgeu";swap=true;}
    else if(!strcmp(op,"bgt")) {base_op="blt";swap=true;}
    else if(!strcmp(op,"ble")) {base_op="bge";swap=true;}
    for(i=0;i<sizeof(branches)/sizeof(branches[0]);++i) {
        const struct BranchKind *b=&branches[i];
        if(strcmp(base_op,b->name)!=0 || count!=b->operands)continue;
        if (!minias_parse_symbol_addend(t[count-1U],&expr) ||
            (expr.addend&3)!=0)
            return false;
        rj=0U;rd=0U;
        if(count>=2U && !la_reg(t[0],&rj))return false;
        if(count==3U && !la_reg(t[1],&rd))return false;
        if(swap && count==3U) {unsigned tmp=rj;rj=rd;rd=tmp;}
        slot=count-1U;
        if (!la_symbol_operand(as,m,slot,&expr))return false;
        m->opcode=(unsigned)b->opcode;
        m->count=count;
        if(count>=2U) {m->operands[0].kind=MINIAS_MC_REGISTER;
                       m->operands[0].reg=rj;}
        if(count==3U) {m->operands[1].kind=MINIAS_MC_REGISTER;
                       m->operands[1].reg=rd;}
        /* Loong B21: high 5 bits in [4:0], low 16 in [25:10].
         * B16: 16 bits in [25:10]; B26: high 10 in [9:0]. */
        m->encoding=b->base |
            /* GNU LoongArch field layout: rj in bits 9:5; rd in bits
             * 4:0 (B16). B21 reuses low bits 4:0 for high displacement. */
            (count>=2U?(rj<<5U):0U) |
            (count==3U?rd:0U);
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

static bool la_parse_vector_pattern(MiniAsMCInst *m,const char *op,
                                    char args[4][192],unsigned count,
                                    bool *recognized) {
    size_t i;
    *recognized=false;
    for(i=0U;i<sizeof(la_vector_patterns)/sizeof(la_vector_patterns[0]);++i) {
        const LaVectorPattern *pattern=&la_vector_patterns[i];
        const LaVectorLayout *layout=&la_vector_layouts[pattern->shape];
        uint32_t encoding=pattern->opcode;
        unsigned arg;
        if(strcmp(pattern->name,op)!=0)continue;
        *recognized=true;
        if(count!=layout->count)return false;
        for(arg=0U;arg<count;++arg) {
            LaOperandClass cls=layout->classes[arg];
            unsigned reg;
            int64_t value;
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
                /* Signed operands retain their low encoded bits; the
                 * descriptor's exact field width and signed bounds are
                 * both checked before truncation; no max-based guessing. */
                uint32_t mask;
                if(pattern->immediate_bits==0U ||
                   pattern->immediate_bits>12U)return false;
                mask=(UINT32_C(1)<<pattern->immediate_bits)-1U;
                encoding|=(((uint32_t)value)&mask)<<layout->shifts[arg];
                m->operands[arg].kind=MINIAS_MC_IMMEDIATE;
                m->operands[arg].immediate=(uint64_t)value;
            } else {
                encoding|=((uint32_t)reg)<<layout->shifts[arg];
                m->operands[arg].kind=MINIAS_MC_REGISTER;
                m->operands[arg].reg=reg;
                m->operands[arg].reg_class=cls==LA_ARG_GPR ?
                    MINIAS_MC_REG_GPR:MINIAS_MC_REG_VECTOR;
            }
        }
        m->count=count;
        m->encoding=encoding;
        return true;
    }
    return false;
}

/* Populate semantic MC operands even for fixed-width scalar words. The ELF
 * encoder happens to need only the stored word today, but a target-specific
 * analysis/verification pass must not have to reparse source text to recover
 * an instruction's register and signed-immediate operands. */
static void la_capture_gpr_operand(MiniAsMCInst *m,unsigned slot,unsigned reg) {
    m->operands[slot].kind=MINIAS_MC_REGISTER;
    m->operands[slot].reg=reg;
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
    LA_BANK_GPR, LA_BANK_FPR, LA_BANK_FCSR, LA_BANK_FCC,
    LA_BANK_SIGNED12, LA_BANK_UNSIGNED5, LA_BANK_UNSIGNED8,
    LA_BANK_UNSIGNED14, LA_BANK_UNSIGNED15
} LaRegisterOperandBank;
typedef struct {
    const char *name;
    uint32_t opcode;
    unsigned count;
    LaRegisterOperandBank banks[3];
    unsigned shifts[3];
    unsigned min_register[3];
} LaSpecialRegisterPattern;

#define LA_FIELDS_2  {0U,5U},{0U}
#define LA_FIELDS_3  {0U,5U,10U},{0U}
static const LaSpecialRegisterPattern la_special_register_patterns[] = {
    {"movgr2fr.d",0x0114a800U,2U,{LA_BANK_FPR,LA_BANK_GPR},LA_FIELDS_2},
    {"movgr2fr.w",0x0114a400U,2U,{LA_BANK_FPR,LA_BANK_GPR},LA_FIELDS_2},
    {"movfr2gr.d",0x0114b800U,2U,{LA_BANK_GPR,LA_BANK_FPR},LA_FIELDS_2},
    {"movfcsr2gr",0x0114c800U,2U,{LA_BANK_GPR,LA_BANK_FCSR},LA_FIELDS_2},
    {"movgr2fcsr",0x0114c000U,2U,{LA_BANK_FCSR,LA_BANK_GPR},LA_FIELDS_2},
    {"movcf2gr",0x0114dc00U,2U,{LA_BANK_GPR,LA_BANK_FCC},LA_FIELDS_2},
    {"movgr2cf",0x0114d800U,2U,{LA_BANK_FCC,LA_BANK_GPR},LA_FIELDS_2},
    {"fld.s",0x2b000000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fst.s",0x2b400000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fld.d",0x2b800000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
    {"fst.d",0x2bc00000U,3U,{LA_BANK_FPR,LA_BANK_GPR,LA_BANK_SIGNED12},LA_FIELDS_3},
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
#undef LA_FIELDS_2
#undef LA_FIELDS_3

static bool la_parse_special_register_pattern(MiniAsMCInst *m,const char *op,
                                               char args[4][192],unsigned count,
                                               bool *recognized) {
    size_t i;
    *recognized=false;
    for(i=0U;i<sizeof(la_special_register_patterns)/
                   sizeof(la_special_register_patterns[0]);++i) {
        const LaSpecialRegisterPattern *rule=&la_special_register_patterns[i];
        unsigned arg;
        if(strcmp(rule->name,op)!=0)continue;
        *recognized=true;
        if(count!=rule->count)return false;
        m->encoding=rule->opcode;
        for(arg=0U;arg<count;++arg) {
            unsigned reg=0U;
            int64_t imm=0;
            bool valid=false, immediate=false;
            unsigned width=0U;
            switch(rule->banks[arg]) {
                case LA_BANK_GPR:valid=la_reg(args[arg],&reg);break;
                case LA_BANK_FPR:
                    valid=la_special_reg(args[arg],"$f",31U,&reg);break;
                case LA_BANK_FCSR:
                    valid=la_special_reg(args[arg],"$fcsr",3U,&reg);break;
                case LA_BANK_FCC:
                    valid=la_special_reg(args[arg],"$fcc",7U,&reg);break;
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
                const uint32_t mask=(UINT32_C(1)<<width)-1U;
                m->encoding|=(((uint32_t)imm)&mask)<<rule->shifts[arg];
                la_capture_imm_operand(m,arg,imm);
            } else {
                m->encoding|=((uint32_t)reg)<<rule->shifts[arg];
                la_capture_gpr_operand(m,arg,reg);
                if(rule->banks[arg]==LA_BANK_FPR)
                    m->operands[arg].reg_class=MINIAS_MC_REG_FPR;
            }
        }
        m->count=count;
        return true;
    }
    return false;
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

static const LaScalarPattern *la_find_scalar_pattern(const char *op) {
    size_t i;
    for(i=0U;i<sizeof(la_scalar_patterns)/sizeof(la_scalar_patterns[0]);++i)
        if(strcmp(la_scalar_patterns[i].name,op)==0)
            return &la_scalar_patterns[i];
    return NULL;
}

static bool la_parse_scalar_pattern(MiniAsMCInst *m,const char *op,
                                    char args[4][192],unsigned count,
                                    bool *recognized) {
    const LaScalarPattern *rule=la_find_scalar_pattern(op);
    unsigned rd=0U,rj=0U,rk=0U,need;
    int64_t value=0;
    uint32_t bits=0U,mask=0U;
    *recognized=rule!=NULL;
    if(rule==NULL)return false;
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
    m->encoding=rule->opcode;
    if(rule->shape==LA_SCALAR_GPR2) {
        m->encoding|=(rj<<5U)|rd;
        la_capture_gpr_operand(m,0U,rd);
        la_capture_gpr_operand(m,1U,rj);
    } else if(rule->shape==LA_SCALAR_GPR3 ||
              rule->shape==LA_SCALAR_ATOMIC) {
        if(!la_reg(args[2],&rk))return false;
        /* Atomics swap assembly operand 1/2 field placement relative to
         * ordinary three-register instructions. */
        m->encoding|=(rule->shape==LA_SCALAR_ATOMIC ?
            (rj<<10U)|(rk<<5U) : (rk<<10U)|(rj<<5U))|rd;
        la_capture_rrr(m,rd,rj,rk);
        if(need==4U)la_capture_imm_operand(m,3U,0);
    } else {
        if(rule->shape==LA_SCALAR_UPPER20) {
            if(!minias_eval_i64_expression(args[1],&value) ||
               value<-524288 || value>524287)return false;
            m->encoding|=(((uint32_t)value)&UINT32_C(0xfffff))<<5U;
            la_capture_gpr_operand(m,0U,rd);
            la_capture_imm_operand(m,1U,value);
        } else {
            if(!minias_eval_i64_expression(args[2],&value))return false;
            switch(rule->shape) {
                case LA_SCALAR_IMM12:
                    if(value<-2048 || value>2047)return false;
                    bits=12U;break;
                case LA_SCALAR_UIMM12:
                    if(value<0 || value>4095)return false;
                    bits=12U;break;
                case LA_SCALAR_SHIFT5:
                    if(value<0 || value>31)return false;
                    bits=5U;break;
                case LA_SCALAR_SHIFT6:
                    if(value<0 || value>63)return false;
                    bits=6U;break;
                case LA_SCALAR_PTR14:
                    if(value<-32768 || value>32764 || value%4!=0)
                        return false;
                    value/=4;
                    bits=14U;break;
                default:return false;
            }
            mask=(UINT32_C(1)<<bits)-1U;
            m->encoding|=(((uint32_t)value)&mask)<<10U;
            m->encoding|=(rj<<5U);
            la_capture_rri(m,rd,rj,rule->shape==LA_SCALAR_PTR14 ?
                           value*4 : value);
        }
        m->encoding|=rd;
    }
    m->count=need;
    return true;
}

static bool la_parse(MiniAs *as,MiniAsStmt *s) {
    MiniAsMCInst *m=minias_mcinst_alloc(4U);
    char t[4][192];unsigned n,i,rd,rj,rk;
    if(!m){minias_set_error(as,"out-of-memory:loongarch64-mcinst");return false;}
    if(!*s->args) {
        static const struct {const char *name;uint32_t bits;} privileged[]={
            {"tlbclr",UINT32_C(0x06482000)},
            {"tlbflush",UINT32_C(0x06482400)},
            {"tlbsrch",UINT32_C(0x06482800)},
            {"tlbrd",UINT32_C(0x06482c00)},
            {"tlbwr",UINT32_C(0x06483000)},
            {"tlbfill",UINT32_C(0x06483400)},
            {"ertn",UINT32_C(0x06483800)}
        };
        for(i=0U;i<sizeof(privileged)/sizeof(privileged[0]);++i)
            if(!strcmp(s->op,privileged[i].name)) {
                m->encoding=privileged[i].bits;goto done;
            }
        if(!strcmp(s->op,"nop")){m->encoding=UINT32_C(0x03400000);goto done;}
        if(!strcmp(s->op,"ret")){m->encoding=UINT32_C(0x4c000020);goto done;}
    }
    if(!la_split(s->args,t,&n))goto fail;
    if(la_branch_instruction(as,m,s->op,t,n)) goto done;
    /* Target-private instruction formats are selected by mnemonic first;
     * malformed SIMD operands must not fall through into scalar encoders. */
    {
        bool vector_mnemonic=false;
        if(la_parse_vector_pattern(m,s->op,t,n,&vector_mnemonic))goto done;
        if(vector_mnemonic)goto fail;
    }
    if(n==2U && !strcmp(s->op,"la.pcrel") && la_reg(t[0],&rd)) {
        MiniAsSymbolExpr expr;
        if(!minias_parse_symbol_addend(t[1],&expr) ||
           !la_symbol_operand(as,m,1U,&expr))goto fail;
        m->opcode=LA_MC_PCREL_PSEUDO;
        m->count=2U;
        m->encoding=UINT32_C(0x1a000000)|rd;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        goto done;
    }
    {
        bool special_mnemonic=false;
        if(la_parse_special_register_pattern(m,s->op,t,n,&special_mnemonic))
            goto done;
        if(special_mnemonic)goto fail;
    }
    if(n==2U && (!strcmp(s->op,"li.w") || !strcmp(s->op,"li.d")) &&
       la_reg(t[0],&rd)) {
        int64_t value;
        if(minias_eval_i64_expression(t[1],&value) &&
           value>=-2048 && value<=2047) {
            m->count=2U;
            m->encoding=UINT32_C(0x02800000) |
                (((uint32_t)value&UINT32_C(0xfff))<<10U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint64_t)value;
            goto done;
        }
        if(!strcmp(s->op,"li.w") &&
           minias_eval_i64_expression(t[1],&value) &&
           value>=4096 && value<=INT32_MAX &&
           value%4096!=0) {
            m->opcode=LA_MC_LI32_PAIR;
            m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint64_t)value;
            goto done;
        }
        /* Single-word GNU LI.W for exact 4K multiples: LU12I.W.
         * Other large constants may require a two-instruction sequence;
         * continue to reject those until variable-size pseudos exist. */
        if(minias_eval_i64_expression(t[1],&value) &&
           value%4096==0 && value>=INT32_MIN && value<=INT32_MAX) {
            int64_t hi=value/4096;
            m->count=2U;
            m->encoding=UINT32_C(0x14000000)|
                (((uint32_t)hi&UINT32_C(0xfffff))<<5U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint64_t)value;
            goto done;
        }
    }
    /* GNU LI.W for 0x800..0xfff is a single ORI from $zero. Explicit
     * 4KB-aligned large values use LU12I.W above; no variable-sized
     * multi-instruction pseudoinstructions are guessed here. */
    if(n==2U && !strcmp(s->op,"li.w") && la_reg(t[0],&rd)) {
        int64_t v;
        if(minias_eval_i64_expression(t[1],&v) && v>=2048 && v<=4095) {
            m->count=2U;
            m->encoding=UINT32_C(0x03800000) |
                ((uint32_t)v<<10U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint64_t)v;
            goto done;
        }
    }
    /* Full signed/unsigned 64-bit LI.D macro expansion uses the exact
     * GNU GAS 12/20/20/12 sign-extension reduction, not merely a
     * hard-coded fix for Linux's 0x8000/0x9000 upper bits. */
    if(n==2U && !strcmp(s->op,"li.d") && la_reg(t[0],&rd)) {
        uint64_t bits;
        if(minias_eval_u64_expression(t[1],&bits)) {
            m->opcode=LA_MC_LI64_SEQUENCE;m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=bits;
            goto done;
        }
    }
    if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        static const struct {const char *op;uint32_t base;unsigned bits;} bitfield[]={
            {"bstrins.w",UINT32_C(0x00600000),5U},
            {"bstrpick.w",UINT32_C(0x00608000),5U},
            {"bstrins.d",UINT32_C(0x00800000),6U},
            {"bstrpick.d",UINT32_C(0x00c00000),6U}
        };
        uint64_t msb,lsb;
        if(minias_eval_u64_expression(t[2],&msb) &&
           minias_eval_u64_expression(t[3],&lsb)) {
            for(i=0U;i<sizeof(bitfield)/sizeof(bitfield[0]);++i)
                if(strcmp(s->op,bitfield[i].op)==0 &&
                   msb<(UINT64_C(1)<<bitfield[i].bits) &&
                   lsb<=msb) {
                    m->count=4U;
                    m->encoding=bitfield[i].base |
                        ((uint32_t)msb<<16U)|((uint32_t)lsb<<10U) |
                        (rj<<5U)|rd;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=rd;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=rj;
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=msb;
                    m->operands[3].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[3].immediate=lsb;
                    goto done;
                }
        }
    }
    if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
       la_reg(t[2],&rk) &&
       (!strcmp(s->op,"bytepick.w") || !strcmp(s->op,"bytepick.d"))) {
        uint64_t offset;bool wide=!strcmp(s->op,"bytepick.d");
        if(minias_eval_u64_expression(t[3],&offset) &&
           offset<(wide?8U:4U)) {
            m->count=4U;
            m->encoding=(wide?UINT32_C(0x000c0000):UINT32_C(0x00080000)) |
                        ((uint32_t)offset<<15U)|(rk<<10U)|(rj<<5U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_REGISTER;m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_REGISTER;m->operands[2].reg=rk;
            m->operands[3].kind=MINIAS_MC_IMMEDIATE;
            m->operands[3].immediate=offset;
            goto done;
        }
    }
    /* GNU LA opcode tables: sa2 lives in bits 16:15 and the source
     * syntax specifies sa2+1 (one through four), not the encoded 0..3.
     * Index/shift-add is a single target-local MCInst, not Common logic. */
    if(n==4U && la_reg(t[0],&rd) && la_reg(t[1],&rj) &&
       la_reg(t[2],&rk)) {
        static const struct {const char *name;uint32_t base;} alsls[]={
            {"alsl.w",UINT32_C(0x00040000)},
            {"alsl.wu",UINT32_C(0x00060000)},
            {"alsl.d",UINT32_C(0x002c0000)}
        };
        uint64_t shift;
        if(minias_eval_u64_expression(t[3],&shift) &&
           shift>=1U && shift<=4U) {
            for(i=0U;i<sizeof(alsls)/sizeof(alsls[0]);++i) {
                if(strcmp(s->op,alsls[i].name)!=0)continue;
                m->count=4U;
                m->encoding=alsls[i].base |
                    (((uint32_t)shift-1U)<<15U)|
                    (rk<<10U)|(rj<<5U)|rd;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=rj;
                m->operands[2].kind=MINIAS_MC_REGISTER;
                m->operands[2].reg=rk;
                m->operands[3].kind=MINIAS_MC_IMMEDIATE;
                m->operands[3].immediate=shift;
                goto done;
            }
        }
    }
    /* LoongArch jr register == jirl $zero,$reg,0.  No relocation. */
    if(!strcmp(s->op,"jr") && n==1U && la_reg(t[0],&rj)) {
        m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rj;
        m->encoding=UINT32_C(0x4c000000)|(rj<<5U);goto done;
    }
    /* JIRL is a signed 16-bit displacement in 4-byte units. The JR alias
     * above is a canonical zero-link JIRL, not a second instruction family. */
    if(!strcmp(s->op,"jirl") && n==3U &&
       la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        int64_t delta;
        if(minias_eval_i64_expression(t[2],&delta) &&
           delta>=-131072 && delta<=131068 && delta%4==0) {
            m->count=3U;
            m->operands[0].kind=MINIAS_MC_REGISTER;m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_REGISTER;m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=(uint64_t)delta;
            m->encoding=UINT32_C(0x4c000000) |
                (((uint32_t)(delta/4)&UINT32_C(0xffff))<<10U) |
                (rj<<5U)|rd;
            goto done;
        }
    }
    if (n==2U && la_reg(t[0],&rd)) {
        MiniAsSymbolExpr expr;
        if(strcmp(s->op,"pcalau12i")==0 &&
           la_reloc_expr(t[1],"%pc_hi20",&expr)) {
            if(!la_symbol_operand(as,m,1U,&expr))goto fail;
            m->opcode=LA_MC_PCALA_HI20;
            m->count=2U;m->encoding=UINT32_C(0x1a000000)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            goto done;
        }
    }
    if(!strcmp(s->op,"move") && n==2U &&
        la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        m->count=2U;m->encoding=UINT32_C(0x00150000) |
            (rj<<5U)|rd;
        la_capture_gpr_operand(m,0U,rd);
        la_capture_gpr_operand(m,1U,rj);
        goto done;
    }
    /* LoongArch PREFETCH has a u5 cache hint, a base register and either
     * a signed 12-bit displacement or an index register. Both variants
     * use the exact GNU opcodes and shared typed MCInst/ELF pipeline. */
    if(n==3U && (!strcmp(s->op,"preld") || !strcmp(s->op,"preldx"))) {
        uint64_t hint;
        int64_t displacement;
        if(!minias_eval_u64_expression(t[0],&hint) || hint>31U ||
           !la_reg(t[1],&rj))goto fail;
        if(!strcmp(s->op,"preld")) {
            if(!minias_eval_i64_expression(t[2],&displacement) ||
               displacement<-2048 || displacement>2047)goto fail;
            m->encoding=UINT32_C(0x2ac00000) |
                (((uint32_t)displacement&UINT32_C(0xfff))<<10U) |
                (rj<<5U)|(uint32_t)hint;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=(uint64_t)displacement;
        }else {
            if(!la_reg(t[2],&rk))goto fail;
            m->encoding=UINT32_C(0x382c0000) |
                (rk<<10U)|(rj<<5U)|(uint32_t)hint;
            m->operands[2].kind=MINIAS_MC_REGISTER;
            m->operands[2].reg=rk;
        }
        m->count=3U;
        m->operands[0].kind=MINIAS_MC_IMMEDIATE;
        m->operands[0].immediate=hint;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rj;
        goto done;
    }
    /* Signed si16 GOT/LDptr address arithmetic, no guessed relocations. */
    if(n==3U && !strcmp(s->op,"addu16i.d")) {
        int64_t imm;
        if(!la_reg(t[0],&rd) || !la_reg(t[1],&rj) ||
           !minias_eval_i64_expression(t[2],&imm) ||
           imm<-32768 || imm>32767)goto fail;
        m->count=3U;
        m->encoding=UINT32_C(0x10000000) |
            (((uint32_t)imm&UINT32_C(0xffff))<<10U) |
            (rj<<5U)|rd;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rj;
        m->operands[2].kind=MINIAS_MC_IMMEDIATE;
        m->operands[2].immediate=(uint64_t)imm;
        goto done;
    }
    /* Symbolic PCALA low-part needs a relocation. The same scalar opcode
     * descriptor supplies the signed 12-bit field and GPR positions. */
    if(n==3U && la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        const LaScalarPattern *rule=la_find_scalar_pattern(s->op);
        MiniAsSymbolExpr expr;
        if(rule!=NULL && rule->shape==LA_SCALAR_IMM12 &&
           la_reloc_expr(t[2],"%pc_lo12",&expr)) {
            if(!la_symbol_operand(as,m,2U,&expr))goto fail;
            m->opcode=LA_MC_PCALA_LO12;
            m->count=3U;
            m->encoding=rule->opcode|(rj<<5U)|rd;
            la_capture_gpr_operand(m,0U,rd);
            la_capture_gpr_operand(m,1U,rj);
            goto done;
        }
    }
    {
        bool scalar_mnemonic=false;
        if(la_parse_scalar_pattern(m,s->op,t,n,&scalar_mnemonic))goto done;
        if(scalar_mnemonic)goto fail;
    }
fail:
    minias_set_error(as,"unsupported-loongarch64-operands:%s:%s:line=%zu",
                     s->op,s->args,s->line);
    minias_mcinst_destroy(m);return false;
done:
    s->mcinst=m;return true;
}
static bool la_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    if(!s->mcinst){minias_set_error(as,"missing-la-mcinst");return false;}
    if(s->mcinst->opcode==LA_MC_LI64_SEQUENCE) {
        uint32_t words[4];
        unsigned count=la_li_d_words(s->mcinst->operands[1].immediate,
                        s->mcinst->operands[0].reg,words);
        *size=count*4U;
        return true;
    }
    *size=(s->mcinst->opcode==LA_MC_PCREL_PSEUDO ||
           s->mcinst->opcode==LA_MC_LI32_PAIR)?8U:4U;
    return true;
}
static bool la_encode(MiniAs *as,const MiniAsStmt *s) {
    unsigned char bytes[4];
    const MiniAsMCInst *m=s->mcinst;
    uint32_t code;
    unsigned i, slot=0U;
    MiniAsFixupKind kind=MINIAS_FIXUP_NONE;
    const MiniAsSymbolExpr *expr=NULL;
    if(!m){minias_set_error(as,"missing-la-mcinst");return false;}
    code=m->encoding;
    if(m->opcode==LA_MC_LI64_SEQUENCE) {
        uint32_t words[4];unsigned count,j;
        unsigned char body[16];
        count=la_li_d_words(m->operands[1].immediate,
                            m->operands[0].reg,words);
        if(count==0U || count>4U || s->size!=4U*count) {
            minias_set_error(as,"invalid-la-li64-layout:line=%zu",s->line);
            return false;
        }
        for(j=0U;j<count;++j)
            for(i=0U;i<4U;++i)
                body[4U*j+i]=(unsigned char)(words[j]>>(8U*i));
        return minias_emit_instruction_bytes(as,s->section,body,4U*count);
    }
    if(m->opcode==LA_MC_LI32_PAIR) {
        const uint32_t v=(uint32_t)m->operands[1].immediate;
        const unsigned rd=m->operands[0].reg;
        const uint32_t hi=UINT32_C(0x14000000)|((v>>12U)<<5U)|rd;
        const uint32_t lo=UINT32_C(0x03800000)|
            ((v&UINT32_C(0xfff))<<10U)|(rd<<5U)|rd;
        unsigned char pair[8];
        if(s->size!=8U){minias_set_error(as,"invalid-la-li32-layout");return false;}
        for(i=0U;i<4U;++i){
            pair[i]=(unsigned char)(hi>>(i*8U));
            pair[i+4U]=(unsigned char)(lo>>(i*8U));
        }
        return minias_emit_instruction_bytes(as,s->section,pair,sizeof(pair));
    }
    if(m->opcode==LA_MC_PCREL_PSEUDO) {
        const MiniAsSymbolExpr *symbol=m->operands[1].symbol;
        unsigned rd=m->operands[0].reg;
        uint32_t first=UINT32_C(0x1a000000)|rd;
        uint32_t second=UINT32_C(0x02c00000)|(rd<<5U)|rd;
        unsigned char pair[8];
        if(!symbol || s->size!=8U) {
            minias_set_error(as,"invalid-la-pcrel-layout:line=%zu",s->line);
            return false;
        }
        for(i=0U;i<4U;++i) {
            pair[i]=(unsigned char)(first>>(8U*i));
            pair[4U+i]=(unsigned char)(second>>(8U*i));
        }
        return minias_emit_instruction_bytes(as,s->section,pair,sizeof(pair)) &&
            minias_add_fixup(as,s->section,s->offset,
                             MINIAS_FIXUP_LARCH_PCALA_HI20,4U,
                             symbol->name,symbol->addend) &&
            minias_add_fixup(as,s->section,s->offset+4U,
                             MINIAS_FIXUP_LARCH_PCALA_LO12,4U,
                             symbol->name,symbol->addend);
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
            MiniAsSymbol *target;
            int64_t diff;
            kind=m->opcode==LA_MC_BRANCH16 ?
                 MINIAS_FIXUP_LARCH_B16 :
                 m->opcode==LA_MC_BRANCH21 ?
                 MINIAS_FIXUP_LARCH_B21 : MINIAS_FIXUP_LARCH_B26;
            slot=m->count-1U;
            expr=m->operands[slot].symbol;
            target=minias_get_symbol(as,expr->name,false);
            if(target != NULL && target->defined &&
               target->section==s->section &&
               target->value<=INT64_MAX && s->offset<=INT64_MAX) {
                diff=(int64_t)target->value-(int64_t)s->offset;
                if((expr->addend>0 && diff>INT64_MAX-expr->addend) ||
                   (expr->addend<0 && diff<INT64_MIN-expr->addend)) {
                    minias_set_error(as,"loong-branch-addend-overflow");
                    return false;
                }
                diff+=expr->addend;
                /* Ranges include the low two implicit zero bits. */
                if((diff&3)!=0 ||
                   (kind==MINIAS_FIXUP_LARCH_B16 &&
                      (diff<-(INT64_C(1)<<17) || diff>=(INT64_C(1)<<17))) ||
                   (kind==MINIAS_FIXUP_LARCH_B21 &&
                      (diff<-(INT64_C(1)<<22) || diff>=(INT64_C(1)<<22))) ||
                   (kind==MINIAS_FIXUP_LARCH_B26 &&
                      (diff<-(INT64_C(1)<<27) || diff>=(INT64_C(1)<<27)))) {
                    minias_set_error(as,"loong-branch-out-of-range:%s:line=%zu",
                                     s->args,s->line);
                    return false;
                }
                {
                    uint32_t off=(uint32_t)(diff/4);
                    code|=kind==MINIAS_FIXUP_LARCH_B16 ?
                        ((off&UINT32_C(0xffff))<<10U) :
                        kind==MINIAS_FIXUP_LARCH_B21 ?
                        ((off&UINT32_C(0xffff))<<10U) |
                            ((off>>16U)&UINT32_C(0x1f)) :
                        ((off&UINT32_C(0xffff))<<10U) |
                            ((off>>16U)&UINT32_C(0x3ff));
                    kind=MINIAS_FIXUP_NONE;
                }
            }
            break;
        }
        default: break;
    }
    for(i=0U;i<4U;++i)bytes[i]=(unsigned char)(code>>(8U*i));
    if(!minias_emit_instruction_bytes(as,s->section,bytes,sizeof(bytes)))
        return false;
    if(kind==MINIAS_FIXUP_NONE)return true;
    expr=m->operands[slot].symbol;
    return minias_add_fixup(as,s->section,s->offset,kind,4U,
                            expr->name,expr->addend);
}
static bool la_align(MiniAs *as,int section,size_t size) {
    static const unsigned char nop[4]={0x00,0x00,0x40,0x03};
    if(size%4U){minias_set_error(as,"unalignable-loongarch-code-gap");return false;}
    while(size){if(!minias_emit_instruction_bytes(as,section,nop,4U))return false;size-=4U;}
    return true;
}
static bool la_profile(const char *march,const char *mabi,
                       MiniAsAssemblyConfig *cfg,char *err,size_t cap) {
    uint32_t abi;
    if(march && strcmp(march,"loongarch64"))goto bad;
    if(!mabi || !strcmp(mabi,"lp64d"))abi=3U;
    else if(!strcmp(mabi,"lp64f"))abi=2U;
    else if(!strcmp(mabi,"lp64s"))abi=1U;
    else goto bad;
    memset(cfg,0,sizeof(*cfg));
    cfg->target=minias_target_loongarch64();
    cfg->elf_flags=UINT32_C(0x40)|abi; /* psABI v1 + precise LP64 FPU ABI */
    return true;
bad:
    if(cap)snprintf(err,cap,"unsupported-loongarch64-isa-abi");
    return false;
}
static bool la_validate(MiniAs *as) {
    size_t i;
    for(i=0U;i<as->section_count;++i)
        if((as->sections[i].flags & MINIAS_SHF_EXECINSTR) &&
           as->sections[i].size%4U){
            minias_set_error(as,"unaligned-loongarch-text");
            return false;
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
    {MINIAS_FIXUP_ADD,4U,50U},
    {MINIAS_FIXUP_ADD,8U,51U},
    {MINIAS_FIXUP_SUB,4U,55U},
    {MINIAS_FIXUP_SUB,8U,56U},
    {MINIAS_FIXUP_LARCH_PCALA_HI20,4U,71U},
    {MINIAS_FIXUP_LARCH_PCALA_LO12,4U,72U},
    {MINIAS_FIXUP_LARCH_B16,4U,64U},
    {MINIAS_FIXUP_LARCH_B21,4U,65U},
    {MINIAS_FIXUP_LARCH_B26,4U,66U}
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
static const MiniAsTargetOps ops={
    .name="loongarch64",.comment_prefix='#',.elf_machine=258U,
    .resolve_profile=la_profile,.parse_mcinst=la_parse,
    .measure_mcinst=la_measure,.encode=la_encode,
    .append_code_alignment=la_align,.supports_fixup=la_supports_fixup,
    .fixup_relocation_type=la_reloc,
    .validate_output=la_validate,
    .cfi_implicit_eh_frame=true,
    .cfi_code_align=4U,
    .cfi_data_align=-8,
    .cfi_return_reg=1U,
    .cfi_stack_reg=3U,
    .cfi_address_size=8U
};
const MiniAsTargetOps *minias_target_loongarch64(void){return &ops;}
