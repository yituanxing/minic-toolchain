#include "minias_internal.h"
#include "x86_64_fixup_kinds.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A first, fail-closed GNU AT&T SysV x86-64 target. All common expression,
 * symbol, section, fragment, and ELF handling lives outside this file. */
#define X64_CODE32 UINT32_C(1)

enum { X64_ZERO = 1, X64_REG_BINARY, X64_STACK, X64_MOV_IMM32, X64_ENDBR64, X64_ALU_IMM, X64_REG_MEMORY, X64_BRANCH, X64_IMM_MEMORY, X64_MOVQ_IMMREG, X64_MOVABS_IMMREG, X64_SETCC, X64_BINARY8, X64_MOVSLQ, X64_MOVZX_REG, X64_SHIFT_IMM, X64_TEST8_IMM, X64_PUSH_IMM, X64_CMOV_REG, X64_SHIFT_CL, X64_UNARY, X64_INDIRECT_REG, X64_BSWAP, X64_TEST_IMM_REG, X64_IMUL_IMM, X64_BIT_IMMREG, X64_DOUBLE_SHIFT, X64_SYSTEM_MOVE, X64_BMI2_SHIFT };

/* AT&T addressing is an x86-specific machine operand; neither its ModR/M
 * register rules nor FS/GS selector have meaning in the shared MC model.
 * Common retains an opaque owning pointer and asks this Target to dispose
 * the nested symbolic displacement when the MCInst's lifetime ends. */
typedef struct X64Memory {
    bool has_base;
    bool pc_relative;
    unsigned address_space;
    unsigned base;
    bool has_index;
    unsigned index;
    unsigned scale;
    int64_t displacement;
    MiniAsSymbolExpr *symbol;
} X64Memory;

static void x64_dispose_memory(void *opaque) {
    X64Memory *memory = opaque;
    if (memory == NULL) return;
    free(memory->symbol);
    free(memory);
}

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

/* Low-byte registers are separate from legacy AH/CH/DH/BH. The latter
 * cannot coexist with any REX-required byte register. */
/* Source-width-specific registers belong to x86, not Common MCInst. */
/* XMM register indices remain Target-owned, not fake GPR widths. */
static bool x64_xmm_register(const char *text,unsigned *reg) {
    char *end;unsigned long id;
    if(strncmp(text,"%xmm",4U) || !isdigit((unsigned char)text[4]))return false;
    errno=0;id=strtoul(text+4U,&end,10);
    if(errno || *end!='\0' || id>=16UL)return false;
    *reg=(unsigned)id;return true;
}

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

/* Legacy AH/CH/DH/BH occupy ModR/M register IDs 4..7 *without* REX.
 * Keep this classification distinct from SPL/BPL/SIL/DIL in target ISA. */
static bool x64_high_register8(const char *name,unsigned *reg) {
    static const char *const high[]={"%ah","%ch","%dh","%bh"};
    unsigned i;
    for(i=0U;i<4U;++i)
        if(!strcmp(name,high[i])){*reg=4U+i;return true;}
    return false;
}

/* One target ISA descriptor for implicit-one, immediate and CL-count shifts;
 * never duplicate width-specific shift tables or Core emission logic. */
static bool x64_shift_descriptor(const char *op,unsigned *group,
                                 unsigned *bits) {
    size_t length=strlen(op);
    if(length<2U)return false;
    *bits=op[length-1U]=='b'?8U:op[length-1U]=='w'?16U:
          op[length-1U]=='l'?32U:op[length-1U]=='q'?64U:0U;
    if(!*bits)return false;
    if(length==4U && (!strncmp(op,"sal",3U) ||
                      !strncmp(op,"shl",3U))) *group=4U;
    else if(length==4U && !strncmp(op,"shr",3U))*group=5U;
    else if(length==4U && !strncmp(op,"sar",3U))*group=7U;
    else if(length==4U && !strncmp(op,"rol",3U))*group=0U;
    else if(length==4U && !strncmp(op,"ror",3U))*group=1U;
    else return false;
    return true;
}
static bool x64_shift_register(const char *s,unsigned bits,unsigned *reg,
                               bool *legacy_high) {
    bool wide=false;
    *legacy_high=false;
    if(bits==8U) {
        *legacy_high=x64_high_register8(s,reg);
        return *legacy_high || x64_register8(s,reg);
    }
    if(bits==16U)return x64_register16(s,reg);
    return x64_register(s,reg,&wide) && wide==(bits==64U);
}

/* One ISA-owned condition-code table shared by CMOV, SETcc, and Jcc aliases.
 * Extending a condition synonym must never require a second opcode table. */
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

/* x86 GCC tracepoint references exceed 48 characters; this is an operand
 * token capacity, not the GNU symbol-name limit. Preserve fail-closed caps. */
#define X64_OPERAND_CAP 320U
static bool x64_system_register(const char *s,unsigned *number,unsigned *kind) {
    static const struct {const char *name;unsigned reg;} segments[]={
        {"es",0U},{"cs",1U},{"ss",2U},{"ds",3U},{"fs",4U},{"gs",5U}
    };
    size_t i;
    if(!s || *s++!='%')return false;
    for(i=0U;i<sizeof(segments)/sizeof(segments[0]);++i)
        if(!strcmp(s,segments[i].name)) {
            *number=segments[i].reg;*kind=0U;return true;
        }
    if((s[0]=='c'&&s[1]=='r') || (s[0]=='d'&&s[1]=='b')) {
        bool control=s[0]=='c';
        unsigned reg=0U;
        const char *p=s+2U;
        if(!*p)return false;
        for(;*p;++p) {
            if(*p<'0' || *p>'9')return false;
            reg=reg*10U+(unsigned)(*p-'0');
            if(reg>15U)return false;
        }
        if(control?!(reg==0U||reg==2U||reg==3U||reg==4U||reg==8U):
                   reg>7U)return false;
        *kind=control?1U:2U;*number=reg;return true;
    }
    return false;
}
static bool x64_args2(const char *args, char a[X64_OPERAND_CAP],
                      char b[X64_OPERAND_CAP]) {
    char *out[2]={a,b};
    const size_t capacity[2]={X64_OPERAND_CAP,X64_OPERAND_CAP};
    unsigned n=0U;
    return minias_split_mc_operands(args,out,capacity,2U,&n) && n==2U;
}

/* Three GNU AT&T operands have two TOP-LEVEL commas, while the middle
 * memory operand may itself contain SIB commas. Keep this Target-owned
 * tokenizer shared by BMI2 and future three-operand x86 ISA families. */
static bool x64_args3(const char *args,
                      char first[X64_OPERAND_CAP],
                      char second[X64_OPERAND_CAP],
                      char third[X64_OPERAND_CAP]) {
    char *out[3]={first,second,third};
    const size_t capacity[3]={X64_OPERAND_CAP,X64_OPERAND_CAP,
                               X64_OPERAND_CAP};
    unsigned n=0U;
    return minias_split_mc_operands(args,out,capacity,3U,&n) && n==3U;
}

/* GNU AT&T disp(%base[, %index[, scale]]) plus symbol(%rip).
 * SymbolExpr remains typed all the way into the existing common ELF fixup.
 * The PC-relative addressing mode is target-owned (ModR/M rm=101). */
/* Target-only GNU AT&T address expression normalization. Core already
 * evaluates integer expressions and owns semantic ELF Fixups; x86 only
 * separates a displacement's optional one-symbol affine operand.
 * A number+symbol is equivalent to symbol+number in GNU GAS, and
 * parentheses around that expression do not change relocation kind. */
static char *x64_unwrap_disp(char *text) {
    for(;;) {
        size_t length=strlen(text),i;
        unsigned depth=0U;
        bool wraps=true;
        if(length<2U || text[0]!='(' || text[length-1U]!=')')
            return text;
        for(i=0U;i<length;++i) {
            if(text[i]=='(')++depth;
            else if(text[i]==')') {
                if(depth==0U)return text;
                --depth;
                if(depth==0U && i+1U!=length) {wraps=false;break;}
            }
        }
        if(!wraps || depth)return text;
        text[length-1U]='\0';
        ++text;
    }
}
static bool x64_symbolic_displacement(const char *raw,
                                      MiniAsSymbolExpr *expr) {
    char buf[X64_OPERAND_CAP],*operand,*p;
    size_t len=strlen(raw);
    unsigned depth=0U;
    if(!len || len>=sizeof(buf))return false;
    memcpy(buf,raw,len+1U);
    operand=x64_unwrap_disp(buf);
    if(minias_parse_symbol_addend(operand,expr))return true;
    for(p=operand;*p;++p) {
        int64_t leading;
        MiniAsSymbolExpr tail;
        if(*p=='(') {++depth;continue;}
        if(*p==')') {
            if(!depth)return false;
            --depth;continue;
        }
        if(*p!='+' || depth!=0U || p==operand)continue;
        *p='\0';
        if(!minias_eval_i64_expression(operand,&leading) ||
           !minias_parse_symbol_addend(x64_unwrap_disp(p+1U),&tail))
            return false;
        if((leading>0 && tail.addend>INT64_MAX-leading) ||
           (leading<0 && tail.addend<INT64_MIN-leading))
            return false;
        tail.addend+=leading;
        *expr=tail;
        return true;
    }
    return false;
}

