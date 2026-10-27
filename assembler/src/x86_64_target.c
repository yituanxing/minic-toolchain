#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A first, fail-closed GNU AT&T SysV x86-64 target. All common expression,
 * symbol, section, fragment, and ELF handling lives outside this file. */
enum { X64_ZERO = 1, X64_REG_BINARY, X64_STACK, X64_MOV_IMM32, X64_ENDBR64, X64_ALU_IMM, X64_MOV_MEMORY };

static bool x64_register(const char *text, unsigned *reg, bool *wide) {
    static const char *const names64[16] = {
        "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
        "r8","r9","r10","r11","r12","r13","r14","r15"
    };
    static const char *const names32[16] = {
        "eax","ecx","edx","ebx","esp","ebp","esi","edi",
        "r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"
    };
    unsigned i;
    if (*text != '%') return false;
    ++text;
    for (i = 0U; i < 16U; ++i) {
        if (strcmp(text, names64[i]) == 0) { *reg=i; *wide=true; return true; }
        if (strcmp(text, names32[i]) == 0) { *reg=i; *wide=false; return true; }
    }
    return false;
}

static bool x64_args2(const char *args, char a[48], char b[48]) {
    const char *comma = strchr(args, ',');
    size_t n, m;
    if (!comma || strchr(comma+1, ',')) return false;
    n = (size_t)(comma - args);
    while (n && isspace((unsigned char)args[n-1])) --n;
    while (n && isspace((unsigned char)*args)) {++args;--n;}
    comma++;
    while (isspace((unsigned char)*comma)) ++comma;
    m = strlen(comma);
    while (m && isspace((unsigned char)comma[m-1])) --m;
    if (!n || !m || n>=48 || m>=48) return false;
    memcpy(a,args,n); a[n]='\0';
    memcpy(b,comma,m); b[m]='\0';
    return true;
}


/* GNU AT&T disp(%base) form. This first memory subset deliberately rejects
 * RIP-symbol PC relocation, scaled index and symbolic displacements until
 * their complete target-specific fixups exist. */
static bool x64_parse_base_memory(const char *text, MiniAsMCMemory *out) {
    const char *open = strchr(text, '(');
    const char *close;
    char regbuf[48], dispbuf[48], *end;
    size_t reglen, displen;
    unsigned base;
    bool wide;
    long long displacement = 0;
    if (!open) return false;
    close = strchr(open, ')');
    if (!close || close[1] != '\0' || strchr(open, ','))
        return false;
    reglen = (size_t)(close - open - 1);
    displen = (size_t)(open - text);
    if (!reglen || reglen >= sizeof(regbuf) || displen >= sizeof(dispbuf))
        return false;
    memcpy(regbuf, open + 1, reglen); regbuf[reglen] = '\0';
    if (!x64_register(regbuf, &base, &wide) || !wide)
        return false;
    if (displen != 0U) {
        memcpy(dispbuf, text, displen); dispbuf[displen] = '\0';
        errno=0; displacement=strtoll(dispbuf,&end,0);
        if (errno || end==dispbuf || *end ||
            displacement<INT32_MIN || displacement>INT32_MAX)
            return false;
    }
    memset(out, 0, sizeof(*out));
    out->has_base=true;out->base=base;out->scale=1U;
    out->displacement=(int64_t)displacement;
    return true;
}

static unsigned x64_disp_bytes(const MiniAsMCMemory *mem) {
    if (mem->displacement==0 && (mem->base & 7U)!=5U) return 0U;
    return mem->displacement>=-128 && mem->displacement<=127 ? 1U : 4U;
}

static bool x64_is_mem_candidate(const char *text) {
    return strchr(text, '(') != NULL;
}

