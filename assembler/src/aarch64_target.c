#include "minias_internal.h"
#include "aarch64_fixup_kinds.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define A64_FEATURE_LSE UINT32_C(0x10000000) /* Target-owned feature bit */
#define A64_FEATURE_TLBI_RANGE UINT32_C(0x20000000) /* ARMv8.4 range */
#define A64_FEATURE_CRC UINT32_C(0x40000000) /* ARM CRC extension */
#define A64_FEATURE_CRYPTO UINT32_C(0x80000000) /* ARM AES/crypto extension */
#define A64_FEATURE_MTE UINT32_C(0x08000000) /* ARM memtag extension */

/*
 * First non-RISC-V MiniAS vertical slice.
 *
 * The common assembler owns sections, labels, fixed-size statement layout,
 * symbols and ELF writing. AArch64 owns instructions, immediate syntax,
 * executable NOP alignment and relocation types. Unsupported encodings fail
 * closed rather than pretending to support the entire AArch64 ISA.
 */

enum {
    AARCH64_R_ABS64 = 257U,
    AARCH64_R_ABS32 = 258U,
    AARCH64_R_PREL64 = 260U,
    AARCH64_R_PREL32 = 261U,
    AARCH64_R_LD_PREL_LO19 = 273U,
    AARCH64_R_ADR_PREL_LO21 = 274U,
    AARCH64_R_ADR_PREL_PG_HI21 = 275U,
    AARCH64_R_ADR_GOT_PAGE = 311U,
    AARCH64_R_LD64_GOT_LO12_NC = 312U,
    AARCH64_R_ADD_ABS_LO12_NC = 277U,
    AARCH64_R_LDST8_ABS_LO12_NC = 278U,
    AARCH64_R_LDST16_ABS_LO12_NC = 284U,
    AARCH64_R_LDST32_ABS_LO12_NC = 285U,
    AARCH64_R_LDST64_ABS_LO12_NC = 286U,
    AARCH64_R_TSTBR14 = 279U,
    AARCH64_R_CONDBR19 = 280U,
    AARCH64_R_JUMP26 = 282U,
    AARCH64_R_CALL26 = 283U,
    AARCH64_R_MOVW_UABS_G0 = 263U,
    AARCH64_R_MOVW_UABS_G0_NC = 264U,
    AARCH64_R_MOVW_UABS_G1 = 265U,
    AARCH64_R_MOVW_UABS_G1_NC = 266U,
    AARCH64_R_MOVW_UABS_G2 = 267U,
    AARCH64_R_MOVW_UABS_G2_NC = 268U,
    AARCH64_R_MOVW_UABS_G3 = 269U,
    AARCH64_R_MOVW_SABS_G0 = 270U,
    AARCH64_R_MOVW_SABS_G1 = 271U,
    AARCH64_R_MOVW_SABS_G2 = 272U
};

/* GNU AArch64 NAME .req REGISTER aliases belong exclusively to the target.
 * State is per MiniAs instance, never a static/global register table.
 * Common owns only the opaque backend state lifecycle and hook routing. */
typedef struct A64AliasState {
    unsigned count;
    struct {char alias[40];char canonical[16];} entries[128];
} A64AliasState;

/* ISA capabilities and parser aliases have one Target-owned lifecycle.
 * The alias dictionary is allocated only for source files using `.req`,
 * while every A64 parse site observes the same independent feature state.
 * Common has no ISA feature bits and never interprets this registry. */
typedef struct A64TargetState {
    uint32_t features;
    A64AliasState *aliases;
} A64TargetState;

static uint32_t a64_features(const MiniAs *as) {
    const A64TargetState *st=(const A64TargetState *)as->target_state;
    return st?st->features:0U;
}

static bool a64_init_target_state(MiniAs *as,
                                  const MiniAsAssemblyConfig *config) {
    A64TargetState *st;
    (void)config;
    st=calloc(1U,sizeof(*st));
    if(!st) {minias_set_error(as,"out-of-memory:aarch64-target-state");return false;}
    st->features=0U; /* default armv8-a, extensions via Target directives */
    as->target_state=st;
    return true;
}

static void a64_destroy_target_state(MiniAs *as) {
    A64TargetState *st=(A64TargetState *)as->target_state;
    if(st)free(st->aliases);
    free(st);
    as->target_state=NULL;
}

/* .req resolution is valid only in a *typed register operand* position.
 * In particular `ldr x0,=label` must never rewrite a symbol named label,
 * and branch expressions/relocation modifiers are not register operands.
 * The caller already knows the ISA register grammar; this routine merely
 * substitutes one lexical register atom (including a vector arrangement or
 * lane suffix) without modifying the source or any symbol expression. */
static const char *a64_resolve_register_alias(const A64AliasState *st,
                                               const char *text,
                                               char *buffer,size_t capacity) {
    unsigned i;
    const char *body=text;
    size_t body_len;
    size_t prefix=0U;
    if(!st || !st->count)return text;
    if(*body=='{') {++body;prefix=1U;}
    body_len=strlen(body);
    for(i=0U;i<st->count;++i) {
        size_t n=strlen(st->entries[i].alias);
        const char *tail;
        size_t canonical_len,suffix_len;
        if(n>body_len)continue;
        tail=body+n;
        if(strncmp(body,st->entries[i].alias,n)!=0 ||
           (*tail!='\0' && *tail!='.' && *tail!='[' &&
            *tail!='}' && !isspace((unsigned char)*tail)))continue;
        canonical_len=strlen(st->entries[i].canonical);
        suffix_len=strlen(tail);
        if(prefix+canonical_len+suffix_len>=capacity)return text;
        if(prefix)buffer[0]='{';
        memcpy(buffer+prefix,st->entries[i].canonical,canonical_len);
        memcpy(buffer+prefix+canonical_len,tail,suffix_len+1U);
        return buffer;
    }
    return text;
}

static bool parse_register(const char *text, unsigned *reg, bool *is_64) {
    char *end;
    unsigned long value;
    if ((text[0] != 'x' && text[0] != 'w') ||
        !isdigit((unsigned char)text[1])) return false;
    errno = 0;
    value = strtoul(text + 1, &end, 10);
    if (errno != 0 || *end != '\0' || value > 30U) return false;
    *reg = (unsigned)value;
    *is_64 = text[0] == 'x';
    return true;
}

/* Some GNU aliases (BR/RET/MOV-constant) accept Xn/Wn but intentionally
 * reject SP/ZR. Preserve that narrower grammar when resolving aliases. */
static bool a64_parse_named_gpr(const A64AliasState *aliases,const char *text,
                                 unsigned *reg,bool *wide) {
    char resolved[96];
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    return parse_register(text,reg,wide);
}

/* ISA family dispatch must inspect the *register operand*, never assume
 * its source spelling starts with x/v/d: an alias is a different token. */
static char a64_register_prefix(const A64AliasState *aliases,const char *text) {
    char atom[96],expanded[96];
    size_t length;
    while(isspace((unsigned char)*text))++text;
    length=strcspn(text,", \t\n\r");
    if(length==0U || length>=sizeof(atom))return *text;
    memcpy(atom,text,length);atom[length]='\0';
    return *a64_resolve_register_alias(aliases,atom,expanded,sizeof(expanded));
}

static bool parse_imm16(const char *text, unsigned *result) {
    uint64_t value;
    /* A64 GNU preprocessed MOVZ/MOVK literals may be arithmetic, e.g.
     * (((1)<<15)|0x1234). Use Common's symbol-free unsigned expression
     * evaluator; reject >16-bit, unknown symbols and negative values. */
    if (*text == '#') ++text;
    if(!minias_eval_u64_expression(text,&value) || value>65535U)
        return false;
    *result=(unsigned)value;
    return true;
}

/* Seven GNU MOVZ/MOVK/MOVN ELF MOVW groups. The relocation
 * determines HW=0..3. Never fold a symbolic address to numeric imm16
 * or delegate AArch64 field placement to Common ELF serialization. */
static bool a64_movw_relocation(const char *text,MiniAsSymbolExpr *expr,
                               MiniAsFixupKind *kind,unsigned *group,
                               bool *constant,uint64_t *bits,
                               bool *signed_negative) {
    static const struct {const char *name;MiniAsFixupKind fixup;
                         unsigned hw;} forms[]={
        {"abs_g0",MINIAS_FIXUP_A64_MOVW_G0,0U},
        {"abs_g0_nc",MINIAS_FIXUP_A64_MOVW_G0_NC,0U},
        {"abs_g1",MINIAS_FIXUP_A64_MOVW_G1,1U},
        {"abs_g1_nc",MINIAS_FIXUP_A64_MOVW_G1_NC,1U},
        {"abs_g2",MINIAS_FIXUP_A64_MOVW_G2,2U},
        {"abs_g2_nc",MINIAS_FIXUP_A64_MOVW_G2_NC,2U},
        {"abs_g3",MINIAS_FIXUP_A64_MOVW_G3,3U},
        {"abs_g0_s",MINIAS_FIXUP_A64_MOVW_SG0,0U},
        {"abs_g1_s",MINIAS_FIXUP_A64_MOVW_SG1,1U},
        {"abs_g2_s",MINIAS_FIXUP_A64_MOVW_SG2,2U}
    };
    unsigned i;
    if(*text=='#')++text;
    if(*text++!=':')return false;
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i) {
        size_t n=strlen(forms[i].name);
        if(strncmp(text,forms[i].name,n)!=0 || text[n]!=':')continue;
        const char *value=text+n+1U;
        int64_t immediate=0;
        if(minias_parse_symbol_addend(value,expr)) {
            *constant=false;*signed_negative=false;
        }else {
            /* GNU accepts the full unsigned 64-bit range in :abs_g3:.
             * The signed-only evaluator rejected 0x8000... and Linux
             * MOVW macro constants despite their valid 16-bit HW lanes. */
            bool signed_constant=minias_eval_i64_expression(value,&immediate);
            uint64_t value_bits=(uint64_t)immediate;
            unsigned width=(forms[i].hw+1U)*16U;
            bool checked_signed=forms[i].name[strlen(forms[i].name)-1U]=='s';
            bool unchecked=strstr(forms[i].name,"_nc")!=NULL;
            if(!signed_constant &&
               !minias_eval_u64_expression(value,&value_bits))return false;
            if(checked_signed) {
                /* A signed relocation needs a true signed value. Avoid
                 * signed INT64_MIN negation when HW=3 (full 64 bits). */
                if(!signed_constant)return false;
                if(width<64U) {
                    int64_t bound=INT64_C(1)<<(width-1U);
                    if(immediate< -bound || immediate>=bound)return false;
                }
            }else if(!unchecked && width<64U &&
                     value_bits>=(UINT64_C(1)<<width))return false;
            /* Keep GNU MOVN signed negatives distinct from the upper
             * unsigned half: both have the same 64-bit storage bits. */
            *constant=true;
            *bits=value_bits;
            *signed_negative=signed_constant && immediate<0 && checked_signed;
        }
        *kind=forms[i].fixup;*group=forms[i].hw;
        return true;
    }
    return false;
}

static bool split_two(const char *args, char *first, size_t cap1,
                      char *second, size_t cap2) {
    char *out[2]={first,second};
    const size_t capacity[2]={cap1,cap2};
    unsigned count=0U;
    return minias_split_mc_operands(args,out,capacity,2U,&count) &&
           count==2U;
}

/* AArch64 is fixed 4B. Measurement consumes the parsed MCInst just as
 * the RISC-V variable-length backend does; no duplicate mnemonic allowlist
 * or operand parsing is necessary before the instruction fragment exists. */
static bool aarch64_measure_mcinst(MiniAs *as, const MiniAsStmt *stmt,
                                   uint32_t *size) {
    if (stmt->mcinst == NULL) {
        minias_set_error(as, "missing-aarch64-mcinst:line=%zu", stmt->line);
        return false;
    }
    *size = 4U;
    return true;
}

static bool append_word(MiniAs *as, int section, uint32_t word) {
    unsigned char bytes[4];
    unsigned i;
    for (i = 0U; i != 4U; ++i)
        bytes[i] = (unsigned char)(word >> (i * 8U));
    return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));
}

/* First fully typed backend: parse GNU A64 operand syntax once, when the
 * instruction fragment is recorded. Encoding consumes only MCInst fields,
 * never re-splits source operand strings or reparses reloc expressions. */
enum {
    A64_MC_NOP = 1,
    A64_MC_HINT,
    A64_MC_CSEL_ALIAS,
    A64_MC_COND_SELECT,
    A64_MC_COND_COMPARE,
    A64_MC_SHIFT,
    A64_MC_EXTR,
    A64_MC_ARITH_ALIAS,
    A64_MC_MRS,
    A64_MC_MSR,
    A64_MC_SYS_OP,
    A64_MC_BARRIER,
    A64_MC_RET,
    A64_MC_SVC,
    A64_MC_BRK,
    A64_MC_B,
    A64_MC_BL,
    A64_MC_CBZ,
    A64_MC_CBNZ,
    A64_MC_BCOND,
    A64_MC_TBZ,
    A64_MC_TBNZ,
    A64_MC_WIDE_IMM,
    A64_MC_BR,
    A64_MC_BLR,
    A64_MC_ADR,
    A64_MC_ADRP,
    A64_MC_ADD_LO12,
    A64_MC_LDST_LO12,
    A64_MC_LDST_GOT_LO12,
    A64_MC_MOV_REG,
    A64_MC_ADD_REG,
    A64_MC_EXT_ALU,
    A64_MC_SUB_REG,
    A64_MC_ADDS_REG,
    A64_MC_SUBS_REG,
    A64_MC_AND_REG,
    A64_MC_ORR_REG,
    A64_MC_EOR_REG,
    A64_MC_ADD_IMM,
    A64_MC_SUB_IMM,
    A64_MC_ADDS_IMM,
    A64_MC_SUBS_IMM,
    A64_MC_CMP_REG,
    A64_MC_CMP_IMM,
    A64_MC_CMN_REG,
    A64_MC_CMN_IMM,
    A64_MC_LDR_LITERAL,
    A64_MC_LDST_UNSCALED,
    A64_MC_LDST_SCALED,
    A64_MC_LDST_UNPRIV,
    A64_MC_LDST_INDEX,
    A64_MC_STP,
    A64_MC_LDP,
    A64_MC_LDPSW,
    A64_MC_BITMASK_IMM,
    A64_MC_BITFIELD,
    A64_MC_TST_REG,
    A64_MC_BIC_REG,
    A64_MC_EXTEND_W,
    A64_MC_REV,
    A64_MC_ACQUIRE_RELEASE,
    A64_MC_MVN_REG,
    A64_MC_SYSTEM_IMM,
    A64_MC_LSE,
    A64_MC_PRFM,
    A64_MC_PRFM_REG,
    A64_MC_EXCLUSIVE,
    A64_MC_SIMD_SINGLE,
    A64_MC_SIMD_MOVE,
    A64_MC_SIMD_CRYPTO,
    A64_MC_SIMD_LOGIC,
    A64_MC_SIMD_DUP,
    A64_MC_SIMD_REV32,
    A64_MC_LSE_PAIR,
    A64_MC_MTE_STGM,
    A64_MC_SIMD_EXT,
    A64_MC_SIMD_IMM,
    A64_MC_EXCLUSIVE_PAIR,
    A64_MC_MTE_STZ2G
};

/* GNU AArch64 operands split on TOP-LEVEL commas, not commas within
 * [xN,#offset]. Unlike copying a generic tokenizer into every target,
 * this helper is strictly A64 addressing grammar. */
static bool split_operands_sized(const char *args, char *out[4],
                                 const size_t capacity[4], unsigned *n) {
    return minias_split_mc_operands(args,out,capacity,4U,n);
}

/* All normal A64 operands still use the fast, bounded stack tokenizer.
 * The GNU MOVW macro may contain a generated expression longer than 384
 * bytes. It calls the SAME sized splitter with transient dynamic storage,
 * not a second source-text parser or an ISA-specific expression rewrite. */
static bool split_operands(const char *args, char out[4][384], unsigned *n) {
    char *slots[4]={out[0],out[1],out[2],out[3]};
    const size_t caps[4]={384U,384U,384U,384U};
    return split_operands_sized(args,slots,caps,n);
}

/* SIMD/FP registers carry explicit Target-bank metadata in the MC envelope.
 * Alias substitution is performed here, after the operand position has been
 * identified as a register; arbitrary source symbols remain untouched. */
static bool parse_a64_q_reg(const A64AliasState *aliases,const char *text,unsigned *number) {
    char resolved[96];
    char *end;
    unsigned long n;
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    if(text[0]!='q' || !isdigit((unsigned char)text[1]))return false;
    errno=0;n=strtoul(text+1U,&end,10);
    if(errno || *end!='\0' || n>31UL)return false;
    *number=(unsigned)n;return true;
}

/* One vector register arrangement for LD1/ST1, no multi-vector lists
 * or lane load pseudo-expansions accepted until independently proven. */
static bool parse_a64_single_v(const A64AliasState *aliases,const char *text,unsigned *reg,
                               uint32_t *arrange) {
    char resolved[96];
    char copy[96],*p,*end;
    unsigned long n;
    size_t len;
    static const struct {const char *s;uint32_t flags;} forms[]={
        {".16b",UINT32_C(0x40000000)},
        {".8b",UINT32_C(0x00000000)},
        {".8h",UINT32_C(0x40000400)},
        {".4h",UINT32_C(0x00000400)},
        {".4s",UINT32_C(0x40000800)},
        {".2s",UINT32_C(0x00000800)},
        {".2d",UINT32_C(0x40000c00)},
        {".1d",UINT32_C(0x00000c00)}
    };
    unsigned i;
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    len=strlen(text);
    if(len<6U || len>=sizeof(copy) ||
       text[0]!='{' || text[len-1U]!='}')return false;
    memcpy(copy,text+1U,len-2U);copy[len-2U]='\0';
    p=minias_trim(copy);
    if(*p!='v' || !isdigit((unsigned char)p[1]))return false;
    errno=0;n=strtoul(p+1U,&end,10);
    if(errno || n>31UL)return false;
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i) {
        if(!strcmp(end,forms[i].s)) {
            *reg=(unsigned)n;*arrange=forms[i].flags;
            return true;
        }
    }
    return false;
}

/* GNU vector MOV aliases ORR of the same vector register.
 * Reuse the existing vector arrangement recognizer without loosening
 * LD1/ST1's brace-only register-list grammar. */
/* SHA/PMULL 128-bit polynomial destination is .1Q, distinct from
 * ordinary .2D views and from scalar Qn operands. */
static bool a64_vector_1q(const A64AliasState *aliases,const char *text,unsigned *reg) {
    char resolved[96];
    char *end;
    unsigned long value;
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    if(text[0]!='v' || !isdigit((unsigned char)text[1]))return false;
    errno=0;value=strtoul(text+1U,&end,10);
    if(errno || value>31UL || strcmp(end,".1q"))return false;
    *reg=(unsigned)value;return true;
}

static bool a64_vector_view(const A64AliasState *aliases,const char *text,unsigned *reg,
                            uint32_t *arrange) {
    char wrapped[96];
    size_t n=strlen(text);
    if(n<4U || n+3U>sizeof(wrapped))return false;
    wrapped[0]='{';
    memcpy(wrapped+1U,text,n);
    wrapped[n+1U]='}';wrapped[n+2U]='\0';
    return parse_a64_single_v(aliases,wrapped,reg,arrange);
}

/* GNU .req may bind scalar or vector bank registers, not only GPRs.
 * Keep the alias state per assembler target instance and resolve at
 * statement parse; unknown register classes remain rejected. */
static bool a64_req_vector_bank(const char *text,unsigned *reg) {
    char *end;
    unsigned long num;
    if(!text[0] || !strchr("vqdsbh",text[0]) ||
       !isdigit((unsigned char)text[1]))
        return false;
    errno=0;num=strtoul(text+1U,&end,10);
    if(errno || *end!='\0' || num>31UL)return false;
    *reg=(unsigned)num;return true;
}

/* AArch64 structure LD1/ST1 register lists are either contiguous
 * vM.T-vN.T ranges or comma-separated contiguous lists, up to four.
 * Reuse the single-vector arrangement decoder for EACH member; no
 * parallel vector ISA parser and no implicit register skipping. */
static bool a64_structure_vlist(const A64AliasState *aliases,const char *text,unsigned *first,
                                uint32_t *arrange,unsigned *number) {
    char copy[96],*body,*p,*dash,*comma;
    unsigned start,last,member=0U;
    uint32_t flags,each;
    size_t len=strlen(text);
    if(len<6U || len>=sizeof(copy) ||
       text[0]!='{' || text[len-1U]!='}')return false;
    memcpy(copy,text+1U,len-2U);copy[len-2U]='\0';
    body=minias_trim(copy);
    dash=strchr(body,'-');
    if(dash) {
        if(strchr(body,',') || strchr(dash+1U,'-'))return false;
        *dash='\0';
        if(!a64_vector_view(aliases,minias_trim(body),&start,&flags) ||
           !a64_vector_view(aliases,minias_trim(dash+1U),&last,&each) ||
           flags!=each)return false;
        member=(last+32U-start)%32U+1U;
        if(member<2U || member>4U)return false;
    }else {
        p=body;start=0U;flags=0U;
        do {
            comma=strchr(p,',');
            if(comma)*comma='\0';
            if(!a64_vector_view(aliases,minias_trim(p),&last,&each) ||
               member==4U)return false;
            if(member==0U){start=last;flags=each;}
            else if(each!=flags || last!=(start+member)%32U)
                return false;
            ++member;
            if(!comma)break;
            p=comma+1U;
        }while(*p);
        if(comma && !*p)return false;
    }
    *first=start;*arrange=flags;*number=member;
    return true;
}

/* SYS opcode fields are exactly op1,CRn,CRm,op2,Rt.
 * Unlike ordinary A64 operands this form has FIVE fields; parse here
 * rather than raising Common's global 4-operand limit or duplicating
 * the entire instruction tokenizer. */
static bool parse_a64_reg(const A64AliasState *aliases,const char *text, unsigned *number, bool *is64,
                          bool *is_sp) {
    char resolved[96];
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    *is_sp = false;
    if (strcmp(text, "sp") == 0 || strcmp(text, "wsp") == 0) {
        *number = 31U;
        *is64 = text[0] == 's';
        *is_sp = true;
        return true;
    }
    if (strcmp(text, "xzr") == 0 || strcmp(text, "wzr") == 0) {
        *number = 31U;
        *is64 = text[0] == 'x';
        return true;
    }
    return parse_register(text, number, is64);
}

static bool parse_signed_number(const char *text, int64_t *out) {
    /* The existing Common constant-expression parser already implements
     * GNU nested arithmetic, shifts and bitwise operators. Do not create
     * a second numeric parser for AArch64 immediates and memory offsets. */
    if(*text=='#')++text;
    return *text!='\0' && minias_eval_i64_expression(text,out);
}

static bool a64_sys_fields(const A64AliasState *aliases,const char *args,uint32_t *word,unsigned *rt) {
    char text[256],tok[5][80];char *start,*end;
    int64_t numbers[4];unsigned i;
    bool wide,sp;
    size_t n=strlen(args);
    if(n>=sizeof(text))return false;
    memcpy(text,args,n+1U);
    start=text;
    for(i=0U;i<5U;++i) {
        size_t len;
        end=strchr(start,',');
        if((i<4U && !end) || (i==4U && end))return false;
        if(end)*end='\0';
        start=minias_trim(start);len=strlen(start);
        if(len==0U || len>=sizeof(tok[0]))return false;
        memcpy(tok[i],start,len+1U);
        if(i==4U)break;
        start=end+1U;
    }
    for(i=0U;i<4U;++i) {
        const char *v=tok[i];
        if(i==1U || i==2U){
            if(*v!='c')return false;
            ++v;
        }
        if(!parse_signed_number(v,&numbers[i]) || numbers[i]<0 ||
           numbers[i]>(i==0U?7:i==3U?7:15))return false;
    }
    if(!parse_a64_reg(aliases,tok[4],rt,&wide,&sp) || !wide || sp)return false;
    *word=UINT32_C(0xd5080000)|
          ((uint32_t)numbers[0]<<16U)|
          ((uint32_t)numbers[1]<<12U)|
          ((uint32_t)numbers[2]<<8U)|
          ((uint32_t)numbers[3]<<5U);
    return true;
}

/* The MC envelope carries register identity without requiring Common to
 * enumerate AArch64 register banks. A zero bank is never produced by an
 * explicitly bound register; the target-private discriminator remains
 * available to later ISA legalization/encoding without guessing from bits. */
typedef enum A64RegisterBank {
    A64_BANK_GPR = 1,
    A64_BANK_SIMD_VECTOR,
    A64_BANK_FP_SCALAR,
    A64_BANK_SIMD_Q
} A64RegisterBank;

static void put_reg_bank(MiniAsMCOperand *dst, unsigned number, bool reg64,
                         A64RegisterBank bank) {
    dst->kind = MINIAS_MC_REGISTER;
    dst->reg = number;
    dst->register_64 = reg64;
    dst->reg_class=bank==A64_BANK_GPR?MINIAS_MC_REG_GPR:
                   bank==A64_BANK_FP_SCALAR?MINIAS_MC_REG_FPR:
                   MINIAS_MC_REG_VECTOR;
    dst->target_reg_bank=(uint16_t)bank;
}

static void put_reg(MiniAsMCOperand *dst, unsigned number, bool reg64) {
    put_reg_bank(dst,number,reg64,A64_BANK_GPR);
}

static void put_vector(MiniAsMCOperand *dst, unsigned number) {
    put_reg_bank(dst,number,true,A64_BANK_SIMD_VECTOR);
}

static void put_fp_scalar(MiniAsMCOperand *dst, unsigned number, bool wide) {
    put_reg_bank(dst,number,wide,A64_BANK_FP_SCALAR);
}

static void put_q(MiniAsMCOperand *dst, unsigned number) {
    put_reg_bank(dst,number,true,A64_BANK_SIMD_Q);
}

static void put_immediate(MiniAsMCOperand *dst, int64_t value) {
    dst->kind = MINIAS_MC_IMMEDIATE;
    dst->immediate = (uint64_t)value;
}

/* AArch64 addressing is a semantic operand, not an accidental grouping of
 * registers and already-scaled machine immediates.  The shared MC envelope
 * owns the allocation and never interprets this target-private structure.
 * Symbol references are inline NameId tokens, not ELF symbol IDs. */
typedef enum A64OffsetKind {
    A64_OFFSET_CONSTANT,
    A64_OFFSET_DIFFERENCE,
    A64_OFFSET_LO12,
    A64_OFFSET_GOT_LO12
} A64OffsetKind;
typedef struct A64OffsetExpr {
    A64OffsetKind kind;
    int64_t constant;
    MiniAsSymbolDifferenceRef difference;
    MiniAsSymbolRef symbol;
} A64OffsetExpr;
typedef enum A64AddressMode {
    A64_ADDRESS_OFFSET,
    A64_ADDRESS_PRE_INDEX,
    A64_ADDRESS_POST_INDEX,
    A64_ADDRESS_REGISTER_INDEX
} A64AddressMode;
typedef struct A64Memory {
    unsigned base;
    A64AddressMode mode;
    A64OffsetExpr offset;
    unsigned access_bytes;
    unsigned index;
    unsigned extend_option;
    bool scaled_index;
    bool post_index_register;
    unsigned post_register;
} A64Memory;

static bool a64_parse_deferred_difference(MiniAs *as,const char *source,
                                          MiniAsSymbolDifference *difference);

static bool a64_parse_address_offset(MiniAs *as,const char *source,
                                      A64OffsetExpr *result) {
    MiniAsSymbolDifference difference;
    int64_t value;
    while(isspace((unsigned char)*source))++source;
    if(*source=='#')++source;
    while(isspace((unsigned char)*source))++source;
    memset(result,0,sizeof(*result));
    if(minias_evaluate_absolute_operand(as,source,&value)) {
        result->kind=A64_OFFSET_CONSTANT;
        result->constant=value;
        return true;
    }
    if(!a64_parse_deferred_difference(as,source,&difference))return false;
    if(!minias_bind_symbol_difference_ref(as,&difference,&result->difference))
        return false;
    result->kind=A64_OFFSET_DIFFERENCE;
    return true;
}

static bool a64_resolve_address_offset(MiniAs *as,const A64Memory *memory,
                                       int64_t *value) {
    if(memory->offset.kind==A64_OFFSET_CONSTANT) {
        *value=memory->offset.constant;
        return true;
    }
    if(memory->offset.kind==A64_OFFSET_DIFFERENCE)
        return minias_resolve_symbol_difference_ref(
                   as,&memory->offset.difference,value);
    return false;
}

static bool a64_attach_memory(MiniAs *as,MiniAsMCInst *inst,unsigned slot,
                              const A64Memory *parsed) {
    A64Memory *owned=malloc(sizeof(*owned));
    if(!owned) {
        minias_set_error(as,"out-of-memory:aarch64-memory-operand");
        return false;
    }
    *owned=*parsed;
    inst->operands[slot].kind=MINIAS_MC_MEMORY;
    inst->operands[slot].memory=owned;
    inst->dispose_memory=free;
    return true;
}

static bool a64_attach_difference(MiniAs *as,MiniAsMCInst *inst,
                                  unsigned slot,
                                  const MiniAsSymbolDifference *difference) {
    MiniAsSymbolDifferenceRef *owned=malloc(sizeof(*owned));
    if(!owned) {
        minias_set_error(as,"out-of-memory:aarch64-difference-operand");
        return false;
    }
    if(!minias_bind_symbol_difference_ref(as,difference,owned)) {
        free(owned);return false;
    }
    inst->operands[slot].kind=MINIAS_MC_BOUND_SYMBOL_DIFFERENCE;
    inst->operands[slot].bound_difference=owned;
    return true;
}

static bool a64_resolve_immediate(MiniAs *as,const MiniAsMCOperand *operand,
                                  int64_t *value) {
    if(operand->kind==MINIAS_MC_IMMEDIATE) {
        *value=(int64_t)operand->immediate;
        return true;
    }
    if(operand->kind==MINIAS_MC_BOUND_SYMBOL_DIFFERENCE)
        return minias_resolve_symbol_difference_ref(as,
                   operand->bound_difference,value);
    return false;
}

static const A64Memory *a64_memory_at(const MiniAsMCInst *inst,unsigned slot) {
    return slot<inst->count && inst->operands[slot].kind==MINIAS_MC_MEMORY?
           (const A64Memory *)inst->operands[slot].memory:NULL;
}

/* The base and bracket grammar of [Xn, offset] is common to literal,
 * symbolic LO12 and ordinary numeric memory operands.  Keep it target-local
 * and parse the offset's *kind* only in the consuming instruction family;
 * a general-number parser must not reject a valid :lo12: relocation. */
