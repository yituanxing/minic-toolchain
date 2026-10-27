#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
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
static bool la_split(const char *args,char t[3][192],unsigned *n) {
    const char *p=args,*start=args;unsigned c=0;
    for(;;++p) {
        if(*p!=',' && *p!='\0')continue;
        size_t length=(size_t)(p-start);
        while(length && isspace((unsigned char)*start)){++start;--length;}
        while(length && isspace((unsigned char)start[length-1U]))--length;
        if(!length || length>=192 || c>=3)return false;
        memcpy(t[c],start,length);t[c++][length]='\0';
        if(!*p)break;
        start=p+1;
    }
    *n=c;return true;
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
                                  const char *op, char t[3][192],
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
    MiniAsSymbolExpr expr;
    for(i=0;i<sizeof(branches)/sizeof(branches[0]);++i) {
        const struct BranchKind *b=&branches[i];
        if(strcmp(op,b->name)!=0 || count!=b->operands)continue;
        if (!minias_parse_symbol_addend(t[count-1U],&expr) ||
            (expr.addend&3)!=0)
            return false;
        rj=0U;rd=0U;
        if(count>=2U && !la_reg(t[0],&rj))return false;
        if(count==3U && !la_reg(t[1],&rd))return false;
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
        {"mul.w",UINT32_C(0x001c0000)},
        {"mul.d",UINT32_C(0x001d8000)}
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
    MiniAsMCInst *m=calloc(1U,sizeof(*m)+3U*sizeof(MiniAsMCOperand));
    char t[3][192],*end;unsigned n,i,rd,rj,rk;
    long val;
    if(!m){minias_set_error(as,"out-of-memory:loongarch64-mcinst");return false;}
    if(!*s->args) {
        if(!strcmp(s->op,"nop")){m->encoding=UINT32_C(0x03400000);goto done;}
        if(!strcmp(s->op,"ret")){m->encoding=UINT32_C(0x4c000020);goto done;}
    }
    if(!la_split(s->args,t,&n))goto fail;
    if(la_branch_instruction(as,m,s->op,t,n)) goto done;
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
        long upper;
        errno=0;upper=strtol(t[1],&end,0);
        if (!errno && end!=t[1] && !*end &&
            upper>=-524288L && upper<=524287L) {
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
    if(!strcmp(s->op,"move") && n==2U &&
        la_reg(t[0],&rd) && la_reg(t[1],&rj)) {
        m->count=2U;m->encoding=UINT32_C(0x00150000) |
            (rj<<5U)|rd;goto done;
    }
    if(n!=3U || !la_reg(t[0],&rd) || !la_reg(t[1],&rj))goto fail;
    if(la_reg(t[2],&rk)) {
        for(i=0U;i<sizeof(gpr3)/sizeof(gpr3[0]);++i)
            if(!strcmp(s->op,gpr3[i].name)) {
                m->count=3U;m->encoding=gpr3[i].code |
                    (rk<<10U)|(rj<<5U)|rd;goto done;
            }
    } else {
        MiniAsSymbolExpr expr;
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
        errno=0;val=strtol(t[2],&end,0);
        if(!errno && end!=t[2] && !*end) {
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
fail:
    minias_set_error(as,"unsupported-loongarch64-operands:%s:%s:line=%zu",
                     s->op,s->args,s->line);
    free(m);return false;
done:
    s->mcinst=m;return true;
}
static bool la_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    if(!s->mcinst){minias_set_error(as,"missing-la-mcinst");return false;}
    *size=4U;return true;
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
    if(!minias_section_append(as,s->section,bytes,sizeof(bytes)))
        return false;
    if(kind==MINIAS_FIXUP_NONE)return true;
    expr=m->operands[slot].symbol;
    return minias_add_fixup(as,s->section,s->offset,kind,4U,
                            expr->name,expr->addend);
}
static bool la_align(MiniAs *as,int section,size_t size) {
    static const unsigned char nop[4]={0x00,0x00,0x40,0x03};
    if(size%4U){minias_set_error(as,"unalignable-loongarch-code-gap");return false;}
    while(size){if(!minias_section_append(as,section,nop,4U))return false;size-=4U;}
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
    .validate_output=la_validate
};
const MiniAsTargetOps *minias_target_loongarch64(void){return &ops;}