static bool x64_parse(MiniAs *as, MiniAsStmt *s) {
    static const struct {const char *op; unsigned char bytes[3]; unsigned size;} zeros[]={
        {"nop",{0x90},1}, {"ret",{0xc3},1}, {"retq",{0xc3},1},
        {"leave",{0xc9},1}, {"leaveq",{0xc9},1},
        {"syscall",{0x0f,0x05},2}, {"int3",{0xcc},1},
        {"ud2",{0x0f,0x0b},2}, {"hlt",{0xf4},1}
    };
    static const struct {const char *op; unsigned char opcode; bool wide;} binary[]={
        {"movq",0x89,true}, {"movl",0x89,false},
        {"addq",0x01,true}, {"addl",0x01,false},
        {"subq",0x29,true}, {"subl",0x29,false},
        {"xorq",0x31,true}, {"xorl",0x31,false},
        {"andq",0x21,true}, {"andl",0x21,false},
        {"orq",0x09,true},  {"orl",0x09,false},
        {"cmpq",0x39,true},{"cmpl",0x39,false}
    };
    MiniAsMCInst *m=calloc(1U,sizeof(*m)+2U*sizeof(MiniAsMCOperand));
    unsigned i, rs, rd;
    bool ws, wd;
    char src[48],dst[48];
    if (!m) {minias_set_error(as,"out-of-memory:x86_64-mcinst");return false;}
    if (!strcmp(s->op,"endbr64") && !*s->args) {
        m->opcode=X64_ENDBR64;m->encoding=UINT32_C(0xfa1e0ff3);
        m->count=0U;goto done;
    }
    for(i=0U;i<sizeof(zeros)/sizeof(zeros[0]);++i) {
        if (!strcmp(s->op,zeros[i].op) && !*s->args) {
            m->opcode=X64_ZERO; m->count=0U;
            m->encoding=(unsigned)zeros[i].bytes[0] |
                (unsigned)zeros[i].bytes[1]<<8U |
                (unsigned)zeros[i].bytes[2]<<16U;
            m->operands[0].immediate=zeros[i].size;goto done;
        }
    }
    for(i=0U;i<sizeof(binary)/sizeof(binary[0]);++i) {
        if (strcmp(s->op,binary[i].op) || !x64_args2(s->args,src,dst) ||
            !x64_register(src,&rs,&ws) || !x64_register(dst,&rd,&wd) ||
            ws!=binary[i].wide || wd!=binary[i].wide) continue;
        m->opcode=X64_REG_BINARY; m->count=2U; m->encoding=binary[i].opcode;
        m->operands[0].reg=rs;m->operands[1].reg=rd;
        m->operands[0].register_64=binary[i].wide;
        goto done;
    }
    if ((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq")) &&
        x64_register(s->args,&rs,&ws) && ws) {
        m->opcode=X64_STACK;m->count=1U;m->operands[0].reg=rs;
        m->encoding=!strcmp(s->op,"pushq")?0x50U:0x58U;goto done;
    }
    if (!strcmp(s->op,"movl") && x64_args2(s->args,src,dst) &&
        src[0]=='$' && x64_register(dst,&rd,&wd) && !wd) {
        char *end;
        long long num;
        errno=0;
        num=strtoll(src+1,&end,0);
        if (!errno && end!=src+1 && !*end &&
            num>=INT32_MIN && num<=UINT32_MAX) {
            m->opcode=X64_MOV_IMM32;m->count=1U;
            m->operands[0].reg=rd;m->operands[0].immediate=(uint32_t)num;
            goto done;
        }
    }
    /* Group-1 ALU immediate form: GNU -S emits ADD/SUB $imm,%reg
     * frequently in Linux.  Sign-extended imm8 is canonical if it fits;
     * otherwise imm32; full memory ModR/M remains a separate target task. */
    if ((!strcmp(s->op,"addq") || !strcmp(s->op,"addl") ||
         !strcmp(s->op,"subq") || !strcmp(s->op,"subl")) &&
        x64_args2(s->args,src,dst) && src[0]=='$' &&
        x64_register(dst,&rd,&wd) &&
        wd==(s->op[strlen(s->op)-1U]=='q')) {
        char *end;
        long long num;
        errno=0;
        num=strtoll(src+1,&end,0);
        if (!errno && end!=src+1 && !*end &&
            num>=INT32_MIN && num<=INT32_MAX) {
            m->opcode=X64_ALU_IMM;m->count=2U;
            m->encoding=s->op[0]=='s'?5U:0U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].register_64=wd;
            m->operands[0].reg=rd;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint32_t)num;
            goto done;
        }
    }
    /* Retain memory addressing as one typed operand through layout,
     * encoding, and eventual ELF fixup. No source-string re-splitting. */
    if ((!strcmp(s->op,"movq") || !strcmp(s->op,"movl")) &&
        x64_args2(s->args,src,dst) &&
        (x64_is_mem_candidate(src) || x64_is_mem_candidate(dst))) {
        MiniAsMCMemory parsed;
        unsigned r;
        bool wide, memory_source;
        if (x64_register(src,&r,&wide) &&
            x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;
        } else if (x64_parse_base_memory(src,&parsed) &&
                   x64_register(dst,&r,&wide)) {
            memory_source=true;
        } else goto invalid;
        if (wide != (s->op[3]=='q')) goto invalid;
        m->operands[1].memory=malloc(sizeof(parsed));
        if (!m->operands[1].memory) {
            minias_set_error(as,"out-of-memory:x86_64-mc-memory");
            free(m);return false;
        }
        *m->operands[1].memory=parsed;
        m->opcode=X64_MOV_MEMORY; m->count=2U;
        m->encoding=memory_source?0x8bU:0x89U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=r;
        m->operands[0].register_64=wide;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
invalid:
    minias_set_error(as,"unsupported-x86_64-operand:%s:%s:line=%zu",
                     s->op,s->args,s->line);
    free(m);return false;
done:
    s->mcinst=m;return true;
}

