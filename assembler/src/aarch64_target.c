#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define A64_FEATURE_LSE UINT32_C(0x10000000) /* target_options feature bit */

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
    AARCH64_R_ADD_ABS_LO12_NC = 277U,
    AARCH64_R_LDST8_ABS_LO12_NC = 278U,
    AARCH64_R_LDST16_ABS_LO12_NC = 284U,
    AARCH64_R_LDST32_ABS_LO12_NC = 285U,
    AARCH64_R_LDST64_ABS_LO12_NC = 286U,
    AARCH64_R_TSTBR14 = 279U,
    AARCH64_R_CONDBR19 = 280U,
    AARCH64_R_JUMP26 = 282U,
    AARCH64_R_CALL26 = 283U
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
    char *end;
    unsigned long value;
    /* GCC's AArch64 -S commonly omits '#' on MOVZ integer immediates. */
    if (*text == '#') ++text;
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 65535U)
        return false;
    *result = (unsigned)value;
    return true;
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
    A64_MC_EXCLUSIVE
};

/* GNU AArch64 operands split on TOP-LEVEL commas, not commas within
 * [xN,#offset]. Unlike copying a generic tokenizer into every target,
 * this helper is strictly A64 addressing grammar. */
static bool split_operands(const char *args, char out[4][96], unsigned *n) {
    unsigned depth = 0U, count = 0U;
    const char *start = args, *p = args;
    for (;;) {
        char ch = *p;
        if (ch == '[') ++depth;
        if (ch == ']') {
            if (!depth) return false;
            --depth;
        }
        if ((ch == ',' && depth == 0U) || ch == '\0') {
            size_t len = (size_t)(p - start);
            while (len && isspace((unsigned char)*start)) { ++start; --len; }
            while (len && isspace((unsigned char)start[len - 1U])) --len;
            if (!len || len >= sizeof(out[0]) || count == 4U || depth)
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
        if(strncmp(text,ops[i],n) || !isspace((unsigned char)text[n]))
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
    /* Common GNU Linux bit/byte-swap and acquire/release families.
     * All operands are typed once; no instruction-name checks in encode. */
    if ((!strcmp(op,"ldxr") || !strcmp(op,"ldaxr")) && n==2U) {
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || spd ||
           !parse_a64_mem(t[1],&base,&imm,&mode) ||
           imm!=0 || mode!=0U)return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=2U;
        inst->encoding=!strcmp(op,"ldaxr")?1U:0U;
        put_reg(&inst->operands[0],rd,wd);
        put_reg(&inst->operands[1],base,true);
        return true;
    }
    if ((!strcmp(op,"stxr") || !strcmp(op,"stlxr")) && n==3U) {
        if(!parse_a64_reg(t[0],&rd,&wd,&spd) || wd || spd ||
           !parse_a64_reg(t[1],&rn,&wn,&spn) || spn ||
           !parse_a64_mem(t[2],&base,&imm,&mode) ||
           imm!=0 || mode!=0U)return false;
        inst->opcode=A64_MC_EXCLUSIVE;inst->count=3U;
        inst->encoding=!strcmp(op,"stlxr")?3U:2U;
        put_reg(&inst->operands[0],rd,false);
        put_reg(&inst->operands[1],rn,wn);
        put_reg(&inst->operands[2],base,true);
        return true;
    }
    if(!strcmp(op,"prfm") && n==2U) {
        unsigned hint=0U,unit=0U;
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
    if ((strcmp(op, "stp") == 0 || strcmp(op, "ldp") == 0 ||
         strcmp(op,"ldpsw")==0) && (n == 3U || n == 4U)) {
        bool signed_pair=!strcmp(op,"ldpsw");
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) || spd ||
            !parse_a64_reg(t[1], &rn, &wn, &spn) || spn || wd != wn ||
            !parse_a64_mem(t[2], &base, &imm, &mode))
            return false;
        if(signed_pair && !wd)return false;
        if (n == 4U) {
            if (mode != 0U || imm != 0 ||
                !parse_signed_number(t[3], &imm)) return false;
            mode = 2U;
        }
        if (imm % (signed_pair?4:(wd?8:4)) != 0 ||
            imm < -(int64_t)(signed_pair?256:(wd?512:256)) ||
            imm > (int64_t)(signed_pair?252:(wd?504:252)))
            return false;
        inst->opcode = signed_pair?A64_MC_LDPSW:
                       strcmp(op, "stp") == 0 ? A64_MC_STP : A64_MC_LDP;
        inst->encoding = mode;
        inst->count = 4U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], rn, wn);
        put_reg(&inst->operands[2], base, true);
        put_immediate(&inst->operands[3], imm / (signed_pair?4:(wd?8:4)));
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
       parse_a64_reg(tok[0],&rd,&wd,&spd) && wd && !spd &&
       minias_parse_symbol_addend(tok[1],&expr) &&
       a64_attach_expr(inst,1U,&expr)) {
        inst->opcode=A64_MC_ADRP;inst->count=2U;
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
        if(sz<4U || tok[1][0]!='[' || tok[1][sz-1U]!=']' ||
           sz-2U>=sizeof(inner))return false;
        memcpy(inner,tok[1]+1U,sz-2U);inner[sz-2U]='\0';
        comma=strchr(inner,',');
        if(!comma)return false;
        *comma++='\0';
        if(!parse_a64_reg(minias_trim(inner),&regnum,&wn,&spn) || !wn)
            return false;
        b=minias_trim(comma);
        if(!a64_lo12_expr(b,&expr) ||
           !a64_attach_expr(inst,2U,&expr))return false;
        rn=regnum;
    }
    unit=!strcmp(op,"ldrsw")?4U:op[3]=='b'?1U:op[3]=='h'?2U:wd?8U:4U;
    if((unit==1U||unit==2U) && wd) {
        free(inst->operands[2].symbol);
        inst->operands[2].symbol=NULL;
        return false;
    }
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
static bool a64_system_register(const char *name,uint32_t *mrs,uint32_t *msr) {
    static const struct {const char *name;uint32_t read,write;} table[]={
        {"sp_el0",UINT32_C(0xd5384100),UINT32_C(0xd5184100)},
        {"tpidr_el0",UINT32_C(0xd53bd040),UINT32_C(0xd51bd040)},
        {"tpidrro_el0",UINT32_C(0xd53bd060),0U},
        /* Kernel EL1 registers, GNU ARM system-register encodings. */
        {"tpidr_el1",UINT32_C(0xd538d080),UINT32_C(0xd518d080)},
        {"tpidr_el2",UINT32_C(0xd53cd040),UINT32_C(0xd51cd040)},
        {"daif",UINT32_C(0xd53b4220),UINT32_C(0xd51b4220)},
        /* Verified against independently assembled AArch64 GNU-compatible
         * system-register encodings. A zero field means no legal access,
         * not a fallback to arbitrary sysreg bit patterns. */
        {"currentel",UINT32_C(0xd5384240),0U},
        {"CurrentEL",UINT32_C(0xd5384240),0U},
        {"sctlr_el1",UINT32_C(0xd5381000),UINT32_C(0xd5181000)},
        {"sctlr_el2",UINT32_C(0xd53c1000),UINT32_C(0xd51c1000)},
        {"sctlr_el3",UINT32_C(0xd53e1000),UINT32_C(0xd51e1000)},
        {"ttbr0_el1",UINT32_C(0xd5382000),UINT32_C(0xd5182000)},
        {"ttbr1_el1",UINT32_C(0xd5382020),UINT32_C(0xd5182020)},
        {"ttbr0_el2",UINT32_C(0xd53c2000),UINT32_C(0xd51c2000)},
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
    for(i=0U;i<sizeof(table)/sizeof(table[0]);++i)
        if(!strcmp(name,table[i].name)) {
            *mrs=table[i].read;*msr=table[i].write;return true;
        }
    return false;
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
        {"stadd",1U,true},{"stclr",2U,true},{"stset",3U,true}
    };
    char tok[4][96];
    unsigned i,n,rs,rt=31U,rn,mode,group=0U;
    bool wide,wr,sp;
    int64_t off;
    const char *suffix=NULL;
    for(i=0U;i<sizeof(ops)/sizeof(ops[0]);++i) {
        size_t plen=strlen(ops[i].name);
        if(strncmp(op,ops[i].name,plen)==0) {
            group=i;suffix=op+plen;break;
        }
    }
    if(suffix==NULL || !(*suffix=='\0' ||
       !strcmp(suffix,"a") || !strcmp(suffix,"l") ||
       !strcmp(suffix,"al")) ||
       (ops[group].store && *suffix && strcmp(suffix,"l")) ||
       (as->target_options&A64_FEATURE_LSE)==0U)return false;
    if(!split_operands(args,tok,&n) ||
       n!=(ops[group].store?2U:3U) ||
       !parse_a64_reg(tok[0],&rs,&wide,&sp) || sp ||
       (!ops[group].store &&
        (!parse_a64_reg(tok[1],&rt,&wr,&sp) || sp || wr!=wide)) ||
       !parse_a64_mem(tok[n-1U],&rn,&off,&mode) ||
       off!=0 || mode!=0U)return false;
    inst->opcode=A64_MC_LSE;inst->count=3U;
    inst->encoding=ops[group].kind |
      (ops[group].store?0x10U:0U) |
      ((strchr(suffix,'a')!=NULL)?0x20U:0U) |
      ((strchr(suffix,'l')!=NULL)?0x40U:0U);
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
           alu_n!=(ops[alu_k].unary?2U:3U) ||
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
        put_reg(&inst->operands[0],alu_rd,alu_wide);
        put_reg(&inst->operands[1],alu_rn,alu_wide);
        put_reg(&inst->operands[2],alu_rm,alu_mwide);
    } else if(!strcmp(op,"ubfx") || !strcmp(op,"ubfiz") ||
              !strcmp(op,"sbfx") || !strcmp(op,"sbfiz") ||
              !strcmp(op,"bfi")  || !strcmp(op,"bfxil")) {
        /* One ISA-owned UBFM/SBFM/BFM bitfield selector for GNU aliases:
         * extracts use imms=lsb+width-1, insertions wrap immr modulo
         * XLEN and use imms=width-1. No Common parser or ELF fork. */
        char bit_tok[4][96];
        unsigned bit_n,bit_rd,bit_rn,reg_bits;
        bool bit_wide,bit_source_wide,bit_sp;
        int64_t lsb,width;
        bool insertion=!strcmp(op,"ubfiz") || !strcmp(op,"sbfiz") ||
                       !strcmp(op,"bfi");
        uint32_t base=!strcmp(op,"ubfx") || !strcmp(op,"ubfiz")?
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
           width<1 || width>(int64_t)reg_bits-lsb) {
            minias_set_error(as,"bad-aarch64-bitfield-range:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_BITFIELD;inst->count=4U;
        inst->encoding=base;
        put_reg(&inst->operands[0],bit_rd,bit_wide);
        put_reg(&inst->operands[1],bit_rn,bit_source_wide);
        put_immediate(&inst->operands[2],
                      insertion?((int64_t)reg_bits-lsb)%(int64_t)reg_bits:lsb);
        put_immediate(&inst->operands[3],
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
              !strcmp(op,"tlbi")) {
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
            {"tlbi","vmalle1",UINT32_C(0xd508871f),false},
            {"tlbi","vmalle1is",UINT32_C(0xd508831f),false},
            {"tlbi","vae1",UINT32_C(0xd5088720),true},
            {"tlbi","vae1is",UINT32_C(0xd5088320),true},
            {"tlbi","vale1",UINT32_C(0xd50887a0),true},
            {"tlbi","vale1is",UINT32_C(0xd50883a0),true},
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
        bool wide;
        uint32_t mrscode=0U,msrcode=0U;
        bool read=!strcmp(op,"mrs");
        if(read ?
           (!parse_register(first,&rt,&wide) || !wide ||
            !a64_system_register(second,&mrscode,&msrcode) ||
            mrscode==0U) :
           (!parse_register(second,&rt,&wide) || !wide ||
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
    } else if((!strcmp(op,"br")||!strcmp(op,"blr")) &&
       parse_register(args,&reg,&reg64) && reg64) {
        inst->opcode=!strcmp(op,"br")?A64_MC_BR:A64_MC_BLR;
        inst->count=1U;
        put_reg(&inst->operands[0],reg,true);
    } else if((!strcmp(op,"movz") || !strcmp(op,"movk") ||
               !strcmp(op,"movn"))) {
        char parts[4][96];unsigned n,rd,imm16,shift=0U;
        bool wide,sp;
        if(!split_operands(args,parts,&n) || (n!=2U && n!=3U) ||
           !parse_a64_reg(parts[0],&rd,&wide,&sp) || sp ||
           !parse_imm16(parts[1],&imm16)) {
            minias_set_error(as,"bad-aarch64-wide-immediate:line=%zu",stmt->line);
            free(inst);return false;
        }
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
        inst->opcode=A64_MC_WIDE_IMM;inst->count=2U;
        inst->encoding=(!strcmp(op,"movz")?0U:!strcmp(op,"movk")?1U:2U)
                      |(shift<<2U);
        put_reg(&inst->operands[0],rd,wide);
        put_immediate(&inst->operands[1],(int64_t)imm16);
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
        bool negative=*literal=='-';
        int64_t signed_value=0;
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
            fixup=MINIAS_FIXUP_A64_ADRP_HI21;
            break;
        case A64_MC_ADD_LO12:
            code=(inst->operands[0].register_64?
                  UINT32_C(0x91000000):UINT32_C(0x11000000))|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
            expr=inst->operands[2].symbol;
            fixup=MINIAS_FIXUP_A64_ADD_LO12;
            break;
        case A64_MC_LDST_LO12:
            code=inst->encoding|(inst->operands[1].reg<<5U)|
                 inst->operands[0].reg;
            expr=inst->operands[2].symbol;
            fixup=inst->encoding==UINT32_C(0x39400000) ||
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
            unsigned shift=inst->encoding>>2U;
            bool wide=inst->operands[0].register_64;
            uint32_t base=variant==0U?UINT32_C(0x52800000):
                          variant==1U?UINT32_C(0x72800000):
                                      UINT32_C(0x12800000);
            code=base|(wide?UINT32_C(0x80000000):0U)|
                (shift<<21U)|
                ((uint32_t)inst->operands[1].immediate<<5U)|
                inst->operands[0].reg;
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
            unsigned kind=inst->encoding;
            if(kind<2U) {
                bool wide=inst->operands[0].register_64;
                code=(wide?UINT32_C(0xc85f7c00):UINT32_C(0x885f7c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (inst->operands[1].reg<<5U) | inst->operands[0].reg;
            }else {
                bool wide=inst->operands[1].register_64;
                code=(wide?UINT32_C(0xc8007c00):UINT32_C(0x88007c00)) |
                     ((kind&1U)?UINT32_C(0x8000):0U) |
                     (inst->operands[0].reg<<16U) |
                     (inst->operands[2].reg<<5U) | inst->operands[1].reg;
            }
            break;
        }
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
            uint32_t base;
            if(kind==0U) {
                base=wide?UINT32_C(0xc8a07c00):UINT32_C(0x88a07c00);
                if(acquire)base|=UINT32_C(0x00400000);
                if(release)base|=UINT32_C(0x00008000);
            }else {
                base=wide?UINT32_C(0xf8200000):UINT32_C(0xb8200000);
                if(acquire)base|=UINT32_C(0x00800000);
                if(release)base|=UINT32_C(0x00400000);
                base|=kind==2U?UINT32_C(0x1000):
                      kind==3U?UINT32_C(0x3000):
                      kind==4U?UINT32_C(0x8000):0U;
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
        case A64_MC_LDST_INDEX:
            code=inst->encoding|(inst->operands[2].reg<<16U)|
                 (inst->operands[1].reg<<5U)|inst->operands[0].reg;
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
            uint32_t base = inst->opcode==A64_MC_LDPSW?
                (inst->encoding==1U?UINT32_C(0x69c00000):
                 inst->encoding==2U?UINT32_C(0x68c00000):
                                    UINT32_C(0x69400000)):
                inst->opcode == A64_MC_STP ?
                (inst->encoding == 1U ? UINT32_C(0x29800000) :
                 inst->encoding == 2U ? UINT32_C(0x28800000) : UINT32_C(0x29000000)) :
                (inst->encoding == 1U ? UINT32_C(0x29c00000) :
                 inst->encoding == 2U ? UINT32_C(0x28c00000) : UINT32_C(0x29400000));
            if (sf && inst->opcode!=A64_MC_LDPSW)
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
        if(!parse_a64_reg(canonical,&n,&wide,&sp) ||
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
        bool v85=!strcmp(args,"armv8.5-a");
        if(strcmp(args,"armv8-a")!=0 && !v85 &&
           strcmp(args,"armv8-a+crypto")!=0) {
            minias_set_error(as, "unsupported-aarch64-architecture:%s:line=%zu",
                             args, line);
            return false;
        }
        if(v85)as->target_options|=A64_FEATURE_LSE;
        else as->target_options&=~A64_FEATURE_LSE;
        return true;
    }
    if(!strcmp(op,".arch_extension")) {
        *handled=true;
        if(!strcmp(args,"lse"))as->target_options |= A64_FEATURE_LSE;
        else if(!strcmp(args,"nolse"))as->target_options &= ~A64_FEATURE_LSE;
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
            if(!*word || !minias_eval_u64_expression(word,&bits) ||
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
