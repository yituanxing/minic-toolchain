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
#define X64_CODE16 UINT32_C(2)
#define X64_CODE16_GCC UINT32_C(3)

/* The IA-32 thunk mode is ISA parser state, not a Common assembler option.
 * Each instruction captures the bits at its lexical site so a later .code64
 * directive cannot change an earlier MCInst during layout/relaxation. */
typedef struct X64TargetState {
    uint32_t code_mode;
} X64TargetState;
static bool x64_cond_code(const char *suffix,unsigned *cc);

/* One GNU unsuffixed-width alias inventory for both the legacy ISA gate and
 * the register-directed suffix inference. The latter still rejects any
 * operand whose width cannot be established from a real register token. */
static bool x64_infers_width(const char *name) {
    static const char *const names[]={
        "mov","cmp","test","add","sub","adc","sbb",
        "and","or","xor","push","pop",
        "shl","sal","shr","sar","rol","ror",
        "inc","dec","not","neg","mul","imul","div","idiv",
        "bt","bts","btr","btc","xadd","cmpxchg","shld","shrd"
    };
    size_t i;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i)
        if(!strcmp(name,names[i]))return true;
    return false;
}

/* The compatibility ISA is deliberately allow-listed. An instruction can
 * enter this set only after the x86 machine encoder supports its actual
 * 16/32-bit semantics; the other 64-bit decoder forms remain fail-closed. */
static bool x64_legacy_opcode_allowed(const char *name,uint32_t mode) {
    static const char *const names[]={
        "pushl","popl","pushw","popw","push","pop",
        "pushf","popf","pushfl","popfl","pushal","popal",
        "mov","movl","movw","movb","movzwl","movzbl",
        "nop","ret","retl","leave","lretw","lret",
        "cli","sti","cld","hlt","wbinvd","pause","cpuid","lfence",
        "syscall",
        "call","calll","jmp","jmpl","ljmpw","ljmpl","ljmp",
        "lcallw","lcalll",
        "cmpb","cmpw","cmpl","testb","testw","testl",
        "addw","addl","andb","andw","orl","xorl","xorw","andl","orb",
        "subw","subl","shrw","shrl","sall","incl","decl","sbbl","adcl",
        "imull","lgdtl","lidtl","lmsw","lsl",
        "lock","btl","btsl","btrl","btcl","shldl","shrdl",
        "rep","repe","repz","repne","repnz",
        "int","rdtsc","sysenter","rdmsr","wrmsr",
        "movsl","movsb","stosl","stosb","leal",
        "inb","inw","inl","outb","outw","outl",
        "rdtscp","negl","divl","mull","movsbl",
        /* Linux real-mode boot GCC uses these already-decoded machine
         * forms; keep the mode gate aligned with actual target support. */
        "fninit","cltd","notl","orw","shll","addb","incb",
        /* GNU Linux boot's remaining real/protected-mode forms use
         * already-owned typed zero, unary, MOVSX and system-memory
         * encoders.  Keep only their proven legacy-mode spellings. */
        "fnstsw","idivl","movswl","cmpsb","stosw",
        "ltr","lgdt","lldt",
        /* Further genuine Linux boot GCC/EFI spellings after the first
         * grouped ISA blocker batch, all backed by existing MC forms. */
        "fnstcw","sgdtl","sidtl","lretl"
    };
    size_t i;
    if(x64_infers_width(name))return true;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i) {
        if(!strcmp(name,names[i]))return true;
    }
    /* Legacy Jcc/SETcc use the one target condition table. Parse and
     * encode must still validate operands; mode acceptance is not a second
     * catalog of aliases that can drift from the actual ISA descriptor. */
    {
        unsigned cc;
        if((name[0]=='j' && x64_cond_code(name+1U,&cc)) ||
           (!strncmp(name,"set",3U) && x64_cond_code(name+3U,&cc)) ||
           (!strncmp(name,"cmov",4U) && x64_cond_code(name+4U,&cc)))
            return true;
    }
    /* The mode does not license 64-bit forms even if their byte values
     * happen to alias meaningful legacy opcodes. */
    (void)mode;
    return false;
}

enum { X64_ZERO = 1, X64_REG_BINARY, X64_STACK, X64_MOV_IMM32, X64_ALU_IMM, X64_REG_MEMORY, X64_BRANCH, X64_IMM_MEMORY, X64_MOVQ_IMMREG, X64_MOVABS_IMMREG, X64_SETCC, X64_BINARY8, X64_MOVSLQ, X64_MOVZX_REG, X64_SHIFT_REGISTER, X64_TEST8_IMM, X64_PUSH_IMM, X64_CMOV_REG, X64_UNARY, X64_INDIRECT_REG, X64_BSWAP, X64_TEST_IMM_REG, X64_IMUL_IMM, X64_BIT_IMMREG, X64_DOUBLE_SHIFT, X64_SYSTEM_MOVE, X64_BMI2_SHIFT, X64_XMM_IMM_REG, X64_FAR_BRANCH, X64_MOFFS };
#define X64_ENCODING_NOTRACK UINT32_C(0x80000)
#define X64_ENCODING_F2 UINT32_C(0x100000)
#define X64_ENCODING_IMM8 UINT32_C(0x200000)
#define X64_ENCODING_LOCK UINT32_C(0x400000)
#define X64_FORM_I386_GOTPC UINT32_C(1)
/* target_form's low bits select the existing machine alternative; the high
 * byte retains the decoded operand width.  Unlike a source mnemonic this
 * survives normalization (`btr` -> `btrl`) and relaxation unchanged. */
#define X64_FORM_MASK UINT32_C(0x00ffffff)
#define X64_FORM_WIDTH_SHIFT 24U
#define X64_FORM_VALUE(m) ((m)->target_form & X64_FORM_MASK)
#define X64_FORM_OPERAND_BITS(m) ((m)->target_form >> X64_FORM_WIDTH_SHIFT)

typedef enum X64DisplacementReloc {
    X64_DISP_ABSOLUTE=0,
    X64_DISP_GOTPCREL,
    X64_DISP_I386_GOT32X,
    X64_DISP_I386_GOTOFF,
    X64_DISP_I386_GOTPC
} X64DisplacementReloc;

typedef enum X64ShiftCountForm {
    X64_SHIFT_COUNT_IMMEDIATE = 0,
    X64_SHIFT_COUNT_CL = 1
} X64ShiftCountForm;

/* AT&T addressing is an x86-specific machine operand; neither its ModR/M
 * register rules nor FS/GS selector have meaning in the shared MC model.
 * Common retains an opaque owning pointer; the optional SymbolRef is an
 * inline token and never owns a second heap allocation or ELF symbol. */
typedef struct X64Memory {
    bool has_base;
    bool pc_relative;
    bool address_32; /* Captured at instruction site for .code16gcc/.code32. */
    bool address_16; /* Real-mode ModR/M, including disp16-only r/m=110. */
    unsigned address16_rm;
    unsigned address_space;
    unsigned base;
    bool has_index;
    unsigned index;
    unsigned scale;
    int64_t displacement;
    /* Stable Common symbol identity, never an ELF SymbolID or x86 address
     * component. A name pool token does not create an object symbol. */
    bool has_symbol;
    /* Resolved only after final fragment layout; no ELF relocation. */
    bool has_difference;
    /* x86 ELF relocation flavor is an addressing-mode property; Common
     * still owns only the canonical undecorated symbol reference. */
    X64DisplacementReloc reloc_flavor;
    MiniAsSymbolRef symbol;
    MiniAsSymbolDifferenceRef difference;
} X64Memory;

static void x64_dispose_memory(void *opaque) {
    free(opaque);
}

/* The parser transfers ownership of one fully parsed x86 addressing mode to
 * the retained MC operand through this single boundary.  Both the machine
 * address and its bound displacement SymbolRef copy atomically by value. */
static bool x64_attach_memory_operand(MiniAs *as, MiniAsMCInst *inst,
                                      X64Memory *parsed,
                                      const char *failure) {
    X64Memory *owned = malloc(sizeof(*owned));
    if (owned == NULL) {
        minias_set_error(as, "%s", failure);
        return false;
    }
    *owned = *parsed;
    inst->operands[1].kind = MINIAS_MC_MEMORY;
    inst->operands[1].memory = owned;
    return true;
}

/* Target parsers attach name-independent symbolic operands only after the
 * source expression has passed its ISA-specific range and form checks.
 * Common owns the name token; MCInst owns this small retained reference. */
static bool x64_attach_symbol_operand(MiniAs *as, MiniAsMCInst *inst,
                                      unsigned slot,
                                      const MiniAsSymbolExpr *expression,
                                      const char *failure) {
    MiniAsSymbolRef *bound=malloc(sizeof(*bound));
    if(bound==NULL) {
        minias_set_error(as,"%s",failure);
        return false;
    }
    if(!minias_bind_symbol_ref(as,expression,bound)) {
        free(bound);
        return false;
    }
    inst->operands[slot].kind=MINIAS_MC_BOUND_SYMBOL;
    inst->operands[slot].bound_symbol=bound;
    return true;
}