static bool x64_parse_base_memory(const char *text, X64Memory *out) {
    /* GNU Kbuild emits horizontal space around AT&T displacement operators,
     * parentheses and even register prefix punctuation ("(% rip)").
     * Normalize these within this x86 target addressing grammar; reject
     * whitespace *inside* identifiers so invalid input cannot become
     * an accidentally legal symbol/register. Shared expressions remain
     * owned by the Common symbol/fixup engine. */
    char compact[X64_OPERAND_CAP];
    size_t used=0U;
    const char *scan;
    for(scan=text;*scan;++scan) {
        if(isspace((unsigned char)*scan)) {
            const char *next=scan+1U;
            unsigned char prior=used?(unsigned char)compact[used-1U]:0U;
            while(isspace((unsigned char)*next))++next;
            if((isalnum(prior) || prior=='_' || prior=='.') &&
               (isalnum((unsigned char)*next) || *next=='_' || *next=='.'))
                return false;
            continue;
        }
        if(used+1U>=sizeof(compact))return false;
        compact[used++]=*scan;
    }
    compact[used]='\0';
    text=compact;
    const char *open,*close,*p;
    unsigned segment_space=0U;
    char regs[3][48], dispbuf[X64_OPERAND_CAP], *end;
    size_t dlen;
    unsigned count=0U, base=0U, index=0U;
    unsigned long scale=1UL;
    bool wide, symbolic=false, pc_relative=false;
    MiniAsSymbolExpr symbolic_expr;
    long long displacement=0;
    if(strncmp(text,"%fs:",4U)==0)segment_space=1U;
    else if(strncmp(text,"%gs:",4U)==0)segment_space=2U;
    if(segment_space)text+=4U;
    /* Last '(' begins the actual AT&T base/index tuple; preceding pairs
     * may belong to a symbolic or numeric displacement expression. */
    open=strrchr(text,'(');
    if (!open) {
        unsigned space=0U;
        const char *operand;
        MiniAsSymbolExpr expr;
        space=segment_space;
        operand=text;
        /* AT&T absolute disp32 and absolute-symbol memory are legal
         * without FS/GS. Keep them distinct from RIP-relative addressing. */
        if(!*operand || *operand=='%' || *operand=='$')return false;
        errno=0;displacement=strtoll(operand,&end,0);
        if(errno || end==operand || *end ||
           displacement<INT32_MIN || displacement>INT32_MAX) {
            int64_t arithmetic;
            if(minias_eval_i64_expression(operand,&arithmetic) &&
               arithmetic>=INT32_MIN && arithmetic<=INT32_MAX)
                displacement=(long long)arithmetic;
            else {
                if(!x64_symbolic_displacement(operand,&expr))return false;
                symbolic=true;displacement=0;
            }
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
            int64_t arithmetic;
            if(minias_eval_i64_expression(dispbuf,&arithmetic) &&
               arithmetic>=INT32_MIN && arithmetic<=INT32_MAX)
                displacement=(long long)arithmetic;
            else {
                if(!x64_symbolic_displacement(dispbuf,&symbolic_expr) ||
                   symbolic_expr.addend<INT64_MIN+4)
                    return false;
                symbolic=true;
                displacement=0;
            }
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
        if(count<1U || !x64_register(regs[0],&base,&wide) || !wide)
            return false;
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
        /* GNU sym+addend(%base,%index,scale) uses an ELF ABS32S relocation
         * in a forced disp32 slot. This is distinct from RIP+PC32; target
         * uses existing typed memory and the same reloc/fixup producer. */
        out->symbol=malloc(sizeof(symbolic_expr));
        if(!out->symbol)return false;
        *out->symbol=symbolic_expr;
    }
    return true;
}

static unsigned x64_disp_bytes(const X64Memory *mem) {
    /* A symbol forces a 4-byte relocation slot even at a zero addend.
     * Otherwise a base register might choose a zero/disp8 ModR/M mode. */
    if(mem->symbol)return 4U;
    /* Segment overrides prefix an address, not its ModR/M/SIB mode. */
    if (mem->pc_relative || !mem->has_base)return 4U;
    if (mem->displacement==0 && (mem->base & 7U)!=5U) return 0U;
    return mem->displacement>=-128 && mem->displacement<=127 ? 1U : 4U;
}

static bool x64_is_mem_candidate(const char *text) {
    return strchr(text, '(') != NULL ||
           strncmp(text,"%gs:",4U)==0 || strncmp(text,"%fs:",4U)==0 ||
           (*text && *text!='%' && *text!='$');
}

static bool x64_parse(MiniAs *as, MiniAsStmt *s) {
    static const struct {const char *op; unsigned char bytes[3]; unsigned size;} zeros[]={
        {"nop",{0x90},1}, {"ret",{0xc3},1}, {"retq",{0xc3},1},
        {"leave",{0xc9},1}, {"leaveq",{0xc9},1},
        {"syscall",{0x0f,0x05},2}, {"int3",{0xcc},1},
        {"ud2",{0x0f,0x0b},2}, {"hlt",{0xf4},1},
        {"cli",{0xfa},1},{"sti",{0xfb},1},
        {"cld",{0xfc},1},{"std",{0xfd},1},
        {"clc",{0xf8},1},{"stc",{0xf9},1},{"cmc",{0xf5},1},
        {"stac",{0x0f,0x01,0xcb},3},
        {"clac",{0x0f,0x01,0xca},3},
        {"cpuid",{0x0f,0xa2},2},
        {"rdtsc",{0x0f,0x31},2},{"rdtscp",{0x0f,0x01,0xf9},3},
        {"rdmsr",{0x0f,0x32},2},{"wrmsr",{0x0f,0x30},2},
        {"rdpmc",{0x0f,0x33},2},{"fninit",{0xdb,0xe3},2},
        {"fnclex",{0xdb,0xe2},2},
        {"emms",{0x0f,0x77},2},
        {"xsetbv",{0x0f,0x01,0xd1},3},
        {"xgetbv",{0x0f,0x01,0xd0},3},
        {"vmcall",{0x0f,0x01,0xc1},3},
        {"vmmcall",{0x0f,0x01,0xd9},3},
        {"iretq",{0x48,0xcf},2},{"iretw",{0x66,0xcf},2},
        {"lretq",{0x48,0xcb},2},{"lret",{0xcb},1},
        /* GNU long-mode SYSRET aliases differ only by REX.W. */
        {"sysretq",{0x48,0x0f,0x07},3},
        {"sysretl",{0x0f,0x07},2},{"sysret",{0x0f,0x07},2},
        {"fwait",{0x9b},1},{"wait",{0x9b},1},
        {"cltd",{0x99},1},{"cwtl",{0x98},1},
        {"stosl",{0xab},1},{"stosq",{0x48,0xab},2},
        {"stosw",{0x66,0xab},2},{"movsb",{0xa4},1},
        {"movsw",{0x66,0xa5},2},
        {"movsl",{0xa5},1},{"movsq",{0x48,0xa5},2},
        {"stosb",{0xaa},1},
        {"lodsb",{0xac},1},{"lodsw",{0x66,0xad},2},
        {"lodsl",{0xad},1},{"lodsq",{0x48,0xad},2},
        {"scasb",{0xae},1},{"scasw",{0x66,0xaf},2},
        {"scasl",{0xaf},1},{"scasq",{0x48,0xaf},2},
        {"cmpsb",{0xa6},1},{"cmpsw",{0x66,0xa7},2},
        {"cmpsl",{0xa7},1},{"cmpsq",{0x48,0xa7},2},
        {"outsb",{0x6e},1},{"outsw",{0x66,0x6f},2},
        {"outsl",{0x6f},1},{"insb",{0x6c},1},
        {"insw",{0x66,0x6d},2},{"insl",{0x6d},1},{"sfence",{0x0f,0xae,0xf8},3},
        {"lfence",{0x0f,0xae,0xe8},3},{"mfence",{0x0f,0xae,0xf0},3},
        {"swapgs",{0x0f,0x01,0xf8},3},
        {"wbinvd",{0x0f,0x09},2},{"invd",{0x0f,0x08},2},
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
        {"popf",{0x9d},1},{"popfq",{0x9d},1}
    };
    static const struct {const char *op; unsigned opcode; bool wide;} binary[]={
        {"movq",0x89,true}, {"movl",0x89,false},
        {"addq",0x01,true}, {"addl",0x01,false},
        {"adcq",0x11,true},{"adcl",0x11,false},
        {"subq",0x29,true}, {"subl",0x29,false},
        {"sbbq",0x19,true}, {"sbbl",0x19,false},
        {"xorq",0x31,true}, {"xorl",0x31,false},
        {"andq",0x21,true}, {"andl",0x21,false},
        {"orq",0x09,true},  {"orl",0x09,false},
        {"cmpq",0x39,true},{"cmpl",0x39,false},
        {"testq",0x85,true},{"testl",0x85,false},
        {"movw",0x89,false},{"addw",0x01,false},{"adcw",0x11,false},
        {"subw",0x29,false},{"sbbw",0x19,false},
        {"xorw",0x31,false},{"andw",0x21,false},
        {"orw",0x09,false},{"cmpw",0x39,false},
        {"testw",0x85,false},
        /* Retain x86 ISA format in the same typed register/ModR/M path.
         * 0x100 marks 0F prefix; operand widths remain x86 Target-only. */
        {"btl",0x1a3U,false},{"btq",0x1a3U,true},
        {"btsl",0x1abU,false},{"btsq",0x1abU,true},
        {"btrl",0x1b3U,false},{"btrq",0x1b3U,true},
        {"btcl",0x1bbU,false},{"btcq",0x1bbU,true},
        {"xaddl",0x1c1U,false},{"xaddq",0x1c1U,true},
        {"xaddw",0x1c1U,false},{"btsw",0x1abU,false},
        {"btrw",0x1b3U,false},{"btcw",0x1bbU,false},
        {"btw",0x1a3U,false},{"cmpxchgw",0x1b1U,false},
        {"cmpxchgl",0x1b1U,false},{"cmpxchgq",0x1b1U,true}
    };
    /* reg/memory opcodes: source register to dest memory and vice versa.
     * TEST uses a commutative encoding; memory location is always r/m. */
    static const struct {
        const char *op; unsigned rm_dest, rm_src; bool wide;
    } regmem[] = {
        {"movq",0x89,0x8b,true},{"movl",0x89,0x8b,false},
        {"cmpq",0x39,0x3b,true},{"cmpl",0x39,0x3b,false},
        {"xchgq",0x87,0x87,true},{"xchgl",0x87,0x87,false},
        {"testq",0x85,0x85,true},{"testl",0x85,0x85,false},
        {"addq",0x01,0x03,true},{"addl",0x01,0x03,false},
        {"adcq",0x11,0x13,true},{"adcl",0x11,0x13,false},
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
        {"cmpxchgq",0xb1,0,true},{"cmpxchgl",0xb1,0,false},
        {"xaddw",0xc1,0,false},{"cmpxchgw",0xb1,0,false},
        {"cmpxchgb",0xb0,0,false},{"xaddb",0xc0,0,false},
        {"btw",0xa3,0,false},{"btsw",0xab,0,false},
        {"btrw",0xb3,0,false},{"btcw",0xbb,0,false},
        {"popcntq",0,0x1b8U,true},{"popcntl",0,0x1b8U,false},
        {"lzcntq",0,0x1bdU,true},{"lzcntl",0,0x1bdU,false},
        {"tzcntq",0,0x1bcU,true},{"tzcntl",0,0x1bcU,false},
        {"imulq",0,0x1afU,true},{"imull",0,0x1afU,false},
        /* Existing typed memory ModR/M encoder owns REX/SIB/PC32 for
         * 0F B6/B7 zero-extension and 63 sign-extension. */
        {"movzbl",0,0x1b6U,false},{"movzbq",0,0x1b6U,true},
        {"movzwl",0,0x1b7U,false},{"movzwq",0,0x1b7U,true},
        /* MOVSX is identical ModR/M ownership; only 0F BE/BF differs. */
        {"movsbl",0,0x1beU,false},{"movsbq",0,0x1beU,true},
        {"movsbw",0,0x1beU,false},{"movzbw",0,0x1b6U,false},
        {"movswl",0,0x1bfU,false},{"movswq",0,0x1bfU,true},
        {"movslq",0,0x63U,true},
        /* Explicit scalar widths, all reuse typed ModR/M, SIB and Fixup. */
        {"movb",0x88U,0x8aU,false},{"cmpb",0x38U,0x3aU,false},
        {"testb",0x84U,0x84U,false},{"addb",0x00U,0x02U,false},
        {"subb",0x28U,0x2aU,false},{"andb",0x20U,0x22U,false},
        {"adcb",0x10U,0x12U,false},{"sbbb",0x18U,0x1aU,false},
        {"orb",0x08U,0x0aU,false},{"xorb",0x30U,0x32U,false},
        {"xchgb",0x86U,0x86U,false},
        {"movw",0x89U,0x8bU,false},{"cmpw",0x39U,0x3bU,false},
        {"xchgw",0x87U,0x87U,false},
        {"testw",0x85U,0x85U,false},{"addw",0x01U,0x03U,false},
        {"subw",0x29U,0x2bU,false},{"andw",0x21U,0x23U,false},
        {"adcw",0x11U,0x13U,false},{"sbbw",0x19U,0x1bU,false},
        {"imulw",0,0x1afU,false},
        {"orw",0x09U,0x0bU,false},{"xorw",0x31U,0x33U,false}
    };
    MiniAsMCInst *m=minias_mcinst_alloc(3U);
    unsigned i, rs, rd;
    bool ws, wd;
    char src[X64_OPERAND_CAP],dst[X64_OPERAND_CAP];
    MiniAsStmt normalized, *const original=s;
    char suffixed_op[24];
    if (!m) {minias_set_error(as,"out-of-memory:x86_64-mcinst");return false;}
    m->dispose_memory=x64_dispose_memory;
    /* IA-32 mixed-mode within ELF64. Unvalidated code32 encodings fail
     * closed; these are the actual GNU EFI thunk PUSH/LRET instructions. */
    if((s->site.mode_bits&X64_CODE32)!=0U &&
       strcmp(s->op,"pushl") && strcmp(s->op,"lret")) {
        minias_set_error(as,"unsupported-x86-code32-operand:%s:line=%zu",
                         s->op,s->line);
        minias_mcinst_destroy(m);return false;
    }
    /* GNU REP BSF/BSR spell the F3-prefixed BMI bit-scan instructions.
     * Lower the alias via the SAME target-only TZCNT/LZCNT parser and
     * retained MCInst/REX/ModR/M/ELF fixup paths, without another
     * machine-code special case or mutable source-wide prefix state. */
    if(!strcmp(s->op,"rep") || !strcmp(s->op,"repz") ||
       !strcmp(s->op,"repe")) {
        const char *p=s->args,*suffix=NULL,*canonical=NULL;
        MiniAsStmt alias;
        while(*p==' ' || *p=='\t')++p;
        if(!strncmp(p,"bsfq",4U) && isspace((unsigned char)p[4]))
            {canonical="tzcntq";suffix=p+4U;}
        else if(!strncmp(p,"bsfl",4U) && isspace((unsigned char)p[4]))
            {canonical="tzcntl";suffix=p+4U;}
        else if(!strncmp(p,"bsrq",4U) && isspace((unsigned char)p[4]))
            {canonical="lzcntq";suffix=p+4U;}
        else if(!strncmp(p,"bsrl",4U) && isspace((unsigned char)p[4]))
            {canonical="lzcntl";suffix=p+4U;}
        if(canonical) {
            while(*suffix==' ' || *suffix=='\t')++suffix;
            if(!*suffix)goto invalid;
            alias=*s;alias.op=(char *)canonical;
            alias.args=(char *)suffix;alias.mcinst=NULL;
            minias_mcinst_destroy(m);
            if(!x64_parse(as,&alias))return false;
            original->mcinst=alias.mcinst;
            return true;
        }
    }
    /*
     * GNU AT&T instruction suffix inference is a target-only grammar rule.
     * Resolve the W/L/Q/B width from an explicit register operand, then
     * delegate to the *existing* typed MCInst decoder. Never infer memory
     * widths without a register: GAS defaults for ambiguous memory forms
     * are not a safe ISA contract.
     */
    {
        static const char *const inferred[]={
            "mov","cmp","test","add","sub","adc","sbb",
            "and","or","xor","push","pop",
            "shl","sal","shr","sar","rol","ror",
            "inc","dec","not","neg","mul","imul","div","idiv",
            "bt","bts","btr","btc","xadd","cmpxchg","shld","shrd"
        };
        bool alias=false;
        for(i=0U;i<sizeof(inferred)/sizeof(inferred[0]);++i)
            if(!strcmp(s->op,inferred[i])){alias=true;break;}
        if(alias) {
            char suffix=0;
            unsigned pass;
            for(pass=0U;pass<2U && !suffix;++pass) {
                const char *start=pass==0U?strrchr(s->args,','):s->args;
                const char *end=pass==0U?NULL:strchr(s->args,',');
                char operand[48];
                size_t length;
                unsigned reg;
                bool wide=false;
                if(start && pass==0U)++start;
                if(!start)start=s->args;
                while(isspace((unsigned char)*start))++start;
                length=end && end>=start?(size_t)(end-start):strlen(start);
                while(length && isspace((unsigned char)start[length-1U]))
                    --length;
                if(length==0U || length>=sizeof(operand))continue;
                memcpy(operand,start,length);operand[length]='\0';
                if(x64_register(operand,&reg,&wide))
                    suffix=wide?'q':'l';
                else if(x64_register16(operand,&reg))suffix='w';
                else if(x64_register8(operand,&reg) ||
                        x64_high_register8(operand,&reg))suffix='b';
            }
            if(suffix) {
                size_t length=strlen(s->op);
                if(length+2U>sizeof(suffixed_op)) {
                    minias_mcinst_destroy(m);return false;
                }
                memcpy(suffixed_op,s->op,length);
                suffixed_op[length]=suffix;
                suffixed_op[length+1U]='\0';
                normalized=*s;normalized.op=suffixed_op;s=&normalized;
            }
        }
    }
    /* Typed group-9 entropy generators, 0F C7 /6 or /7.  GNU permits
     * implicit destination width from the GPR, or explicit w/l/q. */
    if(!strncmp(s->op,"rdrand",6U) || !strncmp(s->op,"rdseed",6U)) {
        size_t len=strlen(s->op);
        unsigned reg;
        bool word=false,wide=false;
        /* GNU GAS infers RDRAND/RDSEED register size; w/l/q suffixed
         * spellings are not accepted by GNU, even if encodable. */
        if(len!=6U)goto invalid;
        if(x64_register16(s->args,&reg))word=true;
        else if(!x64_register(s->args,&reg,&wide))goto invalid;
        m->opcode=X64_UNARY;m->count=1U;
        m->encoding=UINT32_C(0xc7)|
            ((!strncmp(s->op,"rdrand",6U)?6U:7U)<<8U)|
            UINT32_C(0x8000)|(word?UINT32_C(0x2000):0U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=reg;
        m->operands[0].register_64=wide;
        goto done;
    }
    /* 0F C3 /r non-temporal integer store: the existing memory encoder
     * owns REX/SIB/segment and common semantic ELF fixups. */
    if(!strcmp(s->op,"movnti") || !strcmp(s->op,"movntil") ||
       !strcmp(s->op,"movntiq")) {
        X64Memory mem;
        unsigned reg;
        bool wide;
        if(!x64_args2(s->args,src,dst) ||
           !x64_register(src,&reg,&wide) ||
           (!strcmp(s->op,"movntil") && wide) ||
           (!strcmp(s->op,"movntiq") && !wide) ||
           !x64_is_mem_candidate(dst) ||
           !x64_parse_base_memory(dst,&mem))goto invalid;
        m->operands[1].memory=malloc(sizeof(mem));
        if(!m->operands[1].memory) {
            free(mem.symbol);minias_mcinst_destroy(m);
            minias_set_error(as,"out-of-memory:x86-movnti");
            return false;
        }
        *(X64Memory *)m->operands[1].memory=mem;
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=UINT32_C(0x100)|UINT32_C(0xc3);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=reg;
        m->operands[0].register_64=wide;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
    /* One XMM-memory ISA descriptor family: typed ModR/M, SIB and
     * Common PC32 fixups; 0F38 map is the sole extra escape flag. */
    {
        static const struct {const char *name;uint32_t op;bool store;} vec[]={
            {"movntdqa",UINT32_C(0x1052a),false},
            {"movntdq",UINT32_C(0x005e7),true},
            {"movdqa",UINT32_C(0x0056f),false},
            {"movdqa",UINT32_C(0x0057f),true},
            {"movdqu",UINT32_C(0x0216f),false},
            {"movdqu",UINT32_C(0x0217f),true},
            {"movaps",UINT32_C(0x00128),false},
            {"movaps",UINT32_C(0x00129),true},
            {"movups",UINT32_C(0x00110),false},
            {"movups",UINT32_C(0x00111),true}
        };
        size_t k;
        for(k=0U;k<sizeof(vec)/sizeof(vec[0]);++k) {
            X64Memory mem;unsigned xmm=0U;
            const char *memptr,*xmmtext;
            if(strcmp(s->op,vec[k].name))continue;
            if(!x64_args2(s->args,src,dst))continue;
            memptr=vec[k].store?dst:src;
            xmmtext=vec[k].store?src:dst;
            if(!x64_xmm_register(xmmtext,&xmm) ||
               !x64_is_mem_candidate(memptr) ||
               !x64_parse_base_memory(memptr,&mem))continue;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-xmm-mem");return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=vec[k].op;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=xmm;
            m->operands[0].register_64=false;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        }
    }
    /* BMI2 shifts share a typed three-register VEX target encoding.
     * GNU AT&T order is count, source, destination. The count register is
     * complemented in VEX.vvvv; all three operands must share width. */
    if(!strncmp(s->op,"shlx",4U) ||
       !strncmp(s->op,"shrx",4U) ||
       !strncmp(s->op,"sarx",4U)) {
        char first[X64_OPERAND_CAP];
        char source[X64_OPERAND_CAP],dest[X64_OPERAND_CAP];
        unsigned count_reg=0U,source_reg=0U,dest_reg=0U;
        unsigned pp=s->op[1]=='h'?(s->op[2]=='l'?1U:3U):2U;
        bool wc=false,source_wide=false,dest_wide=false,ok=false;
        bool memory=false;
        X64Memory mem;
        size_t len=strlen(s->op);
        /* GNU third operands may contain SIB commas. Reuse the
         * original top-level AT&T splitter twice, so the instruction
         * grammar never mistakes (%r15,%r13,4) for extra operands. */
        if(x64_args3(s->args,first,source,dest) &&
           x64_register(first,&count_reg,&wc) &&
           x64_register(dest,&dest_reg,&dest_wide) &&
           wc==dest_wide &&
           (len==4U || (len==5U && s->op[4]==(dest_wide?'q':'l')))) {
            if(x64_register(source,&source_reg,&source_wide))
                ok=source_wide==dest_wide;
            else if(x64_is_mem_candidate(source) &&
                    x64_parse_base_memory(source,&mem)) {
                memory=true;ok=true;
            }
        }
        if(!ok)goto invalid;
        if(memory) {
            /* Keep VEX map/pp in target encoding; reuse the existing
             * typed memory emitter, ModR/M, SIB and ELF PC32/ABS32S.
             * operands[0] = destination, [1] = memory, [2] = count. */
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-bmi2-memory");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=3U;
            m->encoding=UINT32_C(0x40000)|pp;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=dest_reg;
            m->operands[0].register_64=dest_wide;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            m->operands[2].kind=MINIAS_MC_REGISTER;
            m->operands[2].reg=count_reg;
        } else {
            m->opcode=X64_BMI2_SHIFT;m->count=3U;
            m->encoding=pp;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=count_reg;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=source_reg;
            m->operands[2].kind=MINIAS_MC_REGISTER;
            m->operands[2].reg=dest_reg;
            m->operands[2].register_64=dest_wide;
        }
        goto done;
    }

    /* ANDN and BZHI are BMI1/BMI2 VEX.0F38 three-register families.
     * GNU AT&T order is input, input, destination. ANDN's VEX.vvvv
     * encodes input #2, while BZHI's VEX.vvvv encodes input #1.
     * Reuse the established BMI2 VEX typed register MCInst; the
     * 0x10000 target encoding marker selects F2/F5 instead of F7. */
    if(!strcmp(s->op,"andn") || !strcmp(s->op,"andnl") ||
       !strcmp(s->op,"andnq") || !strcmp(s->op,"bzhi") ||
       !strcmp(s->op,"bzhil") || !strcmp(s->op,"bzhiq")) {
        char first[X64_OPERAND_CAP],source[X64_OPERAND_CAP];
        char dest[X64_OPERAND_CAP];
        unsigned r0=0U,r1=0U,bmi_dst=0U;
        bool w0=false,w1=false,bmi_wide=false;
        bool andn=!strncmp(s->op,"andn",4U);
        size_t name_len=strlen(s->op);
        if(!x64_args3(s->args,first,source,dest) ||
           !x64_register(first,&r0,&w0) ||
           !x64_register(source,&r1,&w1) ||
           !x64_register(dest,&bmi_dst,&bmi_wide) ||
           w0!=bmi_wide || w1!=bmi_wide ||
           !(name_len==(andn?4U:4U) ||
             (name_len==5U && s->op[4]==(bmi_wide?'q':'l'))))goto invalid;
        m->opcode=X64_BMI2_SHIFT;m->count=3U;
        m->encoding=UINT32_C(0x10000)|
                    ((andn?UINT32_C(0xf2):UINT32_C(0xf5))<<8U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=andn?r1:r0;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=andn?r0:r1;
        m->operands[2].kind=MINIAS_MC_REGISTER;
        m->operands[2].reg=bmi_dst;
        m->operands[2].register_64=bmi_wide;
        goto done;
    }
    /*
     * GNU prefetch/cache-maintenance family is memory-only. Encode it via
     * the existing target MC memory ModR/M, SIB, segment and ELF PC32 path.
     * /n is the opcode extension in the ModR/M reg field (not a GPR).
     */
    {
        static const struct {const char *name;uint32_t opcode;
                             unsigned extension;bool prefix66;} hints[]={
            {"prefetchnta",0x18U,0U,false},
            {"prefetcht0",0x18U,1U,false},
            {"prefetcht1",0x18U,2U,false},
            {"prefetcht2",0x18U,3U,false},
            {"prefetchw",0x0dU,1U,false},
            {"prefetchwt1",0x0dU,2U,false},
            {"clflush",0xaeU,7U,false},
            {"clflushopt",0xaeU,7U,true},
            {"clwb",0xaeU,6U,true}
        };
        for(i=0U;i<sizeof(hints)/sizeof(hints[0]);++i) {
            X64Memory mem;
            if(strcmp(s->op,hints[i].name)!=0)continue;
            if(!x64_is_mem_candidate(s->args) ||
               !x64_parse_base_memory(s->args,&mem))goto invalid;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-prefetch");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x100)|hints[i].opcode|
                        (hints[i].prefix66?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=hints[i].extension;
            m->operands[0].register_64=false;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        }
    }
    /* INVPCID's address is an M128 memory source, destination an
     * encoded GPR64 (REX.R without REX.W). Use the existing x86
     * 66/0F38 ModR/M/SIB target path and semantic PC32 relocation.
     * Do not copy the ELF writer or pretend it is a unary opcode. */
    if(!strcmp(s->op,"invpcid")) {
        X64Memory mem;
        unsigned destreg;
        bool wide;
        if(!x64_args2(s->args,src,dst) ||
           !x64_is_mem_candidate(src) ||
           !x64_register(dst,&destreg,&wide) || !wide ||
           !x64_parse_base_memory(src,&mem))goto invalid;
        m->operands[1].memory=malloc(sizeof(mem));
        if(!m->operands[1].memory) {
            free(mem.symbol);minias_mcinst_destroy(m);
            minias_set_error(as,"out-of-memory:x86-invpcid");
            return false;
        }
        *(X64Memory *)m->operands[1].memory=mem;
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=UINT32_C(0x10000)|UINT32_C(0x100)|
                    UINT32_C(0x400)|UINT32_C(0x82);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=destreg;
        m->operands[0].register_64=false;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
    /* Real kernel x87 scalar memory operations reuse the *same*
     * typed ModR/M + PC32 fixup producer as integer memory operations.
     * The opcode-extension digit is not an FP register. */
    {
        static const struct {const char *name;uint32_t opcode;
                             unsigned group;} fpmem[]={
            {"fldl",UINT32_C(0xdd),0U},
            {"fildl",UINT32_C(0xdb),0U},
            /* x87 m64real DD and DC arithmetic families use the same
             * typed ModR/M group opcode and PC32 ELF relocation. */
            {"faddl",UINT32_C(0xdc),0U},
            {"fmull",UINT32_C(0xdc),1U},
            {"fsubrl",UINT32_C(0xdc),5U},
            {"fsubl",UINT32_C(0xdc),4U},
            {"fdivl",UINT32_C(0xdc),6U},
            {"fdivrl",UINT32_C(0xdc),7U},
            {"fstpl",UINT32_C(0xdd),3U},
            {"fistpl",UINT32_C(0xdb),3U}
        };
        unsigned k;
        for(k=0U;k<sizeof(fpmem)/sizeof(fpmem[0]);++k) {
            X64Memory mem;
            if(strcmp(s->op,fpmem[k].name))continue;
            if(!x64_is_mem_candidate(s->args) ||
               !x64_parse_base_memory(s->args,&mem))goto invalid;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-x87-memory");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=fpmem[k].opcode;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=fpmem[k].group;
            m->operands[0].register_64=false;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        }
    }
    /* GNU LAR/LSL descriptor access: 0F02/0F03, target-owned 16-bit
     * selector source and destination operand-size rule. Both register
     * and memory sources reuse typed binary/memory MC emitters and
     * ELF PC32 fixups. REX.W is never emitted for GPR64 aliases. */
    if(!strcmp(s->op,"lar") || !strcmp(s->op,"lsl")) {
        unsigned sys_source=0U,sys_destination=0U;
        bool srcword=false,dstword=false,srcwide=false,dstwide=false;
        if(!x64_args2(s->args,src,dst))goto invalid;
        dstword=x64_register16(dst,&sys_destination);
        if(!dstword && !x64_register(dst,&sys_destination,&dstwide))goto invalid;
        srcword=x64_register16(src,&sys_source);
        if(!srcword && x64_register(src,&sys_source,&srcwide)) {
            if(dstword)goto invalid;
        } else if(srcword) {
            /* A 16-bit selector in 32/64-bit destination form is GNU
             * legal and does not itself request the 66 prefix. */
        } else if(x64_is_mem_candidate(src)) {
            X64Memory mem;
            if(!x64_parse_base_memory(src,&mem))goto invalid;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-lar-lsl");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x100)|
                        (dstword?UINT32_C(0x400):0U)|
                        (!strcmp(s->op,"lar")?2U:3U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=sys_destination;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        } else goto invalid;
        m->opcode=X64_REG_BINARY;m->count=2U;
        m->encoding=UINT32_C(0x100)|UINT32_C(0x800)|
                    (dstword?UINT32_C(0x400):0U)|
                    (!strcmp(s->op,"lar")?2U:3U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=sys_source;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=sys_destination;
        goto done;
    }
    /* GCC x86 CPU-local descriptor table and invalidation instructions
     * use 0F00/0F01 with a constant /digit ModR/M extension. Reuse the
     * existing relocation-capable memory encoder for all families. */
    {
        static const struct {const char *name;uint8_t opcode;uint8_t group;} sysmem[]={
            {"sgdt",0x01U,0U},{"sidt",0x01U,1U},
            {"lgdt",0x01U,2U},{"lidt",0x01U,3U},
            {"smsw",0x01U,4U},{"lmsw",0x01U,6U},
            {"invlpg",0x01U,7U},
            {"sldt",0x00U,0U},{"str",0x00U,1U},
            {"lldt",0x00U,2U},{"ltr",0x00U,3U},
            {"verr",0x00U,4U},{"verw",0x00U,5U},
            {"fxsave",0xaeU,0U},{"fxsaveq",0xaeU,0U},
            {"fxrstor",0xaeU,1U},{"fxrstorq",0xaeU,1U},
            {"ldmxcsr",0xaeU,2U},{"stmxcsr",0xaeU,3U},
            {"cmpxchg8b",0xc7U,1U},{"cmpxchg16b",0xc7U,1U},
            {"fldcw",0xd9U,5U},{"fnstcw",0xd9U,7U},
            {"fnstsw",0xddU,7U}
        };
        for(i=0U;i<sizeof(sysmem)/sizeof(sysmem[0]);++i) {
            X64Memory mem;
            if(strcmp(s->op,sysmem[i].name)!=0)continue;
            /* 0F 00 LTR/LLDT/STR/VERR/... also encode a register as
             * ModR/M mod=11; use the same typed unary MCInst as RDRAND.
             * Long-mode GNU uses 66 for extended 16-bit GPR forms. */
            if(sysmem[i].opcode==0U) {
                unsigned reg=0U;
                bool word=x64_register16(s->args,&reg);
                bool wide=false;
                /* GNU accepts AX/EAX/RAX aliases for descriptor r/m16.
                 * Only SLDT and STR preserve a 66 prefix for explicit
                 * 16-bit destinations; LLDT/LTR/VERR/VERW never do.
                 * REX.W is ignored for these opcodes even with RAX. */
                if(word || x64_register(s->args,&reg,&wide)) {
                    bool emit66=word && sysmem[i].group<=1U;
                    m->opcode=X64_UNARY;m->count=1U;
                    m->encoding=UINT32_C(0x8000)|
                        (uint32_t)sysmem[i].opcode|
                        ((uint32_t)sysmem[i].group<<8U)|
                        (emit66?UINT32_C(0x2000):0U);
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=reg;
                    goto done;
                }
            }
            if(!x64_is_mem_candidate(s->args) ||
               !x64_parse_base_memory(s->args,&mem))goto invalid;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-system-memory");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=(sysmem[i].opcode==0xd9U ||
                         sysmem[i].opcode==0xddU?0U:UINT32_C(0x100))|
                        sysmem[i].opcode;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=sysmem[i].group;
            m->operands[0].register_64=
                !strcmp(s->op,"fxsaveq") || !strcmp(s->op,"fxrstorq") ||
                !strcmp(s->op,"cmpxchg16b");
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        }
    }
    /* FS/GS base accesses use mandatory F3 + 0F AE /0..3 with the
     * size determined by the GPR; retain x86 target-owned ISA grammar,
     * and reuse the typed unary register encoder (including REX.W). */
    {
        static const char *const names[4]={
            "rdfsbase","rdgsbase","wrfsbase","wrgsbase"
        };
        for(i=0U;i<4U;++i) {
            unsigned reg;
            bool wide;
            if(strcmp(s->op,names[i]))continue;
            if(!x64_register(s->args,&reg,&wide))goto invalid;
            m->opcode=X64_UNARY;m->count=1U;
            m->encoding=UINT32_C(0x10000)|UINT32_C(0x8000)|
                UINT32_C(0xae)|((uint32_t)i<<8U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=reg;
            m->operands[0].register_64=wide;
            goto done;
        }
    }
    /* SHRD/SHLD reg-reg signed imm8 or implicit CL are the same typed
     * ModR/M 0F AC/AD and 0F A4/A5 ISA group. This shares ordinary
     * GPR register identification and does not fork Common ELF handling. */
    if(!strcmp(s->op,"shrdq") || !strcmp(s->op,"shrdl") ||
       !strcmp(s->op,"shrdw") || !strcmp(s->op,"shldq") ||
       !strcmp(s->op,"shldl") || !strcmp(s->op,"shldw")) {
        const char *comma;
        bool use_imm=false,src_wide=false,dst_wide=false;
        unsigned src_reg,dst_reg;
        uint64_t imm=0U;
        const bool word=s->op[4]=='w',qword=s->op[4]=='q';
        const bool shrd=s->op[2]=='r';
        if(s->args[0]=='$') {
            char literal[48];
            size_t length;
            comma=strchr(s->args,',');
            if(!comma)goto invalid;
            length=(size_t)(comma-s->args);
            if(length<2U || length>=sizeof(literal))goto invalid;
            memcpy(literal,s->args,length);literal[length]='\0';
            if(!minias_eval_u64_expression(literal+1U,&imm) ||
               (int64_t)imm < -256 || (int64_t)imm > 255)
                goto invalid;
            if(!x64_args2(comma+1U,src,dst))goto invalid;
            use_imm=true;
        }else if(!x64_args2(s->args,src,dst))goto invalid;
        if(word) {
            if(!x64_register16(src,&src_reg) ||
               !x64_register16(dst,&dst_reg))goto invalid;
        }else if(!x64_register(src,&src_reg,&src_wide) ||
                 !x64_register(dst,&dst_reg,&dst_wide) ||
                 src_wide!=dst_wide || src_wide!=qword)goto invalid;
        m->opcode=X64_DOUBLE_SHIFT;m->count=3U;
        m->encoding=(shrd?(use_imm?0xacU:0xadU):
                            (use_imm?0xa4U:0xa5U))|
                    (word?0x400U:0U)|(use_imm?0x200U:0U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=src_reg;
        m->operands[0].register_64=qword;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=dst_reg;
        m->operands[1].register_64=qword;
        m->operands[2].kind=MINIAS_MC_IMMEDIATE;
        m->operands[2].immediate=imm;
        goto done;
    }
    /* GNU .code64 stack-memory aliases reuse existing Group-5 /6 and
     * Group-1A /0 ModR/M pipeline. */
    if((!strcmp(s->op,"push") || !strcmp(s->op,"pop")) &&
       x64_is_mem_candidate(s->args) && s->args[0]!='$') {
        normalized=*s;
        normalized.op=!strcmp(s->op,"push")?"pushq":"popq";
        s=&normalized;
    }
    /* Linux entry and KVM control/debug/segment GPR transfers use one
     * retained ISA-typed ModR/M instruction. No Common encoder changes. */
    if((!strcmp(s->op,"mov") || !strcmp(s->op,"movq") ||
        !strcmp(s->op,"movl") || !strcmp(s->op,"movw")) &&
       x64_args2(s->args,src,dst)) {
        unsigned sys=0U,kind=0U,gpr=0U;
        bool sys_source=x64_system_register(src,&sys,&kind);
        bool sys_dest=false,gpr64=false,gpr16=false;
        if(!sys_source)sys_dest=x64_system_register(dst,&sys,&kind);
        if(sys_source || sys_dest) {
            const char *gp=sys_source?dst:src;
            bool gpr_ok=x64_register(gp,&gpr,&gpr64);
            if(!gpr_ok) {
                gpr_ok=x64_register16(gp,&gpr);
                gpr16=gpr_ok;
            }
            if(!gpr_ok && kind==0U &&
               (!strcmp(s->op,"movw") || !strcmp(s->op,"mov")) &&
               x64_is_mem_candidate(gp)) {
                /* GNU segment MOV 8C /r (seg -> memory) and 8E /r
                 * (memory -> seg) share target ModR/M and Common
                 * symbolic relocation machinery with other operands.
                 * Loading CS is architecturally invalid. */
                X64Memory mem;
                if((!sys_source && sys==1U) ||
                   !x64_parse_base_memory(gp,&mem))goto invalid;
                m->operands[1].memory=malloc(sizeof(mem));
                if(!m->operands[1].memory){
                    free(mem.symbol);minias_mcinst_destroy(m);
                    minias_set_error(as,"out-of-memory:x86-seg-memory");
                    return false;
                }
                *(X64Memory *)m->operands[1].memory=mem;
                m->opcode=X64_REG_MEMORY;m->count=2U;
                m->encoding=sys_source?UINT32_C(0x8c):UINT32_C(0x8e);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=sys;
                m->operands[0].register_64=false;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                goto done;
            }
            if(!gpr_ok || (kind!=0U && (gpr16 || !gpr64)) ||
               (kind==0U && !sys_source && sys==1U) ||
               (!strcmp(s->op,"movw") && !gpr16) ||
               (!strcmp(s->op,"movl") && (gpr16 || gpr64)) ||
               (!strcmp(s->op,"movq") && (gpr16 || (!gpr64 && kind!=0U))))
                goto invalid;
            m->opcode=X64_SYSTEM_MOVE;m->count=2U;
            m->encoding=(kind==0U?(sys_source?0x8cU:0x8eU):
                         kind==1U?(sys_source?0x20U:0x22U):
                                   (sys_source?0x21U:0x23U))|
                        (kind==0U?0U:0x100U)|
                        (kind==0U && gpr16 && sys_source?0x400U:0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=sys;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=gpr;
            goto done;
        }
    }
    /* GNU WAITPKG TPAUSE/UMWAIT/UMONITOR use a mandatory legacy
     * prefix + 0F AE /digit and a typed register r/m operand.
     * The opcode extension is NOT a source GPR and must never emit
     * REX.W; allow REX.B for registers 8..15. */
    if(!strcmp(s->op,"tpause") || !strcmp(s->op,"umwait") ||
       !strcmp(s->op,"umonitor")) {
        bool is_monitor=!strcmp(s->op,"umonitor");
        unsigned wait_reg;
        bool wait_wide=false;
        if(!x64_register(s->args,&wait_reg,&wait_wide) ||
           wait_wide!=is_monitor)goto invalid;
        m->opcode=X64_UNARY;m->count=1U;
        m->encoding=UINT32_C(0x8000)|UINT32_C(0xae)|
            (is_monitor?UINT32_C(0x10000)|UINT32_C(0x600):
             !strcmp(s->op,"tpause")?
               UINT32_C(0x2000)|UINT32_C(0x600):
               UINT32_C(0x20000)|UINT32_C(0x600));
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=wait_reg;
        m->operands[0].register_64=false;
        goto done;
    }
    if (!strcmp(s->op,"endbr64") && !*s->args) {
        m->opcode=X64_ENDBR64;m->encoding=UINT32_C(0xfa1e0ff3);
        m->count=0U;goto done;
    }
    /* All GNU REP/REPE/REPNE string forms use the canonical zero-arg
     * opcode descriptor; operand-size 66 precedes REP, whereas REX.W
     * follows REP. No duplicate hard-coded REP STOS/MOVS implementations.
     * Common retains the entire prefix+opcode as one sized MC fragment. */
    if(!strcmp(s->op,"rep") || !strcmp(s->op,"repz") ||
       !strcmp(s->op,"repe") || !strcmp(s->op,"repne") ||
       !strcmp(s->op,"repnz")) {
        unsigned char prefix=(!strcmp(s->op,"repne") ||
                              !strcmp(s->op,"repnz"))?0xf2U:0xf3U;
        for(i=0U;i<sizeof(zeros)/sizeof(zeros[0]);++i) {
            const char *name=zeros[i].op;
            unsigned char bytes[3]={0U,0U,0U};
            unsigned n=0U,j;
            if(strcmp(s->args,name))continue;
            if(strncmp(name,"movs",4U) && strncmp(name,"stos",4U) &&
               strncmp(name,"scas",4U) && strncmp(name,"cmps",4U) &&
               strncmp(name,"lods",4U) && strncmp(name,"outs",4U) &&
               strncmp(name,"ins",3U))break;
            if(zeros[i].size>2U)break;
            if(zeros[i].bytes[0]==0x66U)bytes[n++]=0x66U;
            bytes[n++]=prefix;
            for(j=bytes[0]==0x66U?1U:0U;j<zeros[i].size;++j)
                bytes[n++]=zeros[i].bytes[j];
            m->opcode=X64_ZERO;m->count=0U;
            m->encoding=(unsigned)bytes[0]|
                        ((unsigned)bytes[1]<<8U)|
                        ((unsigned)bytes[2]<<16U);
            m->operands[0].immediate=n;
            goto done;
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
    /* GNU IMUL $imm,reg[,reg] : use 6B sign-extended imm8 or 69 imm16/32.
     * Decode into typed operands; F7 /5 and 0F AF use their existing
     * unary and binary target encoders without duplicating ALU machinery. */
    if(s->args[0]=='$' &&
       (!strcmp(s->op,"imulq") || !strcmp(s->op,"imull") ||
        !strcmp(s->op,"imulw") || !strcmp(s->op,"imul"))) {
        const char *comma=strchr(s->args,',');
        char immtext[48],from[X64_OPERAND_CAP],to[X64_OPERAND_CAP];
        size_t length;
        unsigned imul_src,imul_dst;
        bool imul_ws=false,imul_wd=false;
        bool word=!strcmp(s->op,"imulw");
        uint64_t bits;
        if(!comma)goto invalid;
        length=(size_t)(comma-s->args);
        while(length && isspace((unsigned char)s->args[length-1U]))
            --length;
        if(length<2U || length>=sizeof(immtext))goto invalid;
        memcpy(immtext,s->args,length);
        immtext[length]='\0';
        if(strchr(comma+1,',')!=NULL) {
            if(!x64_args2(comma+1,from,to))goto invalid;
        }else {
            char tmp[X64_OPERAND_CAP];
            if(!x64_args2(s->args,tmp,to))goto invalid;
            memcpy(from,to,strlen(to)+1U);
        }
        if(!minias_eval_u64_expression(immtext+1,&bits) ||
           (int64_t)bits<INT32_MIN ||
           (int64_t)bits>(word?65535:
               !strcmp(s->op,"imulq")?INT32_MAX:UINT32_MAX))
            goto invalid;
        /* Three-operand IMUL permits r/m source; share Group-1 target
         * immediate memory ModR/M/SIB/PC32 encoding with 69/6B opcode. */
        if(x64_is_mem_candidate(from)) {
            X64Memory mem;
            bool destwide=false;
            if(!(word?x64_register16(to,&imul_dst):
                 x64_register(to,&imul_dst,&destwide) &&
                 (!strcmp(s->op,"imul") ||
                  destwide==(!strcmp(s->op,"imulq")))) ||
               !x64_parse_base_memory(from,&mem))goto invalid;
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-imul-mem");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_IMM_MEMORY;m->count=3U;
            m->encoding=((int64_t)bits>=-128 && (int64_t)bits<=127?
                          UINT32_C(0x6b):UINT32_C(0x69))|
                         (word?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=imul_dst;
            m->operands[0].register_64=!word && destwide;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=bits;
            goto done;
        }
        if(word) {
            if(!x64_register16(from,&imul_src) ||
               !x64_register16(to,&imul_dst))goto invalid;
        }else {
            if(!x64_register(from,&imul_src,&imul_ws) ||
               !x64_register(to,&imul_dst,&imul_wd) || imul_ws!=imul_wd ||
               (!strcmp(s->op,"imulq") && !imul_ws) ||
               (!strcmp(s->op,"imull") && imul_ws))goto invalid;
        }
        m->opcode=X64_IMUL_IMM;m->count=3U;
        m->encoding=word?UINT32_C(0x400):0U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=imul_src;
        m->operands[0].register_64=!word && imul_ws;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=imul_dst;
        m->operands[1].register_64=!word && imul_wd;
        m->operands[2].kind=MINIAS_MC_IMMEDIATE;
        m->operands[2].immediate=bits;
        goto done;
    }
    /* SETcc r/m8: ISA-owned condition with existing typed memory,
     * ModR/M /0, segment, SIB and ELF PC32 fixup. */
    if(!strncmp(s->op,"set",3U) && x64_is_mem_candidate(s->args)) {
        unsigned cc;
        X64Memory mem;
        if(x64_cond_code(s->op+3U,&cc) &&
           x64_parse_base_memory(s->args,&mem)) {
            m->operands[1].memory=malloc(sizeof(mem));
            if(!m->operands[1].memory) {
                free(mem.symbol);minias_mcinst_destroy(m);
                minias_set_error(as,"out-of-memory:x86-setcc-mem");
                return false;
            }
            *(X64Memory *)m->operands[1].memory=mem;
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x300)|UINT32_C(0x90)|cc;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=0U;
            m->operands[1].kind=MINIAS_MC_MEMORY;
            goto done;
        }
    }
    /* GNU IN/OUT port I/O through DX has an implicit accumulator.
     * Encode as a validated fixed-size target instruction, not as a
     * permissive pseudo-op or a new Common machine-code owner. */
    if(!strcmp(s->op,"inb") || !strcmp(s->op,"inw") ||
       !strcmp(s->op,"inl") || !strcmp(s->op,"outb") ||
       !strcmp(s->op,"outw") || !strcmp(s->op,"outl")) {
        char from[X64_OPERAND_CAP],to[X64_OPERAND_CAP];
        bool input=s->op[0]=='i';
        /* INB/INW/INL and OUTB/OUTW/OUTL differ in mnemonic length. */
        char width=s->op[strlen(s->op)-1U];
        const char *acc=width=='b'?"%al":width=='w'?"%ax":"%eax";
        if(!x64_args2(s->args,from,to))goto invalid;
        if((input?from:to)[0]=='$' &&
           !strcmp(input?to:from,acc)) {
            uint64_t port;
            if(!minias_eval_u64_expression((input?from:to)+1U,&port)
               || port>255U)goto invalid;
            m->opcode=X64_ZERO;
            m->encoding=width=='w'?UINT32_C(0x66):0U;
            m->encoding|=(input?(width=='b'?UINT32_C(0xe4):
                                          UINT32_C(0xe5)):
                                  (width=='b'?UINT32_C(0xe6):
                                          UINT32_C(0xe7)))<<(width=='w'?8U:0U);
            m->encoding|=(uint32_t)port<<(width=='w'?16U:8U);
            m->operands[0].immediate=width=='w'?3U:2U;
            goto done;
        }
        /* GNU GAS canonicalizes the DX port register as (%dx) in
         * inline-asm and accepts both %dx and (%dx) spellings. This
         * is a port operand, not a normal memory address. */
        if((strcmp(input?from:to,"%dx") &&
            strcmp(input?from:to,"(%dx)")) ||
           strcmp(input?to:from,acc))goto invalid;
        m->opcode=X64_ZERO;
        m->encoding=width=='w'?UINT32_C(0x66):0U;
        m->encoding|=(input?(width=='b'?UINT32_C(0xec):UINT32_C(0xed)):
                            (width=='b'?UINT32_C(0xee):UINT32_C(0xef)))
                     <<(width=='w'?8U:0U);
        m->operands[0].immediate=width=='w'?2U:1U;
        goto done;
    }
    /* x87 stack-pop arithmetic family (DE /n, st(0) source).
     * The target ISA group field is an opcode extension, not a GPR.
     * Reuse the proven fixed-word instruction fragment; do not let
     * the generic x86 memory grammar reinterpret %st(N) as an address. */
    {
        static const struct {const char *name;unsigned group;} ops[]={
            {"faddp",0xc0U},{"fmulp",0xc8U},
            {"fsubp",0xe0U},{"fsubrp",0xe8U},
            {"fdivp",0xf0U},{"fdivrp",0xf8U}
        };
        unsigned k;
        for(k=0U;k<sizeof(ops)/sizeof(ops[0]);++k) {
            unsigned reg=1U;
            if(strcmp(s->op,ops[k].name))continue;
            if(*s->args) {
                char st0[X64_OPERAND_CAP],sti[X64_OPERAND_CAP];
                const char *digits;
                char *end;
                unsigned long id;
                if(!x64_args2(s->args,st0,sti) ||
                   strcmp(st0,"%st") || strncmp(sti,"%st(",4U))
                    goto invalid;
                digits=sti+4U;
                if(!isdigit((unsigned char)*digits))goto invalid;
                errno=0;id=strtoul(digits,&end,10);
                if(errno || id>7UL || strcmp(end,")"))goto invalid;
                reg=(unsigned)id;
            }
            m->opcode=X64_ZERO;
            m->encoding=UINT32_C(0xde)|
                        ((uint32_t)(ops[k].group+reg)<<8U);
            m->operands[0].immediate=2U;
            goto done;
        }
    }
    /* Group-3 NOT/NEG and Group-5 INC/DEC, shared typed ModR/M
     * between register and memory forms. No duplicated memory encoder. */
    {
        static const struct {const char *op;unsigned opcode,group;bool wide;} unary[]={
            {"notb",0xf6U,2U,false},{"notw",0xf7U,2U,false},
            {"notl",0xf7U,2U,false},{"notq",0xf7U,2U,true},
            {"negb",0xf6U,3U,false},{"negw",0xf7U,3U,false},
            {"negl",0xf7U,3U,false},{"negq",0xf7U,3U,true},
            {"incb",0xfeU,0U,false},{"incw",0xffU,0U,false},
            {"incl",0xffU,0U,false},{"incq",0xffU,0U,true},
            {"decb",0xfeU,1U,false},{"decw",0xffU,1U,false},
            {"decl",0xffU,1U,false},{"decq",0xffU,1U,true},
            /* GNU Group-3 /4=/MUL, /5=IMUL, /6=DIV, /7=IDIV;
             * both reg and memory forms use the established unary path. */
            {"mulb",0xf6U,4U,false},{"mulw",0xf7U,4U,false},
            {"mull",0xf7U,4U,false},{"mulq",0xf7U,4U,true},
            {"imulb",0xf6U,5U,false},{"imulw",0xf7U,5U,false},
            {"imull",0xf7U,5U,false},{"imulq",0xf7U,5U,true},
            {"divb",0xf6U,6U,false},{"divw",0xf7U,6U,false},
            {"divl",0xf7U,6U,false},{"divq",0xf7U,6U,true},
            {"idivb",0xf6U,7U,false},{"idivw",0xf7U,7U,false},
            {"idivl",0xf7U,7U,false},{"idivq",0xf7U,7U,true}
        };
        for(i=0U;i<sizeof(unary)/sizeof(unary[0]);++i) {
            if(strcmp(s->op,unary[i].op)!=0)continue;
            /* IMUL has unary F7 /5 and binary 0F AF /r forms.
             * Dispatch based on operand arity before consuming its token;
             * otherwise unary matching erroneously rejects valid GNU IMUL. */
            if((!strcmp(s->op,"imulw") || !strcmp(s->op,"imull") ||
                !strcmp(s->op,"imulq")) &&
               strchr(s->args,',')!=NULL)continue;
            {
                size_t len=strlen(s->op);
                bool byte=s->op[len-1U]=='b';
                bool word=s->op[len-1U]=='w';
                bool high=false;
                bool valid;
                if(byte) {
                    valid=x64_register8(s->args,&rd);
                    if(!valid)high=valid=x64_high_register8(s->args,&rd);
                }else if(word)valid=x64_register16(s->args,&rd);
                else valid=x64_register(s->args,&rd,&wd) &&
                           wd==unary[i].wide;
                if(valid) {
                    m->opcode=X64_UNARY;m->count=1U;
                    m->encoding=unary[i].opcode|(unary[i].group<<8U)|
                         (byte?UINT32_C(0x1000):word?UINT32_C(0x2000):0U)|
                         (high?UINT32_C(0x4000):0U);
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].register_64=!byte&&!word&&wd;
                    m->operands[0].reg=rd;
                    goto done;
                }
            }
            if(x64_is_mem_candidate(s->args)) {
                X64Memory parsed;
                if(!x64_parse_base_memory(s->args,&parsed))goto invalid;
                m->operands[1].memory=malloc(sizeof(parsed));
                if(!m->operands[1].memory) {
                    free(parsed.symbol);minias_mcinst_destroy(m);
                    minias_set_error(as,"out-of-memory:x86-unary-memory");
                    return false;
                }
                *(X64Memory *)m->operands[1].memory=parsed;
                m->opcode=X64_REG_MEMORY;m->count=2U;
                m->encoding=unary[i].opcode|
                    (s->op[strlen(s->op)-1U]=='w'?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=unary[i].group;
                m->operands[0].register_64=unary[i].wide;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                goto done;
            }
            goto invalid;
        }
    }
    /* GNU Linux unsuffixed BSWAP chooses width from register spelling.
     * Keep the 0F C8+r opcode in x86 Target, never special-case Core. */
    if((!strcmp(s->op,"bswap") || !strcmp(s->op,"bswapl") ||
        !strcmp(s->op,"bswapq")) && x64_register(s->args,&rd,&wd) &&
       (!strcmp(s->op,"bswap") ||
        wd==(!strcmp(s->op,"bswapq")))) {
        m->opcode=X64_BSWAP;m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[0].register_64=wd;
        goto done;
    }
    /* Native long-mode PUSH/POP memory forms reuse typed ModR/M/SIB.
     * FF /6 and 8F /0 imply 64-bit width: emitting REX.W is incorrect. */
    if((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq")) &&
       s->args[0]!='$' && x64_is_mem_candidate(s->args)) {
        X64Memory parsed;
        if(!x64_parse_base_memory(s->args,&parsed))goto invalid;
        m->operands[1].memory=malloc(sizeof(parsed));
        if(!m->operands[1].memory) {
            minias_set_error(as,"out-of-memory:x86-stack-memory");
            free(parsed.symbol);minias_mcinst_destroy(m);return false;
        }
        *(X64Memory *)m->operands[1].memory=parsed;
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=!strcmp(s->op,"pushq")?0xffU:0x8fU;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=!strcmp(s->op,"pushq")?6U:0U;
        m->operands[0].register_64=false;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
    /* GNU implicit-count-one shift/rotate now resolves through the SAME
     * descriptor and machine-length paths as explicit immediate/CL. */
    {
        unsigned group,bits;
        bool legacy_high=false;
        if(x64_shift_descriptor(s->op,&group,&bits) &&
           x64_shift_register(s->args,bits,&rd,&legacy_high)) {
            m->opcode=X64_SHIFT_IMM;m->encoding=group|
                (bits==8U?0x200U:bits==16U?0x400U:0U)|
                (legacy_high?0x800U:0U);
            m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[0].register_64=bits==64U;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=1U;
            goto done;
        }
    }
    /* GNU shift/rotate memory forms share Group-2 ModR/M, displacement
     * parsing and ELF fixup with existing typed Group-1 memory instructions.
     * Opcode chooses D0/D1 for count 1, D2/D3 for CL and C0/C1 for
     * imm8; only the target ISA owns size selection and group selector. */
    {
        unsigned group,bits;
        if(x64_shift_descriptor(s->op,&group,&bits)) {
            const char *operand=NULL;
            uint64_t shift=1U;
            bool imm=false,via_cl=false;
            uint32_t base;
            X64Memory mem;
            if(x64_args2(s->args,src,dst) &&
               x64_is_mem_candidate(dst)) {
                operand=dst;
                if(!strcmp(src,"%cl"))via_cl=true;
                else if(*src=='$') {
                    if(!minias_eval_u64_expression(src+1U,&shift) ||
                       (shift>255U && shift<UINT64_MAX-127U))goto invalid;
                    imm=true;
                }else goto invalid;
            }else if(x64_is_mem_candidate(s->args)) {
                operand=s->args;
            }
            if(operand && x64_parse_base_memory(operand,&mem)) {
                base=bits==8U?(via_cl?UINT32_C(0xd2):
                       (imm && shift!=1U)?UINT32_C(0xc0):UINT32_C(0xd0)):
                       via_cl?UINT32_C(0xd3):
                       (imm && shift!=1U)?UINT32_C(0xc1):UINT32_C(0xd1);
                m->opcode=((base==0xc0U || base==0xc1U)?
                           X64_IMM_MEMORY:X64_REG_MEMORY);
                m->count=m->opcode==X64_IMM_MEMORY?3U:2U;
                /* The ModR/M.reg field is a Group-2 /opcode extension,
                 * never a low-byte GPR. Byte width is already encoded by
                 * C0/D0/D2; setting 0x200 would synthesize an invalid REX
                 * prefix whenever the selector (/4 SAL, /5 SHR, /7 SAR)
                 * happens to be >=4. */
                m->encoding=base|(bits==16U?0x400U:0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=group;
                m->operands[0].register_64=bits==64U;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                m->operands[1].memory=malloc(sizeof(mem));
                if(!m->operands[1].memory) {
                    free(mem.symbol);minias_mcinst_destroy(m);
                    minias_set_error(as,"out-of-memory:x86-shift-mem");
                    return false;
                }
                *(X64Memory *)m->operands[1].memory=mem;
                if(m->opcode==X64_IMM_MEMORY) {
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=shift;
                }
                goto done;
            }
        }
    }
    /* Byte ALU variants and sign-extending MOV all remain target local. */
    if(x64_args2(s->args,src,dst)) {
        static const struct {const char *op;uint32_t opcode;} byteop[]={
            {"movb",0x88U},{"addb",0x00U},{"adcb",0x10U},{"sbbb",0x18U},{"orb",0x08U},
            {"andb",0x20U},{"subb",0x28U},{"xorb",0x30U},
            {"cmpb",0x38U},{"testb",0x84U}
        };
        unsigned first,second;
        if(!strncmp(s->op,"cmov",4U)) {
            bool word=x64_register16(src,&first) &&
                      x64_register16(dst,&second);
            bool matched=word ||
                (x64_register(src,&first,&ws) &&
                 x64_register(dst,&second,&wd) && wd==ws);
            const char *name=s->op+4U;
            char cond[20];
            size_t clen=strlen(name);
            unsigned cc;
            if(matched && clen!=0U && clen<sizeof(cond)) {
                memcpy(cond,name,clen+1U);
                /* GNU CMOVw/l/q suffixes are optional when the registers
                 * imply a width. The condition itself may end in 'l',
                 * e.g. CMOVL is a valid unsuffixed condition alias. */
                if((cond[clen-1U]=='q' || cond[clen-1U]=='l' ||
                    cond[clen-1U]=='w') && clen>1U &&
                   !x64_cond_code(cond,&cc)) {
                    char suffix=cond[clen-1U];
                    cond[clen-1U]='\0';
                    if((suffix=='w')!=word ||
                       (!word && (suffix=='q')!=wd))goto invalid;
                }
                if(x64_cond_code(cond,&cc)) {
                    m->opcode=X64_CMOV_REG;m->count=2U;
                    m->encoding=cc|(word?UINT32_C(0x400):0U);
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=first;
                    m->operands[0].register_64=!word && ws;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=second;
                    goto done;
                }
            }
        }
        /* MOVZX/MOVSX source byte/word with independently typed W/L/Q
         * destination. One target MCInst implements both ISA opcode groups. */
        if((!strcmp(s->op,"movzbl") || !strcmp(s->op,"movzbq") ||
            !strcmp(s->op,"movzwl") || !strcmp(s->op,"movzwq") ||
            !strcmp(s->op,"movsbl") || !strcmp(s->op,"movsbq") ||
             !strcmp(s->op,"movsbw") || !strcmp(s->op,"movzbw") ||
            !strcmp(s->op,"movswl") || !strcmp(s->op,"movswq")) &&
           ((s->op[4]=='b' && (x64_register8(src,&first) ||
                               x64_high_register8(src,&first))) ||
            (s->op[4]=='w' && x64_register16(src,&first))) &&
           ((s->op[5]=='w' && x64_register16(dst,&second)) ||
             (s->op[5]!='w' && x64_register(dst,&second,&wd) &&
              wd==(s->op[5]=='q')))) {
            bool legacy_high=s->op[4]=='b' &&
                (!strcmp(src,"%ah") || !strcmp(src,"%ch") ||
                 !strcmp(src,"%dh") || !strcmp(src,"%bh"));
            /* GNU AH/CH/DH/BH require no REX, including REX.W.  A word
             * destination never assigned 'wd', since register16() has a
             * separate width contract; interrogate the canonical suffix. */
            if(legacy_high && (second>=8U || s->op[5]=='q'))goto invalid;
            m->opcode=X64_MOVZX_REG;m->count=2U;
            m->encoding=(s->op[4]=='b'?
                         (s->op[3]=='z'?0xb6U:0xbeU):
                         (s->op[3]=='z'?0xb7U:0xbfU))|
                        (legacy_high?0x800U:0U)|
                         (s->op[5]=='w'?0x400U:0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=second;
            m->operands[1].register_64=(s->op[5]=='w')?false:wd;
            goto done;
        }
        if(src[0]=='$' || !strcmp(src,"%cl")) {
            unsigned group,bits;
            uint64_t count=0U;
            bool legacy_high=false;
            if(x64_shift_descriptor(s->op,&group,&bits) &&
               x64_shift_register(dst,bits,&second,&legacy_high) &&
               (!strcmp(src,"%cl") ||
                (minias_eval_u64_expression(src+1,&count) &&
                 (count<=255U || count>=UINT64_MAX-127U)))) {
                m->opcode=!strcmp(src,"%cl")?
                    X64_SHIFT_CL:X64_SHIFT_IMM;
                m->count=2U;
                m->encoding=group|(bits==8U?0x200U:
                                   bits==16U?0x400U:0U)|
                            (legacy_high?0x800U:0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=second;
                m->operands[0].register_64=bits==64U;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=count;
                goto done;
            }
        }
        if(!strcmp(s->op,"testb") && src[0]=='$') {
            uint64_t bits;
            bool high=x64_high_register8(dst,&second);
            if((high || x64_register8(dst,&second)) &&
               minias_eval_u64_expression(src+1,&bits) &&
               (bits<=255U || bits>=UINT64_MAX-127U)) {
                m->opcode=X64_TEST8_IMM;m->count=2U;
                m->encoding=high?0x800U:0U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=second;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=bits;
                goto done;
            }
        }
        if((!strcmp(s->op,"bsf") || !strcmp(s->op,"bsfl") ||
            !strcmp(s->op,"bsfq") || !strcmp(s->op,"bsr") ||
            !strcmp(s->op,"bsrl") || !strcmp(s->op,"bsrq")) &&
           x64_register(src,&first,&ws) &&
           x64_register(dst,&second,&wd) && ws==wd &&
           (strlen(s->op)==3U ||
            wd==(s->op[strlen(s->op)-1U]=='q'))) {
            m->opcode=X64_REG_BINARY;m->count=2U;
            /* 0x800: reverse 0F BC/BD ModR/M REG/RM direction. */
            m->encoding=0x900U|(s->op[2]=='f'?0xbcU:0xbdU);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;
            m->operands[0].register_64=wd;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=second;
            goto done;
        }
        /* GNU two-register IMUL: 0F AF /r with destination in ModR/M.reg.
         * Do not create a second ALU operand encoder. */
        if((!strcmp(s->op,"imul") || !strcmp(s->op,"imull") ||
            !strcmp(s->op,"imulq") || !strcmp(s->op,"imulw")) &&
           (( !strcmp(s->op,"imulw") &&
              x64_register16(src,&first) && x64_register16(dst,&second)) ||
            (strcmp(s->op,"imulw") && x64_register(src,&first,&ws) &&
             x64_register(dst,&second,&wd) && ws==wd &&
             (!strcmp(s->op,"imul") ||
              wd==(!strcmp(s->op,"imulq")))))) {
            m->opcode=X64_REG_BINARY;m->count=2U;
            m->encoding=UINT32_C(0x9af) |
                (!strcmp(s->op,"imulw")?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;
            m->operands[0].register_64=!strcmp(s->op,"imulw")?false:ws;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=second;
            goto done;
        }
        /* F3 0F B8 /r with existing reversed ModR/M register encoding. */
        if((!strcmp(s->op,"popcntl") || !strcmp(s->op,"popcntq") ||
            !strcmp(s->op,"lzcntl") || !strcmp(s->op,"lzcntq") ||
            !strcmp(s->op,"tzcntl") || !strcmp(s->op,"tzcntq")) &&
           x64_register(src,&first,&ws) &&
           x64_register(dst,&second,&wd) && ws==wd &&
           wd==(s->op[strlen(s->op)-1U]=='q')) {
            m->opcode=X64_REG_BINARY;m->count=2U;
            m->encoding=UINT32_C(0x2000)|UINT32_C(0x900)|
                (!strncmp(s->op,"lzcnt",5U)?UINT32_C(0xbd):
                 !strncmp(s->op,"tzcnt",5U)?UINT32_C(0xbc):
                 UINT32_C(0xb8));
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=first;
            m->operands[0].register_64=wd;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=second;
            goto done;
        }
        /* Legacy 8-bit high registers share ModR/M IDs 4..7 with SPL,
         * BPL, SIL, DIL; any REX byte makes them mean the low registers.
         * Keep the high flags Target-owned and enforce this ISA constraint. */
        {
            bool highsrc=x64_high_register8(src,&first);
            bool highdst=x64_high_register8(dst,&second);
            bool srcreg=highsrc||x64_register8(src,&first);
            bool dstreg=highdst||x64_register8(dst,&second);
            if(srcreg && dstreg) {
                bool rexneeded=first>=8U || second>=8U ||
                               (first>=4U && !highsrc) ||
                               (second>=4U && !highdst);
                for(i=0U;i<sizeof(byteop)/sizeof(byteop[0]);++i) {
                    if(strcmp(s->op,byteop[i].op)!=0)continue;
                    if((highsrc || highdst) && rexneeded)goto invalid;
                    m->opcode=X64_BINARY8;m->count=2U;
                    m->encoding=byteop[i].opcode|
                                (highsrc?UINT32_C(0x1000):0U)|
                                (highdst?UINT32_C(0x2000):0U);
                    m->operands[0].kind=MINIAS_MC_REGISTER;
                    m->operands[0].reg=first;
                    m->operands[1].kind=MINIAS_MC_REGISTER;
                    m->operands[1].reg=second;
                    goto done;
                }
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
    if(!strncmp(s->op,"set",3U)) {
        unsigned cc;
        bool high8=x64_high_register8(s->args,&rd);
        if(x64_cond_code(s->op+3U,&cc) &&
           (high8 || x64_register8(s->args,&rd))) {
            m->opcode=X64_SETCC;m->count=1U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            /* AH/CH/DH/BH encode ModR/M ids 4..7 without REX;
             * SPL/BPL/SIL/DIL use those same ids but require REX. */
            m->encoding=0x90U+cc+(high8?UINT32_C(0x1000):0U);
            goto done;
        }
    }
    for(i=0U;i<sizeof(binary)/sizeof(binary[0]);++i) {
        size_t name_len=strlen(s->op);
        size_t form_len=strlen(binary[i].op);
        bool is_word=binary[i].op[form_len-1U]=='w';
        bool exact=!strcmp(s->op,binary[i].op);
        bool inferred=form_len==name_len+1U &&
            !strncmp(s->op,binary[i].op,name_len) &&
            (binary[i].op[form_len-1U]=='w' ||
             binary[i].op[form_len-1U]=='l' ||
             binary[i].op[form_len-1U]=='q');
        if((!exact && !inferred) || !x64_args2(s->args,src,dst))continue;
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
    /* Reuse the same condition-code policy as CMOV: JMP/CALL have fixed
     * opcode identities, while every Jcc synonym maps to 0x70+cc (short)
     * or 0x0f 0x80+cc (near). The MC relaxation plan chooses the width;
     * external targets retain PC32/PLT32 fixups with the x86 P+4 bias. */
    {
        unsigned condition=0U,encoding=0U;
        bool call=!strcmp(s->op,"call") || !strcmp(s->op,"callq");
        bool jump=!strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq");
        bool conditional=s->op[0]=='j' &&
                         x64_cond_code(s->op+1U,&condition);
        if(call || jump || conditional) {
            MiniAsSymbolExpr expr;
            encoding=call?0xe8U:jump?0xe9U:0x180U|condition;
            /* Register-indirect CALL/JMP are FF /2 and /4, not PLT32
             * symbolic branches. Dispatch them to the MCInst group-5
             * register parser below rather than rejecting '*' here. */
            if(!(s->args[0]=='*' && (call || jump))) {
                if(!minias_parse_symbol_addend(s->args,&expr) ||
                   expr.addend<INT64_MIN+4)goto invalid;
                m->operands[0].symbol=malloc(sizeof(expr));
                if(!m->operands[0].symbol) {
                    minias_set_error(as,"out-of-memory:x86-branch-symbol");
                    minias_mcinst_destroy(m);return false;
                }
                *m->operands[0].symbol=expr;
                m->operands[0].kind=MINIAS_MC_SYMBOL;
                m->opcode=X64_BRANCH;m->count=1U;
                m->encoding=encoding;
                goto done;
            }
        }
    }
    /* GNU AT&T defaults unsuffixed immediate PUSH to the active 64-bit
     * operand size. Linux head_64.S emits `push $0` in its return path;
     * reuse the single typed PUSH imm8/imm32 encoding and ABS32S fixup. */
    if((!strcmp(s->op,"pushq") || !strcmp(s->op,"push") ||
       ((s->site.mode_bits&X64_CODE32)!=0U &&
        !strcmp(s->op,"pushl"))) && s->args[0]=='$') {
        uint64_t bits;
        MiniAsSymbolExpr expr;
        if(!minias_eval_u64_expression(s->args+1,&bits) &&
           minias_parse_symbol_addend(s->args+1,&expr)) {
            m->opcode=X64_PUSH_IMM;m->count=1U;
            m->operands[0].kind=MINIAS_MC_SYMBOL;
            m->operands[0].symbol=malloc(sizeof(expr));
            if(!m->operands[0].symbol) {
                minias_mcinst_destroy(m);minias_set_error(as,"out-of-memory:x86-push-symbol");
                return false;
            }
            *m->operands[0].symbol=expr;
            goto done;
        }
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
    /* Register-indirect and memory-indirect CALL/JMP share the Group-5
     * ISA semantics. The latter uses the existing typed memory, PC32
     * fixup, segment override and SIB encoder; no new Core path. */
    if ((!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
         !strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq") ||
         !strcmp(s->op,"ljmp") || !strcmp(s->op,"ljmpl") ||
         !strcmp(s->op,"lcall") || !strcmp(s->op,"lcalll")) &&
        s->args[0]=='*' && x64_is_mem_candidate(s->args+1)) {
        X64Memory parsed;
        if (!x64_parse_base_memory(s->args+1,&parsed))goto invalid;
        m->operands[1].memory=malloc(sizeof(parsed));
        if (!m->operands[1].memory) {
            free(parsed.symbol);minias_mcinst_destroy(m);
            minias_set_error(as,"out-of-memory:x86-indirect-memory");
            return false;
        }
        *(X64Memory *)m->operands[1].memory=parsed;
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=0xffU;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=(!strcmp(s->op,"call") ||
                            !strcmp(s->op,"callq"))?2U:
                           (!strcmp(s->op,"lcall") ||
                            !strcmp(s->op,"lcalll"))?3U:
                           (!strcmp(s->op,"ljmp") ||
                            !strcmp(s->op,"ljmpl"))?5U:4U;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
    if ((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq") ||
         !strcmp(s->op,"push") || !strcmp(s->op,"pop") ||
         ((s->site.mode_bits&X64_CODE32)!=0U &&
          !strcmp(s->op,"pushl"))) &&
        x64_register(s->args,&rs,&ws) &&
        (ws || ((s->site.mode_bits&X64_CODE32)!=0U &&
                !strcmp(s->op,"pushl") && !ws && rs<8U))) {
        m->opcode=X64_STACK;m->count=1U;m->operands[0].reg=rs;
        m->encoding=(!strcmp(s->op,"pushq") ||
                     !strcmp(s->op,"push") ||
                     !strcmp(s->op,"pushl"))?0x50U:0x58U;goto done;
    }
    /* GNU AT&T B0+rb immediate: no REX with AH/CH/DH/BH, but explicit
     * REX for SPL/BPL/SIL/DIL and extended low bytes. */
    if(!strcmp(s->op,"movb") && x64_args2(s->args,src,dst) &&
       src[0]=='$') {
        uint64_t bits;
        unsigned reg;
        bool high=x64_high_register8(dst,&reg);
        if(high || x64_register8(dst,&reg)) {
            if(!minias_eval_u64_expression(src+1U,&bits) ||
               (int64_t)bits<-256 || (int64_t)bits>255)goto invalid;
            m->opcode=X64_MOV_IMM32;m->count=1U;
            m->encoding=0x200U|(high?0x800U:0U);
            m->operands[0].reg=reg;
            m->operands[0].immediate=(uint8_t)bits;
            goto done;
        }
        /* MOVB imm8-to-memory stays in the generic C6 /0 path. */
    }

    /* MOVW imm16-to-register is B8+r with 66 prefix, not Group-1
     * arithmetic. Keep it in the existing MOV immediate typed opcode;
     * 0x400 selects imm16 in both size and emit phases. */
    if(!strcmp(s->op,"movw") && x64_args2(s->args,src,dst) &&
       src[0]=='$' && x64_register16(dst,&rd)) {
        uint64_t imm16;
        if(!minias_eval_u64_expression(src+1U,&imm16) ||
           (int64_t)imm16<-32768 || (int64_t)imm16>65535)
            goto invalid;
        m->opcode=X64_MOV_IMM32;m->count=1U;
        m->encoding=UINT32_C(0x400);
        m->operands[0].reg=rd;
        m->operands[0].immediate=(uint16_t)imm16;
        goto done;
    }
    if (!strcmp(s->op,"movl") && x64_args2(s->args,src,dst) &&
        src[0]=='$' && x64_register(dst,&rd,&wd) && !wd) {
        uint64_t bits;
        if(minias_eval_u64_expression(src+1U,&bits) &&
           (bits<=UINT32_MAX ||
            ((int64_t)bits>=INT32_MIN && (int64_t)bits<0) ||
            /* GNU `$~0xfa1e0ff3` carries the mathematically negative
             * 64-bit complement, then encodes its low imm32. Permit only
             * explicitly bitwise complemented high-all-ones values, not
             * unrelated out-of-range literals such as $0x100000000. */
            (strchr(src+1U,'~')!=NULL &&
             (bits>>32U)==UINT32_MAX))) {
            m->opcode=X64_MOV_IMM32;m->count=1U;
            m->operands[0].reg=rd;
            m->operands[0].immediate=(uint32_t)bits;
            goto done;
        }
    }
    /* GNU encodes MOVABS (and MOVQ imm64 outside signed32 range) through
     * REX.W B8+r imm64. Keep ABS64 symbol relocation distinct from the
     * GNU sign-extending C7 /0 imm32 form. */
    if(x64_args2(s->args,src,dst) && src[0]=='$' &&
       (!strcmp(s->op,"movabsq") || !strcmp(s->op,"movabs") ||
        !strcmp(s->op,"movq")) &&
       x64_register(dst,&rd,&wd) && wd) {
        uint64_t bits;
        MiniAsSymbolExpr expr;
        bool numeric=minias_eval_u64_expression(src+1,&bits);
        bool symbolic=!numeric &&
            (!strcmp(s->op,"movabsq") || !strcmp(s->op,"movabs")) &&
            minias_parse_symbol_addend(src+1,&expr);
        if((numeric && ((!strcmp(s->op,"movabsq") ||
                         !strcmp(s->op,"movabs")) ||
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
                    minias_mcinst_destroy(m);return false;
                }
                *m->operands[1].symbol=expr;
            }else m->operands[1].immediate=bits;
            goto done;
        }
    }
    /* 0F BA /4../7 ib; reuse typed ModR/M/SIB and ELF Fixup machinery. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *name;unsigned group;bool wide;} forms[]={
            {"btw",4U,false},
            {"btl",4U,false},{"btq",4U,true},
            {"btsw",5U,false},
            {"btsl",5U,false},{"btsq",5U,true},
            {"btrw",6U,false},
            {"btrl",6U,false},{"btrq",6U,true},
            {"btcw",7U,false},
            {"btcl",7U,false},{"btcq",7U,true}
        };
        for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i) {
            uint64_t immediate;
            bool word=s->op[strlen(s->op)-1U]=='w';
            if(strcmp(s->op,forms[i].name))continue;
            if(!minias_eval_u64_expression(src+1,&immediate) ||
               immediate>255U)goto invalid;
            if((word && x64_register16(dst,&rd)) ||
               (!word && x64_register(dst,&rd,&wd) &&
                wd==forms[i].wide)) {
                m->opcode=X64_BIT_IMMREG;m->count=2U;
                m->encoding=forms[i].group|(word?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;
                m->operands[0].register_64=!word && wd;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=immediate;
                goto done;
            }
            if(x64_is_mem_candidate(dst)) {
                X64Memory mem;
                if(!x64_parse_base_memory(dst,&mem))goto invalid;
                m->operands[1].memory=malloc(sizeof(mem));
                if(!m->operands[1].memory) {
                    free(mem.symbol);minias_mcinst_destroy(m);
                    minias_set_error(as,"out-of-memory:x86-bit-imm");
                    return false;
                }
                *(X64Memory *)m->operands[1].memory=mem;
                m->opcode=X64_IMM_MEMORY;m->count=3U;
                m->encoding=UINT32_C(0x1ba)|
                            (word?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=forms[i].group;
                m->operands[0].register_64=forms[i].wide;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                m->operands[2].immediate=immediate;
                goto done;
            }
            goto invalid;
        }
    }
    /* GNU Linux compiler source uses C7, 81, 83 and F7 immediates against
     * memory and registers. Keep the same typed memory/ModR/M engine. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *op;unsigned group;} groups[]={
            {"addq",0U},{"addl",0U},{"adcq",2U},{"adcl",2U},
            {"orq",1U},{"orl",1U},
            {"andq",4U},{"andl",4U},{"subq",5U},{"subl",5U},
            {"sbbq",3U},{"sbbl",3U},
            {"xorq",6U},{"xorl",6U},{"cmpq",7U},{"cmpl",7U},
            {"addb",0U},{"adcb",2U},{"sbbb",3U},{"orb",1U},{"andb",4U},{"subb",5U},
            {"xorb",6U},{"cmpb",7U},
            {"addw",0U},{"adcw",2U},{"orw",1U},{"sbbw",3U},{"andw",4U},
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
        /* One typed ELF sign-ext imm32 operand for C7 MOVQ and all
         * Group-1 32/64-bit immediate-to-memory instructions. */
        if(!numeric &&
           (!strcmp(s->op,"movq") ||
            (has_group && (s->op[strlen(s->op)-1U]=='q' ||
                           s->op[strlen(s->op)-1U]=='l'))) &&
           minias_parse_symbol_addend(src+1,&sym))is_sym=true;
        val=(int64_t)bits;
        {
            char suffix=s->op[strlen(s->op)-1U];
            bool in_range=numeric &&
                (suffix=='b'?(val>=-256 && val<=255):
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
                        minias_mcinst_destroy(m);return false;
                    }
                    *m->operands[1].symbol=sym;
                    m->operands[1].kind=MINIAS_MC_SYMBOL;
                }else {
                    m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[1].immediate=bits;
                }
                goto done;
            }
            if(x64_is_mem_candidate(dst) &&
               (has_group || !strcmp(s->op,"movq") ||
                !strcmp(s->op,"movl") || !strcmp(s->op,"movw") ||
                !strcmp(s->op,"movb") || !strcmp(s->op,"testq") ||
                !strcmp(s->op,"testl") || !strcmp(s->op,"testw") ||
                !strcmp(s->op,"testb"))) {
                X64Memory parsed;
                if(s->op[strlen(s->op)-1U]=='b' &&
                   (val < -256 || val > 255))goto invalid;
                if(!x64_parse_base_memory(dst,&parsed))goto invalid;
                /* Keep both expressions typed, for independent GNU PC32
                 * displacement and 32S immediate ELF relocations. */
                m->operands[1].memory=malloc(sizeof(parsed));
                if(!m->operands[1].memory){
                    free(parsed.symbol);
                    minias_set_error(as,"out-of-memory:x86-imm-memory");
                    minias_mcinst_destroy(m);return false;
                }
                *(X64Memory *)m->operands[1].memory=parsed;
                m->opcode=X64_IMM_MEMORY;m->count=3U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=has_group?group:0U;
                m->operands[0].register_64=wide;
                m->operands[1].kind=MINIAS_MC_MEMORY;
                if(is_sym) {
                    m->operands[2].symbol=malloc(sizeof(sym));
                    if(!m->operands[2].symbol) {
                        minias_set_error(as,"out-of-memory:x86-imm-memory-symbol");
                        /* MCInst owns the memory descriptor and its nested
                         * displacement symbol even when operand 2 fails. */
                        minias_mcinst_destroy(m);
                        return false;
                    }
                    *m->operands[2].symbol=sym;
                    m->operands[2].kind=MINIAS_MC_SYMBOL;
                } else {
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=bits;
                }
                m->encoding=(has_group ?
                    (suffix=='b'?0x80U:
                     (!is_sym && val>=-128 && val<=127)?0x83U:0x81U):
                    (!strcmp(s->op,"movb")?0xc6U:
                     !strcmp(s->op,"testb")?0xf6U:
                     !strncmp(s->op,"test",4U)?0xf7U:0xc7U)) |
                    (suffix=='w'?0x400U:0U);
                goto done;
            }
        }
        }
    }
    /* GNU ELF64 Group-1 ALU symbolic imm32: 32S for 64-bit operands,
     * ABS32 for 32-bit operands. Use the existing typed ALU machine code
     * path rather than string-patching relocations after emission. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *name;unsigned group;} names[]={
            {"add",0U},{"or",1U},{"adc",2U},{"sbb",3U},
            {"and",4U},{"sub",5U},{"xor",6U},{"cmp",7U}
        };
        MiniAsSymbolExpr expr;
        bool wide;
        unsigned reg,k;
        size_t n=strlen(s->op);
        if((n==4U || n==3U) &&
           (s->op[n-1U]=='l' || s->op[n-1U]=='q') &&
           !minias_eval_u64_expression(src+1,&m->operands[1].immediate) &&
           minias_parse_symbol_addend(src+1,&expr) &&
           x64_register(dst,&reg,&wide) &&
           wide==(s->op[n-1U]=='q')) {
            for(k=0U;k<sizeof(names)/sizeof(names[0]);++k) {
                size_t length=strlen(names[k].name);
                if(n!=length+1U || strncmp(s->op,names[k].name,length))
                    continue;
                m->opcode=X64_ALU_IMM;m->count=2U;m->encoding=names[k].group;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=reg;
                m->operands[0].register_64=wide;
                m->operands[1].kind=MINIAS_MC_SYMBOL;
                m->operands[1].symbol=malloc(sizeof(expr));
                if(!m->operands[1].symbol) {
                    minias_mcinst_destroy(m);minias_set_error(as,"out-of-memory:x86-alu-symbol");
                    return false;
                }
                *m->operands[1].symbol=expr;
                goto done;
            }
        }
    }
    /* Genuine Linux x86 forward subtractive immediate:
     * addq $(identity_mapped - relocate_kernel), %r8.
     * GNU preserves imm32 for forward deltas even when final size fits imm8. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *name;unsigned group;} alu[]={
            {"add",0U},{"or",1U},{"adc",2U},{"sbb",3U},
            {"and",4U},{"sub",5U},{"xor",6U},{"cmp",7U}
        };
        size_t len=strlen(s->op);char suffix=len?s->op[len-1U]:'\0';
        unsigned reg,k;bool wide;
        MiniAsSymbolDifference diff;
        if((suffix=='q' || suffix=='l') &&
           minias_parse_symbol_difference(src+1U,&diff) &&
           x64_register(dst,&reg,&wide) && wide==(suffix=='q')) {
            for(k=0U;k<sizeof(alu)/sizeof(alu[0]);++k) {
                size_t width=strlen(alu[k].name);
                if(len!=width+1U ||
                   strncmp(s->op,alu[k].name,width)!=0)continue;
                m->opcode=X64_ALU_IMM;m->count=2U;
                m->encoding=alu[k].group;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=reg;
                m->operands[0].register_64=wide;
                m->operands[1].kind=MINIAS_MC_SYMBOL_DIFFERENCE;
                m->operands[1].difference=malloc(sizeof(diff));
                if(!m->operands[1].difference) {
                    minias_set_error(as,"out-of-memory:x86-alu-difference");
                    minias_mcinst_destroy(m);return false;
                }
                *m->operands[1].difference=diff;
                /* The typed symbol difference is resolved against finalized
                 * fragment anchors by X64_ALU_IMM emission. Its instruction
                 * width is already conservatively fixed by x64_measure, so
                 * it must not force a whole-source parser replay when an
                 * unrelated branch grows during layout. */
                goto done;
            }
        }
    }
    /* GNU TEST is Group-3 /0, NOT Group-1 ALU; byte TEST already has
     * a separate 8-bit form. Retain non-byte operand size in MCInst
     * so parse, measure and encode agree on 66/REX and imm width. */
    if((!strcmp(s->op,"testw") || !strcmp(s->op,"testl") ||
        !strcmp(s->op,"testq")) && x64_args2(s->args,src,dst) &&
       src[0]=='$') {
        unsigned bits=!strcmp(s->op,"testw")?16U:
                      !strcmp(s->op,"testl")?32U:64U;
        uint64_t imm;
        bool width64=false;
        bool matched=bits==16U?x64_register16(dst,&rd):
                     x64_register(dst,&rd,&width64) &&
                     width64==(bits==64U);
        if(matched && minias_eval_u64_expression(src+1,&imm) &&
           (bits==16U?
            ((int64_t)imm>=-32768 && (int64_t)imm<=65535):
            bits==32U?
            (imm<=UINT32_MAX || ((int64_t)imm>=INT32_MIN && (int64_t)imm<0)):
            (int64_t)imm>=INT32_MIN && (int64_t)imm<=INT32_MAX)) {
            m->opcode=X64_TEST_IMM_REG;m->count=2U;
            m->encoding=bits==16U?0x400U:0U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[0].register_64=bits==64U;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=imm;
            goto done;
        }
    }
    /* One typed Group-1 ALU register-immediate path across 8/16/32/64.
     * Encoded target width flags are disjoint from the three-bit ALU group;
     * the existing MCInst length/emit phases own the real byte count. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        static const struct {const char *name;unsigned group;} groups[]={
            {"add",0U},{"or",1U},{"adc",2U},{"sbb",3U},{"and",4U},
            {"sub",5U},{"xor",6U},{"cmp",7U}
        };
        size_t len=strlen(s->op);
        char width=len?s->op[len-1U]:'\0';
        unsigned bits=width=='b'?8U:width=='w'?16U:
                      width=='l'?32U:width=='q'?64U:0U;
        unsigned reg=0U,group=0U;
        bool matched=false,wide=false,legacy_high8=false;
        uint64_t imm;
        if(bits && minias_eval_u64_expression(src+1,&imm) &&
           ((bits==8U && (int64_t)imm>=-256 && (int64_t)imm<=255) ||
            (bits==16U && (int64_t)imm>=-32768 && (int64_t)imm<=65535) ||
            (bits==32U && (imm<=UINT32_MAX ||
                 ((int64_t)imm>=INT32_MIN && (int64_t)imm<0) ||
                 /* GNU Linux's (~0xFF000000) has an explicitly unary
                  * complemented 64-bit bit pattern but ALU-l uses only
                  * the low 32 bits. Do not generalize silent truncation
                  * to unrelated overflowing positive constants. */
                 (strchr(src+1U,'~')!=NULL &&
                  (imm>>32U)==UINT32_MAX))) ||
            (bits==64U && (int64_t)imm>=INT32_MIN &&
                           (int64_t)imm<=INT32_MAX)) &&
           ((bits==8U &&
             ((legacy_high8=x64_high_register8(dst,&reg)) ||
              x64_register8(dst,&reg))) ||
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
                                   bits==16U?0x400U:0U)|
                            (legacy_high8?0x800U:0U);
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
        X64Memory parsed;
        unsigned r, reg_bits=0U;
        bool wide=false, memory_source, legacy_high8=false;
        bool byte_add_memory_opcode_zero=false;
        const unsigned *chosen = NULL;
        unsigned target_opcode=0U;
        if (x64_register(src,&r,&wide) &&
            x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=wide?64U:32U;
        } else if (x64_high_register8(src,&r) &&
                   x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=8U;legacy_high8=true;
        } else if (x64_register8(src,&r) &&
                   x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=8U;
        } else if (x64_register16(src,&r) &&
                   x64_parse_base_memory(dst,&parsed)) {
            memory_source=false;reg_bits=16U;
        } else if (x64_register(dst,&r,&wide) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=wide?64U:32U;
        } else if (x64_high_register8(dst,&r) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=8U;legacy_high8=true;
        } else if (x64_register8(dst,&r) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=8U;
        } else if (x64_register16(dst,&r) &&
                   x64_parse_base_memory(src,&parsed)) {
            memory_source=true;reg_bits=16U;
        } else goto invalid;
        if(legacy_high8 && (parsed.base>=8U ||
           (parsed.has_index && parsed.index>=8U))) {
            free(parsed.symbol);goto invalid;
        }
        /* Two-byte CMOVcc/BSF/BSR r/m -> register share the same retained
         * memory operand, SIB/REX/PC32 fixup and GNU AT&T register width.
         * The condition code and opcode stay ISA-owned. */
        if(memory_source && reg_bits>=16U) {
            unsigned op=0U,cc=0U;
            const char *cond=NULL;
            bool is_cmov=!strncmp(s->op,"cmov",4U);
            bool valid=false;
            if(is_cmov) {
                cond=s->op+4U;
                if(x64_cond_code(cond,&cc))valid=true;
                else {
                    size_t len=strlen(cond);
                    if(len>1U && len<30U) {
                        char bare[32];
                        char suffix=cond[len-1U];
                        memcpy(bare,cond,len-1U);
                        bare[len-1U]='\0';
                        valid=((suffix=='w' && reg_bits==16U) ||
                               (suffix=='l' && reg_bits==32U) ||
                               (suffix=='q' && reg_bits==64U)) &&
                              x64_cond_code(bare,&cc);
                    }
                }
                if(valid)op=0x140U+cc;
            } else if(!strncmp(s->op,"bsf",3U) ||
                      !strncmp(s->op,"bsr",3U)) {
                size_t len=strlen(s->op);
                bool inferred=len==3U;
                bool explicit_width=len==4U &&
                    ((s->op[3]=='w' && reg_bits==16U) ||
                     (s->op[3]=='l' && reg_bits==32U) ||
                     (s->op[3]=='q' && reg_bits==64U));
                if(inferred || explicit_width)
                    op=s->op[2]=='f'?0x1bcU:0x1bdU;
            }
            if(op!=0U){target_opcode=op;chosen=&target_opcode;}
        }
        if(!chosen)for (i=0U;i<sizeof(regmem)/sizeof(regmem[0]);++i) {
            /* GAS accepts unsuffixed mem/reg forms if the register
             * establishes the width. Keep the inference in this x86 ISA
             * decoder, never in the Common assembly lexer. */
            size_t opname_len=strlen(s->op);
            size_t form_len=strlen(regmem[i].op);
            bool exact=!strcmp(s->op,regmem[i].op);
            bool inferred=form_len==opname_len+1U &&
                !strncmp(s->op,regmem[i].op,opname_len) &&
                (regmem[i].op[form_len-1U]=='b'?reg_bits==8U:
                 regmem[i].op[form_len-1U]=='w'?reg_bits==16U:
                 regmem[i].op[form_len-1U]=='q'?reg_bits==64U:
                 regmem[i].op[form_len-1U]=='l' && reg_bits==32U);
            char suffix=regmem[i].op[form_len-1U];
            if((exact || inferred) && wide==regmem[i].wide &&
               (suffix=='b'?reg_bits==8U:
                suffix=='w'?reg_bits==16U:
                suffix=='q'?reg_bits==64U:reg_bits==32U)) {
                if((!strcmp(s->op,"leaq")||!strcmp(s->op,"leal") ||
                    !strcmp(s->op,"movzbl") || !strcmp(s->op,"movzbq") ||
                    !strcmp(s->op,"movzwl") || !strcmp(s->op,"movzwq") ||
                    !strcmp(s->op,"movslq") ||
                    !strcmp(s->op,"imulq") || !strcmp(s->op,"imull") ||
                    !strcmp(s->op,"imulw")) && !memory_source)continue;
                chosen=memory_source?&regmem[i].rm_src:&regmem[i].rm_dest;
                /* x86 ADD r8,r/m8 opcode 00 is real. A zero descriptor
                 * normally denotes an unsupported operand direction, but
                 * must not suppress this genuinely valid instruction. */
                byte_add_memory_opcode_zero=!memory_source &&
                    !strcmp(regmem[i].op,"addb");
                break;
            }
        }
        if (!chosen || (*chosen==0U && !byte_add_memory_opcode_zero)) {
            free(parsed.symbol);goto invalid;
        }
        m->operands[1].memory=malloc(sizeof(parsed));
        if (!m->operands[1].memory) {
            minias_set_error(as,"out-of-memory:x86_64-mc-memory");
            free(parsed.symbol);minias_mcinst_destroy(m);return false;
        }
        *(X64Memory *)m->operands[1].memory=parsed;
        m->opcode=X64_REG_MEMORY; m->count=2U;
        /* Target-local encoding flags: 0x100=two-byte opcode,
         * 0x200=low-byte REX mode, 0x400=66 word-size override. */
        m->encoding=*chosen |
            ((!strncmp(s->op,"bt",2U) || !strncmp(s->op,"xadd",4U) ||
              !strncmp(s->op,"cmpxchg",7U)) ? 0x100U : 0U) |
            ((!strncmp(s->op,"popcnt",6U) ||
               !strncmp(s->op,"lzcnt",5U) ||
               !strncmp(s->op,"tzcnt",5U))?UINT32_C(0x2000):0U) |
            (reg_bits==8U?0x200U:reg_bits==16U?0x400U:0U)|
            (legacy_high8?0x800U:0U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=r;
        m->operands[0].register_64=wide;
        m->operands[1].kind=MINIAS_MC_MEMORY;
        goto done;
    }
invalid:
    minias_set_error(as,"unsupported-x86_64-operand:%s:%s:line=%zu",
                     original->op,original->args,original->line);
    minias_mcinst_destroy(m);return false;
done:
    original->mcinst=m;return true;
}

/* One target-owned memory-instruction layout, consumed by sizing and
 * emission. Previously the two paths independently counted SIB, REX,
 * operand prefixes, the displacement and immediate suffix. Keeping those
 * width decisions in one place protects the Common fragment fixed point
 * when new x86 addressing forms are introduced. */
typedef struct X64MemoryLayout {
    unsigned immediate_bytes;
    unsigned displacement_bytes;
    bool has_sib;
    bool has_rex;
    bool is_vex3;
    uint32_t instruction_bytes;
} X64MemoryLayout;

static X64MemoryLayout x64_memory_layout(const MiniAsMCInst *m) {
    const X64Memory *mem=m->operands[1].memory;
    const unsigned opcode=m->encoding&UINT32_C(0xff);
    X64MemoryLayout layout;
    memset(&layout,0,sizeof(layout));
    layout.displacement_bytes=x64_disp_bytes(mem);
    layout.has_sib=!mem->pc_relative &&
        (!mem->has_base || mem->has_index || (mem->base&7U)==4U);
    layout.is_vex3=(m->encoding&UINT32_C(0x40000))!=0U;
    if(layout.is_vex3) {
        /* VEX3 prefix + opcode + ModR/M, no legacy REX byte. */
        layout.instruction_bytes=5U+(mem->address_space?1U:0U)+
            (layout.has_sib?1U:0U)+layout.displacement_bytes;
        return layout;
    }
    if(m->opcode==X64_IMM_MEMORY) {
        layout.immediate_bytes=opcode==0x83U || opcode==0xc6U ||
            opcode==0x80U || opcode==0xf6U || opcode==0xbaU ||
            opcode==0x6bU || opcode==0xc0U || opcode==0xc1U?1U:
            (m->encoding&UINT32_C(0x400))?2U:4U;
    }
    layout.has_rex=m->operands[0].register_64 ||
        m->operands[0].reg>=8U ||
        ((m->encoding&UINT32_C(0x200)) &&
         m->operands[0].reg>=4U && !(m->encoding&UINT32_C(0x800))) ||
        mem->base>=8U || (mem->has_index && mem->index>=8U);
    layout.instruction_bytes=layout.immediate_bytes+2U+
        ((m->encoding&UINT32_C(0x100))?1U:0U)+
        ((m->encoding&UINT32_C(0x10000))?1U:0U)+
        ((m->encoding&UINT32_C(0x2000))?1U:0U)+
        ((m->encoding&UINT32_C(0x400))?1U:0U)+
        (mem->address_space?1U:0U)+
        (layout.has_rex?1U:0U)+
        (layout.has_sib?1U:0U)+layout.displacement_bytes;
    return layout;
}

static bool x64_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    const MiniAsMCInst *m=s->mcinst;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    switch(m->opcode) {
        case X64_ZERO: *size=(uint32_t)m->operands[0].immediate;return true;
        case X64_BMI2_SHIFT: *size=5U;return true;
        case X64_SYSTEM_MOVE:
            *size=2U+((m->encoding&0x100U)?1U:0U)+
                  ((m->encoding&0x400U)?1U:0U)+
                  (m->operands[0].reg>=8U ||
                   m->operands[1].reg>=8U?1U:0U);
            return true;
        case X64_DOUBLE_SHIFT:
            *size=3U+((m->encoding&0x400U)?1U:0U)+
                  ((m->encoding&0x200U)?1U:0U)+
                  (m->operands[0].register_64 ||
                   m->operands[0].reg>=8U ||
                   m->operands[1].reg>=8U?1U:0U);
            return true;
        case X64_ENDBR64: *size=4U;return true;
        case X64_SETCC:
            *size=3U+(m->operands[0].reg>=4U &&
                      !(m->encoding&UINT32_C(0x1000))?1U:0U);return true;
        case X64_BINARY8:
            *size=2U+((m->operands[0].reg>=8U ||
                        m->operands[1].reg>=8U ||
                        (m->operands[0].reg>=4U &&
                         !(m->encoding&UINT32_C(0x1000))) ||
                        (m->operands[1].reg>=4U &&
                         !(m->encoding&UINT32_C(0x2000))))?1U:0U);
            return true;
        case X64_MOVSLQ: *size=3U;return true;
        case X64_CMOV_REG:
            *size=3U+((m->encoding&UINT32_C(0x400))?1U:0U)+
                (m->operands[0].register_64 ||
                       m->operands[0].reg>=8U || m->operands[1].reg>=8U?
                       1U:0U);return true;
        case X64_UNARY: {
            bool byte=(m->encoding&UINT32_C(0x1000))!=0U;
            bool word=(m->encoding&UINT32_C(0x2000))!=0U;
            bool high=(m->encoding&UINT32_C(0x4000))!=0U;
            unsigned r=m->operands[0].reg;
            bool rex=m->operands[0].register_64 || r>=8U ||
                     (byte && r>=4U && !high);
            *size=2U+(word?1U:0U)+(rex?1U:0U)+
                  ((m->encoding&UINT32_C(0x8000))?1U:0U)+
                  ((m->encoding&UINT32_C(0x10000))?1U:0U)+
                  ((m->encoding&UINT32_C(0x20000))?1U:0U);return true;
        }
        case X64_BSWAP:
            *size=2U+(m->operands[0].register_64 ||
                       m->operands[0].reg>=8U?1U:0U);return true;
        case X64_TEST_IMM_REG:
            *size=((m->encoding&0x400U)?1U:0U)+
                  (m->operands[0].register_64 ||
                   m->operands[0].reg>=8U?1U:0U)+
                  (m->operands[0].reg==0U?1U:2U)+
                  ((m->encoding&0x400U)?2U:4U);
            return true;
        case X64_SHIFT_CL:
            *size=2U+((m->encoding&0x400U)?1U:0U)+
                  (m->operands[0].register_64 ||
                   m->operands[0].reg>=8U ||
                   ((m->encoding&0x200U) && m->operands[0].reg>=4U &&
                    !(m->encoding&0x800U))?1U:0U);
            return true;
        case X64_MOVZX_REG:
            *size=3U+((m->encoding&0x400U)?1U:0U)+((!(m->encoding&0x800U) &&
                         (((m->encoding&0xffU)==0xb6U ||
                           (m->encoding&0xffU)==0xbeU) &&
                          m->operands[0].reg>=4U)) ||
                       m->operands[0].reg>=8U || m->operands[1].reg>=8U ||
                       m->operands[1].register_64?1U:0U);return true;
        case X64_SHIFT_IMM:
            *size=((m->encoding&0x400U)?1U:0U)+
                  (m->operands[0].register_64 ||
                   m->operands[0].reg>=8U ||
                   ((m->encoding&0x200U) && m->operands[0].reg>=4U &&
                    !(m->encoding&0x800U))?1U:0U)+
                  (m->operands[1].immediate==1U?2U:3U);return true;
        case X64_TEST8_IMM:
            *size=m->operands[0].reg==0U?2U:
                  (3U+(m->operands[0].reg>=4U &&
                       !(m->encoding&0x800U)?1U:0U));return true;
        case X64_IMUL_IMM: {
            int64_t val=(int64_t)m->operands[2].immediate;
            bool word=(m->encoding&0x400U)!=0U;
            bool rex=m->operands[0].register_64 ||
                     m->operands[0].reg>=8U || m->operands[1].reg>=8U;
            unsigned bytes=val>=-128 && val<=127?1U:word?2U:4U;
            *size=(word?1U:0U)+(rex?1U:0U)+2U+bytes;
            return true;
        }
        case X64_BIT_IMMREG:
            *size=4U+((m->encoding&UINT32_C(0x400))?1U:0U)+
                (m->operands[0].register_64 ||
                       m->operands[0].reg>=8U?1U:0U);
            return true;
        case X64_PUSH_IMM: {
            int32_t imm=(int32_t)(uint32_t)m->operands[0].immediate;
            *size=m->operands[0].kind==MINIAS_MC_SYMBOL?5U:
                  imm>=-128 && imm<=127?2U:5U;
            return true;
        }
        case X64_BRANCH:
            /* Start JMP/Jcc at GNU's shortest encoding; target relaxation
             * promotes external/out-of-range edges before final emission. */
            *size=m->encoding==0xe8U?5U:2U;return true;
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            *size=x64_memory_layout(m).instruction_bytes;
            return true;
        }
        case X64_MOVQ_IMMREG: *size=7U;return true;
        case X64_MOVABS_IMMREG: *size=10U;return true;
        case X64_ALU_IMM: {
            bool symbol=m->operands[1].kind==MINIAS_MC_SYMBOL ||
                        m->operands[1].kind==MINIAS_MC_SYMBOL_DIFFERENCE;
            bool byte=(m->encoding&0x200U)!=0U;
            bool word=(m->encoding&0x400U)!=0U;
            int64_t imm=symbol?0:
                (!byte && !word && !m->operands[0].register_64)?
                (int32_t)(uint32_t)m->operands[1].immediate:
                (int64_t)m->operands[1].immediate;
            bool short_imm=!symbol && imm>=-128 && imm<=127;
            bool acc=m->operands[0].reg==0U;
            unsigned base=byte?(acc?2U:3U):
                 word?(acc&&!short_imm?3U:short_imm?3U:4U):
                 (short_imm?3U:acc?5U:6U);
            *size=(word?1U:0U)+
                  (m->operands[0].register_64 || m->operands[0].reg>=8U ||
                   (byte && m->operands[0].reg>=4U &&
                    !(m->encoding&0x800U))?1U:0U)+base;
            return true;
        }
        case X64_STACK: *size=(m->operands[0].reg>=8U)?2U:1U;return true;
        case X64_INDIRECT_REG:
            *size=m->operands[0].reg>=8U?3U:2U;return true;
        case X64_MOV_IMM32:
            if(m->encoding&0x200U) {
                unsigned reg=m->operands[0].reg;
                *size=2U+(reg>=8U ||
                    (reg>=4U && !(m->encoding&0x800U))?1U:0U);
            }else if(m->encoding&UINT32_C(0x400))
                *size=4U+(m->operands[0].reg>=8U?1U:0U);
            else *size=m->operands[0].reg>=8U?6U:5U;
            return true;
        case X64_REG_BINARY:
            *size=((m->encoding&0x2000U)?1U:0U)+
                   ((m->encoding&0x400U)?1U:0U)+
                  ((m->encoding&0x100U)?1U:0U)+
                  ((m->operands[0].register_64 ||
                   m->operands[0].reg>=8U || m->operands[1].reg>=8U)?3U:2U);
            return true;
        default: minias_set_error(as,"unknown-x86_64-mcinst");return false;
    }
}

/* A branch is classified once by the x86 parser: X64_BRANCH and its retained
 * opcode carry every short/near alternative.  Neither relaxation nor final
 * encoding may consult a source mnemonic a second time. */
static uint32_t x64_branch_near_width(const MiniAsMCInst *m) {
    return m->encoding>=0x100U?6U:5U;
}

typedef enum X64BranchDistanceKind {
    X64_BRANCH_EXTERNAL,
    X64_BRANCH_LOCAL,
    X64_BRANCH_LOCAL_OVERFLOW
} X64BranchDistanceKind;

/* Resolve the final target displacement once for both the MC layout range
 * planner and emitter.  Internal addresses are ELF section offsets, and the
 * target ISA defines the PC base as the byte after the entire instruction.
 * External/cross-section references retain an explicit PC32/PLT32 fixup. */
static X64BranchDistanceKind x64_branch_distance(MiniAs *as,
                                                 const MiniAsStmt *stmt,
                                                 uint32_t instruction_width,
                                                 int64_t *distance) {
    const MiniAsMCInst *inst=stmt->mcinst;
    const MiniAsSymbolExpr *expr=inst->operands[0].symbol;
    MiniAsSymbol *symbol=minias_get_symbol(as,expr->name,false);
    int64_t delta;
    if(!symbol || !symbol->defined || symbol->section!=stmt->section)
        return X64_BRANCH_EXTERNAL;
    if(symbol->value>(uint64_t)INT64_MAX ||
       stmt->offset>(uint64_t)INT64_MAX-instruction_width)
        return X64_BRANCH_LOCAL_OVERFLOW;
    delta=(int64_t)symbol->value-
          ((int64_t)stmt->offset+(int64_t)instruction_width);
    if((expr->addend>0 && delta>INT64_MAX-expr->addend) ||
       (expr->addend<0 && delta<INT64_MIN-expr->addend))
        return X64_BRANCH_LOCAL_OVERFLOW;
    *distance=delta+expr->addend;
    return X64_BRANCH_LOCAL;
}

/* One typed target-owned alternative plan, consumed in both phases of the
 * Common monotonic fixed point.  terminal_size is the only size permitted
 * for a forced near branch; required_size depends on this layout iteration.
 * Every non-branch is an identity plan and never invents an alternative. */
static bool x64_plan_instruction_relaxation(MiniAs *as,
                                             const MiniAsStmt *stmt,
                                             MiniAsTargetRelaxPlan *plan) {
    const MiniAsMCInst *inst=stmt->mcinst;
    int64_t distance=0;
    X64BranchDistanceKind result;
    if(!inst || !plan) {
        minias_set_error(as,"x86-missing-typed-relaxation-instruction");
        return false;
    }
    plan->current_size=stmt->size;
    plan->required_size=stmt->size;
    plan->terminal_size=stmt->size;
    if(inst->opcode!=X64_BRANCH)return true;
    if(inst->encoding==0xe8U || stmt->size!=2U)return true;
    plan->terminal_size=x64_branch_near_width(inst);
    result=x64_branch_distance(as,stmt,2U,&distance);
    if(result!=X64_BRANCH_LOCAL || distance<-128 || distance>127)
        plan->required_size=plan->terminal_size;
    return true;
}

static bool x64_encode(MiniAs *as,const MiniAsStmt *s) {
    const MiniAsMCInst *m=s->mcinst;
    unsigned char code[16];size_t n=0;unsigned r,d;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    switch(m->opcode) {
        case X64_BMI2_SHIFT: {
            unsigned count=m->operands[0].reg;
            unsigned base=m->operands[1].reg;
            unsigned dst=m->operands[2].reg;
            bool wide=m->operands[2].register_64;
            code[n++]=0xc4U;
            code[n++]=(unsigned char)((dst>=8U?0U:0x80U)|
                          0x40U|(base>=8U?0U:0x20U)|2U);
            code[n++]=(unsigned char)((wide?0x80U:0U)|
                          (((~count)&15U)<<3U)|(m->encoding&3U));
            code[n++]=(unsigned char)((m->encoding&UINT32_C(0x10000))?
                       (m->encoding>>8U)&UINT32_C(0xff):UINT32_C(0xf7));
            code[n++]=(unsigned char)(0xc0U|((dst&7U)<<3U)|(base&7U));
            break;
        }
        case X64_SYSTEM_MOVE: {
            unsigned sys=m->operands[0].reg,gpr=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(sys>=8U || gpr>=8U)
                code[n++]=(unsigned char)(0x40U|
                    ((sys>>3U)<<2U)|(gpr>>3U));
            if(m->encoding&0x100U)code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|
                ((sys&7U)<<3U)|(gpr&7U));
            break;
        }
        case X64_DOUBLE_SHIFT: {
            unsigned src_reg=m->operands[0].reg;
            unsigned dst_reg=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(m->operands[0].register_64 || src_reg>=8U || dst_reg>=8U)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|
                    ((src_reg>>3U)<<2U)|(dst_reg>>3U));
            code[n++]=0x0fU;
            code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|
                    ((src_reg&7U)<<3U)|(dst_reg&7U));
            if(m->encoding&0x200U)
                code[n++]=(unsigned char)m->operands[2].immediate;
            break;
        }
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
            if(r>=4U && !(m->encoding&UINT32_C(0x1000)))
                code[n++]=(unsigned char)(0x40U|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|(r&7U));
            break;
        case X64_BINARY8:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(r>=8U||d>=8U||
               (r>=4U && !(m->encoding&UINT32_C(0x1000))) ||
               (d>=4U && !(m->encoding&UINT32_C(0x2000))))
                code[n++]=(unsigned char)(0x40U|((r>>3U)<<2U)|(d>>3U));
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|((r&7U)<<3U)|(d&7U));
            break;
        case X64_CMOV_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U || d>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)(0x40U+(m->encoding&15U));
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_UNARY:
            r=m->operands[0].reg;
            if(m->encoding&UINT32_C(0x10000))code[n++]=0xf3U;
            if(m->encoding&UINT32_C(0x20000))code[n++]=0xf2U;
            if(m->encoding&UINT32_C(0x2000))code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U ||
               ((m->encoding&UINT32_C(0x1000)) && r>=4U &&
                !(m->encoding&UINT32_C(0x4000))))
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            if(m->encoding&UINT32_C(0x8000))code[n++]=0x0fU;
            code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|
                (((m->encoding>>8U)&7U)<<3U)|(r&7U));
            break;
        case X64_BSWAP:
            r=m->operands[0].reg;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=0x0fU;
            code[n++]=(unsigned char)(0xc8U+(r&7U));
            break;
        case X64_TEST_IMM_REG: {
            unsigned j,bytes=(m->encoding&0x400U)?2U:4U;
            r=m->operands[0].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            if(r==0U)code[n++]=0xa9U;
            else {
                code[n++]=0xf7U;
                code[n++]=(unsigned char)(0xc0U|(r&7U));
            }
            for(j=0U;j<bytes;++j)
                code[n++]=(unsigned char)(m->operands[1].immediate>>(8U*j));
            break;
        }
        case X64_SHIFT_CL:
            r=m->operands[0].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U ||
               ((m->encoding&0x200U) && r>=4U &&
                !(m->encoding&0x800U)))
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=(m->encoding&0x200U)?0xd2U:0xd3U;
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            break;
        case X64_MOVZX_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if((!(m->encoding&0x800U) &&
                ((m->encoding&0xffU)==0xb6U ||
                 (m->encoding&0xffU)==0xbeU) && r>=4U) ||
               r>=8U || d>=8U ||
               m->operands[1].register_64)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[1].register_64?8U:0U)|
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_SHIFT_IMM:
            r=m->operands[0].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U ||
               ((m->encoding&0x200U) && r>=4U &&
                !(m->encoding&0x800U)))
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=(m->encoding&0x200U)?
                (m->operands[1].immediate==1U?0xd0U:0xc0U):
                (m->operands[1].immediate==1U?0xd1U:0xc1U);
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            if(m->operands[1].immediate!=1U)
                code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_TEST8_IMM:
            r=m->operands[0].reg;
            if(r==0U) code[n++]=0xa8U;
            else {
                if(r>=4U && !(m->encoding&0x800U))
                    code[n++]=(unsigned char)(0x40U|(r>>3U));
                code[n++]=0xf6U;
                code[n++]=(unsigned char)(0xc0U|(r&7U));
            }
            code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_IMUL_IMM: {
            int64_t val=(int64_t)m->operands[2].immediate;
            bool word=(m->encoding&0x400U)!=0U;
            unsigned j,bytes=val>=-128 && val<=127?1U:word?2U:4U;
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(word)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U || d>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=bytes==1U?0x6bU:0x69U;
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            for(j=0U;j<bytes;++j)
                code[n++]=(unsigned char)(m->operands[2].immediate>>(8U*j));
            break;
        }
        case X64_BIT_IMMREG:
            r=m->operands[0].reg;
            if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=0xbaU;
            code[n++]=(unsigned char)(0xc0U|
                ((m->encoding&7U)<<3U)|(r&7U));
            code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_PUSH_IMM: {
            bool symbol=m->operands[0].kind==MINIAS_MC_SYMBOL;
            int32_t imm=symbol?0:(int32_t)(uint32_t)m->operands[0].immediate;
            unsigned i;
            bool short_imm=!symbol && imm>=-128 && imm<=127;
            code[n++]=short_imm?0x6aU:0x68U;
            for(i=0U;i<(short_imm?1U:4U);++i)
                code[n++]=(unsigned char)((uint32_t)imm>>(8U*i));
            if(symbol) {
                const MiniAsSymbolExpr *expr=m->operands[0].symbol;
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                return minias_add_fixup(as,s->section,s->offset+1U,
                    MINIAS_FIXUP_X64_ABS32S,4U,expr->name,expr->addend);
            }
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
            MiniAsFixupKind kind=MINIAS_FIXUP_X64_PLT32;
            size_t width=s->size;
            int64_t displacement=0;
            X64BranchDistanceKind resolution;
            bool local;
            unsigned i;
            if(width!=2U && width!=x64_branch_near_width(m)) {
                minias_set_error(as,"x86-branch-invalid-layout:line=%zu",s->line);
                return false;
            }
            resolution=x64_branch_distance(as,s,(uint32_t)width,&displacement);
            local=resolution!=X64_BRANCH_EXTERNAL;
            if(resolution==X64_BRANCH_LOCAL_OVERFLOW) {
                minias_set_error(as,"x86-branch-address-or-addend-overflow:line=%zu",
                                 s->line);
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
            if(local &&
               ((width==2U && (displacement<-128 || displacement>127)) ||
                (width!=2U &&
                 (displacement<INT32_MIN || displacement>INT32_MAX)))) {
                minias_set_error(as,"x86-branch-displacement-range:line=%zu",
                                 s->line);
                return false;
            }
            for(i=0U;i<(width==2U?1U:4U);++i)
                code[n++]=(unsigned char)((uint32_t)displacement>>(8U*i));
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(local)return true;
            return minias_add_fixup(as,s->section,s->offset+width-4U,kind,
                                    4U,expr->name,expr->addend-4);
        }
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            const X64Memory *mem=m->operands[1].memory;
            const X64MemoryLayout layout=x64_memory_layout(m);
            if(layout.instruction_bytes>sizeof(code)) {
                minias_set_error(as,"x86-memory-instruction-exceeds-encoder-capacity");
                return false;
            }
            unsigned extra=layout.immediate_bytes;
            uint32_t immediate=extra?
                (uint32_t)m->operands[2].immediate:0U;
            unsigned low_base=mem->base&7U;
            unsigned bytes=layout.displacement_bytes;
            unsigned mod=!mem->has_base?0U:
                         bytes==0U?0U:bytes==1U?1U:2U;
            unsigned i;
            uint32_t disp=(uint32_t)mem->displacement;
            if(mem->address_space)
                code[n++]=(unsigned char)(mem->address_space==1U?0x64U:0x65U);
            r=m->operands[0].reg;
            if(layout.is_vex3) {
                unsigned count=m->operands[2].reg;
                code[n++]=0xc4U;
                code[n++]=(unsigned char)((r>=8U?0U:0x80U)|
                         (!mem->has_index || mem->index<8U?0x40U:0U)|
                         (!mem->has_base || mem->base<8U?0x20U:0U)|0x02U);
                code[n++]=(unsigned char)(
                         (m->operands[0].register_64?0x80U:0U)|
                         (((~count)&15U)<<3U)|(m->encoding&3U));
                code[n++]=0xf7U;
            } else {
                if(m->encoding&0x400U)code[n++]=0x66U;
                if(m->encoding&0x2000U)code[n++]=0xf3U;
                if(layout.has_rex)
                    code[n++]=(unsigned char)(0x40U |
                        (m->operands[0].register_64?8U:0U) |
                        ((r>>3U)<<2U) |
                        ((mem->has_index?mem->index>>3U:0U)<<1U) |
                        (mem->base>>3U));
                /* Width markers 0x200/0x400 do not imply the 0F escape. */
                if(m->encoding&0x100U)code[n++]=0x0fU;
                if(m->encoding&0x10000U)code[n++]=0x38U;
                code[n++]=(unsigned char)m->encoding;
            }
            code[n++]=(unsigned char)((((mem->pc_relative || !mem->has_base)?0U:mod)<<6U)|
                ((r&7U)<<3U)|
                (mem->pc_relative?5U:
                 (!mem->has_base || mem->has_index || low_base==4U)?
                     4U:low_base));
            if(layout.has_sib) {
                unsigned sib_scale=mem->scale==8U?3U:
                                   mem->scale==4U?2U:
                                   mem->scale==2U?1U:0U;
                code[n++]=(unsigned char)((sib_scale<<6U) |
                    ((mem->has_index?mem->index&7U:4U)<<3U) |
                    (mem->has_base?low_base:5U));
            }
            if(!mem->pc_relative && mem->symbol!=NULL) {
                size_t relocation_at=n;
                for(i=0U;i<4U;++i)code[n++]=0U;
                for(i=0U;i<extra;++i)
                    code[n++]=(unsigned char)(immediate>>(8U*i));
                if(n!=layout.instruction_bytes || n!=s->size) {
                    minias_set_error(as,"invalid-x86-symbolic-disp32-layout");
                    return false;
                }
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                if(!minias_add_fixup(as,s->section,
                    s->offset+relocation_at,MINIAS_FIXUP_X64_ABS32S,4U,
                    mem->symbol->name,mem->symbol->addend))return false;
                if(m->opcode==X64_IMM_MEMORY &&
                   m->operands[2].kind==MINIAS_MC_SYMBOL) {
                    const MiniAsSymbolExpr *value=m->operands[2].symbol;
                    if(extra!=4U || !value)return false;
                    return minias_add_fixup(as,s->section,s->offset+n-4U,
                         m->operands[0].register_64?
                             MINIAS_FIXUP_X64_ABS32S:MINIAS_FIXUP_ABS,
                         4U,value->name,value->addend);
                }
                return true;
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
                    if(n!=layout.instruction_bytes || n!=s->size) {
                        minias_set_error(as,"invalid-x86-rip-memory-layout");
                        return false;
                    }
                    if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                    if(!local &&
                       !minias_add_fixup(as,s->section,s->offset+relocation_at,
                           MINIAS_FIXUP_X64_PC32,4U,mem->symbol->name,
                           mem->symbol->addend-4-(int64_t)extra))return false;
                    if(m->opcode==X64_IMM_MEMORY &&
                       m->operands[2].kind==MINIAS_MC_SYMBOL) {
                        const MiniAsSymbolExpr *value=m->operands[2].symbol;
                        if(extra!=4U || !value)return false;
                        return minias_add_fixup(as,s->section,s->offset+n-4U,
                            m->operands[0].register_64?
                                MINIAS_FIXUP_X64_ABS32S:MINIAS_FIXUP_ABS,
                            4U,value->name,value->addend);
                    }
                    return true;
                }
            }
            for (i=0U;i<bytes;++i)
                code[n++]=(unsigned char)(disp>>(8U*i));
            for(i=0U;i<extra;++i)
                code[n++]=(unsigned char)(immediate>>(8U*i));
            if(n!=layout.instruction_bytes || n!=s->size) {
                minias_set_error(as,"invalid-x86-memory-layout");
                return false;
            }
            if(m->opcode==X64_IMM_MEMORY &&
               m->operands[2].kind==MINIAS_MC_SYMBOL) {
                const MiniAsSymbolExpr *imm=m->operands[2].symbol;
                if(extra!=4U || !imm || n!=s->size) {
                    minias_set_error(as,"invalid-x86-abs32s-memory-layout");
                    return false;
                }
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                return minias_add_fixup(as,s->section,s->offset+n-4U,
                    MINIAS_FIXUP_X64_ABS32S,4U,imm->name,imm->addend);
            }
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
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
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
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(src->kind!=MINIAS_MC_SYMBOL)return true;
            return minias_add_fixup(as,s->section,s->offset+3U,
                MINIAS_FIXUP_X64_ABS32S,4U,src->symbol->name,src->symbol->addend);
        }
        case X64_ALU_IMM: {
            unsigned i,group=m->encoding&7U;
            bool difference=m->operands[1].kind==MINIAS_MC_SYMBOL_DIFFERENCE;
            bool symbol=m->operands[1].kind==MINIAS_MC_SYMBOL || difference;
            uint64_t imm=symbol?0U:m->operands[1].immediate;
            int64_t resolved_diff=0;
            if(difference) {
                if(!minias_resolve_symbol_difference(
                     as,m->operands[1].difference,&resolved_diff) ||
                   resolved_diff<INT32_MIN || resolved_diff>INT32_MAX) {
                    minias_set_error(as,"invalid-x86-same-section-difference:%s",
                                     s->args);
                    return false;
                }
                imm=(uint32_t)(int32_t)resolved_diff;
            }
            bool byte=(m->encoding&0x200U)!=0U;
            bool word=(m->encoding&0x400U)!=0U;
            int64_t signed_imm=(!byte && !word &&
                !m->operands[0].register_64)?
                (int32_t)(uint32_t)imm:(int64_t)imm;
            bool short_imm=!symbol && signed_imm>=-128 && signed_imm<=127;
            bool accumulator;
            unsigned nbytes;
            r=m->operands[0].reg;
            accumulator=r==0U && (byte || !short_imm);
            if(word)code[n++]=0x66U;
            if(m->operands[0].register_64 || r>=8U ||
               (byte && r>=4U && !(m->encoding&0x800U)))
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) | (r>>3U));
            if(accumulator) {
                code[n++]=(unsigned char)(0x04U+(group<<3U)+(byte?0U:1U));
                nbytes=byte?1U:word?2U:4U;
            } else {
                code[n++]=byte?0x80U:short_imm?0x83U:0x81U;
                code[n++]=(unsigned char)(0xc0U | (group<<3U) | (r&7U));
                nbytes=byte || short_imm?1U:word?2U:4U;
            }
            for(i=0U;i<nbytes;++i)
                code[n++]=(unsigned char)(imm>>(8U*i));
            if(symbol && !difference) {
                const MiniAsSymbolExpr *expr=m->operands[1].symbol;
                if(nbytes!=4U) {
                    minias_set_error(as,"invalid-x86-alu-symbol-width");
                    return false;
                }
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                return minias_add_fixup(as,s->section,s->offset+n-4U,
                    m->operands[0].register_64?
                        MINIAS_FIXUP_X64_ABS32S:MINIAS_FIXUP_ABS,
                    4U,expr->name,expr->addend);
            }
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
            if(m->encoding&0x200U) {
                if(r>=8U || (r>=4U && !(m->encoding&0x800U)))
                    code[n++]=(unsigned char)(0x40U|(r>>3U));
                code[n++]=(unsigned char)(0xb0U+(r&7U));
                code[n++]=(unsigned char)m->operands[0].immediate;
            }else {
                if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
                if(r>=8U)code[n++]=0x41U;
                code[n++]=(unsigned char)(0xb8U+(r&7U));
                for(i=0U;i<((m->encoding&UINT32_C(0x400))?2U:4U);++i)
                    code[n++]=(unsigned char)
                        (m->operands[0].immediate>>(8U*i));
            }
            break;
        }
        case X64_REG_BINARY:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(m->encoding&0x2000U)code[n++]=0xf3U;
            if (m->operands[0].register_64 || r>=8U || d>=8U)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) |
                    (((m->encoding&0x800U)?d:r)>>3U)<<2U |
                    (((m->encoding&0x800U)?r:d)>>3U));
            if(m->encoding&0x100U)code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U |
                ((((m->encoding&0x800U)?d:r)&7U)<<3U) |
                (((m->encoding&0x800U)?r:d)&7U));
            break;
        default: minias_set_error(as,"bad-x86_64-mcinst");return false;
    }
    return minias_emit_instruction_bytes(as,s->section,code,n);
}