static bool a64_memory_components(const A64AliasState *aliases,const char *text,unsigned *base,
                                  char offset[96],unsigned *mode) {
    char inside[96], *comma, *regtext;
    size_t size = strlen(text);
    bool is64, issp;
    if (size < 3U || text[0] != '[') return false;
    *mode = 0U;
    if (text[size - 1U] == '!') {
        *mode = 1U;
        --size;
    }
    if (text[size - 1U] != ']' || size - 2U >= sizeof(inside)) return false;
    memcpy(inside, text + 1, size - 2U);
    inside[size - 2U] = '\0';
    comma = strchr(inside, ',');
    if (comma) *comma++ = '\0';
    regtext = inside;
    while (isspace((unsigned char)*regtext)) ++regtext;
    size = strlen(regtext);
    while (size && isspace((unsigned char)regtext[size - 1U]))
        regtext[--size] = '\0';
    if (!parse_a64_reg(aliases,regtext, base, &is64, &issp) || !is64)
        return false;
    offset[0] = '\0';
    if (comma) {
        while (isspace((unsigned char)*comma)) ++comma;
        size = strlen(comma);
        while (size && isspace((unsigned char)comma[size - 1U]))
            comma[--size] = '\0';
        memcpy(offset,comma,size+1U);
    }
    return true;
}

/* Bracket and post-index spelling is decoded once, before the consumer
 * selects an ISA transfer form. An ABS .set is frozen at this lexical site;
 * a forward same-section difference instead survives until final layout. */
static bool a64_parse_memory(MiniAs *as,const A64AliasState *aliases,
                             const char *bracket,const char *post,
                             A64Memory *memory) {
    char displacement[96];
    unsigned pre_mode;
    unsigned postreg;
    bool wide,sp;
    memset(memory,0,sizeof(*memory));
    if(!a64_memory_components(aliases,bracket,&memory->base,
                              displacement,&pre_mode))return false;
    memory->mode=pre_mode?A64_ADDRESS_PRE_INDEX:A64_ADDRESS_OFFSET;
    if(post!=NULL) {
        if(pre_mode || *displacement)return false;
        memory->mode=A64_ADDRESS_POST_INDEX;
        if(parse_a64_reg(aliases,post,&postreg,&wide,&sp) && wide && !sp) {
            memory->post_index_register=true;
            memory->post_register=postreg;
            return true;
        }
        return a64_parse_address_offset(as,post,&memory->offset);
    }
    return !*displacement ||
           a64_parse_address_offset(as,displacement,&memory->offset);
}

static bool a64_is_plain_base(const A64Memory *memory) {
    return memory->mode==A64_ADDRESS_OFFSET &&
           memory->offset.kind==A64_OFFSET_CONSTANT &&
           memory->offset.constant==0;
}

/* Atomic 128-bit CASP{A,L,AL} GNU operand sequence has 4 GPRs
 * plus one memory register. The typed MCInst encodes only the leading
 * register of each consecutive even pair; validate both members here.
 * Reuse the EXISTING four-operand A64 tokenizer after isolating [Rn].
 */
static bool a64_casp_fields(MiniAs *as,const A64AliasState *aliases,const char *args,unsigned *rs,unsigned *rt,
                             A64Memory *memory,bool *wide) {
    char buffer[384],tokens[4][384],*last;
    unsigned n,r1,r3;
    bool w0,w1,w2,w3,sp;
    size_t len=strlen(args);
    if(len>=sizeof(buffer))return false;
    memcpy(buffer,args,len+1U);
    last=strrchr(buffer,',');
    if(!last)return false;
    *last++='\0';last=minias_trim(last);
    if(!split_operands(buffer,tokens,&n) || n!=4U ||
       !parse_a64_reg(aliases,tokens[0],rs,&w0,&sp) || sp ||
       !parse_a64_reg(aliases,tokens[1],&r1,&w1,&sp) || sp ||
       !parse_a64_reg(aliases,tokens[2],rt,&w2,&sp) || sp ||
       !parse_a64_reg(aliases,tokens[3],&r3,&w3,&sp) || sp ||
       !(w0==w1 && w0==w2 && w0==w3) ||
       (*rs&1U) || (*rt&1U) ||
       *rs>=30U || *rt>=30U ||
       r1!=*rs+1U || r3!=*rt+1U ||
       !a64_parse_memory(as,aliases,last,NULL,memory) ||
       !a64_is_plain_base(memory))return false;
    *wide=w0;return true;
}

/* Element-indexed INS Vd.T[index], Rn.
 * The lane index is the architectural imm5 encoded in bits[20:16].
 * No independent FP register representation is needed in Common IR. */
static bool a64_ins_lane(const A64AliasState *aliases,const char *text,unsigned *vd,uint32_t *imm5,
                          bool *x_source) {
    char resolved[96];
    char *end;unsigned long num,index;
    char elem;
    unsigned bits;
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    if(text[0]!='v' || !isdigit((unsigned char)text[1]))return false;
    errno=0;num=strtoul(text+1U,&end,10);
    if(errno || num>31UL || *end!='.' || !end[1] ||
       end[2]!='[')return false;
    elem=end[1];
    bits=elem=='b'?0U:elem=='h'?1U:elem=='s'?2U:
         elem=='d'?3U:4U;
    if(bits==4U || !isdigit((unsigned char)end[3]))return false;
    errno=0;index=strtoul(end+3U,&end,10);
    if(errno || *end!=']' || end[1]!='\0' ||
       index>=(16UL>>bits))return false;
    *vd=(unsigned)num;
    *imm5=((uint32_t)index<<(bits+1U))|(1U<<bits);
    *x_source=bits==3U;
    return true;
}

/* One target FP scalar register-bank recognizer for S/D GPR transfers,
 * also reused by earlier D-lane MOV. No Core register-bank ISA leak. */
static bool a64_scalar_fp_reg(const A64AliasState *aliases,const char *text,unsigned *out,
                              bool *wide) {
    char resolved[96];
    char *end;unsigned long v;
    text=a64_resolve_register_alias(aliases,text,resolved,sizeof(resolved));
    if((text[0]!='s' && text[0]!='d') ||
       !isdigit((unsigned char)text[1]))return false;
    errno=0;v=strtoul(text+1U,&end,10);
    if(errno || *end!='\0' || v>31UL)return false;
    *out=(unsigned)v;*wide=text[0]=='d';return true;
}
static bool a64_scalar_fp_d(const A64AliasState *aliases,const char *text,unsigned *out) {
    bool wide;
    return a64_scalar_fp_reg(aliases,text,out,&wide) && wide;
}
/* A64 register-indexed memory with typed base/index, extension and scale.
 * This does not change shared Section, MCInst, Fixup or ELF ownership. */
static bool a64_indexed_memory(const A64AliasState *aliases,const char *text,unsigned *base,
                                unsigned *index,unsigned *option,
                                bool *scaled,unsigned amount_shift) {
    char inside[96],tokens[4][384];
    unsigned n;
    size_t length=strlen(text),k;
    bool wide,sp;
    const char *mode;
    int64_t amount=0;
    if(length<5U || text[0]!='[' || text[length-1U]!=']' ||
       length-2U>=sizeof(inside))return false;
    memcpy(inside,text+1U,length-2U);
    inside[length-2U]='\0';
    if(!split_operands(inside,tokens,&n) || (n!=2U && n!=3U) ||
       !parse_a64_reg(aliases,tokens[0],base,&wide,&sp) || !wide ||
       !parse_a64_reg(aliases,tokens[1],index,&wide,&sp) || sp)return false;
    *option=3U;*scaled=false;
    if(n==2U)return wide;
    mode=tokens[2];
    if(!strncmp(mode,"uxtw",4U) &&
       (!mode[4] || isspace((unsigned char)mode[4]))) {
        if(wide)return false;
        *option=2U;k=4U;
    }else if(!strncmp(mode,"sxtw",4U) &&
             (!mode[4] || isspace((unsigned char)mode[4]))) {
        if(wide)return false;
        *option=6U;k=4U;
    }else if(!strncmp(mode,"lsl",3U) &&
             (!mode[3] || isspace((unsigned char)mode[3]))) {
        if(!wide)return false;
        *option=3U;k=3U;
    }else if(!strncmp(mode,"sxtx",4U) &&
             (!mode[4] || isspace((unsigned char)mode[4]))) {
        if(!wide)return false;
        *option=7U;k=4U;
    }else return false;
    mode+=k;
    while(isspace((unsigned char)*mode))++mode;
    if(*mode && !parse_signed_number(mode,&amount))return false;
    if(amount!=0 && amount!=(int64_t)amount_shift)return false;
    *scaled=amount!=0;
    return true;
}

static bool a64_parse_indexed_address(const A64AliasState *aliases,
                                      const char *text,unsigned access_bytes,
                                      A64Memory *memory) {
    unsigned shift=access_bytes==1U?0U:access_bytes==2U?1U:
                   access_bytes==4U?2U:3U;
    memset(memory,0,sizeof(*memory));
    if(!a64_indexed_memory(aliases,text,&memory->base,&memory->index,
                           &memory->extend_option,&memory->scaled_index,
                           shift))return false;
    memory->mode=A64_ADDRESS_REGISTER_INDEX;
    memory->access_bytes=access_bytes;
    return true;
}

/* Typed A64 shifted register operand bits; no Common grammar fork. */
static bool a64_shift_fields(const char *text,bool wide,bool allow_ror,
                             uint32_t *result) {
    static const char *const ops[]={"lsl","lsr","asr","ror"};
    unsigned i;
    int64_t value;
    const char *arg;
    for(i=0U;i<4U;++i) {
        size_t n=strlen(ops[i]);
        if(strncmp(text,ops[i],n) ||
           (text[n]!='#' && !isspace((unsigned char)text[n])))
            continue;
        if(i==3U && !allow_ror)return false;
        arg=text+n;
        while(isspace((unsigned char)*arg))++arg;
        if(!parse_signed_number(arg,&value) || value<0 ||
           value>=(wide?64:32))return false;
        *result=(i<<22U)|((uint32_t)value<<10U);
        return true;
    }
    return false;
}
/* A64 extended register option: UXT{B,H,W,X}, SXT{B,H,W,X},
 * with 0..4 bit scaling. LSL is an UXTX alias only for SP-sensitive
 * add/sub instruction forms; plain shifted ALU stays the base encoding. */
static bool a64_extended_fields(const char *text,bool indexwide,
                                 bool sp_mode,uint32_t *bits) {
    static const char *const mode[]={
        "uxtb","uxth","uxtw","uxtx","sxtb","sxth","sxtw","sxtx"
    };
    unsigned i,option=0U;
    const char *arg=NULL;
    int64_t shift=0;
    if(sp_mode && indexwide && !strncmp(text,"lsl",3U) &&
       (!text[3] || isspace((unsigned char)text[3]))) {
        option=3U;arg=text+3U;
    }else {
        for(i=0U;i<8U;++i) {
            if(!strncmp(text,mode[i],4U) &&
               (!text[4] || isspace((unsigned char)text[4]))) {
                option=i;arg=text+4U;break;
            }
        }
        if(!arg)return false;
    }
    if(indexwide!=(option==3U || option==7U))return false;
    while(isspace((unsigned char)*arg))++arg;
    if(*arg && !parse_signed_number(arg,&shift))return false;
    if(shift<0 || shift>4)return false;
    *bits=(option<<13U)|((uint32_t)shift<<10U);
    return true;
}
/* Enumerate exact A64 rotated/replicated logical-immediate bitfields:
 * one Target grammar supports AND/ORR/EOR/ANDS/TST, not Core patches. */
static bool a64_mask_bits(uint64_t value,bool wide,uint32_t *enc) {
    unsigned width=wide?64U:32U,e,n,r;
    if(!wide && (value>>32U))return false;
    for(e=2U;e<=width;e*=2U) {
        uint64_t mask=e==64U?UINT64_MAX:(UINT64_C(1)<<e)-1U;
        for(n=1U;n<e;++n) {
            uint64_t run=(UINT64_C(1)<<n)-1U;
            for(r=0U;r<e;++r) {
                uint64_t part=r?((run>>r)|(run<<(e-r)))&mask:run;
                uint64_t full=0U;unsigned k;
                for(k=0U;k<width;k+=e)full|=part<<k;
                if(full==value) {
                    unsigned imms=((0U-2U*e)|(n-1U))&63U;
                    *enc=((e==64U?1U:0U)<<12U)|(r<<6U)|imms;
                    return true;
                }
            }
        }
    }
    return false;
}

/* One AArch64-owned description of the scalar load/store instruction bank.
 * The source spellings (scaled, forced-unscaled and unprivileged) are distinct
 * grammar variants of the SAME machine-width and register constraints.  Keep
 * their opcode bases in one table instead of three diverging decoders.  All
 * four addressing encodings share these precise A64 bitfield deltas:
 *   unsigned imm12 -> signed imm9       : -0x01000000
 *   unsigned imm12 -> register indexed  : -0x00dff800
 *   unsigned imm12 -> unprivileged imm9  : -0x00fff800
 * These are target-local ISA properties, not Common MC operand semantics. */
enum {
    A64_SCALAR_W=1U,
    A64_SCALAR_X=2U,
    A64_SCALAR_DIRECT=0U,
    A64_SCALAR_FORCE_UNSCALED=1U,
    A64_SCALAR_UNPRIVILEGED=2U
};
enum { A64_PAIR_NONTEMPORAL=1U };
typedef struct A64ScalarMemoryForm {
    unsigned bytes; /* 0: GPR destination determines transfer width */
    unsigned allowed_registers;
    uint32_t scaled_w;
    uint32_t scaled_x;
    bool supports_lo12_relocation;
} A64ScalarMemoryForm;

static const A64ScalarMemoryForm a64_scalar_memory_forms[]={
    {0U,A64_SCALAR_W|A64_SCALAR_X,
        UINT32_C(0xb9400000),UINT32_C(0xf9400000),true},
    {0U,A64_SCALAR_W|A64_SCALAR_X,
        UINT32_C(0xb9000000),UINT32_C(0xf9000000),true},
    {1U,A64_SCALAR_W,
        UINT32_C(0x39400000),0U,true},
    {1U,A64_SCALAR_W,
        UINT32_C(0x39000000),0U,true},
    {2U,A64_SCALAR_W,
        UINT32_C(0x79400000),0U,true},
    {2U,A64_SCALAR_W,
        UINT32_C(0x79000000),0U,true},
    {4U,A64_SCALAR_X,
        0U,UINT32_C(0xb9800000),true},
    {1U,A64_SCALAR_W|A64_SCALAR_X,
        UINT32_C(0x39c00000),UINT32_C(0x39800000),false},
    {2U,A64_SCALAR_W|A64_SCALAR_X,
        UINT32_C(0x79c00000),UINT32_C(0x79800000),false}
};

typedef struct A64ScalarMemorySpelling {
    const char *mnemonic;
    unsigned form;
    unsigned policy;
} A64ScalarMemorySpelling;

static const A64ScalarMemorySpelling a64_scalar_memory_spellings[]={
    {"ldr",0U,A64_SCALAR_DIRECT},{"str",1U,A64_SCALAR_DIRECT},
    {"ldrb",2U,A64_SCALAR_DIRECT},{"strb",3U,A64_SCALAR_DIRECT},
    {"ldrh",4U,A64_SCALAR_DIRECT},{"strh",5U,A64_SCALAR_DIRECT},
    {"ldrsw",6U,A64_SCALAR_DIRECT},{"ldrsb",7U,A64_SCALAR_DIRECT},
    {"ldrsh",8U,A64_SCALAR_DIRECT},
    {"ldur",0U,A64_SCALAR_FORCE_UNSCALED},
    {"stur",1U,A64_SCALAR_FORCE_UNSCALED},
    {"ldurb",2U,A64_SCALAR_FORCE_UNSCALED},
    {"sturb",3U,A64_SCALAR_FORCE_UNSCALED},
    {"ldurh",4U,A64_SCALAR_FORCE_UNSCALED},
    {"sturh",5U,A64_SCALAR_FORCE_UNSCALED},
    {"ldursw",6U,A64_SCALAR_FORCE_UNSCALED},
    {"ldursb",7U,A64_SCALAR_FORCE_UNSCALED},
    {"ldursh",8U,A64_SCALAR_FORCE_UNSCALED},
    {"ldtr",0U,A64_SCALAR_UNPRIVILEGED},
    {"sttr",1U,A64_SCALAR_UNPRIVILEGED},
    {"ldtrb",2U,A64_SCALAR_UNPRIVILEGED},
    {"sttrb",3U,A64_SCALAR_UNPRIVILEGED},
    {"ldtrh",4U,A64_SCALAR_UNPRIVILEGED},
    {"sttrh",5U,A64_SCALAR_UNPRIVILEGED},
    {"ldtrsw",6U,A64_SCALAR_UNPRIVILEGED}
};

static const A64ScalarMemoryForm *a64_scalar_memory_form(const char *op,
                                                          unsigned *policy) {
    unsigned i;
    for(i=0U;i<sizeof(a64_scalar_memory_spellings)/
                     sizeof(a64_scalar_memory_spellings[0]);++i) {
        if(strcmp(op,a64_scalar_memory_spellings[i].mnemonic)==0) {
            *policy=a64_scalar_memory_spellings[i].policy;
            return &a64_scalar_memory_forms[
                a64_scalar_memory_spellings[i].form];
        }
    }
    return NULL;
}

/* Final layout can change a symbol difference used as a memory displacement.
 * Resolve such a displacement through the SAME ISA table as ordinary scalar
 * loads/stores, rather than maintaining an independent six-mnemonic encoder
 * with a second set of opcode bases and load/store widths.  A positive
 * displacement takes unsigned-scaled imm12 where possible, otherwise signed
 * unscaled imm9.  The memory operand itself retains the symbolic difference;
 * an explicitly unscaled/unprivileged instruction never enters this chooser. */
static bool a64_encode_deferred_scalar_memory(const MiniAsMCInst *inst,
                                               const A64Memory *memory,
                                               int64_t offset,
                                               bool *matched,
                                               uint32_t *word) {
    bool wide=inst->operands[0].register_64;
    uint32_t original;
    const A64ScalarMemoryForm *form;
    unsigned unit;
    *matched=false;
    /* The parser retained its exact scalar form ID. Recomputing the form by
     * scanning machine-bit templates is an implicit parser/encoder coupling:
     * two ISA forms may legally have identical base words, but must retain
     * distinct constraints and relocation policy. */
    if(inst->target_form==0U ||
       inst->target_form>sizeof(a64_scalar_memory_forms)/
                         sizeof(a64_scalar_memory_forms[0]))return false;
    form=&a64_scalar_memory_forms[inst->target_form-1U];
    original=wide?form->scaled_x:form->scaled_w;
    if(inst->encoding!=original || memory->mode!=A64_ADDRESS_OFFSET)
        return false;
    unit=form->bytes?form->bytes:(wide?8U:4U);
    *matched=true;
    if(offset>=0 && offset%(int64_t)unit==0 &&
       offset/(int64_t)unit<=4095) {
        *word=original|((uint32_t)(offset/(int64_t)unit)<<10U);
        return true;
    }
    if(offset>=-256 && offset<=255) {
        *word=(original-UINT32_C(0x01000000))|
              (((uint32_t)offset&511U)<<12U);
        return true;
    }
    return false;
}

static bool a64_parse_scalar_memory(MiniAs *as,const A64AliasState *aliases,MiniAsMCInst *inst,const char *op,
                                   char t[4][384],unsigned n) {
    unsigned policy,rd,unit;
    bool wide,sp;
    int64_t offset=0;
    uint32_t base_opcode;
    A64Memory memory;
    const A64ScalarMemoryForm *form=a64_scalar_memory_form(op,&policy);
    if(!form || (n!=2U && n!=3U) ||
       (policy!=A64_SCALAR_DIRECT && n!=2U) ||
       !parse_a64_reg(aliases,t[0],&rd,&wide,&sp) || sp ||
       !(form->allowed_registers & (wide?A64_SCALAR_X:A64_SCALAR_W)))
        return false;
    base_opcode=wide?form->scaled_x:form->scaled_w;
    /* A64 owns this descriptor index; Common only retains the opaque ID. */
    inst->target_form=(uint32_t)(form-a64_scalar_memory_forms)+1U;
    unit=form->bytes?form->bytes:(wide?8U:4U);
    if(n==2U && policy==A64_SCALAR_DIRECT &&
       a64_parse_indexed_address(aliases,t[1],unit,&memory)) {
        inst->opcode=A64_MC_LDST_INDEX;inst->count=2U;
        inst->encoding=base_opcode;
        put_reg(&inst->operands[0],rd,wide);
        return a64_attach_memory(as,inst,1U,&memory);
    }
    if(!a64_parse_memory(as,aliases,t[1],n==3U?t[2]:NULL,
                         &memory))return false;
    memory.access_bytes=unit;
    if(memory.post_index_register)return false;
    if(memory.offset.kind==A64_OFFSET_CONSTANT)
        offset=memory.offset.constant;
    if(policy==A64_SCALAR_UNPRIVILEGED) {
        if(memory.mode!=A64_ADDRESS_OFFSET ||
           (memory.offset.kind==A64_OFFSET_CONSTANT &&
            (offset<-256 || offset>255)))return false;
        inst->opcode=A64_MC_LDST_UNPRIV;inst->count=2U;
        inst->encoding=base_opcode;
    }else {
        if(policy==A64_SCALAR_FORCE_UNSCALED ||
           memory.mode!=A64_ADDRESS_OFFSET ||
           (memory.offset.kind==A64_OFFSET_CONSTANT &&
            (offset<0 || offset%(int64_t)unit!=0))) {
            if(memory.offset.kind==A64_OFFSET_CONSTANT &&
               (offset<-256 || offset>255))return false;
            inst->opcode=A64_MC_LDST_UNSCALED;inst->count=2U;
            inst->encoding=base_opcode;
        }else {
            if(memory.offset.kind==A64_OFFSET_CONSTANT &&
               offset/(int64_t)unit>4095)return false;
            inst->opcode=A64_MC_LDST_SCALED;
            inst->count=2U;
            inst->encoding=base_opcode;
        }
    }
    put_reg(&inst->operands[0],rd,wide);
    return a64_attach_memory(as,inst,1U,&memory);
}

/* Parsed target-owned instruction family. Complete AArch64 still requires
 * ADRP/GOT, conditional branches, shifts, and relocation fixup bitfields. */