static bool x64_attach_symbol_difference_operand(
    MiniAs *as,MiniAsMCInst *inst,unsigned slot,
    const MiniAsSymbolDifference *difference,const char *failure) {
    MiniAsSymbolDifferenceRef *bound=malloc(sizeof(*bound));
    if(bound==NULL) {
        minias_set_error(as,"%s",failure);
        return false;
    }
    if(!minias_bind_symbol_difference_ref(as,difference,bound)) {
        free(bound);
        return false;
    }
    inst->operands[slot].kind=MINIAS_MC_BOUND_SYMBOL_DIFFERENCE;
    inst->operands[slot].bound_difference=bound;
    return true;
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
/* GAS symbol@MODIFIER is machine syntax, not part of the Common symbol
 * name. Preserve the shared expression parser by removing only the
 * exact supported x86 modifier, retaining any trailing arithmetic. */
static bool x64_strip_elf_symbol_modifier(const char *raw,
                                          const char *modifier,
                                          char output[X64_OPERAND_CAP]) {
    const char *at=strstr(raw,modifier);
    const size_t marker_len=strlen(modifier);
    const size_t full_len=strlen(raw);
    const char *tail;
    size_t before,remaining;
    if(at==NULL || at==raw || full_len>=X64_OPERAND_CAP)return false;
    before=(size_t)(at-raw);
    tail=at+marker_len;
    if(*tail!='\0' && *tail!='+' && *tail!='-')return false;
    remaining=strlen(tail);
    if(before+remaining>=X64_OPERAND_CAP)return false;
    memcpy(output,raw,before);
    memcpy(output+before,tail,remaining+1U);
    return true;
}

static bool x64_symbolic_displacement(const char *raw,
                                      MiniAsSymbolExpr *expr,
                                      X64DisplacementReloc *flavor,
                                      bool elf32) {
    char buf[X64_OPERAND_CAP],*operand,*p;
    char normalized[X64_OPERAND_CAP];
    size_t len=strlen(raw);
    *flavor=X64_DISP_ABSOLUTE;
    unsigned depth=0U;
    if(!len || len>=sizeof(buf))return false;
    memcpy(buf,raw,len+1U);
    operand=x64_unwrap_disp(buf);
    if(x64_strip_elf_symbol_modifier(operand,"@GOTPCREL",normalized)) {
        X64DisplacementReloc inner;
        if(elf32)return false;
        if(!x64_symbolic_displacement(normalized,expr,&inner,elf32) ||
           inner!=X64_DISP_ABSOLUTE)return false;
        *flavor=X64_DISP_GOTPCREL;
        return true;
    }
    if(elf32 && x64_strip_elf_symbol_modifier(operand,"@GOTOFF",normalized)) {
        X64DisplacementReloc inner;
        if(!x64_symbolic_displacement(normalized,expr,&inner,elf32) ||
           inner!=X64_DISP_ABSOLUTE)return false;
        *flavor=X64_DISP_I386_GOTOFF;
        return true;
    }
    if(elf32 && x64_strip_elf_symbol_modifier(operand,"@GOT",normalized)) {
        X64DisplacementReloc inner;
        if(!x64_symbolic_displacement(normalized,expr,&inner,elf32) ||
           inner!=X64_DISP_ABSOLUTE)return false;
        *flavor=X64_DISP_I386_GOT32X;
        return true;
    }
    if(minias_parse_symbol_addend(operand,expr)) {
        if(elf32 && !strcmp(expr->name,"_GLOBAL_OFFSET_TABLE_"))
            *flavor=X64_DISP_I386_GOTPC;
        return true;
    }
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

static bool x64_parse_base_memory(MiniAs *as,const char *text, X64Memory *out) {
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
    bool wide, symbolic=false, difference=false, pc_relative=false;
    X64DisplacementReloc reloc_flavor=X64_DISP_ABSOLUTE;
    const X64TargetState *state=(const X64TargetState *)as->target_state;
    const bool code16=state!=NULL &&
        (state->code_mode==X64_CODE16 || state->code_mode==X64_CODE16_GCC);
    bool address32=state!=NULL && state->code_mode!=0U;
    bool address16=false;
    unsigned legacy_rm=0U;
    MiniAsSymbolExpr symbolic_expr;
    MiniAsSymbolDifference difference_expr;
    long long displacement=0;
    if(strncmp(text,"%fs:",4U)==0)segment_space=1U;
    else if(strncmp(text,"%gs:",4U)==0)segment_space=2U;
    else if(strncmp(text,"%cs:",4U)==0)segment_space=3U;
    else if(strncmp(text,"%ds:",4U)==0)segment_space=4U;
    else if(strncmp(text,"%es:",4U)==0)segment_space=5U;
    else if(strncmp(text,"%ss:",4U)==0)segment_space=6U;
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
                if(x64_symbolic_displacement(operand,&expr,
                       &reloc_flavor,as->output_elf32) &&
                   reloc_flavor!=X64_DISP_GOTPCREL)symbolic=true;
                else if(minias_parse_symbol_difference(operand,
                        &difference_expr))difference=true;
                else return false;
                displacement=0;
            }
        }
        memset(out,0,sizeof(*out));
        out->address_16=code16;
        out->address16_rm=6U;
        out->address_32=address32 && !code16;
        out->scale=1U;out->address_space=space;
        out->displacement=(int64_t)displacement;
        out->reloc_flavor=reloc_flavor;
        if(symbolic) {
            if(!minias_bind_symbol_ref(as,&expr,&out->symbol))return false;
            out->has_symbol=true;
        }
        if(difference) {
            if(!minias_bind_symbol_difference_ref(as,&difference_expr,
                    &out->difference))return false;
            out->has_difference=true;
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
                if(x64_symbolic_displacement(dispbuf,&symbolic_expr,
                       &reloc_flavor,as->output_elf32) &&
                   symbolic_expr.addend>=INT64_MIN+4)
                    symbolic=true;
                else if(minias_parse_symbol_difference(dispbuf,
                        &difference_expr))difference=true;
                else return false;
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
    if(code16 && count<=2U && regs[0][0]!='\0') {
        unsigned first,second=0U;
        bool first16=x64_register16(regs[0],&first) && first<8U;
        bool second16=count==2U &&
            x64_register16(regs[1],&second) && second<8U;
        if(first16 && (count==1U || second16)) {
            /* 16-bit address-mode rm: BX+SI, BX+DI, BP+SI, BP+DI,
             * SI, DI, [disp16/BP], BX. These are ISA ModR/M encodings,
             * not a 32-bit SIB with an accidentally truncated register. */
            if(count==1U) {
                legacy_rm=first==6U?4U:first==7U?5U:
                          first==5U?6U:first==3U?7U:8U;
            }else {
                legacy_rm=(first==3U && second==6U) ||
                          (first==6U && second==3U)?0U:
                          (first==3U && second==7U) ||
                          (first==7U && second==3U)?1U:
                          (first==5U && second==6U) ||
                          (first==6U && second==5U)?2U:
                          (first==5U && second==7U) ||
                          (first==7U && second==5U)?3U:8U;
            }
            if(legacy_rm>=8U)return false;
            address16=true;address32=false;
            base=first;index=second;
        }
    }
    if(address16) {
        /* Already validated the full 16-bit base/index pair. */
    } else if(count==1U && strcmp(regs[0],"%rip")==0) {
        if(state!=NULL && state->code_mode!=0U)return false;
        /* In long mode RIP-relative addresses use DS implicitly. GNU GAS
         * accepts an explicit %ds: spelling but omits its redundant 3E
         * prefix. Preserve this in the typed addressing mode, not through
         * a separate opcode-specific encoding exception. */
        if(segment_space==4U)segment_space=0U;
        pc_relative=true;
    } else if(count>=2U && regs[0][0]=='\0') {
        /* GNU disp(,%index,scale): no base, mandatory SIB disp32. */
        base=5U;
    } else {
        if(count<1U || !x64_register(regs[0],&base,&wide) ||
           wide==address32)
            return false;
    }
    if((reloc_flavor==X64_DISP_GOTPCREL && !pc_relative) ||
       (reloc_flavor!=X64_DISP_ABSOLUTE &&
        reloc_flavor!=X64_DISP_GOTPCREL && pc_relative))return false;
    if (count>=2U && !address16) {
        if (!x64_register(regs[1],&index,&wide) ||
            wide==address32 ||
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
    out->address_32=address32 && code16;
    out->address_16=address16;
    out->address16_rm=legacy_rm;
    out->has_base=!pc_relative && regs[0][0]!='\0';
    out->pc_relative=pc_relative;
    out->address_space=segment_space;
    out->base=base;out->has_index=(count>=2U);
    out->index=index;out->scale=(unsigned)scale;
    out->displacement=(int64_t)displacement;
    out->reloc_flavor=reloc_flavor;
    if(symbolic) {
        /* GNU sym+addend(%base,%index,scale) uses an ELF ABS32S relocation
         * in a forced disp32 slot. This is distinct from RIP+PC32; target
         * uses existing typed memory and the same reloc/fixup producer. */
        if(!minias_bind_symbol_ref(as,&symbolic_expr,&out->symbol))return false;
        out->has_symbol=true;
    }
    if(difference) {
        if(!minias_bind_symbol_difference_ref(as,&difference_expr,
                &out->difference))return false;
        out->has_difference=true;
    }
    return true;
}

static unsigned x64_disp_bytes(const X64Memory *mem) {
    /* Symbolic displacements keep their full width until final layout:
     * references need a relocation slot, same-section differences are
     * resolved later. Otherwise we could prematurely choose disp8. */
    if(mem->has_symbol || mem->has_difference)
        return mem->address_16?2U:4U;
    if(mem->address_16) {
        if(!mem->has_base)return 2U;
        if(mem->displacement==0 && mem->address16_rm!=6U)return 0U;
        return mem->displacement>=-128 && mem->displacement<=127?1U:2U;
    }
    /* Segment overrides prefix an address, not its ModR/M/SIB mode. */
    if (mem->pc_relative || !mem->has_base)return 4U;
    if (mem->displacement==0 && (mem->base & 7U)!=5U) return 0U;
    return mem->displacement>=-128 && mem->displacement<=127 ? 1U : 4U;
}

static bool x64_is_mem_candidate(const char *text) {
    return strchr(text, '(') != NULL ||
           strncmp(text,"%gs:",4U)==0 || strncmp(text,"%fs:",4U)==0 ||
           strncmp(text,"%cs:",4U)==0 || strncmp(text,"%ds:",4U)==0 ||
           strncmp(text,"%es:",4U)==0 || strncmp(text,"%ss:",4U)==0 ||
           (*text && *text!='%' && *text!='$');
}

/* One ISA descriptor is the opcode authority for register/register,
 * register/memory, memory/register and Group-1 immediate forms.  A zero
 * opcode is VALID (addb %r8b,(%rax) uses 00 /r); availability is an
 * independent typed fact and never inferred from its numeric encoding. */
typedef struct X64BinaryFormDescriptor {
    const char *stem;
    unsigned rm_destination_opcode;
    unsigned rm_source_opcode;
    bool has_rm_destination_opcode;
    bool has_rm_source_opcode;
    bool has_immediate_group;
    unsigned immediate_group;
} X64BinaryFormDescriptor;

static const X64BinaryFormDescriptor x64_binary_forms[]={
    {"mov", 0x89U,0x8bU,true,true,false,0U},
    {"add", 0x01U,0x03U,true,true,true,0U},
    {"or",  0x09U,0x0bU,true,true,true,1U},
    {"adc", 0x11U,0x13U,true,true,true,2U},
    {"sbb", 0x19U,0x1bU,true,true,true,3U},
    {"and", 0x21U,0x23U,true,true,true,4U},
    {"sub", 0x29U,0x2bU,true,true,true,5U},
    {"xor", 0x31U,0x33U,true,true,true,6U},
    {"cmp", 0x39U,0x3bU,true,true,true,7U},
    {"test",0x85U,0x85U,true,true,false,0U}
};

static const X64BinaryFormDescriptor *x64_binary_form(
    const char *name,unsigned *width) {
    const size_t n=strlen(name);
    size_t i;
    if(n<2U)return NULL;
    *width=name[n-1U]=='b'?8U:name[n-1U]=='w'?16U:
           name[n-1U]=='l'?32U:name[n-1U]=='q'?64U:0U;
    if(*width==0U)return NULL;
    for(i=0U;i<sizeof(x64_binary_forms)/sizeof(x64_binary_forms[0]);++i) {
        const X64BinaryFormDescriptor *form=&x64_binary_forms[i];
        const size_t stem_length=strlen(form->stem);
        if(n==stem_length+1U &&
           strncmp(name,form->stem,stem_length)==0)return form;
    }
    return NULL;
}

static unsigned x64_binary_opcode(const X64BinaryFormDescriptor *form,
                                   bool memory_source,unsigned width) {
    const unsigned base=memory_source?form->rm_source_opcode:
                                       form->rm_destination_opcode;
    return base-(width==8U?1U:0U);
}

static bool x64_alu_group(const char *name,unsigned *group) {
    unsigned width;
    const X64BinaryFormDescriptor *form=x64_binary_form(name,&width);
    (void)width;
    if(form==NULL || !form->has_immediate_group)return false;
    *group=form->immediate_group;
    return true;
}

/* The lexer may infer an unsuffixed AT&T width from a register and then
 * normalize the instruction before parsing.  Capture that final ISA width
 * exactly once in the retained target form.  Emission/relaxation must never
 * rediscover operand sizes by comparing original source spellings. */
static unsigned x64_legacy_operand_bits(const MiniAsStmt *s,
                                        const MiniAsMCInst *m) {
    const char *name=s->op;
    size_t length;
    char suffix;
    if(m->opcode==X64_ZERO) {
        if(!strcmp(name,"rep") || !strcmp(name,"repz") ||
           !strcmp(name,"repe") || !strcmp(name,"repne") ||
           !strcmp(name,"repnz"))name=s->args;
        if(!strcmp(name,"pushfl") || !strcmp(name,"popfl") ||
           !strcmp(name,"pushal") || !strcmp(name,"popal") ||
           !strcmp(name,"retl") || !strcmp(name,"cltd") ||
           !strcmp(name,"cwtl"))return 32U;
        if(!strcmp(name,"lretw"))return 16U;
        if(!strcmp(name,"lretl"))return 32U;
        if(s->site.mode_bits==X64_CODE16_GCC &&
           (!strcmp(name,"ret") || !strcmp(name,"leave")))return 32U;
        if(strncmp(name,"movs",4U) && strncmp(name,"stos",4U) &&
           strncmp(name,"scas",4U) && strncmp(name,"cmps",4U) &&
           strncmp(name,"lods",4U) && strncmp(name,"outs",4U) &&
           strncmp(name,"ins",3U) && strncmp(name,"in",2U) &&
           strncmp(name,"out",3U))return 0U;
    }else if(m->opcode==X64_BRANCH) {
        if(m->encoding!=0xe8U)return 0U;
        return s->site.mode_bits==X64_CODE16_GCC ||
               !strcmp(name,"calll")?32U:16U;
    }else if(m->opcode==X64_FAR_BRANCH) {
        return m->encoding==4U?32U:16U;
    }else if(m->opcode==X64_STACK || m->opcode==X64_PUSH_IMM) {
        if(!strcmp(name,"pushw") || !strcmp(name,"popw"))return 16U;
        if(!strcmp(name,"pushl") || !strcmp(name,"popl") ||
           s->site.mode_bits==X64_CODE16_GCC)return 32U;
        return 16U;
    }else if(m->opcode==X64_INDIRECT_REG ||
             (m->opcode==X64_REG_MEMORY &&
              (m->encoding&UINT32_C(0xff))==0xffU &&
              m->operands[0].reg>=2U && m->operands[0].reg<=5U)) {
        const unsigned group=m->opcode==X64_INDIRECT_REG?
            m->encoding&7U:m->operands[0].reg;
        if(group==3U || group==5U) {
            if(!strcmp(name,"lcallw") || !strcmp(name,"ljmpw"))return 16U;
            if(!strcmp(name,"lcalll") || !strcmp(name,"ljmpl") ||
               (group==4U && !strcmp(name,"jmpl")))return 32U;
        }
        return (group==2U || group==3U) &&
               s->site.mode_bits==X64_CODE16_GCC?32U:16U;
    }else if(m->opcode==X64_SYSTEM_MOVE &&
             (m->encoding&UINT32_C(0x100))) {
        /* MOV to/from control/debug registers is never 66-extended. */
        return 0U;
    }else {
        switch(m->opcode) {
            case X64_REG_BINARY: case X64_REG_MEMORY:
            case X64_ALU_IMM: case X64_IMM_MEMORY:
            case X64_MOV_IMM32: case X64_MOVZX_REG:
            case X64_SHIFT_REGISTER: case X64_IMUL_IMM:
            case X64_UNARY: case X64_CMOV_REG:
            case X64_TEST_IMM_REG: case X64_BIT_IMMREG:
            case X64_DOUBLE_SHIFT: case X64_MOFFS:
            case X64_SYSTEM_MOVE: break;
            default: return 0U;
        }
    }
    length=strlen(name);
    if(length==0U)return 0U;
    suffix=name[length-1U];
    return suffix=='b'?8U:suffix=='w'?16U:
           suffix=='l'?32U:suffix=='q'?64U:0U;
}

static bool x64_parse(MiniAs *as, MiniAsStmt *s) {
    static const struct {const char *op; unsigned char bytes[3]; unsigned size;} zeros[]={
        {"nop",{0x90},1}, {"ret",{0xc3},1}, {"retq",{0xc3},1},
        {"retl",{0xc3},1}, {"lretw",{0xcb},1},
        {"lretl",{0xcb},1},
        {"pushfl",{0x9c},1}, {"popfl",{0x9d},1},
        {"pushal",{0x60},1}, {"popal",{0x61},1},
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
        {"sysenter",{0x0f,0x34},2},
        {"rdmsr",{0x0f,0x32},2},{"wrmsr",{0x0f,0x30},2},
        {"rdpmc",{0x0f,0x33},2},{"fninit",{0xdb,0xe3},2},
        {"fnclex",{0xdb,0xe2},2},
        {"fprem",{0xd9,0xf8},2},
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
    /* Extended reg/memory forms beyond the common ALU/MOV/TEST descriptor.
     * Availability is explicit: an absent direction has no opcode at all. */
    static const struct {
        const char *op; unsigned rm_dest, rm_src; bool wide;
        bool has_rm_dest,has_rm_src;
    } regmem[] = {
        {"xchgq",0x87,0x87,true,true,true},
        {"xchgl",0x87,0x87,false,true,true},
        {"leaq",0,0x8d,true,false,true},
        {"leal",0,0x8d,false,false,true},
        /* 0F xx /r: same typed memory ModR/M machinery. */
        {"btq",0xa3,0,true,true,false},{"btl",0xa3,0,false,true,false},
        {"btsq",0xab,0,true,true,false},{"btsl",0xab,0,false,true,false},
        {"btrq",0xb3,0,true,true,false},{"btrl",0xb3,0,false,true,false},
        {"btcq",0xbb,0,true,true,false},{"btcl",0xbb,0,false,true,false},
        {"xaddq",0xc1,0,true,true,false},{"xaddl",0xc1,0,false,true,false},
        {"cmpxchgq",0xb1,0,true,true,false},{"cmpxchgl",0xb1,0,false,true,false},
        {"xaddw",0xc1,0,false,true,false},{"cmpxchgw",0xb1,0,false,true,false},
        {"cmpxchgb",0xb0,0,false,true,false},{"xaddb",0xc0,0,false,true,false},
        {"btw",0xa3,0,false,true,false},{"btsw",0xab,0,false,true,false},
        {"btrw",0xb3,0,false,true,false},{"btcw",0xbb,0,false,true,false},
        {"popcntq",0,0x1b8U,true,false,true},{"popcntl",0,0x1b8U,false,false,true},
        {"lzcntq",0,0x1bdU,true,false,true},{"lzcntl",0,0x1bdU,false,false,true},
        {"tzcntq",0,0x1bcU,true,false,true},{"tzcntl",0,0x1bcU,false,false,true},
        {"imulq",0,0x1afU,true,false,true},{"imull",0,0x1afU,false,false,true},
        /* Existing typed memory ModR/M encoder owns REX/SIB/PC32 for
         * 0F B6/B7 zero-extension and 63 sign-extension. */
        {"movzbl",0,0x1b6U,false,false,true},{"movzbq",0,0x1b6U,true,false,true},
        {"movzwl",0,0x1b7U,false,false,true},{"movzwq",0,0x1b7U,true,false,true},
        /* MOVSX is identical ModR/M ownership; only 0F BE/BF differs. */
        {"movsbl",0,0x1beU,false,false,true},{"movsbq",0,0x1beU,true,false,true},
        {"movsbw",0,0x1beU,false,false,true},{"movzbw",0,0x1b6U,false,false,true},
        {"movswl",0,0x1bfU,false,false,true},{"movswq",0,0x1bfU,true,false,true},
        {"movslq",0,0x63U,true,false,true},
        /* Explicit scalar widths, all reuse typed ModR/M, SIB and Fixup. */
        {"xchgb",0x86U,0x86U,false,true,true},
        {"xchgw",0x87U,0x87U,false,true,true},
        {"imulw",0,0x1afU,false,false,true},
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
    if(s->site.mode_bits==X64_CODE32 &&
       !x64_legacy_opcode_allowed(s->op,s->site.mode_bits)) {
        minias_set_error(as,"unsupported-x86-code32-operand:%s:line=%zu",
                         s->op,s->line);
        minias_mcinst_destroy(m);return false;
    }
    if(s->site.mode_bits==X64_CODE16 ||
       s->site.mode_bits==X64_CODE16_GCC) {
        /* Register and ordinary 32-bit-address MOV/stack encodings reuse
         * the typed x86 MC form, with operand/address mode prefixes owned
         * by this Target. Keep unproved far/near control and mixed-width
         * instructions fail-closed until an independent GNU oracle exists. */
        if(!x64_legacy_opcode_allowed(s->op,s->site.mode_bits)) {
            minias_set_error(as,"unsupported-x86-code16-operand:%s:line=%zu",
                             s->op,s->line);
            minias_mcinst_destroy(m);return false;
        }
    }
    if(!strcmp(s->op,"lock") && s->args[0]!='\0') {
        const char *operand=s->args;
        MiniAsStmt alias;
        /* LOCK must prefix one retained read-modify-write MCInst; it
         * cannot be emitted as an unrelated standalone instruction. */
        if(strncmp(operand,"btsl",4U) ||
           !isspace((unsigned char)operand[4]))goto invalid;
        operand+=4U;
        while(isspace((unsigned char)*operand))++operand;
        alias=*s;alias.op="btsl";alias.args=(char *)operand;alias.mcinst=NULL;
        minias_mcinst_destroy(m);
        if(!x64_parse(as,&alias))return false;
        m=alias.mcinst;
        if(m==NULL || m->opcode!=X64_IMM_MEMORY) {
            minias_mcinst_destroy(m);
            minias_set_error(as,"invalid-x86-lock-rmw:line=%zu",s->line);
            return false;
        }
        m->encoding|=X64_ENCODING_LOCK;
        original->mcinst=m;
        return true;
    }
    /* GNU GCC emits CET `notrack jmp *table(,%rax,8)` for indirect
     * switch dispatch. NOTRACK is the DS (3e) prefix of the SAME group-5
     * CALL/JMP MCInst, not a separate instruction, and must be included in
     * the fragment size and every relocation offset. */
    if(!strcmp(s->op,"notrack")) {
        const char *p=s->args,*operand;
        const char *mnemonic=NULL;
        MiniAsStmt alias;
        while(isspace((unsigned char)*p))++p;
        if(!strncmp(p,"jmpq",4U) && isspace((unsigned char)p[4]))
            {mnemonic="jmpq";operand=p+4U;}
        else if(!strncmp(p,"jmp",3U) && isspace((unsigned char)p[3]))
            {mnemonic="jmp";operand=p+3U;}
        else if(!strncmp(p,"callq",5U) && isspace((unsigned char)p[5]))
            {mnemonic="callq";operand=p+5U;}
        else if(!strncmp(p,"call",4U) && isspace((unsigned char)p[4]))
            {mnemonic="call";operand=p+4U;}
        if(mnemonic==NULL)goto invalid;
        while(isspace((unsigned char)*operand))++operand;
        if(*operand!='*')goto invalid;
        alias=*s;alias.op=(char *)mnemonic;
        alias.args=(char *)operand;alias.mcinst=NULL;
        minias_mcinst_destroy(m);
        if(!x64_parse(as,&alias))return false;
        m=alias.mcinst;
        if(m==NULL || (m->opcode!=X64_INDIRECT_REG &&
           !(m->opcode==X64_REG_MEMORY &&
             (m->encoding&UINT32_C(0xff))==UINT32_C(0xff)))) {
            minias_mcinst_destroy(m);
            minias_set_error(as,"invalid-x86-notrack-branch:line=%zu",s->line);
            return false;
        }
        m->encoding|=X64_ENCODING_NOTRACK;
        original->mcinst=m;
        return true;
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
        if(x64_infers_width(s->op)) {
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
           !x64_parse_base_memory(as,dst,&mem))goto invalid;
        if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-movnti")) {
            minias_mcinst_destroy(m);return false;
        }
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=UINT32_C(0x100)|UINT32_C(0xc3);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=reg;
        m->operands[0].register_64=wide;
        goto done;
    }
    /* Mixed-width SSE conversion and transfer instructions share the same
     * register/memory ModR/M encoding as packed SSE. Their AT&T suffixes
     * select the GPR data width, not a second instruction implementation. */
    {
        static const struct {const char *name;uint32_t encoding;
                             bool source_xmm;bool gpr_wide;} conversions[]={
            {"movq",UINT32_C(0x0056e),false,true},
            {"movq",UINT32_C(0x0057e),true,true},
            {"movd",UINT32_C(0x0056e),false,false},
            {"movd",UINT32_C(0x0057e),true,false},
            {"cvtsi2sdl",X64_ENCODING_F2|UINT32_C(0x0012a),false,false},
            {"cvtsi2sdq",X64_ENCODING_F2|UINT32_C(0x0012a),false,true}
        };
        size_t k;
        for(k=0U;k<sizeof(conversions)/sizeof(conversions[0]);++k) {
            const char *regtext,*rmtext;
            unsigned reg=0U,rm=0U;
            bool wide=false,rm_is_register=false;
            X64Memory mem;
            if(strcmp(s->op,conversions[k].name) ||
               !x64_args2(s->args,src,dst))continue;
            regtext=conversions[k].source_xmm?src:dst;
            rmtext=conversions[k].source_xmm?dst:src;
            if(!x64_xmm_register(regtext,&reg))continue;
            if(conversions[k].source_xmm) {
                if(x64_register(rmtext,&rm,&wide) &&
                   wide==conversions[k].gpr_wide)rm_is_register=true;
            }else if(x64_register(rmtext,&rm,&wide) &&
                     wide==conversions[k].gpr_wide)rm_is_register=true;
            if(rm_is_register) {
                m->opcode=X64_REG_BINARY;m->count=2U;
                m->encoding=conversions[k].encoding|UINT32_C(0x800);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rm;
                m->operands[1].reg=reg;
                /* ModR/M.reg is XMM for MOVD/Q, CVTSI2SD and their
                 * register-reversed transfer forms. */
                m->operands[0].register_64=conversions[k].gpr_wide;
                goto done;
            }
            if(!x64_is_mem_candidate(rmtext) ||
               !x64_parse_base_memory(as,rmtext,&mem))continue;
            if(!x64_attach_memory_operand(as,m,&mem,
                    "out-of-memory:x86-conversion-memory")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=conversions[k].encoding;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=reg;
            m->operands[0].register_64=conversions[k].gpr_wide;
            goto done;
        }
    }
    /* CVTTSD2SI reverses the register classes: destination GPR occupies
     * ModR/M.reg, source XMM or memory occupies ModR/M.rm. */
    if(!strcmp(s->op,"cvttsd2sil") || !strcmp(s->op,"cvttsd2siq")) {
        const bool want_wide=!strcmp(s->op,"cvttsd2siq");
        unsigned integer_reg,xmm_reg;
        bool wide;
        X64Memory mem;
        if(!x64_args2(s->args,src,dst) ||
           !x64_register(dst,&integer_reg,&wide) || wide!=want_wide)
            goto invalid;
        if(x64_xmm_register(src,&xmm_reg)) {
            m->opcode=X64_REG_BINARY;m->count=2U;
            m->encoding=X64_ENCODING_F2|UINT32_C(0x0012c)|UINT32_C(0x800);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=xmm_reg;
            m->operands[0].register_64=want_wide;
            m->operands[1].kind=MINIAS_MC_REGISTER;
            m->operands[1].reg=integer_reg;
            goto done;
        }
        if(!x64_is_mem_candidate(src) ||
           !x64_parse_base_memory(as,src,&mem))goto invalid;
        if(!x64_attach_memory_operand(as,m,&mem,
                "out-of-memory:x86-cvttsd2si-memory")) {
            minias_mcinst_destroy(m);return false;
        }
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=X64_ENCODING_F2|UINT32_C(0x0012c);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=integer_reg;
        m->operands[0].register_64=want_wide;
        goto done;
    }
    /* Packed SSE shift-immediate opcodes are 66 0F 73 /subop imm8;
     * model the literal and XMM register as retained MC operands. */
    if(!strcmp(s->op,"psrldq") || !strcmp(s->op,"pslldq")) {
        uint64_t imm;
        if(!x64_args2(s->args,src,dst) || *src!='$' ||
           !minias_eval_u64_expression(src+1U,&imm) || imm>255U ||
           !x64_xmm_register(dst,&rd))goto invalid;
        m->opcode=X64_XMM_IMM_REG;m->count=2U;
        m->encoding=!strcmp(s->op,"psrldq")?3U:7U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        m->operands[1].kind=MINIAS_MC_IMMEDIATE;
        m->operands[1].immediate=imm;
        goto done;
    }
    /* SSE2 comparison predicates have one opcode (F2 0F C2 /r imm8),
     * not independent machine encodings for CMPNLTSD, CMPNLESD, etc.
     * The GCC mnemonic fixes the predicate byte while the usual XMM
     * register/memory operands determine the ModR/M layout. */
    {
        static const struct {const char *name;unsigned predicate;} compares[]={
            {"cmpeqsd",0U},{"cmpltsd",1U},{"cmplesd",2U},
            {"cmpunordsd",3U},{"cmpneqsd",4U},{"cmpnltsd",5U},
            {"cmpnlesd",6U},{"cmpordsd",7U}
        };
        size_t k;
        for(k=0U;k<sizeof(compares)/sizeof(compares[0]);++k) {
            unsigned source;
            X64Memory mem;
            if(strcmp(s->op,compares[k].name))continue;
            if(!x64_args2(s->args,src,dst) ||
               !x64_xmm_register(dst,&rd))goto invalid;
            if(x64_xmm_register(src,&source)) {
                m->opcode=X64_XMM_IMM_REG;m->count=3U;
                m->encoding=X64_ENCODING_F2|UINT32_C(0xc2);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=source;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=rd;
                m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                m->operands[2].immediate=compares[k].predicate;
                goto done;
            }
            if(!x64_is_mem_candidate(src) ||
               !x64_parse_base_memory(as,src,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,
                   "out-of-memory:x86-sse-compare-memory")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=3U;
            m->encoding=X64_ENCODING_F2|X64_ENCODING_IMM8|
                UINT32_C(0x001c2);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            m->operands[2].kind=MINIAS_MC_IMMEDIATE;
            m->operands[2].immediate=compares[k].predicate;
            goto done;
        }
    }
    /* GNU GCC emits both XMM-register and XMM-memory operands from the
     * same packed-SSE ISA families. Keep mandatory-prefix/opcode selection
     * in one target descriptor, and reuse the existing ModR/M plans for
     * register operands and memory (including symbolic RIP-relative loads).
     * 0F38 map remains the sole extra opcode escape flag. */
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
            {"movups",UINT32_C(0x00111),true},
            {"movapd",UINT32_C(0x00528),false},
            {"movapd",UINT32_C(0x00529),true},
            {"movsd",X64_ENCODING_F2|UINT32_C(0x00110),false},
            {"movsd",X64_ENCODING_F2|UINT32_C(0x00111),true},
            {"movss",UINT32_C(0x02110),false},
            {"movss",UINT32_C(0x02111),true},
            {"pxor",UINT32_C(0x005ef),false},
            {"pand",UINT32_C(0x005db),false},
            {"por",UINT32_C(0x005eb),false},
            {"paddq",UINT32_C(0x005d4),false},
            {"paddd",UINT32_C(0x005fe),false},
            {"andpd",UINT32_C(0x00554),false},
            {"andnpd",UINT32_C(0x00555),false},
            {"orpd",UINT32_C(0x00556),false},
            {"xorpd",UINT32_C(0x00557),false},
            {"mulsd",X64_ENCODING_F2|UINT32_C(0x00159),false},
            {"addsd",X64_ENCODING_F2|UINT32_C(0x00158),false},
            {"divsd",X64_ENCODING_F2|UINT32_C(0x0015e),false},
            {"subsd",X64_ENCODING_F2|UINT32_C(0x0015c),false},
            {"sqrtsd",X64_ENCODING_F2|UINT32_C(0x00151),false},
            {"maxsd",X64_ENCODING_F2|UINT32_C(0x0015f),false},
            {"minsd",X64_ENCODING_F2|UINT32_C(0x0015d),false},
            {"ucomisd",UINT32_C(0x0052e),false},
            {"comisd",UINT32_C(0x0052f),false},
            {"cvtss2sd",UINT32_C(0x0215a),false},
            {"cvtsd2ss",X64_ENCODING_F2|UINT32_C(0x0015a),false}
        };
        size_t k;
        for(k=0U;k<sizeof(vec)/sizeof(vec[0]);++k) {
            X64Memory mem;unsigned xmm=0U,other=0U;
            const char *memptr,*xmmtext;
            if(strcmp(s->op,vec[k].name))continue;
            if(!x64_args2(s->args,src,dst))continue;
            memptr=vec[k].store?dst:src;
            xmmtext=vec[k].store?src:dst;
            if(!x64_xmm_register(xmmtext,&xmm))continue;
            if(x64_xmm_register(memptr,&other)) {
                if(!strcmp(vec[k].name,"movntdqa") ||
                   !strcmp(vec[k].name,"movntdq"))continue;
                m->opcode=X64_REG_BINARY;m->count=2U;
                m->encoding=vec[k].op|UINT32_C(0x800);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=other;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=xmm;
                goto done;
            }
            if(!x64_is_mem_candidate(memptr) ||
               !x64_parse_base_memory(as,memptr,&mem))continue;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-xmm-mem")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=vec[k].op;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=xmm;
            m->operands[0].register_64=false;
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
                    x64_parse_base_memory(as,source,&mem)) {
                memory=true;ok=true;
            }
        }
        if(!ok)goto invalid;
        if(memory) {
            /* Keep VEX map/pp in target encoding; reuse the existing
             * typed memory emitter, ModR/M, SIB and ELF PC32/ABS32S.
             * operands[0] = destination, [1] = memory, [2] = count. */
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-bmi2-memory")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=3U;
            m->encoding=UINT32_C(0x40000)|pp;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=dest_reg;
            m->operands[0].register_64=dest_wide;
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
               !x64_parse_base_memory(as,s->args,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-prefetch")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x100)|hints[i].opcode|
                        (hints[i].prefix66?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=hints[i].extension;
            m->operands[0].register_64=false;
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
           !x64_parse_base_memory(as,src,&mem))goto invalid;
        if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-invpcid")) {
            minias_mcinst_destroy(m);return false;
        }
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=UINT32_C(0x10000)|UINT32_C(0x100)|
                    UINT32_C(0x400)|UINT32_C(0x82);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=destreg;
        m->operands[0].register_64=false;
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
               !x64_parse_base_memory(as,s->args,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-x87-memory")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=fpmem[k].opcode;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=fpmem[k].group;
            m->operands[0].register_64=false;
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
            if(!x64_parse_base_memory(as,src,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-lar-lsl")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x100)|
                        (dstword?UINT32_C(0x400):0U)|
                        (!strcmp(s->op,"lar")?2U:3U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=sys_destination;
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
            {"sgdt",0x01U,0U},{"sgdtl",0x01U,0U},
            {"sidt",0x01U,1U},{"sidtl",0x01U,1U},
            {"lgdt",0x01U,2U},{"lgdtl",0x01U,2U},
            {"lidt",0x01U,3U},{"lidtl",0x01U,3U},
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
            /* FNSTSW %ax is DF E0 (implicit AX), unlike the independent
             * FNSTSW m2byte form DD /7 in this descriptor. */
            if(!strcmp(s->op,"fnstsw") && !strcmp(s->args,"%ax")) {
                m->opcode=X64_ZERO;m->count=0U;
                m->encoding=UINT32_C(0xe0df);m->target_form=2U;
                goto done;
            }
            /* 0F 00 LTR/LLDT/STR/VERR/... also encode a register as
             * ModR/M mod=11; use the same typed unary MCInst as RDRAND.
             * Long-mode GNU uses 66 for extended 16-bit GPR forms. */
            if(sysmem[i].opcode==0U ||
               (sysmem[i].opcode==1U && sysmem[i].group==6U)) {
                unsigned reg=0U;
                bool word=x64_register16(s->args,&reg);
                bool wide=false;
                /* GNU accepts AX/EAX/RAX aliases for descriptor r/m16.
                 * Only SLDT and STR preserve a 66 prefix for explicit
                 * 16-bit destinations; LLDT/LTR/VERR/VERW never do.
                 * REX.W is ignored for these opcodes even with RAX. */
                if(word || x64_register(s->args,&reg,&wide)) {
                    bool emit66=word && sysmem[i].opcode==0U &&
                                sysmem[i].group<=1U;
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
               !x64_parse_base_memory(as,s->args,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-system-memory")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=(sysmem[i].opcode==0xd9U ||
                         sysmem[i].opcode==0xddU?0U:UINT32_C(0x100))|
                        sysmem[i].opcode;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=sysmem[i].group;
            m->operands[0].register_64=
                !strcmp(s->op,"fxsaveq") || !strcmp(s->op,"fxrstorq") ||
                !strcmp(s->op,"cmpxchg16b");
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
                   !x64_parse_base_memory(as,gp,&mem))goto invalid;
                if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-seg-memory")) {
                    minias_mcinst_destroy(m);return false;
                }
                m->opcode=X64_REG_MEMORY;m->count=2U;
                m->encoding=sys_source?UINT32_C(0x8c):UINT32_C(0x8e);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=sys;
                m->operands[0].register_64=false;
                goto done;
            }
            if(!gpr_ok || (kind!=0U && (gpr16 ||
               (s->site.mode_bits==0U && !gpr64))) ||
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
        m->opcode=X64_ZERO;
        m->target_form=4U;
        m->encoding=UINT32_C(0xfa1e0ff3);
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
            m->target_form=n;
            goto done;
        }
    }
    for(i=0U;i<sizeof(zeros)/sizeof(zeros[0]);++i) {
        if (!strcmp(s->op,zeros[i].op) && !*s->args) {
            if(s->site.mode_bits==0U &&
               (!strcmp(s->op,"pushfl") || !strcmp(s->op,"popfl") ||
                !strcmp(s->op,"pushal") || !strcmp(s->op,"popal")))
                goto invalid;
            m->opcode=X64_ZERO; m->count=0U;
            m->encoding=(unsigned)zeros[i].bytes[0] |
                (unsigned)zeros[i].bytes[1]<<8U |
                (unsigned)zeros[i].bytes[2]<<16U;
            m->target_form=zeros[i].size;goto done;
        }
    }
    if(!strcmp(s->op,"int") && s->args[0]=='$') {
        uint64_t vector;
        if(!minias_eval_u64_expression(s->args+1U,&vector) ||
           vector>255U)goto invalid;
        m->opcode=X64_ZERO;m->count=0U;m->target_form=2U;
        m->encoding=UINT32_C(0xcd)|((uint32_t)vector<<8U);
        goto done;
    }
    /* x87 register-stack pop: DD D8+i, where the operand is a literal
     * ST(0)..ST(7) selector, not a GPR or memory displacement. */
    if(!strcmp(s->op,"fstp") && !strncmp(s->args,"%st(",4U) &&
       s->args[4]>='0' && s->args[4]<='7' &&
       s->args[5]==')' && s->args[6]=='\0') {
        m->opcode=X64_ZERO;m->count=0U;
        m->encoding=UINT32_C(0xdd)|
            ((UINT32_C(0xd8)+(uint32_t)(s->args[4]-'0'))<<8U);
        m->target_form=2U;goto done;
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
               !x64_parse_base_memory(as,from,&mem))goto invalid;
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-imul-mem")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_IMM_MEMORY;m->count=3U;
            m->encoding=((int64_t)bits>=-128 && (int64_t)bits<=127?
                          UINT32_C(0x6b):UINT32_C(0x69))|
                         (word?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=imul_dst;
            m->operands[0].register_64=!word && destwide;
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
           x64_parse_base_memory(as,s->args,&mem)) {
            if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-setcc-mem")) {
                minias_mcinst_destroy(m);return false;
            }
            m->opcode=X64_REG_MEMORY;m->count=2U;
            m->encoding=UINT32_C(0x300)|UINT32_C(0x90)|cc;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=0U;
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
            m->target_form=width=='w'?3U:2U;
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
        m->target_form=width=='w'?2U:1U;
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
            m->target_form=2U;
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
                if(!x64_parse_base_memory(as,s->args,&parsed))goto invalid;
                if(!x64_attach_memory_operand(as,m,&parsed,"out-of-memory:x86-unary-memory")) {
                    minias_mcinst_destroy(m);return false;
                }
                m->opcode=X64_REG_MEMORY;m->count=2U;
                m->encoding=unary[i].opcode|
                    (s->op[strlen(s->op)-1U]=='w'?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=unary[i].group;
                m->operands[0].register_64=unary[i].wide;
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
    /* All x86 stack-memory forms share Group-5 FF /6 and 8F /0;
     * operand-size prefixes are chosen from the lexical mode, never
     * inferred from the address register's independent width. */
    if((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq") ||
        (s->site.mode_bits!=0U &&
         (!strcmp(s->op,"pushl") || !strcmp(s->op,"popl") ||
          !strcmp(s->op,"pushw") || !strcmp(s->op,"popw")))) &&
       s->args[0]!='$' && x64_is_mem_candidate(s->args)) {
        X64Memory parsed;
        if(!x64_parse_base_memory(as,s->args,&parsed))goto invalid;
        if(!x64_attach_memory_operand(as,m,&parsed,"out-of-memory:x86-stack-memory")) {
            minias_mcinst_destroy(m);return false;
        }
        m->opcode=X64_REG_MEMORY;m->count=2U;
        const bool pushing=!strncmp(s->op,"push",4U);
        const bool word=s->op[4]=='w';
        m->encoding=(pushing?0xffU:0x8fU)|
                    (word?UINT32_C(0x400):0U);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=pushing?6U:0U;
        m->operands[0].register_64=false;
        goto done;
    }
    /* One Group-2 lowering path for implicit 1, CL and immediate counts,
     * with either register or memory destination. The subsequent register
     * plan and existing memory layout own all byte-level decisions; no
     * second register-shift decoder later in the parse function. */
    {
        unsigned group,bits,reg=0U;
        if(x64_shift_descriptor(s->op,&group,&bits)) {
            const char *destination=s->args;
            X64ShiftCountForm count_form=X64_SHIFT_COUNT_IMMEDIATE;
            uint64_t count=1U;
            bool legacy_high=false;
            X64Memory mem;
            if(x64_args2(s->args,src,dst)) {
                destination=dst;
                if(!strcmp(src,"%cl"))count_form=X64_SHIFT_COUNT_CL;
                else if(*src=='$') {
                    if(!minias_eval_u64_expression(src+1U,&count) ||
                       (count>255U && count<UINT64_MAX-127U))goto invalid;
                }else goto invalid;
            }
            if(x64_shift_register(destination,bits,&reg,&legacy_high)) {
                m->opcode=X64_SHIFT_REGISTER;
                m->count=2U;
                m->target_form=(uint32_t)count_form;
                m->encoding=group|(bits==8U?0x200U:
                                   bits==16U?0x400U:0U)|
                            (legacy_high?0x800U:0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=reg;
                m->operands[0].register_64=bits==64U;
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=count;
                goto done;
            }
            if(!x64_is_mem_candidate(destination) ||
               !x64_parse_base_memory(as,destination,&mem))goto invalid;
            {
                const bool byte=bits==8U;
                const bool has_imm=count_form!=X64_SHIFT_COUNT_CL && count!=1U;
                const unsigned opcode=count_form==X64_SHIFT_COUNT_CL?
                    (byte?0xd2U:0xd3U):has_imm?
                    (byte?0xc0U:0xc1U):(byte?0xd0U:0xd1U);
                m->opcode=has_imm?X64_IMM_MEMORY:X64_REG_MEMORY;
                m->count=has_imm?3U:2U;
                /* Group-2 /n is an opcode extension, not a GPR.  Setting
                 * byte-register REX bits here would corrupt selector /4. */
                m->encoding=opcode|(bits==16U?0x400U:0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=group;
                m->operands[0].register_64=bits==64U;
                if(!x64_attach_memory_operand(as,m,&mem,
                        "out-of-memory:x86-shift-mem")) {
                    minias_mcinst_destroy(m);return false;
                }
                if(has_imm) {
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=count;
                }
                goto done;
            }
        }
    }
    /* Byte ALU variants and sign-extending MOV all remain target local. */
    if(x64_args2(s->args,src,dst)) {
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
                unsigned width=0U;
                const X64BinaryFormDescriptor *form=
                    x64_binary_form(s->op,&width);
                bool rexneeded=first>=8U || second>=8U ||
                               (first>=4U && !highsrc) ||
                               (second>=4U && !highdst);
                if(form && width==8U &&
                   form->has_rm_destination_opcode) {
                    if((highsrc || highdst) && rexneeded)goto invalid;
                    m->opcode=X64_BINARY8;m->count=2U;
                    m->encoding=x64_binary_opcode(form,false,width)|
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
    /* Plain scalar ALU/MOV/TEST register encodings are governed by the
     * same descriptor as their memory and immediate counterparts. */
    {
        unsigned width=0U;
        const X64BinaryFormDescriptor *form=x64_binary_form(s->op,&width);
        if(form && width>=16U && form->has_rm_destination_opcode &&
           x64_args2(s->args,src,dst)) {
            bool valid=false;
            if(width==16U) {
                valid=x64_register16(src,&rs) && x64_register16(dst,&rd);
                ws=false;wd=false;
            }else {
                valid=x64_register(src,&rs,&ws) &&
                      x64_register(dst,&rd,&wd) &&
                      ws==(width==64U) && wd==ws;
            }
            if(valid) {
                m->opcode=X64_REG_BINARY;m->count=2U;
                m->encoding=x64_binary_opcode(form,false,width)|
                    (width==16U?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rs;
                m->operands[0].register_64=width==64U;
                m->operands[1].kind=MINIAS_MC_REGISTER;
                m->operands[1].reg=rd;
                goto done;
            }
        }
    }
    /* Register forms share the exact descriptor table used by memory
     * ModR/M forms. A second xadd/cmpxchg/bt opcode table previously omitted
     * byte XADD/CMPXCHG entirely. Keep the ISA-special compact XCHG encoding
     * in this one typed lowering path, not in the source scanner. */
    for(i=0U;i<sizeof(regmem)/sizeof(regmem[0]);++i) {
        const char *form=regmem[i].op;
        size_t name_len=strlen(s->op),form_len=strlen(form);
        const bool family=!strncmp(form,"bt",2U) ||
            !strncmp(form,"xadd",4U) ||
            !strncmp(form,"cmpxchg",7U) ||
            !strncmp(form,"xchg",4U);
        const bool is_xchg=!strncmp(form,"xchg",4U);
        const bool exact=!strcmp(s->op,form);
        const bool inferred=form_len==name_len+1U &&
            !strncmp(s->op,form,name_len);
        const char suffix=form[form_len-1U];
        unsigned width=suffix=='b'?8U:suffix=='w'?16U:
                       suffix=='l'?32U:suffix=='q'?64U:0U;
        bool highsrc=false,highdst=false,valid=false;
        if(!family || !regmem[i].has_rm_dest || width==0U ||
           (!exact && !inferred) ||
           !x64_args2(s->args,src,dst))continue;
        if(width==8U) {
            highsrc=x64_high_register8(src,&rs);
            highdst=x64_high_register8(dst,&rd);
            valid=(highsrc || x64_register8(src,&rs)) &&
                  (highdst || x64_register8(dst,&rd));
            if(valid &&
               (rs>=8U || rd>=8U ||
                (rs>=4U && !highsrc) || (rd>=4U && !highdst)) &&
               (highsrc || highdst))goto invalid;
        }else if(width==16U) {
            valid=x64_register16(src,&rs) && x64_register16(dst,&rd);
        }else {
            valid=x64_register(src,&rs,&ws) &&
                  x64_register(dst,&rd,&wd) &&
                  ws==(width==64U) && wd==ws;
        }
        if(!valid)continue;
        m->opcode=width==8U?X64_BINARY8:X64_REG_BINARY;
        m->count=2U;
        m->encoding=regmem[i].rm_dest |
            (!is_xchg?UINT32_C(0x100):0U) |
            (width==16U?UINT32_C(0x400):0U) |
            (highsrc?UINT32_C(0x1000):0U) |
            (highdst?UINT32_C(0x2000):0U);
        /* GNU selects 90+reg for XCHG with the accumulator; XCHG
         * eax,eax is 87 c0, but rax,rax is the canonical single NOP. */
        if(is_xchg && width!=8U && (rs==0U || rd==0U) &&
           !(width==32U && rs==0U && rd==0U))
            m->encoding|=UINT32_C(0x4000);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rs;
        m->operands[0].register_64=width==64U;
        m->operands[1].kind=MINIAS_MC_REGISTER;
        m->operands[1].reg=rd;
        goto done;
    }
    /* Far immediate transfer is distinct from near PC-relative branches:
     * EA moffs16/32,selector16. The Target owns both instruction size
     * and an ABS16/ABS32 fixup against the offset when symbolic. */
    if(s->site.mode_bits!=0U &&
       (!strcmp(s->op,"ljmpw") || !strcmp(s->op,"ljmpl") ||
        !strcmp(s->op,"lcallw") || !strcmp(s->op,"lcalll")) &&
       x64_args2(s->args,src,dst) && src[0]=='$' && dst[0]=='$') {
        uint64_t segment,offset;
        MiniAsSymbolExpr symbol;
        MiniAsSymbolDifference difference;
        if(!minias_eval_u64_expression(src+1U,&segment) ||
           segment>UINT16_MAX)goto invalid;
        m->opcode=X64_FAR_BRANCH;m->count=2U;
        m->encoding=(!strcmp(s->op,"ljmpl") ||
                     !strcmp(s->op,"lcalll"))?4U:2U;
        m->target_form=(!strcmp(s->op,"lcallw") ||
                        !strcmp(s->op,"lcalll"))?1U:0U;
        m->operands[0].kind=MINIAS_MC_IMMEDIATE;
        m->operands[0].immediate=segment;
        if(minias_eval_u64_expression(dst+1U,&offset)) {
            if(offset>(m->encoding==2U?UINT16_MAX:UINT32_MAX))
                goto invalid;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=offset;
        }else if(minias_parse_symbol_addend(dst+1U,&symbol)) {
            if(!x64_attach_symbol_operand(as,m,1U,&symbol,
                    "out-of-memory:x86-far-symbol")) {
                minias_mcinst_destroy(m);return false;
            }
        }else if(minias_parse_symbol_difference_absolute_tail(
                     as,dst+1U,&difference)) {
            if(!x64_attach_symbol_difference_operand(as,m,1U,&difference,
                    "out-of-memory:x86-far-difference")) {
                minias_mcinst_destroy(m);return false;
            }
        }else goto invalid;
        goto done;
    }
    /* Reuse the same condition-code policy as CMOV: JMP/CALL have fixed
     * opcode identities, while every Jcc synonym maps to 0x70+cc (short)
     * or 0x0f 0x80+cc (near). The MC relaxation plan chooses the width;
     * external targets retain PC32/PLT32 fixups with the x86 P+4 bias. */
    {
        unsigned condition=0U,encoding=0U;
        bool call=!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
                  !strcmp(s->op,"calll");
        bool jump=!strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq");
        bool conditional=s->op[0]=='j' &&
                         x64_cond_code(s->op+1U,&condition);
        if(call || jump || conditional) {
            MiniAsSymbolExpr expr;
            char normal[X64_OPERAND_CAP];
            bool explicit_plt=false;
            const char *branch_operand=s->args;
            encoding=call?0xe8U:jump?0xe9U:0x180U|condition;
            /* Register-indirect CALL/JMP are FF /2 and /4, not PLT32
             * symbolic branches. Dispatch them to the MCInst group-5
             * register parser below rather than rejecting '*' here. */
            if(!(s->args[0]=='*' && (call || jump))) {
                if(x64_strip_elf_symbol_modifier(s->args,"@PLT",normal)) {
                    branch_operand=normal;
                    explicit_plt=true;
                }
                if(!minias_parse_symbol_addend(branch_operand,&expr) ||
                   expr.addend<INT64_MIN+4)goto invalid;
                if(!x64_attach_symbol_operand(as,m,0U,&expr,
                        "out-of-memory:x86-branch-symbol")) {
                    minias_mcinst_destroy(m);return false;
                }
                m->opcode=X64_BRANCH;m->count=1U;
                m->encoding=encoding;
                m->target_form=explicit_plt?1U:0U;
                goto done;
            }
        }
    }
    /* GNU AT&T defaults unsuffixed immediate PUSH to the active 64-bit
     * operand size. Linux head_64.S emits `push $0` in its return path;
     * reuse the single typed PUSH imm8/imm32 encoding and ABS32S fixup. */
    if((!strcmp(s->op,"pushq") || !strcmp(s->op,"push") ||
         (s->site.mode_bits!=0U && !strcmp(s->op,"pushw")) ||
         (s->site.mode_bits!=0U &&
        !strcmp(s->op,"pushl"))) && s->args[0]=='$') {
        uint64_t bits;
        MiniAsSymbolExpr expr;
        /* Already-resolved SHN_ABS .set/.equ operands are machine constants.
         * Record them as typed immediates now, so the shared PUSH encoding
         * plan selects GNU's imm8 alternative and emits no false relocation.
         * Strictly reject section-relative labels here: their temporary
         * value may be zero but GNU requires PUSH imm32 + ABS32S relocation.
         * The generic absolute evaluator also admits relocatable labels
         * for layout-sensitive directives; the DATA constant oracle rejects
         * them and keeps PUSH's immediate-vs-symbol ownership unambiguous. */
        if(minias_parse_absolute_data_bits(as,s->args+1,&bits) &&
           (bits<=(uint64_t)INT32_MAX ||
            bits>=UINT64_C(0xffffffff80000000))) {
            m->opcode=X64_PUSH_IMM;m->count=1U;
            m->encoding=!strcmp(s->op,"pushw")?UINT32_C(0x400):0U;
            m->operands[0].kind=MINIAS_MC_IMMEDIATE;
            m->operands[0].immediate=bits;
            goto done;
        }
        if(minias_parse_symbol_addend(s->args+1,&expr)) {
            m->opcode=X64_PUSH_IMM;m->count=1U;
            m->encoding=!strcmp(s->op,"pushw")?UINT32_C(0x400):0U;
            if(!x64_attach_symbol_operand(as,m,0U,&expr,
                    "out-of-memory:x86-push-symbol")) {
                minias_mcinst_destroy(m);
                return false;
            }
            goto done;
        }
    }
    if ((!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
         !strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq") ||
         !strcmp(s->op,"jmpl")) &&
        s->args[0]=='*' &&
        x64_register(s->args+1,&rs,&ws) &&
        (ws || (s->site.mode_bits!=0U && !ws && rs<8U))) {
        m->opcode=X64_INDIRECT_REG;m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rs;m->operands[0].register_64=ws;
        m->encoding=(!strcmp(s->op,"call")||!strcmp(s->op,"callq"))?
                    2U:4U;
        goto done;
    }
    /* Register-indirect and memory-indirect CALL/JMP share the Group-5
     * ISA semantics. The latter uses the existing typed memory, PC32
     * fixup, segment override and SIB encoder; no new Core path. */
    if ((!strcmp(s->op,"call") || !strcmp(s->op,"callq") ||
         !strcmp(s->op,"jmp") || !strcmp(s->op,"jmpq") ||
         !strcmp(s->op,"jmpl") ||
         !strcmp(s->op,"ljmp") || !strcmp(s->op,"ljmpl") ||
         !strcmp(s->op,"ljmpw") ||
         !strcmp(s->op,"lcall") || !strcmp(s->op,"lcalll") ||
         !strcmp(s->op,"lcallw")) &&
        s->args[0]=='*' && x64_is_mem_candidate(s->args+1)) {
        X64Memory parsed;
        if (!x64_parse_base_memory(as,s->args+1,&parsed))goto invalid;
        if(!x64_attach_memory_operand(as,m,&parsed,"out-of-memory:x86-indirect-memory")) {
            minias_mcinst_destroy(m);return false;
        }
        m->opcode=X64_REG_MEMORY;m->count=2U;
        m->encoding=0xffU;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=(!strcmp(s->op,"call") ||
                            !strcmp(s->op,"callq"))?2U:
                           (!strcmp(s->op,"lcall") ||
                            !strcmp(s->op,"lcalll") ||
                            !strcmp(s->op,"lcallw"))?3U:
                           (!strcmp(s->op,"ljmp") ||
                            !strcmp(s->op,"ljmpl") ||
                            !strcmp(s->op,"ljmpw"))?5U:4U;
        goto done;
    }
    if ((!strcmp(s->op,"pushq") || !strcmp(s->op,"popq") ||
         !strcmp(s->op,"push") || !strcmp(s->op,"pop") ||
         (s->site.mode_bits!=0U &&
          (!strcmp(s->op,"pushl") || !strcmp(s->op,"popl")))) &&
        x64_register(s->args,&rs,&ws) &&
        (ws || (s->site.mode_bits!=0U &&
                (!strcmp(s->op,"pushl") || !strcmp(s->op,"popl") ||
                 (s->site.mode_bits==X64_CODE16_GCC &&
                  (!strcmp(s->op,"push") || !strcmp(s->op,"pop")))) &&
                !ws && rs<8U))) {
        m->opcode=X64_STACK;m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rs;
        m->encoding=(!strcmp(s->op,"pushq") ||
                     !strcmp(s->op,"push") ||
                     !strcmp(s->op,"pushl"))?0x50U:0x58U;goto done;
    }
    if(s->site.mode_bits!=0U &&
       (!strcmp(s->op,"pushw") || !strcmp(s->op,"popw")) &&
       x64_register16(s->args,&rs) && rs<8U) {
        m->opcode=X64_STACK;m->count=1U;
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rs;
        m->encoding=!strcmp(s->op,"pushw")?0x50U:0x58U;
        goto done;
    }
    if(s->site.mode_bits!=0U &&
       (!strcmp(s->op,"pushw") || !strcmp(s->op,"popw"))) {
        static const struct {
            const char *reg;
            uint8_t push_first,pop_first,push_second,pop_second;
        } segments[]={
            {"%es",0x06U,0x07U,0U,0U},
            {"%cs",0x0eU,0U,0U,0U},
            {"%ss",0x16U,0x17U,0U,0U},
            {"%ds",0x1eU,0x1fU,0U,0U},
            {"%fs",0x0fU,0x0fU,0xa0U,0xa1U},
            {"%gs",0x0fU,0x0fU,0xa8U,0xa9U}
        };
        const bool pushing=!strcmp(s->op,"pushw");
        for(i=0U;i<sizeof(segments)/sizeof(segments[0]);++i) {
            if(strcmp(s->args,segments[i].reg))continue;
            if(!pushing && segments[i].pop_first==0U)goto invalid;
            m->opcode=X64_ZERO;m->count=0U;
            m->encoding=(unsigned)(pushing?segments[i].push_first:
                                   segments[i].pop_first);
            m->target_form=1U;
            if(segments[i].push_second!=0U) {
                m->encoding|=(uint32_t)(pushing?segments[i].push_second:
                                      segments[i].pop_second)<<8U;
                m->target_form=2U;
            }
            goto done;
        }
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
            m->opcode=X64_MOV_IMM32;m->count=2U;
            m->encoding=0x200U|(high?0x800U:0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=reg;
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint8_t)bits;
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
        MiniAsSymbolExpr sym;
        bool numeric=minias_eval_u64_expression(src+1U,&imm16);
        bool symbolic=!numeric && as->output_elf32 &&
            minias_parse_symbol_addend(src+1U,&sym);
        if((!numeric && !symbolic) ||
           (numeric && ((int64_t)imm16<-32768 || (int64_t)imm16>65535)))
            goto invalid;
        m->opcode=X64_MOV_IMM32;m->count=2U;
        m->encoding=UINT32_C(0x400);
        m->operands[0].kind=MINIAS_MC_REGISTER;
        m->operands[0].reg=rd;
        if(symbolic) {
            if(!x64_attach_symbol_operand(as,m,1U,&sym,
                    "out-of-memory:x86-movw-symbol")) {
                minias_mcinst_destroy(m);return false;
            }
        }else {
            m->operands[1].kind=MINIAS_MC_IMMEDIATE;
            m->operands[1].immediate=(uint16_t)imm16;
        }
        goto done;
    }
    if (!strcmp(s->op,"movl") && x64_args2(s->args,src,dst) &&
        src[0]=='$' && x64_register(dst,&rd,&wd) && !wd) {
        uint64_t bits;
        MiniAsSymbolExpr symbol;
        MiniAsSymbolDifference difference_expr;
        bool numeric=minias_eval_u64_expression(src+1U,&bits);
        bool symbolic=!numeric && minias_parse_symbol_addend(src+1U,&symbol);
        bool difference=!numeric && !symbolic &&
            minias_parse_symbol_difference_absolute_tail(
                as,src+1U,&difference_expr);
        if(symbolic || difference || (numeric &&
           (bits<=UINT32_MAX ||
            ((int64_t)bits>=INT32_MIN && (int64_t)bits<0) ||
            /* GNU `$~0xfa1e0ff3` carries the mathematically negative
             * 64-bit complement, then encodes its low imm32. Permit only
             * explicitly bitwise complemented high-all-ones values, not
             * unrelated out-of-range literals such as $0x100000000. */
            (strchr(src+1U,'~')!=NULL &&
             (bits>>32U)==UINT32_MAX)))) {
            m->opcode=X64_MOV_IMM32;m->count=2U;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=rd;
            if(difference) {
                if(!x64_attach_symbol_difference_operand(as,m,1U,
                        &difference_expr,"out-of-memory:x86-movl-difference")) {
                    minias_mcinst_destroy(m);return false;
                }
            } else if(symbolic) {
                if(as->output_elf32 &&
                   !strcmp(symbol.name,"_GLOBAL_OFFSET_TABLE_"))
                    m->target_form=X64_FORM_I386_GOTPC;
                if(!x64_attach_symbol_operand(as,m,1U,&symbol,
                        "out-of-memory:x86-movl-imm32-symbol")) {
                    minias_mcinst_destroy(m);return false;
                }
            }else {
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=(uint32_t)bits;
            }
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
            if(symbolic) {
                if(!x64_attach_symbol_operand(as,m,1U,&expr,
                        "out-of-memory:x86-movabs-symbol")) {
                    minias_mcinst_destroy(m);return false;
                }
            }else {
                m->operands[1].kind=MINIAS_MC_IMMEDIATE;
                m->operands[1].immediate=bits;
            }
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
                if(!x64_parse_base_memory(as,dst,&mem))goto invalid;
                if(!x64_attach_memory_operand(as,m,&mem,"out-of-memory:x86-bit-imm")) {
                    minias_mcinst_destroy(m);return false;
                }
                m->opcode=X64_IMM_MEMORY;m->count=3U;
                m->encoding=UINT32_C(0x1ba)|
                            (word?UINT32_C(0x400):0U);
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=forms[i].group;
                m->operands[0].register_64=forms[i].wide;
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
        uint64_t bits=0U;
        int64_t val;
        unsigned group=0U;
        bool has_group=x64_alu_group(s->op,&group);
        bool wide=s->op[strlen(s->op)-1U]=='q';
        MiniAsSymbolExpr sym;
        MiniAsSymbolDifference diff;
        bool is_sym=false;
        bool is_difference=false;
        bool numeric;
        numeric=minias_eval_u64_expression(src+1,&bits);
        /* One typed ELF sign-ext imm32 operand for C7 MOVQ and all
         * Group-1 32/64-bit immediate-to-memory instructions. */
        if(!numeric &&
           (!strcmp(s->op,"movq") || !strcmp(s->op,"movl") ||
            (as->output_elf32 && !strcmp(s->op,"movw")) ||
            (has_group && (s->op[strlen(s->op)-1U]=='q' ||
                           s->op[strlen(s->op)-1U]=='l' ||
                           (as->output_elf32 &&
                            s->op[strlen(s->op)-1U]=='w')))) &&
           minias_parse_symbol_addend(src+1,&sym))is_sym=true;
        if(!numeric && !is_sym && x64_is_mem_candidate(dst) &&
           (!strcmp(s->op,"movl") || !strcmp(s->op,"movq") ||
            (has_group && (s->op[strlen(s->op)-1U]=='q' ||
                           s->op[strlen(s->op)-1U]=='l'))) &&
           minias_parse_symbol_difference_absolute_tail(as,src+1,&diff))
            is_difference=true;
        val=(int64_t)bits;
        {
            char suffix=s->op[strlen(s->op)-1U];
            bool in_range=numeric &&
                (suffix=='b'?(val>=-256 && val<=255):
                 suffix=='w'?(val>=-32768 && val<=65535):
                 val>=INT32_MIN && val<=INT32_MAX);
        if(in_range || is_sym || is_difference) {
            if(!strcmp(s->op,"movq") &&
               x64_register(dst,&rd,&wd) && wd) {
                m->opcode=X64_MOVQ_IMMREG;m->count=2U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=rd;m->operands[0].register_64=true;
                if(is_sym){
                    if(!x64_attach_symbol_operand(as,m,1U,&sym,
                            "out-of-memory:x86-imm-symbol")) {
                        minias_mcinst_destroy(m);return false;
                    }
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
                if(!x64_parse_base_memory(as,dst,&parsed))goto invalid;
                /* Keep both expressions typed, for independent GNU PC32
                 * displacement and 32S immediate ELF relocations. */
                if(!x64_attach_memory_operand(as,m,&parsed,"out-of-memory:x86-imm-memory")) {
                    minias_mcinst_destroy(m);return false;
                }
                m->opcode=X64_IMM_MEMORY;m->count=3U;
                m->operands[0].kind=MINIAS_MC_REGISTER;
                m->operands[0].reg=has_group?group:0U;
                m->operands[0].register_64=wide;
                if(is_difference) {
                    if(!x64_attach_symbol_difference_operand(as,m,2U,&diff,
                            "out-of-memory:x86-imm-memory-difference")) {
                        minias_mcinst_destroy(m);
                        return false;
                    }
                } else if(is_sym) {
                    if(!x64_attach_symbol_operand(as,m,2U,&sym,
                            "out-of-memory:x86-imm-memory-symbol")) {
                        /* MCInst owns the memory descriptor and its nested
                         * displacement symbol even when operand 2 fails. */
                        minias_mcinst_destroy(m);
                        return false;
                    }
                } else {
                    m->operands[2].kind=MINIAS_MC_IMMEDIATE;
                    m->operands[2].immediate=bits;
                }
                m->encoding=(has_group ?
                    (suffix=='b'?0x80U:
                     (!is_sym && !is_difference && val>=-128 && val<=127)?0x83U:0x81U):
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
        MiniAsSymbolExpr expr;
        bool wide=false;
        unsigned reg,group;
        size_t n=strlen(s->op);
        if((n==4U || n==3U) &&
           (s->op[n-1U]=='l' || s->op[n-1U]=='q' ||
            (as->output_elf32 && s->op[n-1U]=='w')) &&
           !minias_eval_u64_expression(src+1,&m->operands[1].immediate) &&
           minias_parse_symbol_addend(src+1,&expr) &&
           (s->op[n-1U]=='w'?x64_register16(dst,&reg):
            x64_register(dst,&reg,&wide)) &&
           wide==(s->op[n-1U]=='q') && x64_alu_group(s->op,&group)) {
            m->opcode=X64_ALU_IMM;m->count=2U;
            m->encoding=group|(s->op[n-1U]=='w'?UINT32_C(0x400):0U);
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=reg;
            m->operands[0].register_64=wide;
            if(as->output_elf32 && s->op[n-1U]=='l' &&
               !strcmp(expr.name,"_GLOBAL_OFFSET_TABLE_"))
                m->target_form=X64_FORM_I386_GOTPC;
            if(!x64_attach_symbol_operand(as,m,1U,&expr,
                    "out-of-memory:x86-alu-symbol")) {
                minias_mcinst_destroy(m);
                return false;
            }
            goto done;
        }
    }
    /* Genuine Linux x86 forward subtractive immediate:
     * addq $(identity_mapped - relocate_kernel), %r8.
     * GNU preserves imm32 for forward deltas even when final size fits imm8. */
    if(x64_args2(s->args,src,dst) && src[0]=='$') {
        size_t len=strlen(s->op);char suffix=len?s->op[len-1U]:'\0';
        unsigned reg,group;bool wide;
        MiniAsSymbolDifference diff;
        if((suffix=='q' || suffix=='l') &&
           minias_parse_symbol_difference(src+1U,&diff) &&
           x64_register(dst,&reg,&wide) && wide==(suffix=='q') &&
           x64_alu_group(s->op,&group)) {
            m->opcode=X64_ALU_IMM;m->count=2U;
            m->encoding=group;
            m->operands[0].kind=MINIAS_MC_REGISTER;
            m->operands[0].reg=reg;
            m->operands[0].register_64=wide;
            if(!x64_attach_symbol_difference_operand(as,m,1U,&diff,
                    "out-of-memory:x86-alu-difference")) {
                minias_mcinst_destroy(m);return false;
            }
            /* GNU chooses the short group-1 form for backward, already
             * defined section-local pairs. Range is checked only in MC
             * layout; the existing monotonic relaxer can promote to imm32
             * if earlier fragment growth pushes the delta out of imm8. A
             * forward/external pair starts and remains in the safe imm32. */
            const MiniAsSymbol *positive=minias_symbol_ref_lookup(
                as,m->operands[1].bound_difference->positive);
            const MiniAsSymbol *negative=minias_symbol_ref_lookup(
                as,m->operands[1].bound_difference->negative);
            if(positive && negative && positive->defined &&
               negative->defined && positive->section>=0 &&
               positive->section==negative->section)
                m->encoding|=X64_ENCODING_IMM8;
            goto done;
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
        size_t len=strlen(s->op);
        char width=len?s->op[len-1U]:'\0';
        unsigned bits=width=='b'?8U:width=='w'?16U:
                      width=='l'?32U:width=='q'?64U:0U;
        unsigned reg=0U,group=0U;
        bool wide=false,legacy_high8=false;
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
             wide==(bits==64U))) && x64_alu_group(s->op,&group)) {
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
    /* Retain memory addressing as one typed operand through layout,
     * encoding, and eventual ELF fixup. No source-string re-splitting. */
    if (x64_args2(s->args,src,dst) &&
        (x64_is_mem_candidate(src) || x64_is_mem_candidate(dst))) {
        X64Memory parsed;
        unsigned r, reg_bits=0U;
        bool wide=false, memory_source, legacy_high8=false;
        const unsigned *chosen = NULL;
        unsigned target_opcode=0U;
        if (x64_register(src,&r,&wide) &&
            x64_parse_base_memory(as,dst,&parsed)) {
            memory_source=false;reg_bits=wide?64U:32U;
        } else if (x64_high_register8(src,&r) &&
                   x64_parse_base_memory(as,dst,&parsed)) {
            memory_source=false;reg_bits=8U;legacy_high8=true;
        } else if (x64_register8(src,&r) &&
                   x64_parse_base_memory(as,dst,&parsed)) {
            memory_source=false;reg_bits=8U;
        } else if (x64_register16(src,&r) &&
                   x64_parse_base_memory(as,dst,&parsed)) {
            memory_source=false;reg_bits=16U;
        } else if (x64_register(dst,&r,&wide) &&
                   x64_parse_base_memory(as,src,&parsed)) {
            memory_source=true;reg_bits=wide?64U:32U;
        } else if (x64_high_register8(dst,&r) &&
                   x64_parse_base_memory(as,src,&parsed)) {
            memory_source=true;reg_bits=8U;legacy_high8=true;
        } else if (x64_register8(dst,&r) &&
                   x64_parse_base_memory(as,src,&parsed)) {
            memory_source=true;reg_bits=8U;
        } else if (x64_register16(dst,&r) &&
                   x64_parse_base_memory(as,src,&parsed)) {
            memory_source=true;reg_bits=16U;
        } else goto invalid;
        if(legacy_high8 && (parsed.base>=8U ||
           (parsed.has_index && parsed.index>=8U))) {
            goto invalid;
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
        if(!chosen) {
            unsigned width=0U;
            const X64BinaryFormDescriptor *form=x64_binary_form(s->op,&width);
            if(form && width==reg_bits &&
               (memory_source?form->has_rm_source_opcode:
                              form->has_rm_destination_opcode)) {
                target_opcode=x64_binary_opcode(form,memory_source,width);
                chosen=&target_opcode;
            }
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
                if(memory_source?!regmem[i].has_rm_src:
                                 !regmem[i].has_rm_dest)continue;
                chosen=memory_source?&regmem[i].rm_src:&regmem[i].rm_dest;
                break;
            }
        }
        if(!chosen) {
            goto invalid;
        }
        if(!x64_attach_memory_operand(as,m,&parsed,"out-of-memory:x86_64-mc-memory")) {
            minias_mcinst_destroy(m);return false;
        }
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
        if(s->site.mode_bits!=0U &&
           !parsed.has_base && !parsed.has_index &&
           r==0U &&
           (*chosen==0x8bU || *chosen==0x89U ||
            *chosen==0x8aU || *chosen==0x88U)) {
            /* A0/A1/A2/A3 encode accumulator+absolute moffs with no
             * ModR/M. GNU GAS chooses this both for 16-bit absolute MOV
             * (disp16) and 32-bit absolute MOV (disp32). Both retain the
             * existing typed ABS fixup and one address-width decision. */
            m->opcode=X64_MOFFS;
            m->encoding=*chosen==0x8bU?0xa1U:
                        *chosen==0x89U?0xa3U:
                        *chosen==0x8aU?0xa0U:0xa2U;
        }
        goto done;
    }
invalid:
    minias_set_error(as,"unsupported-x86_64-operand:%s:%s:line=%zu",
                     original->op,original->args,original->line);
    minias_mcinst_destroy(m);return false;
done:
    if(original->site.mode_bits!=0U) {
        const unsigned bits=x64_legacy_operand_bits(s,m);
        m->target_form=(m->target_form&X64_FORM_MASK)|
            (bits<<X64_FORM_WIDTH_SHIFT);
    }
    if(original->site.mode_bits!=0U) {
        size_t k;
        /* Legacy encodings cannot address R8-R15 or use 64-bit GPRs,
         * even though the long-mode parser accepts their spelling. */
        for(k=0U;k<m->count;++k) {
            if(m->operands[k].kind==MINIAS_MC_REGISTER &&
               (m->operands[k].reg>=8U ||
                m->operands[k].register_64))goto invalid;
            if(m->operands[k].kind==MINIAS_MC_MEMORY &&
               m->operands[k].memory) {
                const X64Memory *mem=(const X64Memory *)m->operands[k].memory;
                if((mem->has_base && mem->base>=8U) ||
                   (mem->has_index && mem->index>=8U))goto invalid;
            }
        }
    }
    original->mcinst=m;return true;
}

/* One target-owned memory-instruction layout, consumed by sizing and
 * emission. Previously the two paths independently counted SIB, REX,
 * operand prefixes, the displacement and immediate suffix. Keeping those
 * width decisions in one place protects the Common fragment fixed point
 * when new x86 addressing forms are introduced. */
typedef struct X64MemoryLayout {
    unsigned char prefix[8];
    unsigned prefix_bytes;
    unsigned char opcode;
    unsigned char modrm;
    unsigned char sib;
    unsigned immediate_bytes;
    unsigned displacement_bytes;
    uint32_t displacement_offset;
    uint32_t immediate_offset;
    bool has_sib;
    bool has_rex;
    bool is_vex3;
    uint32_t instruction_bytes;
} X64MemoryLayout;

static X64MemoryLayout x64_memory_layout(const MiniAsMCInst *m,
                                          uint32_t mode_bits) {
    const X64Memory *mem=m->operands[1].memory;
    const unsigned opcode=m->encoding&UINT32_C(0xff);
    const unsigned reg=m->operands[0].reg;
    const unsigned low_base=mem->base&7U;
    X64MemoryLayout layout;
    memset(&layout,0,sizeof(layout));
    layout.displacement_bytes=x64_disp_bytes(mem);
    layout.has_sib=!mem->address_16 && !mem->pc_relative &&
        ((mode_bits==0U && !mem->has_base) || mem->has_index ||
         (mem->has_base && (mem->base&7U)==4U));
    layout.is_vex3=(m->encoding&UINT32_C(0x40000))!=0U;
    if(layout.is_vex3) {
        /* The VEX3 alternative excludes legacy REX and 66/F3 prefixes. */
        const unsigned count=m->operands[2].reg;
        if(mem->address_space)
            layout.prefix[layout.prefix_bytes++]=
                (unsigned char)(mem->address_space==1U?0x64U:0x65U);
        layout.prefix[layout.prefix_bytes++]=0xc4U;
        layout.prefix[layout.prefix_bytes++]=(unsigned char)(
            (reg>=8U?0U:0x80U)|
            (!mem->has_index || mem->index<8U?0x40U:0U)|
            (!mem->has_base || mem->base<8U?0x20U:0U)|0x02U);
        layout.prefix[layout.prefix_bytes++]=(unsigned char)(
            (m->operands[0].register_64?0x80U:0U)|
            (((~count)&15U)<<3U)|(m->encoding&3U));
        layout.opcode=0xf7U;
    } else if(m->opcode==X64_IMM_MEMORY ||
              (m->encoding&X64_ENCODING_IMM8)) {
        layout.immediate_bytes=(m->encoding&X64_ENCODING_IMM8) ||
            opcode==0x83U || opcode==0xc6U ||
            opcode==0x80U || opcode==0xf6U || opcode==0xbaU ||
            opcode==0x6bU || opcode==0xc0U || opcode==0xc1U?1U:
            (m->encoding&UINT32_C(0x400))?2U:4U;
    }
    if(!layout.is_vex3) {
        layout.has_rex=!mem->address_16 &&
            (m->operands[0].register_64 || reg>=8U ||
            ((m->encoding&UINT32_C(0x200)) && reg>=4U &&
             !(m->encoding&UINT32_C(0x800))) ||
            mem->base>=8U || (mem->has_index && mem->index>=8U));
        if(m->encoding&X64_ENCODING_NOTRACK)
            layout.prefix[layout.prefix_bytes++]=0x3eU;
        if((m->encoding&X64_ENCODING_LOCK) &&
           mode_bits!=X64_CODE16 && mode_bits!=X64_CODE16_GCC)
            layout.prefix[layout.prefix_bytes++]=0xf0U;
        if(mem->address_space && mode_bits!=X64_CODE16 &&
           mode_bits!=X64_CODE16_GCC) {
            static const unsigned char segprefix[]={
                0U,0x64U,0x65U,0x2eU,0x3eU,0x26U,0x36U
            };
            if(mem->address_space<sizeof(segprefix))
                layout.prefix[layout.prefix_bytes++]=segprefix[mem->address_space];
        }
        if((m->encoding&UINT32_C(0x400)) &&
           mode_bits!=X64_CODE16 && mode_bits!=X64_CODE16_GCC)
            layout.prefix[layout.prefix_bytes++]=0x66U;
        if(m->encoding&UINT32_C(0x2000))layout.prefix[layout.prefix_bytes++]=0xf3U;
        if(m->encoding&X64_ENCODING_F2)layout.prefix[layout.prefix_bytes++]=0xf2U;
        if(layout.has_rex)
            layout.prefix[layout.prefix_bytes++]=(unsigned char)(0x40U |
                (m->operands[0].register_64?8U:0U) |
                ((reg>>3U)<<2U) |
                ((mem->has_index?mem->index>>3U:0U)<<1U) |
                (mem->base>>3U));
        if(m->encoding&UINT32_C(0x100))layout.prefix[layout.prefix_bytes++]=0x0fU;
        if(m->encoding&UINT32_C(0x10000))layout.prefix[layout.prefix_bytes++]=0x38U;
        layout.opcode=(unsigned char)opcode;
    }
    if(mem->address_16) {
        const unsigned bytes=layout.displacement_bytes;
        const unsigned mod=!mem->has_base?0U:
                           bytes==0U?0U:bytes==1U?1U:2U;
        layout.modrm=(unsigned char)((mod<<6U)|((reg&7U)<<3U)|
                                     (mem->has_base?mem->address16_rm:6U));
    }else {
        const unsigned bytes=layout.displacement_bytes;
        const unsigned mod=!mem->has_base?0U:
                           bytes==0U?0U:bytes==1U?1U:2U;
        layout.modrm=(unsigned char)(
            (((mem->pc_relative || !mem->has_base)?0U:mod)<<6U)|
            ((reg&7U)<<3U)|
            (mem->pc_relative?5U:
             layout.has_sib?4U:
             !mem->has_base?5U:low_base));
    }
    if(layout.has_sib) {
        const unsigned scale=mem->scale==8U?3U:
                             mem->scale==4U?2U:
                             mem->scale==2U?1U:0U;
        layout.sib=(unsigned char)((scale<<6U)|
            ((mem->has_index?mem->index&7U:4U)<<3U)|
            (mem->has_base?low_base:5U));
    }
    layout.displacement_offset=layout.prefix_bytes+2U+
        (layout.has_sib?1U:0U);
    layout.immediate_offset=layout.displacement_offset+
        layout.displacement_bytes;
    layout.instruction_bytes=layout.immediate_offset+layout.immediate_bytes;
    return layout;
}

/* One Target-owned encoding decision shared by measurement and emission.
 * Common never decodes the x86 ISA's REX, ModR/M, SIB, and immediate modes;
 * it only retains this MCInst and sizes the resulting fragment. */
typedef struct X64EncodingPlan {
    X64MemoryLayout memory;
    bool rex;
    bool alu_word;
    bool alu_accumulator;
    unsigned char alu_opcode;
    unsigned char alu_modrm;
    bool shift_word;
    unsigned char shift_opcode;
    unsigned char shift_modrm;
    unsigned immediate_bytes;
    uint32_t instruction_bytes;
    uint32_t immediate_offset;
} X64EncodingPlan;

static X64EncodingPlan x64_encoding_plan_variant(
    const MiniAsMCInst *m,uint32_t mode_bits,bool promote_alu_imm) {
    X64EncodingPlan plan = {0};
    const unsigned first=m->operands[0].reg;
    const unsigned second=m->operands[1].reg;
    const bool wide=m->operands[0].register_64;
    switch(m->opcode) {
        case X64_REG_MEMORY:
        case X64_IMM_MEMORY:
            plan.memory=x64_memory_layout(m,mode_bits);
            plan.instruction_bytes=plan.memory.instruction_bytes;
            break;
        case X64_ALU_IMM: {
            const unsigned group=m->encoding&7U;
            const bool symbolic=m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL ||
                m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE;
            const bool byte=(m->encoding&UINT32_C(0x200))!=0U;
            const bool word=(m->encoding&UINT32_C(0x400))!=0U;
            const int64_t imm=symbolic?0:
                (!byte && !word && !wide)?
                    (int32_t)(uint32_t)m->operands[1].immediate:
                    (int64_t)m->operands[1].immediate;
            const bool short_imm=!promote_alu_imm &&
                ((!symbolic && imm>=-128 && imm<=127) ||
                 (m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE &&
                  (m->encoding&X64_ENCODING_IMM8)!=0U));
            plan.rex=wide || first>=8U ||
                (byte && first>=4U && !(m->encoding&UINT32_C(0x800)));
            plan.alu_word=word;
            plan.alu_accumulator=first==0U && (byte || !short_imm);
            plan.immediate_bytes=byte || short_imm?1U:word?2U:4U;
            plan.alu_opcode=(unsigned char)(plan.alu_accumulator?
                0x04U+(group<<3U)+(byte?0U:1U):
                byte?0x80U:short_imm?0x83U:0x81U);
            plan.alu_modrm=(unsigned char)(0xc0U|(group<<3U)|(first&7U));
            plan.immediate_offset=(word && mode_bits!=X64_CODE16 &&
                 mode_bits!=X64_CODE16_GCC?1U:0U)+(plan.rex?1U:0U)+
                1U+(plan.alu_accumulator?0U:1U);
            plan.instruction_bytes=plan.immediate_offset+plan.immediate_bytes;
            break;
        }
        case X64_SYSTEM_MOVE:
            plan.rex=first>=8U || second>=8U;
            break;
        case X64_DOUBLE_SHIFT:
        case X64_CMOV_REG:
        case X64_REG_BINARY:
        case X64_IMUL_IMM:
            plan.rex=wide || first>=8U || second>=8U;
            break;
        case X64_BINARY8:
            plan.rex=first>=8U || second>=8U ||
                (first>=4U && !(m->encoding&UINT32_C(0x1000))) ||
                (second>=4U && !(m->encoding&UINT32_C(0x2000)));
            break;
        case X64_UNARY:
            plan.rex=wide || first>=8U ||
                ((m->encoding&UINT32_C(0x1000)) && first>=4U &&
                 !(m->encoding&UINT32_C(0x4000)));
            break;
        case X64_SHIFT_REGISTER: {
            const bool byte=(m->encoding&UINT32_C(0x200))!=0U;
            const bool cl=X64_FORM_VALUE(m)==X64_SHIFT_COUNT_CL;
            const bool imm=!cl && m->operands[1].immediate!=1U;
            plan.rex=wide || first>=8U ||
                ((m->encoding&UINT32_C(0x200)) && first>=4U &&
                 !(m->encoding&UINT32_C(0x800)));
            plan.shift_word=(m->encoding&UINT32_C(0x400))!=0U;
            plan.shift_opcode=(unsigned char)(cl?(byte?0xd2U:0xd3U):
                imm?(byte?0xc0U:0xc1U):(byte?0xd0U:0xd1U));
            plan.shift_modrm=(unsigned char)(0xc0U|
                ((m->encoding&7U)<<3U)|(first&7U));
            plan.immediate_bytes=imm?1U:0U;
            plan.immediate_offset=(plan.shift_word?1U:0U)+
                (plan.rex?1U:0U)+2U;
            plan.instruction_bytes=plan.immediate_offset+plan.immediate_bytes;
            break;
        }
        case X64_MOVZX_REG:
            plan.rex=(!(m->encoding&UINT32_C(0x800)) &&
                      ((m->encoding&0xffU)==0xb6U ||
                       (m->encoding&0xffU)==0xbeU) && first>=4U) ||
                first>=8U || second>=8U ||
                m->operands[1].register_64;
            break;
        case X64_SETCC:
            plan.rex=first>=4U && !(m->encoding&UINT32_C(0x1000));
            break;
        case X64_BSWAP:
        case X64_TEST_IMM_REG:
        case X64_BIT_IMMREG:
            plan.rex=wide || first>=8U;
            break;
        case X64_TEST8_IMM:
            plan.rex=first!=0U && first>=4U &&
                     !(m->encoding&UINT32_C(0x800));
            break;
        default:
            break;
    }
    if(m->opcode==X64_IMUL_IMM) {
        const int64_t immediate=(int64_t)m->operands[2].immediate;
        plan.immediate_bytes=immediate>=-128 && immediate<=127?1U:
            (m->encoding&UINT32_C(0x400))?2U:4U;
    } else if(m->opcode==X64_PUSH_IMM) {
        const bool symbolic=m->operands[0].kind==MINIAS_MC_BOUND_SYMBOL;
        const int32_t immediate=symbolic?0:
            (int32_t)(uint32_t)m->operands[0].immediate;
        plan.immediate_bytes=!symbolic &&
            immediate>=-128 && immediate<=127?1U:
            (m->encoding&UINT32_C(0x400))?2U:4U;
    }
    return plan;
}

static X64EncodingPlan x64_encoding_plan(
    const MiniAsMCInst *m,uint32_t mode_bits) {
    return x64_encoding_plan_variant(m,mode_bits,false);
}

/* A register-only instruction has exactly one wire image, independently of
 * whether MCLayout is asking its size or the emitter is asking its bytes.
 * Symbolic operands, PC-relative branches and memory ModR/M remain in their
 * existing dedicated plans, since those need final layout or FixupRequests.
 * This target-private value is also the sole authority for prefix ordering,
 * high-byte REX exclusions, opcode maps and immediate-field width. */
typedef struct X64RegisterEncodingPlan {
    unsigned char bytes[16];
    uint32_t size;
} X64RegisterEncodingPlan;

static bool x64_register_encoding_plan(const MiniAsMCInst *m,
                                       uint32_t mode_bits,
                                       X64RegisterEncodingPlan *result) {
    const X64EncodingPlan plan=x64_encoding_plan(m,0U);
    unsigned char *code=result->bytes;
    size_t n=0U;
    unsigned r,d,i;
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
        case X64_XMM_IMM_REG:
            r=m->operands[0].reg;
            if(m->count==3U) {
                d=m->operands[1].reg;
                code[n++]=(m->encoding&X64_ENCODING_F2)?0xf2U:0x66U;
                if(r>=8U || d>=8U)
                    code[n++]=(unsigned char)(0x40U|((d>>3U)<<2U)|(r>>3U));
                code[n++]=0x0fU;
                code[n++]=(unsigned char)m->encoding;
                code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
                code[n++]=(unsigned char)m->operands[2].immediate;
            } else {
                code[n++]=0x66U;
                if(r>=8U)code[n++]=0x41U;
                code[n++]=0x0fU;
                code[n++]=0x73U;
                code[n++]=(unsigned char)(0xc0U|
                    ((m->encoding&7U)<<3U)|(r&7U));
                code[n++]=(unsigned char)m->operands[1].immediate;
            }
            break;
        case X64_SYSTEM_MOVE: {
            unsigned sys=m->operands[0].reg,gpr=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    ((sys>>3U)<<2U)|(gpr>>3U));
            if(m->encoding&0x100U)code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|
                ((sys&7U)<<3U)|(gpr&7U));
            break;
        }
        case X64_DOUBLE_SHIFT: {
            unsigned src=m->operands[0].reg,dst=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|
                    ((src>>3U)<<2U)|(dst>>3U));
            code[n++]=0x0fU;
            code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|
                    ((src&7U)<<3U)|(dst&7U));
            if(m->encoding&0x200U)
                code[n++]=(unsigned char)m->operands[2].immediate;
            break;
        }
        case X64_ZERO:
            if(X64_FORM_VALUE(m)>sizeof(result->bytes) ||
               X64_FORM_VALUE(m)>sizeof(m->encoding))return false;
            for(i=0U;i<X64_FORM_VALUE(m);++i)
                code[n++]=(unsigned char)(m->encoding>>(8U*i));
            break;
        case X64_SETCC:
            r=m->operands[0].reg;
            if(plan.rex)code[n++]=(unsigned char)(0x40U|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|(r&7U));
            break;
        case X64_BINARY8:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|((r>>3U)<<2U)|(d>>3U));
            if(m->encoding&UINT32_C(0x100))code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|((r&7U)<<3U)|(d&7U));
            break;
        case X64_MOVSLQ:
            r=m->operands[0].reg;d=m->operands[1].reg;
            code[n++]=(unsigned char)(0x48U|((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x63U;
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_CMOV_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;
            code[n++]=(unsigned char)(0x40U+(m->encoding&15U));
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_UNARY:
            r=m->operands[0].reg;
            /* IA-32 has a compact one-byte INC/DEC GPR form (40+r / 48+r).
             * Those opcodes are REX prefixes in long mode, so the common
             * legacy-mode selector decides which wire image GNU GAS uses.
             * Width and target-owned 66 prefixes are normalized below. */
            if(mode_bits!=0U && r<8U &&
               (m->encoding&UINT32_C(0xff))==UINT32_C(0xff) &&
               ((m->encoding>>8U)&7U)<=1U &&
               !m->operands[0].register_64) {
                if(m->encoding&UINT32_C(0x2000))code[n++]=0x66U;
                code[n++]=(unsigned char)(0x40U|
                    (((m->encoding>>8U)&1U)<<3U)|r);
                break;
            }
            if(m->encoding&UINT32_C(0x10000))code[n++]=0xf3U;
            if(m->encoding&UINT32_C(0x20000))code[n++]=0xf2U;
            if(m->encoding&UINT32_C(0x2000))code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            if(m->encoding&UINT32_C(0x8000))code[n++]=0x0fU;
            code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|
                (((m->encoding>>8U)&7U)<<3U)|(r&7U));
            break;
        case X64_BSWAP:
            r=m->operands[0].reg;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=0x0fU;
            code[n++]=(unsigned char)(0xc8U+(r&7U));
            break;
        case X64_TEST_IMM_REG: {
            unsigned bytes=(m->encoding&0x400U)?2U:4U;
            r=m->operands[0].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            if(r==0U)code[n++]=0xa9U;
            else {
                code[n++]=0xf7U;
                code[n++]=(unsigned char)(0xc0U|(r&7U));
            }
            for(i=0U;i<bytes;++i)
                code[n++]=(unsigned char)(m->operands[1].immediate>>(8U*i));
            break;
        }
        case X64_SHIFT_REGISTER:
            r=m->operands[0].reg;
            if(X64_FORM_VALUE(m)!=X64_SHIFT_COUNT_IMMEDIATE &&
               X64_FORM_VALUE(m)!=X64_SHIFT_COUNT_CL)return false;
            if(plan.shift_word)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=plan.shift_opcode;
            code[n++]=plan.shift_modrm;
            if(n!=plan.immediate_offset)return false;
            for(i=0U;i<plan.immediate_bytes;++i)
                code[n++]=(unsigned char)(m->operands[1].immediate>>(8U*i));
            break;
        case X64_MOVZX_REG:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[1].register_64?8U:0U)|
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=(unsigned char)(m->encoding&0xffU);
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            break;
        case X64_TEST8_IMM:
            r=m->operands[0].reg;
            if(r==0U)code[n++]=0xa8U;
            else {
                if(plan.rex)code[n++]=(unsigned char)(0x40U|(r>>3U));
                code[n++]=0xf6U;
                code[n++]=(unsigned char)(0xc0U|(r&7U));
            }
            code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_IMUL_IMM: {
            unsigned bytes=plan.immediate_bytes;
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&0x400U)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|
                    ((d>>3U)<<2U)|(r>>3U));
            code[n++]=bytes==1U?0x6bU:0x69U;
            code[n++]=(unsigned char)(0xc0U|((d&7U)<<3U)|(r&7U));
            for(i=0U;i<bytes;++i)
                code[n++]=(unsigned char)(m->operands[2].immediate>>(8U*i));
            break;
        }
        case X64_BIT_IMMREG:
            r=m->operands[0].reg;
            if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|(r>>3U));
            code[n++]=0x0fU;code[n++]=0xbaU;
            code[n++]=(unsigned char)(0xc0U|
                ((m->encoding&7U)<<3U)|(r&7U));
            code[n++]=(unsigned char)m->operands[1].immediate;
            break;
        case X64_MOV_IMM32:
            r=m->operands[0].reg;
            /* B8+r imm32 accepts an R_X86_64_32 operand.  The ELF fixup
             * stays attached to the typed symbol, never to pointer bits. */
            if(m->encoding&0x200U) {
                if(r>=8U || (r>=4U && !(m->encoding&0x800U)))
                    code[n++]=(unsigned char)(0x40U|(r>>3U));
                code[n++]=(unsigned char)(0xb0U+(r&7U));
                code[n++]=(unsigned char)m->operands[1].immediate;
            }else {
                if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
                if(r>=8U)code[n++]=0x41U;
                code[n++]=(unsigned char)(0xb8U+(r&7U));
                for(i=0U;i<((m->encoding&UINT32_C(0x400))?2U:4U);++i)
                    code[n++]=(unsigned char)((m->operands[1].kind!=MINIAS_MC_IMMEDIATE?
                        UINT64_C(0):m->operands[1].immediate)>>(8U*i));
            }
            break;
        case X64_REG_BINARY:
            r=m->operands[0].reg;d=m->operands[1].reg;
            if(m->encoding&UINT32_C(0x400))code[n++]=0x66U;
            if(m->encoding&UINT32_C(0x4000)) {
                /* 90+rd accumulator XCHG, including canonical 66 90 and
                 * xchgq %rax,%rax -> 90.  REX.B extends the non-AX reg. */
                const unsigned other=r==0U?d:r;
                if(!(other==0U && m->operands[0].register_64) &&
                   (m->operands[0].register_64 || other>=8U))
                    code[n++]=(unsigned char)(0x40U|
                        (m->operands[0].register_64?8U:0U)|
                        (other>>3U));
                code[n++]=(unsigned char)(0x90U|(other&7U));
                break;
            }
            if(m->encoding&UINT32_C(0x2000))code[n++]=0xf3U;
            if(m->encoding&X64_ENCODING_F2)code[n++]=0xf2U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U|
                    (m->operands[0].register_64?8U:0U)|
                    (((m->encoding&0x800U)?d:r)>>3U)<<2U|
                    (((m->encoding&0x800U)?r:d)>>3U));
            if(m->encoding&0x100U)code[n++]=0x0fU;
            code[n++]=(unsigned char)m->encoding;
            code[n++]=(unsigned char)(0xc0U|
                ((((m->encoding&0x800U)?d:r)&7U)<<3U)|
                (((m->encoding&0x800U)?r:d)&7U));
            break;
        case X64_STACK:
            r=m->operands[0].reg;
            if(r>=8U)code[n++]=0x41U;
            code[n++]=(unsigned char)(m->encoding+(r&7U));
            break;
        case X64_INDIRECT_REG:
            r=m->operands[0].reg;
            if(m->encoding&X64_ENCODING_NOTRACK)code[n++]=0x3eU;
            if(r>=8U)code[n++]=0x41U;
            code[n++]=0xffU;
            code[n++]=(unsigned char)(0xc0U|((m->encoding&7U)<<3U)|(r&7U));
            break;
        default:return false;
    }
    if(n>sizeof(result->bytes))return false;
    result->size=(uint32_t)n;
    return true;
}

/* 0x66 is a width *override*, not a property of a 16-bit register.
 * Legacy 16-bit operands use the CPU default, even where the shared native
 * encoder produced 66 for MOVW, TESTW, REP STOSW, INW, and other families.
 * Decode recorded the semantic width; no opcode name/list is needed here. */
static void x64_apply_code16_register_default(const MiniAsStmt *s,
                                              X64RegisterEncodingPlan *plan) {
    const MiniAsMCInst *m=s->mcinst;
    if((s->site.mode_bits==X64_CODE16 ||
        s->site.mode_bits==X64_CODE16_GCC) &&
       X64_FORM_OPERAND_BITS(m)==16U &&
       plan->size>1U && plan->bytes[0]==0x66U) {
        memmove(plan->bytes,plan->bytes+1U,plan->size-1U);
        --plan->size;
    }
}

/* Lexical CPU mode and ELF object class are different contracts: GNU GAS
 * emits .code16gcc C instructions in ELF32 REL objects. Width comes from
 * the retained typed MCInst, never from a mnemonic spelling or white-list.
 * Each prefix is a Target-owned machine alternative counted before layout. */
static bool x64_legacy_mode_prefixes(MiniAs *as,const MiniAsStmt *s,
                                     unsigned char prefix[4],size_t *count) {
    const MiniAsMCInst *m=s->mcinst;
    bool operand32=false,address32=false;
    *count=0U;
    if(s->site.mode_bits==X64_CODE32) {
        /* Native register/memory encoders already own their 66 override.
         * A far immediate CALL/JMP bypasses those planners; its retained
         * offset width must still override code32's 32-bit default. */
        if(m && m->opcode==X64_FAR_BRANCH &&
           X64_FORM_OPERAND_BITS(m)==16U)
            prefix[(*count)++]=0x66U;
        if(m && m->opcode==X64_MOFFS && m->operands[1].memory) {
            const X64Memory *mem=(const X64Memory *)m->operands[1].memory;
            static const unsigned char segprefix[]={
                0U,0x64U,0x65U,0x2eU,0x3eU,0x26U,0x36U
            };
            if(mem->address_space>=sizeof(segprefix))return false;
            if(mem->address_space)
                prefix[(*count)++]=segprefix[mem->address_space];
        }
        return true;
    }
    if(s->site.mode_bits!=X64_CODE16 &&
       s->site.mode_bits!=X64_CODE16_GCC)return true;
    if(m==NULL) {
        minias_set_error(as,"missing-x86-code16-mcinst");return false;
    }
    operand32=X64_FORM_OPERAND_BITS(m)==32U;
    if((m->opcode==X64_REG_MEMORY || m->opcode==X64_IMM_MEMORY ||
        m->opcode==X64_MOFFS) &&
       m->count>=2U && m->operands[1].kind==MINIAS_MC_MEMORY &&
       m->operands[1].memory!=NULL) {
        const X64Memory *memory=(const X64Memory *)m->operands[1].memory;
        address32=memory->address_32;
        if(memory->address_space!=0U) {
            static const unsigned char segprefix[]={
                0U,0x64U,0x65U,0x2eU,0x3eU,0x26U,0x36U
            };
            if(memory->address_space>=sizeof(segprefix))return false;
            prefix[(*count)++]=segprefix[memory->address_space];
        }
    }
    /* GAS places address-size before operand-size for mixed 16/32
     * instructions, including GCC's 67 66 8B memory operands. */
    if(m->opcode==X64_SYSTEM_MOVE &&
       (m->encoding&UINT32_C(0x100)))operand32=false;
    if(address32)prefix[(*count)++]=0x67U;
    if(operand32)prefix[(*count)++]=0x66U;
    /* GNU GAS canonicalizes LOCK after a required operand-size override:
     * `.code16 lock btsl ...` is 66 f0 0f ba, not f0 66 0f ba. */
    if(m->encoding&X64_ENCODING_LOCK)prefix[(*count)++]=0xf0U;
    return true;
}

static bool x64_measure_base(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    const MiniAsMCInst *m=s->mcinst;
    X64RegisterEncodingPlan register_plan;
    X64EncodingPlan plan;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    if(x64_register_encoding_plan(m,s->site.mode_bits,&register_plan)) {
        x64_apply_code16_register_default(s,&register_plan);
        *size=register_plan.size;
        return true;
    }
    plan=x64_encoding_plan(m,s->site.mode_bits);
    switch(m->opcode) {
        case X64_PUSH_IMM: *size=1U+plan.immediate_bytes;return true;
        case X64_FAR_BRANCH: *size=3U+m->encoding;return true;
        case X64_MOFFS: {
            const X64Memory *mem=(const X64Memory *)m->operands[1].memory;
            *size=1U+(mem->address_16?2U:4U);
            /* Segment prefix and operand/address overrides are counted by
             * the same legacy-mode planner as every other memory form. */
            return true;
        }
        case X64_BRANCH:
            /* Start JMP/Jcc at GNU's shortest encoding; target relaxation
             * promotes external/out-of-range edges before final emission. */
            *size=m->encoding==0xe8U?
                (s->site.mode_bits==X64_CODE16 &&
                 X64_FORM_OPERAND_BITS(m)!=32U?3U:5U):2U;
            return true;
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            *size=plan.instruction_bytes;
            return true;
        }
        case X64_MOVQ_IMMREG: *size=7U;return true;
        case X64_MOVABS_IMMREG: *size=10U;return true;
        case X64_ALU_IMM: *size=plan.instruction_bytes;return true;
        default: minias_set_error(as,"unknown-x86_64-mcinst");return false;
    }
}

static bool x64_measure(MiniAs *as,const MiniAsStmt *s,uint32_t *size) {
    unsigned char prefix[4];size_t n;
    if(!x64_legacy_mode_prefixes(as,s,prefix,&n) ||
       !x64_measure_base(as,s,size))return false;
    if(*size>UINT32_MAX-n) {
        minias_set_error(as,"x86-code16-size-overflow");return false;
    }
    *size+=(uint32_t)n;
    return true;
}

/* A branch is classified once by the x86 parser: X64_BRANCH and its retained
 * opcode carry every short/near alternative.  Neither relaxation nor final
 * encoding may consult a source mnemonic a second time. */
static uint32_t x64_branch_near_width(const MiniAsStmt *stmt) {
    const MiniAsMCInst *m=stmt->mcinst;
    const bool mode16=stmt->site.mode_bits==X64_CODE16 ||
                      stmt->site.mode_bits==X64_CODE16_GCC;
    if(m->encoding==0xe8U && X64_FORM_OPERAND_BITS(m)==32U)
        return 5U;
    return m->encoding>=0x100U?(mode16?4U:6U):(mode16?3U:5U);
}

typedef enum X64BranchDistanceKind {
    X64_BRANCH_EXTERNAL,
    X64_BRANCH_LOCAL,
    X64_BRANCH_LOCAL_OVERFLOW
} X64BranchDistanceKind;

/* Binding is part of the x86 ELF expression policy, not merely a property
 * of the target's final address. GNU GAS keeps same-section GLOBAL CALL and
 * RIP-relative references relocatable, and keeps WEAK branches relocatable
 * as well: their definitions can be preempted by the final link. Ordinary
 * strong GLOBAL JMP/Jcc retain GNU's same-section short-branch relaxation.
 * Treat unknown/nonlocal bindings conservatively rather than silently
 * fixing a potentially preemptible symbol to a private displacement. */
static bool x64_can_resolve_same_section(const MiniAsSymbol *symbol,
                                         const MiniAsStmt *stmt,
                                         bool call_or_rip) {
    if(symbol==NULL || !symbol->defined || symbol->section!=stmt->section)
        return false;
    if(symbol->bind==MINIAS_STB_LOCAL)return true;
    return symbol->bind==MINIAS_STB_GLOBAL && !call_or_rip;
}

/* Resolve the final target displacement once for both the MC layout range
 * planner and emitter.  Internal addresses are ELF section offsets, and the
 * target ISA defines the PC base as the byte after the entire instruction.
 * External/cross-section references retain an explicit PC32/PLT32 fixup. */
static X64BranchDistanceKind x64_branch_distance(MiniAs *as,
                                                 const MiniAsStmt *stmt,
                                                 uint32_t instruction_width,
                                                 int64_t *distance) {
    const MiniAsMCInst *inst=stmt->mcinst;
    const MiniAsSymbolRef *expr=inst->operands[0].bound_symbol;
    const MiniAsSymbol *symbol=minias_symbol_ref_lookup(as,*expr);
    int64_t delta;
    if(X64_FORM_VALUE(inst)==1U ||
       !x64_can_resolve_same_section(symbol,stmt,inst->encoding==0xe8U))
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
    if(inst->opcode==X64_ALU_IMM &&
       inst->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE &&
       (inst->encoding&X64_ENCODING_IMM8)!=0U) {
        /* A backward symbol pair starts with GNU's 83 /group imm8. Only
         * the final section layout can certify its range. Once promoted,
         * the Common grow-only fixed point never shrinks the instruction. */
        X64EncodingPlan short_plan=x64_encoding_plan(inst,stmt->site.mode_bits);
        X64EncodingPlan long_plan=x64_encoding_plan_variant(
            inst,stmt->site.mode_bits,true);
        unsigned char prefixes[4];size_t prefix_count;
        int64_t value;
        if(!x64_legacy_mode_prefixes(as,stmt,prefixes,&prefix_count))
            return false;
        plan->terminal_size=long_plan.instruction_bytes+(uint32_t)prefix_count;
        if(stmt->size==short_plan.instruction_bytes+prefix_count &&
           (!minias_resolve_symbol_difference_ref(as,
                inst->operands[1].bound_difference,&value) ||
            value<-128 || value>127))
            plan->required_size=plan->terminal_size;
        return true;
    }
    if(inst->opcode!=X64_BRANCH)return true;
    if(inst->encoding==0xe8U || stmt->size!=2U)return true;
    plan->terminal_size=x64_branch_near_width(stmt);
    result=x64_branch_distance(as,stmt,2U,&distance);
    if(result!=X64_BRANCH_LOCAL || distance<-128 || distance>127)
        plan->required_size=plan->terminal_size;
    return true;
}

/* The ISA owns the relocation field's PC bias; the Common SymbolRef owns
 * only stable name identity and its adjusted addend. Avoid signed overflow
 * while constructing a deferred fixup proposal. */
static bool x64_adjust_symbol_ref(MiniAs *as,MiniAsSymbolRef input,
                                  int64_t correction,MiniAsSymbolRef *out) {
    if((correction>0 && input.addend>INT64_MAX-correction) ||
       (correction<0 && input.addend<INT64_MIN-correction)) {
        minias_set_error(as,"x86-symbol-fixup-addend-overflow");
        return false;
    }
    *out=input;
    out->addend+=correction;
    return true;
}

/* One memory-displacement policy for every ModR/M form. A RIP-relative
 * reference to a preemptible same-section symbol must keep its PC32 record;
 * the displacement is folded only for genuinely local references. */
static bool x64_resolve_memory_displacement(MiniAs *as,const MiniAsStmt *stmt,
                                            const X64Memory *mem,
                                            uint32_t displacement_offset,
                                            uint32_t *displacement,
                                            bool *needs_pc32,
                                            MiniAsSymbolRef *difference_relocation) {
    *displacement=(uint32_t)mem->displacement;
    *needs_pc32=false;
    if(mem->has_difference) {
        int64_t resolved;
        bool relocation;
        if(!minias_resolve_symbol_difference_field(as,&mem->difference,
                stmt->section,stmt->offset+displacement_offset,&resolved,
                &relocation,difference_relocation) ||
           (relocation && mem->address_16) ||
           (!relocation && mem->address_16 &&
            (resolved<INT16_MIN || resolved>UINT16_MAX)) ||
           (!relocation && !mem->address_16 &&
            (resolved<INT32_MIN || resolved>UINT32_MAX))) {
            minias_set_error(as,"invalid-x86-memory-symbol-difference");
            return false;
        }
        *displacement=relocation?0U:(uint32_t)resolved;
        *needs_pc32=relocation;
        return true;
    }
    if(!mem->has_symbol || !mem->pc_relative)return true;
    /* The GOT entry is link-time state, not the target symbol's local
     * address. Even same-section definitions must keep their GOT fixup. */
    if(mem->reloc_flavor==X64_DISP_GOTPCREL) {
        *displacement=0U;
        *needs_pc32=true;
        return true;
    }
    const MiniAsSymbol *symbol=minias_symbol_ref_lookup(as,mem->symbol);
    int64_t distance;
    if(!x64_can_resolve_same_section(symbol,stmt,true)) {
        *displacement=0U;
        *needs_pc32=true;
        return true;
    }
    if(symbol->value>INT64_MAX || stmt->offset>INT64_MAX ||
       stmt->offset>(uint64_t)INT64_MAX-(uint64_t)stmt->size) {
        minias_set_error(as,"x86-rip-address-overflow");
        return false;
    }
    distance=(int64_t)symbol->value-(int64_t)(stmt->offset+stmt->size);
    if((mem->symbol.addend>0 && distance>INT64_MAX-mem->symbol.addend) ||
       (mem->symbol.addend<0 && distance<INT64_MIN-mem->symbol.addend)) {
        minias_set_error(as,"x86-rip-addend-overflow");
        return false;
    }
    distance+=mem->symbol.addend;
    if(distance<INT32_MIN || distance>INT32_MAX) {
        minias_set_error(as,"x86-rip-rel32-out-of-range");
        return false;
    }
    *displacement=(uint32_t)distance;
    return true;
}

/* A single relocation owner for the three old memory-emission branches:
 * absolute symbolic displacement, RIP-relative PC32, and symbolic immediate.
 * The immediate field's ELF signedness is determined solely by its ISA
 * operand width; it must not change with the addressing mode. */
static bool x64_emit_memory_fixups(MiniAs *as,const MiniAsStmt *stmt,
                                   const MiniAsMCInst *inst,
                                   const X64Memory *mem,
                                   const X64MemoryLayout *layout,
                                   bool needs_pc32,
                                   MiniAsSymbolRef difference_relocation) {
    if(mem->has_difference && needs_pc32 &&
       !minias_encode_add_fixup_ref(as,stmt->section,
           stmt->offset+layout->displacement_offset,
           MINIAS_FIXUP_X64_PC32,4U,difference_relocation,false))
        return false;
    if(mem->has_symbol) {
        if(mem->pc_relative && needs_pc32) {
            MiniAsSymbolRef relative;
            MiniAsFixupKind kind=MINIAS_FIXUP_X64_PC32;
            if(mem->reloc_flavor==X64_DISP_GOTPCREL) {
                /* GNU GAS allows link-time MOV->LEA relaxation for GOTPCREL
                 * loads. The REX variant records the present REX prefix;
                 * non-MOV consumers retain non-relaxable GOTPCREL. */
                if((inst->encoding&UINT32_C(0xff))==UINT32_C(0x8b))
                    kind=layout->has_rex?
                        MINIAS_FIXUP_X64_REX_GOTPCRELX:
                        MINIAS_FIXUP_X64_GOTPCRELX;
                else
                    kind=MINIAS_FIXUP_X64_GOTPCREL;
            }
            if(!x64_adjust_symbol_ref(as,mem->symbol,
                    -4-(int64_t)layout->immediate_bytes,&relative) ||
               !minias_encode_add_fixup_ref(as,stmt->section,
                    stmt->offset+layout->displacement_offset,
                    kind,4U,relative,
                    as->output_elf32 && kind==MINIAS_FIXUP_X64_PC32))
                return false;
        } else if(!mem->pc_relative) {
            MiniAsFixupKind kind=
                (as->output_elf32 || layout->displacement_bytes==2U)?
                    MINIAS_FIXUP_ABS:MINIAS_FIXUP_X64_ABS32S;
            if(mem->reloc_flavor==X64_DISP_I386_GOT32X)
                kind=MINIAS_FIXUP_X86_GOT32X;
            else if(mem->reloc_flavor==X64_DISP_I386_GOTOFF)
                kind=MINIAS_FIXUP_X86_GOTOFF;
            else if(mem->reloc_flavor==X64_DISP_I386_GOTPC)
                kind=MINIAS_FIXUP_X86_GOTPC;
            MiniAsSymbolRef adjusted;
            if(kind==MINIAS_FIXUP_X86_GOTPC &&
               !x64_adjust_symbol_ref(as,mem->symbol,
                   (int64_t)layout->displacement_offset,&adjusted))return false;
            if(!minias_encode_add_fixup_ref(as,stmt->section,
                   stmt->offset+layout->displacement_offset,
                   kind,layout->displacement_bytes,
                   kind==MINIAS_FIXUP_X86_GOTPC?adjusted:mem->symbol,
                   kind==MINIAS_FIXUP_X86_GOTOFF))return false;
        }
    }
    if(inst->opcode==X64_IMM_MEMORY &&
       inst->operands[2].kind==MINIAS_MC_BOUND_SYMBOL) {
        if((layout->immediate_bytes!=2U && layout->immediate_bytes!=4U) ||
           inst->operands[2].bound_symbol==NULL) {
            minias_set_error(as,"invalid-x86-memory-immediate-fixup-width");
            return false;
        }
        return minias_encode_add_fixup_ref(as,stmt->section,
            stmt->offset+layout->immediate_offset,
            inst->operands[0].register_64 && !as->output_elf32?
                MINIAS_FIXUP_X64_ABS32S:MINIAS_FIXUP_ABS,
            layout->immediate_bytes,*inst->operands[2].bound_symbol,false);
    }
    return true;
}

static bool x64_encode_base(MiniAs *as,const MiniAsStmt *s) {
    const MiniAsMCInst *m=s->mcinst;
    X64RegisterEncodingPlan register_plan;
    X64EncodingPlan plan;
    unsigned char code[16];size_t n=0;unsigned r;
    if (!m) {minias_set_error(as,"missing-x86_64-mcinst");return false;}
    if(x64_register_encoding_plan(m,s->site.mode_bits,&register_plan)) {
        x64_apply_code16_register_default(s,&register_plan);
        bool difference_relocation=false;
        MiniAsSymbolRef difference_relocation_ref;
        if(m->opcode==X64_MOV_IMM32 &&
           m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE) {
            const unsigned width=m->encoding&UINT32_C(0x400)?2U:4U;
            int64_t resolved;
            if(width!=4U || register_plan.size<width ||
               !minias_resolve_symbol_difference_field(
                   as,m->operands[1].bound_difference,s->section,
                   s->offset+register_plan.size-width,&resolved,
                   &difference_relocation,&difference_relocation_ref) ||
               (!difference_relocation &&
                (resolved<INT32_MIN || resolved>UINT32_MAX))) {
                minias_set_error(as,"invalid-x86-movl-symbol-difference");
                return false;
            }
            uint32_t immediate=difference_relocation?0U:(uint32_t)resolved;
            for(unsigned i=0U;i<width;++i)
                register_plan.bytes[register_plan.size-width+i]=
                    (unsigned char)(immediate>>(8U*i));
        }
        if(!minias_emit_instruction_bytes(as,s->section,
                register_plan.bytes,register_plan.size))return false;
        if(difference_relocation)
            return minias_encode_add_fixup_ref(as,s->section,
                s->offset+register_plan.size-4U,
                MINIAS_FIXUP_X64_PC32,4U,difference_relocation_ref,false);
        if(m->opcode==X64_MOV_IMM32 &&
           m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL) {
            const unsigned width=m->encoding&UINT32_C(0x400)?2U:4U;
            if((m->encoding!=0U && m->encoding!=UINT32_C(0x400)) ||
               register_plan.size<width) {
                minias_set_error(as,"invalid-x86-movl-symbolic-imm32-layout");
                return false;
            }
            MiniAsSymbolRef adjusted;
            MiniAsFixupKind kind=X64_FORM_VALUE(m)==X64_FORM_I386_GOTPC?
                MINIAS_FIXUP_X86_GOTPC:MINIAS_FIXUP_ABS;
            if(kind==MINIAS_FIXUP_X86_GOTPC &&
               !x64_adjust_symbol_ref(as,*m->operands[1].bound_symbol,
                   (int64_t)(register_plan.size-width),&adjusted))return false;
            return minias_encode_add_fixup_ref(as,s->section,
                s->offset+register_plan.size-width,
                kind,width,kind==MINIAS_FIXUP_X86_GOTPC?
                    adjusted:*m->operands[1].bound_symbol,false);
        }
        return true;
    }
    plan=x64_encoding_plan(m,s->site.mode_bits);
    if(m->opcode==X64_ALU_IMM &&
       m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE &&
       (m->encoding&X64_ENCODING_IMM8)!=0U &&
       s->size!=plan.instruction_bytes) {
        plan=x64_encoding_plan_variant(m,s->site.mode_bits,true);
    }
    switch(m->opcode) {
        case X64_PUSH_IMM: {
            bool symbol=m->operands[0].kind==MINIAS_MC_BOUND_SYMBOL;
            int32_t imm=symbol?0:(int32_t)(uint32_t)m->operands[0].immediate;
            unsigned i;
            bool short_imm=plan.immediate_bytes==1U;
            code[n++]=short_imm?0x6aU:0x68U;
            for(i=0U;i<plan.immediate_bytes;++i)
                code[n++]=(unsigned char)((uint32_t)imm>>(8U*i));
            if(symbol) {
                const MiniAsSymbolRef *expr=m->operands[0].bound_symbol;
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                return minias_encode_add_fixup_ref(as,s->section,s->offset+1U,
                    (m->encoding&UINT32_C(0x400))?
                        MINIAS_FIXUP_ABS:MINIAS_FIXUP_X64_ABS32S,
                    plan.immediate_bytes,*expr,false);
            }
            break;
        }
        case X64_FAR_BRANCH: {
            const unsigned width=m->encoding;
            const bool symbolic=m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL;
            const bool difference=
                m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE;
            bool difference_relocation=false;
            MiniAsSymbolRef difference_relocation_ref;
            uint64_t offset=symbolic || difference?0U:m->operands[1].immediate;
            const uint16_t seg=(uint16_t)m->operands[0].immediate;
            unsigned i;
            if(width!=2U && width!=4U) {
                minias_set_error(as,"x86-far-invalid-offset-width");
                return false;
            }
            if(difference) {
                int64_t resolved;
                if(!minias_resolve_symbol_difference_field(as,
                       m->operands[1].bound_difference,s->section,
                       s->offset+1U,&resolved,&difference_relocation,
                       &difference_relocation_ref) ||
                   (difference_relocation && width!=4U) ||
                   (!difference_relocation &&
                    ((width==2U && (resolved<INT16_MIN ||
                                    resolved>UINT16_MAX)) ||
                     (width==4U && (resolved<INT32_MIN ||
                                    resolved>UINT32_MAX))))) {
                    minias_set_error(as,"invalid-x86-far-symbol-difference:%s",
                                     s->args);
                    return false;
                }
                offset=difference_relocation?0U:(uint32_t)resolved;
            }
            code[n++]=X64_FORM_VALUE(m)==1U?0x9aU:0xeaU;
            for(i=0U;i<width;++i)
                code[n++]=(unsigned char)(offset>>(8U*i));
            code[n++]=(unsigned char)seg;
            code[n++]=(unsigned char)(seg>>8U);
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(difference_relocation)
                return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+1U,MINIAS_FIXUP_X64_PC32,width,
                    difference_relocation_ref,false);
            if(!symbolic)return true;
            return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+1U,MINIAS_FIXUP_ABS,width,
                    *m->operands[1].bound_symbol,false);
        }
        case X64_MOFFS: {
            const X64Memory *mem=(const X64Memory *)m->operands[1].memory;
            unsigned i;
            const unsigned width=mem && mem->address_16?2U:4U;
            if(mem==NULL ||
               mem->has_base || mem->has_index) {
                minias_set_error(as,"x86-moffs-invalid-address");return false;
            }
            code[n++]=(unsigned char)m->encoding;
            for(i=0U;i<width;++i)
                code[n++]=(unsigned char)((uint64_t)mem->displacement>>(8U*i));
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(!mem->has_symbol)return true;
            return minias_encode_add_fixup_ref(as,s->section,
                s->offset+n-width,MINIAS_FIXUP_ABS,width,mem->symbol,false);
        }
        case X64_BRANCH: {
            const MiniAsSymbolRef *expr=m->operands[0].bound_symbol;
            const MiniAsSymbol *target=minias_symbol_ref_lookup(as,*expr);
            const bool defined_local_cross_section=target && target->defined &&
                target->bind==MINIAS_STB_LOCAL && target->section>=0 &&
                target->section!=s->section;
            /* GNU's ELF64 native .code64 external JMP/Jcc/CALL all default
             * to PLT32. Inside lexical .code32 (even in an ELF64 object),
             * GNU uses PC32 instead. Defined local cross-section targets
             * also use PC32; explicit @PLT always overrides either mode. */
            const bool use_plt=X64_FORM_VALUE(m)==1U ||
                (s->site.mode_bits==0U && !as->output_elf32 &&
                 !defined_local_cross_section);
            MiniAsFixupKind kind=use_plt?MINIAS_FIXUP_X64_PLT32:
                MINIAS_FIXUP_X64_PC32;
            size_t width=s->size;
            int64_t displacement=0;
            X64BranchDistanceKind resolution;
            bool local;
            unsigned i;
            if(width!=2U && width!=x64_branch_near_width(s) &&
               !(m->encoding==0xe8U && width==5U &&
                 s->site.mode_bits==X64_CODE16_GCC)) {
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
            const unsigned disp_width=width==2U?1U:
                (m->encoding==0xe8U || m->encoding<0x100U)?
                    (unsigned)width-1U:(unsigned)width-2U;
            if(local &&
               ((width==2U && (displacement<-128 || displacement>127)) ||
                (disp_width==2U &&
                 (displacement<INT16_MIN || displacement>INT16_MAX)) ||
                (disp_width==4U &&
                 (displacement<INT32_MIN || displacement>INT32_MAX)))) {
                minias_set_error(as,"x86-branch-displacement-range:line=%zu",
                                 s->line);
                return false;
            }
            for(i=0U;i<disp_width;++i)
                code[n++]=(unsigned char)((uint32_t)displacement>>(8U*i));
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(local)return true;
            {
                MiniAsSymbolRef pc_relative;
                if(!x64_adjust_symbol_ref(as,*expr,
                        -(int64_t)disp_width,&pc_relative))return false;
                return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+width-disp_width,
                    disp_width==2U?MINIAS_FIXUP_PREL32:kind,
                    disp_width,pc_relative,
                    disp_width==4U &&
                    kind==MINIAS_FIXUP_X64_PC32);
            }
        }
        case X64_IMM_MEMORY:
        case X64_REG_MEMORY: {
            const X64Memory *mem=m->operands[1].memory;
            const X64MemoryLayout layout=plan.memory;
            bool needs_pc32=false;
            bool difference_relocation=false;
            MiniAsSymbolRef difference_relocation_ref;
            MiniAsSymbolRef displacement_relocation_ref={0};
            uint32_t disp=0U;
            if(layout.instruction_bytes>sizeof(code)) {
                minias_set_error(as,"x86-memory-instruction-exceeds-encoder-capacity");
                return false;
            }
            const unsigned extra=layout.immediate_bytes;
            uint32_t immediate=extra?
                (uint32_t)m->operands[2].immediate:0U;
            unsigned i;
            if(m->opcode==X64_IMM_MEMORY &&
               m->operands[2].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE) {
                int64_t resolved;
                if(extra!=4U ||
                   !minias_resolve_symbol_difference_field(
                       as,m->operands[2].bound_difference,s->section,
                       s->offset+layout.immediate_offset,&resolved,
                       &difference_relocation,&difference_relocation_ref) ||
                   (!difference_relocation &&
                    (resolved<INT32_MIN || resolved>UINT32_MAX))) {
                    minias_set_error(as,"invalid-x86-memory-immediate-difference:%s:line=%zu",
                                     s->args,s->line);
                    return false;
                }
                immediate=difference_relocation?0U:(uint32_t)resolved;
            }
            if(!x64_resolve_memory_displacement(as,s,mem,
                   layout.displacement_offset,&disp,&needs_pc32,
                   &displacement_relocation_ref))
                return false;
            memcpy(code,layout.prefix,layout.prefix_bytes);
            n=layout.prefix_bytes;
            code[n++]=layout.opcode;
            code[n++]=layout.modrm;
            if(layout.has_sib)code[n++]=layout.sib;
            if(n!=layout.displacement_offset) {
                minias_set_error(as,"invalid-x86-memory-plan-offset");
                return false;
            }
            for (i=0U;i<layout.displacement_bytes;++i)
                code[n++]=(unsigned char)(disp>>(8U*i));
            for(i=0U;i<extra;++i)
                code[n++]=(unsigned char)(immediate>>(8U*i));
            if(n!=layout.instruction_bytes || n!=s->size) {
                minias_set_error(as,"invalid-x86-memory-layout");
                return false;
            }
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(!x64_emit_memory_fixups(as,s,m,mem,&layout,needs_pc32,
                   displacement_relocation_ref))
                return false;
            if(difference_relocation)
                return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+layout.immediate_offset,
                    MINIAS_FIXUP_X64_PC32,4U,difference_relocation_ref,false);
            return true;
        }
        case X64_MOVABS_IMMREG: {
            const MiniAsMCOperand *value=&m->operands[1];
            unsigned i;
            r=m->operands[0].reg;
            code[n++]=(unsigned char)(0x48U|(r>>3U));
            code[n++]=(unsigned char)(0xb8U|(r&7U));
            for(i=0U;i<8U;++i)
                code[n++]=(unsigned char)((value->kind==MINIAS_MC_BOUND_SYMBOL?
                    UINT64_C(0):value->immediate)>>(8U*i));
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(value->kind!=MINIAS_MC_BOUND_SYMBOL)return true;
            return minias_encode_add_fixup_ref(as,s->section,s->offset+2U,
                MINIAS_FIXUP_X64_ABS64,8U,
                *value->bound_symbol,false);
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
                    (src->kind==MINIAS_MC_BOUND_SYMBOL?0U:src->immediate))>>(8U*i));
            if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
            if(src->kind!=MINIAS_MC_BOUND_SYMBOL)return true;
            return minias_encode_add_fixup_ref(as,s->section,s->offset+3U,
                MINIAS_FIXUP_X64_ABS32S,4U,*src->bound_symbol,false);
        }
        case X64_ALU_IMM: {
            unsigned i;
            bool difference=m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE;
            bool symbol=m->operands[1].kind==MINIAS_MC_BOUND_SYMBOL || difference;
            uint64_t imm=symbol?0U:m->operands[1].immediate;
            int64_t resolved_diff=0;
            bool difference_relocation=false;
            MiniAsSymbolRef difference_relocation_ref;
            if(difference) {
                if((plan.immediate_bytes!=1U && plan.immediate_bytes!=4U) ||
                   !minias_resolve_symbol_difference_field(
                       as,m->operands[1].bound_difference,s->section,
                       s->offset+plan.immediate_offset,&resolved_diff,
                       &difference_relocation,&difference_relocation_ref) ||
                   (difference_relocation && plan.immediate_bytes!=4U) ||
                   (!difference_relocation &&
                    (plan.immediate_bytes==1U?
                        (resolved_diff<-128 || resolved_diff>127):
                        (resolved_diff<INT32_MIN || resolved_diff>UINT32_MAX)))) {
                    minias_set_error(as,"invalid-x86-alu-symbol-difference:%s",
                                     s->args);
                    return false;
                }
                imm=difference_relocation?0U:(uint32_t)resolved_diff;
            }
            r=m->operands[0].reg;
            if(plan.alu_word && s->site.mode_bits!=X64_CODE16 &&
               s->site.mode_bits!=X64_CODE16_GCC)code[n++]=0x66U;
            if(plan.rex)
                code[n++]=(unsigned char)(0x40U |
                    (m->operands[0].register_64?8U:0U) | (r>>3U));
            code[n++]=plan.alu_opcode;
            if(!plan.alu_accumulator)code[n++]=plan.alu_modrm;
            if(n!=plan.immediate_offset) {
                minias_set_error(as,"invalid-x86-alu-plan-offset");return false;
            }
            for(i=0U;i<plan.immediate_bytes;++i)
                code[n++]=(unsigned char)(imm>>(8U*i));
            if(difference_relocation) {
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+plan.immediate_offset,
                    MINIAS_FIXUP_X64_PC32,4U,difference_relocation_ref,false);
            }
            if(symbol && !difference) {
                const MiniAsSymbolRef *expr=m->operands[1].bound_symbol;
                MiniAsSymbolRef adjusted;
                MiniAsFixupKind kind=X64_FORM_VALUE(m)==X64_FORM_I386_GOTPC?
                    MINIAS_FIXUP_X86_GOTPC:
                    m->operands[0].register_64 && !as->output_elf32?
                        MINIAS_FIXUP_X64_ABS32S:MINIAS_FIXUP_ABS;
                if(plan.immediate_bytes!=2U && plan.immediate_bytes!=4U) {
                    minias_set_error(as,"invalid-x86-alu-symbol-width");
                    return false;
                }
                if(!minias_emit_instruction_bytes(as,s->section,code,n))return false;
                /* GNU R_386_GOTPC computes GOT + A - P.  GCC's GOT-base
                 * ADD expects GOT relative to the *instruction start*;
                 * the embedded REL addend therefore includes the byte
                 * distance to the immediate field (1 for accumulator
                 * opcode, 2 for opcode+ModR/M). */
                if(kind==MINIAS_FIXUP_X86_GOTPC &&
                   !x64_adjust_symbol_ref(as,*expr,
                       (int64_t)plan.immediate_offset,&adjusted))return false;
                return minias_encode_add_fixup_ref(as,s->section,
                    s->offset+plan.immediate_offset,
                    kind,plan.immediate_bytes,
                    kind==MINIAS_FIXUP_X86_GOTPC?adjusted:*expr,false);
            }
            if(n!=s->size) {
                minias_set_error(as,"x86-alu-layout-size-drift:%zu!=%u:%s",
                                 n,s->size,s->args);
                return false;
            }
            break;
        }
        default: minias_set_error(as,"bad-x86_64-mcinst");return false;
    }
    return minias_emit_instruction_bytes(as,s->section,code,n);
}

static bool x64_encode(MiniAs *as,const MiniAsStmt *s) {
    MiniAsStmt native;
    unsigned char prefix[4];size_t n;
    if(!x64_legacy_mode_prefixes(as,s,prefix,&n))return false;
    if(n==0U)return x64_encode_base(as,s);
    if(s->size<n || s->offset>SIZE_MAX-n) {
        minias_set_error(as,"x86-code16-invalid-prefix-layout");return false;
    }
    if(!minias_emit_instruction_bytes(as,s->section,prefix,n))return false;
    native=*s;
    native.offset+=n;
    native.size-=(uint32_t)n;
    return x64_encode_base(as,&native);
}

static bool x64_code_align(MiniAs *as,const MiniAsStmt *stmt,size_t n) {
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
    /* GNU GAS --32 uses i386-compatible LEA/CS-prefix NOPs instead of the
     * newer 0F 1F family. Real-mode .code16/.code16gcc has its own 16-bit
     * ModR/M spellings. Keep these ISA profiles at the frozen ALIGN site,
     * not in a final-mode mutable global or a second Common fill path. */
    static const unsigned char nops32[15][15]={
        {0x90},
        {0x66,0x90},
        {0x8d,0x76,0x00},
        {0x8d,0x74,0x26,0x00},
        {0x2e,0x8d,0x74,0x26,0x00},
        {0x8d,0xb6,0x00,0x00,0x00,0x00},
        {0x8d,0xb4,0x26,0x00,0x00,0x00,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x90},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x66,0x90},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x8d,0x76,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x8d,0x74,0x26,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x2e,0x8d,0x74,0x26,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x8d,0xb6,0x00,0x00,0x00,0x00},
        {0x2e,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,0x8d,0xb4,0x26,0x00,0x00,0x00,0x00}
    };
    static const unsigned char nops16[15][15]={
        {0x90},
        {0x89,0xf6},
        {0x8d,0x74,0x00},
        {0x8d,0xb4,0x00,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00,0x90},
        {0x2e,0x8d,0xb4,0x00,0x00,0x89,0xf6},
        {0x2e,0x8d,0xb4,0x00,0x00,0x8d,0x74,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00,0x8d,0xb4,0x00,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00,0x90},
        {0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00,0x89,0xf6},
        {0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00,0x8d,0x74,0x00},
        {0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00,0x8d,0xb4,0x00,0x00},
        {0xeb,0x0d,0x2e,0x8d,0xb4,0x00,0x00,0x2e,0x8d,0xb4,0x00,0x00,0x8d,0x74,0x00}
    };
    const bool legacy32=as->output_elf32 && stmt->site.mode_bits==X64_CODE32;
    const bool legacy16=as->output_elf32 &&
        (stmt->site.mode_bits==X64_CODE16 ||
         stmt->site.mode_bits==X64_CODE16_GCC);
    const int section=stmt->section;
    /* GNU GAS leaves a single 0x90 at the beginning of a code-alignment
     * fragment immediately following a raw DATA fragment, then fills the
     * remainder with its canonical multi-byte table. This differs from
     * alignment after a machine INSN (or another ALIGN) and matters to
     * byte-exact Linux .S comparisons. Common owns the predecessor and
     * subsection test; Target only chooses the machine-specific NOP bytes. */
    if(n!=0U && minias_code_alignment_preceded_by_adjacent_data(as,section)) {
        if(!minias_emit_instruction_bytes(as,section,nops[0],1U))return false;
        --n;
    }
    while(n!=0U) {
        const size_t limit=legacy32 || legacy16?15U:11U;
        size_t width=n>limit?limit:n;
        const unsigned char *bytes=legacy16?nops16[width-1U]:
            legacy32?nops32[width-1U]:nops[width-1U];
        if(!minias_emit_instruction_bytes(as,section,bytes,width))
            return false;
        n-=width;
    }
    return true;
}

