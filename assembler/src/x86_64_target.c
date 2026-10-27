#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A first, fail-closed GNU AT&T SysV x86-64 target. All common expression,
 * symbol, section, fragment, and ELF handling lives outside this file. */
enum { X64_ZERO = 1, X64_REG_BINARY, X64_STACK, X64_MOV_IMM32, X64_ENDBR64, X64_ALU_IMM, X64_REG_MEMORY, X64_BRANCH, X64_IMM_MEMORY, X64_MOVQ_IMMREG, X64_MOVABS_IMMREG, X64_SETCC, X64_BINARY8, X64_MOVSLQ, X64_MOVZX_REG, X64_SHIFT_IMM, X64_TEST8_IMM, X64_PUSH_IMM, X64_CMOV_REG, X64_SHIFT_CL, X64_UNARY, X64_INDIRECT_REG };

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

/* GNU low-byte registers; high AH/CH/DH/BH require no-REX grammar and
 * are deliberately excluded until that separate legacy encoding is added. */
/* Source-width-specific registers belong to x86, not Common MCInst. */
static bool x64_register16(const char *text,unsigned *reg) {
    static const char *const names[16]={
        "ax","cx","dx","bx","sp","bp","si","di",
        "r8w","r9w","r10w","r11w","r12w","r13w","r14w","r15w"
    };
    unsigned i;
    if(*text!='%')return false;
    for(i=0U;i<16U;++i)
        if(!strcmp(text+1,names[i])){*reg=i;return true;}
    return false;
}

static bool x64_register8(const char *text,unsigned *reg) {
    static const char *const low8[16]={
        "al","cl","dl","bl","spl","bpl","sil","dil",
        "r8b","r9b","r10b","r11b","r12b","r13b","r14b","r15b"
    };
    unsigned i;
    if(*text!='%')return false;
    for(i=0U;i<16U;++i)
        if(strcmp(text+1,low8[i])==0){*reg=i;return true;}
    return false;
}

/* One ISA-owned condition-code table shared by CMOV and Jcc aliases. */
static bool x64_cond_code(const char *suffix, unsigned *cc) {
    static const struct {const char *name;unsigned cc;} names[]={
        {"o",0U},{"no",1U},{"b",2U},{"c",2U},{"nae",2U},
        {"ae",3U},{"nb",3U},{"nc",3U},{"e",4U},{"z",4U},
        {"ne",5U},{"nz",5U},{"be",6U},{"na",6U},
        {"a",7U},{"nbe",7U},{"s",8U},{"ns",9U},
        {"p",10U},{"pe",10U},{"np",11U},{"po",11U},
        {"l",12U},{"nge",12U},{"ge",13U},{"nl",13U},
        {"le",14U},{"ng",14U},{"g",15U},{"nle",15U}
    };
    size_t i;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i)
        if(!strcmp(suffix,names[i].name)){*cc=names[i].cc;return true;}
    return false;
}

static bool x64_args2(const char *args, char a[48], char b[48]) {
    /* AT&T memory forms can contain commas inside SIB parentheses.
     * Split operands once at top-level, not once per comma token. */
    const char *p, *comma = NULL, *right;
    unsigned depth = 0U;
    size_t n, m;
    for (p=args; *p; ++p) {
        if (*p=='(') ++depth;
        else if (*p==')') {
            if (!depth) return false;
            --depth;
        } else if (*p==',' && depth==0U) {
            if (comma) return false;
            comma=p;
        }
    }
    if (depth || !comma) return false;
    n=(size_t)(comma-args);
    while (n && isspace((unsigned char)args[n-1U])) --n;
    while (n && isspace((unsigned char)*args)) {++args;--n;}
    right=comma+1;
    while (isspace((unsigned char)*right)) ++right;
    m=strlen(right);
    while (m && isspace((unsigned char)right[m-1U])) --m;
    if (!n || !m || n>=48U || m>=48U) return false;
    memcpy(a,args,n);a[n]='\0';
    memcpy(b,right,m);b[m]='\0';
    return true;
}

/* GNU AT&T disp(%base[, %index[, scale]]) plus symbol(%rip).
 * SymbolExpr remains typed all the way into the existing common ELF fixup.
 * The PC-relative addressing mode is target-owned (ModR/M rm=101). */
static bool x64_parse_base_memory(const char *text, MiniAsMCMemory *out) {
    const char *open,*close,*p;
    unsigned segment_space=0U;
    char regs[3][48], dispbuf[48], *end;
    size_t dlen;
    unsigned count=0U, base=0U, index=0U;
    unsigned long scale=1UL;
    bool wide, symbolic=false, pc_relative=false;
    MiniAsSymbolExpr symbolic_expr;
    long long displacement=0;
    if(strncmp(text,"%fs:",4U)==0)segment_space=1U;
    else if(strncmp(text,"%gs:",4U)==0)segment_space=2U;
    if(segment_space)text+=4U;
    open=strchr(text,'(');
    if (!open) {
        unsigned space=0U;
        const char *operand;
        MiniAsSymbolExpr expr;
        space=segment_space;
        if(!space)return false;
        operand=text;
        if(!*operand)return false;
        errno=0;displacement=strtoll(operand,&end,0);
        if(errno || end==operand || *end ||
           displacement<INT32_MIN || displacement>INT32_MAX) {
            if(!minias_parse_symbol_addend(operand,&expr))return false;
            symbolic=true;displacement=0;
        }
        memset(out,0,sizeof(*out));
        out->scale=1U;out->address_space=space;
        out->displacement=(int64_t)displacement;
        if(symbolic) {
            out->symbol=malloc(sizeof(expr));
            if(!out->symbol)return false;
            *out->symbol=expr;
        }
        return true;
    }
    close=strchr(open+1,')');
    if (!close || close[1]!='\0') return false;
    dlen=(size_t)(open-text);
    if (dlen>=sizeof(dispbuf)) return false;
    if (dlen) {
        memcpy(dispbuf,text,dlen);dispbuf[dlen]='\0';
        errno=0;displacement=strtoll(dispbuf,&end,0);
        if (errno || end==dispbuf || *end ||
            displacement<INT32_MIN || displacement>INT32_MAX) {
            if(!minias_parse_symbol_addend(dispbuf,&symbolic_expr) ||
               symbolic_expr.addend<INT64_MIN+4)
                return false;
            symbolic=true;
            displacement=0;
        }
    }
    p=open+1;
    while (p<close) {
        const char *start=p;
        size_t n;
        while (p<close && *p!=',') ++p;
        n=(size_t)(p-start);
        while (n && isspace((unsigned char)*start)) {++start;--n;}
        while (n && isspace((unsigned char)start[n-1U])) --n;
        if ((n==0U && count!=0U) || n>=sizeof(regs[0]) ||
            count==3U) return false;
        memcpy(regs[count],start,n);regs[count][n]='\0';
        ++count;
        if (p<close) ++p;
    }
    if(count==1U && strcmp(regs[0],"%rip")==0) {
        pc_relative=true;
    } else if(count>=2U && regs[0][0]=='\0') {
        /* GNU disp(,%index,scale): no base, mandatory SIB disp32. */
        base=5U;
    } else {
        if(count<1U || !x64_register(regs[0],&base,&wide) || !wide ||
           symbolic)return false;
    }
    if (count>=2U) {
        if (!x64_register(regs[1],&index,&wide) || !wide ||
            index==4U) /* %rsp is unencodable as SIB index. */
            return false;
    }
    if (count==3U) {
        errno=0;scale=strtoul(regs[2],&end,0);
        if (errno || end==regs[2] || *end ||
            (scale!=1UL && scale!=2UL &&
             scale!=4UL && scale!=8UL))
            return false;
    }
    memset(out,0,sizeof(*out));
    out->has_base=!pc_relative && regs[0][0]!='\0';
    out->pc_relative=pc_relative;
    out->address_space=segment_space;
    out->base=base;out->has_index=(count>=2U);
    out->index=index;out->scale=(unsigned)scale;
    out->displacement=(int64_t)displacement;
    if(symbolic) {
        if(!pc_relative)return false;
        out->symbol=malloc(sizeof(symbolic_expr));
        if(!out->symbol)return false;
        *out->symbol=symbolic_expr;
    }
    return true;
}

