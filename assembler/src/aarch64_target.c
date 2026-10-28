#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define A64_FEATURE_LSE UINT32_C(0x10000000) /* target_options feature bit */
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

static void a64_destroy_aliases(MiniAs *as) {
    free(as->target_state);
    as->target_state=NULL;
}

static char *a64_expand_register_aliases(MiniAs *as,const char *input) {
    const A64AliasState *st=(const A64AliasState *)as->target_state;
    size_t len=strlen(input),pos=0U,out=0U,limit;
    char *expanded;
    if(!st || !st->count)return minias_strdup(input);
    if(len>SIZE_MAX/8U-1U)return NULL;
    limit=8U*(len+1U);
    expanded=malloc(limit);
    if(!expanded)return NULL;
    while(pos<len) {
        unsigned k;
        size_t begin=pos;
        const char *replacement=NULL;
        if(isalpha((unsigned char)input[pos]) || input[pos]=='_') {
            ++pos;
            while(pos<len && (isalnum((unsigned char)input[pos]) ||
                              input[pos]=='_'))++pos;
            for(k=0U;k<st->count;++k) {
                size_t n=strlen(st->entries[k].alias);
                if(pos-begin==n &&
                   !memcmp(input+begin,st->entries[k].alias,n)) {
                    replacement=st->entries[k].canonical;
                    break;
                }
            }
        }else ++pos;
        if(replacement) {
            size_t n=strlen(replacement);
            if(n>=limit-out){free(expanded);return NULL;}
            memcpy(expanded+out,replacement,n);out+=n;
        }else {
            size_t n=pos-begin;
            if(n>=limit-out){free(expanded);return NULL;}
            memcpy(expanded+out,input+begin,n);out+=n;
        }
    }
    expanded[out]='\0';
    return expanded;
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
        int64_t immediate;
        if(minias_parse_symbol_addend(value,expr)) {
            *constant=false;*signed_negative=false;
        }else if(minias_eval_i64_expression(value,&immediate)) {
            /* GNU's checked signed SABS_Gn modifier rejects constants
             * outside its (16*n+16)-bit SIGNED range. UABS_Gn (without
             * NC) similarly rejects overflow above its chunk's
             * unsigned range. Do not silently truncate checked relocs. */
            unsigned width=(forms[i].hw+1U)*16U;
            bool checked_signed=forms[i].name[strlen(forms[i].name)-1U]=='s';
            bool unchecked=strstr(forms[i].name,"_nc")!=NULL;
            uint64_t value_bits=(uint64_t)immediate;
            if(checked_signed) {
                uint64_t bound=UINT64_C(1)<<(width-1U);
                if(immediate<-(int64_t)bound ||
                   immediate>=(int64_t)bound)return false;
            }else if(!unchecked && width<64U &&
                     value_bits>=(UINT64_C(1)<<width))return false;
            /* GNU mov_q expands to :abs_gN_s:CONSTANT as well as a
             * symbolic relocation.  Numeric modifiers must extract
             * the 16-bit HW lane, not be treated as unresolved symbols.
             * For signed negatives GNU selects MOVN and complements the
             * numeric lane before encoding. */
            *constant=true;
            *bits=(uint64_t)immediate;
            *signed_negative=immediate<0 &&
                             forms[i].name[strlen(forms[i].name)-1U]=='s';
        }else return false;
        *kind=forms[i].fixup;*group=forms[i].hw;
        return true;
    }
    return false;
}

static bool split_two(const char *args, char *first, size_t cap1,
                      char *second, size_t cap2) {
    const char *comma = strchr(args, ',');
    size_t length;
    if (comma == NULL || strchr(comma + 1, ',') != NULL) return false;
    length = (size_t)(comma - args);
    while (length != 0U && isspace((unsigned char)args[length - 1U]))
        --length;
    if (length == 0U || length >= cap1) return false;
    memcpy(first, args, length);
    first[length] = '\0';
    args = comma + 1;
    while (isspace((unsigned char)*args)) ++args;
    length = strlen(args);
    while (length != 0U && isspace((unsigned char)args[length - 1U]))
        --length;
    if (length == 0U || length >= cap2) return false;
    memcpy(second, args, length);
    second[length] = '\0';
    return true;
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
    return minias_section_append(as, section, bytes, sizeof(bytes));
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
    A64_MC_MOVZ,
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
    A64_MC_LDR,
    A64_MC_STR,
    A64_MC_LDRB,
    A64_MC_STRB,
    A64_MC_LDRH,
    A64_MC_STRH,
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
    A64_MC_EXCLUSIVE_PAIR
};

/* GNU AArch64 operands split on TOP-LEVEL commas, not commas within
 * [xN,#offset]. Unlike copying a generic tokenizer into every target,
 * this helper is strictly A64 addressing grammar. */
static bool split_operands(const char *args, char out[4][96], unsigned *n) {
    unsigned depth = 0U, braces = 0U, count = 0U;
    const char *start = args, *p = args;
    for (;;) {
        char ch = *p;
        if (ch == '[') ++depth;
        if (ch == ']') {
            if (!depth) return false;
            --depth;
        }
        if (ch == '{') ++braces;
        if (ch == '}') {
            if (!braces) return false;
            --braces;
        }
        if ((ch == ',' && depth == 0U && braces == 0U) || ch == '\0') {
            size_t len = (size_t)(p - start);
            while (len && isspace((unsigned char)*start)) { ++start; --len; }
            while (len && isspace((unsigned char)start[len - 1U])) --len;
            if (!len || len >= sizeof(out[0]) || count == 4U || depth || braces)
                return false;
            memcpy(out[count], start, len);
            out[count][len] = '\0';
            ++count;
            if (ch == '\0') break;
            start = p + 1;
        }
        if (ch == '\0') break;
        ++p;
    }
    *n = count;
    return true;
}

/* SIMD/FP registers are a distinct target ISA register bank. Avoid
 * pretending Q/V operands are GPRs or changing the common MC register
 * representation: bank selection is kept in a target encoding flag. */
static bool parse_a64_q_reg(const char *text,unsigned *number) {
    char *end;
    unsigned long n;
    if(text[0]!='q' || !isdigit((unsigned char)text[1]))return false;
    errno=0;n=strtoul(text+1U,&end,10);
    if(errno || *end!='\0' || n>31UL)return false;
    *number=(unsigned)n;return true;
}

/* One vector register arrangement for LD1/ST1, no multi-vector lists
 * or lane load pseudo-expansions accepted until independently proven. */