static bool x64_explicit_alignment_fill_is_code_nop(uint8_t fill) {
    /* GNU GAS treats .balign/.p2align with 0x90 on an executable x86
     * section as a request for canonical multi-byte machine NOPs. */
    return fill==0x90U;
}

static bool x64_profile(const char *march,const char *mabi,
                        MiniAsAssemblyConfig *cfg,char *err,size_t cap) {
    bool i386=(mabi && !strcmp(mabi,"i386"));
    if ((march && strcmp(march,"x86-64") && strcmp(march,"i386")) ||
        (mabi && strcmp(mabi,"sysv") && !i386) ||
        (i386 && march && strcmp(march,"i386"))) {
        if(cap) snprintf(err,cap,"unsupported-x86_64-isa-abi");
        return false;
    }
    memset(cfg,0,sizeof(*cfg));cfg->target=minias_target_x86_64();
    if(i386) {
        /* GNU --32 is an object-format profile, independent of a later
         * source-local `.code16gcc` / `.code32` CPU mode switch. */
        cfg->elf32=true;
        cfg->object_machine=3U; /* EM_386 */
        cfg->output_rel_relocations=true;
    }
    return true;
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
    if(kind==MINIAS_FIXUP_X64_GOTPCREL)return 9U;
    if(kind==MINIAS_FIXUP_X64_GOTPCRELX)return 41U;
    if(kind==MINIAS_FIXUP_X64_REX_GOTPCRELX)return 42U;
    return 0U;
}

