#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A first, fail-closed GNU AT&T SysV x86-64 target. All common expression,
 * symbol, section, fragment, and ELF handling lives outside this file. */
enum { X64_ZERO = 1, X64_REG_BINARY, X64_STACK, X64_MOV_IMM32 };

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
