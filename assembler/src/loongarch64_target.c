#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LoongArch64 LA64 basic integer target. GNU binutils tables own the reference
 * encodings; extensions and fixup relocation families are not guessed. */
static bool la_reg(const char *s,unsigned *r) {
    static const struct {const char *s;unsigned r;} aliases[]={
        {"zero",0U},{"ra",1U},{"tp",2U},{"sp",3U},
        {"a0",4U},{"a1",5U},{"a2",6U},{"a3",7U},
        {"a4",8U},{"a5",9U},{"a6",10U},{"a7",11U},
        {"t0",12U},{"t1",13U},{"t2",14U},{"t3",15U},
        {"t4",16U},{"t5",17U},{"t6",18U},{"t7",19U},
        {"t8",20U},{"u0",21U},{"fp",22U},{"s9",31U}
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
static bool la_split(const char *args,char t[3][48],unsigned *n) {
    const char *p=args,*start=args;unsigned c=0;
    for(;;++p) {
        if(*p!=',' && *p!='\0')continue;
        size_t length=(size_t)(p-start);
        while(length && isspace((unsigned char)*start)){++start;--length;}
        while(length && isspace((unsigned char)start[length-1U]))--length;
        if(!length || length>=48 || c>=3)return false;
        memcpy(t[c],start,length);t[c++][length]='\0';
        if(!*p)break;
        start=p+1;
    }
    *n=c;return true;
}

static bool la_parse(MiniAs *as,MiniAsStmt *s) {
    static const struct {const char *name;uint32_t code;} gpr3[]={
        {"add.d",UINT32_C(0x00108000)},{"sub.d",UINT32_C(0x00118000)},
        {"add.w",UINT32_C(0x00100000)},{"sub.w",UINT32_C(0x00110000)},
        {"and",UINT32_C(0x00148000)},{"or",UINT32_C(0x00150000)},
        {"xor",UINT32_C(0x00158000)},{"slt",UINT32_C(0x00120000)},
        {"sltu",UINT32_C(0x00128000)}
    };
    static const struct {const char *name;uint32_t code;} imm12[]={
        {"addi.d",UINT32_C(0x02c00000)},
        {"addi.w",UINT32_C(0x02800000)},
        {"ld.d",UINT32_C(0x28c00000)},
        {"st.d",UINT32_C(0x29c00000)},
        {"ld.w",UINT32_C(0x28800000)},
        {"st.w",UINT32_C(0x29800000)}
    };
    MiniAsMCInst *m=calloc(1U,sizeof(*m)+3U*sizeof(MiniAsMCOperand));
    char t[3][48],*end;unsigned n,i,rd,rj,rk;
    long val;
    if(!m){minias_set_error(as,"out-of-memory:loongarch64-mcinst");return false;}
    if(!*s->args) {
        if(!strcmp(s->op,"nop")){m->encoding=UINT32_C(0x03400000);goto done;}
        if(!strcmp(s->op,"ret")){m->encoding=UINT32_C(0x4c000020);goto done;}
    }
    if(!la_split(s->args,t,&n))goto fail;
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
        errno=0;val=strtol(t[2],&end,0);
        if(!errno && end!=t[2] && !*end &&
            val>=-2048L && val<=2047L) {
            for(i=0U;i<sizeof(imm12)/sizeof(imm12[0]);++i)
                if(!strcmp(s->op,imm12[i].name)) {
                    m->count=3U;m->encoding=imm12[i].code |
                        (((uint32_t)val & UINT32_C(0xfff))<<10U) |
                        (rj<<5U)|rd;goto done;
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
    unsigned char bytes[4];uint32_t code;unsigned i;
    if(!s->mcinst){minias_set_error(as,"missing-la-mcinst");return false;}
    code=s->mcinst->encoding;
    for(i=0U;i<4U;++i)bytes[i]=(unsigned char)(code>>(8U*i));
    return minias_section_append(as,s->section,bytes,sizeof(bytes));
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
    (void)kind;(void)width;return 0U; /* Missing relocations fail closed. */
}
static const MiniAsTargetOps ops={
    .name="loongarch64",.comment_prefix='#',.elf_machine=258U,
    .resolve_profile=la_profile,.parse_mcinst=la_parse,
    .measure_mcinst=la_measure,.encode=la_encode,
    .append_code_alignment=la_align,.fixup_relocation_type=la_reloc,
    .validate_output=la_validate
};
const MiniAsTargetOps *minias_target_loongarch64(void){return &ops;}