static uint32_t x64_reloc_for_object(const MiniAs *as,
                                     MiniAsFixupKind kind,unsigned width) {
    if(!as->output_elf32)return x64_reloc(kind,width);
    /* ELF32 i386 REL: addends live in their destination sections.  The
     * generic writer handles the object encoding; Target only maps the
     * ABI relocation family, never a Linux/Kbuild filename special case. */
    if(width==2U) {
        if(kind==MINIAS_FIXUP_ABS)return 20U; /* R_386_16 */
        if(kind==MINIAS_FIXUP_PREL32 || kind==MINIAS_FIXUP_X64_PC32)
            return 21U; /* R_386_PC16 */
        return 0U;
    }
    if(width!=4U)return 0U;
    if(kind==MINIAS_FIXUP_ABS || kind==MINIAS_FIXUP_X64_ABS32S)
        return 1U; /* R_386_32 */
    if(kind==MINIAS_FIXUP_X64_PC32 || kind==MINIAS_FIXUP_PREL32)
        return 2U; /* R_386_PC32 */
    if(kind==MINIAS_FIXUP_X64_PLT32)return 4U; /* R_386_PLT32 */
    if(kind==MINIAS_FIXUP_X86_GOTPC)return 10U; /* R_386_GOTPC */
    if(kind==MINIAS_FIXUP_X86_GOT32X)return 43U; /* R_386_GOT32X */
    if(kind==MINIAS_FIXUP_X86_GOTOFF)return 9U; /* R_386_GOTOFF */
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
        case MINIAS_FIXUP_X64_GOTPCREL:
        case MINIAS_FIXUP_X64_GOTPCRELX:
        case MINIAS_FIXUP_X64_REX_GOTPCRELX:
            return width==4U;
        default:
            return false;
    }
}
static bool x64_supports_fixup_for_object(const MiniAs *as,
                                          MiniAsFixupKind kind,unsigned width) {
    if(as->output_elf32 && width==2U)
        return kind==MINIAS_FIXUP_ABS || kind==MINIAS_FIXUP_PREL32 ||
               kind==MINIAS_FIXUP_X64_PC32;
    if(as->output_elf32 && width==4U &&
       (kind==MINIAS_FIXUP_X86_GOTPC ||
        kind==MINIAS_FIXUP_X86_GOT32X ||
        kind==MINIAS_FIXUP_X86_GOTOFF))return true;
    return x64_supports_fixup(kind,width);
}