static bool x64_code_align(MiniAs *as,int section,size_t n) {
    /* GNU as x86-64 code alignment uses Intel multi-byte NOPs, not a run
     * of 0x90 bytes. Keep the exact GNU profile (1..11 bytes) in the Target,
     * while Common owns the ALIGN fragment and atomically commits its bytes.
     * Longer gaps are decomposed into 11-byte + remaining legal NOPs. */
    static const unsigned char nops[11][11]={
        {0x90},
        {0x66,0x90},
        {0x0f,0x1f,0x00},
        {0x0f,0x1f,0x40,0x00},
        {0x0f,0x1f,0x44,0x00,0x00},
        {0x66,0x0f,0x1f,0x44,0x00,0x00},
        {0x0f,0x1f,0x80,0x00,0x00,0x00,0x00},
        {0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00},
        {0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00},
        {0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00},
        {0x66,0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00}
    };
    /* GNU GAS leaves a single 0x90 at the beginning of a code-alignment
     * fragment immediately following a raw DATA fragment, then fills the
     * remainder with its canonical multi-byte table. This differs from
     * alignment after a machine INSN (or another ALIGN) and matters to
     * byte-exact Linux .S comparisons. The current retained statement IDs
     * let the Target inspect its immediate predecessor without reparsing. */
    if(n!=0U && as->emitting_fragment && as->emitting_fragment_id!=0U &&
       as->emitting_fragment_id<as->stmt_count) {
        const MiniAsStmt *owner=&as->stmts[as->emitting_fragment_id];
        const MiniAsStmt *previous=&as->stmts[as->emitting_fragment_id-1U];
        if(previous->kind==MINIAS_STMT_DATA && previous->section==section &&
           previous->subsection==owner->subsection &&
           previous->offset<=UINT64_MAX-previous->size &&
           previous->offset+previous->size==owner->offset) {
            if(!minias_emit_instruction_bytes(as,section,nops[0],1U))return false;
            --n;
        }
    }
    while(n!=0U) {
        size_t width=n>11U?11U:n;
        if(!minias_emit_instruction_bytes(as,section,nops[width-1U],width))
            return false;
        n-=width;
    }
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
    if(kind==MINIAS_FIXUP_PREL64)
        return width==8U?24U:0U; /* R_X86_64_PC64 */
    if(width!=4U)return 0U;
    if(kind==MINIAS_FIXUP_X64_PC32 || kind==MINIAS_FIXUP_PREL32)
        return 2U;  /* R_X86_64_PC32 */
    if(kind==MINIAS_FIXUP_X64_PLT32)return 4U; /* R_X86_64_PLT32 */
    if(kind==MINIAS_FIXUP_X64_ABS32S)return 11U; /* R_X86_64_32S */
    return 0U;
}

