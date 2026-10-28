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
        if(!minias_parse_symbol_addend(t[1],&expr))return false;
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

static bool la_parse(MiniAs *as,MiniAsStmt *s) {
    static const struct {const char *name;uint32_t code;} gpr3[]={
        {"add.d",UINT32_C(0x00108000)},{"sub.d",UINT32_C(0x00118000)},
        {"add.w",UINT32_C(0x00100000)},{"sub.w",UINT32_C(0x00110000)},
        {"and",UINT32_C(0x00148000)},{"or",UINT32_C(0x00150000)},
        {"xor",UINT32_C(0x00158000)},{"slt",UINT32_C(0x00120000)},
        {"sltu",UINT32_C(0x00128000)},
        {"maskeqz",UINT32_C(0x00130000)},
        {"masknez",UINT32_C(0x00138000)},
        {"nor",UINT32_C(0x00140000)},
        {"orn",UINT32_C(0x00160000)},
        {"andn",UINT32_C(0x00168000)},
        {"sll.w",UINT32_C(0x00170000)},
        {"srl.w",UINT32_C(0x00178000)},
        {"sra.w",UINT32_C(0x00180000)},
        {"sll.d",UINT32_C(0x00188000)},
        {"srl.d",UINT32_C(0x00190000)},
        {"sra.d",UINT32_C(0x00198000)},
        {"rotr.w",UINT32_C(0x001b0000)},
        {"rotr.d",UINT32_C(0x001b8000)},
        {"mul.w",UINT32_C(0x001c0000)},
        {"mul.d",UINT32_C(0x001d8000)},
        {"mulh.w",UINT32_C(0x001c8000)},
        {"mulh.wu",UINT32_C(0x001d0000)},
        {"mulh.d",UINT32_C(0x001e0000)},
        {"mulh.du",UINT32_C(0x001e8000)},
        {"div.w",UINT32_C(0x00200000)},
        {"mod.w",UINT32_C(0x00208000)},
        {"div.wu",UINT32_C(0x00210000)},
        {"mod.wu",UINT32_C(0x00218000)},
        {"div.d",UINT32_C(0x00220000)},
        {"mod.d",UINT32_C(0x00228000)},
        {"div.du",UINT32_C(0x00230000)},
        {"mod.du",UINT32_C(0x00238000)},
        {"ldx.b",UINT32_C(0x38000000)},
        {"ldx.h",UINT32_C(0x38040000)},
        {"ldx.w",UINT32_C(0x38080000)},
        {"ldx.d",UINT32_C(0x380c0000)},
        {"stx.b",UINT32_C(0x38100000)},
        {"stx.h",UINT32_C(0x38140000)},
        {"stx.w",UINT32_C(0x38180000)},
        {"stx.d",UINT32_C(0x381c0000)},
        {"ldx.bu",UINT32_C(0x38200000)},
        {"ldx.hu",UINT32_C(0x38240000)},
        {"ldx.wu",UINT32_C(0x38280000)}
    };
    static const struct {const char *name;uint32_t code;} imm12[]={
        {"addi.d",UINT32_C(0x02c00000)},
        {"addi.w",UINT32_C(0x02800000)},
        {"slti",UINT32_C(0x02000000)},
        {"sltui",UINT32_C(0x02400000)},
        {"lu52i.d",UINT32_C(0x03000000)},
        {"ld.d",UINT32_C(0x28c00000)},
        {"st.d",UINT32_C(0x29c00000)},
        {"ld.w",UINT32_C(0x28800000)},
        {"st.w",UINT32_C(0x29800000)},
        {"ld.b",UINT32_C(0x28000000)},
        {"ld.h",UINT32_C(0x28400000)},
        {"st.b",UINT32_C(0x29000000)},
        {"st.h",UINT32_C(0x29400000)},
        {"ld.bu",UINT32_C(0x2a000000)},
        {"ld.hu",UINT32_C(0x2a400000)},
        {"ld.wu",UINT32_C(0x2a800000)}
    };
    /* GNU LA64 so10:14<<2; require byte offset aligned to four,
     * encode the signed scaled si14 without inventing relocations. */
    static const struct {const char *name;uint32_t code;} ptr14[]={
        {"ll.w",UINT32_C(0x20000000)},
        {"sc.w",UINT32_C(0x21000000)},
        {"ll.d",UINT32_C(0x22000000)},
        {"sc.d",UINT32_C(0x23000000)},
        {"ldptr.w",UINT32_C(0x24000000)},
        {"stptr.w",UINT32_C(0x25000000)},
        {"ldptr.d",UINT32_C(0x26000000)},
        {"stptr.d",UINT32_C(0x27000000)}
    };
    static const struct {const char *name;uint32_t code;} unsigned12[]={
        {"andi",UINT32_C(0x03400000)},
        {"ori",UINT32_C(0x03800000)},
        {"xori",UINT32_C(0x03c00000)}
    };
    static const struct {
        const char *name;uint32_t code;unsigned bits;
    } shifts[]={
        {"slli.w",UINT32_C(0x00408000),5U},
        {"slli.d",UINT32_C(0x00410000),6U},
        {"srli.w",UINT32_C(0x00448000),5U},
        {"srli.d",UINT32_C(0x00450000),6U},
        {"srai.w",UINT32_C(0x00488000),5U},
        {"srai.d",UINT32_C(0x00490000),6U},
        {"rotri.w",UINT32_C(0x004c8000),5U},
        {"rotri.d",UINT32_C(0x004d0000),6U}
    };
    static const struct {const char *name;uint32_t code;} upper20[]={
        {"lu12i.w",UINT32_C(0x14000000)},
        {"lu32i.d",UINT32_C(0x16000000)},
        {"pcaddi",UINT32_C(0x18000000)},
        {"pcalau12i",UINT32_C(0x1a000000)},
        {"pcaddu12i",UINT32_C(0x1c000000)},
        {"pcaddu18i",UINT32_C(0x1e000000)}
    };
    /* GNU binutils: r0:5,r5:5 fixed unary integer transforms. */
    static const struct {const char *name;uint32_t code;} unary[]={
        {"clo.w",UINT32_C(0x00001000)},{"clz.w",UINT32_C(0x00001400)},
        {"cto.w",UINT32_C(0x00001800)},{"ctz.w",UINT32_C(0x00001c00)},
        {"clo.d",UINT32_C(0x00002000)},{"clz.d",UINT32_C(0x00002400)},
        {"cto.d",UINT32_C(0x00002800)},{"ctz.d",UINT32_C(0x00002c00)},
        {"revb.2h",UINT32_C(0x00003000)},
        {"revb.4h",UINT32_C(0x00003400)},
        {"revb.2w",UINT32_C(0x00003800)},
        {"revb.d",UINT32_C(0x00003c00)},
        {"revh.2w",UINT32_C(0x00004000)},
        {"revh.d",UINT32_C(0x00004400)},
        {"bitrev.4b",UINT32_C(0x00004800)},
        {"bitrev.8b",UINT32_C(0x00004c00)},
        {"bitrev.w",UINT32_C(0x00005000)},
        {"bitrev.d",UINT32_C(0x00005400)},
        {"ext.w.h",UINT32_C(0x00005800)},
        {"ext.w.b",UINT32_C(0x00005c00)},
        {"rdtimel.w",UINT32_C(0x00006000)},
        {"rdtimeh.w",UINT32_C(0x00006400)},
        {"rdtime.d",UINT32_C(0x00006800)},
        {"cpucfg",UINT32_C(0x00006c00)},
        {"iocsrrd.b",UINT32_C(0x06480000)},
        {"iocsrrd.h",UINT32_C(0x06480400)},
        {"iocsrrd.w",UINT32_C(0x06480800)},
        {"iocsrrd.d",UINT32_C(0x06480c00)},
        {"iocsrwr.b",UINT32_C(0x06481000)},
        {"iocsrwr.h",UINT32_C(0x06481400)},
        {"iocsrwr.w",UINT32_C(0x06481800)},
        {"iocsrwr.d",UINT32_C(0x06481c00)}
    };
    /* GNU atomic opcodes r0:5,r10:5,r5:5: the second assembly
     * operand occupies rk/bit10; the third is the memory base rj/bit5.
     * Never mix the register field order into ordinary gpr3. */
    static const struct {const char *name;uint32_t code;} atomics[]={
        {"amswap.w",UINT32_C(0x38600000)},
        {"amswap.d",UINT32_C(0x38608000)},
        {"amadd.w",UINT32_C(0x38610000)},
        {"amadd.d",UINT32_C(0x38618000)},
        {"amand.w",UINT32_C(0x38620000)},
        {"amand.d",UINT32_C(0x38628000)},
        {"amor.w",UINT32_C(0x38630000)},
        {"amor.d",UINT32_C(0x38638000)},
        {"amxor.w",UINT32_C(0x38640000)},
        {"amxor.d",UINT32_C(0x38648000)},
        {"ammax.w",UINT32_C(0x38650000)},
        {"ammax.d",UINT32_C(0x38658000)},
        {"ammin.w",UINT32_C(0x38660000)},
        {"ammin.d",UINT32_C(0x38668000)},
        {"ammax.wu",UINT32_C(0x38670000)},
        {"ammax.du",UINT32_C(0x38678000)},
        {"ammin.wu",UINT32_C(0x38680000)},
        {"ammin.du",UINT32_C(0x38688000)},
        {"amswap_db.w",UINT32_C(0x38690000)},
        {"amswap_db.d",UINT32_C(0x38698000)},
        {"amadd_db.w",UINT32_C(0x386a0000)},
        {"amadd_db.d",UINT32_C(0x386a8000)},
        {"amand_db.w",UINT32_C(0x386b0000)},
        {"amand_db.d",UINT32_C(0x386b8000)},
        {"amor_db.w",UINT32_C(0x386c0000)},
        {"amor_db.d",UINT32_C(0x386c8000)},
        {"amxor_db.w",UINT32_C(0x386d0000)},
        {"amxor_db.d",UINT32_C(0x386d8000)}
    };
    MiniAsMCInst *m=calloc(1U,sizeof(*m)+4U*sizeof(MiniAsMCOperand));
    char t[4][192],*end;unsigned n,i,rd,rj,rk;
    long val;
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
    if(n==2U && la_special_reg(t[0],"$f",31U,&rd) &&
       la_reg(t[1],&rj) &&
       (!strcmp(s->op,"movgr2fr.d") || !strcmp(s->op,"movgr2fr.w"))) {
        m->count=2U;
        m->encoding=(!strcmp(s->op,"movgr2fr.d") ?
                    UINT32_C(0x0114a800):UINT32_C(0x0114a400)) |
                    (rj<<5U)|rd;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rj;
        goto done;
    }
    if(n==2U && !strcmp(s->op,"movfcsr2gr") &&
       la_reg(t[0],&rd) &&
       la_special_reg(t[1],"$fcsr",3U,&rj)) {
        m->count=2U;
        m->encoding=UINT32_C(0x0114c800)|(rj<<5U)|rd;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rj;
        goto done;
    }
    if(n==2U && !strcmp(s->op,"movgr2fcsr") &&
       la_special_reg(t[0],"$fcsr",3U,&rd) && la_reg(t[1],&rj)) {
        m->count=2U;m->encoding=UINT32_C(0x0114c000)|(rj<<5U)|rd;
        goto done;
    }
    if(n==2U && !strcmp(s->op,"movfr2gr.d") &&
       la_reg(t[0],&rd) &&
       la_special_reg(t[1],"$f",31U,&rj)) {
        m->count=2U;m->encoding=UINT32_C(0x0114b800)|(rj<<5U)|rd;
        goto done;
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
        uint64_t bits;uint32_t words[4];
        if(minias_eval_u64_expression(t[1],&bits)) {
            m->opcode=LA_MC_LI64_SEQUENCE;m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=bits;
            (void)la_li_d_words(bits,rd,words);
            goto done;
        }
    }
    /* GNU LSX/LASX vector loads/stores use a 12-bit signed byte offset,
     * base GPR rj and independent vector register vd in r0's slot. The
     * assembler may encode these GNU inline-asm extensions even while the
     * compiler's active emission subset remains scalar; ISA capabilities
     * are distinct from generated-code instruction selection. */
    if(n==3U && la_reg(t[1],&rj)) {
        static const struct {const char *name,*prefix;uint32_t base;} vec[]={
            {"vld","$vr",UINT32_C(0x2c000000)},
            {"vst","$vr",UINT32_C(0x2c400000)},
            {"xvld","$xr",UINT32_C(0x2c800000)},
            {"xvst","$xr",UINT32_C(0x2cc00000)}
        };
        int64_t off;
        if(minias_eval_i64_expression(t[2],&off) &&
           off>=-2048 && off<=2047) {
            for(i=0U;i<sizeof(vec)/sizeof(vec[0]);++i)
                if(!strcmp(s->op,vec[i].name) &&
                   la_special_reg(t[0],vec[i].prefix,31U,&rd)) {
                    m->count=3U;
                    m->encoding=vec[i].base |
                        (((uint32_t)off&UINT32_C(0xfff))<<10U) |
                        (rj<<5U)|rd;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=rd;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=rj;
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=(uint64_t)off;
                    goto done;
                }
        }
    }
    /* GNU privileged INVTLB u5, rj, rk, with r0/i0 as operation code.
     * Existing Common MCInst emission and architecture ELF ownership. */

    if(n==3U && la_special_reg(t[0],"$vr",31U,&rd) &&
       la_special_reg(t[1],"$vr",31U,&rj)) {
        static const struct {const char *name;uint32_t op;} logical[]={
            {"vand.v",UINT32_C(0x71260000)},
            {"vor.v",UINT32_C(0x71268000)},
            {"vxor.v",UINT32_C(0x71270000)},
            {"vnor.v",UINT32_C(0x71278000)},
            {"vandn.v",UINT32_C(0x71280000)},
            {"vorn.v",UINT32_C(0x71288000)}
        };
        static const struct {const char *name;uint32_t op;} masks[]={
            {"vandi.b",UINT32_C(0x73d00000)},
            {"vori.b",UINT32_C(0x73d40000)},
            {"vxori.b",UINT32_C(0x73d80000)},
            {"vnori.b",UINT32_C(0x73dc0000)}
        };
        uint64_t imm;
        if(la_special_reg(t[2],"$vr",31U,&rk))
            for(i=0U;i<sizeof(logical)/sizeof(logical[0]);++i)
                if(!strcmp(s->op,logical[i].name)){
                    m->count=3U;
                    m->encoding=logical[i].op|(rk<<10U)|(rj<<5U)|rd;
                    goto done;
                }
        if(minias_eval_u64_expression(t[2],&imm) && imm<=255U)
            for(i=0U;i<sizeof(masks)/sizeof(masks[0]);++i)
                if(!strcmp(s->op,masks[i].name)){
                    m->count=3U;
                    m->encoding=masks[i].op|((uint32_t)imm<<10U)|
                        (rj<<5U)|rd;
                    goto done;
                }
    }
    if(n==3U && la_special_reg(t[0],"$f",31U,&rd) &&
       la_reg(t[1],&rj)) {
        static const struct {const char *name;uint32_t op;} fp[]={
            {"fld.s",UINT32_C(0x2b000000)},
            {"fst.s",UINT32_C(0x2b400000)},
            {"fld.d",UINT32_C(0x2b800000)},
            {"fst.d",UINT32_C(0x2bc00000)}
        };
        int64_t offset;
        if(minias_eval_i64_expression(t[2],&offset) &&
           offset>=-2048 && offset<=2047)
            for(i=0U;i<sizeof(fp)/sizeof(fp[0]);++i)
                if(!strcmp(s->op,fp[i].name)){
                    m->count=3U;
                    m->encoding=fp[i].op|
                        (((uint32_t)offset&UINT32_C(0xfff))<<10U)|
                        (rj<<5U)|rd;
                    goto done;
                }
    }
    /* LSX and LASX share the r0/r5/r10 register encoding but have
     * distinct architecture opcodes and register classes. The assembler
     * accepts ISA extensions independently of MiniC's emission subset. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj) &&
       la_special_reg(t[2],"$xr",31U,&rk)) {
        static const struct {const char *name;uint32_t opcode;} lasx[]={
            {"xvand.v",UINT32_C(0x75260000)},
            {"xvor.v",UINT32_C(0x75268000)},
            {"xvxor.v",UINT32_C(0x75270000)},
            {"xvnor.v",UINT32_C(0x75278000)},
            {"xvandn.v",UINT32_C(0x75280000)},
            {"xvorn.v",UINT32_C(0x75288000)}
        };
        for(i=0U;i<sizeof(lasx)/sizeof(lasx[0]);++i)
            if(!strcmp(s->op,lasx[i].name)) {
                m->count=3U;
                m->encoding=lasx[i].opcode|(rk<<10U)|(rj<<5U)|rd;
                goto done;
            }
    }
    /* Signed LSX compare-immediate u/s5 family, not a GPR comparison. */
    if(n==3U && la_special_reg(t[0],"$vr",31U,&rd) &&
       la_special_reg(t[1],"$vr",31U,&rj)) {
        static const struct {const char *name;uint32_t opcode;} cmp[]={
            {"vslti.b",UINT32_C(0x72860000)},
            {"vslti.h",UINT32_C(0x72868000)},
            {"vslti.w",UINT32_C(0x72870000)},
            {"vslti.d",UINT32_C(0x72878000)}
        };
        static const struct {const char *name;uint32_t opcode;
                             unsigned max_shift;} vector_shifts[]={
            {"vslli.b",UINT32_C(0x732c2000),7U},
            {"vslli.h",UINT32_C(0x732c4000),15U},
            {"vslli.w",UINT32_C(0x732c8000),31U},
            {"vslli.d",UINT32_C(0x732d0000),63U},
            {"vsrli.b",UINT32_C(0x73302000),7U},
            {"vsrli.h",UINT32_C(0x73304000),15U},
            {"vsrli.w",UINT32_C(0x73308000),31U},
            {"vsrli.d",UINT32_C(0x73310000),63U},
            {"vsrai.b",UINT32_C(0x73342000),7U},
            {"vsrai.h",UINT32_C(0x73344000),15U},
            {"vsrai.w",UINT32_C(0x73348000),31U},
            {"vsrai.d",UINT32_C(0x73350000),63U}
        };
        int64_t imm;
        if(minias_eval_i64_expression(t[2],&imm)) {
            if(imm>=-16 && imm<=15)
                for(i=0U;i<sizeof(cmp)/sizeof(cmp[0]);++i)
                    if(!strcmp(s->op,cmp[i].name)) {
                        m->count=3U;
                        m->encoding=cmp[i].opcode|
                            (((uint32_t)imm&31U)<<10U)|(rj<<5U)|rd;
                        goto done;
                    }
            if(imm>=0)
                for(i=0U;i<sizeof(vector_shifts)/sizeof(vector_shifts[0]);++i)
                    if(!strcmp(s->op,vector_shifts[i].name) &&
                       (uint64_t)imm<=vector_shifts[i].max_shift) {
                        m->count=3U;
                        m->encoding=vector_shifts[i].opcode|
                            ((uint32_t)imm<<10U)|(rj<<5U)|rd;
                        goto done;
                    }
        }
    }
    /* LASX signed vector immediate compare: same typed si5/register
     * field model as LSX but distinct ISA opcodes and XR register class. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        static const struct {const char *name;uint32_t base;} cv[]={
            {"xvslti.b",UINT32_C(0x76860000)},
            {"xvslti.h",UINT32_C(0x76868000)},
            {"xvslti.w",UINT32_C(0x76870000)},
            {"xvslti.d",UINT32_C(0x76878000)}
        };
        int64_t imm;
        if(minias_eval_i64_expression(t[2],&imm) &&
           imm>=-16 && imm<=15)
            for(i=0U;i<sizeof(cv)/sizeof(cv[0]);++i)
                if(!strcmp(s->op,cv[i].name)) {
                    m->count=3U;
                    m->encoding=cv[i].base |
                        (((uint32_t)imm&31U)<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* LASX vector shifts share the LSX typed unsigned count and register
     * bitfield layout but retain separate ISA encoding and XR register class. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        static const struct {const char *name;uint32_t code;unsigned max;} v[]={
            {"xvslli.b",UINT32_C(0x772c2000),7U},
            {"xvslli.h",UINT32_C(0x772c4000),15U},
            {"xvslli.w",UINT32_C(0x772c8000),31U},
            {"xvslli.d",UINT32_C(0x772d0000),63U},
            {"xvsrli.b",UINT32_C(0x77302000),7U},
            {"xvsrli.h",UINT32_C(0x77304000),15U},
            {"xvsrli.w",UINT32_C(0x77308000),31U},
            {"xvsrli.d",UINT32_C(0x77310000),63U}
        };
        uint64_t amount;
        if(minias_eval_u64_expression(t[2],&amount))
            for(i=0U;i<sizeof(v)/sizeof(v[0]);++i)
                if(!strcmp(s->op,v[i].name) && amount<=v[i].max) {
                    m->count=3U;
                    m->encoding=v[i].code|
                        ((uint32_t)amount<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* LSX lane extract to GPR: r0/v5/u10 immediate, widths set index cap.
     * The vector register is not a GPR and GNU requires index in range. */
    if(n==3U && la_reg(t[0],&rd) &&
       la_special_reg(t[1],"$vr",31U,&rj)) {
        static const struct {const char *name;uint32_t base;unsigned max;} pick[]={
            {"vpickve2gr.b",UINT32_C(0x72ef8000),15U},
            {"vpickve2gr.h",UINT32_C(0x72efc000),7U},
            {"vpickve2gr.w",UINT32_C(0x72efe000),3U},
            {"vpickve2gr.d",UINT32_C(0x72eff000),1U},
            {"vpickve2gr.bu",UINT32_C(0x72f38000),15U},
            {"vpickve2gr.hu",UINT32_C(0x72f3c000),7U},
            {"vpickve2gr.wu",UINT32_C(0x72f3e000),3U},
            {"vpickve2gr.du",UINT32_C(0x72f3f000),1U}
        };
        uint64_t lane;
        if(minias_eval_u64_expression(t[2],&lane))
            for(i=0U;i<sizeof(pick)/sizeof(pick[0]);++i)
                if(!strcmp(s->op,pick[i].name) &&
                   lane<=pick[i].max) {
                    m->count=3U;
                    m->encoding=pick[i].base |
                        ((uint32_t)lane<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* LASX XR-to-GPR lane extraction is the 256-bit variant of LSX
     * VPICKVE2GR, retaining the same typed lane format with 2x lane count.
     * No Common ISA knowledge or private fixups required. */
    if(n==3U && la_reg(t[0],&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        /* From upstream GNU binutils loongarch-opc.c. LASX XVPICK
         * exposes W/D lanes only, not the LSX B/H variants; do not infer
         * opcodes or pseudo-mnemonics from unrelated SIMD encodings. */
        static const struct {const char *name;uint32_t opcode;unsigned max;} pick[]={
            {"xvpickve2gr.w", UINT32_C(0x76efc000),7U},
            {"xvpickve2gr.d", UINT32_C(0x76efe000),3U},
            {"xvpickve2gr.wu",UINT32_C(0x76f3c000),7U},
            {"xvpickve2gr.du",UINT32_C(0x76f3e000),3U}
        };
        uint64_t lane;
        if(minias_eval_u64_expression(t[2],&lane))
            for(i=0U;i<sizeof(pick)/sizeof(pick[0]);++i)
                if(!strcmp(s->op,pick[i].name) &&
                   lane<=pick[i].max) {
                    m->count=3U;
                    m->encoding=pick[i].opcode|
                        ((uint32_t)lane<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* LASX XVPERMI W/D/Q all share the existing typed XR/XR/u8
     * fields, with GNU binutils ISA opcode ownership kept in Target. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        static const struct {const char *op;uint32_t opcode;} perms[]={
            {"xvpermi.w",UINT32_C(0x77e40000)},
            {"xvpermi.d",UINT32_C(0x77e80000)},
            {"xvpermi.q",UINT32_C(0x77ec0000)}
        };
        uint64_t bits;
        if(minias_eval_u64_expression(t[2],&bits) && bits<=255U)
            for(i=0U;i<sizeof(perms)/sizeof(perms[0]);++i)
                if(!strcmp(s->op,perms[i].op)) {
                    m->count=3U;
                    m->encoding=perms[i].opcode|
                        ((uint32_t)bits<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* GNU LoongArch LASX replicate lane-zero group (XR0/XR5) and LSX
     * GPR-to-lane injection (V0/R5/u10). No second vector encoder: both
     * flow through existing target-owned 32-bit typed MCInst words. */
    if(n==2U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        static const struct {const char *op;uint32_t code;} repl[]={
            {"xvreplve0.b",UINT32_C(0x77070000)},
            {"xvreplve0.h",UINT32_C(0x77078000)},
            {"xvreplve0.w",UINT32_C(0x7707c000)},
            {"xvreplve0.d",UINT32_C(0x7707e000)},
            {"xvreplve0.q",UINT32_C(0x7707f000)}
        };
        for(i=0U;i<sizeof(repl)/sizeof(repl[0]);++i)
            if(!strcmp(s->op,repl[i].op)) {
                m->count=2U;
                m->encoding=repl[i].code|(rj<<5U)|rd;
                goto done;
            }
    }
    if(n==3U && la_special_reg(t[0],"$vr",31U,&rd) &&
       la_reg(t[1],&rj)) {
        static const struct {const char *op;uint32_t code;unsigned max;} lanes[]={
            {"vinsgr2vr.b",UINT32_C(0x72eb8000),15U},
            {"vinsgr2vr.h",UINT32_C(0x72ebc000),7U},
            {"vinsgr2vr.w",UINT32_C(0x72ebe000),3U},
            {"vinsgr2vr.d",UINT32_C(0x72ebf000),1U}
        };
        uint64_t lane;
        if(minias_eval_u64_expression(t[2],&lane))
            for(i=0U;i<sizeof(lanes)/sizeof(lanes[0]);++i)
                if(!strcmp(s->op,lanes[i].op) &&
                   lane<=lanes[i].max) {
                    m->count=3U;
                    m->encoding=lanes[i].code|
                        ((uint32_t)lane<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* GNU LASX XR lane injection: vector x0, scalar r5, lane u10.
     * Distinct from LSX VR injections, but same existing typed raw
     * MCInst and common ELF encoder. GNU loongarch-opc.c is authoritative
     * for both .w/.d opcode words and legal lane ranges. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_reg(t[1],&rj)) {
        static const struct {const char *name;uint32_t base;unsigned max;} inject[]={
            {"xvinsgr2vr.w",UINT32_C(0x76ebc000),7U},
            {"xvinsgr2vr.d",UINT32_C(0x76ebe000),3U}
        };
        uint64_t lane;
        if(minias_eval_u64_expression(t[2],&lane))
            for(i=0U;i<sizeof(inject)/sizeof(inject[0]);++i)
                if(!strcmp(s->op,inject[i].name) && lane<=inject[i].max) {
                    m->count=3U;
                    m->encoding=inject[i].base|
                        ((uint32_t)lane<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    /* GNU privileged LDDIR page-table walk uses target u10:8.
     * Malformed levels are rejected before MCInst emission. */
    if(n==3U && !strcmp(s->op,"lddir") &&
       la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        uint64_t level;
        if(minias_eval_u64_expression(t[2],&level) && level<=255U) {
            m->count=3U;
            m->encoding=UINT32_C(0x06400000)|
                ((uint32_t)level<<10U)|(rj<<5U)|rd;
            goto done;
        }
    }
    /* GNU privileged software-walk LDPTE (r5, u10:8) is the paired
     * page-table instruction to LDDIR. Both use target-owned exact
     * instruction encoding and share the existing raw MC emitter. */
    if(n==2U && !strcmp(s->op,"ldpte") && la_reg(t[0],&rj)) {
        uint64_t level;
        if(minias_eval_u64_expression(t[1],&level) && level<=255U) {
            m->count=2U;
            m->encoding=UINT32_C(0x06440000)|
                        ((uint32_t)level<<10U)|(rj<<5U);
            goto done;
        }
    }
    /* FCC-to-GPR: condition register is u3 in r5, GPR is r0. */
    if(n==2U && !strcmp(s->op,"movcf2gr") &&
       la_reg(t[0],&rd) &&
       la_special_reg(t[1],"$fcc",7U,&rj)) {
        m->count=2U;
        m->encoding=UINT32_C(0x0114dc00)|(rj<<5U)|rd;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rj;
        goto done;
    }
    if(n==2U && !strcmp(s->op,"movgr2cf") &&
       la_special_reg(t[0],"$fcc",7U,&rd) &&
       la_reg(t[1],&rj)) {
        m->count=2U;
        m->encoding=UINT32_C(0x0114d800)|(rj<<5U)|rd;
        goto done;
    }
    /* LASX unsigned 8-bit immediate logical family, X-vector separate. */
    if(n==3U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj)) {
        static const struct {const char *name;uint32_t opcode;} masks[]={
            {"xvandi.b",UINT32_C(0x77d00000)},
            {"xvori.b",UINT32_C(0x77d40000)},
            {"xvxori.b",UINT32_C(0x77d80000)},
            {"xvnori.b",UINT32_C(0x77dc0000)}
        };
        uint64_t imm;
        if(minias_eval_u64_expression(t[2],&imm) && imm<=255U)
            for(i=0U;i<sizeof(masks)/sizeof(masks[0]);++i)
                if(!strcmp(s->op,masks[i].name)) {
                    m->count=3U;
                    m->encoding=masks[i].opcode |
                        ((uint32_t)imm<<10U)|(rj<<5U)|rd;
                    goto done;
                }
    }
    if(n==3U && !strcmp(s->op,"invtlb") &&
       la_reg(t[1],&rj) && la_reg(t[2],&rk)) {
        uint64_t operation;
        if(minias_eval_u64_expression(t[0],&operation) && operation<=31U) {
            m->count=3U;
            m->encoding=UINT32_C(0x06498000) |
                        (rk<<10U)|(rj<<5U)|(uint32_t)operation;
            m->operands[0].kind=MINIAS_MC_IMMEDIATE;
            m->operands[0].immediate=operation;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_REGISTER;
            m->operands[2].reg=rk;
            goto done;
        }
    }
    /* CACOP is a u5 operation plus GPR base and signed si12 offset.
     * The system opcode is described entirely by target-specific bits,
     * leaving ELF/data/fragment ownership in Common. */
    if(n==3U && !strcmp(s->op,"cacop") && la_reg(t[1],&rj)) {
        uint64_t operation;int64_t offset;
        if(minias_eval_u64_expression(t[0],&operation) &&
           operation<=31U &&
           minias_eval_i64_expression(t[2],&offset) &&
           offset>=-2048 && offset<=2047) {
            m->count=3U;
            m->encoding=UINT32_C(0x06000000)|
                (((uint32_t)offset&UINT32_C(0xfff))<<10U)|
                (rj<<5U)|(uint32_t)operation;
            m->operands[0].kind=MINIAS_MC_IMMEDIATE;
            m->operands[0].immediate=operation;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=(uint64_t)offset;
            goto done;
        }
    }
    /* All four are GNU LA64 unsigned-15-bit immediate instructions. */
    if(n==1U) {
        static const struct {const char *op;uint32_t word;} direct[]={
            {"break",UINT32_C(0x002a0000)},
            {"syscall",UINT32_C(0x002b0000)},
            {"dbar",UINT32_C(0x38720000)},
            {"ibar",UINT32_C(0x38728000)},
            {"idle",UINT32_C(0x06488000)}
        };
        uint64_t v;
        if(minias_eval_u64_expression(t[0],&v) && v<=32767U) {
            for(i=0U;i<sizeof(direct)/sizeof(direct[0]);++i)
                if(strcmp(s->op,direct[i].op)==0) {
                    m->count=1U;m->encoding=direct[i].word|(uint32_t)v;
                    m->operands[0].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[0].immediate=v;
                    goto done;
                }
        }
    }
    /* Genuine four-vector operand format v0/v5/v10/v15; not a
     * generic three-register shortcut. GNU VSHUF.B and vector BITSEL. */
    if(n==4U && la_special_reg(t[0],"$vr",31U,&rd) &&
       la_special_reg(t[1],"$vr",31U,&rj) &&
       la_special_reg(t[2],"$vr",31U,&rk)) {
        unsigned fourth;
        if(la_special_reg(t[3],"$vr",31U,&fourth) &&
           (!strcmp(s->op,"vshuf.b") || !strcmp(s->op,"vbitsel.v"))) {
            m->count=4U;
            m->encoding=(!strcmp(s->op,"vshuf.b") ?
                        UINT32_C(0x0d500000):UINT32_C(0x0d100000)) |
                         (fourth<<15U)|(rk<<10U)|(rj<<5U)|rd;
            goto done;
        }
    }
    if(n==4U && la_special_reg(t[0],"$xr",31U,&rd) &&
       la_special_reg(t[1],"$xr",31U,&rj) &&
       la_special_reg(t[2],"$xr",31U,&rk)) {
        unsigned fourth;
        if(la_special_reg(t[3],"$xr",31U,&fourth) &&
           (!strcmp(s->op,"xvshuf.b") || !strcmp(s->op,"xvbitsel.v"))) {
            m->count=4U;
            m->encoding=(!strcmp(s->op,"xvshuf.b") ?
                        UINT32_C(0x0d600000):UINT32_C(0x0d200000)) |
                         (fourth<<15U)|(rk<<10U)|(rj<<5U)|rd;
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
        long long delta;
        errno=0;delta=strtoll(t[2],&end,0);
        if(!errno && end!=t[2] && !*end &&
           delta>=-131072LL && delta<=131068LL && delta%4LL==0LL) {
            m->count=3U;
            m->operands[0].kind=MINIAS_MC_REGISTER;m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_REGISTER;m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=(uint64_t)delta;
            m->encoding=UINT32_C(0x4c000000) |
                (((uint32_t)(delta/4LL)&UINT32_C(0xffff))<<10U) |
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
        int64_t upper;
        if (minias_eval_i64_expression(t[1],&upper) &&
            upper>=-524288 && upper<=524287) {
            for(i=0U;i<sizeof(upper20)/sizeof(upper20[0]);++i)
                if(!strcmp(s->op,upper20[i].name)) {
                    m->count=2U;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=rd;
                    m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[1].immediate=(uint64_t)(int64_t)upper;
                    m->encoding=upper20[i].code |
                        ((((uint32_t)upper)&UINT32_C(0xfffff))<<5U) | rd;
                    goto done;
                }
        }
    }
    /* Privileged CSR opcodes, validated exact 14-bit immediate.
     * CSRWR and CSRXCHG are not regular GPR instruction families. */
    if(n==2U && la_reg(t[0],&rd) &&
       (!strcmp(s->op,"csrrd") || !strcmp(s->op,"csrwr"))) {
        uint64_t csr;
        if(minias_eval_u64_expression(t[1],&csr) && csr<=16383U) {
            m->count=2U;
            m->encoding=(!strcmp(s->op,"csrrd")?
                         UINT32_C(0x04000000):UINT32_C(0x04000020))|
                        ((uint32_t)csr<<10U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=csr;
            goto done;
        }
    }
    if(n==3U && !strcmp(s->op,"csrxchg") &&
       la_reg(t[0],&rd) && la_reg(t[1],&rj) && rj>=2U) {
        uint64_t csr;
        if(minias_eval_u64_expression(t[2],&csr) && csr<=16383U) {
            m->count=3U;
            m->encoding=UINT32_C(0x04000000)|
                ((uint32_t)csr<<10U)|(rj<<5U)|rd;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=rj;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=csr;
            goto done;
        }
    }
    if(n==2U && la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        for(i=0U;i<sizeof(unary)/sizeof(unary[0]);++i) {
            if(strcmp(s->op,unary[i].name)==0) {
                m->count=2U;
                m->encoding=unary[i].code|(rj<<5U)|rd;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=rj;
                goto done;
            }
        }
    }
    if(!strcmp(s->op,"move") && n==2U &&
        la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        m->count=2U;m->encoding=UINT32_C(0x00150000) |
            (rj<<5U)|rd;goto done;
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
    if((n!=3U && n!=4U) ||
       !la_reg(t[0],&rd) || !la_reg(t[1],&rj)) goto fail;
    if(n==4U) {
        uint64_t reserved;
        /* Real GCC LoongArch .S uses GAS's atomic pseudo fourth operand
         * u0:0. The fourth operand is mandatory zero, not an ISA field. */
        if(!minias_eval_u64_expression(t[3],&reserved) || reserved!=0U)
            goto fail;
    }
    if(la_reg(t[2],&rk)) {
        for(i=0U;i<sizeof(atomics)/sizeof(atomics[0]);++i)
            if(!strcmp(s->op,atomics[i].name)) {
                m->count=n;
                m->encoding=atomics[i].code|(rj<<10U)|(rk<<5U)|rd;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=rj;
                m->operands[2].kind=MINIAS_MC_REGISTER;
                m->operands[2].reg=rk;
                if(n==4U) {
                    m->operands[3].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[3].immediate=0U;
                }
                goto done;
            }
        if(n==4U)goto fail;
        for(i=0U;i<sizeof(gpr3)/sizeof(gpr3[0]);++i)
            if(!strcmp(s->op,gpr3[i].name)) {
                m->count=3U;m->encoding=gpr3[i].code |
                    (rk<<10U)|(rj<<5U)|rd;goto done;
            }
    } else {
        MiniAsSymbolExpr expr;
        if(n==4U)goto fail;
        if(la_reloc_expr(t[2],"%pc_lo12",&expr)) {
            for(i=0U;i<sizeof(imm12)/sizeof(imm12[0]);++i)
                if(strcmp(s->op,imm12[i].name)==0) {
                    if(!la_symbol_operand(as,m,2U,&expr))goto fail;
                    m->opcode=LA_MC_PCALA_LO12;
                    m->count=3U;
                    m->encoding=imm12[i].code|(rj<<5U)|rd;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=rd;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=rj;
                    goto done;
                }
        }
        {
        int64_t signed_value;
        if(minias_eval_i64_expression(t[2],&signed_value) &&
           signed_value>=LONG_MIN && signed_value<=LONG_MAX) {
            val=(long)signed_value;
            if (val>=-2048L && val<=2047L) {
                for(i=0U;i<sizeof(imm12)/sizeof(imm12[0]);++i)
                    if(!strcmp(s->op,imm12[i].name)) {
                        m->count=3U;m->encoding=imm12[i].code |
                            (((uint32_t)val & UINT32_C(0xfff))<<10U) |
                            (rj<<5U)|rd;goto done;
                    }
            }
            if (val>=0 && val<=4095L) {
                for(i=0U;i<sizeof(unsigned12)/sizeof(unsigned12[0]);++i)
                    if(!strcmp(s->op,unsigned12[i].name)) {
                        m->count=3U;
                        m->encoding=unsigned12[i].code |
                            ((uint32_t)val<<10U) | (rj<<5U) | rd;
                        goto done;
                    }
            }
            if (val>=0 && val<=63L) {
                for(i=0U;i<sizeof(shifts)/sizeof(shifts[0]);++i)
                    if(!strcmp(s->op,shifts[i].name) &&
                        (unsigned long)val < (1UL<<shifts[i].bits)) {
                        m->count=3U;
                        m->encoding=shifts[i].code |
                            ((uint32_t)val<<10U) | (rj<<5U) | rd;
                        goto done;
                    }
            }
            if (val>=-32768L && val<=32764L && val%4L==0L) {
                for(i=0U;i<sizeof(ptr14)/sizeof(ptr14[0]);++i)
                    if(!strcmp(s->op,ptr14[i].name)) {
                        m->count=3U;m->encoding=ptr14[i].code |
                            ((((uint32_t)(val/4L)) & UINT32_C(0x3fff))<<10U) |
                            (rj<<5U)|rd;goto done;
                    }
            }
        }
        }
    }
fail:
    minias_set_error(as,"unsupported-loongarch64-operands:%s:%s:line=%zu",
                     s->op,s->args,s->line);
    free(m);return false;
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
static uint32_t la_reloc(MiniAsFixupKind kind,unsigned width) {
    if(width!=4U && width!=8U)return 0U;
    switch(kind) {
        case MINIAS_FIXUP_ABS:
            return width==8U?2U:1U; /* R_LARCH_64 / R_LARCH_32 */
        case MINIAS_FIXUP_PREL32:
            return width==4U?99U:0U; /* R_LARCH_32_PCREL */
        case MINIAS_FIXUP_ADD:
            return width==8U?51U:50U; /* ADD64 / ADD32 */
        case MINIAS_FIXUP_SUB:
            return width==8U?56U:55U; /* SUB64 / SUB32 */
        case MINIAS_FIXUP_LARCH_PCALA_HI20:
            return width==4U?71U:0U;
        case MINIAS_FIXUP_LARCH_PCALA_LO12:
            return width==4U?72U:0U;
        case MINIAS_FIXUP_LARCH_B16:
            return width==4U?64U:0U;
        case MINIAS_FIXUP_LARCH_B21:
            return width==4U?65U:0U;
        case MINIAS_FIXUP_LARCH_B26:
            return width==4U?66U:0U;
        default: return 0U;
    }
}
static const MiniAsTargetOps ops={
    .name="loongarch64",.comment_prefix='#',.elf_machine=258U,
    .resolve_profile=la_profile,.parse_mcinst=la_parse,
    .measure_mcinst=la_measure,.encode=la_encode,
    .append_code_alignment=la_align,.fixup_relocation_type=la_reloc,
    .validate_output=la_validate,
    .cfi_implicit_eh_frame=true,
    .cfi_code_align=4U,
    .cfi_data_align=-8,
    .cfi_return_reg=1U,
    .cfi_stack_reg=3U,
    .cfi_address_size=8U
};
const MiniAsTargetOps *minias_target_loongarch64(void){return &ops;}