static bool parse_integer_family(MiniAs *as,const A64AliasState *aliases,MiniAsMCInst *inst,
                                 const char *op, const char *args) {
    char t[4][384];
    unsigned n = 0U, rd, rn, rm;
    bool wd, wn, wm, spd, spn, spm;
    int64_t imm;
    unsigned kind = 0U;

    if (!split_operands(args, t, &n)) return false;
    /* Explicit add/sub 12-bit LSL #12 is a *distinct* instruction form
     * even for #0: keep shift bit for GNU exact-byte comparison. */
    if(n==4U && (!strcmp(op,"add") || !strcmp(op,"sub") ||
                 !strcmp(op,"adds") || !strcmp(op,"subs")) &&
       parse_a64_reg(aliases,t[0],&rd,&wd,&spd) &&
       parse_a64_reg(aliases,t[1],&rn,&wn,&spn) && wd==wn &&
       !(rd==31U&&!spd) && !(rn==31U&&!spn) &&
       parse_signed_number(t[2],&imm) &&
       strncmp(t[3],"lsl",3U)==0 &&
       isspace((unsigned char)t[3][3])) {
        int64_t shift;
        if(!parse_signed_number(minias_trim(t[3]+3U),&shift) ||
           shift!=12 || imm<0 || imm>4095 ||
           ((!strcmp(op,"adds") || !strcmp(op,"subs")) && spd))
            return false;
        inst->opcode=!strcmp(op,"add")?A64_MC_ADD_IMM:
                     !strcmp(op,"sub")?A64_MC_SUB_IMM:
                     !strcmp(op,"adds")?A64_MC_ADDS_IMM:A64_MC_SUBS_IMM;
        inst->count=3U;inst->encoding=UINT32_C(0x00400000);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        put_immediate(&inst->operands[2],imm);
        return true;
    }
    /* GNU extended ADD/SUB/ADDS/SUBS/CMP/CMN register forms.
     * This is an AArch64 Target ISA variant, not an alternate Common IR. */
    if(n==4U || n==3U) {
        bool cmp=n==3U && (!strcmp(op,"cmp") || !strcmp(op,"cmn"));
        bool stack_alu=n==3U && (!strcmp(op,"add") || !strcmp(op,"sub"));
        bool alu=(n==4U && (!strcmp(op,"add") || !strcmp(op,"sub") ||
                            !strcmp(op,"adds") || !strcmp(op,"subs"))) ||
                 stack_alu;
        if(cmp || alu) {
            unsigned erd,ern,erm;
            bool dwide,nwide,mwide,dsp,nsp,msp;
            uint32_t ext;
            bool sp_sensitive;
            if(parse_a64_reg(aliases,t[0],&erd,&dwide,&dsp) &&
               (cmp || parse_a64_reg(aliases,t[1],&ern,&nwide,&nsp)) &&
               parse_a64_reg(aliases,t[cmp?1U:2U],&erm,&mwide,&msp) && !msp &&
               (cmp || dwide==nwide)) {
                if(cmp) {
                    ern=erd;nwide=dwide;nsp=dsp;erd=31U;dsp=false;
                }
                sp_sensitive=dsp||nsp;
                if((!stack_alu || sp_sensitive) &&
                   (dwide || !mwide) &&
                   ((!strcmp(op,"add") || !strcmp(op,"sub")) || !dsp) &&
                   a64_extended_fields(cmp?t[2]:
                       stack_alu?(mwide?"lsl":"uxtw"):t[3],
                       mwide,sp_sensitive,&ext)) {
                    uint32_t ext_base=!strcmp(op,"add")?UINT32_C(0x0b200000):
                                  !strcmp(op,"sub")?UINT32_C(0x4b200000):
                                  !strcmp(op,"adds") || !strcmp(op,"cmn")?
                                                    UINT32_C(0x2b200000):
                                                    UINT32_C(0x6b200000);
                    inst->opcode=A64_MC_EXT_ALU;inst->count=3U;
                    inst->encoding=ext_base|ext;
                    put_reg(&inst->operands[0],erd,dwide);
                    put_reg(&inst->operands[1],ern,nwide);
                    put_reg(&inst->operands[2],erm,mwide);
                    return true;
                }
            }
        }
    }
    /* MVN shifted is the existing ORN Rd,ZR,Rm machine word. */
    if(!strcmp(op,"mvn") && n==3U) {
        uint32_t shift;
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(aliases,t[1],&rm,&wm,&spm) || spm || wm!=wd ||
           !a64_shift_fields(t[2],wd,true,&shift))return false;
        inst->opcode=A64_MC_BIC_REG;inst->count=3U;
        inst->encoding=UINT32_C(0x20000000)|shift;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],31U,wd);
        put_reg(&inst->operands[2],rm,wd);
        return true;
    }
    /* GNU shifted register arithmetic (4 operands) and compares (3).
     * This extends existing typed instruction families; no new encoder. */
    if(n==4U || n==3U) {
        bool compare=n==3U &&
            (!strcmp(op,"cmp") || !strcmp(op,"cmn") || !strcmp(op,"tst"));
        bool alu=n==4U &&
            (!strcmp(op,"add") || !strcmp(op,"sub") ||
             !strcmp(op,"adds") || !strcmp(op,"subs") ||
             !strcmp(op,"and") || !strcmp(op,"orr") ||
             !strcmp(op,"eor") || !strcmp(op,"ands") ||
             !strcmp(op,"bic") || !strcmp(op,"bics") ||
             !strcmp(op,"orn") || !strcmp(op,"eon"));
        if(compare || alu) {
            uint32_t shift;
            unsigned index=compare?1U:2U;
            bool logical=!strcmp(op,"and") || !strcmp(op,"orr") ||
                         !strcmp(op,"eor") || !strcmp(op,"ands") ||
                         !strcmp(op,"bic") || !strcmp(op,"bics") ||
                         !strcmp(op,"orn") || !strcmp(op,"eon") || !strcmp(op,"tst");
            if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
               (!compare && (!parse_a64_reg(aliases,t[1],&rn,&wn,&spn) ||
                             spn || wn!=wd)) ||
               !parse_a64_reg(aliases,t[index],&rm,&wm,&spm) || spm ||
               wm!=wd ||
               !a64_shift_fields(t[compare?2U:3U],wd,logical,&shift))
                return false;
            inst->encoding=shift;
            if(compare) {
                inst->opcode=!strcmp(op,"cmp")?A64_MC_CMP_REG:
                             !strcmp(op,"cmn")?A64_MC_CMN_REG:
                                               A64_MC_TST_REG;
                inst->count=2U;
                put_reg(&inst->operands[0],rd,wd);
                put_reg(&inst->operands[1],rm,wd);
            }else {
                inst->opcode=!strcmp(op,"add")?A64_MC_ADD_REG:
                             !strcmp(op,"sub")?A64_MC_SUB_REG:
                             !strcmp(op,"adds")?A64_MC_ADDS_REG:
                             !strcmp(op,"subs")?A64_MC_SUBS_REG:
                             !strcmp(op,"and")?A64_MC_AND_REG:
                             !strcmp(op,"orr")?A64_MC_ORR_REG:
                             !strcmp(op,"eor")?A64_MC_EOR_REG:
                             !strcmp(op,"ands")?A64_MC_TST_REG:A64_MC_BIC_REG;
                if(!strcmp(op,"bics"))inst->encoding|=UINT32_C(0x60000000);
                else if(!strcmp(op,"orn"))inst->encoding|=UINT32_C(0x20000000);
                else if(!strcmp(op,"eon"))inst->encoding|=UINT32_C(0x40000000);
                inst->count=3U;
                put_reg(&inst->operands[0],rd,wd);
                put_reg(&inst->operands[1],rn,wd);
                put_reg(&inst->operands[2],rm,wd);
            }
            return true;
        }
    }
    /* GNU ARMv8 single-vector structure LD1/ST1. The A64 bank and
     * arrangement are ISA-owned, but base register and memory policy
     * use the same shared typed operands as integer loads/stores. */
    if((!strcmp(op,"ld1") || !strcmp(op,"st1")) &&
       (n==2U || n==3U)) {
        uint32_t arrangement;
        unsigned vector_count;
        A64Memory memory;
        static const uint32_t offsets[]={0U,UINT32_C(0x7000),
             UINT32_C(0xa000),UINT32_C(0x6000),UINT32_C(0x2000)};
        if(!a64_structure_vlist(aliases,t[0],&rd,&arrangement,&vector_count) ||
           !a64_parse_memory(as,aliases,t[1],n==3U?t[2]:NULL,&memory) ||
           (n==2U?!a64_is_plain_base(&memory):
             memory.mode!=A64_ADDRESS_POST_INDEX))return false;
        memory.access_bytes=vector_count*
            ((arrangement&UINT32_C(0x40000000))?16U:8U);
        if(n==3U && !memory.post_index_register &&
           (memory.offset.kind!=A64_OFFSET_CONSTANT ||
            memory.offset.constant!=(int64_t)memory.access_bytes))return false;
        inst->opcode=A64_MC_SIMD_SINGLE;inst->count=2U;
        inst->encoding=arrangement|
                       offsets[vector_count]|
                       (!strcmp(op,"ld1")?UINT32_C(0x0c400000):
                                          UINT32_C(0x0c000000));
        put_vector(&inst->operands[0],rd);
        return a64_attach_memory(as,inst,1U,&memory);
    }
    /* Common GNU Linux bit/byte-swap and acquire/release families.
     * All operands are typed once; no instruction-name checks in encode. */
    if ((!strcmp(op,"ldxr") || !strcmp(op,"ldaxr") ||
         !strcmp(op,"ldxrb") || !strcmp(op,"ldaxrb") ||
         !strcmp(op,"ldxrh") || !strcmp(op,"ldaxrh")) && n==2U) {
        A64Memory memory;
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
           !a64_parse_memory(as,aliases,t[1],NULL,&memory) ||
           !a64_is_plain_base(&memory))return false;
        if((strchr(op,'b') || strchr(op,'h')) && wd)return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=2U;
        inst->encoding=(strncmp(op,"ldax",4U)==0?1U:0U)|
                        (op[strlen(op)-1U]=='b'?0x10U:
                         op[strlen(op)-1U]=='h'?0x20U:0U);
        put_reg(&inst->operands[0],rd,wd);
        memory.access_bytes=op[strlen(op)-1U]=='b'?1U:
                            op[strlen(op)-1U]=='h'?2U:wd?8U:4U;
        return a64_attach_memory(as,inst,1U,&memory);
    }
    if ((!strcmp(op,"stxr") || !strcmp(op,"stlxr") ||
         !strcmp(op,"stxrb") || !strcmp(op,"stlxrb") ||
         !strcmp(op,"stxrh") || !strcmp(op,"stlxrh")) && n==3U) {
        A64Memory memory;
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || wd || spd ||
           !parse_a64_reg(aliases,t[1],&rn,&wn,&spn) || spn ||
           !a64_parse_memory(as,aliases,t[2],NULL,&memory) ||
           !a64_is_plain_base(&memory))return false;
        if((op[strlen(op)-1U]=='b' || op[strlen(op)-1U]=='h') && wn)
            return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=3U;
        inst->encoding=(strncmp(op,"stlx",4U)==0?3U:2U)|
                        (op[strlen(op)-1U]=='b'?0x10U:
                         op[strlen(op)-1U]=='h'?0x20U:0U);
        put_reg(&inst->operands[0],rd,false);
        put_reg(&inst->operands[1],rn,wn);
        memory.access_bytes=op[strlen(op)-1U]=='b'?1U:
                            op[strlen(op)-1U]=='h'?2U:wn?8U:4U;
        return a64_attach_memory(as,inst,2U,&memory);
    }
    if(!strcmp(op,"prfm") && n==2U) {
        unsigned hint=0U,unit=0U;
        unsigned j;
        A64Memory memory;
        /* GNU accepts uppercase/lowercase PRFM op names, but the target
         * policy remains strictly enumerated, not fuzzy matching. */
        for(j=0U;t[0][j];++j)t[0][j]=(char)tolower((unsigned char)t[0][j]);
        if(t[0][0]=='#' || (t[0][0]>='0'&&t[0][0]<='9')) {
            if(!parse_signed_number(t[0],&imm) || imm<0 || imm>31)
                return false;
            hint=(unsigned)imm;
        }else {
            const char *suffix=t[0]+5U;
            /* A64 PRFM <prfop>: type PLD=0b00, PLI=0b01,
             * PST=0b10 in op[4:3]. Each unit step is 8, not a
             * mnemonic-order ordinal. GNU byte oracle catches this. */
            if(!strncmp(t[0],"pldl",4U))unit=0U;
            else if(!strncmp(t[0],"plil",4U))unit=1U;
            else if(!strncmp(t[0],"pstl",4U))unit=2U;
            else return false;
            if(strlen(t[0])<6U || t[0][4]<'1' || t[0][4]>'3')
                return false;
            hint=unit*8U+2U*(unsigned)(t[0][4]-'1');
            if(!strcmp(suffix,"strm"))++hint;
            else if(strcmp(suffix,"keep"))return false;
        }
        if(a64_parse_indexed_address(aliases,t[1],8U,&memory)) {
            /* 0xF8A0_0800 shares the indexed LDST encoder's option and
             * scaled bit fields; hint Rt is an immediate 0..31 rather
             * than a GPR. */
            inst->opcode=A64_MC_PRFM_REG;inst->count=2U;
            inst->encoding=UINT32_C(0xf9800000);
            put_immediate(&inst->operands[0],(int64_t)hint);
            return a64_attach_memory(as,inst,1U,&memory);
        }
        if(!a64_parse_memory(as,aliases,t[1],NULL,&memory) ||
           memory.mode!=A64_ADDRESS_OFFSET ||
           (memory.offset.kind==A64_OFFSET_CONSTANT &&
            (memory.offset.constant<0 || memory.offset.constant%8!=0 ||
             memory.offset.constant/8>4095)))
            return false;
        memory.access_bytes=8U;
        inst->opcode=A64_MC_PRFM;inst->count=2U;
        put_immediate(&inst->operands[0],(int64_t)hint);
        return a64_attach_memory(as,inst,1U,&memory);
    }
    /* Short sign/zero extensions lower to the existing typed SBFM/UBFM
     * encoder; GNU's UXT{B,H} always writes Wd (zeroing upper X bits). */
    if((!strcmp(op,"sxtb") || !strcmp(op,"sxth") ||
        !strcmp(op,"uxtb") || !strcmp(op,"uxth")) && n==2U) {
        bool signed_alias=op[0]=='s';
        bool byte_alias=op[3]=='b';
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(aliases,t[1],&rn,&wn,&spn) || spn || wn ||
           (!signed_alias && wn))return false;
        inst->opcode=A64_MC_BITFIELD;inst->count=4U;
        inst->encoding=signed_alias?UINT32_C(0x13000000):
                                    UINT32_C(0x53000000);
        put_reg(&inst->operands[0],rd,signed_alias?wd:false);
        put_reg(&inst->operands[1],rn,signed_alias?wd:false);
        put_immediate(&inst->operands[2],0);
        put_immediate(&inst->operands[3],byte_alias?7:15);
        return true;
    }
    if ((!strcmp(op,"sxtw") || !strcmp(op,"uxtw")) && n==2U) {
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || !wd || spd ||
           !parse_a64_reg(aliases,t[1],&rn,&wn,&spn) || wn || spn)return false;
        inst->opcode=A64_MC_EXTEND_W;inst->count=2U;
        inst->encoding=!strcmp(op,"sxtw")?0U:1U;
        put_reg(&inst->operands[0],rd,true);
        put_reg(&inst->operands[1],rn,false);
        return true;
    }
    /* All one-source data processing forms reuse a typed two-register
     * A64 MCInst; target-only encoding. REV32 is available for X registers. */
    if((!strcmp(op,"rev") || !strcmp(op,"rev16") ||
        !strcmp(op,"rev32") || !strcmp(op,"rbit") ||
        !strcmp(op,"clz") || !strcmp(op,"cls")) && n==2U) {
        uint32_t opcodebits;
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(aliases,t[1],&rn,&wn,&spn) || spn || wd!=wn ||
           (!strcmp(op,"rev32") && !wd))return false;
        opcodebits=!strcmp(op,"rbit")?UINT32_C(0x5ac00000):
                   !strcmp(op,"rev16")?UINT32_C(0x5ac00400):
                   !strcmp(op,"rev32")?UINT32_C(0x5ac00800):
                   !strcmp(op,"rev")?(wd?UINT32_C(0x5ac00c00):
                                         UINT32_C(0x5ac00800)):
                   !strcmp(op,"clz")?UINT32_C(0x5ac01000):
                                      UINT32_C(0x5ac01400);
        inst->opcode=A64_MC_REV;inst->count=2U;
        inst->encoding=opcodebits;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        return true;
    }
    if((!strcmp(op,"ldar") || !strcmp(op,"stlr") ||
        !strcmp(op,"ldarb") || !strcmp(op,"stlrb") ||
        !strcmp(op,"ldarh") || !strcmp(op,"stlrh")) && n==2U) {
        A64Memory memory;
        bool load=op[0]=='l';
        bool byte=op[4]=='b';
        bool half=op[4]=='h';
        if(!parse_a64_reg(aliases,t[0],&rd,&wd,&spd) || spd ||
           ((byte || half) && wd) ||
           !a64_parse_memory(as,aliases,t[1],NULL,&memory) ||
           !a64_is_plain_base(&memory))return false;
        inst->opcode=A64_MC_ACQUIRE_RELEASE;inst->count=2U;
        inst->encoding=byte?(load?2U:4U):
                       half?(load?3U:5U):(load?1U:0U);
        put_reg(&inst->operands[0],rd,wd);
        memory.access_bytes=byte?1U:half?2U:wd?8U:4U;
        return a64_attach_memory(as,inst,1U,&memory);
    }
    if(!strcmp(op,"tst") && n==2U) {
        uint64_t bits;uint32_t fields;
        if(!parse_a64_reg(aliases,t[0],&rn,&wn,&spn) || spn)return false;
        if(parse_a64_reg(aliases,t[1],&rm,&wm,&spm)) {
            if(spm || wn!=wm)return false;
            inst->opcode=A64_MC_TST_REG;inst->count=2U;
            put_reg(&inst->operands[0],rn,wn);
            put_reg(&inst->operands[1],rm,wn);
            return true;
        }
        {
            const char *literal=t[1][0]=='#'?t[1]+1:t[1];
            if(!minias_eval_u64_expression(literal,&bits))return false;
            /* GNU GAS accepts negative W-register logical immediates.
             * Mask only a syntactically negative literal to the 32-bit
             * destination width: do not truncate oversized positive 64-bit
             * constants into valid but incorrect W-register operands. */
            while(isspace((unsigned char)*literal))++literal;
            if(!wn && *literal=='-')bits=(uint32_t)bits;
            if(!a64_mask_bits(bits,wn,&fields))return false;
        }
        inst->opcode=A64_MC_BITMASK_IMM;inst->count=3U;
        inst->encoding=UINT32_C(0x72000000);
        put_reg(&inst->operands[0],31U,wn);
        put_reg(&inst->operands[1],rn,wn);
        put_immediate(&inst->operands[2],fields);
        return true;
    }
    if (strcmp(op, "mov") == 0 && n == 2U) {
        if (!parse_a64_reg(aliases,t[0], &rd, &wd, &spd) ||
            !parse_a64_reg(aliases,t[1], &rn, &wn, &spn) || wd != wn)
            return false;
        /* SP form aliases ADD #0. Register form aliases ORR with ZR. */
        if (spd || spn) {
            if ((rd == 31U && !spd) || (rn == 31U && !spn))
                return false;
        }
        inst->opcode = A64_MC_MOV_REG;
        inst->count = 2U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], rn, wn);
        inst->encoding = (spd || spn) ? 1U : 0U;
        return true;
    }
    if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
         strcmp(op, "adds") == 0 || strcmp(op, "subs") == 0 ||
         strcmp(op, "and") == 0 || strcmp(op, "orr") == 0 ||
         strcmp(op, "eor") == 0 || strcmp(op, "ands") == 0 ||
         (strcmp(op,"bic")==0 || strcmp(op,"bics")==0 ||
          strcmp(op,"orn")==0 || strcmp(op,"eon")==0)) && n == 3U) {
        if (!parse_a64_reg(aliases,t[0], &rd, &wd, &spd) ||
            !parse_a64_reg(aliases,t[1], &rn, &wn, &spn) || wd != wn)
            return false;
        if (parse_a64_reg(aliases,t[2], &rm, &wm, &spm)) {
            if (wd != wm || spd || spn || spm) return false;
            kind = strcmp(op, "add") == 0 ? A64_MC_ADD_REG :
                   strcmp(op, "sub") == 0 ? A64_MC_SUB_REG :
                   strcmp(op, "adds") == 0 ? A64_MC_ADDS_REG :
                   strcmp(op, "subs") == 0 ? A64_MC_SUBS_REG :
                   strcmp(op, "and") == 0 ? A64_MC_AND_REG :
                   strcmp(op, "orr") == 0 ? A64_MC_ORR_REG :
                   strcmp(op, "ands") == 0 ? A64_MC_TST_REG :
                   (!strcmp(op,"bic") || !strcmp(op,"bics") ||
                    !strcmp(op,"orn") || !strcmp(op,"eon")) ? A64_MC_BIC_REG :
                                             A64_MC_EOR_REG;
            put_reg(&inst->operands[2], rm, wm);
            if(!strcmp(op,"bics"))inst->encoding=UINT32_C(0x60000000);
            else if(!strcmp(op,"orn"))inst->encoding=UINT32_C(0x20000000);
            else if(!strcmp(op,"eon"))inst->encoding=UINT32_C(0x40000000);
        } else if (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
                   strcmp(op, "adds") == 0 || strcmp(op, "subs") == 0) {
            MiniAsSymbolDifference difference;
            bool numeric=parse_signed_number(t[2],&imm);
            if((rd==31U&&!spd) || (rn==31U&&!spn) ||
               (numeric?(imm<0 || (imm>4095 &&
                  (imm%4096!=0 || imm/4096>4095))):
                !a64_parse_deferred_difference(as,t[2][0]=='#'?t[2]+1U:t[2],
                                               &difference)))return false;
            kind = strcmp(op, "add") == 0 ? A64_MC_ADD_IMM :
                   strcmp(op, "sub") == 0 ? A64_MC_SUB_IMM :
                   strcmp(op, "adds") == 0 ? A64_MC_ADDS_IMM : A64_MC_SUBS_IMM;
            if(numeric)put_immediate(&inst->operands[2],imm);
            else if(!a64_attach_difference(as,inst,2U,&difference))return false;
        } else if((!strcmp(op,"and") || !strcmp(op,"orr") ||
                   !strcmp(op,"eor") || !strcmp(op,"ands") ||
                   !strcmp(op,"bic")) &&
                  !spd && !spn) {
            uint64_t bits;uint32_t fields;
            {
                const char *literal=t[2][0]=='#'?t[2]+1:t[2];
                if(!minias_eval_u64_expression(literal,&bits))return false;
                while(isspace((unsigned char)*literal))++literal;
                if(!wd && *literal=='-')bits=(uint32_t)bits;
                if(!strcmp(op,"bic") || !strcmp(op,"bics"))
                    bits=(~bits)&(wd?UINT64_MAX:UINT32_MAX);
                if(!a64_mask_bits(bits,wd,&fields))return false;
            }
            kind=A64_MC_BITMASK_IMM;
            inst->encoding=!strcmp(op,"and")?UINT32_C(0x12000000):
                           !strcmp(op,"orr")?UINT32_C(0x32000000):
                           !strcmp(op,"eor")?UINT32_C(0x52000000):
                           !strcmp(op,"bic")?UINT32_C(0x12000000):
                                              UINT32_C(0x72000000);
            put_immediate(&inst->operands[2],fields);
        } else return false;
        inst->opcode = kind;
        inst->count = 3U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], rn, wn);
        return true;
    }
    if ((strcmp(op, "cmp") == 0 || strcmp(op, "cmn") == 0) && n == 2U) {
        if (!parse_a64_reg(aliases,t[0], &rn, &wn, &spn)) return false;
        if (parse_a64_reg(aliases,t[1], &rm, &wm, &spm)) {
            if (wn != wm || spn || spm) return false;
            kind = strcmp(op,"cmp")==0 ? A64_MC_CMP_REG : A64_MC_CMN_REG;
            put_reg(&inst->operands[1], rm, wm);
        } else {
            MiniAsSymbolDifference difference;
            bool numeric=parse_signed_number(t[1],&imm);
            bool deferred=!numeric &&
                a64_parse_deferred_difference(as,t[1][0]=='#'?t[1]+1U:t[1],
                                               &difference);
            if((!numeric && !deferred) ||
               (numeric && (imm==INT64_MIN ||
                 (imm<0 && strcmp(op,"cmp")) ||
                 (imm<0?-imm:imm)>INT64_C(4095)*4096 ||
                 ((imm<0?-imm:imm)>4095 &&
                  (imm<0?-imm:imm)%4096!=0))))return false;
            /* GNU accepts the arithmetic negative CMP pseudooperand
             * in Linux AES macros and encodes it using CMN #positive.
             * Keep prior strict rejection of explicitly negative CMN
             * operands: the existing native contract tests that gate. */
            bool negate=numeric && imm<0;
            kind=negate?A64_MC_CMN_IMM:
                 !strcmp(op,"cmp")?A64_MC_CMP_IMM:A64_MC_CMN_IMM;
            if(numeric)put_immediate(&inst->operands[1],negate?-imm:imm);
            else if(!a64_attach_difference(as,inst,1U,&difference))return false;
        }
        inst->opcode = kind;
        inst->count = 2U;
        put_reg(&inst->operands[0], rn, wn);
        return true;
    }

    /* Scalar memory operands share one target ISA descriptor across
     * unsigned scaled, pre/post-index, explicitly unscaled and unprivileged
     * encodings. GNU distinguishes the addressing policy, not the core
     * register/width grammar; this keeps all forms in sync. */
    if(a64_parse_scalar_memory(as,aliases,inst,op,t,n))return true;
    if ((!strcmp(op, "stp") || !strcmp(op, "ldp") ||
         !strcmp(op,"ldpsw") || !strcmp(op,"stnp") ||
         !strcmp(op,"ldnp")) && (n == 3U || n == 4U)) {
        A64Memory memory;
        bool signed_pair=!strcmp(op,"ldpsw");
        bool nontemporal=!strcmp(op,"stnp") || !strcmp(op,"ldnp");
        bool qpair=parse_a64_q_reg(aliases,t[0],&rd);
        if(qpair) {
            if(signed_pair || !parse_a64_q_reg(aliases,t[1],&rn))return false;
            wd=true;wn=true;spd=false;spn=false;
        } else if(!parse_a64_reg(aliases,t[0], &rd, &wd, &spd) || spd ||
                  !parse_a64_reg(aliases,t[1], &rn, &wn, &spn) ||
                  spn || wd!=wn)return false;
        if(!a64_parse_memory(as,aliases,t[2],n==4U?t[3]:NULL,
                             &memory) || memory.post_index_register)
            return false;
        if(signed_pair && !wd)return false;
        /* LDNP/STNP have ONLY signed scaled imm7 offset addressing;
         * pre/post writeback encodings belong to LDP/STP. */
        if(nontemporal && memory.mode!=A64_ADDRESS_OFFSET)return false;
        memory.access_bytes=qpair?16U:(signed_pair?4U:(wd?8U:4U));
        imm=memory.offset.constant;
        if(memory.offset.kind==A64_OFFSET_CONSTANT &&
           (imm % (int64_t)memory.access_bytes!=0 ||
            imm < -(int64_t)memory.access_bytes*64 ||
            imm > (int64_t)memory.access_bytes*63))
            return false;
        inst->opcode = signed_pair?A64_MC_LDPSW:
                       (!strcmp(op, "stp") || !strcmp(op,"stnp")) ?
                          A64_MC_STP : A64_MC_LDP;
        /* Non-temporal is an instruction form, NOT a writeback mode or
         * encoded displacement. The memory operand retains addressing. */
        inst->target_form=nontemporal?A64_PAIR_NONTEMPORAL:0U;
        inst->count = 3U;
        if(qpair) {
            put_q(&inst->operands[0],rd);
            put_q(&inst->operands[1],rn);
        }else {
            put_reg(&inst->operands[0],rd,wd);
            put_reg(&inst->operands[1],rn,wn);
        }
        return a64_attach_memory(as,inst,2U,&memory);
    }
    return false;
}


/* GNU AArch64 branch condition codes are an ISA-owned decoding table. */
static bool a64_condition_code(const char *suffix,unsigned *condition) {
    static const char *const names[]={
        "eq","ne","cs","cc","mi","pl","vs","vc",
        "hi","ls","ge","lt","gt","le"
    };
    unsigned i;
    if(!strcmp(suffix,"hs"))suffix="cs";
    else if(!strcmp(suffix,"lo"))suffix="cc";
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i)
        if(!strcmp(suffix,names[i])){*condition=i;return true;}
    return false;
}
static bool a64_branch_condition(const char *op,unsigned *condition) {
    /* GNU AArch64 accepts both B.EQ and BEQ (and B.HI/BHI etc.).
     * The shared condition table owns aliases for CSET/CSEL and branches;
     * do not create a second AArch condition grammar. */
    if(!strncmp(op,"b.",2U))
        return a64_condition_code(op+2U,condition);
    return op[0]=='b' && op[1]!='\0' &&
           a64_condition_code(op+1U,condition);
}

static bool a64_attach_expr(MiniAs *as,MiniAsMCInst *inst,unsigned slot,
                            const MiniAsSymbolExpr *expr) {
    MiniAsSymbolRef *copy=malloc(sizeof(*copy));
    if(!copy)return false;
    if(!minias_bind_symbol_ref(as,expr,copy)) {free(copy);return false;}
    inst->operands[slot].kind=MINIAS_MC_BOUND_SYMBOL;
    inst->operands[slot].bound_symbol=copy;
    return true;
}

/* GNU AArch64 :lo12: addends are ELF relocation operands, NOT integer
 * immediates. Keep ownership in SymbolExpr/MCInst and shared ELF RELA. */
static bool a64_lo12_expr(const char *text,MiniAsSymbolExpr *expr) {
    /* GCC also spells the relocation as #:lo12:NAME. */
    if(*text=='#')++text;
    return strncmp(text,":lo12:",6U)==0 &&
           minias_parse_symbol_addend(text+6U,expr);
}

static bool a64_parse_relocated(MiniAs *as,const A64AliasState *aliases,
                                MiniAsMCInst *inst,
                                const char *op,const char *args) {
    char tok[4][384];
    unsigned n,rd,rn,policy;
    bool wd,wn,spd,spn;
    MiniAsSymbolExpr expr;
    const A64ScalarMemoryForm *memory_form;
    if(!split_operands(args,tok,&n))return false;
    if(!strcmp(op,"adr") && n==2U &&
       parse_a64_reg(aliases,tok[0],&rd,&wd,&spd) && wd && !spd &&
       minias_parse_symbol_addend(tok[1],&expr) &&
       a64_attach_expr(as,inst,1U,&expr)) {
        inst->opcode=A64_MC_ADR;inst->count=2U;
        put_reg(&inst->operands[0],rd,true);
        return true;
    }
    if(!strcmp(op,"adrp") && n==2U &&
       parse_a64_reg(aliases,tok[0],&rd,&wd,&spd) && wd && !spd) {
        /* GNU :got:SYMBOL selects a DIFFERENT ELF relocation from the
         * default page-relative ADRP. The instruction bytes are equal,
         * but confusing 275 with 311 would break Linux GOT links. */
        bool got=strncmp(tok[1],":got:",5U)==0;
        if(!minias_parse_symbol_addend(got?tok[1]+5U:tok[1],&expr) ||
           !a64_attach_expr(as,inst,1U,&expr))return false;
        inst->opcode=A64_MC_ADRP;inst->count=2U;
        inst->target_form=got?1U:0U; /* GOT page vs ordinary page */
        put_reg(&inst->operands[0],rd,true);
        return true;
    }
    if(!strcmp(op,"add") && n==3U &&
       parse_a64_reg(aliases,tok[0],&rd,&wd,&spd) &&
       parse_a64_reg(aliases,tok[1],&rn,&wn,&spn) && wd==wn &&
       !(rd==31U&&!spd) && !(rn==31U&&!spn) &&
       a64_lo12_expr(tok[2],&expr) &&
       a64_attach_expr(as,inst,2U,&expr)) {
        inst->opcode=A64_MC_ADD_LO12;inst->count=3U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        return true;
    }
    if(n!=2U || !parse_a64_reg(aliases,tok[0],&rd,&wd,&spd) || spd)
        return false;
    /* The ordinary and symbolic paths must not maintain competing
     * width, transfer size and opcode tables.  In particular, LDRSW's
     * signed 32-bit load into Xd has a 4-byte relocation unit.  The
     * spelling policy ensures LDUR/LDTR forms never accept :lo12:. */
    memory_form=a64_scalar_memory_form(op,&policy);
    if(!memory_form || policy!=A64_SCALAR_DIRECT ||
       !memory_form->supports_lo12_relocation ||
       !(memory_form->allowed_registers &
         (wd?A64_SCALAR_X:A64_SCALAR_W)))return false;
    {
        char displacement[96],*b=displacement;
        unsigned mode;
        bool got_lo12;
        A64Memory memory;
        if(!a64_memory_components(aliases,tok[1],&rn,displacement,&mode) ||
           mode!=0U || !*displacement)
            return false;
        memset(&memory,0,sizeof(memory));
        memory.base=rn;
        memory.mode=A64_ADDRESS_OFFSET;
        memory.access_bytes=memory_form->bytes?memory_form->bytes:
                            wd?8U:4U;
        if(*b=='#')++b;
        got_lo12=strncmp(b,":got_lo12:",10U)==0;
        if(got_lo12 && (strcmp(op,"ldr")!=0 || !wd))return false;
        if(!(got_lo12?minias_parse_symbol_addend(b+10U,&expr):
                        a64_lo12_expr(b,&expr)) ||
           !minias_bind_symbol_ref(as,&expr,&memory.offset.symbol))return false;
        memory.offset.kind=got_lo12?A64_OFFSET_GOT_LO12:A64_OFFSET_LO12;
        if(got_lo12)inst->opcode=A64_MC_LDST_GOT_LO12;
        inst->count=2U;
        put_reg(&inst->operands[0],rd,wd);
        if(!a64_attach_memory(as,inst,1U,&memory))return false;
    }
    if(inst->opcode!=A64_MC_LDST_GOT_LO12)
        inst->opcode=A64_MC_LDST_LO12;
    inst->target_form=(uint32_t)(memory_form-a64_scalar_memory_forms)+1U;
    inst->encoding=wd?memory_form->scaled_x:memory_form->scaled_w;
    return true;
}

/* Architecturally defined base EL0 system registers; no permissive
 * arbitrary sysreg numeric escape. Named register policy stays target-only. */
/* ARM64 architecture defines the syntactic system-register escape:
 * S<op0>_<op1>_C<CRn>_C<CRm>_<op2>. Each field has a fixed binary
 * width. Validate every token and range in Target ISA rather than
 * adding hundreds of one-off system register aliases or accepting
 * unbounded integers into the instruction word. */
static bool a64_numbered_system_register(const char *name,
                                         uint32_t *mrs,uint32_t *msr) {
    const char *p=name;
    uint32_t field[5],bits;
    unsigned i;
    if(tolower((unsigned char)*p)!='s')return false;
    ++p;
    for(i=0U;i<5U;++i) {
        char *end;unsigned long value;
        static const unsigned maxval[5]={3U,7U,15U,15U,7U};
        if(i==2U || i==3U) {
            if(tolower((unsigned char)*p)!='c')return false;
            ++p;
        }
        if(!isdigit((unsigned char)*p))return false;
        errno=0;
        value=strtoul(p,&end,10);
        if(errno || value>maxval[i])return false;
        field[i]=(uint32_t)value;
        if(i<4U) {
            if(*end!='_')return false;
            p=end+1U;
        } else if(*end!='\0')return false;
    }
    bits=(field[0]<<19U)|(field[1]<<16U)|
         (field[2]<<12U)|(field[3]<<8U)|(field[4]<<5U);
    *mrs=UINT32_C(0xd5200000)|bits;
    *msr=UINT32_C(0xd5000000)|bits;
    return true;
}