static unsigned x64_disp_bytes(const MiniAsMCMemory *mem) {
    if (mem->pc_relative || mem->address_space || !mem->has_base)return 4U;
    if (mem->displacement==0 && (mem->base & 7U)!=5U) return 0U;
    return mem->displacement>=-128 && mem->displacement<=127 ? 1U : 4U;
}

static bool x64_is_mem_candidate(const char *text) {
    return strchr(text, '(') != NULL ||
           strncmp(text,"%gs:",4U)==0 || strncmp(text,"%fs:",4U)==0;
}

static bool x64_parse(MiniAs *as, MiniAsStmt *s) {
    static const struct {const char *op; unsigned char bytes[3]; unsigned size;} zeros[]={
        {"nop",{0x90},1}, {"ret",{0xc3},1}, {"retq",{0xc3},1},
        {"leave",{0xc9},1}, {"leaveq",{0xc9},1},
        {"syscall",{0x0f,0x05},2}, {"int3",{0xcc},1},
        {"ud2",{0x0f,0x0b},2}, {"hlt",{0xf4},1},
        {"pause",{0xf3,0x90},2},
        {"cltq",{0x48,0x98},2},{"cqto",{0x48,0x99},2},
        {"lock",{0xf0},1},
        /* GNU Linux .s uses standalone segment/repeat/operand prefixes.
         * They are 1-byte instruction fragments, NOT pseudo section state. */
        {"cs",{0x2e},1},{"ds",{0x3e},1},{"es",{0x26},1},
        {"ss",{0x36},1},{"fs",{0x64},1},{"gs",{0x65},1},
        {"rep",{0xf3},1},{"repe",{0xf3},1},{"repz",{0xf3},1},
        {"repne",{0xf2},1},{"repnz",{0xf2},1},
        {"data16",{0x66},1},{"addr32",{0x67},1},{"rex.W",{0x48},1},
        {"pushf",{0x9c},1},{"pushfq",{0x9c},1},
        {"popf",{0x9d},1},{"popfq",{0x9d},1},
        {"rep stosq",{0xf3,0x48,0xab},3},
        {"rep movsq",{0xf3,0x48,0xa5},3},
        {"rep stosb",{0xf3,0xaa},2}
    };
    static const struct {const char *op; unsigned char opcode; bool wide;} binary[]={
        {"movq",0x89,true}, {"movl",0x89,false},
        {"addq",0x01,true}, {"addl",0x01,false},
        {"subq",0x29,true}, {"subl",0x29,false},
        {"sbbq",0x19,true}, {"sbbl",0x19,false},
        {"xorq",0x31,true}, {"xorl",0x31,false},
        {"andq",0x21,true}, {"andl",0x21,false},
        {"orq",0x09,true},  {"orl",0x09,false},
        {"cmpq",0x39,true},{"cmpl",0x39,false},
        {"testq",0x85,true},{"testl",0x85,false},
        {"movw",0x89,false},{"addw",0x01,false},
        {"subw",0x29,false},{"sbbw",0x19,false},
        {"xorw",0x31,false},{"andw",0x21,false},
        {"orw",0x09,false},{"cmpw",0x39,false},
        {"testw",0x85,false}
    };
    /* reg/memory opcodes: source register to dest memory and vice versa.
     * TEST uses a commutative encoding; memory location is always r/m. */
    static const struct {
        const char *op; unsigned rm_dest, rm_src; bool wide;
    } regmem[] = {
        {"movq",0x89,0x8b,true},{"movl",0x89,0x8b,false},
        {"cmpq",0x39,0x3b,true},{"cmpl",0x39,0x3b,false},
        {"testq",0x85,0x85,true},{"testl",0x85,0x85,false},
        {"addq",0x01,0x03,true},{"addl",0x01,0x03,false},
        {"subq",0x29,0x2b,true},{"subl",0x29,0x2b,false},
        {"sbbq",0x19,0x1b,true},{"sbbl",0x19,0x1b,false},
        {"andq",0x21,0x23,true},{"andl",0x21,0x23,false},
        {"orq",0x09,0x0b,true},{"orl",0x09,0x0b,false},
        {"xorq",0x31,0x33,true},{"xorl",0x31,0x33,false},
        {"leaq",0,0x8d,true},{"leal",0,0x8d,false},
        /* 0F xx /r: same typed memory ModR/M machinery. */
        {"btq",0xa3,0,true},{"btl",0xa3,0,false},
        {"btsq",0xab,0,true},{"btsl",0xab,0,false},
        {"btrq",0xb3,0,true},{"btrl",0xb3,0,false},
        {"btcq",0xbb,0,true},{"btcl",0xbb,0,false},
        {"xaddq",0xc1,0,true},{"xaddl",0xc1,0,false},
        /* Existing typed memory ModR/M encoder owns REX/SIB/PC32 for
         * 0F B6/B7 zero-extension and 63 sign-extension. */
        {"movzbl",0,0x1b6U,false},{"movzbq",0,0x1b6U,true},
        {"movzwl",0,0x1b7U,false},{"movzwq",0,0x1b7U,true},
        {"movslq",0,0x63U,true},
        /* Explicit scalar widths, all reuse typed ModR/M, SIB and Fixup. */
        {"movb",0x88U,0x8aU,false},{"cmpb",0x38U,0x3aU,false},
        {"testb",0x84U,0x84U,false},{"addb",0x00U,0x02U,false},
        {"subb",0x28U,0x2aU,false},{"andb",0x20U,0x22U,false},
        {"orb",0x08U,0x0aU,false},{"xorb",0x30U,0x32U,false},
        {"movw",0x89U,0x8bU,false},{"cmpw",0x39U,0x3bU,false},
        {"testw",0x85U,0x85U,false},{"addw",0x01U,0x03U,false},
        {"subw",0x29U,0x2bU,false},{"andw",0x21U,0x23U,false},
        {"orw",0x09U,0x0bU,false},{"xorw",0x31U,0x33U,false}
    };
    MiniAsMCInst *m=calloc(1U,sizeof(*m)+3U*sizeof(MiniAsMCOperand));
    unsigned i, rs, rd;
    bool ws, wd;
    char src[48],dst[48];
    if (!m) {minias_set_error(as,"out-of-memory:x86_64-mcinst");return false;}
    if (!strcmp(s->op,"endbr64") && !*s->args) {
        m->opcode=X64_ENDBR64;m->encoding=UINT32_C(0xfa1e0ff3);
        m->count=0U;goto done;
    }
    /* GNU GAS spells REP STOSQ/MOVSQ as opcode + operand, not a single
     * whitespace-bearing mnemonic. The prefix and operation stay one
     * retained instruction fragment for exact-size section layout. */
    if(!strcmp(s->op,"rep") || !strcmp(s->op,"repz")) {
        if(!strcmp(s->args,"stosq")) {
            m->opcode=X64_ZERO;m->encoding=UINT32_C(0xab48f3);
            m->operands[0].immediate=3U;goto done;
        }
        if(!strcmp(s->args,"movsq")) {
            m->opcode=X64_ZERO;m->encoding=UINT32_C(0xa548f3);
            m->operands[0].immediate=3U;goto done;
        }
        if(!strcmp(s->args,"stosb")) {
            m->opcode=X64_ZERO;m->encoding=UINT32_C(0xaaf3);
            m->operands[0].immediate=2U;goto done;
        }
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
    /* Group-3 NOT/NEG and Group-5 INC/DEC, shared typed ModR/M
     * between register and memory forms. No duplicated memory encoder. */
    {
        static const struct {const char *op;unsigned opcode,group;bool wide;} unary[]={
            {"notl",0xf7U,2U,false},{"notq",0xf7U,2U,true},
            {"negl",0xf7U,3U,false},{"negq",0xf7U,3U,true},
            {"incl",0xffU,0U,false},{"incq",0xffU,0U,true},
            {"decl",0xffU,1U,false},{"decq",0xffU,1U,true}
        };
        for(i=0U;i<sizeof(unary)/sizeof(unary[0]);++i) {
            if(strcmp(s->op,unary[i].op)!=0)continue;
            if(x64_register(s->args,&rd,&wd) && wd==unary[i].wide) {
                m->opcode=X64_UNARY;m->count=1U;
                m->encoding=unary[i].opcode | (unary[i].group<<8U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].register_64=wd;
                m->operands[0].reg=rd;
                goto done;
            }
            if(x64_is_mem_candidate(s->args)) {
                MiniAsMCMemory parsed;
                if(!x64_parse_base_memory(s->args,&parsed))goto invalid;
                m->operands[1].memory=malloc(sizeof(parsed));
                if(!m->operands[1].memory) {
                    free(parsed.symbol);free(m);
                    minias_set_error(as,"out-of-memory:x86-unary-memory");
                    return false;
                }
                *m->operands[1].memory=parsed;
                m->opcode=X64_REG_MEMORY;m->count=2U;
                m->encoding=unary[i].opcode;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=unary[i].group;
                m->operands[0].register_64=unary[i].wide;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                goto done;
            }
            goto invalid;
        }
    }
    /* GNU group-2 implicit count=1 shifts, e.g. SHRL %eax.
     * Reuse the existing typed immediate-one form / D1 encoding; no
     * independent measure or emitter path. */
    {
        static const struct {const char *op;unsigned group;bool wide;} one[]={
            {"sall",4U,false},{"salq",4U,true},
            {"shll",4U,false},{"shlq",4U,true},
            {"shrl",5U,false},{"shrq",5U,true},
            {"sarl",7U,false},{"sarq",7U,true}
        };
        if(x64_register(s->args,&rd,&wd)) {
            for(i=0U;i<sizeof(one)/sizeof(one[0]);++i)
                if(!strcmp(s->op,one[i].op) && wd==one[i].wide) {
                    m->opcode=X64_SHIFT_IMM;m->encoding=one[i].group;
                    m->count=2U;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=rd;
                    m->operands[0].register_64=wd;
                    m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[1].immediate=1U;
                    goto done;
                }
        }
    }
    /* Byte ALU variants and sign-extending MOV all remain target local. */
    if(x64_args2(s->args,src,dst)) {
        static const struct {const char *op;uint32_t opcode;} byteop[]={
            {"movb",0x88U},{"addb",0x00U},{"orb",0x08U},
            {"andb",0x20U},{"subb",0x28U},{"xorb",0x30U},
            {"cmpb",0x38U},{"testb",0x84U}
        };
        unsigned first,second;
        if(!strncmp(s->op,"cmov",4U) &&
           x64_register(src,&first,&ws) &&
           x64_register(dst,&second,&wd) && wd==ws) {
            const char *name=s->op+4U;
            char cond[20];
            size_t clen=strlen(name);
            unsigned cc;
            if(clen!=0U && clen<sizeof(cond)) {
                memcpy(cond,name,clen+1U);
                /* AT&T suffix l/q and GNU unsuffixed size inference. */
                if((cond[clen-1U]=='q' || cond[clen-1U]=='l') &&
                    clen>1U && x64_cond_code(cond,&cc)==false) {
                    bool width=cond[clen-1U]=='q';
                    cond[clen-1U]='\0';
                    if(width!=wd)goto invalid;
                }
                if(x64_cond_code(cond,&cc)) {
                    m->opcode=X64_CMOV_REG;m->count=2U;
                    m->encoding=cc;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=first;
                    m->operands[0].register_64=ws;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=second;
                    goto done;
                }
            }
        }
        /* MOVZX source byte/word and independent destination width. */
        if((!strcmp(s->op,"movzbl") || !strcmp(s->op,"movzbq") ||
            !strcmp(s->op,"movzwl") || !strcmp(s->op,"movzwq")) &&
           ((s->op[4]=='b' && x64_register8(src,&first)) ||
            (s->op[4]=='w' && x64_register16(src,&first))) &&
           x64_register(dst,&second,&wd) && wd==(s->op[5]=='q')) {
            m->opcode=X64_MOVZX_REG;m->count=2U;
            m->encoding=s->op[4]=='b'?0xb6U:0xb7U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=second;
            m->operands[1].register_64=wd;
            goto done;
        }
        if((src[0]=='$' || !strcmp(src,"%cl")) && x64_register(dst,&second,&wd)) {
            static const struct {const char *op;unsigned group;bool wide;} shifts[]={
                {"sall",4U,false},{"salq",4U,true},
                {"shll",4U,false},{"shlq",4U,true},
                {"shrl",5U,false},{"shrq",5U,true},
                {"sarl",7U,false},{"sarq",7U,true}
            };
            uint64_t count=0U;
            if((!strcmp(src,"%cl") ||
                (minias_eval_u64_expression(src+1,&count) && count<=255U)))
                for(i=0U;i<sizeof(shifts)/sizeof(shifts[0]);++i)
                    if(!strcmp(s->op,shifts[i].op) && wd==shifts[i].wide) {
                        m->opcode=!strcmp(src,"%cl")?
                            X64_SHIFT_CL:X64_SHIFT_IMM;
                        m->count=2U;
                        m->encoding=shifts[i].group;
                        m->operands[0].kind=MINIAS_MC_REGISTER;
                        m->operands[0].reg=second;
                        m->operands[0].register_64=wd;
                        m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                        m->operands[1].immediate=count;
                        goto done;
                    }
        }
        if(!strcmp(s->op,"testb") && src[0]=='$' &&
           x64_register8(dst,&second)) {
            uint64_t bits;
            if(minias_eval_u64_expression(src+1,&bits) &&
               (bits<=255U || bits>=UINT64_MAX-127U)) {
                m->opcode=X64_TEST8_IMM;m->count=2U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=second;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=bits;
                goto done;
            }
        }
        if(x64_register8(src,&first) && x64_register8(dst,&second)) {
            for(i=0U;i<sizeof(byteop)/sizeof(byteop[0]);++i)
                if(strcmp(s->op,byteop[i].op)==0) {
                    m->opcode=X64_BINARY8;m->count=2U;
                    m->encoding=byteop[i].opcode;
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=first;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=second;
                    goto done;
                }
        }
        if(!strcmp(s->op,"movslq") &&
           x64_register(src,&first,&ws) && !ws &&
           x64_register(dst,&second,&wd) && wd) {
            m->opcode=X64_MOVSLQ;m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;m->operands[1].reg=second;
            goto done;
        }
    }
    {
        static const struct {const char *op;unsigned second_byte;} setcc[]={
            {"seto",0x90U},{"setno",0x91U},
            {"setb",0x92U},{"setc",0x92U},{"setnae",0x92U},
            {"setae",0x93U},{"setnc",0x93U},{"setnb",0x93U},
            {"sete",0x94U},{"setz",0x94U},
            {"setne",0x95U},{"setnz",0x95U},
            {"setbe",0x96U},{"setna",0x96U},
            {"seta",0x97U},{"setnbe",0x97U},
            {"sets",0x98U},{"setns",0x99U},
            {"setp",0x9aU},{"setpe",0x9aU},
            {"setnp",0x9bU},{"setpo",0x9bU},
            {"setl",0x9cU},{"setnge",0x9cU},
            {"setge",0x9dU},{"setnl",0x9dU},
            {"setle",0x9eU},{"setng",0x9eU},
            {"setg",0x9fU},{"setnle",0x9fU}
        };
        for(i=0U;i<sizeof(setcc)/sizeof(setcc[0]);++i) {
            if(strcmp(s->op,setcc[i].op)==0 && x64_register8(s->args,&rd)) {
                m->opcode=X64_SETCC;m->count=1U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;m->encoding=setcc[i].second_byte;
                goto done;
            }
        }
    }
    for(i=0U;i<sizeof(binary)/sizeof(binary[0]);++i) {
        bool is_word=s->op[strlen(s->op)-1U]=='w';
        if(strcmp(s->op,binary[i].op) || !x64_args2(s->args,src,dst))continue;
        if(is_word) {
            if(!x64_register16(src,&rs) || !x64_register16(dst,&rd))continue;
            ws=false;wd=false;
        } else {
            if(!x64_register(src,&rs,&ws) || !x64_register(dst,&rd,&wd))continue;
        }
        if(ws!=binary[i].wide || wd!=binary[i].wide)continue;
        m->opcode=X64_REG_BINARY;m->count=2U;
        m->encoding=binary[i].opcode|(is_word?0x400U:0U);
        m->operands[0].reg=rs;m->operands[1].reg=rd;
        m->operands[0].register_64=binary[i].wide;
        goto done;
    }
    /* Fixed rel32 transfer of control. Short-branch relaxation is not
     * advertised: keeping exact fragment sizes prevents layout corruption.
     * External symbols use ELF PC32 / PLT32 fixups with the x86 P+4 bias. */
    {
        static const struct {const char *op;unsigned encoding;} branches[]={
            {"call",0xe8U},{"callq",0xe8U},
            {"jmp",0xe9U},{"jmpq",0xe9U},
            {"je",0x184U},{"jz",0x184U},{"jne",0x185U},{"jnz",0x185U},
            {"ja",0x187U},{"jae",0x183U},{"jnb",0x183U},
            {"jb",0x182U},{"jbe",0x186U},
            {"jg",0x18fU},{"jge",0x18dU},{"jl",0x18cU},{"jle",0x18eU},
            {"js",0x188U},{"jns",0x189U},{"jo",0x180U},{"jno",0x181U},
            {"jp",0x18aU},{"jnp",0x18bU},{"jc",0x182U},{"jnc",0x183U},
            {"jna",0x186U},{"jnbe",0x187U},{"jnae",0x182U},
            {"jnge",0x18cU},{"jnl",0x18dU},{"jng",0x18eU},{"jnle",0x18fU}
        };
        for(i=0U;i<sizeof(branches)/sizeof(branches[0]);++i) {
            MiniAsSymbolExpr expr;
            if(strcmp(s->op,branches[i].op)!=0)continue;
            /* Register-indirect CALL/JMP are FF /2 and /4, not PLT32
             * symbolic branches. Dispatch them to the MCInst group-5
             * register parser below rather than rejecting '*' here. */
            if(s->args[0]=='*' &&
               (!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
                !strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq")))continue;
            if(!minias_parse_symbol_addend(s->args,&expr) ||
               expr.addend<INT64_MIN+4)goto invalid;
            m->operands[0].symbol=malloc(sizeof(expr));
            if(!m->operands[0].symbol) {
                minias_set_error(as,"out-of-memory:x86-branch-symbol");
                free(m);return false;
            }
            *m->operands[0].symbol=expr;
            m->operands[0].kind=MINIAS_MC_SYMBOL;
            m->opcode=X64_BRANCH;m->count=1U;
            m->encoding=branches[i].encoding;
            goto done;
        }
    }
    if(!strcmp(s->op,"pushq") && s->args[0]=='$') {
        uint64_t bits;
        if(minias_eval_u64_expression(s->args+1,&bits) &&
           (int64_t)bits>=INT32_MIN && (int64_t)bits<=INT32_MAX) {
            m->opcode=X64_PUSH_IMM;m->count=1U;
            m->operands[0].kind=MINIAS_MC_IMMEDIATE;
            m->operands[0].immediate=bits;
            goto done;
        }
    }
    if ((!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
         !strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq")) &&
        s->args[0]=='*' &&
        x64_register(s->args+1,&rs,&ws) && ws) {
        m->opcode=X64_INDIRECT_REG;m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rs;m->operands[0].register_64=true;
        m->encoding=(!strcmp(s->op,"call")||!strcmp(s->op,"callq"))?
                    2U:4U;
        goto done;
    }
    if ((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq") ||
         !strcmp(s->op,"push") || !strcmp(s->op,"pop")) &&
        x64_register(s->args,&rs,&ws) && ws) {
        m->opcode=X64_STACK;m->count=1U;m->operands[0].reg=rs;
        m->encoding=(!strcmp(s->op,"pushq") ||
                     !strcmp(s->op,"push"))?0x50U:0x58U;goto done;
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
    /* GNU encodes MOVABS (and MOVQ imm64 outside signed32 range) through
     * REX.W B8+r imm64. Keep ABS64 symbol relocation distinct from the
     * GNU sign-extending C7 /0 imm32 form. */
    if(x64_args2(s->args,src,dst) && src[0]=='$' &&
       (!strcmp(s->op,"movabsq") || !strcmp(s->op,"movq")) &&
       x64_register(dst,&rd,&wd) && wd) {
        uint64_t bits;
        MiniAsSymbolExpr expr;
        bool numeric=minias_eval_u64_expression(src+1,&bits);
        bool symbolic=!numeric && !strcmp(s->op,"movabsq") &&
            minias_parse_symbol_addend(src+1,&expr);
        if((numeric && (!strcmp(s->op,"movabsq") ||
           (int64_t)bits<INT32_MIN || (int64_t)bits>INT32_MAX)) ||
           symbolic) {
            m->opcode=X64_MOVABS_IMMREG;m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;m->operands[0].register_64=true;
            m->operands[1].kind=symbolic?MINIAS_MC_SYMBOL:MINIAS_MC_IMMEDIATE;
            if(symbolic) {
                m->operands[1].symbol=malloc(sizeof(expr));
                if(!m->operands[1].symbol){
                    minias_set_error(as,"out-of-memory:x86-movabs-symbol");
                    free(m);return false;
                }
                *m->operands[1].symbol=expr;
            }else m->operands[1].immediate=bits;
            goto done;
        }
    }
    /* GNU Linux compiler source uses C7, 81, 83 and F7 immediates against
     * memory and registers. Keep the same typed memory/ModR/M engine. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *op;unsigned group;} groups[]={
            {"addq",0U},{"addl",0U},{"orq",1U},{"orl",1U},
            {"andq",4U},{"andl",4U},{"subq",5U},{"subl",5U},
            {"sbbq",3U},{"sbbl",3U},
            {"xorq",6U},{"xorl",6U},{"cmpq",7U},{"cmpl",7U},
            {"addb",0U},{"orb",1U},{"andb",4U},{"subb",5U},
            {"xorb",6U},{"cmpb",7U},
            {"addw",0U},{"orw",1U},{"sbbw",3U},{"andw",4U},
            {"subw",5U},{"xorw",6U},{"cmpw",7U}
        };
        uint64_t bits=0U;
        int64_t val;
        unsigned group=0U;
        bool has_group=false,wide=s->op[strlen(s->op)-1U]=='q';
        MiniAsSymbolExpr sym;
        bool is_sym=false;
        bool numeric;
        for(i=0U;i<sizeof(groups)/sizeof(groups[0]);++i)
            if(!strcmp(s->op,groups[i].op)){
                group=groups[i].group;has_group=true;break;
            }
        numeric=minias_eval_u64_expression(src+1,&bits);
        if(!numeric && !strcmp(s->op,"movq") &&
           minias_parse_symbol_addend(src+1,&sym))is_sym=true;
        val=(int64_t)bits;
        {
            char suffix=s->op[strlen(s->op)-1U];
            bool in_range=numeric &&
                (suffix=='b'?(val>=-128 && val<=255):
                 suffix=='w'?(val>=-32768 && val<=65535):
                 val>=INT32_MIN && val<=INT32_MAX);
        if(in_range || is_sym) {
            if(!strcmp(s->op,"movq") &&
               x64_register(dst,&rd,&wd) && wd) {
                m->opcode=X64_MOVQ_IMMREG;m->count=2U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;m->operands[0].register_64=true;
                if(is_sym){
                    m->operands[1].symbol=malloc(sizeof(sym));
                    if(!m->operands[1].symbol) {
                        minias_set_error(as,"out-of-memory:x86-imm-symbol");
                        free(m);return false;
                    }
                    *m->operands[1].symbol=sym;
                    m->operands[1].kind=MINIAS_MC_SYMBOL;
                }else {
                    m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[1].immediate=bits;
                }
                goto done;
            }
            if(!is_sym && x64_is_mem_candidate(dst) &&
               (has_group || !strcmp(s->op,"movq") ||
                !strcmp(s->op,"movl") || !strcmp(s->op,"movw") ||
                !strcmp(s->op,"movb") || !strcmp(s->op,"testq") ||
                !strcmp(s->op,"testl") || !strcmp(s->op,"testw") ||
                !strcmp(s->op,"testb"))) {
                MiniAsMCMemory parsed;
                if(s->op[strlen(s->op)-1U]=='b' &&
                   (val < -128 || val > 255))goto invalid;
                if(!x64_parse_base_memory(dst,&parsed))goto invalid;
                m->operands[1].memory=malloc(sizeof(parsed));
                if(!m->operands[1].memory){
                    free(parsed.symbol);
                    minias_set_error(as,"out-of-memory:x86-imm-memory");
                    free(m);return false;
                }
                *m->operands[1].memory=parsed;
                m->opcode=X64_IMM_MEMORY;m->count=3U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=has_group?group:0U;
                m->operands[0].register_64=wide;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                m->operands[2].immediate=bits;
                m->encoding=(has_group ?
                    (suffix=='b'?0x80U:
                     val>=-128 && val<=127?0x83U:0x81U):
                    (!strcmp(s->op,"movb")?0xc6U:
                     !strcmp(s->op,"testb")?0xf6U:
                     !strncmp(s->op,"test",4U)?0xf7U:0xc7U))) |
                    (suffix=='w'?0x400U:0U);
                goto done;
            }
        }
        }
    }
    /* One typed Group-1 ALU register-immediate path across 8/16/32/64.
     * Encoded target width flags are disjoint from the three-bit ALU group;
     * the existing MCInst length/emit phases own the real byte count. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *name;unsigned group;} groups[]={
            {"add",0U},{"or",1U},{"sbb",3U},{"and",4U},
            {"sub",5U},{"xor",6U},{"cmp",7U}
        };
        size_t len=strlen(s->op);
        char width=len?s->op[len-1U]:'\0';
        unsigned bits=width=='b'?8U:width=='w'?16U:
                      width=='l'?32U:width=='q'?64U:0U;
        unsigned reg=0U,group=0U;
        bool matched=false,wide=false;
        uint64_t imm;
        if(bits && minias_eval_u64_expression(src+1,&imm) &&
           ((bits==8U && (int64_t)imm>=-128 && (int64_t)imm<=255) ||
            (bits==16U && (int64_t)imm>=-32768 && (int64_t)imm<=65535) ||
            (bits>=32U && (int64_t)imm>=INT32_MIN &&
                           (int64_t)imm<=INT32_MAX)) &&
           ((bits==8U && x64_register8(dst,&reg)) ||
            (bits==16U && x64_register16(dst,&reg)) ||
            (bits>=32U && x64_register(dst,&reg,&wide) &&
             wide==(bits==64U)))) {
            for(i=0U;i<sizeof(groups)/sizeof(groups[0]);++i) {
                size_t glen=strlen(groups[i].name);
                if(len==glen+1U && !strncmp(s->op,groups[i].name,glen)) {
                    group=groups[i].group;matched=true;break;
                }
            }
            if(matched) {
                m->opcode=X64_ALU_IMM;m->count=2U;
                m->encoding=group|(bits==8U?0x200U:
                                   bits==16U?0x400U:0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].register_64=bits==64U;
                m->operands[0].reg=reg;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=imm;
                goto done;
            }
        }
    }
    /* Retain memory addressing as one typed operand through layout,
     * encoding, and eventual ELF fixup. No source-string re-splitting. */
    if (x64_args2(s->args,src,dst) &&
        (x64_is_mem_candidate(src) || x64_is_mem_candidate(dst))) {
        MiniAsMCMemory parsed;
        unsigned r, reg_bits=0U;
        bool wide=false, memory_source;
        const unsigned *chosen = NULL;
        if (x64_register(src,&r,&wide) &&
            x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=wide?64U:32U;
        } else if (x64_register8(src,&r) &&
                   x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=8U;
        } else if (x64_register16(src,&r) &&
                   x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=16U;
        } else if (x64_register(dst,&r,&wide) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=wide?64U:32U;
        } else if (x64_register8(dst,&r) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=8U;
        } else if (x64_register16(dst,&r) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=16U;
        } else goto invalid;
        for (i=0U;i<sizeof(regmem)/sizeof(regmem[0]);++i) {
            char suffix=s->op[strlen(s->op)-1U];
            if(strcmp(s->op,regmem[i].op)==0 &&
               wide==regmem[i].wide &&
               (suffix=='b'?reg_bits==8U:
                suffix=='w'?reg_bits==16U:
                suffix=='q'?reg_bits==64U:reg_bits==32U)) {
                if((!strcmp(s->op,"leaq")||!strcmp(s->op,"leal") ||
                    !strcmp(s->op,"movzbl") || !strcmp(s->op,"movzbq") ||
                    !strcmp(s->op,"movzwl") || !strcmp(s->op,"movzwq") ||
                    !strcmp(s->op,"movslq")) && !memory_source)continue;
                chosen=memory_source?&regmem[i].rm_src:&regmem[i].rm_dest;
                break;
            }
        }
        if (!chosen || *chosen==0U) {free(parsed.symbol);goto invalid;}
        m->operands[1].memory=malloc(sizeof(parsed));
        if (!m->operands[1].memory) {
            minias_set_error(as,"out-of-memory:x86_64-mc-memory");
            free(parsed.symbol);free(m);return false;
        }
        *m->operands[1].memory=parsed;
        m->opcode=X64_REG_MEMORY; m->count=2U;
        /* Target-local encoding flags: 0x100=two-byte opcode,
         * 0x200=low-byte REX mode, 0x400=66 word-size override. */
        m->encoding=*chosen |
            ((!strncmp(s->op,"bt",2U) || !strncmp(s->op,"xadd",4U)) ?
             0x100U : 0U) |
            (reg_bits==8U?0x200U:reg_bits==16U?0x400U:0U);
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
        case X64_SETCC:
            *size=3U+(m->operands[0].reg>=4U?1U:0U);return true;
        case X64_BINARY8:
            *size=2U+(m->operands[0].reg>=4U||
                       m->operands[1].reg>=4U?1U:0U);return true;
        case X64_MOVSLQ: *size=3U;return true;
        case X64_CMOV_REG:
            *size=3U+(m->operands[0].register_64 ||
                       m->operands[0].reg>=8U || m->operands[1].reg>=8U?
                       1U:0U);return true;
        case X64_UNARY:
            *size=2U+(m->operands[0].register_64 ||
                       m->operands[0].reg>=8U?1U:0U);return true;
        case X64_SHIFT_CL:
            *size=2U+(m->operands[0].register_64 ||
                       m->operands[0].reg>=8U?1U:0U);return true;
        case X64_MOVZX_REG:
            *size=3U+((m->encoding==0xb6U && m->operands[0].reg>=4U) ||
                       m->operands[0].reg>=8U || m->operands[1].reg>=8U ||
                       m->operands[1].register_64?1U:0U);return true;
        case X64_SHIFT_IMM:
            *size=(m->operands[0].register_64 ||
                   m->operands[0].reg>=8U?1U:0U)+
                  (m->operands[1].immediate==1U?2U:3U);return true;
        case X64_TEST8_IMM:
            *size=m->operands[0].reg==0U?2U:
                  (3U+(m->operands[0].reg>=4U?1U:0U));return true;
        case X64_PUSH_IMM: {
            int32_t imm=(int32_t)(uint32_t)m->operands[0].immediate;
            *size=imm>=-128 && imm<=127?2U:5U;return true;
        }
        case X64_BRANCH:
            /* Start JMP/Jcc at GNU's shortest encoding; target relaxation
             * promotes external/out-of-range edges before final emission. */
            *size=m->encoding==0xe8U?5U:2U;return true;
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            const MiniAsMCMemory *mem=m->operands[1].memory;
            unsigned base=m->encoding&UINT32_C(0xff);
            unsigned extra=m->opcode==X64_IMM_MEMORY?
                (base==0x83U || base==0xc6U || base==0x80U ||
                 base==0xf6U?1U:(m->encoding&0x400U)?2U:4U):0U;
            *size=extra+((m->encoding&0x100U)?1U:0U) +
                  ((m->encoding&0x400U)?1U:0U) +
                  (mem->address_space?1U:0U) + (m->operands[0].register_64 ||
                   m->operands[0].reg>=8U ||
                   ((m->encoding&0x200U) && m->operands[0].reg>=4U) ||
                   mem->base>=8U ||
                   (mem->has_index && mem->index>=8U)?1U:0U) +
                  2U + (((mem->address_space && !mem->pc_relative) ||
                          mem->has_index || (mem->base&7U)==4U)?1U:0U) +
                  x64_disp_bytes(mem);
            return true;
        }
        case X64_MOVQ_IMMREG: *size=7U;return true;
        case X64_MOVABS_IMMREG: *size=10U;return true;
        case X64_ALU_IMM: {
            int64_t imm=(int64_t)m->operands[1].immediate;
            bool byte=(m->encoding&0x200U)!=0U;
            bool word=(m->encoding&0x400U)!=0U;
            bool short_imm=imm>=-128 && imm<=127;
            bool acc=m->operands[0].reg==0U;
            unsigned base=byte?(acc?2U:3U):
                 word?(acc&&!short_imm?3U:short_imm?3U:4U):
                 (short_imm?3U:6U);
            *size=(word?1U:0U)+
                  (m->operands[0].register_64 || m->operands[0].reg>=8U ||
                   (byte && m->operands[0].reg>=4U)?1U:0U)+base;
            return true;
        }
        case X64_STACK: *size=(m->operands[0].reg>=8U)?2U:1U;return true;
        case X64_INDIRECT_REG:
            *size=m->operands[0].reg>=8U?3U:2U;return true;
        case X64_MOV_IMM32: *size=(m->operands[0].reg>=8U)?6U:5U;return true;
        case X64_REG_BINARY:
            *size=((m->encoding&0x400U)?1U:0U)+
                  ((m->operands[0].register_64 ||
                   m->operands[0].reg>=8U || m->operands[1].reg>=8U)?3U:2U);
            return true;
        default: minias_set_error(as,"unknown-x86_64-mcinst");return false;
    }
}

/* Use the proven Common growth/forced-fragment framework, not an x86
 * duplicate of section layout. Out-of-range local and external edges
 * grow from 2 to rel32 before any bytes or fixups are emitted. */
static bool x64_relax_instruction(MiniAs *as,const MiniAsStmt *s,uint32_t *next) {
    const MiniAsMCInst *m=s->mcinst;
    const MiniAsSymbolExpr *expr;
    MiniAsSymbol *sym;
    uint32_t large;
    int64_t distance;
    if(!m || m->opcode!=X64_BRANCH)return false;
    *next=s->size;
    if(m->encoding==0xe8U || s->size!=2U)return true;
    large=m->encoding>=0x100U?6U:5U;
    expr=m->operands[0].symbol;
    sym=minias_get_symbol(as,expr->name,false);
    if(!sym || !sym->defined || sym->section!=s->section ||
       sym->value>INT64_MAX || s->offset>(uint64_t)INT64_MAX-2U) {
        *next=large;return true;
    }
    distance=(int64_t)sym->value-(int64_t)(s->offset+2U);
    if((expr->addend>0 && distance>INT64_MAX-expr->addend) ||
       (expr->addend<0 && distance<INT64_MIN-expr->addend)) {
        *next=large;return true;
    }
    distance+=expr->addend;
    if(distance<-128 || distance>127)*next=large;
    return true;
}

static bool x64_expanded_instruction_size(const char *op,uint32_t current,
                                           uint32_t *size) {
    if(current!=2U || op[0]!='j')return false;
    *size=(!strcmp(op,"jmp") || !strcmp(op,"jmpq"))?5U:6U;
    return true;
}

static bool x64_encode(MiniAs *as,const MiniAsStmt *s) {
    const MiniAsMCInst *m=s->mcinst;
    unsigned char code[16];size_t n=0;unsigned r,d;
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
        case X64_SETCC:
            r=m->operands[0].reg;
            if(r>=4U)code[n++]=(unsigned char)(0x40U|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|(r&7U));
            break;
        case X64_BINARY8:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(r>=4U||d>=4U)
                code[n++]=(unsigned char)(0x40U|((r>>3U)<<2U)|(d>>3U));
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|((r&7U)<<3U)|(d&7U));
            break;
        case X64_CMOV_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->operands[0].register_64 || r>=8U || d>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)(0x40U+(m->encoding&15U));
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_UNARY:
            r=m->operands[0].reg;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|
                (((m->encoding>>8U)&7U)<<3U)|(r&7U));
            break;
        case X64_SHIFT_CL:
            r=m->operands[0].reg;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=0xd3U;
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            break;
        case X64_MOVZX_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if((m->encoding==0xb6U && r>=4U) || r>=8U || d>=8U ||
               m->operands[1].register_64)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[1].register_64?8U:0U)|
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_SHIFT_IMM:
            r=m->operands[0].reg;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=m->operands[1].immediate==1U?0xd1U:0xc1U;
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            if(m->operands[1].immediate!=1U)
                code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_TEST8_IMM:
            r=m->operands[0].reg;
            if(r==0U) code[n++]=0xa8U;
            else {
                if(r>=4U)code[n++]=(unsigned char)(0x40U|(r>>3U));
                code[n++]=0xf6U;
                code[n++]=(unsigned char)(0xc0U|(r&7U));
            }
            code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_PUSH_IMM: {
            int32_t imm=(int32_t)(uint32_t)m->operands[0].immediate;
            unsigned i;
            bool short_imm=imm>=-128 && imm<=127;
            code[n++]=short_imm?0x6aU:0x68U;
            for(i=0U;i<(short_imm?1U:4U);++i)
                code[n++]=(unsigned char)((uint32_t)imm>>(8U*i));
            break;
        }
        case X64_MOVSLQ:
            r=m->operands[0].reg;d=m->operands[1].reg;
            code[n++]=(unsigned char)(0x48U|((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x63U;
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_BRANCH: {
            const MiniAsSymbolExpr *expr=m->operands[0].symbol;
            MiniAsSymbol *sym=minias_get_symbol(as,expr->name,false);
            MiniAsFixupKind kind=MINIAS_FIXUP_X64_PLT32;
            size_t width=s->size;
            int64_t displacement=0;
            bool local=sym!=NULL && sym->defined && sym->section==s->section;
            unsigned i;
            if(width!=2U && width!=(m->encoding>=0x100U?6U:5U)) {
                minias_set_error(as,"x86-branch-invalid-layout:line=%zu",s->line);
                return false;
            }
            if(width==2U && (!local || m->encoding==0xe8U)) {
                minias_set_error(as,"x86-branch-unrelaxed:line=%zu",s->line);
                return false;
            }
            if(width==2U) {
                code[n++]=(unsigned char)(m->encoding>=0x100U?
                    0x70U|(m->encoding&15U):0xebU);
            }else {
                if(m->encoding&0x100U)code[n++]=0x0fU;
                code[n++]=(unsigned char)m->encoding;
            }
            if(local) {
                int64_t distance;
                if(sym->value>INT64_MAX || s->offset>INT64_MAX ||
                   (uint64_t)width>(uint64_t)INT64_MAX-s->offset) {
                    minias_set_error(as,"x86-branch-address-overflow");
                    return false;
                }
                distance=(int64_t)sym->value-((int64_t)s->offset+(int64_t)width);
                if((expr->addend>0 && distance>INT64_MAX-expr->addend) ||
                   (expr->addend<0 && distance<INT64_MIN-expr->addend)) {
                    minias_set_error(as,"x86-branch-addend-overflow");return false;
                }
                distance+=expr->addend;
                if((width==2U && (distance<-128 || distance>127)) ||
                   (width!=2U && (distance<INT32_MIN || distance>INT32_MAX))) {
                    minias_set_error(as,"x86-branch-displacement-range:line=%zu",
                                     s->line);
                    return false;
                }
                displacement=distance;
            }
            for(i=0U;i<(width==2U?1U:4U);++i)
                code[n++]=(unsigned char)((uint32_t)displacement>>(8U*i));
            if(!minias_section_append(as,s->section,code,n))return false;
            if(local)return true;
            return minias_add_fixup(as,s->section,s->offset+width-4U,kind,
                                    4U,expr->name,expr->addend-4);
        }
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            const MiniAsMCMemory *mem=m->operands[1].memory;
            unsigned base=m->encoding&UINT32_C(0xff);
            unsigned extra=m->opcode==X64_IMM_MEMORY?
                (base==0x83U || base==0xc6U || base==0x80U ||
                 base==0xf6U?1U:(m->encoding&0x400U)?2U:4U):0U;
            uint32_t immediate=extra?
                (uint32_t)m->operands[2].immediate:0U;
            unsigned low_base=mem->base&7U;
            unsigned bytes=x64_disp_bytes(mem);
            unsigned mod=!mem->has_base?0U:
                         bytes==0U?0U:bytes==1U?1U:2U;
            unsigned i;
            uint32_t disp=(uint32_t)mem->displacement;
            if(mem->address_space)
                code[n++]=(unsigned char)(mem->address_space==1U?0x64U:0x65U);
            if(m->encoding&0x400U)code[n++]=0x66U;
            r=m->operands[0].reg;
            if (m->operands[0].register_64 || r>=8U ||
                ((m->encoding&0x200U) && r>=4U) ||
                mem->base>=8U ||
                (mem->has_index && mem->index>=8U))
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((r>>3U)<<2U) |
                    ((mem->has_index?mem->index>>3U:0U)<<1U) |
                    (mem->base>>3U));
            /* Width markers 0x200/0x400 do not imply the 0F escape. */
            if(m->encoding&0x100U)code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)((((mem->pc_relative || mem->address_space)?0U:mod)<<6U)|
                ((r&7U)<<3U)|
                (mem->pc_relative?5U:
                 mem->address_space?4U:mem->has_index?4U:low_base));
            if(mem->address_space && !mem->pc_relative) {
                code[n++]=0x25U; /* SIB: no base/index, signed disp32 */
            } else if (!mem->pc_relative && (mem->has_index || low_base==4U)) {
                unsigned sib_scale=mem->scale==8U?3U:
                                   mem->scale==4U?2U:
                                   mem->scale==2U?1U:0U;
                code[n++]=(unsigned char)((sib_scale<<6U) |
                    ((mem->has_index?mem->index&7U:4U)<<3U) |
                    (mem->has_base?low_base:5U));
            }
            if(mem->address_space && !mem->pc_relative &&
               mem->symbol!=NULL) {
                size_t relocation_at=n;
                for(i=0U;i<4U;++i)code[n++]=0U;
                for(i=0U;i<extra;++i)
                    code[n++]=(unsigned char)(immediate>>(8U*i));
                if(!minias_section_append(as,s->section,code,n))return false;
                return minias_add_fixup(as,s->section,s->offset+relocation_at,
                            MINIAS_FIXUP_X64_ABS32S,4U,
                            mem->symbol->name,mem->symbol->addend);
            }
            if(mem->pc_relative && mem->symbol!=NULL) {
                MiniAsSymbol *sym=minias_get_symbol(as,mem->symbol->name,false);
                bool local=sym!=NULL && sym->defined && sym->section==s->section;
                if(local) {
                    int64_t distance;
                    if(sym->value>INT64_MAX || s->offset>INT64_MAX ||
                       s->offset>(uint64_t)INT64_MAX-(uint64_t)s->size) {
                        minias_set_error(as,"x86-rip-address-overflow");return false;
                    }
                    distance=(int64_t)sym->value-(int64_t)(s->offset+s->size);
                    if((mem->symbol->addend>0 &&
                        distance>INT64_MAX-mem->symbol->addend) ||
                       (mem->symbol->addend<0 &&
                        distance<INT64_MIN-mem->symbol->addend)) {
                        minias_set_error(as,"x86-rip-addend-overflow");return false;
                    }
                    distance+=mem->symbol->addend;
                    if(distance<INT32_MIN || distance>INT32_MAX) {
                        minias_set_error(as,"x86-rip-rel32-out-of-range");return false;
                    }
                    disp=(uint32_t)distance;
                }
                {
                    size_t relocation_at=n;
                    for(i=0U;i<4U;++i)code[n++]=(unsigned char)(disp>>(8U*i));
                    for(i=0U;i<extra;++i)
                        code[n++]=(unsigned char)(immediate>>(8U*i));
                    if(!minias_section_append(as,s->section,code,n))return false;
                    if(local)return true;
                    return minias_add_fixup(as,s->section,s->offset+relocation_at,
                        MINIAS_FIXUP_X64_PC32,4U,mem->symbol->name,
                        mem->symbol->addend-4-(int64_t)extra);
                }
            }
            for (i=0U;i<bytes;++i)
                code[n++]=(unsigned char)(disp>>(8U*i));
            for(i=0U;i<extra;++i)
                code[n++]=(unsigned char)(immediate>>(8U*i));
            break;
        }
        case X64_MOVABS_IMMREG: {
            const MiniAsMCOperand *value=&m->operands[1];
            unsigned i;
            r=m->operands[0].reg;
            code[n++]=(unsigned char)(0x48U|(r>>3U));
            code[n++]=(unsigned char)(0xb8U|(r&7U));
            for(i=0U;i<8U;++i)
                code[n++]=(unsigned char)((value->kind==MINIAS_MC_SYMBOL?
                    UINT64_C(0):value->immediate)>>(8U*i));
            if(!minias_section_append(as,s->section,code,n))return false;
            if(value->kind!=MINIAS_MC_SYMBOL)return true;
            return minias_add_fixup(as,s->section,s->offset+2U,
                MINIAS_FIXUP_X64_ABS64,8U,
                value->symbol->name,value->symbol->addend);
        }
        case X64_MOVQ_IMMREG: {
            const MiniAsMCOperand *src=&m->operands[1];
            unsigned i;
            r=m->operands[0].reg;
            code[n++]=(unsigned char)(0x48U | (r>>3U));
            code[n++]=0xc7U;
            code[n++]=(unsigned char)(0xc0U | (r&7U));
            for(i=0U;i<4U;++i)
                code[n++]=(unsigned char)(((uint32_t)
                    (src->kind==MINIAS_MC_SYMBOL?0U:src->immediate))>>(8U*i));
            if(!minias_section_append(as,s->section,code,n))return false;
            if(src->kind!=MINIAS_MC_SYMBOL)return true;
            return minias_add_fixup(as,s->section,s->offset+3U,
                MINIAS_FIXUP_X64_ABS32S,4U,src->symbol->name,src->symbol->addend);
        }
        case X64_ALU_IMM: {
            unsigned i,group=m->encoding&7U;
            uint64_t imm=m->operands[1].immediate;
            int64_t signed_imm=(int64_t)imm;
            bool short_imm=signed_imm>=-128 && signed_imm<=127;
            bool byte=(m->encoding&0x200U)!=0U;
            bool word=(m->encoding&0x400U)!=0U;
            bool accumulator;
            unsigned nbytes;
            r=m->operands[0].reg;
            accumulator=r==0U && (byte || (word && !short_imm));
            if(word)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U || (byte && r>=4U))
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) | (r>>3U));
            if(accumulator) {
                code[n++]=(unsigned char)(0x04U+(group<<3U)+(byte?0U:1U));
                nbytes=byte?1U:2U;
            } else {
                code[n++]=byte?0x80U:short_imm?0x83U:0x81U;
                code[n++]=(unsigned char)(0xc0U | (group<<3U) | (r&7U));
                nbytes=byte || short_imm?1U:word?2U:4U;
            }
            for(i=0U;i<nbytes;++i)
                code[n++]=(unsigned char)(imm>>(8U*i));
            break;
        }
        case X64_INDIRECT_REG:
            r=m->operands[0].reg;
            if(r>=8U)code[n++]=0x41U;
            code[n++]=0xffU;
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            break;
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
            if(m->encoding&0x400U)code[n++]=0x66U;
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
    /* Common .long/.quad and CFI anchors use semantic ABS, never a
     * target-private duplicate relocation kind. GNU ELF64 LP64 uses
     * R_X86_64_32 (zero extending) versus R_X86_64_32S (signed). */
    if(kind==MINIAS_FIXUP_ABS) {
        if(width==8U)return 1U;    /* R_X86_64_64 */
        if(width==4U)return 10U;   /* R_X86_64_32 */
        return 0U;
    }
    if(kind==MINIAS_FIXUP_X64_ABS64)
        return width==8U?1U:0U; /* R_X86_64_64 */
    if(width!=4U)return 0U;
    if(kind==MINIAS_FIXUP_X64_PC32 || kind==MINIAS_FIXUP_PREL32)
        return 2U;  /* R_X86_64_PC32 */
    if(kind==MINIAS_FIXUP_X64_PLT32)return 4U; /* R_X86_64_PLT32 */
    if(kind==MINIAS_FIXUP_X64_ABS32S)return 11U; /* R_X86_64_32S */
    return 0U;
}
static const MiniAsTargetOps ops={
    .name="x86_64",.comment_prefix='#',.elf_machine=62U,
    .resolve_profile=x64_profile,.parse_mcinst=x64_parse,
    .measure_mcinst=x64_measure,.encode=x64_encode,
    .relax_instruction=x64_relax_instruction,
    .expanded_instruction_size=x64_expanded_instruction_size,
    .append_code_alignment=x64_code_align,
    .fixup_relocation_type=x64_reloc,.validate_output=x64_validate,
    .word_directive_width=2U,.align_directive_bytes=true,
    .dot_relative_fixup=MINIAS_FIXUP_PREL32
};
const MiniAsTargetOps *minias_target_x86_64(void){return &ops;}