static bool parse_a64_single_v(const char *text,unsigned *reg,
                               uint32_t *arrange) {
    char copy[96],*p,*end;
    unsigned long n;
    size_t len=strlen(text);
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
static bool a64_vector_view(const char *text,unsigned *reg,
                            uint32_t *arrange) {
    char wrapped[96];
    size_t n=strlen(text);
    if(n<4U || n+3U>sizeof(wrapped))return false;
    wrapped[0]='{';
    memcpy(wrapped+1U,text,n);
    wrapped[n+1U]='}';wrapped[n+2U]='\0';
    return parse_a64_single_v(wrapped,reg,arrange);
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
static bool a64_structure_vlist(const char *text,unsigned *first,
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
        if(!a64_vector_view(minias_trim(body),&start,&flags) ||
           !a64_vector_view(minias_trim(dash+1U),&last,&each) ||
           flags!=each)return false;
        member=(last+32U-start)%32U+1U;
        if(member<2U || member>4U)return false;
    }else {
        p=body;start=0U;flags=0U;
        do {
            comma=strchr(p,',');
            if(comma)*comma='\0';
            if(!a64_vector_view(minias_trim(p),&last,&each) ||
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
static bool parse_a64_reg(const char *text, unsigned *number, bool *is64,
                          bool *is_sp) {
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

static bool a64_sys_fields(const char *args,uint32_t *word,unsigned *rt) {
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
    if(!parse_a64_reg(tok[4],rt,&wide,&sp) || !wide || sp)return false;
    *word=UINT32_C(0xd5080000)|
          ((uint32_t)numbers[0]<<16U)|
          ((uint32_t)numbers[1]<<12U)|
          ((uint32_t)numbers[2]<<8U)|
          ((uint32_t)numbers[3]<<5U);
    return true;
}

static void put_reg(MiniAsMCOperand *dst, unsigned number, bool reg64) {
    dst->kind = MINIAS_MC_REGISTER;
    dst->reg = number;
    dst->register_64 = reg64;
}

static void put_immediate(MiniAsMCOperand *dst, int64_t value) {
    dst->kind = MINIAS_MC_IMMEDIATE;
    dst->immediate = (uint64_t)value;
}

/* Only unsigned-scaled load/store forms; do not alias negative offsets,
 * register-indexed modes or other A64 memory encodings silently. */
static bool parse_a64_mem(const char *text, unsigned *base, int64_t *offset,
                          unsigned *mode) {
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
    if (!parse_a64_reg(regtext, base, &is64, &issp) || !is64)
        return false;
    *offset = 0;
    if (comma) {
        while (isspace((unsigned char)*comma)) ++comma;
        size = strlen(comma);
        while (size && isspace((unsigned char)comma[size - 1U]))
            comma[--size] = '\0';
        if (!parse_signed_number(comma, offset)) return false;
    }
    return true;
}

/* Atomic 128-bit CASP{A,L,AL} GNU operand sequence has 4 GPRs
 * plus one memory register. The typed MCInst encodes only the leading
 * register of each consecutive even pair; validate both members here.
 * Reuse the EXISTING four-operand A64 tokenizer after isolating [Rn].
 */
static bool a64_casp_fields(const char *args,unsigned *rs,unsigned *rt,
                             unsigned *rn,bool *wide) {
    char buffer[384],tokens[4][96],*last;
    unsigned n,r1,r3;
    bool w0,w1,w2,w3,sp;
    int64_t offset;unsigned mode;
    size_t len=strlen(args);
    if(len>=sizeof(buffer))return false;
    memcpy(buffer,args,len+1U);
    last=strrchr(buffer,',');
    if(!last)return false;
    *last++='\0';last=minias_trim(last);
    if(!split_operands(buffer,tokens,&n) || n!=4U ||
       !parse_a64_reg(tokens[0],rs,&w0,&sp) || sp ||
       !parse_a64_reg(tokens[1],&r1,&w1,&sp) || sp ||
       !parse_a64_reg(tokens[2],rt,&w2,&sp) || sp ||
       !parse_a64_reg(tokens[3],&r3,&w3,&sp) || sp ||
       !(w0==w1 && w0==w2 && w0==w3) ||
       (*rs&1U) || (*rt&1U) ||
       *rs>=30U || *rt>=30U ||
       r1!=*rs+1U || r3!=*rt+1U ||
       !parse_a64_mem(last,rn,&offset,&mode) ||
       offset!=0 || mode!=0U)return false;
    *wide=w0;return true;
}

/* Element-indexed INS Vd.T[index], Rn.
 * The lane index is the architectural imm5 encoded in bits[20:16].
 * No independent FP register representation is needed in Common IR. */
static bool a64_ins_lane(const char *text,unsigned *vd,uint32_t *imm5,
                          bool *x_source) {
    char *end;unsigned long num,index;
    char elem;
    unsigned bits;
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

/* A64 register-indexed memory with typed base/index, extension and scale.
 * This does not change shared Section, MCInst, Fixup or ELF ownership. */
static bool a64_indexed_memory(const char *text,unsigned *base,
                                unsigned *index,unsigned *option,
                                bool *scaled,unsigned amount_shift) {
    char inside[96],tokens[4][96];
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
       !parse_a64_reg(tokens[0],base,&wide,&sp) || !wide ||
       !parse_a64_reg(tokens[1],index,&wide,&sp) || sp)return false;
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
/* Parsed target-owned instruction family. Complete AArch64 still requires
 * ADRP/GOT, conditional branches, shifts, and relocation fixup bitfields. */
static bool parse_integer_family(MiniAsMCInst *inst,
                                 const char *op, const char *args) {
    char t[4][96];
    unsigned n = 0U, rd, rn, rm, base, mode;
    bool wd, wn, wm, spd, spn, spm;
    int64_t imm;
    unsigned kind = 0U;

    if (!split_operands(args, t, &n)) return false;
    /* Explicit add/sub 12-bit LSL #12 is a *distinct* instruction form
     * even for #0: keep shift bit for GNU exact-byte comparison. */
    if(n==4U && (!strcmp(op,"add") || !strcmp(op,"sub") ||
                 !strcmp(op,"adds") || !strcmp(op,"subs")) &&
       parse_a64_reg(t[0],&rd,&wd,&spd) &&
       parse_a64_reg(t[1],&rn,&wn,&spn) && wd==wn &&
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
            if(parse_a64_reg(t[0],&erd,&dwide,&dsp) &&
               (cmp || parse_a64_reg(t[1],&ern,&nwide,&nsp)) &&
               parse_a64_reg(t[cmp?1U:2U],&erm,&mwide,&msp) && !msp &&
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
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(t[1],&rm,&wm,&spm) || spm || wm!=wd ||
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
            if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
               (!compare && (!parse_a64_reg(t[1],&rn,&wn,&spn) ||
                             spn || wn!=wd)) ||
               !parse_a64_reg(t[index],&rm,&wm,&spm) || spm ||
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
        static const uint32_t offsets[]={0U,UINT32_C(0x7000),
             UINT32_C(0xa000),UINT32_C(0x6000),UINT32_C(0x2000)};
        if(!a64_structure_vlist(t[0],&rd,&arrangement,&vector_count) ||
           !parse_a64_mem(t[1],&base,&imm,&mode) ||
           mode!=0U || imm!=0)return false;
        inst->opcode=A64_MC_SIMD_SINGLE;inst->count=n;
        inst->encoding=arrangement|
                       offsets[vector_count]|
                       (!strcmp(op,"ld1")?UINT32_C(0x0c400000):
                                          UINT32_C(0x0c000000));
        put_reg(&inst->operands[0],rd,true);
        put_reg(&inst->operands[1],base,true);
        if(n==3U) {
            unsigned offset_reg=31U;
            int64_t increment;
            bool wide=false,sp=false;
            /* GNU Q=1 writes back 16 bytes, Q=0 writes back eight.
             * Keep immediate post-increment and register post-index
             * inside this existing typed SIMD MCInst; no duplicated
             * machine encoder or memory-parser guesswork. */
            if(parse_signed_number(t[2],&increment)) {
                if(increment!=
                   (int64_t)(vector_count*
                      ((arrangement&UINT32_C(0x40000000))?16U:8U)))
                    return false;
                put_immediate(&inst->operands[2],31);
            } else {
                if(!parse_a64_reg(t[2],&offset_reg,&wide,&sp) ||
                   !wide || sp)return false;
                put_reg(&inst->operands[2],offset_reg,true);
            }
            inst->encoding|=UINT32_C(0x00800000);
        }
        return true;
    }
    /* Common GNU Linux bit/byte-swap and acquire/release families.
     * All operands are typed once; no instruction-name checks in encode. */
    if ((!strcmp(op,"ldxr") || !strcmp(op,"ldaxr") ||
         !strcmp(op,"ldxrb") || !strcmp(op,"ldaxrb") ||
         !strcmp(op,"ldxrh") || !strcmp(op,"ldaxrh")) && n==2U) {
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_mem(t[1],&base,&imm,&mode) ||
           imm!=0 || mode!=0U)return false;
        if((strchr(op,'b') || strchr(op,'h')) && wd)return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=2U;
        inst->encoding=(strncmp(op,"ldax",4U)==0?1U:0U)|
                        (op[strlen(op)-1U]=='b'?0x10U:
                         op[strlen(op)-1U]=='h'?0x20U:0U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],base,true);
        return true;
    }
    if ((!strcmp(op,"stxr") || !strcmp(op,"stlxr") ||
         !strcmp(op,"stxrb") || !strcmp(op,"stlxrb") ||
         !strcmp(op,"stxrh") || !strcmp(op,"stlxrh")) && n==3U) {
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || wd || spd ||
           !parse_a64_reg(t[1],&rn,&wn,&spn) || spn ||
           !parse_a64_mem(t[2],&base,&imm,&mode) ||
           imm!=0 || mode!=0U)return false;
        if((op[strlen(op)-1U]=='b' || op[strlen(op)-1U]=='h') && wn)
            return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=3U;
        inst->encoding=(strncmp(op,"stlx",4U)==0?3U:2U)|
                        (op[strlen(op)-1U]=='b'?0x10U:
                         op[strlen(op)-1U]=='h'?0x20U:0U);
        put_reg(&inst->operands[0],rd,false);
        put_reg(&inst->operands[1],rn,wn);
        put_reg(&inst->operands[2],base,true);
        return true;
    }
    if(!strcmp(op,"prfm") && n==2U) {
        unsigned hint=0U,unit=0U,index,option;
        bool scaled;
        unsigned j;
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
        if(a64_indexed_memory(t[1],&base,&index,&option,&scaled,3U)) {
            /* 0xF8A0_0800 shares the indexed LDST encoder's option and
             * scaled bit fields; hint Rt is an immediate 0..31 rather
             * than a GPR. */
            inst->opcode=A64_MC_PRFM_REG;inst->count=3U;
            inst->encoding=UINT32_C(0xf8a00800)|(option<<13U)|
                          (scaled?UINT32_C(0x1000):0U);
            put_immediate(&inst->operands[0],(int64_t)hint);
            put_reg(&inst->operands[1],base,true);
            put_reg(&inst->operands[2],index,option==3U || option==7U);
            return true;
        }
        if(!parse_a64_mem(t[1],&base,&imm,&mode) ||
           mode!=0U || imm<0 || imm%8!=0 || imm/8>4095)
            return false;
        inst->opcode=A64_MC_PRFM;inst->count=3U;
        put_immediate(&inst->operands[0],(int64_t)hint);
        put_reg(&inst->operands[1],base,true);
        put_immediate(&inst->operands[2],imm/8);
        return true;
    }
    /* Short sign/zero extensions lower to the existing typed SBFM/UBFM
     * encoder; GNU's UXT{B,H} always writes Wd (zeroing upper X bits). */
    if((!strcmp(op,"sxtb") || !strcmp(op,"sxth") ||
        !strcmp(op,"uxtb") || !strcmp(op,"uxth")) && n==2U) {
        bool signed_alias=op[0]=='s';
        bool byte_alias=op[3]=='b';
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(t[1],&rn,&wn,&spn) || spn || wn ||
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
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || !wd || spd ||
           !parse_a64_reg(t[1],&rn,&wn,&spn) || wn || spn)return false;
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
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_reg(t[1],&rn,&wn,&spn) || spn || wd!=wn ||
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
        bool load=op[0]=='l';
        bool byte=op[4]=='b';
        bool half=op[4]=='h';
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           ((byte || half) && wd) ||
           !parse_a64_mem(t[1],&base,&imm,&mode) ||
           imm!=0 || mode!=0U)return false;
        inst->opcode=A64_MC_ACQUIRE_RELEASE;inst->count=2U;
        inst->encoding=byte?(load?2U:4U):
                       half?(load?3U:5U):(load?1U:0U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],base,true);
        return true;
    }
    if(!strcmp(op,"tst") && n==2U) {
        uint64_t bits;uint32_t fields;
        if(!parse_a64_reg(t[0],&rn,&wn,&spn) || spn)return false;
        if(parse_a64_reg(t[1],&rm,&wm,&spm)) {
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
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) ||
            !parse_a64_reg(t[1], &rn, &wn, &spn) || wd != wn)
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
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) ||
            !parse_a64_reg(t[1], &rn, &wn, &spn) || wd != wn)
            return false;
        if (parse_a64_reg(t[2], &rm, &wm, &spm)) {
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
        } else if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
                    strcmp(op, "adds") == 0 || strcmp(op, "subs") == 0) &&
                   parse_signed_number(t[2], &imm) &&
                   imm >= 0 && (imm<=4095 ||
                   (imm%4096==0 && imm/4096<=4095))) {
            if ((rd == 31U && !spd) || (rn == 31U && !spn))
                return false;
            kind = strcmp(op, "add") == 0 ? A64_MC_ADD_IMM :
                   strcmp(op, "sub") == 0 ? A64_MC_SUB_IMM :
                   strcmp(op, "adds") == 0 ? A64_MC_ADDS_IMM : A64_MC_SUBS_IMM;
            put_immediate(&inst->operands[2], imm);
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
        if (!parse_a64_reg(t[0], &rn, &wn, &spn)) return false;
        if (parse_a64_reg(t[1], &rm, &wm, &spm)) {
            if (wn != wm || spn || spm) return false;
            kind = strcmp(op,"cmp")==0 ? A64_MC_CMP_REG : A64_MC_CMN_REG;
            put_reg(&inst->operands[1], rm, wm);
        } else if (parse_signed_number(t[1], &imm) &&
                   imm >= 0 && (imm <= 4095 ||
                   (imm%4096==0 && imm/4096<=4095))) {
            kind = strcmp(op,"cmp")==0 ? A64_MC_CMP_IMM : A64_MC_CMN_IMM;
            put_immediate(&inst->operands[1], imm);
        } else return false;
        inst->opcode = kind;
        inst->count = 2U;
        put_reg(&inst->operands[0], rn, wn);
        return true;
    }

    if ((!strcmp(op,"ldr") || !strcmp(op,"str") ||
         !strcmp(op,"ldrb") || !strcmp(op,"strb") ||
         !strcmp(op,"ldrh") || !strcmp(op,"strh") ||
         !strcmp(op,"ldrsw") || !strcmp(op,"ldrsb") ||
         !strcmp(op,"ldrsh")) && n==2U) {
        unsigned regbase,index,option,unitshift;
        bool scaled;
        bool byte=!strcmp(op,"ldrb") || !strcmp(op,"strb") ||
                  !strcmp(op,"ldrsb");
        bool half=!strcmp(op,"ldrh") || !strcmp(op,"strh") ||
                  !strcmp(op,"ldrsh");
        bool ldrsw=!strcmp(op,"ldrsw");
        uint32_t encoding;
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           ((byte || half) && op[3]!='s' && wd) ||
           (ldrsw && !wd))return false;
        unitshift=byte?0U:half?1U:(ldrsw || !wd)?2U:3U;
        if(a64_indexed_memory(t[1],&regbase,&index,&option,&scaled,
                              unitshift)) {
            encoding=!strcmp(op,"ldr")?(wd?UINT32_C(0xf8600800):
                                             UINT32_C(0xb8600800)):
                     !strcmp(op,"str")?(wd?UINT32_C(0xf8200800):
                                             UINT32_C(0xb8200800)):
                     !strcmp(op,"ldrb")?UINT32_C(0x38600800):
                     !strcmp(op,"strb")?UINT32_C(0x38200800):
                     !strcmp(op,"ldrh")?UINT32_C(0x78600800):
                     !strcmp(op,"strh")?UINT32_C(0x78200800):
                     ldrsw?UINT32_C(0xb8a00800):
                     !strcmp(op,"ldrsb")?(wd?UINT32_C(0x38a00800):
                                             UINT32_C(0x38e00800)):
                                          (wd?UINT32_C(0x78a00800):
                                             UINT32_C(0x78e00800));
            inst->opcode=A64_MC_LDST_INDEX;inst->count=3U;
            inst->encoding=encoding | (option<<13U) |
                            (scaled?UINT32_C(0x1000):0U);
            put_reg(&inst->operands[0],rd,wd);
            put_reg(&inst->operands[1],regbase,true);
            put_reg(&inst->operands[2],index,option==3U || option==7U);
            return true;
        }
    }
    /* Signed LDRSW/LDRSB/LDRSH, including signed imm9 writeback and
     * scaled unsigned imm12. Reuse machine-word and section emitters. */
    if((!strcmp(op,"ldrsw") || !strcmp(op,"ldrsb") ||
        !strcmp(op,"ldrsh") || !strcmp(op,"ldursw") ||
        !strcmp(op,"ldursb") || !strcmp(op,"ldursh")) &&
       (n==2U || n==3U)) {
        bool byte=!strcmp(op,"ldrsb") || !strcmp(op,"ldursb");
        bool half=!strcmp(op,"ldrsh") || !strcmp(op,"ldursh");
        bool forced=!strncmp(op,"ldur",4U);
        unsigned unit=byte?1U:half?2U:4U;
        uint32_t opcodebits;
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           (!byte && !half && !wd) ||
           !parse_a64_mem(t[1],&base,&imm,&mode))return false;
        if(n==3U) {
            if(forced || mode!=0U || imm!=0 ||
               !parse_signed_number(t[2],&imm))return false;
            mode=2U;
        }
        if(forced || mode!=0U || imm<0 ||
           imm%(int64_t)unit!=0) {
            if(imm<-256 || imm>255)return false;
            inst->opcode=A64_MC_LDST_UNSCALED;
            opcodebits=byte?(wd?UINT32_C(0x38800000):
                                UINT32_C(0x38c00000)):
                       half?(wd?UINT32_C(0x78800000):
                                UINT32_C(0x78c00000)):
                            UINT32_C(0xb8800000);
            inst->encoding=opcodebits|
                           (mode==1U?UINT32_C(0xc00):
                            mode==2U?UINT32_C(0x400):0U);
            put_immediate(&inst->operands[2],imm);
        }else {
            if(imm/(int64_t)unit>4095)return false;
            inst->opcode=A64_MC_LDST_SCALED;
            opcodebits=byte?(wd?UINT32_C(0x39800000):
                                UINT32_C(0x39c00000)):
                       half?(wd?UINT32_C(0x79800000):
                                UINT32_C(0x79c00000)):
                            UINT32_C(0xb9800000);
            inst->encoding=opcodebits;
            put_immediate(&inst->operands[2],imm/(int64_t)unit);
        }
        inst->count=3U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],base,true);
        return true;
    }
    if ((strcmp(op, "ldr") == 0 || strcmp(op, "str") == 0 ||
         strcmp(op, "ldrb") == 0 || strcmp(op, "strb") == 0 ||
         strcmp(op, "ldrh") == 0 || strcmp(op, "strh") == 0 ||
         strcmp(op, "ldur") == 0 || strcmp(op, "stur") == 0 ||
         strcmp(op, "ldurb") == 0 || strcmp(op, "sturb") == 0 ||
         strcmp(op, "ldurh") == 0 || strcmp(op, "sturh") == 0) &&
         (n == 2U || n == 3U)) {
        unsigned unit;
        bool explicitly_unscaled=strstr(op,"dur")!=NULL ||
                                 strstr(op,"tur")!=NULL;
        bool byte=!strcmp(op,"ldrb") || !strcmp(op,"strb") ||
                  !strcmp(op,"ldurb") || !strcmp(op,"sturb");
        bool half=!strcmp(op,"ldrh") || !strcmp(op,"strh") ||
                  !strcmp(op,"ldurh") || !strcmp(op,"sturh");
        bool load=op[0]=='l';
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) || spd ||
            !parse_a64_mem(t[1], &base, &imm, &mode))
            return false;
        if(n==3U) {
            if(explicitly_unscaled || mode!=0U || imm!=0 ||
               !parse_signed_number(t[2],&imm))return false;
            mode=2U;
        }
        unit = byte?1U:half?2U:wd?8U:4U;
        if ((byte || half) && wd) return false;
        if(explicitly_unscaled || mode!=0U || imm<0 ||
           imm%(int64_t)unit!=0) {
            if(imm < -256 || imm > 255)return false;
            inst->opcode=A64_MC_LDST_UNSCALED;
            inst->encoding=byte?
                (load?UINT32_C(0x38400000):UINT32_C(0x38000000)):
                half?(load?UINT32_C(0x78400000):UINT32_C(0x78000000)):
                wd?(load?UINT32_C(0xf8400000):UINT32_C(0xf8000000)):
                    (load?UINT32_C(0xb8400000):UINT32_C(0xb8000000));
            inst->encoding|=mode==1U?UINT32_C(0xc00):
                            mode==2U?UINT32_C(0x400):0U;
            inst->count=3U;
            put_reg(&inst->operands[0],rd,wd);
            put_reg(&inst->operands[1],base,true);
            put_immediate(&inst->operands[2],imm);
            return true;
        }
        if (imm / (int64_t)unit > 4095)return false;
        inst->opcode = strcmp(op, "ldr") == 0 ? A64_MC_LDR :
                       strcmp(op, "str") == 0 ? A64_MC_STR :
                       strcmp(op, "ldrb") == 0 ? A64_MC_LDRB :
                       strcmp(op, "strb") == 0 ? A64_MC_STRB :
                       strcmp(op, "ldrh") == 0 ? A64_MC_LDRH : A64_MC_STRH;
        inst->count = 3U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], base, true);
        put_immediate(&inst->operands[2], imm / (int64_t)unit);
        return true;
    }
    /* Unprivileged signed imm9 load/store family. LDTR/STTR do not share
     * the pre/postindex LDUR writeback bit even for identical imm9 ranges. */
    if((!strcmp(op,"ldtr") || !strcmp(op,"sttr") ||
        !strcmp(op,"ldtrb") || !strcmp(op,"sttrb") ||
        !strcmp(op,"ldtrh") || !strcmp(op,"sttrh") ||
        !strcmp(op,"ldtrsw")) && n==2U) {
        bool load=op[0]=='l';
        bool byte=op[4]=='b';
        bool half=op[4]=='h';
        bool signedword=!strcmp(op,"ldtrsw");
        uint32_t opbits;
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           ((byte || half) && wd) || (signedword && !wd) ||
           !parse_a64_mem(t[1],&base,&imm,&mode) || mode!=0U ||
           imm<-256 || imm>255)return false;
        opbits=signedword?UINT32_C(0xb8800800):
               byte?(load?UINT32_C(0x38400800):UINT32_C(0x38000800)):
               half?(load?UINT32_C(0x78400800):UINT32_C(0x78000800)):
               wd?(load?UINT32_C(0xf8400800):UINT32_C(0xf8000800)):
                  (load?UINT32_C(0xb8400800):UINT32_C(0xb8000800));
        inst->opcode=A64_MC_LDST_UNPRIV;inst->count=3U;
        inst->encoding=opbits;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],base,true);
        put_immediate(&inst->operands[2],imm);
        return true;
    }
    if ((!strcmp(op, "stp") || !strcmp(op, "ldp") ||
         !strcmp(op,"ldpsw") || !strcmp(op,"stnp") ||
         !strcmp(op,"ldnp")) && (n == 3U || n == 4U)) {
        bool signed_pair=!strcmp(op,"ldpsw");
        bool nontemporal=!strcmp(op,"stnp") || !strcmp(op,"ldnp");
        bool qpair=parse_a64_q_reg(t[0],&rd);
        if(qpair) {
            if(signed_pair || !parse_a64_q_reg(t[1],&rn))return false;
            wd=true;wn=true;spd=false;spn=false;
        } else if(!parse_a64_reg(t[0], &rd, &wd, &spd) || spd ||
                  !parse_a64_reg(t[1], &rn, &wn, &spn) ||
                  spn || wd!=wn)return false;
        if(!parse_a64_mem(t[2], &base, &imm, &mode))return false;
        if(signed_pair && !wd)return false;
        /* LDNP/STNP have ONLY signed scaled imm7 offset addressing;
         * pre/post writeback encodings belong to LDP/STP. */
        if(nontemporal && (n!=3U || mode!=0U))return false;
        if (n == 4U) {
            if (mode != 0U || imm != 0 ||
                !parse_signed_number(t[3], &imm)) return false;
            mode = 2U;
        }
        if (imm % (qpair?16:(signed_pair?4:(wd?8:4))) != 0 ||
            imm < -(int64_t)(qpair?1024:(signed_pair?256:(wd?512:256))) ||
            imm > (int64_t)(qpair?1008:(signed_pair?252:(wd?504:252))))
            return false;
        inst->opcode = signed_pair?A64_MC_LDPSW:
                       (!strcmp(op, "stp") || !strcmp(op,"stnp")) ?
                          A64_MC_STP : A64_MC_LDP;
        /* 3 is Target-only non-temporal pair variant; existing typed
         * pair MCInst and Common ELF/section machinery are reused. */
        inst->encoding = (nontemporal?3U:mode) |
                         (qpair?UINT32_C(0x100):0U);
        inst->count = 4U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], rn, wn);
        put_reg(&inst->operands[2], base, true);
        put_immediate(&inst->operands[3],
                      imm / (qpair?16:(signed_pair?4:(wd?8:4))));
        return true;
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