static bool a64_system_register(const char *name,uint32_t *mrs,uint32_t *msr) {
    static const struct {const char *name;uint32_t read,write;} table[]={
        {"sp_el0",UINT32_C(0xd5384100),UINT32_C(0xd5184100)},
        {"tpidr_el0",UINT32_C(0xd53bd040),UINT32_C(0xd51bd040)},
        {"tpidrro_el0",UINT32_C(0xd53bd060),UINT32_C(0xd51bd060)},
        /* Kernel EL1 registers, GNU ARM system-register encodings. */
        {"tpidr_el1",UINT32_C(0xd538d080),UINT32_C(0xd518d080)},
        {"tpidr_el2",UINT32_C(0xd53cd040),UINT32_C(0xd51cd040)},
        {"daif",UINT32_C(0xd53b4220),UINT32_C(0xd51b4220)},
        /* Verified against independently assembled AArch64 GNU-compatible
         * system-register encodings. A zero field means no legal access,
         * not a fallback to arbitrary sysreg bit patterns. */
        {"currentel",UINT32_C(0xd5384240),0U},
        {"sctlr_el1",UINT32_C(0xd5381000),UINT32_C(0xd5181000)},
        {"sctlr_el2",UINT32_C(0xd53c1000),UINT32_C(0xd51c1000)},
        {"sctlr_el3",UINT32_C(0xd53e1000),UINT32_C(0xd51e1000)},
        {"ttbr0_el1",UINT32_C(0xd5382000),UINT32_C(0xd5182000)},
        {"ttbr1_el1",UINT32_C(0xd5382020),UINT32_C(0xd5182020)},
        {"ttbr0_el2",UINT32_C(0xd53c2000),UINT32_C(0xd51c2000)},
        {"vttbr_el2",UINT32_C(0xd53c2100),UINT32_C(0xd51c2100)},
        {"hpfar_el2",UINT32_C(0xd53c6080),UINT32_C(0xd51c6080)},
        {"cnthctl_el2",UINT32_C(0xd53ce100),UINT32_C(0xd51ce100)},
        {"tcr_el1",UINT32_C(0xd5382040),UINT32_C(0xd5182040)},
        {"tcr_el2",UINT32_C(0xd53c2040),UINT32_C(0xd51c2040)},
        {"vbar_el1",UINT32_C(0xd538c000),UINT32_C(0xd518c000)},
        {"vbar_el2",UINT32_C(0xd53cc000),UINT32_C(0xd51cc000)},
        {"vbar_el3",UINT32_C(0xd53ec000),UINT32_C(0xd51ec000)},
        {"esr_el1",UINT32_C(0xd5385200),UINT32_C(0xd5185200)},
        {"esr_el2",UINT32_C(0xd53c5200),UINT32_C(0xd51c5200)},
        {"esr_el3",UINT32_C(0xd53e5200),UINT32_C(0xd51e5200)},
        {"far_el1",UINT32_C(0xd5386000),UINT32_C(0xd5186000)},
        {"far_el2",UINT32_C(0xd53c6000),UINT32_C(0xd51c6000)},
        {"far_el3",UINT32_C(0xd53e6000),UINT32_C(0xd51e6000)},
        {"cntvoff_el2",UINT32_C(0xd53ce060),UINT32_C(0xd51ce060)},
        {"vtcr_el2",UINT32_C(0xd53c2140),UINT32_C(0xd51c2140)},
        {"cntfrq_el0",UINT32_C(0xd53be000),UINT32_C(0xd51be000)},
        {"cntpct_el0",UINT32_C(0xd53be020),0U},
        {"cntvct_el0",UINT32_C(0xd53be040),0U},
        {"cntp_ctl_el0",UINT32_C(0xd53be220),UINT32_C(0xd51be220)},
        {"cntv_ctl_el0",UINT32_C(0xd53be320),UINT32_C(0xd51be320)},
        {"cntp_tval_el0",UINT32_C(0xd53be200),UINT32_C(0xd51be200)},
        {"cntv_tval_el0",UINT32_C(0xd53be300),UINT32_C(0xd51be300)},
        {"dczid_el0",UINT32_C(0xd53b00e0),0U},
        {"id_aa64dfr0_el1",UINT32_C(0xd5380500),0U},
        {"id_aa64mmfr0_el1",UINT32_C(0xd5380700),0U},
        {"id_aa64mmfr1_el1",UINT32_C(0xd5380720),0U},
        {"clidr_el1",UINT32_C(0xd5390020),0U},
        {"ctr_el0",UINT32_C(0xd53b0020),0U},
        {"par_el1",UINT32_C(0xd5387400),UINT32_C(0xd5187400)},
        {"hcr_el2",UINT32_C(0xd53c1100),UINT32_C(0xd51c1100)},
        {"hstr_el2",UINT32_C(0xd53c1160),UINT32_C(0xd51c1160)},
        {"actlr_el1",UINT32_C(0xd5381020),UINT32_C(0xd5181020)},
        {"id_aa64pfr1_el1",UINT32_C(0xd5380420),0U},
        {"pmovsclr_el0",UINT32_C(0xd53b9c60),UINT32_C(0xd51b9c60)},
        {"pmcntenset_el0",UINT32_C(0xd53b9c20),UINT32_C(0xd51b9c20)},
        {"pmintenclr_el1",UINT32_C(0xd5389e40),UINT32_C(0xd5189e40)},
        {"pmintenset_el1",UINT32_C(0xd5389e20),UINT32_C(0xd5189e20)},
        {"pmcr_el0",UINT32_C(0xd53b9c00),UINT32_C(0xd51b9c00)},
        {"pmcntenclr_el0",UINT32_C(0xd53b9c40),UINT32_C(0xd51b9c40)},
        /* S3/S0 sysreg encodings independently assembled with LLVM
         * AArch64 IAS; read-only status is explicitly rejected by MSR. */
        {"vmpidr_el2",UINT32_C(0xd53c00a0),UINT32_C(0xd51c00a0)},
        /* Exact GNU/LLVM MRS/MSR EL2 virtual processor ID register. */
        {"vpidr_el2",UINT32_C(0xd53c0000),UINT32_C(0xd51c0000)},
        {"aidr_el1",UINT32_C(0xd53900e0),0U},
        {"pmceid0_el0",UINT32_C(0xd53b9cc0),0U},
        {"pmccfiltr_el0",UINT32_C(0xd53befe0),UINT32_C(0xd51befe0)},
        {"cntp_cval_el0",UINT32_C(0xd53be240),UINT32_C(0xd51be240)},
        {"mair_el2",UINT32_C(0xd53ca200),UINT32_C(0xd51ca200)},
        {"nzcv",UINT32_C(0xd53b4200),UINT32_C(0xd51b4200)},

        /* Architectural AArch64 GNU/LLVM system register oracles.
         * Exact operand access class is retained; read-only debug
         * authentication status deliberately has no MSR encoding. */
        {"pmuserenr_el0",UINT32_C(0xd53b9e00),UINT32_C(0xd51b9e00)},
        {"pmccntr_el0",UINT32_C(0xd53b9d00),UINT32_C(0xd51b9d00)},
        {"ifsr32_el2",UINT32_C(0xd53c5020),UINT32_C(0xd51c5020)},
        {"cptr_el2",UINT32_C(0xd53c1140),UINT32_C(0xd51c1140)},
        {"dbgauthstatus_el1",UINT32_C(0xd5307ec0),0U},
        {"dbgvcr32_el2",UINT32_C(0xd5340700),UINT32_C(0xd5140700)},
        {"revidr_el1",UINT32_C(0xd53800c0),0U},
        {"spsel",UINT32_C(0xd5384200),UINT32_C(0xd5184200)},
        {"oslar_el1",0U,UINT32_C(0xd5101080)},
        {"oslsr_el1",UINT32_C(0xd5301180),0U},
        {"cntkctl_el1",UINT32_C(0xd538e100),UINT32_C(0xd518e100)},
        {"pmceid1_el0",UINT32_C(0xd53b9ce0),0U},
        {"sp_el1",UINT32_C(0xd53c4100),UINT32_C(0xd51c4100)},
        {"cntv_cval_el0",UINT32_C(0xd53be340),UINT32_C(0xd51be340)},
        {"pmselr_el0",UINT32_C(0xd53b9ca0),UINT32_C(0xd51b9ca0)},
        {"dacr32_el2",UINT32_C(0xd53c3000),UINT32_C(0xd51c3000)},
        {"fpsr",UINT32_C(0xd53b4420),UINT32_C(0xd51b4420)},
        {"fpcr",UINT32_C(0xd53b4400),UINT32_C(0xd51b4400)},
        {"mdcr_el2",UINT32_C(0xd53c1120),UINT32_C(0xd51c1120)},
        {"isr_el1",UINT32_C(0xd538c100),0U},
        {"spsr_el2",UINT32_C(0xd53c4000),UINT32_C(0xd51c4000)},
        {"elr_el2",UINT32_C(0xd53c4020),UINT32_C(0xd51c4020)},
        {"spsr_abt",UINT32_C(0xd53c4320),UINT32_C(0xd51c4320)},
        {"spsr_und",UINT32_C(0xd53c4340),UINT32_C(0xd51c4340)},
        {"spsr_irq",UINT32_C(0xd53c4300),UINT32_C(0xd51c4300)},
        {"spsr_fiq",UINT32_C(0xd53c4360),UINT32_C(0xd51c4360)},
        {"fpexc32_el2",UINT32_C(0xd53c5300),UINT32_C(0xd51c5300)},
        {"mdccint_el1",UINT32_C(0xd5300200),UINT32_C(0xd5100200)},
        {"id_aa64pfr0_el1",UINT32_C(0xd5380400),0U},
        {"id_aa64isar0_el1",UINT32_C(0xd5380600),0U},
        {"id_aa64isar1_el1",UINT32_C(0xd5380620),0U},
        {"osdlr_el1",UINT32_C(0xd5301380),UINT32_C(0xd5101380)},
        {"contextidr_el1",UINT32_C(0xd538d020),UINT32_C(0xd518d020)},
        {"cpacr_el1",UINT32_C(0xd5381040),UINT32_C(0xd5181040)},
        {"mair_el1",UINT32_C(0xd538a200),UINT32_C(0xd518a200)},
        {"amair_el1",UINT32_C(0xd538a300),UINT32_C(0xd518a300)},
        {"spsr_el1",UINT32_C(0xd5384000),UINT32_C(0xd5184000)},
        {"elr_el1",UINT32_C(0xd5384020),UINT32_C(0xd5184020)},
        {"mdccsr_el0",UINT32_C(0xd5330100),0U},
        {"mdscr_el1",UINT32_C(0xd5300240),UINT32_C(0xd5100240)},
        {"midr_el1",UINT32_C(0xd5380000),0U},
        {"mpidr_el1",UINT32_C(0xd53800a0),0U}
    };
    size_t i;
    /* GNU GAS case-folds system-register *names*. Keep one target-owned
     * table and preserve explicit read/write permission masks; never
     * parse an unknown arbitrary sysreg into guessed bits. */
    for(i=0U;i<sizeof(table)/sizeof(table[0]);++i) {
        const unsigned char *a=(const unsigned char *)name;
        const unsigned char *b=(const unsigned char *)table[i].name;
        while(*a && *b && tolower(*a)==tolower(*b)){++a;++b;}
        if(*a==0U && *b==0U) {
            *mrs=table[i].read;*msr=table[i].write;return true;
        }
    }
    /* DBGBVRn/DBGBCRn/DBGWVRn/DBGWCRn (0..15) share a documented
     * architectural CRm index field. Keep this one family decoder in
     * Target ISA; remove redundant per-index table entries above.
     * GNU case-insensitive names are preserved, unknown sysregs fail. */
    {
        static const struct {const char *prefix;uint32_t bits;} family[]={
            {"dbgbvr",UINT32_C(0x80)},{"dbgbcr",UINT32_C(0xa0)},
            {"dbgwvr",UINT32_C(0xc0)},{"dbgwcr",UINT32_C(0xe0)}
        };
        char key[64];
        size_t n=strlen(name),j;
        if(n>=sizeof(key))return false;
        for(j=0U;j<n;++j)key[j]=(char)tolower((unsigned char)name[j]);
        key[n]='\0';
        for(j=0U;j<sizeof(family)/sizeof(family[0]);++j) {
            char *end;
            unsigned long index;
            if(strncmp(key,family[j].prefix,6U)!=0 ||
               !isdigit((unsigned char)key[6]))continue;
            errno=0;
            index=strtoul(key+6U,&end,10);
            if(errno || index>15UL || strcmp(end,"_el1"))continue;
            *mrs=UINT32_C(0xd5300000)|family[j].bits|
                 ((uint32_t)index<<8U);
            *msr=UINT32_C(0xd5100000)|family[j].bits|
                 ((uint32_t)index<<8U);
            return true;
        }
    }
    /* PMU event type and counter banks 0..30 are register families,
     * not 62 unrelated opcodes. Architecture fields CRm/op2 change
     * linearly with index. GNU/LLVM exact bytes define both bases. */
    {
        static const struct {const char *prefix;uint32_t bits;} banks[]={
            {"pmevtyper",UINT32_C(0xd53bec00)},
            {"pmevcntr",UINT32_C(0xd53be800)}
        };
        char key[64];
        size_t n=strlen(name),j;
        if(n<sizeof(key)) {
            for(j=0U;j<n;++j)key[j]=(char)tolower((unsigned char)name[j]);
            key[n]='\0';
            for(j=0U;j<sizeof(banks)/sizeof(banks[0]);++j) {
                size_t k=strlen(banks[j].prefix);
                char *end;
                unsigned long index;
                if(strncmp(key,banks[j].prefix,k)!=0 ||
                   !isdigit((unsigned char)key[k]))continue;
                errno=0;
                index=strtoul(key+k,&end,10);
                if(errno || index>30UL || strcmp(end,"_el0"))continue;
                *mrs=banks[j].bits|((uint32_t)index<<5U);
                *msr=(*mrs & ~UINT32_C(0x00200000));
                return true;
            }
        }
    }
    return a64_numbered_system_register(name,mrs,msr);
}

static bool a64_barrier_option(const char *name,unsigned *imm) {
    static const struct {const char *name;unsigned code;} table[]={
        {"oshld",1U},{"oshst",2U},{"osh",3U},
        {"nshld",5U},{"nshst",6U},{"nsh",7U},
        {"ishld",9U},{"ishst",10U},{"ish",11U},
        {"ld",13U},{"st",14U},{"sy",15U}
    };
    int64_t val;
    size_t i;
    if(parse_signed_number(name,&val)) {
        if(val<0 || val>15)return false;
        *imm=(unsigned)val;return true;
    }
    for(i=0U;i<sizeof(table)/sizeof(table[0]);++i)
        if(!strcmp(name,table[i].name)) {
            *imm=table[i].code;return true;
        }
    return false;
}

/* Target-scoped ARMv8.1 LSE ISA family; no Common or ELF encoding clone.
 * Enforce directive opt-in and typed W/X source, destination and X base.
 * CAS has a distinct acquire/release encoding; ST aliases return to ZR. */
static bool a64_parse_lse(const A64AliasState *aliases,MiniAs *as,MiniAsMCInst *inst,
                          const char *op,const char *args) {
    static const struct {const char *name;unsigned kind;bool store;} ops[]={
        {"cas",0U,false},{"ldadd",1U,false},{"ldclr",2U,false},
        {"ldset",3U,false},{"swp",4U,false},
        {"stadd",1U,true},{"stclr",2U,true},{"stset",3U,true},
        {"ldeor",5U,false},{"steor",5U,true}
    };
    char tok[4][384];
    unsigned i,n,rs,rt=31U,group=0U;
    unsigned subword=0U;
    bool wide,wr,sp;
    const char *suffix=NULL;
    char modifiers[4];
    A64Memory memory;
    for(i=0U;i<sizeof(ops)/sizeof(ops[0]);++i) {
        size_t plen=strlen(ops[i].name);
        if(strncmp(op,ops[i].name,plen)==0) {
            group=i;suffix=op+plen;break;
        }
    }
    if(suffix==NULL ||
       (a64_features(as)&A64_FEATURE_LSE)==0U)return false;
    {
        size_t nmod=strlen(suffix);
        /* A64 LSE size is encoded in bits [31:30], separately from
         * acquire/release semantics. GNU spells CASALH, STADDLB etc.
         * Preserve existing unsuffixed W/X inference. */
        if(nmod && suffix[nmod-1U]=='b'){subword=1U;--nmod;}
        else if(nmod && suffix[nmod-1U]=='h'){subword=2U;--nmod;}
        if(nmod>=sizeof(modifiers))return false;
        memcpy(modifiers,suffix,nmod);modifiers[nmod]='\0';
        if(*modifiers && strcmp(modifiers,"a") &&
           strcmp(modifiers,"l") && strcmp(modifiers,"al"))return false;
        if(ops[group].store && *modifiers && strcmp(modifiers,"l"))
            return false;
    }
    if(!split_operands(args,tok,&n) ||
       n!=(ops[group].store?2U:3U) ||
       !parse_a64_reg(aliases,tok[0],&rs,&wide,&sp) || sp ||
       (subword!=0U && wide) ||
       (!ops[group].store &&
        (!parse_a64_reg(aliases,tok[1],&rt,&wr,&sp) || sp || wr!=wide)) ||
       !a64_parse_memory(as,aliases,tok[n-1U],NULL,&memory) ||
       !a64_is_plain_base(&memory))return false;
    inst->opcode=A64_MC_LSE;inst->count=3U;
    inst->encoding=ops[group].kind |
      (ops[group].store?0x10U:0U) |
      ((strchr(modifiers,'a')!=NULL)?0x20U:0U) |
      ((strchr(modifiers,'l')!=NULL)?0x40U:0U) |
      (subword==1U?0x80U:subword==2U?0x100U:0U);
    put_reg(&inst->operands[0],rs,wide);
    put_reg(&inst->operands[1],rt,wide);
    memory.access_bytes=subword==1U?1U:subword==2U?2U:wide?8U:4U;
    return a64_attach_memory(as,inst,2U,&memory);
}

/* A64's branch relocation can combine a base SymbolRef with a second
 * section-relative difference.  Only that composite uses target_payload;
 * ordinary ALU and address differences live in their typed MC operands. */
typedef struct A64BranchAdjustment {
    MiniAsSymbolDifferenceRef difference;
} A64BranchAdjustment;

/* Preserve the symbol pair while freezing only the purely absolute suffix
 * at its lexical site. For example, '(end-start)+(2*4)' retains end/start
 * and adds eight to the positive symbol's addend. A later fragment growth
 * can then change the distance without changing the lexical constant. */
static bool a64_parse_deferred_difference(MiniAs *as,const char *source,
                                          MiniAsSymbolDifference *difference) {
    char *copy=minias_strdup(source),*body,*p,*split=NULL;
    unsigned depth=0U;
    int64_t suffix=0;
    bool ok=false;
    if(!copy)return false;
    body=minias_expr_strip_outer_parens(minias_trim(copy));
    for(p=body;*p;++p) {
        if(*p=='(')++depth;
        else if(*p==')') {
            if(!depth)goto done;
            --depth;
        }else if(*p=='+' && depth==0U && strchr(body,'-') &&
                 p>strchr(body,'-')) {
            split=p;break;
        }
    }
    if(split) {
        *split++='\0';
        if(!minias_evaluate_absolute_expression(as,minias_trim(split),&suffix))
            goto done;
    }
    if(!minias_parse_symbol_difference(minias_trim(body),difference))goto done;
    if(!minias_checked_add_i64(difference->positive.addend,suffix,
                               &difference->positive.addend))goto done;
    ok=true;
done:
    free(copy);
    return ok;
}

/* Parse a branch base symbol plus an optional parenthesized layout-dependent
 * addend without ever rewriting the source.  The two name refs are retained
 * independently: one names the branch target, the other is an absolute
 * same-section difference to be added after MCLayout reaches its fixed point. */
static bool a64_attach_branch_target(MiniAs *as,MiniAsMCInst *inst,
                                     unsigned slot,const char *source) {
    MiniAsSymbolExpr base;
    MiniAsSymbolDifference difference;
    const char *plus;
    bool composite=false;
    if(!minias_parse_symbol_addend(source,&base)) {
        for(plus=strchr(source,'+');plus;plus=strchr(plus+1U,'+')) {
            const char *open=plus+1U,*close=NULL,*tail;
            char expression[256];
            char *base_text;
            size_t base_size,expr_size;
            unsigned depth=0U;
            while(isspace((unsigned char)*open))++open;
            if(*open!='(')continue;
            for(const char *cursor=open;*cursor;++cursor) {
                if(*cursor=='(')++depth;
                else if(*cursor==')') {
                    if(!depth)break;
                    if(--depth==0U){close=cursor;break;}
                }
            }
            if(!close)continue;
            tail=close+1U;
            while(isspace((unsigned char)*tail))++tail;
            expr_size=(size_t)(close-open-1U);
            if(*tail || expr_size>=sizeof(expression))continue;
            memcpy(expression,open+1U,expr_size);
            expression[expr_size]='\0';
            if(!a64_parse_deferred_difference(as,minias_trim(expression),
                                              &difference))continue;
            base_size=(size_t)(plus-source);
            base_text=malloc(base_size+1U);
            if(!base_text)return false;
            memcpy(base_text,source,base_size);
            base_text[base_size]='\0';
            composite=minias_parse_symbol_addend(minias_trim(base_text),&base);
            free(base_text);
            if(composite)break;
        }
        if(!composite)return false;
    }
    if((base.addend&3)!=0 || !a64_attach_expr(as,inst,slot,&base))return false;
    if(composite) {
        A64BranchAdjustment *adjustment=malloc(sizeof(*adjustment));
        if(!adjustment)return false;
        if(!minias_bind_symbol_difference_ref(as,&difference,
                                               &adjustment->difference)) {
            free(adjustment);return false;
        }
        inst->target_payload=adjustment;
        inst->dispose_target_payload=free;
    }
    return true;
}