/* Public capability checks are semantic, not a probe of the ELF numerical
 * relocation code.  A backend may support a fixup family even where its
 * encoded relocation number is zero; Common cannot infer a policy from an
 * architecture-specific value.  x86 explicitly lacks ELF ADD/SUB pairs. */
static bool x64_supports_fixup(MiniAsFixupKind kind,unsigned width) {
    switch(kind) {
        case MINIAS_FIXUP_ABS:
            return width==4U || width==8U;
        case MINIAS_FIXUP_X64_ABS64:
        case MINIAS_FIXUP_PREL64:
            return width==8U;
        case MINIAS_FIXUP_X64_PC32:
        case MINIAS_FIXUP_PREL32:
        case MINIAS_FIXUP_X64_PLT32:
        case MINIAS_FIXUP_X64_ABS32S:
            return width==4U;
        default:
            return false;
    }
}
/* The GNU EFI thunk switches to IA-32 *within* ELF64.
 * This is per-statement Target mode, never a driver/Core ELF-class switch. */
static bool x64_parse_directive(MiniAs *as,const char *op,const char *args,
                                size_t line,bool *handled) {
    bool code32=!strcmp(op,".code32");
    *handled=code32 || !strcmp(op,".code64");
    if(!*handled)return true;
    if(*args) {
        minias_set_error(as,"unsupported-x86_64-code-mode:%s:line=%zu",
                         args,line);
        return false;
    }
    as->target_options=code32?X64_CODE32:0U;
    return true;
}
/* GNU GAS treats native register tokens in .if as distinct symbolic
 * identities, not integer zero or Common absolute symbols. This ISA
 * dialect callback handles equality/inequality of known registers only;
 * unsupported arithmetic and non-register names remain fail-closed.
 * GNU considers %rax != %eax, even when both encode physical GPR 0. */