static bool x64_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    const MiniAsMCInst *m=s->mcinst;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    switch(m->opcode) {
        case X64_ZERO: *size=(uint32_t)m->operands[0].immediate;return true;
        case X64_ENDBR64: *size=4U;return true;
        case X64_MOV_MEMORY: {
            const MiniAsMCMemory *mem=m->operands[1].memory;
            *size=(m->operands[0].register_64 ||
                   m->operands[0].reg>=8U || mem->base>=8U?1U:0U) +
                  2U + ((mem->base&7U)==4U?1U:0U) +
                  x64_disp_bytes(mem);
            return true;
        }
        case X64_ALU_IMM: {
            int32_t imm=(int32_t)(uint32_t)m->operands[1].immediate;
            *size=(m->operands[0].register_64 || m->operands[0].reg>=8U?1U:0U) +
                  (imm>=-128 && imm<=127?3U:6U);
            return true;
        }
        case X64_STACK: *size=(m->operands[0].reg>=8U)?2U:1U;return true;
        case X64_MOV_IMM32: *size=(m->operands[0].reg>=8U)?6U:5U;return true;
        case X64_REG_BINARY:
            *size=(m->operands[0].register_64 ||
                   m->operands[0].reg>=8U || m->operands[1].reg>=8U)?3U:2U;
            return true;
        default: minias_set_error(as,"unknown-x86_64-mcinst");return false;
    }
}

static bool x64_encode(MiniAs *as,const MiniAsStmt *s) {
    const MiniAsMCInst *m=s->mcinst;
    unsigned char code[8];size_t n=0;unsigned r,d;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    switch(m->opcode) {
        case X64_ZERO: {
            unsigned i;for(i=0U;i<(unsigned)m->operands[0].immediate;++i)
                code[n++]=(unsigned char)(m->encoding>>(8U*i));
            break;
        }
        case X64_ENDBR64: {
            unsigned i;
            for(i=0U;i<4U;++i)code[n++]=(unsigned char)(m->encoding>>(8U*i));
            break;
        }
        case X64_MOV_MEMORY: {
            const MiniAsMCMemory *mem=m->operands[1].memory;
            unsigned low_base=mem->base&7U;
            unsigned bytes=x64_disp_bytes(mem);
            unsigned mod=bytes==0U?0U:bytes==1U?1U:2U;
            unsigned i;
            uint32_t disp=(uint32_t)mem->displacement;
            r=m->operands[0].reg;
            if (m->operands[0].register_64 || r>=8U || mem->base>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((r>>3U)<<2U) | (mem->base>>3U));
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)((mod<<6U)|((r&7U)<<3U)|low_base);
            if (low_base==4U) code[n++]=0x24U; /* no index, SIB base rsp/r12 */
            for (i=0U;i<bytes;++i)
                code[n++]=(unsigned char)(disp>>(8U*i));
            break;
        }
        case X64_ALU_IMM: {
            unsigned i;
            int32_t imm=(int32_t)(uint32_t)m->operands[1].immediate;
            bool short_imm=imm>=-128 && imm<=127;
            r=m->operands[0].reg;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) | (r>>3U));
            code[n++]=short_imm?0x83U:0x81U;
            code[n++]=(unsigned char)(0xc0U | ((m->encoding&7U)<<3U) | (r&7U));
            for(i=0U;i<(short_imm?1U:4U);++i)
                code[n++]=(unsigned char)((uint32_t)imm>>(8U*i));
            break;
        }
        case X64_STACK:
            r=m->operands[0].reg;
            if (r>=8U) code[n++]=0x41;
            code[n++]=(unsigned char)(m->encoding+(r&7U));break;
        case X64_MOV_IMM32: {
            unsigned i;
            r=m->operands[0].reg;
            if (r>=8U) code[n++]=0x41;
            code[n++]=(unsigned char)(0xb8U+(r&7U));
            for(i=0U;i<4U;++i)
                code[n++]=(unsigned char)(m->operands[0].immediate>>(8U*i));
            break;
        }
        case X64_REG_BINARY:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if (m->operands[0].register_64 || r>=8U || d>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((r>>3U)<<2U) | (d>>3U));
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U | ((r&7U)<<3U) | (d&7U));
            break;
        default: minias_set_error(as,"bad-x86_64-mcinst");return false;
    }
    return minias_section_append(as,s->section,code,n);
}

static bool x64_code_align(MiniAs *as,int section,size_t n) {
    static const unsigned char nop=0x90;
    while(n-- != 0U) if (!minias_section_append(as,section,&nop,1U))return false;
    return true;
}

static bool x64_profile(const char *march,const char *mabi,
                        MiniAsAssemblyConfig *cfg,char *err,size_t cap) {
    if ((march && strcmp(march,"x86-64")) ||
        (mabi && strcmp(mabi,"sysv"))) {
        if(cap) snprintf(err,cap,"unsupported-x86_64-isa-abi");
        return false;
    }
    memset(cfg,0,sizeof(*cfg));cfg->target=minias_target_x86_64();return true;
}

static bool x64_validate(MiniAs *as) {(void)as;return true;}
static uint32_t x64_reloc(MiniAsFixupKind kind,unsigned width) {
    (void)kind;(void)width;return 0U; /* Unimplemented, never fake a relocation. */
}
static const MiniAsTargetOps ops={
    .name="x86_64",.comment_prefix='#',.elf_machine=62U,
    .resolve_profile=x64_profile,.parse_mcinst=x64_parse,
    .measure_mcinst=x64_measure,.encode=x64_encode,
    .append_code_alignment=x64_code_align,
    .fixup_relocation_type=x64_reloc,.validate_output=x64_validate
};
const MiniAsTargetOps *minias_target_x86_64(void){return &ops;}