static bool parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
    const A64TargetState *state=(const A64TargetState *)as->target_state;
    const A64AliasState *aliases=state?state->aliases:NULL;
    /* Alias state belongs to this lexical parse site. Typed register
     * recognizers bind the resolved number/bank into MCInst immediately;
     * later .unreq cannot change any previously parsed instruction. */
    MiniAsMCInst *inst = minias_mcinst_alloc(4U);
    unsigned reg, imm;
    bool reg64;
    char first[80], second[80];
    const char *op = stmt->op;
    const char *args = stmt->args;

    if (inst == NULL) {
        minias_set_error(as, "out-of-memory:aarch64-mcinst");
        return false;
    }
    if((!strcmp(op,"ldr") || !strcmp(op,"str")) &&
       (a64_register_prefix(aliases,args)=='s' ||
        a64_register_prefix(aliases,args)=='d' ||
        a64_register_prefix(aliases,args)=='q')) {
        /* S/D/Q FP/SIMD unsigned-scaled memory goes through the already
         * checked typed A64 LDST path; no new emitter or Common ISA. */
        char tok[4][384];unsigned n,rd,width=0U;
        bool fp64=false;
        A64Memory memory;
        uint32_t base;
        if(!split_operands(args,tok,&n) || n!=2U) {
            minias_set_error(as,"bad-aarch64-fp-memory:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        if(a64_scalar_fp_reg(aliases,tok[0],&rd,&fp64))width=fp64?8U:4U;
        else if(parse_a64_q_reg(aliases,tok[0],&rd))width=16U;
        if(width==0U ||
           !a64_parse_memory(as,aliases,tok[1],NULL,&memory) ||
           memory.mode!=A64_ADDRESS_OFFSET ||
           (memory.offset.kind==A64_OFFSET_CONSTANT &&
            (memory.offset.constant<0 ||
             memory.offset.constant%(int64_t)width!=0 ||
             memory.offset.constant/(int64_t)width>4095))) {
            minias_set_error(as,"bad-aarch64-fp-memory:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        base=width==4U?UINT32_C(0xbd000000):
             width==8U?UINT32_C(0xfd000000):
                         UINT32_C(0x3d800000);
        if(!strcmp(op,"ldr"))base|=UINT32_C(0x00400000);
        memory.access_bytes=width;
        inst->opcode=A64_MC_LDST_SCALED;inst->count=2U;
        inst->encoding=base;
        if(width==16U)put_q(&inst->operands[0],rd);
        else put_fp_scalar(&inst->operands[0],rd,fp64);
        if(!a64_attach_memory(as,inst,1U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"ldr") && strchr(args,'=')!=NULL) {
        /* GNU LDR Xt/Wt,=symbol: the Target chooses the literal-load
         * encoding; the queued data/ABS relocation remains Common-owned.
         * Numeric pseudo-immediates are deliberately unsupported until
         * independently proved against GNU MOV/LDR constant selection. */
        char operands[4][384];
        unsigned n,rd;
        bool wide,sp;
        MiniAsSymbolExpr source,pool;
        if(!split_operands(args,operands,&n) || n!=2U ||
           !parse_a64_reg(aliases,operands[0],&rd,&wide,&sp) || sp ||
           operands[1][0]!='=' ||
           !minias_parse_symbol_addend(operands[1]+1U,&source) ||
           !minias_queue_literal(as,&source,wide?8U:4U,&pool) ||
           !a64_attach_expr(as,inst,1U,&pool)) {
            minias_set_error(as,"bad-aarch64-literal-load:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_LDR_LITERAL;inst->count=2U;
        put_reg(&inst->operands[0],rd,wide);
    } else if(!strcmp(op,"ldr") && strchr(args,'[')==NULL &&
              strchr(args,'=')==NULL) {
        /* Real GNU LDR Xt/Wt, LABEL directly addresses a literal via
         * signed PC-relative imm19; unlike =SYMBOL it emits NO pool.
         * Same-section references resolve locally; cross-section and
         * undefined references retain the target-owned ELF LD_PREL_LO19. */
        char operands[4][384];
        unsigned n,rd;
        bool wide,sp;
        MiniAsSymbolExpr source;
        if(!split_operands(args,operands,&n) || n!=2U ||
           !parse_a64_reg(aliases,operands[0],&rd,&wide,&sp) || sp ||
           !minias_parse_symbol_addend(operands[1],&source) ||
           !a64_attach_expr(as,inst,1U,&source)) {
            minias_set_error(as,"bad-aarch64-direct-literal:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_LDR_LITERAL;inst->count=2U;
        inst->target_form=1U; /* target-local direct vs queued =literal */
        put_reg(&inst->operands[0],rd,wide);
    } else if(a64_parse_lse(aliases,as,inst,op,args)) {
        /* LSE grammar is owned by one target decoder. */
    } else if(!strcmp(op,"madd") || !strcmp(op,"msub") ||
              !strcmp(op,"umaddl") || !strcmp(op,"smaddl") ||
              !strcmp(op,"umsubl") || !strcmp(op,"smsubl")) {
        /* Same target multiply-add machine form as MUL/MULL aliases,
         * but retaining the real Ra register rather than encoding ZR. */
        static const struct {const char *name;uint32_t base;bool longop;} tab[]={
            {"madd",UINT32_C(0x1b000000),false},
            {"msub",UINT32_C(0x1b008000),false},
            {"umaddl",UINT32_C(0x9ba00000),true},
            {"smaddl",UINT32_C(0x9b200000),true},
            {"umsubl",UINT32_C(0x9ba08000),true},
            {"smsubl",UINT32_C(0x9b208000),true}
        };
        char tok[4][384];unsigned n,rd,rn,rm,ra,k;
        bool wd,wn,wm,wa,sp;
        for(k=0U;k<sizeof(tab)/sizeof(tab[0]);++k)
            if(!strcmp(op,tab[k].name))break;
        if(k==sizeof(tab)/sizeof(tab[0]) ||
           !split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(aliases,tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(aliases,tok[1],&rn,&wn,&sp) || sp ||
           !parse_a64_reg(aliases,tok[2],&rm,&wm,&sp) || sp ||
           !parse_a64_reg(aliases,tok[3],&ra,&wa,&sp) || sp ||
           (tab[k].longop?(!wd || wn || wm || !wa):
                          (wd!=wn || wd!=wm || wd!=wa))) {
            minias_set_error(as,"bad-aarch64-multiply-accumulate:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_ARITH_ALIAS;inst->count=4U;
        inst->encoding=tab[k].base;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        put_reg(&inst->operands[2],rm,wm);
        put_reg(&inst->operands[3],ra,wa);
    } else if(!strcmp(op,"neg") || !strcmp(op,"negs") ||
              !strcmp(op,"ngc") || !strcmp(op,"ngcs") ||
              !strcmp(op,"adc") || !strcmp(op,"adcs") ||
              !strcmp(op,"sbc") || !strcmp(op,"sbcs") ||
              !strcmp(op,"mul") || !strcmp(op,"mneg") ||
              !strcmp(op,"umull") || !strcmp(op,"smull") ||
              !strcmp(op,"umulh") || !strcmp(op,"smulh") ||
              !strcmp(op,"udiv") || !strcmp(op,"sdiv")) {
        /* GNU A64 three-operand data-processing family. NEG/NGC are
         * SUB/SBC with Rn=ZR; MUL/MNEG/MULL are multiply-add aliases
         * with Ra=ZR. Shared typed operands and one target encoder. */
        static const struct {const char *name;uint32_t base;
                             bool unary;bool long_wide;bool high_wide;} ops[]={
            {"neg",UINT32_C(0x4b000000),true,false,false},
            {"negs",UINT32_C(0x6b000000),true,false,false},
            {"ngc",UINT32_C(0x5a000000),true,false,false},
            {"ngcs",UINT32_C(0x7a000000),true,false,false},
            {"adc",UINT32_C(0x1a000000),false,false,false},
            {"adcs",UINT32_C(0x3a000000),false,false,false},
            {"sbc",UINT32_C(0x5a000000),false,false,false},
            {"sbcs",UINT32_C(0x7a000000),false,false,false},
            {"mul",UINT32_C(0x1b007c00),false,false,false},
            {"mneg",UINT32_C(0x1b00fc00),false,false,false},
            {"umull",UINT32_C(0x9ba07c00),false,true,false},
            {"smull",UINT32_C(0x9b207c00),false,true,false},
            {"umulh",UINT32_C(0x9bc07c00),false,false,true},
            {"smulh",UINT32_C(0x9b407c00),false,false,true},
            {"udiv",UINT32_C(0x1ac00800),false,false,false},
            {"sdiv",UINT32_C(0x1ac00c00),false,false,false}
        };
        char alu_tok[4][384];unsigned alu_n,alu_rd,alu_rn=31U,alu_rm,alu_k;
        bool alu_wide,alu_nwide=false,alu_mwide,alu_sp;
        for(alu_k=0U;alu_k<sizeof(ops)/sizeof(ops[0]);++alu_k)
            if(!strcmp(op,ops[alu_k].name))break;
        if(alu_k==sizeof(ops)/sizeof(ops[0]) ||
           !split_operands(args,alu_tok,&alu_n) ||
           (ops[alu_k].unary?
             (alu_n!=2U &&
              (alu_n!=3U || alu_k>1U)):alu_n!=3U) ||
           !parse_a64_reg(aliases,alu_tok[0],&alu_rd,&alu_wide,&alu_sp) ||
           alu_sp) {
            minias_set_error(as,"bad-aarch64-arith-alias:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        if(!ops[alu_k].unary &&
           (!parse_a64_reg(aliases,alu_tok[1],&alu_rn,&alu_nwide,&alu_sp) ||
            alu_sp)) {
            minias_set_error(as,"bad-aarch64-arith-source:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        if(!parse_a64_reg(aliases,alu_tok[ops[alu_k].unary?1U:2U],
                          &alu_rm,&alu_mwide,&alu_sp) || alu_sp ||
           (ops[alu_k].long_wide?
              (!alu_wide || alu_nwide || alu_mwide):
              ops[alu_k].high_wide?
              (!alu_wide || !alu_nwide || !alu_mwide):
              (alu_mwide!=alu_wide ||
               (!ops[alu_k].unary && alu_nwide!=alu_wide)))) {
            minias_set_error(as,"bad-aarch64-arith-width:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_ARITH_ALIAS;inst->count=3U;
        inst->encoding=ops[alu_k].base;
        if(ops[alu_k].unary && alu_n==3U) {
            uint32_t shift_fields;
            if(!a64_shift_fields(alu_tok[2],alu_wide,false,&shift_fields)) {
                minias_set_error(as,"bad-aarch64-neg-shift:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            inst->encoding|=shift_fields;
        }
        put_reg(&inst->operands[0],alu_rd,alu_wide);
        put_reg(&inst->operands[1],alu_rn,alu_wide);
        put_reg(&inst->operands[2],alu_rm,alu_mwide);
    } else if(!strcmp(op,"ubfm") || !strcmp(op,"sbfm") ||
              !strcmp(op,"bfm") ||
              !strcmp(op,"ubfx") || !strcmp(op,"ubfiz") ||
              !strcmp(op,"sbfx") || !strcmp(op,"sbfiz") ||
              !strcmp(op,"bfi")  || !strcmp(op,"bfxil")) {
        /* One ISA-owned UBFM/SBFM/BFM bitfield selector for GNU aliases:
         * extracts use imms=lsb+width-1, insertions wrap immr modulo
         * XLEN and use imms=width-1. No Common parser or ELF fork. */
        char bit_tok[4][384];
        unsigned bit_n,bit_rd,bit_rn,reg_bits;
        bool bit_wide,bit_source_wide,bit_sp;
        int64_t lsb,width;
        bool raw_form=!strcmp(op,"ubfm") || !strcmp(op,"sbfm") ||
                      !strcmp(op,"bfm");
        bool insertion=!strcmp(op,"ubfiz") || !strcmp(op,"sbfiz") ||
                       !strcmp(op,"bfi");
        uint32_t base=raw_form?
                      (!strcmp(op,"ubfm")?UINT32_C(0x53000000):
                       !strcmp(op,"sbfm")?UINT32_C(0x13000000):
                                             UINT32_C(0x33000000)):
                      !strcmp(op,"ubfx") || !strcmp(op,"ubfiz")?
                      UINT32_C(0x53000000):
                      !strcmp(op,"sbfx") || !strcmp(op,"sbfiz")?
                      UINT32_C(0x13000000):UINT32_C(0x33000000);
        if(!split_operands(args,bit_tok,&bit_n) || bit_n!=4U ||
           !parse_a64_reg(aliases,bit_tok[0],&bit_rd,&bit_wide,&bit_sp) ||
           bit_sp ||
           !parse_a64_reg(aliases,bit_tok[1],&bit_rn,&bit_source_wide,&bit_sp) ||
           bit_sp || bit_wide!=bit_source_wide ||
           !parse_signed_number(bit_tok[2],&lsb) ||
           !parse_signed_number(bit_tok[3],&width)) {
            minias_set_error(as,"bad-aarch64-bitfield-operands:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        reg_bits=bit_wide?64U:32U;
        if(lsb<0 || lsb>=(int64_t)reg_bits ||
           (raw_form ?
             (width<0 || width>=(int64_t)reg_bits) :
             (width<1 || width>(int64_t)reg_bits-lsb))) {
            minias_set_error(as,"bad-aarch64-bitfield-range:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_BITFIELD;inst->count=4U;
        inst->encoding=base;
        put_reg(&inst->operands[0],bit_rd,bit_wide);
        put_reg(&inst->operands[1],bit_rn,bit_source_wide);
        put_immediate(&inst->operands[2],
                      raw_form?lsb:
                      insertion?((int64_t)reg_bits-lsb)%(int64_t)reg_bits:lsb);
        put_immediate(&inst->operands[3],
                      raw_form?width:
                      insertion?width-1:lsb+width-1);
    } else if(!strcmp(op,"lsl") || !strcmp(op,"lsr") ||
              !strcmp(op,"asr") || !strcmp(op,"ror")) {
        char tok[4][384];
        unsigned n,rd,rn,rm;
        bool wide,sourcewide,shiftwide,sp;
        int64_t amount;
        unsigned shiftop=!strcmp(op,"lsl")?0U:
                         !strcmp(op,"lsr")?1U:
                         !strcmp(op,"asr")?2U:3U;
        if(!split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(aliases,tok[0],&rd,&wide,&sp) || sp ||
           !parse_a64_reg(aliases,tok[1],&rn,&sourcewide,&sp) || sp ||
           wide!=sourcewide) {
            minias_set_error(as,"bad-aarch64-shift:%s:line=%zu",args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SHIFT;inst->count=3U;
        inst->encoding=shiftop;
        put_reg(&inst->operands[0],rd,wide);
        put_reg(&inst->operands[1],rn,wide);
        if(parse_a64_reg(aliases,tok[2],&rm,&shiftwide,&sp)) {
            if(sp || shiftwide!=wide) {
                minias_set_error(as,"bad-aarch64-shift-width:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            inst->encoding|=UINT32_C(0x100);
            put_reg(&inst->operands[2],rm,wide);
        }else {
            if(!parse_signed_number(tok[2],&amount) ||
               amount<0 || amount>=(wide?64:32)) {
                minias_set_error(as,"bad-aarch64-shift-immediate:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            put_immediate(&inst->operands[2],amount);
        }
    } else if(!strcmp(op,"extr")) {
        char extra[4][384];unsigned n,rd,rn,rm;
        bool wd,wn,wm,sp;
        int64_t lsb;
        if(!split_operands(args,extra,&n) || n!=4U ||
           !parse_a64_reg(aliases,extra[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(aliases,extra[1],&rn,&wn,&sp) || sp ||
           !parse_a64_reg(aliases,extra[2],&rm,&wm,&sp) || sp ||
           wd!=wn || wd!=wm || !parse_signed_number(extra[3],&lsb) ||
           lsb<0 || lsb>=(wd?64:32)) {
            minias_set_error(as,"bad-aarch64-extr:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_EXTR;inst->count=4U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        put_reg(&inst->operands[2],rm,wm);
        put_immediate(&inst->operands[3],lsb);
    } else if(!strcmp(op,"ccmp") || !strcmp(op,"ccmn")) {
        char tok[4][384];
        unsigned n,rn,rm,cc;
        bool wide,otherwide,sp;
        int64_t ccmp_immediate,nzcv;
        bool literal;
        if(!split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(aliases,tok[0],&rn,&wide,&sp) || sp ||
           !parse_signed_number(tok[2],&nzcv) || nzcv<0 || nzcv>15 ||
           !a64_condition_code(tok[3],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-compare:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        literal=parse_signed_number(tok[1],&ccmp_immediate);
        if(literal && (ccmp_immediate<0 || ccmp_immediate>31)) {
            minias_set_error(as,"bad-aarch64-ccmp-immediate:%s:line=%zu",
                             tok[1],stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        if(!literal && (!parse_a64_reg(aliases,tok[1],&rm,&otherwide,&sp) ||
                        sp || otherwide!=wide)) {
            minias_set_error(as,"bad-aarch64-ccmp-register:%s:line=%zu",
                             tok[1],stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_COND_COMPARE;inst->count=4U;
        inst->encoding=(!strcmp(op,"ccmp")?UINT32_C(0x7a400000):
                                         UINT32_C(0x3a400000))|
                       (wide?UINT32_C(0x80000000):0U)|
                       (literal?UINT32_C(0x800):0U);
        put_reg(&inst->operands[0],rn,wide);
        if(literal)put_immediate(&inst->operands[1],ccmp_immediate);
        else put_reg(&inst->operands[1],rm,wide);
        put_immediate(&inst->operands[2],nzcv);
        put_immediate(&inst->operands[3],cc);
    } else if(!strcmp(op,"csel") || !strcmp(op,"csinc") ||
              !strcmp(op,"csinv") || !strcmp(op,"csneg")) {
        char tok[4][384];
        unsigned n,rd,rn,rm,cc,i;
        bool wd,wn,wm,sp;
        static const struct {const char *name;uint32_t base;} select[]={
            {"csel",UINT32_C(0x1a800000)},
            {"csinc",UINT32_C(0x1a800400)},
            {"csinv",UINT32_C(0x5a800000)},
            {"csneg",UINT32_C(0x5a800400)}
        };
        if(!split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(aliases,tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(aliases,tok[1],&rn,&wn,&sp) || sp || wd!=wn ||
           !parse_a64_reg(aliases,tok[2],&rm,&wm,&sp) || sp || wd!=wm ||
           !a64_condition_code(tok[3],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-select:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        for(i=0U;i<sizeof(select)/sizeof(select[0]);++i)
            if(!strcmp(op,select[i].name))break;
        if(i==sizeof(select)/sizeof(select[0])) {
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_COND_SELECT;inst->count=3U;
        inst->encoding=select[i].base|(cc<<12U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wd);
        put_reg(&inst->operands[2],rm,wd);
    } else if(!strcmp(op,"cneg") || !strcmp(op,"cinv")) {
        char tok[4][384];unsigned n,rd,rn,cc;
        bool wd,wn,sp;
        if(!split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(aliases,tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(aliases,tok[1],&rn,&wn,&sp) || sp ||
           wd!=wn || !a64_condition_code(tok[2],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-alias:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        /* GNU CNEG/CINV invert their condition and duplicate Rn in
         * CSNEG/CSINV. Reuse the identical typed conditional selector
         * rather than adding another ISA/MC output path. */
        inst->opcode=A64_MC_COND_SELECT;inst->count=3U;
        inst->encoding=(!strcmp(op,"cneg")?UINT32_C(0x5a800400):
                                             UINT32_C(0x5a800000))|
                       ((cc^1U)<<12U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wd);
        put_reg(&inst->operands[2],rn,wd);
    } else if(!strcmp(op,"cset") || !strcmp(op,"csetm") ||
       !strcmp(op,"cinc")) {
        char parts[4][384];
        unsigned n,rd,rn=31U,cond;
        bool wd,wn=false,sp;
        if(!split_operands(args,parts,&n) ||
           n!=(!strcmp(op,"cinc")?3U:2U) ||
           !parse_a64_reg(aliases,parts[0],&rd,&wd,&sp) || sp ||
           (!strcmp(op,"cinc") &&
            (!parse_a64_reg(aliases,parts[1],&rn,&wn,&sp) || sp || wn!=wd)) ||
           !a64_condition_code(parts[n-1U],&cond)) {
            minias_set_error(as,"bad-aarch64-csel-alias:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_CSEL_ALIAS;
        inst->count=2U;
        inst->encoding=(cond^1U) |
           (!strcmp(op,"csetm")?0x100U:0U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wd);
    } else if((!strcmp(op,"mvn") || !strcmp(op,"not")) &&
              a64_register_prefix(aliases,args)=='v') {
        /* AdvSIMD MVN/NOT is a two-register 8B/16B operation, not the
         * scalar GPR MVN (ORN with ZR). Reuse the established two-register
         * typed encoder used by native ARM crypto register instructions. */
        char tok[4][384];unsigned n,rd,rn;
        uint32_t da,sa;
        if(!split_operands(args,tok,&n) || n!=2U ||
           !a64_vector_view(aliases,tok[0],&rd,&da) ||
           !a64_vector_view(aliases,tok[1],&rn,&sa) || da!=sa ||
           (da&UINT32_C(0xc00))!=0U) {
            minias_set_error(as,"bad-aarch64-simd-mvn:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_CRYPTO;inst->count=2U;
        inst->encoding=UINT32_C(0x2e205800)|
                       (da&UINT32_C(0x40000000));
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
    } else if(!strcmp(op,"sminv") || !strcmp(op,"smaxv") ||
              !strcmp(op,"uminv") || !strcmp(op,"umaxv") ||
              !strcmp(op,"addv")) {
        /* All horizontal integer vector reductions share one typed
         * 2-register AdvSIMD emitter, with actual B/H/S destination bank
         * and source Q/element legality. GNU GHASH uses SMINV B0,V6.16B. */
        static const struct {const char *name;uint32_t base;} reductions[]={
            {"sminv",UINT32_C(0x0e31a800)},
            {"smaxv",UINT32_C(0x0e30a800)},
            {"uminv",UINT32_C(0x2e31a800)},
            {"umaxv",UINT32_C(0x2e30a800)},
            {"addv", UINT32_C(0x0e31b800)}
        };
        char tok[4][384],*end,resolved[96];unsigned n,rd,rn,k;
        unsigned long dst_number;
        uint32_t arrange;
        unsigned elem;
        const char *dst;
        if(!split_operands(args,tok,&n) || n!=2U ||
           !a64_vector_view(aliases,tok[1],&rn,&arrange))goto bad_reduction;
        dst=a64_resolve_register_alias(aliases,tok[0],resolved,sizeof(resolved));
        if((dst[0]!='b' && dst[0]!='h' && dst[0]!='s') ||
           !isdigit((unsigned char)dst[1]))goto bad_reduction;
        errno=0;dst_number=strtoul(dst+1U,&end,10);
        if(errno || *end!='\0' || dst_number>31UL)goto bad_reduction;
        elem=dst[0]=='b'?0U:dst[0]=='h'?1U:2U;
        if(((arrange&UINT32_C(0xc00))>>10U)!=elem ||
           (elem==2U && !(arrange&UINT32_C(0x40000000))))
            goto bad_reduction;
        rd=(unsigned)dst_number;
        for(k=0U;k<sizeof(reductions)/sizeof(reductions[0]);++k)
            if(!strcmp(op,reductions[k].name))break;
        inst->opcode=A64_MC_SIMD_CRYPTO;inst->count=2U;
        inst->encoding=reductions[k].base|
                       (arrange&UINT32_C(0x40000000))|
                       ((arrange&UINT32_C(0xc00))<<12U);
        put_fp_scalar(&inst->operands[0],rd,false);
        put_vector(&inst->operands[1],rn);
        goto reduction_done;
bad_reduction:
        minias_set_error(as,"bad-aarch64-simd-reduce:%s:line=%zu",
                         args,stmt->line);
        minias_mcinst_destroy(inst);return false;
reduction_done:;
    } else if(!strcmp(op,"mvn") &&
              split_two(args,first,sizeof(first),second,sizeof(second))) {
        unsigned rd,rm;bool wide,otherwide,sp;
        if(!parse_a64_reg(aliases,first,&rd,&wide,&sp) || sp ||
           !parse_a64_reg(aliases,second,&rm,&otherwide,&sp) || sp ||
           wide!=otherwide) {
            minias_set_error(as,"bad-aarch64-mvn:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_MVN_REG;inst->count=2U;
        put_reg(&inst->operands[0],rd,wide);
        put_reg(&inst->operands[1],rm,wide);
    } else if(!strcmp(op,"ic") || !strcmp(op,"dc") ||
              !strcmp(op,"tlbi") || !strcmp(op,"at")) {
        /* System cache/TLB maintenance is one typed SYS instruction
         * family (op0/op1/CRn/CRm/op2 and optional Xt). Only GNU-verified
         * architectural names are accepted; no permissive SYS escape.
         * Encoding and operand policy remain AArch64 Target-owned. */
        static const struct {const char *family,*name;
                              uint32_t word;bool needs_rt;} sysops[]={
            {"ic","ialluis",UINT32_C(0xd508711f),false},
            {"ic","iallu",UINT32_C(0xd508751f),false},
            {"ic","ivau",UINT32_C(0xd50b7520),true},
            {"dc","ivac",UINT32_C(0xd5087620),true},
            {"dc","cvac",UINT32_C(0xd50b7a20),true},
            {"dc","cvau",UINT32_C(0xd50b7b20),true},
            {"dc","civac",UINT32_C(0xd50b7e20),true},
            {"dc","zva",UINT32_C(0xd50b7420),true},
            {"dc","gzva",UINT32_C(0xd50b7480),true},
            /* Architectural AT SYS opcodes use the existing Rt/ELF model. */
            {"at","s1e1r",UINT32_C(0xd5087800),true},
            {"at","s1e1w",UINT32_C(0xd5087820),true},
            {"at","s1e0r",UINT32_C(0xd5087840),true},
            {"at","s1e0w",UINT32_C(0xd5087860),true},
            {"at","s1e2r",UINT32_C(0xd50c7800),true},
            {"at","s1e2w",UINT32_C(0xd50c7820),true},
            {"at","s12e1r",UINT32_C(0xd50c7880),true},
            {"at","s12e1w",UINT32_C(0xd50c78a0),true},
            {"at","s1e3r",UINT32_C(0xd50e7800),true},
            {"at","s1e3w",UINT32_C(0xd50e7820),true},
            {"tlbi","vmalle1",UINT32_C(0xd508871f),false},
            {"tlbi","vmalle1is",UINT32_C(0xd508831f),false},
            {"tlbi","alle1",UINT32_C(0xd50c879f),false},
            {"tlbi","alle1is",UINT32_C(0xd50c839f),false},
            {"tlbi","vae1",UINT32_C(0xd5088720),true},
            {"tlbi","vae1is",UINT32_C(0xd5088320),true},
            {"tlbi","vale1",UINT32_C(0xd50887a0),true},
            {"tlbi","vale1is",UINT32_C(0xd50883a0),true},
            {"tlbi","vale2is",UINT32_C(0xd50c83a0),true},
            {"tlbi","rvae1is",UINT32_C(0xd5088220),true},
            {"tlbi","rvale1is",UINT32_C(0xd50882a0),true},
            {"tlbi","rvae1",UINT32_C(0xd5088620),true},
            {"tlbi","rvale1",UINT32_C(0xd50886a0),true},
            {"tlbi","vaae1",UINT32_C(0xd5088760),true},
            {"tlbi","vaae1is",UINT32_C(0xd5088360),true},
            {"tlbi","vaale1",UINT32_C(0xd50887e0),true},
            {"tlbi","vaale1is",UINT32_C(0xd50883e0),true},
            {"tlbi","aside1",UINT32_C(0xd5088740),true},
            {"tlbi","aside1is",UINT32_C(0xd5088340),true},
            {"tlbi","alle2",UINT32_C(0xd50c871f),false},
            {"tlbi","alle2is",UINT32_C(0xd50c831f),false},
            {"tlbi","ipas2e1",UINT32_C(0xd50c8420),true},
            {"tlbi","ipas2e1is",UINT32_C(0xd50c8020),true},
            {"tlbi","ripas2e1is",UINT32_C(0xd50c8040),true},
            {"tlbi","ripas2le1is",UINT32_C(0xd50c80c0),true},
            {"tlbi","ripas2e1",UINT32_C(0xd50c8440),true},
            {"tlbi","ripas2le1",UINT32_C(0xd50c84c0),true},
            {"tlbi","vmalls12e1",UINT32_C(0xd50c87df),false},
            {"tlbi","vmalls12e1is",UINT32_C(0xd50c83df),false},
            {"tlbi","vae2",UINT32_C(0xd50c8720),true},
            {"tlbi","vae2is",UINT32_C(0xd50c8320),true},
            {"tlbi","alle3",UINT32_C(0xd50e871f),false},
            {"tlbi","alle3is",UINT32_C(0xd50e831f),false},
            {"tlbi","vae3",UINT32_C(0xd50e8720),true},
            {"tlbi","vae3is",UINT32_C(0xd50e8320),true}
        };
        char operation[48],operand[48];
        unsigned rt=31U;
        bool rt64=false,rtsp=false,paired=false,found=false;
        uint32_t word=0U;
        const char *requested=args;
        unsigned i;
        if(split_two(args,operation,sizeof(operation),
                     operand,sizeof(operand))) {
            paired=true;requested=operation;
        }
        for(i=0U;i<sizeof(sysops)/sizeof(sysops[0]);++i) {
            if(strcmp(op,sysops[i].family) ||
               strcmp(requested,sysops[i].name))continue;
            if(sysops[i].needs_rt!=paired)break;
            if(!strcmp(sysops[i].name,"gzva") &&
               (a64_features(as)&A64_FEATURE_MTE)==0U)break;
            /* ARMv8.4 range TLBI requires an explicit architectural
             * profile opt-in. It is not available under baseline v8-a. */
            if((!strncmp(sysops[i].name,"rv",2U) ||
                !strncmp(sysops[i].name,"ripas2",6U)) &&
               (a64_features(as)&A64_FEATURE_TLBI_RANGE)==0U)break;
            if(paired && (!parse_a64_reg(aliases,operand,&rt,&rt64,&rtsp) ||
                          !rt64 || rtsp))break;
            word=sysops[i].word;found=true;break;
        }
        if(!found) {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SYS_OP;inst->count=1U;
        inst->encoding=word&~UINT32_C(31);
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"msr") &&
              split_two(args,first,sizeof(first),second,sizeof(second)) &&
              (!strcmp(first,"SPsel") || !strcmp(first,"spsel")) &&
              (second[0]=='#' || second[0]=='-' ||
               isdigit((unsigned char)second[0]))) {
        int64_t value;
        if(!parse_signed_number(second,&value) || value<0 || value>1) {
            minias_set_error(as,"bad-aarch64-spsel-immediate:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SYSTEM_IMM;inst->count=1U;
        inst->encoding=UINT32_C(0xd50040bf);
        put_immediate(&inst->operands[0],value);
    } else if(!strcmp(op,"msr") &&
              split_two(args,first,sizeof(first),second,sizeof(second)) &&
              (!strcmp(first,"daifset") || !strcmp(first,"daifclr"))) {
        int64_t daif_immediate;
        if(!parse_signed_number(second,&daif_immediate) ||
           daif_immediate<0 || daif_immediate>15) {
            minias_set_error(as,"bad-aarch64-daif-immediate:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SYSTEM_IMM;inst->count=1U;
        inst->encoding=!strcmp(first,"daifset")?
                       UINT32_C(0xd50340df):UINT32_C(0xd50340ff);
        put_immediate(&inst->operands[0],daif_immediate);
    } else if((!strcmp(op,"mrs") || !strcmp(op,"msr")) &&
       split_two(args, first, sizeof(first), second, sizeof(second))) {
        unsigned rt;
        bool wide,sp=false;
        uint32_t mrscode=0U,msrcode=0U;
        bool read=!strcmp(op,"mrs");
        /* Rt=31 is legal XZR for MRS/MSR, unlike SP. Use the
         * existing target register grammar rather than x0..x30 only. */
        if(read ?
           (!parse_a64_reg(aliases,first,&rt,&wide,&sp) || sp || !wide ||
            !a64_system_register(second,&mrscode,&msrcode) ||
            mrscode==0U) :
           (!parse_a64_reg(aliases,second,&rt,&wide,&sp) || sp || !wide ||
            !a64_system_register(first,&mrscode,&msrcode) ||
            msrcode==0U)) {
            minias_set_error(as,"bad-aarch64-sysreg:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=read?A64_MC_MRS:A64_MC_MSR;
        inst->count=1U;
        inst->encoding=read?mrscode:msrcode;
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"dup") ||
              ((!strcmp(op,"rev32") || !strcmp(op,"rev64")) &&
               a64_register_prefix(aliases,args)=='v')) {
        char dst[96],source[96];unsigned vd,rn;
        uint32_t ar;
        bool wide,sp;
        if(!split_two(args,dst,sizeof(dst),source,sizeof(source)) ||
           !a64_vector_view(aliases,dst,&vd,&ar)) {
            minias_set_error(as,"bad-aarch64-simd-lane:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        if(!strcmp(op,"dup")) {
            bool d=((ar&UINT32_C(0xc00))==UINT32_C(0xc00));
            unsigned lane=1U<<((ar&UINT32_C(0xc00))>>10U);
            if((d && !(ar&UINT32_C(0x40000000))) ||
               !parse_a64_reg(aliases,source,&rn,&wide,&sp) || sp || wide!=d) {
                minias_set_error(as,"bad-aarch64-dup-source:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
            inst->encoding=UINT32_C(0x0e000c00)|
                    (ar&UINT32_C(0x40000000))|(lane<<16U);
        }else {
            uint32_t element=ar&UINT32_C(0xc00);
            uint32_t vdarr;
            bool rev64=!strcmp(op,"rev64");
            if((rev64?(element!=0U && element!=UINT32_C(0x400) &&
                         element!=UINT32_C(0x800)):
                       (element!=0U && element!=UINT32_C(0x400))) ||
               !a64_vector_view(aliases,source,&rn,&vdarr) || vdarr!=ar) {
                minias_set_error(as,"bad-aarch64-reverse-vector:%s:line=%zu",
                                 args,stmt->line);minias_mcinst_destroy(inst);return false;
            }
            inst->opcode=A64_MC_SIMD_REV32;inst->count=2U;
            inst->encoding=(rev64?UINT32_C(0x0e200800):
                                  UINT32_C(0x2e200800))|
                     (ar&UINT32_C(0x40000000))|(element<<12U);
        }
        put_vector(&inst->operands[0],vd);
        if(!strcmp(op,"dup"))put_reg(&inst->operands[1],rn,true);
        else put_vector(&inst->operands[1],rn);
    } else if(!strcmp(op,"sys")) {
        uint32_t bits;unsigned rt;
        if(!a64_sys_fields(aliases,args,&bits,&rt)) {
            minias_set_error(as,"bad-aarch64-sys-fields:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SYS_OP;inst->count=1U;
        inst->encoding=bits;
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"casp") || !strcmp(op,"caspa") ||
              !strcmp(op,"caspl") || !strcmp(op,"caspal")) {
        unsigned rs,rt;
        A64Memory memory;
        bool wide;
        if((a64_features(as)&A64_FEATURE_LSE)==0U ||
           !a64_casp_fields(as,aliases,args,&rs,&rt,&memory,&wide)){
            minias_set_error(as,"bad-aarch64-casp:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_LSE_PAIR;inst->count=3U;
        inst->encoding=(wide?UINT32_C(0x48207c00):
                             UINT32_C(0x08207c00))|
                       ((!strcmp(op,"caspa") ||
                         !strcmp(op,"caspal"))?
                         UINT32_C(0x00400000):0U)|
                       (!strcmp(op,"caspl") || !strcmp(op,"caspal")?
                        UINT32_C(0x00008000):0U);
        put_reg(&inst->operands[0],rs,wide);
        put_reg(&inst->operands[1],rt,wide);
        memory.access_bytes=wide?16U:8U;
        if(!a64_attach_memory(as,inst,2U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"ldg")) {
        /* Genuine Linux AArch64 MTE LDG: signed scaled imm9 and ordinary
         * Xn/SP base. Share the typed MTE encoding and ELF code writer. */
        char tok[4][384];unsigned n,rt;bool wide,sp;
        A64Memory memory;
        /* GNU [Xn,#offset] contains a nested comma, so split on A64's
         * top-level operand grammar, NOT the old single-comma split_two. */
        if((a64_features(as)&A64_FEATURE_MTE)==0U ||
           !split_operands(args,tok,&n) || n!=2U ||
           !parse_a64_reg(aliases,tok[0],&rt,&wide,&sp) || sp || !wide ||
           !a64_parse_memory(as,aliases,tok[1],NULL,&memory) ||
           memory.mode!=A64_ADDRESS_OFFSET ||
           (memory.offset.kind==A64_OFFSET_CONSTANT &&
            (memory.offset.constant%16!=0 ||
             memory.offset.constant<-4096 ||
             memory.offset.constant>4080))) {
            minias_set_error(as,"bad-aarch64-ldg:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        memory.access_bytes=16U;
        inst->opcode=A64_MC_MTE_STGM;inst->count=2U;
        inst->encoding=UINT32_C(0xd9600000);
        put_reg(&inst->operands[0],rt,true);
        if(!a64_attach_memory(as,inst,1U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"stgm") || !strcmp(op,"ldgm")) {
        /* One typed MTE2 register+base family, no second instruction or
         * ELF emitter. GNU/Arm encode LDGM with 0xd9e00000. */
        char tok[4][384];unsigned n,rt;
        bool wide,sp;
        A64Memory memory;
        if((a64_features(as)&A64_FEATURE_MTE)==0U ||
           !split_operands(args,tok,&n) || n!=2U ||
           !parse_a64_reg(aliases,tok[0],&rt,&wide,&sp) || sp || !wide ||
           !a64_parse_memory(as,aliases,tok[1],NULL,&memory) ||
           !a64_is_plain_base(&memory)) {
            minias_set_error(as,"bad-aarch64-mte-tags:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_MTE_STGM;inst->count=2U;
        inst->encoding=!strcmp(op,"ldgm")?
                       UINT32_C(0xd9e00000):UINT32_C(0xd9a00000);
        put_reg(&inst->operands[0],rt,true);
        memory.access_bytes=16U;
        if(!a64_attach_memory(as,inst,1U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"ins") ||
              (!strcmp(op,"mov") && a64_register_prefix(aliases,args)=='v' &&
               strchr(args,'[')!=NULL)) {
        /* GNU MOV Vd.element[lane],Wn/Xn is INS's exact lane encoding,
         * not the earlier whole-register MOV/ORR alias. */
        unsigned vd,rn;
        uint32_t imm5,source_imm5=0U,source_marker=0U,source_imm4=0U;
        bool source_x,wide,sp,vector_source;
        if(!split_two(args,first,sizeof(first),second,sizeof(second)) ||
           !a64_ins_lane(aliases,first,&vd,&imm5,&source_x)) {
            minias_set_error(as,"bad-aarch64-ins-lane:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        vector_source=a64_ins_lane(aliases,second,&rn,&source_imm5,&wide);
        if(vector_source) {
            /* GNU INS/MOV Vd.element[i],Vn.element[j]: same element
             * width but independent lane indices. Destination imm5
             * and source imm4 live in separate architectural fields. */
            source_marker=source_imm5&(~source_imm5+1U);
            if(source_marker!=(imm5&(~imm5+1U))) {
                minias_set_error(as,"bad-aarch64-ins-source-width:%s:line=%zu",
                                 args,stmt->line);minias_mcinst_destroy(inst);return false;
            }
            source_imm4=(source_imm5&~source_marker)>>1U;
        }else if(!parse_a64_reg(aliases,second,&rn,&wide,&sp) || sp ||
                 wide!=source_x) {
            minias_set_error(as,"bad-aarch64-ins-source:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
        inst->encoding=(vector_source?UINT32_C(0x6e000400):
                                     UINT32_C(0x4e001c00))|
                       (imm5<<16U)|(source_imm4<<11U);
        put_vector(&inst->operands[0],vd);
        if(vector_source)put_vector(&inst->operands[1],rn);
        else put_reg(&inst->operands[1],rn,wide);
    } else if(!strcmp(op,"fmov")) {
        /* Raw bitwise FMOV between GPR and FP S/D banks (both directions).
         * No software float conversion, and never accept mixed X/S W/D. */
        char dst[96],source[96];unsigned rd,rn;
        bool fp_dst,fp_src,fd=false,fs=false,gd=false,gs=false,sp;
        if(!split_two(args,dst,sizeof(dst),source,sizeof(source))) {
            minias_set_error(as,"bad-aarch64-fmov:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        fp_dst=a64_scalar_fp_reg(aliases,dst,&rd,&fd);
        fp_src=a64_scalar_fp_reg(aliases,source,&rn,&fs);
        if(fp_dst==fp_src ||
           (fp_dst && (!parse_a64_reg(aliases,source,&rn,&gs,&sp) || sp ||
                       fd!=gs)) ||
           (fp_src && (!parse_a64_reg(aliases,dst,&rd,&gd,&sp) || sp ||
                       fs!=gd))) {
            minias_set_error(as,"bad-aarch64-fmov:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
        inst->encoding=fp_dst?(fd?UINT32_C(0x9e670000):
                                     UINT32_C(0x1e270000)):
                               (fs?UINT32_C(0x9e660000):
                                     UINT32_C(0x1e260000));
        if(fp_dst) {
            put_fp_scalar(&inst->operands[0],rd,fd);
            put_reg(&inst->operands[1],rn,gs);
        } else {
            put_reg(&inst->operands[0],rd,gd);
            put_fp_scalar(&inst->operands[1],rn,fs);
        }
    } else if(!strcmp(op,"ushr") || !strcmp(op,"sshr") ||
              !strcmp(op,"sri") || !strcmp(op,"shl") ||
              !strcmp(op,"sli")) {
        /* One AdvSIMD shift-immediate ISA family, not five parsers.
         * Right shifts use immh:immb=2*element_bits-shift; left shifts
         * use element_bits+shift. Preserve signed/unsigned and insert
         * semantics in the target opcode base, not the Common MC layout. */
        char tok[4][384];unsigned n,rd,rn,bits,encoded_shift;
        uint32_t da,sa,base;
        int64_t shift;
        bool right=!strcmp(op,"ushr") || !strcmp(op,"sshr") ||
                   !strcmp(op,"sri");
        if(!split_operands(args,tok,&n) || n!=3U ||
           !a64_vector_view(aliases,tok[0],&rd,&da) ||
           !a64_vector_view(aliases,tok[1],&rn,&sa) || da!=sa ||
           !parse_signed_number(tok[2],&shift)) {
            minias_set_error(as,"bad-aarch64-simd-shift:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        bits=8U<<((da&UINT32_C(0xc00))>>10U);
        if((right?(shift<1 || shift>(int64_t)bits):
                   (shift<0 || shift>=(int64_t)bits)) ||
           (bits==64U && (da&UINT32_C(0x40000000))==0U)) {
            minias_set_error(as,"bad-aarch64-simd-shift-range:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        encoded_shift=right?2U*bits-(unsigned)shift:bits+(unsigned)shift;
        base=!strcmp(op,"ushr")?UINT32_C(0x2f000400):
             !strcmp(op,"sshr")?UINT32_C(0x0f000400):
             !strcmp(op,"sri") ?UINT32_C(0x2f004400):
             !strcmp(op,"shl") ?UINT32_C(0x0f005400):
                                UINT32_C(0x2f005400);
        inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
        inst->encoding=base|(da&UINT32_C(0x40000000))|
                       (encoded_shift<<16U);
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
    } else if(!strcmp(op,"dmb") || !strcmp(op,"dsb") ||
              !strcmp(op,"isb")) {
        unsigned barrier=15U;
        bool is_isb=!strcmp(op,"isb");
        /* Three barrier opcodes share typed MCInst; ISB supports SY. */
        if(is_isb) {
            if(*args && strcmp(args,"sy")!=0 && strcmp(args,"#15")!=0 &&
               strcmp(args,"15")!=0) {
                minias_set_error(as,"bad-aarch64-isb:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
        } else if(!a64_barrier_option(args,&barrier)) {
            minias_set_error(as,"bad-aarch64-barrier:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_BARRIER;
        inst->encoding=(is_isb?UINT32_C(0xd50330df):
                        !strcmp(op,"dsb")?UINT32_C(0xd503309f):
                                          UINT32_C(0xd50330bf)) |
                       (barrier<<8U);

    } else if(!strncmp(op,"crc32",5U)) {
        /* Eight architectural CRC32/CRC32C width forms use one ALU
         * machine word with indexed Rm/Rn/Rd; CRC32X and CRC32CX
         * require X source, but W accumulator and W destination. */
        static const struct {const char *name;uint32_t base;bool xsrc;} crc[]={
            {"crc32b",UINT32_C(0x1ac04000),false},
            {"crc32h",UINT32_C(0x1ac04400),false},
            {"crc32w",UINT32_C(0x1ac04800),false},
            {"crc32x",UINT32_C(0x9ac04c00),true},
            {"crc32cb",UINT32_C(0x1ac05000),false},
            {"crc32ch",UINT32_C(0x1ac05400),false},
            {"crc32cw",UINT32_C(0x1ac05800),false},
            {"crc32cx",UINT32_C(0x9ac05c00),true}
        };
        char tok[4][384];unsigned n,rd,rn,rm,k;
        bool wd,wn,wm,sp;
        if((a64_features(as)&A64_FEATURE_CRC)==0U ||
           !split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(aliases,tok[0],&rd,&wd,&sp) || sp || wd ||
           !parse_a64_reg(aliases,tok[1],&rn,&wn,&sp) || sp || wn ||
           !parse_a64_reg(aliases,tok[2],&rm,&wm,&sp) || sp) {
            minias_set_error(as,"bad-aarch64-crc32:%s:line=%zu",args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        for(k=0U;k<sizeof(crc)/sizeof(crc[0]);++k)
            if(!strcmp(op,crc[k].name))break;
        if(k==sizeof(crc)/sizeof(crc[0]) ||
           wm!=crc[k].xsrc) {
            minias_set_error(as,"bad-aarch64-crc-width:%s:line=%zu",args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_ARITH_ALIAS;inst->count=3U;
        inst->encoding=crc[k].base;
        put_reg(&inst->operands[0],rd,false);
        put_reg(&inst->operands[1],rn,false);
        put_reg(&inst->operands[2],rm,wm);
    } else if(!strcmp(op,"aese") || !strcmp(op,"aesd") ||
              !strcmp(op,"aesmc") || !strcmp(op,"aesimc")) {
        static const struct {const char *name;uint32_t base;} aes[]={
            {"aese",UINT32_C(0x4e284800)},
            {"aesd",UINT32_C(0x4e285800)},
            {"aesmc",UINT32_C(0x4e286800)},
            {"aesimc",UINT32_C(0x4e287800)}
        };
        char tok[4][384];unsigned n,rd,rn,k;
        uint32_t da,sa;
        if((a64_features(as)&A64_FEATURE_CRYPTO)==0U ||
           !split_operands(args,tok,&n) || n!=2U ||
           !a64_vector_view(aliases,tok[0],&rd,&da) ||
           !a64_vector_view(aliases,tok[1],&rn,&sa) ||
           da!=UINT32_C(0x40000000) || sa!=da) {
            minias_set_error(as,"bad-aarch64-aes:%s:line=%zu",args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        for(k=0U;k<sizeof(aes)/sizeof(aes[0]);++k)
            if(!strcmp(op,aes[k].name))break;
        inst->opcode=A64_MC_SIMD_CRYPTO;inst->count=2U;
        inst->encoding=aes[k].base;
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
    } else if(!strcmp(op,"eor") || !strcmp(op,"orr") ||
              !strcmp(op,"and") || !strcmp(op,"bic") ||
              !strcmp(op,"bsl") || !strcmp(op,"bit") ||
              !strcmp(op,"bif") ||
              (!strcmp(op,"add") && a64_register_prefix(aliases,args)=='v') ||
              !strcmp(op,"cmeq") || !strcmp(op,"cmgt") ||
              !strcmp(op,"cmge") || !strcmp(op,"cmhi") ||
              !strcmp(op,"cmhs") || !strcmp(op,"cmtst") ||
              !strcmp(op,"trn1") || !strcmp(op,"trn2") ||
              !strcmp(op,"uzp1") || !strcmp(op,"uzp2") ||
              !strcmp(op,"zip1") || !strcmp(op,"zip2")) {
        char tok[4][384];unsigned n,rd,rn,rm,i;
        uint32_t da,sa,ta;
        static const struct {const char *name;uint32_t base;} table[]={
            {"eor",UINT32_C(0x2e201c00)},
            {"orr",UINT32_C(0x0ea01c00)},
            {"and",UINT32_C(0x0e201c00)},
            {"bic",UINT32_C(0x0e601c00)},
            {"bsl",UINT32_C(0x2e601c00)},
            {"bit",UINT32_C(0x2ea01c00)},
            {"bif",UINT32_C(0x2ee01c00)},
            {"add",UINT32_C(0x0e208400)},
            /* Existing typed three-register AdvSIMD emitter owns all six
             * compare-register opcodes and a single size/Q checker. */
            {"cmeq",UINT32_C(0x2e208c00)},
            {"cmgt",UINT32_C(0x0e203400)},
            {"cmge",UINT32_C(0x0e203c00)},
            {"cmhi",UINT32_C(0x2e203400)},
            {"cmhs",UINT32_C(0x2e203c00)},
            {"cmtst",UINT32_C(0x0e208c00)},
            {"trn1",UINT32_C(0x0e002800)},
            {"trn2",UINT32_C(0x0e006800)},
            {"uzp1",UINT32_C(0x0e001800)},
            {"uzp2",UINT32_C(0x0e005800)},
            {"zip1",UINT32_C(0x0e003800)},
            {"zip2",UINT32_C(0x0e007800)}
        };
        if(split_operands(args,tok,&n) && n==3U &&
           a64_register_prefix(aliases,tok[0])=='v') {
            bool permute=!strcmp(op,"trn1") || !strcmp(op,"trn2") ||
                         !strcmp(op,"uzp1") || !strcmp(op,"uzp2") ||
                         !strcmp(op,"zip1") || !strcmp(op,"zip2");
            bool compare=!strcmp(op,"cmeq") || !strcmp(op,"cmgt") ||
                         !strcmp(op,"cmge") || !strcmp(op,"cmhi") ||
                         !strcmp(op,"cmhs") || !strcmp(op,"cmtst");
            bool sized=permute || compare || !strcmp(op,"add");
            if(!a64_vector_view(aliases,tok[0],&rd,&da) ||
               !a64_vector_view(aliases,tok[1],&rn,&sa) ||
               !a64_vector_view(aliases,tok[2],&rm,&ta) ||
               da!=sa || da!=ta ||
               (sized?
                 ((da&UINT32_C(0xc00))==UINT32_C(0xc00) &&
                  (da&UINT32_C(0x40000000))==0U):
                 (da&UINT32_C(0xc00))!=0U)) {
                minias_set_error(as,"bad-aarch64-simd-logic:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            for(i=0U;i<sizeof(table)/sizeof(table[0]);++i)
                if(!strcmp(op,table[i].name))break;
            inst->opcode=A64_MC_SIMD_LOGIC;inst->count=3U;
            inst->encoding=table[i].base|
                          (da&UINT32_C(0x40000000))|
                          (sized?
                          ((da&UINT32_C(0xc00))<<12U):0U);
            put_vector(&inst->operands[0],rd);
            put_vector(&inst->operands[1],rn);
            put_vector(&inst->operands[2],rm);
        }else if(parse_integer_family(as,aliases,inst,op,args)) {
            /* Scalar ALU uses the same already proven Target machinery. */
        }else {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"ext")) {
        char tok[4][384];unsigned n,rd,rn,rm;
        uint32_t da,sa,ta;
        int64_t index;
        if(!split_operands(args,tok,&n) || n!=4U ||
           !a64_vector_view(aliases,tok[0],&rd,&da) ||
           !a64_vector_view(aliases,tok[1],&rn,&sa) ||
           !a64_vector_view(aliases,tok[2],&rm,&ta) ||
           da!=sa || da!=ta || (da&UINT32_C(0xc00))!=0U ||
           !parse_signed_number(tok[3],&index) || index<0 ||
           index>=((da&UINT32_C(0x40000000))?16:8)) {
            minias_set_error(as,"bad-aarch64-ext:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_EXT;inst->count=4U;
        inst->encoding=UINT32_C(0x2e000000)|
                      (da&UINT32_C(0x40000000))|
                      ((uint32_t)index<<11U);
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
        put_vector(&inst->operands[2],rm);
    } else if(!strcmp(op,"movi")) {
        char vreg[96],text[96];unsigned rd;
        uint32_t ar,base;
        int64_t number;
        unsigned simd_imm;
        if(!split_two(args,vreg,sizeof(vreg),text,sizeof(text)) ||
           !a64_vector_view(aliases,vreg,&rd,&ar) ||
           !parse_signed_number(text,&number)) {
            minias_set_error(as,"bad-aarch64-movi:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        /* GNU MOVI Vd.2D, #bitmask64 forms encode one bit per repeated
         * 64-bit byte: each byte MUST be 0x00 or 0xff. Reuse existing
         * MOVI imm8 field emitter for all legal patterns, not a single
         * hard-coded kernel #0xffffffff special case. */
        if(ar==UINT32_C(0x40000c00)) {
            uint64_t bits=(uint64_t)number;
            unsigned k;
            simd_imm=0U;
            for(k=0U;k<8U;++k) {
                uint8_t octet=(uint8_t)(bits>>(8U*k));
                if(octet!=0U && octet!=UINT8_MAX) {
                    minias_set_error(as,"bad-aarch64-movi-2d-mask:%s:line=%zu",
                                     args,stmt->line);
                    minias_mcinst_destroy(inst);return false;
                }
                if(octet==UINT8_MAX)simd_imm|=1U<<k;
            }
            base=UINT32_C(0x6f00e400);
        }else if(number>=0 && number<=255 &&
                 (ar&UINT32_C(0xc00))==0U) {
            base=UINT32_C(0x0f00e400);
            simd_imm=(unsigned)number;
        }else if(number>=0 && number<=255 &&
                 (ar&UINT32_C(0xc00))==UINT32_C(0x800)) {
            base=UINT32_C(0x0f000400);
            simd_imm=(unsigned)number;
        }else {
            minias_set_error(as,"bad-aarch64-movi-size:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_IMM;inst->count=1U;
        inst->encoding=base|
                      (ar&UINT32_C(0x40000000))|
                      ((simd_imm>>5U)<<16U)|((simd_imm&31U)<<5U);
        put_vector(&inst->operands[0],rd);
    } else if(!strcmp(op,"ldxp") || !strcmp(op,"ldaxp") ||
              !strcmp(op,"stxp") || !strcmp(op,"stlxp")) {
        char tok[4][384];unsigned n,k,rt,rt2,rs=31U;
        bool wt,wt2,ws,sp,store=op[0]=='s';
        A64Memory memory;
        if(!split_operands(args,tok,&n) ||
           n!=(store?4U:3U)) {
            minias_set_error(as,"bad-aarch64-exclusive-pair:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        k=store?1U:0U;
        if((store && (!parse_a64_reg(aliases,tok[0],&rs,&ws,&sp) || sp || ws)) ||
           !parse_a64_reg(aliases,tok[k],&rt,&wt,&sp) || sp ||
           !parse_a64_reg(aliases,tok[k+1U],&rt2,&wt2,&sp) || sp ||
           wt!=wt2 ||
           !a64_parse_memory(as,aliases,tok[k+2U],NULL,&memory) ||
           !a64_is_plain_base(&memory)) {
            minias_set_error(as,"bad-aarch64-exclusive-pair:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_EXCLUSIVE_PAIR;inst->count=4U;
        inst->encoding=(wt?UINT32_C(0xc8200000):
                            UINT32_C(0x88200000))|
                       (store?0U:UINT32_C(0x005f0000))|
                       ((!strcmp(op,"ldaxp") || !strcmp(op,"stlxp"))?
                         UINT32_C(0x8000):0U);
        put_reg(&inst->operands[0],rt,wt);
        put_reg(&inst->operands[1],rt2,wt);
        put_reg(&inst->operands[2],rs,false);
        memory.access_bytes=wt?16U:8U;
        if(!a64_attach_memory(as,inst,3U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if(!strcmp(op,"tbl") || !strcmp(op,"tbx")) {
        /* GNU vector table instructions use contiguous 1..4 vector
         * registers, all Vn.16B; Q controls destination/index 8B/16B.
         * Reuse a64_structure_vlist (also used by LD1/ST1), then emit
         * through the existing 3-register SIMD typed encoder. */
        char tok[4][384];unsigned n,rd,rn,rm,count;
        uint32_t dstarr,tabarr,indexarr;
        if(!split_operands(args,tok,&n) || n!=3U ||
           !a64_vector_view(aliases,tok[0],&rd,&dstarr) ||
           !a64_structure_vlist(aliases,tok[1],&rn,&tabarr,&count) ||
           !a64_vector_view(aliases,tok[2],&rm,&indexarr) ||
           (dstarr!=UINT32_C(0) &&
            dstarr!=UINT32_C(0x40000000)) ||
           tabarr!=UINT32_C(0x40000000) ||
           indexarr!=dstarr || count<1U || count>4U) {
            minias_set_error(as,"bad-aarch64-table-lookup:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_LOGIC;inst->count=3U;
        inst->encoding=UINT32_C(0x0e000000)|
                      (dstarr&UINT32_C(0x40000000))|
                      ((count-1U)<<13U)|
                      (!strcmp(op,"tbx")?UINT32_C(0x1000):0U);
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
        put_vector(&inst->operands[2],rm);
    } else if(!strcmp(op,"pmull") || !strcmp(op,"pmull2") ||
              !strcmp(op,"sha1su0") || !strcmp(op,"sha1su1")) {
        /* Two ARM64 kernel Crypto instruction families share existing
         * typed SIMD two-/three-register emitters and strict feature gate. */
        char tok[4][384];unsigned n,rd,rn,rm=0U;
        uint32_t sa=0U,ta=0U,da=0U;
        bool polynomial=!strncmp(op,"pmull",5U);
        bool pair=!strcmp(op,"pmull2");
        bool tri=polynomial || !strcmp(op,"sha1su0");
        bool valid=(a64_features(as)&A64_FEATURE_CRYPTO)!=0U &&
            split_operands(args,tok,&n) && n==(tri?3U:2U);
        if(valid && polynomial) {
            /* PMULL/PMULL2 have TWO independently checked element
             * widths: polynomial bytes -> Vd.8H and polynomial 64-bit
             * halves -> Vd.1Q. Both reuse the same three-register
             * emitter; Vn/Vm shapes must match low/high selection. */
            bool bytes=a64_vector_view(aliases,tok[0],&rd,&da) &&
                       da==UINT32_C(0x40000400);
            if(!bytes)valid=a64_vector_1q(aliases,tok[0],&rd);
            if(valid) {
                uint32_t source=bytes?
                    (pair?UINT32_C(0x40000000):UINT32_C(0)):
                    (pair?UINT32_C(0x40000c00):UINT32_C(0x00000c00));
                valid=a64_vector_view(aliases,tok[1],&rn,&sa) &&
                      a64_vector_view(aliases,tok[2],&rm,&ta) &&
                      sa==source && ta==source;
            }
            if(valid && bytes)
                inst->encoding=pair?UINT32_C(0x4e20e000):
                                     UINT32_C(0x0e20e000);
        }else if(valid) {
            valid=a64_vector_view(aliases,tok[0],&rd,&da) &&
                  a64_vector_view(aliases,tok[1],&rn,&sa) &&
                  da==UINT32_C(0x40000800) && sa==da &&
                  (!tri || (a64_vector_view(aliases,tok[2],&rm,&ta) && ta==da));
        }
        if(!valid) {
            minias_set_error(as,"bad-aarch64-crypto-poly-sha1:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=tri?A64_MC_SIMD_LOGIC:A64_MC_SIMD_CRYPTO;
        inst->count=tri?3U:2U;
        if(!polynomial ||
           inst->encoding==0U)
            inst->encoding=polynomial?
                  (pair?UINT32_C(0x4ee0e000):UINT32_C(0x0ee0e000)):
                  !strcmp(op,"sha1su0")?UINT32_C(0x5e003000):
                                        UINT32_C(0x5e281800);
        put_vector(&inst->operands[0],rd);
        put_vector(&inst->operands[1],rn);
        if(tri)put_vector(&inst->operands[2],rm);
    } else if(!strcmp(op,"sha1c") || !strcmp(op,"sha1m") ||
              !strcmp(op,"sha1p")) {
        /* GNU ARMv8 Crypto SHA1 round operations share a typed
         * Qd,Sn,Vm.4S encoding; no second SIMD register parser. */
        char arg[4][384];unsigned n,rd,rn,rm;
        bool wide;uint32_t shape;
        if((a64_features(as)&A64_FEATURE_CRYPTO)==0U ||
           !split_operands(args,arg,&n) || n!=3U ||
           !parse_a64_q_reg(aliases,arg[0],&rd) ||
           !a64_scalar_fp_reg(aliases,arg[1],&rn,&wide) || wide ||
           !a64_vector_view(aliases,arg[2],&rm,&shape) ||
           shape!=UINT32_C(0x40000800)) {
            minias_set_error(as,"bad-aarch64-sha1-round:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_LOGIC;inst->count=3U;
        inst->encoding=!strcmp(op,"sha1c")?UINT32_C(0x5e000000):
                       !strcmp(op,"sha1p")?UINT32_C(0x5e001000):
                                            UINT32_C(0x5e002000);
        put_q(&inst->operands[0],rd);
        put_fp_scalar(&inst->operands[1],rn,false);
        put_vector(&inst->operands[2],rm);
    } else if(!strcmp(op,"sha1h")) {
        unsigned rd,rn;
        bool dw,nw;
        char dst[96],src[96];
        if((a64_features(as)&A64_FEATURE_CRYPTO)==0U ||
           !split_two(args,dst,sizeof(dst),src,sizeof(src)) ||
           !a64_scalar_fp_reg(aliases,dst,&rd,&dw) || dw ||
           !a64_scalar_fp_reg(aliases,src,&rn,&nw) || nw) {
            minias_set_error(as,"bad-aarch64-sha1h:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_CRYPTO;inst->count=2U;
        inst->encoding=UINT32_C(0x5e280800);
        put_fp_scalar(&inst->operands[0],rd,false);
        put_fp_scalar(&inst->operands[1],rn,false);
    } else if(!strcmp(op,"sha256su0") || !strcmp(op,"sha256su1") ||
              !strcmp(op,"sha256h") || !strcmp(op,"sha256h2")) {
        /* One typed ARM SHA256 instruction family: H/H2 use scalar Qd,Qn
         * and vector Vm.4S; SU1 uses three vector .4S operands, while
         * SU0 uses two. No target-independent register-bank guessing. */
        char tok[4][384];unsigned n,rd,rn,rm=0U;uint32_t da=0U,sa=0U,ta=0U;
        bool pair=!strcmp(op,"sha256h") || !strcmp(op,"sha256h2");
        bool tri=strcmp(op,"sha256su0")!=0;
        bool valid=(a64_features(as)&A64_FEATURE_CRYPTO)!=0U &&
                   split_operands(args,tok,&n) && n==(tri?3U:2U);
        if(valid && pair) {
            valid=parse_a64_q_reg(aliases,tok[0],&rd) &&
                  parse_a64_q_reg(aliases,tok[1],&rn);
        }else if(valid) {
            valid=a64_vector_view(aliases,tok[0],&rd,&da) &&
                  a64_vector_view(aliases,tok[1],&rn,&sa) &&
                  da==UINT32_C(0x40000800) && sa==da;
        }
        if(valid && tri) {
            valid=a64_vector_view(aliases,tok[2],&rm,&ta) &&
                  ta==UINT32_C(0x40000800);
        }
        if(!valid) {
            minias_set_error(as,"bad-aarch64-sha256:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=tri?A64_MC_SIMD_LOGIC:A64_MC_SIMD_CRYPTO;
        inst->count=tri?3U:2U;
        inst->encoding=!strcmp(op,"sha256h")?UINT32_C(0x5e004000):
                       !strcmp(op,"sha256h2")?UINT32_C(0x5e005000):
                       !strcmp(op,"sha256su1")?UINT32_C(0x5e006000):
                                                UINT32_C(0x5e282800);
        if(pair) {
            put_q(&inst->operands[0],rd);
            put_q(&inst->operands[1],rn);
        } else {
            put_vector(&inst->operands[0],rd);
            put_vector(&inst->operands[1],rn);
        }
        if(tri)put_vector(&inst->operands[2],rm);
    } else if(!strcmp(op,"umov") || !strcmp(op,"smov") ||
              (!strcmp(op,"mov") && a64_register_prefix(aliases,args)=='d')) {
        /* One target-owned lane transfer for UMOV/SMOV and MOV Dn aliases,
         * with distinct signed-extension width rules and GNU encoding. */
        char dst[96],source[96];unsigned rd,rn;
        uint32_t lane;
        bool is_x,wide=false,sp,is_fp=!strcmp(op,"mov");
        bool signext=!strcmp(op,"smov");
        if(!split_two(args,dst,sizeof(dst),source,sizeof(source)) ||
           !a64_ins_lane(aliases,source,&rn,&lane,&is_x)) {
            minias_set_error(as,"bad-aarch64-mov-lane:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        if(is_fp) {
            if(!a64_scalar_fp_d(aliases,dst,&rd) || !is_x)goto invalid_lane;
        }else {
            if(!parse_a64_reg(aliases,dst,&rd,&wide,&sp) || sp)goto invalid_lane;
            /* Signed: W <- B/H, X <- B/H/S. Unsigned: W <- B/H/S,
             * X <- D. Lane imm5's low set bit identifies element width. */
            if(signext) {
                /* imm5 packs the lane index above an element-size bit.
                 * Testing bit 2 incorrectly rejects valid H lanes with an
                 * odd index. W accepts B/H; X accepts B/H/S, never D. */
                if((wide && (lane & UINT32_C(7))==0U) ||
                   (!wide && (lane & UINT32_C(3))==0U))
                    goto invalid_lane;
            }else if(wide!=is_x)goto invalid_lane;
        }
        inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
        inst->encoding=(signext?
                           (wide?UINT32_C(0x4e002c00):
                                 UINT32_C(0x0e002c00)):
                         is_fp?UINT32_C(0x5e000400):
                         is_x?UINT32_C(0x4e003c00):
                              UINT32_C(0x0e003c00))|(lane<<16U);
        if(is_fp)put_fp_scalar(&inst->operands[0],rd,true);
        else put_reg(&inst->operands[0],rd,wide);
        put_vector(&inst->operands[1],rn);
        goto lane_complete;
invalid_lane:
        minias_set_error(as,"bad-aarch64-mov-lane:%s:line=%zu",
                         args,stmt->line);
        minias_mcinst_destroy(inst);return false;
lane_complete:;
    } else if(!strcmp(op,"stg") || !strcmp(op,"stzg") ||
              !strcmp(op,"st2g") || !strcmp(op,"stz2g")) {
        /* All four ARM MTE tag-store forms share the same checked
         * 16-byte signed-scaled addressing and one typed store emitter. */
        char tok[4][384];unsigned n,rt;
        bool wide,sp;
        A64Memory memory;
        if((a64_features(as)&A64_FEATURE_MTE)==0U ||
           !split_operands(args,tok,&n) || (n!=2U && n!=3U) ||
           !parse_a64_reg(aliases,tok[0],&rt,&wide,&sp) || sp || !wide ||
           !a64_parse_memory(as,aliases,tok[1],n==3U?tok[2]:NULL,
                             &memory) || memory.post_index_register) {
            minias_set_error(as,"bad-aarch64-stz2g:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        if(memory.offset.kind==A64_OFFSET_CONSTANT &&
           (memory.offset.constant%16!=0 ||
            memory.offset.constant<-4096 || memory.offset.constant>4080)){
            minias_set_error(as,"bad-aarch64-stz2g-offset:%s:line=%zu",
                             args,stmt->line);minias_mcinst_destroy(inst);return false;
        }
        {
            uint32_t base=!strcmp(op,"stg")?UINT32_C(0xd9200000):
                          !strcmp(op,"stzg")?UINT32_C(0xd9600000):
                          !strcmp(op,"st2g")?UINT32_C(0xd9a00000):
                                                UINT32_C(0xd9e00000);
            memory.access_bytes=16U;
            inst->opcode=A64_MC_MTE_STZ2G;inst->count=2U;
            inst->encoding=base;
        }
        put_reg(&inst->operands[0],rt,true);
        if(!a64_attach_memory(as,inst,1U,&memory)) {
            minias_mcinst_destroy(inst);return false;
        }
    } else if((!strcmp(op,"br")||!strcmp(op,"blr")) &&
       a64_parse_named_gpr(aliases,args,&reg,&reg64) && reg64) {
        inst->opcode=!strcmp(op,"br")?A64_MC_BR:A64_MC_BLR;
        inst->count=1U;
        put_reg(&inst->operands[0],reg,true);
    } else if((!strcmp(op,"movz") || !strcmp(op,"movk") ||
               !strcmp(op,"movn"))) {
        char parts[4][384];unsigned n=0U,rd,imm16=0U,shift=0U;
        char *tokens[4]={parts[0],parts[1],parts[2],parts[3]};
        size_t cap[4]={384U,384U,384U,384U};
        size_t argument_length=strlen(args),k;
        char *large=NULL;
        bool parsed;
        bool wide,sp;
        MiniAsSymbolExpr movw_expr;
        MiniAsFixupKind movw_fixup=MINIAS_FIXUP_NONE;
        bool modifier,constant=false,signed_negative=false;
        uint64_t bits=0U;
        unsigned variant;
        if(argument_length>=sizeof(parts[0])) {
            size_t stride=argument_length+1U;
            if(stride>SIZE_MAX/4U || stride>(1024U*1024U)) {
                minias_set_error(as,"aarch64-operand-text-too-large:line=%zu",
                                 stmt->line);minias_mcinst_destroy(inst);return false;
            }
            large=malloc(stride*4U);
            if(!large) {
                minias_set_error(as,"out-of-memory:aarch64-operand-text");
                free(large);minias_mcinst_destroy(inst);return false;
            }
            for(k=0U;k<4U;++k) {
                tokens[k]=large+k*stride;
                cap[k]=stride;
            }
        }
        parsed=split_operands_sized(args,tokens,cap,&n);
        if(!parsed || (n!=2U && n!=3U) ||
           !parse_a64_reg(aliases,tokens[0],&rd,&wide,&sp) || sp) {
            /* Preserve the real GNU-expanded operand and register
             * shape in the pinned first-blocker census. Unsupported MOVW
             * syntax must not be silently normalized or misattributed. */
            minias_set_error(as,"bad-aarch64-wide-immediate:%s:line=%zu",
                             args,stmt->line);
            free(large);minias_mcinst_destroy(inst);return false;
        }
        modifier=a64_movw_relocation(tokens[1],&movw_expr,&movw_fixup,
                                      &shift,&constant,&bits,&signed_negative);
        if((modifier && (n!=2U || (!wide && shift>=2U))) ||
           (!modifier && !parse_imm16(tokens[1],&imm16))) {
            minias_set_error(as,"bad-aarch64-wide-immediate:%s:line=%zu",
                             args,stmt->line);
            free(large);minias_mcinst_destroy(inst);return false;
        }
        if(modifier && constant)
            imm16=(unsigned)((signed_negative?~bits:bits)>>(16U*shift))&
                  UINT32_C(0xffff);
        if(n==3U) {
            int64_t raw_shift;
            if(strncmp(tokens[2],"lsl ",4U)!=0 ||
               !parse_signed_number(tokens[2]+4U,&raw_shift) ||
               raw_shift<0 || raw_shift>=(wide?64:32) ||
               (raw_shift%16)!=0) {
                minias_set_error(as,"bad-aarch64-mov-shift:line=%zu",stmt->line);
                free(large);minias_mcinst_destroy(inst);return false;
            }
            shift=(unsigned)raw_shift/16U;
        }
        variant=!strcmp(op,"movz")?0U:!strcmp(op,"movk")?1U:2U;
        if(modifier && constant && signed_negative && variant==0U)
            variant=2U;
        inst->opcode=A64_MC_WIDE_IMM;inst->count=2U;
        inst->encoding=variant|(shift<<2U);
        inst->target_form=modifier && !constant?(uint32_t)movw_fixup:0U;
        put_reg(&inst->operands[0],rd,wide);
        if(modifier && !constant) {
            if(!a64_attach_expr(as,inst,1U,&movw_expr)) {
                minias_set_error(as,"out-of-memory:a64-movw-reloc");
                free(large);minias_mcinst_destroy(inst);return false;
            }
        }else put_immediate(&inst->operands[1],(int64_t)imm16);
        free(large);
    } else if((!strcmp(op,"yield") || !strcmp(op,"wfe") ||
               !strcmp(op,"wfi") || !strcmp(op,"sev") ||
               !strcmp(op,"sevl")) && !*args) {
        /* Architectural HINT aliases use the exact same typed MCInst. */
        unsigned hint=!strcmp(op,"yield")?1U:
                      !strcmp(op,"wfe")?2U:!strcmp(op,"wfi")?3U:
                      !strcmp(op,"sev")?4U:5U;
        inst->opcode=A64_MC_HINT;inst->count=1U;
        put_immediate(&inst->operands[0],hint);
    } else if (!strcmp(op,"hint")) {
        int64_t hint;
        MiniAsSymbolExpr expr;
        const char *arg=(*args=='#')?args+1U:args;
        inst->opcode=A64_MC_HINT;inst->count=1U;
        if(parse_signed_number(args,&hint) && hint>=0 && hint<=127) {
            put_immediate(&inst->operands[0],hint);
        }else if(minias_parse_symbol_addend(arg,&expr) &&
                 a64_attach_expr(as,inst,0U,&expr)) {
            /* GNU Linux BTI macros use hint #.L__bti_targets_c.
             * Keep the exact symbol in MCInst until layout/encode instead
             * of treating an ABS .equ alias as an unsupported opcode. */
        }else {
            minias_set_error(as,"bad-aarch64-hint:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
    } else if (strcmp(op, "eret") == 0 && *args == '\0') {
        /* ELR_ELx exception return is a fixed architectural encoding.
         * Use the existing typed system-immediate emission owner; no
         * separate machine byte writer or Common ISA special case. */
        inst->opcode=A64_MC_SYSTEM_IMM;
        inst->count=1U;
        inst->encoding=UINT32_C(0xd69f03e0);
        put_immediate(&inst->operands[0],0);
    } else if (strcmp(op, "nop") == 0 && *args == '\0') {
        inst->opcode = A64_MC_NOP;
    } else if (strcmp(op, "ret") == 0 && *args == '\0') {
        inst->opcode = A64_MC_RET;
    } else if (strcmp(op, "ret") == 0 &&
               a64_parse_named_gpr(aliases,args, &reg, &reg64) && reg64) {
        inst->opcode = A64_MC_RET;
        inst->count = 1U;
        put_reg(&inst->operands[0],reg,true);
    } else if(!strcmp(op,"mov") &&
               split_two(args,first,sizeof(first),second,sizeof(second)) &&
               a64_register_prefix(aliases,first)=='v' &&
               a64_register_prefix(aliases,second)=='v') {
        unsigned vd,vn;
        uint32_t da,sa;
        if(!a64_vector_view(aliases,first,&vd,&da) ||
           !a64_vector_view(aliases,second,&vn,&sa) || da!=sa ||
           (da&UINT32_C(0xc00))!=0U) {
            minias_set_error(as,"bad-aarch64-mov-vector:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_MOVE;inst->count=2U;
        inst->encoding=(da&UINT32_C(0x40000000))?
                 UINT32_C(0x4ea01c00):UINT32_C(0x0ea01c00);
        put_vector(&inst->operands[0],vd);
        put_vector(&inst->operands[1],vn);
    } else if (strcmp(op, "mov") == 0 &&
               split_two(args, first, sizeof(first), second, sizeof(second)) &&
               a64_parse_named_gpr(aliases,first, &reg, &reg64) &&
               parse_imm16(second, &imm)) {
        /* MOV #imm16 is exactly MOVZ with HW=0. Its explicit spelling
         * and the wide-immediate MOVZ/MOVK/MOVN grammar now share one
         * typed form and one machine encoder. */
        inst->opcode = A64_MC_WIDE_IMM;
        inst->count = 2U;
        inst->encoding = 0U; /* MOVZ variant, unshifted */
        put_reg(&inst->operands[0],reg,reg64);
        put_immediate(&inst->operands[1],(int64_t)imm);
    } else if (!strcmp(op,"mov") &&
               split_two(args,first,sizeof(first),second,sizeof(second)) &&
               a64_parse_named_gpr(aliases,first,&reg,&reg64) &&
               (second[0]=='-' || second[0]=='#' ||
                isdigit((unsigned char)second[0]))) {
        /* One GNU MOV constant grammar and encoder, including signed
         * W/X values. Former small-negative special cases belong to the
         * same MOVZ/MOVN/ORR selection, not a second parser algorithm.
         * No multi-instruction pseudo expansion is permitted. */
        uint64_t value,mask=reg64?UINT64_MAX:UINT32_MAX;
        uint32_t fields=0U;
        unsigned j,limit=reg64?4U:2U,variant=0U,chosen=4U;
        const char *literal=second[0]=='#'?second+1U:second;
        /* GNU treats both #-1 and #(-1) as signed constants. Reuse
         * Common's signed expression evaluator; never parse a second
         * target-specific parenthesis/arithmetic grammar. */
        int64_t signed_value=0;
        bool negative=*literal=='-';
        if(!negative && *literal=='(' &&
           parse_signed_number(literal,&signed_value) &&
           signed_value<0)negative=true;
        if(negative) {
            if(!parse_signed_number(literal,&signed_value) ||
               (!reg64 && signed_value<INT32_MIN)) {
                minias_set_error(as,"bad-aarch64-mov-negative:%s:line=%zu",
                                 second,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            value=((uint64_t)signed_value)&mask;
        }else if(!minias_eval_u64_expression(literal,&value) ||
                 (!reg64 && (value>>32U)!=0U)) {
            minias_set_error(as,"bad-aarch64-mov-constant:%s:line=%zu",
                             second,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        for(j=0U;j<limit;++j) {
            uint64_t piece=UINT64_C(0xffff)<<(j*16U);
            if((value&~piece)==0U) {
                chosen=j;variant=0U;break;
            }
        }
        if(chosen==4U) {
            for(j=0U;j<limit;++j) {
                uint64_t piece=UINT64_C(0xffff)<<(j*16U);
                if(((~value)&mask&~piece)==0U) {
                    chosen=j;variant=2U;break;
                }
            }
        }
        if(chosen!=4U) {
            inst->opcode=A64_MC_WIDE_IMM;inst->count=2U;
            inst->encoding=variant|(chosen<<2U);
            put_reg(&inst->operands[0],reg,reg64);
            put_immediate(&inst->operands[1],(int64_t)
                          ((variant?~value:value)>>(chosen*16U)&65535U));
        }else if(a64_mask_bits(value,reg64,&fields)) {
            inst->opcode=A64_MC_BITMASK_IMM;inst->count=3U;
            inst->encoding=UINT32_C(0x32000000);
            put_reg(&inst->operands[0],reg,reg64);
            put_reg(&inst->operands[1],31U,reg64);
            put_immediate(&inst->operands[2],fields);
        }else {
            minias_set_error(as,"bad-aarch64-mov-not-single-encoding:%s:line=%zu",
                             second,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
    } else if (strcmp(op, "brk") == 0 && parse_imm16(args,&imm)) {
        inst->opcode=A64_MC_BRK;inst->count=1U;
        put_immediate(&inst->operands[0],(int64_t)imm);
    } else if ((!strcmp(op,"svc") || !strcmp(op,"hvc") ||
                !strcmp(op,"smc")) && parse_imm16(args, &imm)) {
        inst->opcode = A64_MC_SVC;
        inst->encoding=!strcmp(op,"svc")?1U:!strcmp(op,"hvc")?2U:3U;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_IMMEDIATE;
        inst->operands[0].immediate = imm;
    } else if (strcmp(op, "b") == 0 || strcmp(op, "bl") == 0) {
        /* The symbol-difference addend, when present, is held separately
         * in MCInst until final layout. Never use the old parse-time local
         * difference folding shortcut: it can freeze a stale displacement
         * when a forward .space changes the section after parsing. */
        if (!a64_attach_branch_target(as,inst,0U,args)) {
            minias_set_error(as, "bad-aarch64-branch-target:%s:line=%zu",
                             args, stmt->line);
            minias_mcinst_destroy(inst);
            return false;
        }
        inst->opcode = strcmp(op, "bl") == 0 ? A64_MC_BL : A64_MC_B;
        inst->count = 1U;
    } else if (strcmp(op, "cbz") == 0 || strcmp(op, "cbnz") == 0) {
        bool is_sp;
        if (!split_two(args, first, sizeof(first), second, sizeof(second)) ||
            !parse_a64_reg(aliases,first, &reg, &reg64, &is_sp) || is_sp ||
            !a64_attach_branch_target(as,inst,1U,second)) {
            minias_set_error(as,"bad-aarch64-condbranch:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=strcmp(op,"cbz")==0?A64_MC_CBZ:A64_MC_CBNZ;
        inst->count=2U;
        put_reg(&inst->operands[0],reg,reg64);
    } else if (!strcmp(op,"tbz") || !strcmp(op,"tbnz")) {
        char t[4][384];unsigned n,rd;
        bool wide,sp;
        int64_t bit;
        if(!split_operands(args,t,&n) || n!=3U ||
           !parse_a64_reg(aliases,t[0],&rd,&wide,&sp) || sp ||
           !parse_signed_number(t[1],&bit) || bit<0 ||
           bit>=(wide?64:32) ||
           !a64_attach_branch_target(as,inst,2U,t[2])) {
            minias_set_error(as,"bad-aarch64-testbranch:%s:line=%zu",
                             args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
        inst->opcode=!strcmp(op,"tbz")?A64_MC_TBZ:A64_MC_TBNZ;
        inst->count=3U;
        put_reg(&inst->operands[0],rd,wide);
        put_immediate(&inst->operands[1],bit);
    } else {
        unsigned cond;
        if(a64_branch_condition(op,&cond)) {
            if(!a64_attach_branch_target(as,inst,0U,args)) {
                minias_set_error(as,"bad-aarch64-conditional:%s:line=%zu",
                                 args,stmt->line);
                minias_mcinst_destroy(inst);return false;
            }
            inst->opcode=A64_MC_BCOND;inst->count=1U;
            inst->encoding=cond;
        } else if (a64_parse_relocated(as,aliases,inst,op,args)) {
            /* ADRP/LO12 relocation is typed and target-owned. */
        } else if (parse_integer_family(as,aliases,inst,op,args)) {
            /* Reuse existing typed register and memory IR. */
        } else {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            minias_mcinst_destroy(inst);return false;
        }
    }
    /*
     * The AArch64 integer/memory family is parsed into typed operands.
     * No instruction is silently translated through a RISC-V pipeline.
     */
    stmt->mcinst = inst;
    return true;
}

static bool encode(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *inst = stmt->mcinst;
    uint32_t code;
    MiniAsFixupKind fixup = MINIAS_FIXUP_NONE;
    const MiniAsSymbolRef *expr = NULL;
    MiniAsSymbolRef resolved_branch_expr;
    if (inst == NULL) {
        minias_set_error(as, "missing-aarch64-mcinst:line=%zu", stmt->line);
        return false;
    }
    switch (inst->opcode) {
        case A64_MC_LDR_LITERAL: {
            const MiniAsSymbolRef *literal=inst->operands[1].bound_symbol;
            const MiniAsSymbol *target=literal?
                minias_symbol_ref_lookup(as,*literal):NULL;
            int64_t displacement;
            bool direct=inst->target_form==1U;
            if(!literal || (!direct &&
               (!target || !target->defined ||
                target->section!=stmt->section))) {
                minias_set_error(as,"bad-aarch64-literal-pool-target:%s",
                                 stmt->args);return false;
            }
            code=(inst->operands[0].register_64?
                  UINT32_C(0x58000000):UINT32_C(0x18000000))|
                 inst->operands[0].reg;
            if(!target || !target->defined || target->section!=stmt->section) {
                /* A link-time PC-relative load requires the genuine AArch64
                 * instruction fixup, not a data PREL32 relocation. */
                fixup=MINIAS_FIXUP_A64_LD_PREL_LO19;
                expr=literal;
                break;
            }
            if(target->value>INT64_MAX || stmt->offset>INT64_MAX ||
               (literal->addend>0 &&
                (int64_t)target->value>INT64_MAX-literal->addend) ||
               (literal->addend<0 &&
                (int64_t)target->value<INT64_MIN-literal->addend)) {
                minias_set_error(as,"aarch64-literal-address-overflow");
                return false;
            }
            {
                int64_t target_address=(int64_t)target->value+
                                       literal->addend;
                if(target_address<INT64_MIN+(int64_t)stmt->offset) {
                    minias_set_error(as,"aarch64-literal-pcrel-overflow");
                    return false;
                }
                displacement=target_address-(int64_t)stmt->offset;
            }
            if((displacement&3)!=0 || displacement<-1048576 ||
               displacement>1048572) {
                minias_set_error(as,"aarch64-literal-load-out-of-range:%s",
                                 stmt->args);return false;
            }
            code|=((uint32_t)(displacement/4)&UINT32_C(0x7ffff))<<5U;
            break;
        }
        case A64_MC_BITFIELD: {
            unsigned bit_rd=inst->operands[0].reg;
            unsigned bit_rn=inst->operands[1].reg;
            uint32_t immr=(uint32_t)inst->operands[2].immediate;
            uint32_t imms=(uint32_t)inst->operands[3].immediate;
            code=inst->encoding|
                 (inst->operands[0].register_64?UINT32_C(0x80400000):0U)|
                 (immr<<16U)|(imms<<10U)|(bit_rn<<5U)|bit_rd;
            break;
        }
        case A64_MC_ARITH_ALIAS: {
            unsigned alu_rd=inst->operands[0].reg;
            unsigned alu_rn=inst->operands[1].reg;
            unsigned alu_rm=inst->operands[2].reg;
            code=inst->encoding|
                (inst->operands[0].register_64?UINT32_C(0x80000000):0U)|
                (alu_rm<<16U)|(alu_rn<<5U)|alu_rd|
                (inst->count==4U?(inst->operands[3].reg<<10U):0U);
            break;
        }
        case A64_MC_EXTR:
            code=(inst->operands[0].register_64?
                   UINT32_C(0x93c00000):UINT32_C(0x13800000))|
                 (inst->operands[2].reg<<16U)|
                 ((uint32_t)inst->operands[3].immediate<<10U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_SHIFT: {
            unsigned rd=inst->operands[0].reg;
            unsigned rn=inst->operands[1].reg;
            bool wide=inst->operands[0].register_64;
            unsigned shiftop=inst->encoding&3U;
            if(inst->encoding&UINT32_C(0x100)) {
                unsigned rm=inst->operands[2].reg;
                uint32_t base=UINT32_C(0x1ac02000)+(shiftop<<10U);
                code=base|(wide?UINT32_C(0x80000000):0U)|
                     (rm<<16U)|(rn<<5U)|rd;
            }else {
                unsigned shift=(unsigned)inst->operands[2].immediate;
                unsigned width=wide?64U:32U;
                unsigned immr=shiftop==0U?(width-shift)%width:shift;
                unsigned imms=shiftop==0U?width-1U-shift:width-1U;
                uint32_t base=shiftop==2U?
                     (wide?UINT32_C(0x93400000):UINT32_C(0x13000000)):
                     (wide?UINT32_C(0xd3400000):UINT32_C(0x53000000));
                if(shiftop==3U)
                    code=(wide?UINT32_C(0x93c00000):UINT32_C(0x13800000))|
                         (rn<<16U)|(shift<<10U)|(rn<<5U)|rd;
                else code=base|(immr<<16U)|(imms<<10U)|(rn<<5U)|rd;
            }
            break;
        }
        case A64_MC_COND_COMPARE: {
            unsigned rn=inst->operands[0].reg;
            unsigned operand2=inst->operands[1].kind==MINIAS_MC_IMMEDIATE?
                         (unsigned)inst->operands[1].immediate:
                         inst->operands[1].reg;
            unsigned nzcv=(unsigned)inst->operands[2].immediate;
            unsigned cond=(unsigned)inst->operands[3].immediate;
            code=inst->encoding|(operand2<<16U)|(cond<<12U)|
                 (rn<<5U)|nzcv;
            break;
        }
        case A64_MC_COND_SELECT: {
            unsigned rd=inst->operands[0].reg;
            unsigned rn=inst->operands[1].reg;
            unsigned rm=inst->operands[2].reg;
            bool wide=inst->operands[0].register_64;
            code=inst->encoding|(wide?UINT32_C(0x80000000):0U)|
                 (rm<<16U)|(rn<<5U)|rd;
            break;
        }
        case A64_MC_CSEL_ALIAS: {
            unsigned rd=inst->operands[0].reg;
            unsigned rn=inst->operands[1].reg;
            bool sf=inst->operands[0].register_64;
            code=((inst->encoding&0x100U)?
                    UINT32_C(0x5a800000):UINT32_C(0x1a800400)) |
                 (sf?UINT32_C(0x80000000):0U) |
                 (rn<<16U) | ((inst->encoding&15U)<<12U) |
                 (rn<<5U) | rd;
            break;
        }
        case A64_MC_MRS:
        case A64_MC_MSR:
        case A64_MC_SYS_OP:
            code=inst->encoding|inst->operands[0].reg;
            break;
        case A64_MC_BARRIER:
            code=inst->encoding;
            break;
        case A64_MC_HINT: {
            uint32_t hint;
            if(inst->operands[0].kind==MINIAS_MC_BOUND_SYMBOL) {
                const MiniAsSymbolRef *hint_expr=inst->operands[0].bound_symbol;
                const MiniAsSymbol *symbol=hint_expr?
                    minias_symbol_ref_lookup(as,*hint_expr):NULL;
                int64_t addend=hint_expr?hint_expr->addend:0;
                if(!symbol || !symbol->defined ||
                   symbol->section!=MINIAS_SECTION_ABS ||
                   symbol->value>127U ||
                   addend<-(int64_t)symbol->value ||
                   addend>127-(int64_t)symbol->value) {
                    minias_set_error(as,
                        "invalid-aarch64-absolute-hint:%s:line=%zu",
                        stmt->args,stmt->line);
                    return false;
                }
                hint=(uint32_t)((int64_t)symbol->value+addend);
            }else {
                hint=(uint32_t)inst->operands[0].immediate;
                if(hint>127U) {
                    minias_set_error(as,"aarch64-hint-imm7-out-of-range");
                    return false;
                }
            }
            code=UINT32_C(0xd503201f)|(hint<<5U);
            break;
        }
        case A64_MC_NOP:
            code = UINT32_C(0xd503201f);
            break;
        case A64_MC_RET:
            code = inst->count == 0U ? UINT32_C(0xd65f03c0) :
                UINT32_C(0xd65f0000) | (inst->operands[0].reg << 5U);
            break;
        case A64_MC_ADR: {
            const MiniAsSymbol *target;
            int64_t displacement;
            expr=inst->operands[1].bound_symbol;
            code=UINT32_C(0x10000000)|inst->operands[0].reg;
            fixup=MINIAS_FIXUP_A64_ADR_LO21;
            target=minias_symbol_ref_lookup(as,*expr);
            /* GNU GAS emits ADR_PREL_LO21 relocations for GLOBAL/WEAK
             * references even when their definitions reside in .text.
             * Resolve only non-preemptible section-local symbol targets. */
            if(!target || !target->defined ||
               target->section!=stmt->section ||
               target->bind!=MINIAS_STB_LOCAL)break;
            if(target->value>INT64_MAX || stmt->offset>INT64_MAX ||
               (expr->addend>0 &&
                (int64_t)target->value>INT64_MAX-expr->addend) ||
               (expr->addend<0 &&
                (int64_t)target->value<INT64_MIN-expr->addend)) {
                minias_set_error(as,"aarch64-adr-overflow");return false;
            }
            displacement=(int64_t)target->value+expr->addend;
            if(displacement<INT64_MIN+(int64_t)stmt->offset) {
                minias_set_error(as,"aarch64-adr-pcrel-overflow");
                return false;
            }
            displacement-=(int64_t)stmt->offset;
            if(displacement<-(INT64_C(1)<<20U) ||
               displacement>=(INT64_C(1)<<20U)) {
                minias_set_error(as,"aarch64-adr-range");return false;
            }
            code|=(((uint32_t)displacement&3U)<<29U)|
                  ((((uint32_t)displacement>>2U)&UINT32_C(0x7ffff))<<5U);
            fixup=MINIAS_FIXUP_NONE;break;
        }
        case A64_MC_ADRP:
            code=UINT32_C(0x90000000)|inst->operands[0].reg;
            expr=inst->operands[1].bound_symbol;
            fixup=inst->target_form==1U?MINIAS_FIXUP_A64_ADR_GOT_PAGE:
                                      MINIAS_FIXUP_A64_ADRP_HI21;
            break;
        case A64_MC_ADD_LO12:
            code=(inst->operands[0].register_64?
                  UINT32_C(0x91000000):UINT32_C(0x11000000))|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            expr=inst->operands[2].bound_symbol;
            fixup=MINIAS_FIXUP_A64_ADD_LO12;
            break;
        case A64_MC_LDST_LO12:
        case A64_MC_LDST_GOT_LO12: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            if(!memory ||
               (memory->offset.kind!=A64_OFFSET_LO12 &&
                memory->offset.kind!=A64_OFFSET_GOT_LO12))return false;
            code=inst->encoding|(memory->base<<5U)|inst->operands[0].reg;
            expr=&memory->offset.symbol;
            if(inst->opcode==A64_MC_LDST_GOT_LO12)
                fixup=MINIAS_FIXUP_A64_LD64_GOT_LO12;
            else {
                unsigned bytes;
                const A64ScalarMemoryForm *form;
                if(inst->target_form==0U ||
                   inst->target_form>sizeof(a64_scalar_memory_forms)/
                                      sizeof(a64_scalar_memory_forms[0])) {
                    minias_set_error(as,"missing-aarch64-lo12-memory-form");
                    return false;
                }
                form=&a64_scalar_memory_forms[inst->target_form-1U];
                bytes=form->bytes?form->bytes:
                      (inst->operands[0].register_64?8U:4U);
                fixup=bytes==1U?MINIAS_FIXUP_A64_LDST8_LO12:
                      bytes==2U?MINIAS_FIXUP_A64_LDST16_LO12:
                      bytes==4U?MINIAS_FIXUP_A64_LDST32_LO12:
                                 MINIAS_FIXUP_A64_LDST64_LO12;
            }
            break;
        }
        case A64_MC_BR:
        case A64_MC_BLR:
            code=(inst->opcode==A64_MC_BR?
                UINT32_C(0xd61f0000):UINT32_C(0xd63f0000)) |
                (inst->operands[0].reg<<5U);
            break;
        case A64_MC_WIDE_IMM: {
            unsigned variant=inst->encoding&3U;
            unsigned shift=(inst->encoding>>2U)&3U;
            bool wide=inst->operands[0].register_64;
            uint32_t imm16=(uint32_t)inst->operands[1].immediate;
            if(inst->operands[1].kind==MINIAS_MC_BOUND_SYMBOL) {
                const MiniAsSymbol *symbol;
                MiniAsFixupKind kind=(MiniAsFixupKind)inst->target_form;
                expr=inst->operands[1].bound_symbol;
                symbol=minias_symbol_ref_lookup(as,*expr);
                if(symbol && symbol->defined &&
                   symbol->section==MINIAS_SECTION_ABS) {
                    uint64_t bits=symbol->value+(uint64_t)expr->addend;
                    bool checked_signed=kind==MINIAS_FIXUP_A64_MOVW_SG0 ||
                                        kind==MINIAS_FIXUP_A64_MOVW_SG1 ||
                                        kind==MINIAS_FIXUP_A64_MOVW_SG2;
                    bool checked_unsigned=kind==MINIAS_FIXUP_A64_MOVW_G0 ||
                                          kind==MINIAS_FIXUP_A64_MOVW_G1 ||
                                          kind==MINIAS_FIXUP_A64_MOVW_G2 ||
                                          kind==MINIAS_FIXUP_A64_MOVW_G3;
                    unsigned width=(shift+1U)*16U;
                    int64_t signed_bits=(int64_t)bits;
                    if(checked_signed && width<64U) {
                        int64_t bound=INT64_C(1)<<(width-1U);
                        if(signed_bits< -bound || signed_bits>=bound) {
                            minias_set_error(as,
                                "aarch64-movw-absolute-range:%s:line=%zu",
                                stmt->args,stmt->line);
                            return false;
                        }
                    }else if(checked_unsigned && width<64U &&
                             bits>=(UINT64_C(1)<<width)) {
                        minias_set_error(as,
                            "aarch64-movw-absolute-overflow:%s:line=%zu",
                            stmt->args,stmt->line);
                        return false;
                    }
                    /* GNU SABS_Gn encodes negative absolute values with
                     * MOVN, complementing its immediate 16-bit lane. A
                     * parsed symbol may become ABS only after deferred .set
                     * resolution. It MUST NOT leave a fake ELF relocation. */
                    if(checked_signed && signed_bits<0 && variant==0U)
                        variant=2U;
                    imm16=(uint32_t)(((checked_signed &&
                         signed_bits<0)?~bits:bits)>>(shift*16U))&UINT32_C(0xffff);
                    fixup=MINIAS_FIXUP_NONE;
                }else {
                    imm16=0U;
                    fixup=kind;
                }
            }
            {
                uint32_t base=variant==0U?UINT32_C(0x52800000):
                              variant==1U?UINT32_C(0x72800000):
                                          UINT32_C(0x12800000);
                code=base|(wide?UINT32_C(0x80000000):0U)|
                     (shift<<21U)|(imm16<<5U)|inst->operands[0].reg;
            }
            break;
        }
        case A64_MC_BRK:
            code = UINT32_C(0xd4200000) |
                ((uint32_t)inst->operands[0].immediate<<5U);
            break;
        case A64_MC_SVC:
            code = UINT32_C(0xd4000000)|inst->encoding|
                ((uint32_t)inst->operands[0].immediate << 5U);
            break;

        case A64_MC_EXCLUSIVE: {
            unsigned kind=inst->encoding&3U;
            bool byte=(inst->encoding&0x10U)!=0U;
            bool half=(inst->encoding&0x20U)!=0U;
            const A64Memory *memory=a64_memory_at(inst,kind<2U?1U:2U);
            if(!memory)return false;
            if(kind<2U) {
                bool wide=inst->operands[0].register_64;
                code=(byte?UINT32_C(0x085f7c00):
                      half?UINT32_C(0x485f7c00):
                      wide?UINT32_C(0xc85f7c00):UINT32_C(0x885f7c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (memory->base<<5U) | inst->operands[0].reg;
            }else {
                bool wide=inst->operands[1].register_64;
                code=(byte?UINT32_C(0x08007c00):
                      half?UINT32_C(0x48007c00):
                      wide?UINT32_C(0xc8007c00):UINT32_C(0x88007c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (inst->operands[0].reg<<16U) |
                     (memory->base<<5U) | inst->operands[1].reg;
            }
            break;
        }
        case A64_MC_LSE_PAIR: {
            const A64Memory *memory=a64_memory_at(inst,2U);
            if(!memory)return false;
            code=inst->encoding|
                 (inst->operands[0].reg<<16U)|
                 (memory->base<<5U)|
                 inst->operands[1].reg;
            break;
        }
        case A64_MC_MTE_STZ2G:
        case A64_MC_MTE_STGM: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            int64_t offset;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset) ||
               offset%16!=0 || offset<-4096 || offset>4080) {
                minias_set_error(as,"aarch64-final-mte-memory-offset-out-of-range");
                return false;
            }
            code=inst->encoding|
                 (inst->opcode==A64_MC_MTE_STZ2G?
                   (memory->mode==A64_ADDRESS_OFFSET?UINT32_C(0x800):
                    memory->mode==A64_ADDRESS_PRE_INDEX?UINT32_C(0xc00):
                    UINT32_C(0x400)):0U)|
                 (((uint32_t)(offset/16)&511U)<<12U)|
                 (memory->base<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_SIMD_DUP:
        case A64_MC_SIMD_REV32:
            code=inst->encoding|
                 (inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            break;
        case A64_MC_SIMD_IMM:
            code=inst->encoding|inst->operands[0].reg;
            break;
        case A64_MC_SIMD_EXT:
            code=inst->encoding|
                 (inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            break;
        case A64_MC_EXCLUSIVE_PAIR: {
            const A64Memory *memory=a64_memory_at(inst,3U);
            if(!memory)return false;
            code=inst->encoding|
                 (inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<10U)|
                 (memory->base<<5U)|
                 inst->operands[0].reg;
            break;
        }
        case A64_MC_SIMD_LOGIC:
            code=inst->encoding|
                 (inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            break;
        case A64_MC_SIMD_CRYPTO:
            code=inst->encoding|(inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            break;
        case A64_MC_SIMD_MOVE: {
            unsigned src=inst->operands[1].reg;
            code=inst->encoding|(src<<16U)|(src<<5U)|
                 inst->operands[0].reg;
            break;
        }
        case A64_MC_SIMD_SINGLE: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            if(!memory)return false;
            code=inst->encoding|
                 (memory->mode==A64_ADDRESS_POST_INDEX?
                    UINT32_C(0x00800000)|
                    ((memory->post_index_register?
                       memory->post_register:31U)<<16U):0U)|
                 (memory->base<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_PRFM: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            int64_t offset;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset) ||
               offset<0 || offset%8!=0 || offset/8>4095) {
                minias_set_error(as,"aarch64-final-prfm-memory-offset-out-of-range");
                return false;
            }
            code=UINT32_C(0xf9800000) |
                 ((uint32_t)(offset/8)<<10U) |
                 (memory->base<<5U) |
                 (uint32_t)inst->operands[0].immediate;
            break;
        }
        case A64_MC_LSE: {
            unsigned kind=inst->encoding&15U;
            bool acquire=(inst->encoding&0x20U)!=0U;
            bool release=(inst->encoding&0x40U)!=0U;
            bool wide=inst->operands[0].register_64;
            bool byte=(inst->encoding&0x80U)!=0U;
            bool half=(inst->encoding&0x100U)!=0U;
            const A64Memory *memory=a64_memory_at(inst,2U);
            uint32_t base;
            if(!memory)return false;
            if(kind==0U) {
                base=byte?UINT32_C(0x08a07c00):
                     half?UINT32_C(0x48a07c00):
                     wide?UINT32_C(0xc8a07c00):UINT32_C(0x88a07c00);
                if(acquire)base|=UINT32_C(0x00400000);
                if(release)base|=UINT32_C(0x00008000);
            }else {
                base=byte?UINT32_C(0x38200000):
                     half?UINT32_C(0x78200000):
                     wide?UINT32_C(0xf8200000):UINT32_C(0xb8200000);
                if(acquire)base|=UINT32_C(0x00800000);
                if(release)base|=UINT32_C(0x00400000);
                base|=kind==2U?UINT32_C(0x1000):
                      kind==3U?UINT32_C(0x3000):
                      kind==4U?UINT32_C(0x8000):
                      kind==5U?UINT32_C(0x2000):0U;
            }
            code=base|(inst->operands[0].reg<<16U)|
                 (memory->base<<5U)|inst->operands[1].reg;
            break;
        }
        case A64_MC_BITMASK_IMM: {
            bool wide=inst->operands[0].register_64;
            uint32_t f=(uint32_t)inst->operands[2].immediate;
            code=inst->encoding|(wide?UINT32_C(0x80000000):0U)|
                 ((f&UINT32_C(0x1000))<<10U)|
                 (((f>>6U)&63U)<<16U)|((f&63U)<<10U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_TST_REG:
        case A64_MC_BIC_REG: {
            bool wide=inst->operands[0].register_64;
            bool test=inst->opcode==A64_MC_TST_REG;
            bool alias=test && inst->count==2U;
            unsigned rd=alias?31U:inst->operands[0].reg;
            unsigned rn=alias?inst->operands[0].reg:inst->operands[1].reg;
            unsigned rm=alias?inst->operands[1].reg:inst->operands[2].reg;
            code=(test?UINT32_C(0x6a000000):UINT32_C(0x0a200000))|
                 (wide?UINT32_C(0x80000000):0U)|inst->encoding|
                 (rm<<16U)|(rn<<5U)|rd;
            break;
        }
        case A64_MC_EXTEND_W:
            /* GNU GAS canonicalizes UXTW Xd,Wn as MOV Wd,Wn
             * (ORR Wd,WZR,Wn). UBFM Xd,Xn,#0,#31 is equivalent but not
             * byte-identical; keep the actual assembler oracle stable. */
            code=inst->encoding ?
                 (UINT32_C(0x2a0003e0) |
                  (inst->operands[1].reg<<16U) | inst->operands[0].reg):
                 (UINT32_C(0x93407c00) |
                  (inst->operands[1].reg<<5U) | inst->operands[0].reg);
            break;
        case A64_MC_REV: {
            bool wide=inst->operands[0].register_64;
            code=inst->encoding|(wide?UINT32_C(0x80000000):0U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_ACQUIRE_RELEASE: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            bool wide=inst->operands[0].register_64;
            uint32_t opcode;
            if(!memory)return false;
            switch(inst->encoding) {
                case 0U:opcode=wide?UINT32_C(0xc89ffc00):UINT32_C(0x889ffc00);break;
                case 1U:opcode=wide?UINT32_C(0xc8dffc00):UINT32_C(0x88dffc00);break;
                case 2U:opcode=UINT32_C(0x08dffc00);break;
                case 3U:opcode=UINT32_C(0x48dffc00);break;
                case 4U:opcode=UINT32_C(0x089ffc00);break;
                case 5U:opcode=UINT32_C(0x489ffc00);break;
                default:minias_set_error(as,"bad-aarch64-acquire-size");return false;
            }
            code=opcode | (memory->base<<5U) |
                 inst->operands[0].reg;
            break;
        }
        case A64_MC_MVN_REG:
            code=(inst->operands[0].register_64?
                   UINT32_C(0xaa2003e0):UINT32_C(0x2a2003e0)) |
                 (inst->operands[1].reg<<16U)|inst->operands[0].reg;
            break;
        case A64_MC_SYSTEM_IMM:
            code=inst->encoding |
                 ((uint32_t)inst->operands[0].immediate<<8U);
            break;
        case A64_MC_MOV_REG: {
            unsigned rd = inst->operands[0].reg;
            unsigned rm = inst->operands[1].reg;
            bool sf = inst->operands[0].register_64;
            code = inst->encoding ?
                (sf ? UINT32_C(0x91000000) : UINT32_C(0x11000000)) |
                    (rm << 5U) | rd :
                (sf ? UINT32_C(0xaa0003e0) : UINT32_C(0x2a0003e0)) |
                    (rm << 16U) | rd;
            break;
        }
        case A64_MC_EXT_ALU:
            code=inst->encoding|
                 (inst->operands[0].register_64?UINT32_C(0x80000000):0U)|
                 (inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_ADD_REG:
        case A64_MC_SUB_REG:
        case A64_MC_ADDS_REG:
        case A64_MC_SUBS_REG:
        case A64_MC_AND_REG:
        case A64_MC_ORR_REG:
        case A64_MC_EOR_REG: {
            bool sf = inst->operands[0].register_64;
            unsigned rd = inst->operands[0].reg;
            unsigned rn = inst->operands[1].reg;
            unsigned rm = inst->operands[2].reg;
            uint32_t base =
                inst->opcode == A64_MC_ADD_REG ? UINT32_C(0x0b000000) :
                inst->opcode == A64_MC_SUB_REG ? UINT32_C(0x4b000000) :
                inst->opcode == A64_MC_ADDS_REG ? UINT32_C(0x2b000000) :
                inst->opcode == A64_MC_SUBS_REG ? UINT32_C(0x6b000000) :
                inst->opcode == A64_MC_AND_REG ? UINT32_C(0x0a000000) :
                inst->opcode == A64_MC_ORR_REG ? UINT32_C(0x2a000000) :
                                                 UINT32_C(0x4a000000);
            code = base | (sf ? UINT32_C(0x80000000) : 0U) |
                   inst->encoding | (rm << 16U) | (rn << 5U) | rd;
            break;
        }
        case A64_MC_ADD_IMM:
        case A64_MC_SUB_IMM:
        case A64_MC_ADDS_IMM:
        case A64_MC_SUBS_IMM: {
            bool sf = inst->operands[0].register_64;
            unsigned rd = inst->operands[0].reg;
            unsigned rn = inst->operands[1].reg;
            int64_t value;
            uint64_t resolved_imm;
            unsigned imm;
            if(!a64_resolve_immediate(as,&inst->operands[2],&value))return false;
            resolved_imm=(uint64_t)value;
            if(value<0 ||
               (resolved_imm>4095U &&
                (resolved_imm%4096U!=0U || resolved_imm/4096U>4095U))) {
                minias_set_error(as,"aarch64-final-alu-imm-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            imm=(unsigned)resolved_imm;
            bool shift12=imm>4095U ||
                           (inst->encoding&UINT32_C(0x00400000))!=0U;
            if(imm>4095U)imm>>=12U;
            code = (inst->opcode == A64_MC_ADD_IMM ? UINT32_C(0x11000000):
                    inst->opcode == A64_MC_SUB_IMM ? UINT32_C(0x51000000):
                    inst->opcode == A64_MC_ADDS_IMM ? UINT32_C(0x31000000):
                                                    UINT32_C(0x71000000)) |
                   (sf ? UINT32_C(0x80000000) : 0U) |
                   (shift12?UINT32_C(0x00400000):0U) |
                   (imm << 10U) | (rn << 5U) | rd;
            break;
        }
        case A64_MC_CMP_REG:
        case A64_MC_CMN_REG: {
            bool sf = inst->operands[0].register_64;
            code = (inst->opcode==A64_MC_CMP_REG ?
                    UINT32_C(0x6b00001f):UINT32_C(0x2b00001f)) |
                   (sf ? UINT32_C(0x80000000) : 0U) |
                   inst->encoding | (inst->operands[1].reg << 16U) |
                   (inst->operands[0].reg << 5U);
            break;
        }
        case A64_MC_CMP_IMM:
        case A64_MC_CMN_IMM: {
            bool sf = inst->operands[0].register_64;
            int64_t value;
            uint64_t imm;
            if(!a64_resolve_immediate(as,&inst->operands[1],&value))return false;
            imm=(uint64_t)value;
            if(value<0 ||
               (imm>4095U && (imm%4096U!=0U || imm/4096U>4095U))) {
                minias_set_error(as,"aarch64-final-compare-imm-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            bool shift12=imm>4095U;
            if(shift12)imm>>=12U;
            code = (inst->opcode==A64_MC_CMP_IMM ?
                    UINT32_C(0x7100001f):UINT32_C(0x3100001f)) |
                   (sf ? UINT32_C(0x80000000) : 0U) |
                   (shift12?UINT32_C(0x00400000):0U) |
                   ((uint32_t)imm << 10U) |
                   (inst->operands[0].reg << 5U);
            break;
        }
        case A64_MC_LDST_UNPRIV: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            int64_t offset;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset) ||
               offset<-256 || offset>255) {
                minias_set_error(as,"aarch64-final-unprivileged-offset-out-of-range");
                return false;
            }
            code=(inst->encoding-UINT32_C(0x00fff800))|
                 (((uint32_t)offset&511U)<<12U)|
                 (memory->base<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_LDST_SCALED: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            int64_t offset;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset)) {
                minias_set_error(as,"aarch64-unresolved-memory-offset");
                return false;
            }
            if(memory->offset.kind==A64_OFFSET_DIFFERENCE &&
               inst->target_form!=0U) {
                bool scalar_matched;
                if(a64_encode_deferred_scalar_memory(inst,memory,offset,
                                                     &scalar_matched,&code)) {
                    code|=(memory->base<<5U)|inst->operands[0].reg;
                    break;
                }
                if(scalar_matched) {
                    minias_set_error(as,
                        "aarch64-final-memory-offset-out-of-range:line=%zu",
                        stmt->line);
                    return false;
                }
            }
            if(memory->access_bytes==0U || offset<0 ||
               offset%(int64_t)memory->access_bytes!=0 ||
               offset/(int64_t)memory->access_bytes>4095) {
                minias_set_error(as,"aarch64-final-scaled-offset-out-of-range");
                return false;
            }
            code=inst->encoding|
                 ((uint32_t)(offset/(int64_t)memory->access_bytes)<<10U)|
                 (memory->base<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_PRFM_REG:
        case A64_MC_LDST_INDEX: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            if(!memory || memory->mode!=A64_ADDRESS_REGISTER_INDEX)return false;
            code=(inst->opcode==A64_MC_PRFM_REG?
                   UINT32_C(0xf8a00800):
                   inst->encoding-UINT32_C(0x00dff800))|
                 (memory->extend_option<<13U)|
                 (memory->scaled_index?UINT32_C(0x1000):0U)|
                 (memory->index<<16U)|(memory->base<<5U)|
                 (inst->opcode==A64_MC_PRFM_REG?
                  (uint32_t)inst->operands[0].immediate:inst->operands[0].reg);
            break;
        }
        case A64_MC_LDST_UNSCALED: {
            const A64Memory *memory=a64_memory_at(inst,1U);
            int64_t offset;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset) ||
               offset<-256 || offset>255) {
                minias_set_error(as,"aarch64-final-unscaled-offset-out-of-range");
                return false;
            }
            code=(inst->encoding-UINT32_C(0x01000000))|
                (memory->mode==A64_ADDRESS_PRE_INDEX?UINT32_C(0xc00):
                 memory->mode==A64_ADDRESS_POST_INDEX?UINT32_C(0x400):0U)|
                (((uint32_t)offset&511U)<<12U)|
                (memory->base<<5U)|inst->operands[0].reg;
            break;
        }
        case A64_MC_STP:
        case A64_MC_LDP:
        case A64_MC_LDPSW: {
            const A64Memory *memory=a64_memory_at(inst,2U);
            bool sf = inst->operands[0].register_64;
            bool qpair=inst->operands[0].target_reg_bank==A64_BANK_SIMD_Q;
            unsigned mode;
            int64_t offset,pair_imm;
            if(!memory || !a64_resolve_address_offset(as,memory,&offset) ||
               memory->access_bytes==0U ||
               offset%(int64_t)memory->access_bytes!=0 ||
               offset/(int64_t)memory->access_bytes < -64 ||
               offset/(int64_t)memory->access_bytes > 63) {
                minias_set_error(as,"aarch64-final-pair-offset-out-of-range");
                return false;
            }
            pair_imm=offset/(int64_t)memory->access_bytes;
            mode=inst->target_form==A64_PAIR_NONTEMPORAL?3U:
                 memory->mode==A64_ADDRESS_PRE_INDEX?1U:
                 memory->mode==A64_ADDRESS_POST_INDEX?2U:0U;
            uint32_t base = inst->opcode==A64_MC_LDPSW?
                (mode==1U?UINT32_C(0x69c00000):
                 mode==2U?UINT32_C(0x68c00000):
                                    UINT32_C(0x69400000)):
                inst->opcode == A64_MC_STP ?
                (mode == 1U ? UINT32_C(0x29800000) :
                 mode == 2U ? UINT32_C(0x28800000) :
                 mode == 3U ? UINT32_C(0x28000000) : UINT32_C(0x29000000)) :
                (mode == 1U ? UINT32_C(0x29c00000) :
                 mode == 2U ? UINT32_C(0x28c00000) :
                 mode == 3U ? UINT32_C(0x28400000) : UINT32_C(0x29400000));
            if(qpair) {
                /* FP/SIMD 128-bit pair: exact A64 V+opc fields; reuse
                 * the GPR signed imm7/pair and memory operand emitter. */
                base=(inst->opcode==A64_MC_STP?
                      (mode==1U?UINT32_C(0xad800000):
                       mode==2U?UINT32_C(0xac800000):
                       mode==3U?UINT32_C(0xac000000):UINT32_C(0xad000000)):
                      (mode==1U?UINT32_C(0xadc00000):
                       mode==2U?UINT32_C(0xacc00000):
                       mode==3U?UINT32_C(0xac400000):UINT32_C(0xad400000)));
            } else if(sf && inst->opcode!=A64_MC_LDPSW)
                base |= UINT32_C(0x80000000);
            code = base |
                   (((uint32_t)pair_imm & 127U) << 15U) |
                   (inst->operands[1].reg << 10U) |
                   (memory->base << 5U) | inst->operands[0].reg;
            break;
        }
        case A64_MC_BCOND:
        case A64_MC_TBZ:
        case A64_MC_TBNZ:
        case A64_MC_CBZ:
        case A64_MC_CBNZ:
        case A64_MC_B:
        case A64_MC_BL: {
            const MiniAsSymbol *target;
            int64_t distance;
            bool cbz=inst->opcode==A64_MC_CBZ ||
                     inst->opcode==A64_MC_CBNZ;
            bool bcond=inst->opcode==A64_MC_BCOND;
            bool testbit=inst->opcode==A64_MC_TBZ ||
                         inst->opcode==A64_MC_TBNZ;
            bool conditional=cbz||bcond;
            unsigned slot=testbit?2U:cbz?1U:0U;
            expr=inst->operands[slot].bound_symbol;
            if(inst->target_payload!=NULL) {
                const A64BranchAdjustment *adjustment=inst->target_payload;
                int64_t value;
                if(!minias_resolve_symbol_difference_ref(
                       as,&adjustment->difference,&value) ||
                   !minias_checked_add_i64(expr->addend,value,
                                           &resolved_branch_expr.addend)) {
                    minias_set_error(as,"aarch64-final-branch-addend-overflow");
                    return false;
                }
                resolved_branch_expr.name_id=expr->name_id;
                expr=&resolved_branch_expr;
            }
            if(testbit) {
                unsigned bit=(unsigned)inst->operands[1].immediate;
                code=(inst->opcode==A64_MC_TBZ ?
                       UINT32_C(0x36000000):UINT32_C(0x37000000)) |
                     ((bit&32U)?UINT32_C(0x80000000):0U) |
                     ((bit&31U)<<19U)|inst->operands[0].reg;
            } else if(bcond) {
                code=UINT32_C(0x54000000) | (inst->encoding&15U);
            } else if(cbz) {
                code=(inst->opcode==A64_MC_CBZ ?
                    UINT32_C(0x34000000):UINT32_C(0x35000000)) |
                    (inst->operands[0].register_64?UINT32_C(0x80000000):0U) |
                    inst->operands[0].reg;
            } else {
                code=inst->opcode==A64_MC_BL ?
                    UINT32_C(0x94000000):UINT32_C(0x14000000);
            }
            fixup=testbit?MINIAS_FIXUP_A64_TSTBR14:
                  conditional?MINIAS_FIXUP_A64_CONDBR19:
                  inst->opcode==A64_MC_BL ?
                    MINIAS_FIXUP_A64_CALL26:MINIAS_FIXUP_A64_JUMP26;
            target = minias_symbol_ref_lookup(as,*expr);
            if (target == NULL || !target->defined ||
                target->section != stmt->section) break;
            if (target->value > INT64_MAX || stmt->offset > INT64_MAX) {
                minias_set_error(as, "aarch64-branch-address-overflow");
                return false;
            }
            distance = (int64_t)target->value - (int64_t)stmt->offset;
            if ((expr->addend > 0 && distance > INT64_MAX - expr->addend) ||
                (expr->addend < 0 && distance < INT64_MIN - expr->addend)) {
                minias_set_error(as, "aarch64-branch-addend-overflow");
                return false;
            }
            distance += expr->addend;
            if ((distance & 3) != 0 ||
                distance < (testbit ? -INT64_C(32768):
                             conditional ? -INT64_C(1048576) :
                                           -INT64_C(134217728)) ||
                distance > (testbit ? INT64_C(32764):
                             conditional ? INT64_C(1048572) :
                                           INT64_C(134217724))) {
                minias_set_error(as, "aarch64-branch-out-of-range:%s:line=%zu",
                                 stmt->args, stmt->line);
                return false;
            }
            code |= testbit ?
                (((uint32_t)(distance/4)&UINT32_C(0x3fff))<<5U) :
                conditional ?
                (((uint32_t)(distance/4)&UINT32_C(0x7ffff))<<5U) :
                ((uint32_t)(distance/4)&UINT32_C(0x03ffffff));
            fixup = MINIAS_FIXUP_NONE;
            break;
        }
        default:
            minias_set_error(as, "unknown-aarch64-mc-opcode:%u:line=%zu",
                             inst->opcode, stmt->line);
            return false;
    }
    if (!append_word(as, stmt->section, code)) return false;
    if(fixup==MINIAS_FIXUP_NONE)return true;
    /* Target describes the binding policy without assuming that the last
     * element of Common's global Fixup array belongs to this instruction.
     * Both bytes and the relocation request are committed by the MC engine. */
    return minias_encode_add_fixup_ref(
        as,stmt->section,stmt->offset,fixup,4U,
        *expr,fixup==MINIAS_FIXUP_A64_LD_PREL_LO19);
}

/* GNU AArch64 profiles compose ISA extensions; do not enumerate each
 * combination as a hard-coded architecture name. Both .arch +feature
 * and .arch_extension reuse this one target-owned feature selector. */
static bool a64_set_isa_feature(uint32_t *bits,const char *name) {
    bool disable=false;
    uint32_t feature;
    if(!strncmp(name,"no",2U)) {
        name+=2U;
        disable=true;
    }
    if(!strcmp(name,"lse"))feature=A64_FEATURE_LSE;
    else if(!strcmp(name,"crc"))feature=A64_FEATURE_CRC;
    else if(!strcmp(name,"crypto"))feature=A64_FEATURE_CRYPTO;
    else if(!strcmp(name,"memtag"))feature=A64_FEATURE_MTE;
    else return false;
    if(disable)*bits&=~feature;
    else *bits|=feature;
    return true;
}

static bool directive(MiniAs *as, const char *op, const char *args,
                      size_t line, bool *handled) {
    A64TargetState *state=(A64TargetState *)as->target_state;
    *handled = false;
    if(strcmp(op,".unreq")==0) {
        A64AliasState *st=state?state->aliases:NULL;
        unsigned i;
        *handled=true;
        if(!st)return true;
        for(i=0U;i<st->count;++i)
            if(!strcmp(st->entries[i].alias,args)) {
                memmove(&st->entries[i],&st->entries[i+1U],
                        (st->count-i-1U)*sizeof(st->entries[0]));
                --st->count;
                return true;
            }
        return true;
    }
    if(op[0]!='.' && !strncmp(args,".req",4U) &&
       isspace((unsigned char)args[4])) {
        const char *canonical=args+4U;
        A64AliasState *st;
        unsigned n;bool wide,sp;size_t i;
        *handled=true;
        while(isspace((unsigned char)*canonical))++canonical;
        if((!parse_a64_reg(NULL,canonical,&n,&wide,&sp) &&
            !a64_req_vector_bank(canonical,&n)) ||
           !isalpha((unsigned char)op[0]) ||
           strlen(op)>=sizeof(((A64AliasState *)0)->entries[0].alias)) {
            minias_set_error(as,"bad-aarch64-register-alias:%s:%s:line=%zu",
                             op,canonical,line);
            return false;
        }
        for(i=1U;op[i];++i)
            if(!isalnum((unsigned char)op[i]) && op[i]!='_') {
                minias_set_error(as,"bad-aarch64-alias-name:%s:line=%zu",
                                 op,line);
                return false;
            }
        if(!state) {
            minias_set_error(as,"missing-aarch64-target-state");
            return false;
        }
        st=state->aliases;
        if(!st) {
            st=calloc(1U,sizeof(*st));
            if(!st){minias_set_error(as,"out-of-memory:aarch64-aliases");return false;}
            state->aliases=st;
        }
        for(i=0U;i<st->count;++i)
            if(!strcmp(st->entries[i].alias,op)) {
                minias_set_error(as,"duplicate-aarch64-alias:%s:line=%zu",
                                 op,line);
                return false;
            }
        if(st->count>=128U) {
            minias_set_error(as,"too-many-aarch64-aliases:line=%zu",line);
            return false;
        }
        strcpy(st->entries[st->count].alias,op);
        strcpy(st->entries[st->count].canonical,canonical);
        ++st->count;
        return true;
    }
    /* GCC emits .arch armv8-a before its first A64 instruction. Verify the
     * exact declared ISA here; never silently accept a richer architecture. */
    if (strcmp(op, ".arch") == 0) {
        const char *plus=strchr(args,'+');
        size_t base_len=plus?(size_t)(plus-args):strlen(args);
        uint32_t bits=a64_features(as)&
                      ~(A64_FEATURE_LSE|A64_FEATURE_TLBI_RANGE|
                        A64_FEATURE_CRC|A64_FEATURE_CRYPTO|A64_FEATURE_MTE);
        bool v85=base_len==strlen("armv8.5-a") &&
                 strncmp(args,"armv8.5-a",base_len)==0;
        bool v8=base_len==strlen("armv8-a") &&
                strncmp(args,"armv8-a",base_len)==0;
        *handled=true;
        if(!v85 && !v8) {
            minias_set_error(as,"unsupported-aarch64-architecture:%s:line=%zu",
                             args,line);
            return false;
        }
        if(v85)bits|=A64_FEATURE_LSE|A64_FEATURE_TLBI_RANGE|
                     A64_FEATURE_CRC;
        while(plus) {
            char name[32];
            const char *start=plus+1U,*next=strchr(start,'+');
            size_t width=next?(size_t)(next-start):strlen(start);
            if(width==0U || width>=sizeof(name)) {
                minias_set_error(as,
                                 "unsupported-aarch64-architecture:%s:line=%zu",
                                 args,line);
                return false;
            }
            memcpy(name,start,width);name[width]='\0';
            if(!a64_set_isa_feature(&bits,name)) {
                minias_set_error(as,
                                 "unsupported-aarch64-architecture:%s:line=%zu",
                                 args,line);
                return false;
            }
            plus=next;
        }
        /* Commit atomically only after ALL feature names were checked:
         * malformed trailing + and unknown extensions never widen ISA. */
        if(!state) {
            minias_set_error(as,"missing-aarch64-target-state");
            return false;
        }
        state->features=bits;
        return true;
    }
    if(!strcmp(op,".arch_extension")) {
        uint32_t bits=a64_features(as);
        *handled=true;
        if(!a64_set_isa_feature(&bits,args)) {
            minias_set_error(as,"unsupported-aarch64-extension:%s:line=%zu",
                             args,line);
            return false;
        }
        if(!state) {
            minias_set_error(as,"missing-aarch64-target-state");
            return false;
        }
        state->features=bits;
        return true;
    }
    if(!strcmp(op,".inst")) {
        /* .inst is a *data-list* directive, not a 4-operand instruction.
         * Validate every comma-delimited entry without the 4x96 MCInst
         * operand bound. Common still plans/emits every 32-bit data word;
         * this target only enforces valid AArch64 word widths. */
        char *copy=minias_strdup(args),*cursor;
        if(!copy) {
            minias_set_error(as,"out-of-memory:aarch64-inst-list");
            return false;
        }
        cursor=copy;
        for(;;) {
            char *comma=strchr(cursor,',');
            char *word;
            uint64_t bits;
            if(comma)*comma='\0';
            word=minias_trim(cursor);
            if(!*word || !minias_parse_absolute_data_bits(as,word,&bits) ||
               (bits>UINT32_MAX &&
                ((int64_t)bits>=0 || (int64_t)bits<INT32_MIN))) {
                minias_set_error(as,"invalid-aarch64-inst-word:%s:line=%zu",
                                 word,line);
                free(copy);return false;
            }
            if(!comma)break;
            cursor=comma+1U;
        }
        free(copy);
        return true;
    }
    if (strcmp(op, ".insn") == 0 || strcmp(op, ".option") == 0) {
        minias_set_error(as, "unsupported-aarch64-directive:%s:line=%zu",
                         op, line);
        return false;
    }
    return true;
}

static bool code_alignment(MiniAs *as, int section, size_t amount) {
    static const unsigned char nop[] = {0x1f, 0x20, 0x03, 0xd5};
    if (amount % sizeof(nop) != 0U) {
        minias_set_error(as, "unalignable-aarch64-code-gap:%zu", amount);
        return false;
    }
    while (amount != 0U) {
        if (!minias_emit_instruction_bytes(as, section, nop, sizeof(nop)))
            return false;
        amount -= sizeof(nop);
    }
    return true;
}

/* AArch64 owns ONE relocation descriptor table for target fixup support,
 * ELF ABI numbering and the accepted named `.reloc` subset.  Width remains
 * explicit: ABS32 and ABS64 are distinct ABI forms, not interchangeable. */
static const struct {
    MiniAsFixupKind kind;
    unsigned width;
    uint32_t relocation;
    const char *named_spelling; /* NULL: not accepted by .reloc name grammar */
} aarch64_fixup_forms[] = {
    {MINIAS_FIXUP_ABS, 4U, AARCH64_R_ABS32, "R_AARCH64_ABS32"},
    {MINIAS_FIXUP_ABS, 8U, AARCH64_R_ABS64, "R_AARCH64_ABS64"},
    {MINIAS_FIXUP_PREL32, 4U, AARCH64_R_PREL32, "R_AARCH64_PREL32"},
    {MINIAS_FIXUP_PREL64, 8U, AARCH64_R_PREL64, "R_AARCH64_PREL64"},
    {MINIAS_FIXUP_A64_CALL26, 4U, AARCH64_R_CALL26, NULL},
    {MINIAS_FIXUP_A64_JUMP26, 4U, AARCH64_R_JUMP26, NULL},
    {MINIAS_FIXUP_A64_CONDBR19, 4U, AARCH64_R_CONDBR19, NULL},
    {MINIAS_FIXUP_A64_LD_PREL_LO19, 4U, AARCH64_R_LD_PREL_LO19, NULL},
    {MINIAS_FIXUP_A64_TSTBR14, 4U, AARCH64_R_TSTBR14, NULL},
    {MINIAS_FIXUP_A64_ADR_LO21, 4U, AARCH64_R_ADR_PREL_LO21, NULL},
    {MINIAS_FIXUP_A64_ADRP_HI21, 4U, AARCH64_R_ADR_PREL_PG_HI21, NULL},
    {MINIAS_FIXUP_A64_ADR_GOT_PAGE, 4U, AARCH64_R_ADR_GOT_PAGE, NULL},
    {MINIAS_FIXUP_A64_LD64_GOT_LO12, 4U, AARCH64_R_LD64_GOT_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_ADD_LO12, 4U, AARCH64_R_ADD_ABS_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_LDST8_LO12, 4U, AARCH64_R_LDST8_ABS_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_LDST16_LO12, 4U, AARCH64_R_LDST16_ABS_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_LDST32_LO12, 4U, AARCH64_R_LDST32_ABS_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_LDST64_LO12, 4U, AARCH64_R_LDST64_ABS_LO12_NC, NULL},
    {MINIAS_FIXUP_A64_MOVW_G0, 4U, AARCH64_R_MOVW_UABS_G0, NULL},
    {MINIAS_FIXUP_A64_MOVW_G0_NC, 4U, AARCH64_R_MOVW_UABS_G0_NC, NULL},
    {MINIAS_FIXUP_A64_MOVW_G1, 4U, AARCH64_R_MOVW_UABS_G1, NULL},
    {MINIAS_FIXUP_A64_MOVW_G1_NC, 4U, AARCH64_R_MOVW_UABS_G1_NC, NULL},
    {MINIAS_FIXUP_A64_MOVW_G2, 4U, AARCH64_R_MOVW_UABS_G2, NULL},
    {MINIAS_FIXUP_A64_MOVW_G2_NC, 4U, AARCH64_R_MOVW_UABS_G2_NC, NULL},
    {MINIAS_FIXUP_A64_MOVW_G3, 4U, AARCH64_R_MOVW_UABS_G3, NULL},
    {MINIAS_FIXUP_A64_MOVW_SG0, 4U, AARCH64_R_MOVW_SABS_G0, NULL},
    {MINIAS_FIXUP_A64_MOVW_SG1, 4U, AARCH64_R_MOVW_SABS_G1, NULL},
    {MINIAS_FIXUP_A64_MOVW_SG2, 4U, AARCH64_R_MOVW_SABS_G2, NULL}
};

static bool aarch64_lookup_fixup_form(MiniAsFixupKind kind, unsigned width,
                                     uint32_t *relocation) {
    size_t i;
    for (i = 0U; i < sizeof(aarch64_fixup_forms) / sizeof(aarch64_fixup_forms[0]); ++i) {
        if (aarch64_fixup_forms[i].kind != kind ||
            aarch64_fixup_forms[i].width != width) continue;
        if (relocation != NULL) *relocation = aarch64_fixup_forms[i].relocation;
        return true;
    }
    return false;
}

static bool aarch64_supports_fixup(MiniAsFixupKind kind, unsigned width) {
    return aarch64_lookup_fixup_form(kind, width, NULL);
}

static uint32_t aarch64_fixup_relocation_type(MiniAsFixupKind kind,
                                              unsigned width) {
    uint32_t relocation = 0U;
    (void)aarch64_lookup_fixup_form(kind, width, &relocation);
    return relocation;
}

static bool validate(MiniAs *as) {
    size_t i;
    for (i = 0U; i < as->section_count; ++i) {
        const MiniAsSection *sec = &as->sections[i];
        if ((sec->flags & MINIAS_SHF_EXECINSTR) != 0U &&
            (sec->size % 4U) != 0U) {
            minias_set_error(as, "unaligned-aarch64-text:%s", sec->name);
            return false;
        }
    }
    return true;
}

static void attributes(const MiniAs *as, unsigned char elf_class,
                       MiniAsObjectAttribute *out) {
    (void)as;
    (void)elf_class;
    memset(out, 0, sizeof(*out));
}

static bool aarch64_resolve_profile(const char *march, const char *mabi,
                                    MiniAsAssemblyConfig *config,
                                    char *error, size_t capacity) {
    if ((march != NULL && strcmp(march, "armv8-a") != 0) ||
        (mabi != NULL && strcmp(mabi, "lp64") != 0)) {
        if (capacity != 0U) {
            (void)snprintf(error, capacity, "unsupported-aarch64-isa-abi");
        }
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->target = minias_target_aarch64();
    return true;
}

static bool a64_named_relocation(const char *name,
                                  MiniAsFixupKind *kind,unsigned *width) {
    size_t i;
    for(i=0U;i<sizeof(aarch64_fixup_forms)/
                 sizeof(aarch64_fixup_forms[0]);++i) {
        if(aarch64_fixup_forms[i].named_spelling!=NULL &&
           !strcmp(name,aarch64_fixup_forms[i].named_spelling)) {
            *kind=aarch64_fixup_forms[i].kind;
            *width=aarch64_fixup_forms[i].width;
            return true;
        }
    }
    return false;
}

static const MiniAsTargetOps ops = {
    .comment_prefix = '/', /* //, not #; AArch64 #42 is an immediate. */
    .elf_machine = 183U, /* EM_AARCH64 */
    .parse_target_directive = directive,
    .init_target_state = a64_init_target_state,
    .destroy_target_state = a64_destroy_target_state,
    .object_attributes = attributes,
    .name = "aarch64",
    .resolve_profile = aarch64_resolve_profile,
    .parse_mcinst = parse_mcinst,
    .measure_mcinst = aarch64_measure_mcinst,
    .encode = encode,
    .append_code_alignment = code_alignment,
    .supports_fixup = aarch64_supports_fixup,
    .fixup_relocation_type = aarch64_fixup_relocation_type,
    .parse_named_relocation = a64_named_relocation,
    .validate_output = validate,
    .dot_relative_fixup = MINIAS_FIXUP_PREL32,
    .inst_directive_width = 4U,
    .supports_literal_pools = true,
    .literal_pool_order = MINIAS_LITERAL_POOL_WIDTH_ASC,
    .cfi_implicit_eh_frame = true,
    .cfi_code_align = 4U,
    .cfi_data_align = -8,
    .cfi_return_reg = 30U,
    .cfi_stack_reg = 31U,
    .cfi_address_size = 8U,
    .cfi_negate_ra_opcode = 0x2dU,
};
const MiniAsTargetOps *minias_target_aarch64(void) {
    return &ops;
}