/* GNU GAS accepts target register names in CFI directives (with or without
 * a percent sign). Core parses numeric DWARF registers first; this target
 * provides the ABI-specific name to DWARF register-number mapping only.
 * ModR/M codes deliberately differ from DWARF on AMD64. */
static bool x64_resolve_cfi_register(const MiniAs *as,const char *name,
                                     unsigned *dwarf_reg) {
    static const unsigned amd64_gpr_dwarf[16]={
        0U,2U,1U,3U,7U,6U,4U,5U,8U,9U,10U,11U,12U,13U,14U,15U
    };
    static const struct {const char *name;unsigned dwarf;} i386_extra[]={
        {"eip",8U},{"eflags",9U},
        {"es",40U},{"cs",41U},{"ss",42U},
        {"ds",43U},{"fs",44U},{"gs",45U}
    };
    char normalized[32];
    const char *bare=name;
    unsigned reg;
    bool wide;
    size_t i,n;
    if(name==NULL || dwarf_reg==NULL)return false;
    if(*bare=='%')++bare;
    n=strlen(bare);
    if(n==0U || n+2U>sizeof(normalized))return false;
    normalized[0]='%';
    memcpy(normalized+1U,bare,n+1U);
    if(as->output_elf32) {
        if(x64_register(normalized,&reg,&wide) && !wide && reg<8U) {
            *dwarf_reg=reg;return true;
        }
        if(x64_register16(normalized,&reg) && reg<8U) {
            *dwarf_reg=reg;return true;
        }
        for(i=0U;i<sizeof(i386_extra)/sizeof(i386_extra[0]);++i)
            if(!strcmp(bare,i386_extra[i].name)) {
                *dwarf_reg=i386_extra[i].dwarf;return true;
            }
        if(x64_xmm_register(normalized,&reg) && reg<8U) {
            *dwarf_reg=21U+reg;return true;
        }
        if(strncmp(bare,"st",2U)==0) {
            if(n==3U && bare[2U]>='0' && bare[2U]<='7') {
                *dwarf_reg=11U+(unsigned)(bare[2U]-'0');return true;
            }
            if(n==5U && bare[2U]=='(' && bare[3U]>='0' &&
               bare[3U]<='7' && bare[4U]==')') {
                *dwarf_reg=11U+(unsigned)(bare[3U]-'0');return true;
            }
        }
        return false;
    }
    if(x64_register(normalized,&reg,&wide) ||
       x64_register16(normalized,&reg)) {
        *dwarf_reg=amd64_gpr_dwarf[reg];return true;
    }
    if(!strcmp(bare,"rip")) {*dwarf_reg=16U;return true;}
    if(x64_xmm_register(normalized,&reg)) {
        *dwarf_reg=17U+reg;return true;
    }
    return false;
}
static void x64_resolve_cfi_abi(const MiniAs *as,MiniAsCfiABI *abi) {
    if(as->output_elf32) {
        /* GNU i386 SysV CFI: EIP=8, ESP=4, 32-bit stack/return slots.
         * .code16/.code32 at individual sites do not alter the object ABI. */
        abi->code_align=1U;
        abi->data_align=-4;
        abi->return_reg=8U;
        abi->stack_reg=4U;
        abi->address_size=4U;
        abi->initial_cfa_offset=4U;
        abi->initial_ra_saved=true;
        abi->eh_final_alignment=4U;
        abi->eh_fde_alignment=4U;
    }
}
static bool x64_init_target_state(MiniAs *as,
                                  const MiniAsAssemblyConfig *config) {
    X64TargetState *state;
    (void)config;
    state=calloc(1U,sizeof(*state));
    if(state==NULL) {
        minias_set_error(as,"out-of-memory:x86-target-state");
        return false;
    }
    state->code_mode=config->elf32?X64_CODE32:0U;
    as->target_state=state;
    return true;
}