static bool a64_attach_expr(MiniAsMCInst *inst,unsigned slot,
                            const MiniAsSymbolExpr *expr) {
    MiniAsSymbolExpr *copy=malloc(sizeof(*copy));
    if(!copy)return false;
    *copy=*expr;
    inst->operands[slot].kind=MINIAS_MC_SYMBOL;
    inst->operands[slot].symbol=copy;
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

static bool a64_parse_relocated(MiniAsMCInst *inst,
                                const char *op,const char *args) {
    char tok[4][96];
    unsigned n,rd,rn,unit=0U;
    bool wd,wn,spd,spn;
    MiniAsSymbolExpr expr;
    if(!split_operands(args,tok,&n))return false;
    if(!strcmp(op,"adr") && n==2U &&
       parse_a64_reg(tok[0],&rd,&wd,&spd) && wd && !spd &&
       minias_parse_symbol_addend(tok[1],&expr) &&
       a64_attach_expr(inst,1U,&expr)) {
        inst->opcode=A64_MC_ADR;inst->count=2U;
        put_reg(&inst->operands[0],rd,true);
        return true;
    }
    if(!strcmp(op,"adrp") && n==2U &&
       parse_a64_reg(tok[0],&rd,&wd,&spd) && wd && !spd) {
        /* GNU :got:SYMBOL selects a DIFFERENT ELF relocation from the
         * default page-relative ADRP. The instruction bytes are equal,
         * but confusing 275 with 311 would break Linux GOT links. */
        bool got=strncmp(tok[1],":got:",5U)==0;
        if(!minias_parse_symbol_addend(got?tok[1]+5U:tok[1],&expr) ||
           !a64_attach_expr(inst,1U,&expr))return false;
        inst->opcode=A64_MC_ADRP;inst->count=2U;
        inst->encoding=got?1U:0U;
        put_reg(&inst->operands[0],rd,true);
        return true;
    }
    if(!strcmp(op,"add") && n==3U &&
       parse_a64_reg(tok[0],&rd,&wd,&spd) &&
       parse_a64_reg(tok[1],&rn,&wn,&spn) && wd==wn &&
       !(rd==31U&&!spd) && !(rn==31U&&!spn) &&
       a64_lo12_expr(tok[2],&expr) &&
       a64_attach_expr(inst,2U,&expr)) {
        inst->opcode=A64_MC_ADD_LO12;inst->count=3U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        return true;
    }
    if(n!=2U || !parse_a64_reg(tok[0],&rd,&wd,&spd) || spd)
        return false;
    if(strcmp(op,"ldr")!=0 && strcmp(op,"str")!=0 &&
       strcmp(op,"ldrb")!=0 && strcmp(op,"strb")!=0 &&
       strcmp(op,"ldrh")!=0 && strcmp(op,"strh")!=0 &&
       strcmp(op,"ldrsw")!=0)
        return false;
    /* LDRSW is an X-only destination, despite its 32-bit source
     * relocation scale. Never permit GNU-invalid Wd here. */
    if(!strcmp(op,"ldrsw") && !wd)return false;
    {
        size_t sz=strlen(tok[1]);
        char inner[96],*comma,*b;
        unsigned regnum;
        bool got_lo12;
        if(sz<4U || tok[1][0]!='[' || tok[1][sz-1U]!=']' ||
           sz-2U>=sizeof(inner))return false;
        memcpy(inner,tok[1]+1U,sz-2U);inner[sz-2U]='\0';
        comma=strchr(inner,',');
        if(!comma)return false;
        *comma++='\0';
        if(!parse_a64_reg(minias_trim(inner),&regnum,&wn,&spn) || !wn)
            return false;
        b=minias_trim(comma);
        if(*b=='#')++b;
        got_lo12=strncmp(b,":got_lo12:",10U)==0;
        if(got_lo12 && (strcmp(op,"ldr")!=0 || !wd))return false;
        if(!(got_lo12?minias_parse_symbol_addend(b+10U,&expr):
                        a64_lo12_expr(b,&expr)) ||
           !a64_attach_expr(inst,2U,&expr))return false;
        rn=regnum;
        if(got_lo12)inst->opcode=A64_MC_LDST_GOT_LO12;
    }
    unit=!strcmp(op,"ldrsw")?4U:op[3]=='b'?1U:op[3]=='h'?2U:wd?8U:4U;
    if((unit==1U||unit==2U) && wd) {
        free(inst->operands[2].symbol);
        inst->operands[2].symbol=NULL;
        return false;
    }
    if(inst->opcode!=A64_MC_LDST_GOT_LO12)
        inst->opcode=A64_MC_LDST_LO12;
    inst->count=3U;
    put_reg(&inst->operands[0],rd,wd);
    put_reg(&inst->operands[1],rn,true);
    inst->encoding=!strcmp(op,"ldrsw")?UINT32_C(0xb9800000):
        strcmp(op,"ldr")==0 ?
        (wd?UINT32_C(0xf9400000):UINT32_C(0xb9400000)):
        strcmp(op,"str")==0 ?
        (wd?UINT32_C(0xf9000000):UINT32_C(0xb9000000)):
        strcmp(op,"ldrb")==0?UINT32_C(0x39400000):
        strcmp(op,"strb")==0?UINT32_C(0x39000000):
        strcmp(op,"ldrh")==0?UINT32_C(0x79400000):
                              UINT32_C(0x79000000);
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
static bool a64_parse_lse(MiniAs *as,MiniAsMCInst *inst,
                          const char *op,const char *args) {
    static const struct {const char *name;unsigned kind;bool store;} ops[]={
        {"cas",0U,false},{"ldadd",1U,false},{"ldclr",2U,false},
        {"ldset",3U,false},{"swp",4U,false},
        {"stadd",1U,true},{"stclr",2U,true},{"stset",3U,true},
        {"ldeor",5U,false},{"steor",5U,true}
    };
    char tok[4][96];
    unsigned i,n,rs,rt=31U,rn,mode,group=0U;
    unsigned subword=0U;
    bool wide,wr,sp;
    int64_t off;
    const char *suffix=NULL;
    char modifiers[4];
    for(i=0U;i<sizeof(ops)/sizeof(ops[0]);++i) {
        size_t plen=strlen(ops[i].name);
        if(strncmp(op,ops[i].name,plen)==0) {
            group=i;suffix=op+plen;break;
        }
    }
    if(suffix==NULL ||
       (as->target_options&A64_FEATURE_LSE)==0U)return false;
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
       !parse_a64_reg(tok[0],&rs,&wide,&sp) || sp ||
       (subword!=0U && wide) ||
       (!ops[group].store &&
        (!parse_a64_reg(tok[1],&rt,&wr,&sp) || sp || wr!=wide)) ||
       !parse_a64_mem(tok[n-1U],&rn,&off,&mode) ||
       off!=0 || mode!=0U)return false;
    inst->opcode=A64_MC_LSE;inst->count=3U;
    inst->encoding=ops[group].kind |
      (ops[group].store?0x10U:0U) |
      ((strchr(modifiers,'a')!=NULL)?0x20U:0U) |
      ((strchr(modifiers,'l')!=NULL)?0x40U:0U) |
      (subword==1U?0x80U:subword==2U?0x100U:0U);
    put_reg(&inst->operands[0],rs,wide);
    put_reg(&inst->operands[1],rt,wide);
    put_reg(&inst->operands[2],rn,true);
    return true;
}

static bool parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
    /* Bind aliases at this precise instruction site; subsequent .unreq
     * or redefining an alias cannot retroactively change prior MCInsts. */
    if(as->target_state) {
        char *expanded=a64_expand_register_aliases(as,stmt->args);
        if(!expanded) {
            minias_set_error(as,"out-of-memory:aarch64-req-expansion");
            return false;
        }
        free(stmt->args);stmt->args=expanded;
    }
    MiniAsMCInst *inst = calloc(1U, sizeof(*inst) +
                                4U * sizeof(MiniAsMCOperand));
    unsigned reg, imm;
    bool reg64;
    char first[80], second[80];
    const char *op = stmt->op;
    const char *args = stmt->args;

    if (inst == NULL) {
        minias_set_error(as, "out-of-memory:aarch64-mcinst");
        return false;
    }
    if(a64_parse_lse(as,inst,op,args)) {
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
        char tok[4][96];unsigned n,rd,rn,rm,ra,k;
        bool wd,wn,wm,wa,sp;
        for(k=0U;k<sizeof(tab)/sizeof(tab[0]);++k)
            if(!strcmp(op,tab[k].name))break;
        if(k==sizeof(tab)/sizeof(tab[0]) ||
           !split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(tok[1],&rn,&wn,&sp) || sp ||
           !parse_a64_reg(tok[2],&rm,&wm,&sp) || sp ||
           !parse_a64_reg(tok[3],&ra,&wa,&sp) || sp ||
           (tab[k].longop?(!wd || wn || wm || !wa):
                          (wd!=wn || wd!=wm || wd!=wa))) {
            minias_set_error(as,"bad-aarch64-multiply-accumulate:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
        char alu_tok[4][96];unsigned alu_n,alu_rd,alu_rn=31U,alu_rm,alu_k;
        bool alu_wide,alu_nwide=false,alu_mwide,alu_sp;
        for(alu_k=0U;alu_k<sizeof(ops)/sizeof(ops[0]);++alu_k)
            if(!strcmp(op,ops[alu_k].name))break;
        if(alu_k==sizeof(ops)/sizeof(ops[0]) ||
           !split_operands(args,alu_tok,&alu_n) ||
           (ops[alu_k].unary?
             (alu_n!=2U &&
              (alu_n!=3U || alu_k>1U)):alu_n!=3U) ||
           !parse_a64_reg(alu_tok[0],&alu_rd,&alu_wide,&alu_sp) ||
           alu_sp) {
            minias_set_error(as,"bad-aarch64-arith-alias:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        if(!ops[alu_k].unary &&
           (!parse_a64_reg(alu_tok[1],&alu_rn,&alu_nwide,&alu_sp) ||
            alu_sp)) {
            minias_set_error(as,"bad-aarch64-arith-source:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        if(!parse_a64_reg(alu_tok[ops[alu_k].unary?1U:2U],
                          &alu_rm,&alu_mwide,&alu_sp) || alu_sp ||
           (ops[alu_k].long_wide?
              (!alu_wide || alu_nwide || alu_mwide):
              ops[alu_k].high_wide?
              (!alu_wide || !alu_nwide || !alu_mwide):
              (alu_mwide!=alu_wide ||
               (!ops[alu_k].unary && alu_nwide!=alu_wide)))) {
            minias_set_error(as,"bad-aarch64-arith-width:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_ARITH_ALIAS;inst->count=3U;
        inst->encoding=ops[alu_k].base;
        if(ops[alu_k].unary && alu_n==3U) {
            uint32_t shift_fields;
            if(!a64_shift_fields(alu_tok[2],alu_wide,false,&shift_fields)) {
                minias_set_error(as,"bad-aarch64-neg-shift:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
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
        char bit_tok[4][96];
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
           !parse_a64_reg(bit_tok[0],&bit_rd,&bit_wide,&bit_sp) ||
           bit_sp ||
           !parse_a64_reg(bit_tok[1],&bit_rn,&bit_source_wide,&bit_sp) ||
           bit_sp || bit_wide!=bit_source_wide ||
           !parse_signed_number(bit_tok[2],&lsb) ||
           !parse_signed_number(bit_tok[3],&width)) {
            minias_set_error(as,"bad-aarch64-bitfield-operands:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        reg_bits=bit_wide?64U:32U;
        if(lsb<0 || lsb>=(int64_t)reg_bits ||
           (raw_form ?
             (width<0 || width>=(int64_t)reg_bits) :
             (width<1 || width>(int64_t)reg_bits-lsb))) {
            minias_set_error(as,"bad-aarch64-bitfield-range:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
        char tok[4][96];
        unsigned n,rd,rn,rm;
        bool wide,sourcewide,shiftwide,sp;
        int64_t amount;
        unsigned shiftop=!strcmp(op,"lsl")?0U:
                         !strcmp(op,"lsr")?1U:
                         !strcmp(op,"asr")?2U:3U;
        if(!split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(tok[0],&rd,&wide,&sp) || sp ||
           !parse_a64_reg(tok[1],&rn,&sourcewide,&sp) || sp ||
           wide!=sourcewide) {
            minias_set_error(as,"bad-aarch64-shift:%s:line=%zu",args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SHIFT;inst->count=3U;
        inst->encoding=shiftop;
        put_reg(&inst->operands[0],rd,wide);
        put_reg(&inst->operands[1],rn,wide);
        if(parse_a64_reg(tok[2],&rm,&shiftwide,&sp)) {
            if(sp || shiftwide!=wide) {
                minias_set_error(as,"bad-aarch64-shift-width:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            inst->encoding|=UINT32_C(0x100);
            put_reg(&inst->operands[2],rm,wide);
        }else {
            if(!parse_signed_number(tok[2],&amount) ||
               amount<0 || amount>=(wide?64:32)) {
                minias_set_error(as,"bad-aarch64-shift-immediate:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            put_immediate(&inst->operands[2],amount);
        }
    } else if(!strcmp(op,"extr")) {
        char extra[4][96];unsigned n,rd,rn,rm;
        bool wd,wn,wm,sp;
        int64_t lsb;
        if(!split_operands(args,extra,&n) || n!=4U ||
           !parse_a64_reg(extra[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(extra[1],&rn,&wn,&sp) || sp ||
           !parse_a64_reg(extra[2],&rm,&wm,&sp) || sp ||
           wd!=wn || wd!=wm || !parse_signed_number(extra[3],&lsb) ||
           lsb<0 || lsb>=(wd?64:32)) {
            minias_set_error(as,"bad-aarch64-extr:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_EXTR;inst->count=4U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wn);
        put_reg(&inst->operands[2],rm,wm);
        put_immediate(&inst->operands[3],lsb);
    } else if(!strcmp(op,"ccmp") || !strcmp(op,"ccmn")) {
        char tok[4][96];
        unsigned n,rn,rm,cc;
        bool wide,otherwide,sp;
        int64_t ccmp_immediate,nzcv;
        bool literal;
        if(!split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(tok[0],&rn,&wide,&sp) || sp ||
           !parse_signed_number(tok[2],&nzcv) || nzcv<0 || nzcv>15 ||
           !a64_condition_code(tok[3],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-compare:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        literal=parse_signed_number(tok[1],&ccmp_immediate);
        if(literal && (ccmp_immediate<0 || ccmp_immediate>31)) {
            minias_set_error(as,"bad-aarch64-ccmp-immediate:%s:line=%zu",
                             tok[1],stmt->line);
            free(inst);return false;
        }
        if(!literal && (!parse_a64_reg(tok[1],&rm,&otherwide,&sp) ||
                        sp || otherwide!=wide)) {
            minias_set_error(as,"bad-aarch64-ccmp-register:%s:line=%zu",
                             tok[1],stmt->line);
            free(inst);return false;
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
        char tok[4][96];
        unsigned n,rd,rn,rm,cc,i;
        bool wd,wn,wm,sp;
        static const struct {const char *name;uint32_t base;} select[]={
            {"csel",UINT32_C(0x1a800000)},
            {"csinc",UINT32_C(0x1a800400)},
            {"csinv",UINT32_C(0x5a800000)},
            {"csneg",UINT32_C(0x5a800400)}
        };
        if(!split_operands(args,tok,&n) || n!=4U ||
           !parse_a64_reg(tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(tok[1],&rn,&wn,&sp) || sp || wd!=wn ||
           !parse_a64_reg(tok[2],&rm,&wm,&sp) || sp || wd!=wm ||
           !a64_condition_code(tok[3],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-select:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        for(i=0U;i<sizeof(select)/sizeof(select[0]);++i)
            if(!strcmp(op,select[i].name))break;
        if(i==sizeof(select)/sizeof(select[0])) {
            free(inst);return false;
        }
        inst->opcode=A64_MC_COND_SELECT;inst->count=3U;
        inst->encoding=select[i].base|(cc<<12U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wd);
        put_reg(&inst->operands[2],rm,wd);
    } else if(!strcmp(op,"cneg") || !strcmp(op,"cinv")) {
        char tok[4][96];unsigned n,rd,rn,cc;
        bool wd,wn,sp;
        if(!split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(tok[0],&rd,&wd,&sp) || sp ||
           !parse_a64_reg(tok[1],&rn,&wn,&sp) || sp ||
           wd!=wn || !a64_condition_code(tok[2],&cc)) {
            minias_set_error(as,"bad-aarch64-conditional-alias:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
        char parts[4][96];
        unsigned n,rd,rn=31U,cond;
        bool wd,wn=false,sp;
        if(!split_operands(args,parts,&n) ||
           n!=(!strcmp(op,"cinc")?3U:2U) ||
           !parse_a64_reg(parts[0],&rd,&wd,&sp) || sp ||
           (!strcmp(op,"cinc") &&
            (!parse_a64_reg(parts[1],&rn,&wn,&sp) || sp || wn!=wd)) ||
           !a64_condition_code(parts[n-1U],&cond)) {
            minias_set_error(as,"bad-aarch64-csel-alias:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_CSEL_ALIAS;
        inst->count=2U;
        inst->encoding=(cond^1U) |
           (!strcmp(op,"csetm")?0x100U:0U);
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],rn,wd);
    } else if(!strcmp(op,"mvn") &&
              split_two(args,first,sizeof(first),second,sizeof(second))) {
        unsigned rd,rm;bool wide,otherwide,sp;
        if(!parse_a64_reg(first,&rd,&wide,&sp) || sp ||
           !parse_a64_reg(second,&rm,&otherwide,&sp) || sp ||
           wide!=otherwide) {
            minias_set_error(as,"bad-aarch64-mvn:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
               (as->target_options&A64_FEATURE_MTE)==0U)break;
            /* ARMv8.4 range TLBI requires an explicit architectural
             * profile opt-in. It is not available under baseline v8-a. */
            if((!strncmp(sysops[i].name,"rv",2U) ||
                !strncmp(sysops[i].name,"ripas2",6U)) &&
               (as->target_options&A64_FEATURE_TLBI_RANGE)==0U)break;
            if(paired && (!parse_a64_reg(operand,&rt,&rt64,&rtsp) ||
                          !rt64 || rtsp))break;
            word=sysops[i].word;found=true;break;
        }
        if(!found) {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SYS_OP;inst->count=1U;
        inst->encoding=word&~UINT32_C(31);
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"msr") &&
              split_two(args,first,sizeof(first),second,sizeof(second)) &&
              (!strcmp(first,"daifset") || !strcmp(first,"daifclr"))) {
        int64_t daif_immediate;
        if(!parse_signed_number(second,&daif_immediate) ||
           daif_immediate<0 || daif_immediate>15) {
            minias_set_error(as,"bad-aarch64-daif-immediate:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
           (!parse_a64_reg(first,&rt,&wide,&sp) || sp || !wide ||
            !a64_system_register(second,&mrscode,&msrcode) ||
            mrscode==0U) :
           (!parse_a64_reg(second,&rt,&wide,&sp) || sp || !wide ||
            !a64_system_register(first,&mrscode,&msrcode) ||
            msrcode==0U)) {
            minias_set_error(as,"bad-aarch64-sysreg:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=read?A64_MC_MRS:A64_MC_MSR;
        inst->count=1U;
        inst->encoding=read?mrscode:msrcode;
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"dup") ||
              (!strcmp(op,"rev32") && args[0]=='v')) {
        char dst[96],source[96];unsigned vd,rn;
        uint32_t ar;
        bool wide,sp;
        if(!split_two(args,dst,sizeof(dst),source,sizeof(source)) ||
           !a64_vector_view(dst,&vd,&ar)) {
            minias_set_error(as,"bad-aarch64-simd-lane:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        if(!strcmp(op,"dup")) {
            bool d=((ar&UINT32_C(0xc00))==UINT32_C(0xc00));
            unsigned lane=1U<<((ar&UINT32_C(0xc00))>>10U);
            if((d && !(ar&UINT32_C(0x40000000))) ||
               !parse_a64_reg(source,&rn,&wide,&sp) || sp || wide!=d) {
                minias_set_error(as,"bad-aarch64-dup-source:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
            inst->encoding=UINT32_C(0x0e000c00)|
                    (ar&UINT32_C(0x40000000))|(lane<<16U);
        }else {
            uint32_t element=ar&UINT32_C(0xc00);
            uint32_t vdarr;
            if((element!=0U && element!=UINT32_C(0x400)) ||
               !a64_vector_view(source,&rn,&vdarr) || vdarr!=ar) {
                minias_set_error(as,"bad-aarch64-rev32-vector:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            inst->opcode=A64_MC_SIMD_REV32;inst->count=2U;
            inst->encoding=UINT32_C(0x2e200800)|
                     (ar&UINT32_C(0x40000000))|
                     (element==UINT32_C(0x400)?
                              UINT32_C(0x00400000):0U);
        }
        put_reg(&inst->operands[0],vd,true);
        put_reg(&inst->operands[1],rn,true);
    } else if(!strcmp(op,"sys")) {
        uint32_t bits;unsigned rt;
        if(!a64_sys_fields(args,&bits,&rt)) {
            minias_set_error(as,"bad-aarch64-sys-fields:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SYS_OP;inst->count=1U;
        inst->encoding=bits;
        put_reg(&inst->operands[0],rt,true);
    } else if(!strcmp(op,"casp") || !strcmp(op,"caspa") ||
              !strcmp(op,"caspl") || !strcmp(op,"caspal")) {
        unsigned rs,rt,rn;
        bool wide;
        if((as->target_options&A64_FEATURE_LSE)==0U ||
           !a64_casp_fields(args,&rs,&rt,&rn,&wide)){
            minias_set_error(as,"bad-aarch64-casp:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
        put_reg(&inst->operands[2],rn,true);
    } else if(!strcmp(op,"stgm")) {
        unsigned rt,rn,mode;
        bool wide,sp;
        int64_t offset;
        if((as->target_options&A64_FEATURE_MTE)==0U ||
           !split_two(args,first,sizeof(first),second,sizeof(second)) ||
           !parse_a64_reg(first,&rt,&wide,&sp) || sp || !wide ||
           !parse_a64_mem(second,&rn,&offset,&mode) ||
           offset!=0 || mode!=0U) {
            minias_set_error(as,"bad-aarch64-stgm:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_MTE_STGM;inst->count=2U;
        inst->encoding=UINT32_C(0xd9a00000);
        put_reg(&inst->operands[0],rt,true);
        put_reg(&inst->operands[1],rn,true);
    } else if(!strcmp(op,"ins")) {
        unsigned vd,rn;
        uint32_t imm5;
        bool source_x,wide,sp;
        if(!split_two(args,first,sizeof(first),second,sizeof(second)) ||
           !a64_ins_lane(first,&vd,&imm5,&source_x) ||
           !parse_a64_reg(second,&rn,&wide,&sp) || sp ||
           wide!=source_x) {
            minias_set_error(as,"bad-aarch64-ins-lane:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_DUP;inst->count=2U;
        inst->encoding=UINT32_C(0x4e001c00)|(imm5<<16U);
        put_reg(&inst->operands[0],vd,true);
        put_reg(&inst->operands[1],rn,wide);
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
                free(inst);return false;
            }
        } else if(!a64_barrier_option(args,&barrier)) {
            minias_set_error(as,"bad-aarch64-barrier:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
        char tok[4][96];unsigned n,rd,rn,rm,k;
        bool wd,wn,wm,sp;
        if((as->target_options&A64_FEATURE_CRC)==0U ||
           !split_operands(args,tok,&n) || n!=3U ||
           !parse_a64_reg(tok[0],&rd,&wd,&sp) || sp || wd ||
           !parse_a64_reg(tok[1],&rn,&wn,&sp) || sp || wn ||
           !parse_a64_reg(tok[2],&rm,&wm,&sp) || sp) {
            minias_set_error(as,"bad-aarch64-crc32:%s:line=%zu",args,stmt->line);
            free(inst);return false;
        }
        for(k=0U;k<sizeof(crc)/sizeof(crc[0]);++k)
            if(!strcmp(op,crc[k].name))break;
        if(k==sizeof(crc)/sizeof(crc[0]) ||
           wm!=crc[k].xsrc) {
            minias_set_error(as,"bad-aarch64-crc-width:%s:line=%zu",args,stmt->line);
            free(inst);return false;
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
        char tok[4][96];unsigned n,rd,rn,k;
        uint32_t da,sa;
        if((as->target_options&A64_FEATURE_CRYPTO)==0U ||
           !split_operands(args,tok,&n) || n!=2U ||
           !a64_vector_view(tok[0],&rd,&da) ||
           !a64_vector_view(tok[1],&rn,&sa) ||
           da!=UINT32_C(0x40000000) || sa!=da) {
            minias_set_error(as,"bad-aarch64-aes:%s:line=%zu",args,stmt->line);
            free(inst);return false;
        }
        for(k=0U;k<sizeof(aes)/sizeof(aes[0]);++k)
            if(!strcmp(op,aes[k].name))break;
        inst->opcode=A64_MC_SIMD_CRYPTO;inst->count=2U;
        inst->encoding=aes[k].base;
        put_reg(&inst->operands[0],rd,true);
        put_reg(&inst->operands[1],rn,true);
    } else if(!strcmp(op,"eor") || !strcmp(op,"orr") ||
              !strcmp(op,"and") || !strcmp(op,"bic") ||
              !strcmp(op,"bsl") || !strcmp(op,"bit") ||
              !strcmp(op,"bif") ||
              (!strcmp(op,"add") && args[0]=='v') ||
              !strcmp(op,"trn1")) {
        char tok[4][96];unsigned n,rd,rn,rm,i;
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
            {"trn1",UINT32_C(0x0e002800)}
        };
        if(split_operands(args,tok,&n) && n==3U &&
           tok[0][0]=='v') {
            if(!a64_vector_view(tok[0],&rd,&da) ||
               !a64_vector_view(tok[1],&rn,&sa) ||
               !a64_vector_view(tok[2],&rm,&ta) ||
               da!=sa || da!=ta ||
               ((!strcmp(op,"trn1") || !strcmp(op,"add"))?
                 ((da&UINT32_C(0xc00))==UINT32_C(0xc00) &&
                  (da&UINT32_C(0x40000000))==0U):
                 (da&UINT32_C(0xc00))!=0U)) {
                minias_set_error(as,"bad-aarch64-simd-logic:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            for(i=0U;i<sizeof(table)/sizeof(table[0]);++i)
                if(!strcmp(op,table[i].name))break;
            inst->opcode=A64_MC_SIMD_LOGIC;inst->count=3U;
            inst->encoding=table[i].base|
                          (da&UINT32_C(0x40000000))|
                          ((!strcmp(op,"trn1") || !strcmp(op,"add"))?
                          ((da&UINT32_C(0xc00))<<12U):0U);
            put_reg(&inst->operands[0],rd,true);
            put_reg(&inst->operands[1],rn,true);
            put_reg(&inst->operands[2],rm,true);
        }else if(parse_integer_family(inst,op,args)) {
            /* Scalar ALU uses the same already proven Target machinery. */
        }else {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            free(inst);return false;
        }
    } else if(!strcmp(op,"ext")) {
        char tok[4][96];unsigned n,rd,rn,rm;
        uint32_t da,sa,ta;
        int64_t index;
        if(!split_operands(args,tok,&n) || n!=4U ||
           !a64_vector_view(tok[0],&rd,&da) ||
           !a64_vector_view(tok[1],&rn,&sa) ||
           !a64_vector_view(tok[2],&rm,&ta) ||
           da!=sa || da!=ta || (da&UINT32_C(0xc00))!=0U ||
           !parse_signed_number(tok[3],&index) || index<0 ||
           index>=((da&UINT32_C(0x40000000))?16:8)) {
            minias_set_error(as,"bad-aarch64-ext:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_EXT;inst->count=4U;
        inst->encoding=UINT32_C(0x2e000000)|
                      (da&UINT32_C(0x40000000))|
                      ((uint32_t)index<<11U);
        put_reg(&inst->operands[0],rd,true);
        put_reg(&inst->operands[1],rn,true);
        put_reg(&inst->operands[2],rm,true);
    } else if(!strcmp(op,"movi")) {
        char vreg[96],text[96];unsigned rd;
        uint32_t ar,base;
        int64_t number;
        unsigned simd_imm;
        if(!split_two(args,vreg,sizeof(vreg),text,sizeof(text)) ||
           !a64_vector_view(vreg,&rd,&ar) ||
           !parse_signed_number(text,&number) ||
           number<0 || number>255) {
            minias_set_error(as,"bad-aarch64-movi:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        /* Legal MOVI splat #imm8 forms share exactly the same 8-bit
         * bit split: imm8[7:5] -> [18:16], imm8[4:0]->[9:5].
         * Constant-only; no ARM symbolic relocation is inferred. */
        if((ar&UINT32_C(0xc00))==0U)
            base=UINT32_C(0x0f00e400);
        else if((ar&UINT32_C(0xc00))==UINT32_C(0x800))
            base=UINT32_C(0x0f000400);
        else if(ar==UINT32_C(0x40000c00) && number==0)
            base=UINT32_C(0x6f00e400);
        else {
            minias_set_error(as,"bad-aarch64-movi-size:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        simd_imm=(unsigned)number;
        inst->opcode=A64_MC_SIMD_IMM;inst->count=1U;
        inst->encoding=base|
                      (ar&UINT32_C(0x40000000))|
                      ((simd_imm>>5U)<<16U)|((simd_imm&31U)<<5U);
        put_reg(&inst->operands[0],rd,true);
    } else if(!strcmp(op,"ldxp") || !strcmp(op,"ldaxp") ||
              !strcmp(op,"stxp") || !strcmp(op,"stlxp")) {
        char tok[4][96];unsigned n,k,rt,rt2,rn,rs=31U;
        bool wt,wt2,ws,sp,store=op[0]=='s';
        int64_t off;unsigned mode;
        if(!split_operands(args,tok,&n) ||
           n!=(store?4U:3U)) {
            minias_set_error(as,"bad-aarch64-exclusive-pair:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        k=store?1U:0U;
        if((store && (!parse_a64_reg(tok[0],&rs,&ws,&sp) || sp || ws)) ||
           !parse_a64_reg(tok[k],&rt,&wt,&sp) || sp ||
           !parse_a64_reg(tok[k+1U],&rt2,&wt2,&sp) || sp ||
           wt!=wt2 ||
           !parse_a64_mem(tok[k+2U],&rn,&off,&mode) ||
           off!=0 || mode!=0U) {
            minias_set_error(as,"bad-aarch64-exclusive-pair:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_EXCLUSIVE_PAIR;inst->count=4U;
        inst->encoding=(wt?UINT32_C(0xc8200000):
                            UINT32_C(0x88200000))|
                       (store?0U:UINT32_C(0x005f0000))|
                       ((!strcmp(op,"ldaxp") || !strcmp(op,"stlxp"))?
                         UINT32_C(0x8000):0U);
        put_reg(&inst->operands[0],rt,wt);
        put_reg(&inst->operands[1],rt2,wt);
        put_reg(&inst->operands[2],rn,true);
        put_reg(&inst->operands[3],rs,false);
    } else if((!strcmp(op,"br")||!strcmp(op,"blr")) &&
       parse_register(args,&reg,&reg64) && reg64) {
        inst->opcode=!strcmp(op,"br")?A64_MC_BR:A64_MC_BLR;
        inst->count=1U;
        put_reg(&inst->operands[0],reg,true);
    } else if((!strcmp(op,"movz") || !strcmp(op,"movk") ||
               !strcmp(op,"movn"))) {
        char parts[4][96];unsigned n,rd,imm16=0U,shift=0U;
        bool wide,sp;
        MiniAsSymbolExpr movw_expr;
        MiniAsFixupKind movw_fixup=MINIAS_FIXUP_NONE;
        bool modifier,constant=false,signed_negative=false;
        uint64_t bits=0U;
        unsigned variant;
        if(!split_operands(args,parts,&n) || (n!=2U && n!=3U) ||
           !parse_a64_reg(parts[0],&rd,&wide,&sp) || sp) {
            minias_set_error(as,"bad-aarch64-wide-immediate:line=%zu",stmt->line);
            free(inst);return false;
        }
        modifier=a64_movw_relocation(parts[1],&movw_expr,&movw_fixup,
                                      &shift,&constant,&bits,&signed_negative);
        if((modifier && (n!=2U || (!wide && shift>=2U))) ||
           (!modifier && !parse_imm16(parts[1],&imm16))) {
            minias_set_error(as,"bad-aarch64-wide-immediate:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        if(modifier && constant)
            imm16=(unsigned)((signed_negative?~bits:bits)>>(16U*shift))&
                  UINT32_C(0xffff);
        if(n==3U) {
            int64_t raw_shift;
            if(strncmp(parts[2],"lsl ",4U)!=0 ||
               !parse_signed_number(parts[2]+4U,&raw_shift) ||
               raw_shift<0 || raw_shift>=(wide?64:32) ||
               (raw_shift%16)!=0) {
                minias_set_error(as,"bad-aarch64-mov-shift:line=%zu",stmt->line);
                free(inst);return false;
            }
            shift=(unsigned)raw_shift/16U;
        }
        variant=!strcmp(op,"movz")?0U:!strcmp(op,"movk")?1U:2U;
        if(modifier && constant && signed_negative && variant==0U)
            variant=2U;
        inst->opcode=A64_MC_WIDE_IMM;inst->count=2U;
        inst->encoding=variant|(shift<<2U)|
                      (modifier && !constant?
                        ((uint32_t)movw_fixup<<4U):0U);
        put_reg(&inst->operands[0],rd,wide);
        if(modifier && !constant) {
            if(!a64_attach_expr(inst,1U,&movw_expr)) {
                minias_set_error(as,"out-of-memory:a64-movw-reloc");
                free(inst);return false;
            }
        }else put_immediate(&inst->operands[1],(int64_t)imm16);
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
                 a64_attach_expr(inst,0U,&expr)) {
            /* GNU Linux BTI macros use hint #.L__bti_targets_c.
             * Keep the exact symbol in MCInst until layout/encode instead
             * of treating an ABS .equ alias as an unsupported opcode. */
        }else {
            minias_set_error(as,"bad-aarch64-hint:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
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
               parse_register(args, &reg, &reg64) && reg64) {
        inst->opcode = A64_MC_RET;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_REGISTER;
        inst->operands[0].reg = reg;
        inst->operands[0].register_64 = true;
    } else if(!strcmp(op,"mov") &&
               split_two(args,first,sizeof(first),second,sizeof(second)) &&
               first[0]=='v' && second[0]=='v') {
        unsigned vd,vn;
        uint32_t da,sa;
        if(!a64_vector_view(first,&vd,&da) ||
           !a64_vector_view(second,&vn,&sa) || da!=sa ||
           (da&UINT32_C(0xc00))!=0U) {
            minias_set_error(as,"bad-aarch64-mov-vector:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_SIMD_MOVE;inst->count=2U;
        inst->encoding=(da&UINT32_C(0x40000000))?
                 UINT32_C(0x4ea01c00):UINT32_C(0x0ea01c00);
        put_reg(&inst->operands[0],vd,true);
        put_reg(&inst->operands[1],vn,true);
    } else if (strcmp(op, "mov") == 0 &&
               split_two(args, first, sizeof(first), second, sizeof(second)) &&
               parse_register(first, &reg, &reg64) &&
               parse_imm16(second, &imm)) {
        inst->opcode = A64_MC_MOVZ;
        inst->count = 2U;
        inst->operands[0].kind = MINIAS_MC_REGISTER;
        inst->operands[0].reg = reg;
        inst->operands[0].register_64 = reg64;
        inst->operands[1].kind = MINIAS_MC_IMMEDIATE;
        inst->operands[1].immediate = imm;
    } else if (!strcmp(op,"mov") &&
               split_two(args,first,sizeof(first),second,sizeof(second)) &&
               parse_register(first,&reg,&reg64) &&
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
                free(inst);return false;
            }
            value=((uint64_t)signed_value)&mask;
        }else if(!minias_eval_u64_expression(literal,&value) ||
                 (!reg64 && (value>>32U)!=0U)) {
            minias_set_error(as,"bad-aarch64-mov-constant:%s:line=%zu",
                             second,stmt->line);
            free(inst);return false;
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
            free(inst);return false;
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
        MiniAsSymbolExpr expr;
        if (!minias_parse_symbol_addend(args, &expr)) {
            minias_set_error(as, "bad-aarch64-branch-target:%s:line=%zu",
                             args, stmt->line);
            free(inst);
            return false;
        }
        if ((expr.addend & 3) != 0) {
            minias_set_error(as, "bad-aarch64-branch-addend:%s:line=%zu",
                             args, stmt->line);
            free(inst);
            return false;
        }
        inst->operands[0].symbol = malloc(sizeof(expr));
        if (inst->operands[0].symbol == NULL) {
            minias_set_error(as, "out-of-memory:aarch64-symbol-mcinst");
            free(inst);
            return false;
        }
        *inst->operands[0].symbol = expr;
        inst->opcode = strcmp(op, "bl") == 0 ? A64_MC_BL : A64_MC_B;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_SYMBOL;
    } else if (strcmp(op, "cbz") == 0 || strcmp(op, "cbnz") == 0) {
        MiniAsSymbolExpr expr;
        bool is_sp;
        if (!split_two(args, first, sizeof(first), second, sizeof(second)) ||
            !parse_a64_reg(first, &reg, &reg64, &is_sp) || is_sp ||
            !minias_parse_symbol_addend(second, &expr) ||
            (expr.addend & 3) != 0) {
            minias_set_error(as,"bad-aarch64-condbranch:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->operands[1].symbol=malloc(sizeof(expr));
        if(!inst->operands[1].symbol) {
            minias_set_error(as,"out-of-memory:aarch64-condbranch-symbol");
            free(inst);return false;
        }
        *inst->operands[1].symbol=expr;
        inst->opcode=strcmp(op,"cbz")==0?A64_MC_CBZ:A64_MC_CBNZ;
        inst->count=2U;
        put_reg(&inst->operands[0],reg,reg64);
        inst->operands[1].kind=MINIAS_MC_SYMBOL;
    } else if (!strcmp(op,"tbz") || !strcmp(op,"tbnz")) {
        char t[4][96];unsigned n,rd;
        bool wide,sp;
        int64_t bit;
        MiniAsSymbolExpr expr;
        if(!split_operands(args,t,&n) || n!=3U ||
           !parse_a64_reg(t[0],&rd,&wide,&sp) || sp ||
           !parse_signed_number(t[1],&bit) || bit<0 ||
           bit>=(wide?64:32) ||
           !minias_parse_symbol_addend(t[2],&expr) ||
           (expr.addend&3)!=0 || !a64_attach_expr(inst,2U,&expr)) {
            minias_set_error(as,"bad-aarch64-testbranch:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=!strcmp(op,"tbz")?A64_MC_TBZ:A64_MC_TBNZ;
        inst->count=3U;
        put_reg(&inst->operands[0],rd,wide);
        put_immediate(&inst->operands[1],bit);
    } else {
        unsigned cond;
        if(a64_branch_condition(op,&cond)) {
            MiniAsSymbolExpr expr;
            if(!minias_parse_symbol_addend(args,&expr) ||
               (expr.addend&3)!=0 || !a64_attach_expr(inst,0U,&expr)) {
                minias_set_error(as,"bad-aarch64-conditional:%s:line=%zu",
                                 args,stmt->line);
                free(inst);return false;
            }
            inst->opcode=A64_MC_BCOND;inst->count=1U;
            inst->encoding=cond;
        } else if (a64_parse_relocated(inst,op,args)) {
            /* ADRP/LO12 relocation is typed and target-owned. */
        } else if (parse_integer_family(inst,op,args)) {
            /* Reuse existing typed register and memory IR. */
        } else {
            minias_set_error(as,"bad-aarch64-operands:%s:%s:line=%zu",
                             op,args,stmt->line);
            free(inst);return false;
        }
    }
    /*
     * The AArch64 integer/memory family is parsed into typed operands.
     * No instruction is silently translated through a RISC-V pipeline.
     */
    /* End parser return follows. */
    if(0) {
        /* The AArch64 integer/memory family is parsed into typed operands. */
    }
    stmt->mcinst = inst;
    return true;
}

static bool encode(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *inst = stmt->mcinst;
    uint32_t code;
    MiniAsFixupKind fixup = MINIAS_FIXUP_NONE;
    const MiniAsSymbolExpr *expr = NULL;
    if (inst == NULL) {
        minias_set_error(as, "missing-aarch64-mcinst:line=%zu", stmt->line);
        return false;
    }
    switch (inst->opcode) {
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
            if(inst->operands[0].kind==MINIAS_MC_SYMBOL) {
                const MiniAsSymbolExpr *hint_expr=inst->operands[0].symbol;
                MiniAsSymbol *symbol=hint_expr?
                    minias_get_symbol(as,hint_expr->name,false):NULL;
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
            expr=inst->operands[1].symbol;
            code=UINT32_C(0x10000000)|inst->operands[0].reg;
            fixup=MINIAS_FIXUP_A64_ADR_LO21;
            target=minias_get_symbol(as,expr->name,false);
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
            displacement=(int64_t)target->value+expr->addend-
                         (int64_t)stmt->offset;
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
            expr=inst->operands[1].symbol;
            fixup=inst->encoding==1U?MINIAS_FIXUP_A64_ADR_GOT_PAGE:
                                      MINIAS_FIXUP_A64_ADRP_HI21;
            break;
        case A64_MC_ADD_LO12:
            code=(inst->operands[0].register_64?
                  UINT32_C(0x91000000):UINT32_C(0x11000000))|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            expr=inst->operands[2].symbol;
            fixup=MINIAS_FIXUP_A64_ADD_LO12;
            break;
        case A64_MC_LDST_LO12:
        case A64_MC_LDST_GOT_LO12:
            code=inst->encoding|(inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            expr=inst->operands[2].symbol;
            fixup=inst->opcode==A64_MC_LDST_GOT_LO12?
                      MINIAS_FIXUP_A64_LD64_GOT_LO12:
                  inst->encoding==UINT32_C(0x39400000) ||
                  inst->encoding==UINT32_C(0x39000000) ?
                      MINIAS_FIXUP_A64_LDST8_LO12 :
                  inst->encoding==UINT32_C(0x79400000) ||
                  inst->encoding==UINT32_C(0x79000000) ?
                      MINIAS_FIXUP_A64_LDST16_LO12 :
                  inst->encoding==UINT32_C(0xf9400000) ||
                  inst->encoding==UINT32_C(0xf9000000) ?
                      MINIAS_FIXUP_A64_LDST64_LO12 :
                      MINIAS_FIXUP_A64_LDST32_LO12;
            break;
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
            uint32_t base=variant==0U?UINT32_C(0x52800000):
                          variant==1U?UINT32_C(0x72800000):
                                      UINT32_C(0x12800000);
            code=base|(wide?UINT32_C(0x80000000):0U)|
                (shift<<21U)|
                (inst->operands[1].kind==MINIAS_MC_SYMBOL?0U:
                 ((uint32_t)inst->operands[1].immediate<<5U))|
                inst->operands[0].reg;
            if(inst->operands[1].kind==MINIAS_MC_SYMBOL) {
                expr=inst->operands[1].symbol;
                fixup=(MiniAsFixupKind)(inst->encoding>>4U);
            }
            break;
        }
        case A64_MC_MOVZ:
            code = (inst->operands[0].register_64 ?
                    UINT32_C(0xd2800000) : UINT32_C(0x52800000)) |
                ((uint32_t)inst->operands[1].immediate << 5U) |
                inst->operands[0].reg;
            break;
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
            if(kind<2U) {
                bool wide=inst->operands[0].register_64;
                code=(byte?UINT32_C(0x085f7c00):
                      half?UINT32_C(0x485f7c00):
                      wide?UINT32_C(0xc85f7c00):UINT32_C(0x885f7c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (inst->operands[1].reg<<5U) | inst->operands[0].reg;
            }else {
                bool wide=inst->operands[1].register_64;
                code=(byte?UINT32_C(0x08007c00):
                      half?UINT32_C(0x48007c00):
                      wide?UINT32_C(0xc8007c00):UINT32_C(0x88007c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (inst->operands[0].reg<<16U) |
                     (inst->operands[2].reg<<5U) | inst->operands[1].reg;
            }
            break;
        }
        case A64_MC_LSE_PAIR:
            code=inst->encoding|
                 (inst->operands[0].reg<<16U)|
                 (inst->operands[2].reg<<5U)|
                 inst->operands[1].reg;
            break;
        case A64_MC_MTE_STGM:
            code=inst->encoding|
                 (inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            break;
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
        case A64_MC_EXCLUSIVE_PAIR:
            code=inst->encoding|
                 (inst->operands[3].reg<<16U)|
                 (inst->operands[1].reg<<10U)|
                 (inst->operands[2].reg<<5U)|
                 inst->operands[0].reg;
            break;
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
        case A64_MC_SIMD_SINGLE:
            code=inst->encoding|
                 (inst->count==3U?
                    ((inst->operands[2].kind==MINIAS_MC_REGISTER?
                       inst->operands[2].reg:
                       (unsigned)inst->operands[2].immediate)<<16U):0U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_PRFM:
            code=UINT32_C(0xf9800000) |
                 ((uint32_t)inst->operands[2].immediate<<10U) |
                 (inst->operands[1].reg<<5U) |
                 (uint32_t)inst->operands[0].immediate;
            break;
        case A64_MC_LSE: {
            unsigned kind=inst->encoding&15U;
            bool acquire=(inst->encoding&0x20U)!=0U;
            bool release=(inst->encoding&0x40U)!=0U;
            bool wide=inst->operands[0].register_64;
            bool byte=(inst->encoding&0x80U)!=0U;
            bool half=(inst->encoding&0x100U)!=0U;
            uint32_t base;
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
                 (inst->operands[2].reg<<5U)|inst->operands[1].reg;
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
            bool wide=inst->operands[0].register_64;
            uint32_t opcode;
            switch(inst->encoding) {
                case 0U:opcode=wide?UINT32_C(0xc89ffc00):UINT32_C(0x889ffc00);break;
                case 1U:opcode=wide?UINT32_C(0xc8dffc00):UINT32_C(0x88dffc00);break;
                case 2U:opcode=UINT32_C(0x08dffc00);break;
                case 3U:opcode=UINT32_C(0x48dffc00);break;
                case 4U:opcode=UINT32_C(0x089ffc00);break;
                case 5U:opcode=UINT32_C(0x489ffc00);break;
                default:minias_set_error(as,"bad-aarch64-acquire-size");return false;
            }
            code=opcode | (inst->operands[1].reg<<5U) |
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
            unsigned imm = (unsigned)inst->operands[2].immediate;
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
            uint64_t imm=inst->operands[1].immediate;
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
        case A64_MC_LDST_UNPRIV:
            code=inst->encoding|
                 (((uint32_t)inst->operands[2].immediate&511U)<<12U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_LDST_SCALED:
            code=inst->encoding|
                 ((uint32_t)inst->operands[2].immediate<<10U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_PRFM_REG:
        case A64_MC_LDST_INDEX:
            code=inst->encoding|(inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<5U)|
                 (inst->opcode==A64_MC_PRFM_REG?
                  (uint32_t)inst->operands[0].immediate:inst->operands[0].reg);
            break;
        case A64_MC_LDST_UNSCALED:
            code=inst->encoding |
                (((uint32_t)inst->operands[2].immediate&511U)<<12U) |
                (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            break;
        case A64_MC_LDR:
        case A64_MC_STR:
        case A64_MC_LDRB:
        case A64_MC_STRB:
        case A64_MC_LDRH:
        case A64_MC_STRH: {
            bool sf = inst->operands[0].register_64;
            uint32_t base =
                inst->opcode == A64_MC_LDR ? (sf ? UINT32_C(0xf9400000) : UINT32_C(0xb9400000)) :
                inst->opcode == A64_MC_STR ? (sf ? UINT32_C(0xf9000000) : UINT32_C(0xb9000000)) :
                inst->opcode == A64_MC_LDRB ? UINT32_C(0x39400000) :
                inst->opcode == A64_MC_STRB ? UINT32_C(0x39000000) :
                inst->opcode == A64_MC_LDRH ? UINT32_C(0x79400000) :
                                              UINT32_C(0x79000000);
            code = base | ((uint32_t)inst->operands[2].immediate << 10U) |
                   (inst->operands[1].reg << 5U) | inst->operands[0].reg;
            break;
        }
        case A64_MC_STP:
        case A64_MC_LDP:
        case A64_MC_LDPSW: {
            bool sf = inst->operands[0].register_64;
            bool qpair=(inst->encoding&UINT32_C(0x100))!=0U;
            unsigned mode=inst->encoding&3U;
            uint32_t base = inst->opcode==A64_MC_LDPSW?
                (inst->encoding==1U?UINT32_C(0x69c00000):
                 inst->encoding==2U?UINT32_C(0x68c00000):
                                    UINT32_C(0x69400000)):
                inst->opcode == A64_MC_STP ?
                (inst->encoding == 1U ? UINT32_C(0x29800000) :
                 inst->encoding == 2U ? UINT32_C(0x28800000) :
                 inst->encoding == 3U ? UINT32_C(0x28000000) : UINT32_C(0x29000000)) :
                (inst->encoding == 1U ? UINT32_C(0x29c00000) :
                 inst->encoding == 2U ? UINT32_C(0x28c00000) :
                 inst->encoding == 3U ? UINT32_C(0x28400000) : UINT32_C(0x29400000));
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
                   (((uint32_t)inst->operands[3].immediate & 127U) << 15U) |
                   (inst->operands[1].reg << 10U) |
                   (inst->operands[2].reg << 5U) | inst->operands[0].reg;
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
            expr=inst->operands[slot].symbol;
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
            target = minias_get_symbol(as, expr->name, false);
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
    return fixup == MINIAS_FIXUP_NONE ||
           minias_add_fixup(as, stmt->section, stmt->offset, fixup, 4U,
                            expr->name, expr->addend);
}

static bool directive(MiniAs *as, const char *op, const char *args,
                      size_t line, bool *handled) {
    *handled = false;
    if(strcmp(op,".unreq")==0) {
        A64AliasState *st=(A64AliasState *)as->target_state;
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
        if((!parse_a64_reg(canonical,&n,&wide,&sp) &&
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
        st=(A64AliasState *)as->target_state;
        if(!st) {
            st=calloc(1U,sizeof(*st));
            if(!st){minias_set_error(as,"out-of-memory:aarch64-aliases");return false;}
            as->target_state=st;
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
        *handled = true;
        /* These are recognized GNU assembler ISA profiles, not a claim
         * that every v8.5/crypto instruction is implemented. Unsupported
         * machine encodings still fail in the typed Target MC parser. */
        bool v85=!strcmp(args,"armv8.5-a") ||
                 !strcmp(args,"armv8.5-a+memtag");
        if(strcmp(args,"armv8-a")!=0 && !v85 &&
           strcmp(args,"armv8-a+crypto")!=0 &&
           strcmp(args,"armv8-a+crc")!=0) {
            minias_set_error(as, "unsupported-aarch64-architecture:%s:line=%zu",
                             args, line);
            return false;
        }
        as->target_options&=~(A64_FEATURE_LSE|
                              A64_FEATURE_TLBI_RANGE|
                              A64_FEATURE_CRC|A64_FEATURE_CRYPTO|
                              A64_FEATURE_MTE);
        if(v85)as->target_options|=(A64_FEATURE_LSE|
                                    A64_FEATURE_TLBI_RANGE|
                                    A64_FEATURE_CRC);
        if(!strcmp(args,"armv8-a+crc"))
            as->target_options|=A64_FEATURE_CRC;
        if(!strcmp(args,"armv8.5-a+memtag"))
            as->target_options|=A64_FEATURE_MTE;
        if(!strcmp(args,"armv8-a+crypto"))
            as->target_options|=A64_FEATURE_CRYPTO;
        return true;
    }
    if(!strcmp(op,".arch_extension")) {
        *handled=true;
        if(!strcmp(args,"lse"))as->target_options |= A64_FEATURE_LSE;
        else if(!strcmp(args,"nolse"))as->target_options &= ~A64_FEATURE_LSE;
        else if(!strcmp(args,"crc"))as->target_options |= A64_FEATURE_CRC;
        else if(!strcmp(args,"nocrc"))as->target_options &= ~A64_FEATURE_CRC;
        else if(!strcmp(args,"crypto"))as->target_options |= A64_FEATURE_CRYPTO;
        else if(!strcmp(args,"nocrypto"))as->target_options &= ~A64_FEATURE_CRYPTO;
        else if(!strcmp(args,"memtag"))as->target_options |= A64_FEATURE_MTE;
        else if(!strcmp(args,"nomemtag"))as->target_options &= ~A64_FEATURE_MTE;
        else {
            minias_set_error(as,"unsupported-aarch64-extension:%s:line=%zu",
                             args,line);
            return false;
        }
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
        if (!minias_section_append(as, section, nop, sizeof(nop)))
            return false;
        amount -= sizeof(nop);
    }
    return true;
}

static uint32_t aarch64_fixup_relocation_type(MiniAsFixupKind kind,
                                               unsigned width) {
    switch (kind) {
        case MINIAS_FIXUP_ABS:
            return width == 4U ? AARCH64_R_ABS32 :
                   width == 8U ? AARCH64_R_ABS64 : 0U;
        case MINIAS_FIXUP_PREL32:
            return width == 4U ? AARCH64_R_PREL32 : 0U;
        case MINIAS_FIXUP_PREL64:
            return width == 8U ? AARCH64_R_PREL64 : 0U;
        case MINIAS_FIXUP_A64_CALL26:
            return width == 4U ? AARCH64_R_CALL26 : 0U;
        case MINIAS_FIXUP_A64_JUMP26:
            return width == 4U ? AARCH64_R_JUMP26 : 0U;
        case MINIAS_FIXUP_A64_CONDBR19:
            return width == 4U ? AARCH64_R_CONDBR19 : 0U;
        case MINIAS_FIXUP_A64_TSTBR14:
            return width == 4U ? AARCH64_R_TSTBR14 : 0U;
        case MINIAS_FIXUP_A64_ADR_LO21:
            return width == 4U ? AARCH64_R_ADR_PREL_LO21 : 0U;
        case MINIAS_FIXUP_A64_ADRP_HI21:
            return width == 4U ? AARCH64_R_ADR_PREL_PG_HI21 : 0U;
        case MINIAS_FIXUP_A64_ADR_GOT_PAGE:
            return width == 4U ? AARCH64_R_ADR_GOT_PAGE : 0U;
        case MINIAS_FIXUP_A64_LD64_GOT_LO12:
            return width == 4U ? AARCH64_R_LD64_GOT_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_ADD_LO12:
            return width == 4U ? AARCH64_R_ADD_ABS_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_LDST8_LO12:
            return width == 4U ? AARCH64_R_LDST8_ABS_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_LDST16_LO12:
            return width == 4U ? AARCH64_R_LDST16_ABS_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_LDST32_LO12:
            return width == 4U ? AARCH64_R_LDST32_ABS_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_LDST64_LO12:
            return width == 4U ? AARCH64_R_LDST64_ABS_LO12_NC : 0U;
        case MINIAS_FIXUP_A64_MOVW_G0:
            return width == 4U ? AARCH64_R_MOVW_UABS_G0 : 0U;
        case MINIAS_FIXUP_A64_MOVW_G0_NC:
            return width == 4U ? AARCH64_R_MOVW_UABS_G0_NC : 0U;
        case MINIAS_FIXUP_A64_MOVW_G1:
            return width == 4U ? AARCH64_R_MOVW_UABS_G1 : 0U;
        case MINIAS_FIXUP_A64_MOVW_G1_NC:
            return width == 4U ? AARCH64_R_MOVW_UABS_G1_NC : 0U;
        case MINIAS_FIXUP_A64_MOVW_G2:
            return width == 4U ? AARCH64_R_MOVW_UABS_G2 : 0U;
        case MINIAS_FIXUP_A64_MOVW_G2_NC:
            return width == 4U ? AARCH64_R_MOVW_UABS_G2_NC : 0U;
        case MINIAS_FIXUP_A64_MOVW_G3:
            return width == 4U ? AARCH64_R_MOVW_UABS_G3 : 0U;
        case MINIAS_FIXUP_A64_MOVW_SG0:
            return width == 4U ? AARCH64_R_MOVW_SABS_G0 : 0U;
        case MINIAS_FIXUP_A64_MOVW_SG1:
            return width == 4U ? AARCH64_R_MOVW_SABS_G1 : 0U;
        case MINIAS_FIXUP_A64_MOVW_SG2:
            return width == 4U ? AARCH64_R_MOVW_SABS_G2 : 0U;
        default: return 0U;
    }
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

static const MiniAsTargetOps ops = {
    .comment_prefix = '/', /* //, not #; AArch64 #42 is an immediate. */
    .elf_machine = 183U, /* EM_AARCH64 */
    .parse_target_directive = directive,
    .destroy_target_state = a64_destroy_aliases,
    .object_attributes = attributes,
    .name = "aarch64",
    .resolve_profile = aarch64_resolve_profile,
    .parse_mcinst = parse_mcinst,
    .measure_mcinst = aarch64_measure_mcinst,
    .encode = encode,
    .append_code_alignment = code_alignment,
    .fixup_relocation_type = aarch64_fixup_relocation_type,
    .validate_output = validate,
    .dot_relative_fixup = MINIAS_FIXUP_PREL32,
    .inst_directive_width = 4U,
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