static bool x64_gnu_register_token(const char *token) {
    unsigned reg;
    bool wide;
    return x64_register(token,&reg,&wide) ||
           x64_register16(token,&reg) ||
           x64_register8(token,&reg) ||
           x64_high_register8(token,&reg) ||
           x64_xmm_register(token,&reg);
}
static bool x64_evaluate_target_if(const char *expression, bool *result) {
    char buffer[256],*left,*right,*delimiter;
    bool unequal=false;
    size_t length;
    if(!expression || !result)return false;
    length=strlen(expression);
    if(length==0U || length>=sizeof(buffer))return false;
    memcpy(buffer,expression,length+1U);
    left=minias_trim(buffer);
    delimiter=strstr(left,"==");
    if(!delimiter) {
        delimiter=strstr(left,"!=");
        unequal=true;
    }
    if(!delimiter)return false;
    *delimiter='\0';
    right=minias_trim(delimiter+2);
    left=minias_trim(left);
    if(!x64_gnu_register_token(left) ||
       !x64_gnu_register_token(right))return false;
    *result=(strcmp(left,right)==0)!=unequal;
    return true;
}
static bool x64_named_relocation(const char *name,
                                  MiniAsFixupKind *kind,unsigned *width) {
    static const struct {
        const char *name;
        MiniAsFixupKind kind;
        unsigned width;
    } forms[]={
        {"R_X86_64_PC32",MINIAS_FIXUP_X64_PC32,4U},
        {"R_X86_64_PLT32",MINIAS_FIXUP_X64_PLT32,4U},
        {"R_X86_64_32",MINIAS_FIXUP_ABS,4U},
        {"R_X86_64_32S",MINIAS_FIXUP_X64_ABS32S,4U},
        {"R_X86_64_64",MINIAS_FIXUP_ABS,8U}
    };
    unsigned i;
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i) {
        if(!strcmp(name,forms[i].name)) {
            *kind=forms[i].kind;*width=forms[i].width;
            return true;
        }
    }
    return false;
}
static const MiniAsTargetOps ops={
    .name="x86_64",.comment_prefix='#',.elf_machine=62U,
    .resolve_profile=x64_profile,.parse_mcinst=x64_parse,
    .parse_target_directive=x64_parse_directive,
    .evaluate_target_if=x64_evaluate_target_if,
    .measure_mcinst=x64_measure,.encode=x64_encode,
    .plan_instruction_relaxation=x64_plan_instruction_relaxation,
    .append_code_alignment=x64_code_align,
    .fixup_relocation_type=x64_reloc,
    .supports_fixup=x64_supports_fixup,
    .parse_named_relocation=x64_named_relocation,
    .default_exec_alignment=1U,
    .word_directive_width=2U,.align_directive_bytes=true,
    .dot_relative_fixup=MINIAS_FIXUP_PREL32,
    .common_default_alignment_cap=16U,
    /* System V AMD64 DWARF: code slots are bytes, saved GPRs occupy
     * eight-byte stack slots, return address=RIP(16), CFA base=RSP(7).
     * The Common CFI encoder already owns the real .debug_frame FDE. */
    .cfi_implicit_eh_frame=true,
    .fold_local_abs_relocation_to_section=true,
    .cfi_code_align=1U,
    .cfi_data_align=-8,
    .cfi_return_reg=16U,
    .cfi_stack_reg=7U,
    .cfi_initial_cfa_offset=8U,
    .cfi_address_size=8U,
    .cfi_initial_ra_saved=true
};
const MiniAsTargetOps *minias_target_x86_64(void){return &ops;}