static void x64_capture_site_state(const MiniAs *as,
                                   MiniAsTargetSiteState *site) {
    const X64TargetState *state=(const X64TargetState *)as->target_state;
    site->mode_bits=state==NULL?0U:state->code_mode;
}

static void x64_destroy_target_state(MiniAs *as) {
    free(as->target_state);
    as->target_state=NULL;
}

/* The GNU EFI thunk switches to IA-32 *within* ELF64.
 * This is per-statement Target mode, never a driver/Core ELF-class switch. */
static bool x64_parse_directive(MiniAs *as,const char *op,const char *args,
                                size_t line,bool *handled) {
    bool code32=!strcmp(op,".code32");
    bool code16=!strcmp(op,".code16");
    bool code16gcc=!strcmp(op,".code16gcc");
    X64TargetState *state=(X64TargetState *)as->target_state;
    *handled=code32 || code16 || code16gcc || !strcmp(op,".code64");
    if(!*handled)return true;
    if(*args) {
        minias_set_error(as,"unsupported-x86_64-code-mode:%s:line=%zu",
                         args,line);
        return false;
    }
    if(state==NULL) {
        minias_set_error(as,"missing-x86-target-state");
        return false;
    }
    state->code_mode=code16gcc?X64_CODE16_GCC:
                     code16?X64_CODE16:code32?X64_CODE32:0U;
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
        {"R_X86_64_GOTPCREL",MINIAS_FIXUP_X64_GOTPCREL,4U},
        {"R_X86_64_GOTPCRELX",MINIAS_FIXUP_X64_GOTPCRELX,4U},
        {"R_X86_64_REX_GOTPCRELX",MINIAS_FIXUP_X64_REX_GOTPCRELX,4U},
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
    .init_target_state=x64_init_target_state,
    .capture_site_state=x64_capture_site_state,
    .destroy_target_state=x64_destroy_target_state,
    .parse_target_directive=x64_parse_directive,
    .evaluate_target_if=x64_evaluate_target_if,
    .measure_mcinst=x64_measure,.encode=x64_encode,
    .plan_instruction_relaxation=x64_plan_instruction_relaxation,
    .append_code_alignment=x64_code_align,
    .explicit_alignment_fill_is_code_nop=x64_explicit_alignment_fill_is_code_nop,
    .fixup_relocation_type=x64_reloc,
    .fixup_relocation_type_for_object=x64_reloc_for_object,
    .supports_fixup_for_object=x64_supports_fixup_for_object,
    .resolve_cfi_register=x64_resolve_cfi_register,
    .resolve_cfi_abi=x64_resolve_cfi_abi,
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
    .fold_local_pcrel_relocation_to_section=true,
    .fold_synthetic_local_relocation_to_section=true,
    /* Intermediate FDEs use 4B DWARF32 padding; only the final FDE
     * closes the ELF section at 8B, matching GNU GAS. */
    .cfi_eh_fde_alignment=4U,
    .cfi_eh_final_alignment=8U,
    .cfi_code_align=1U,
    .cfi_data_align=-8,
    .cfi_return_reg=16U,
    .cfi_stack_reg=7U,
    .cfi_initial_cfa_offset=8U,
    .cfi_address_size=8U,
    .cfi_initial_ra_saved=true
};
const MiniAsTargetOps *minias_target_x86_64(void){return &ops;}
