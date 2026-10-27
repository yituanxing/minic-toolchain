#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    A64_MC_MRS,
    A64_MC_MSR,
    A64_MC_DMB,
    A64_MC_RET,
    A64_MC_MOVZ,
    A64_MC_SVC,
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
    A64_MC_ADRP,
    A64_MC_ADD_LO12,
    A64_MC_LDST_LO12,
    A64_MC_MOV_REG,
    A64_MC_ADD_REG,
    A64_MC_SUB_REG,
    A64_MC_AND_REG,
    A64_MC_ORR_REG,
    A64_MC_EOR_REG,
    A64_MC_ADD_IMM,
    A64_MC_SUB_IMM,
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
    A64_MC_STP,
    A64_MC_LDP
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
    char *end;
    long long value;
    if (*text == '#') ++text;
    if (!*text) return false;
    errno = 0;
    value = strtoll(text, &end, 0);
    if (errno || end == text || *end) return false;
    *out = (int64_t)value;
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
         strcmp(op, "and") == 0 || strcmp(op, "orr") == 0 ||
         strcmp(op, "eor") == 0) && n == 3U) {
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) ||
            !parse_a64_reg(t[1], &rn, &wn, &spn) || wd != wn)
            return false;
        if (parse_a64_reg(t[2], &rm, &wm, &spm)) {
            if (wd != wm || spd || spn || spm) return false;
            kind = strcmp(op, "add") == 0 ? A64_MC_ADD_REG :
                   strcmp(op, "sub") == 0 ? A64_MC_SUB_REG :
                   strcmp(op, "and") == 0 ? A64_MC_AND_REG :
                   strcmp(op, "orr") == 0 ? A64_MC_ORR_REG : A64_MC_EOR_REG;
            put_reg(&inst->operands[2], rm, wm);
        } else if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0) &&
                   parse_signed_number(t[2], &imm) &&
                   imm >= 0 && imm <= 4095) {
            if ((rd == 31U && !spd) || (rn == 31U && !spn))
                return false;
            kind = strcmp(op, "add") == 0 ? A64_MC_ADD_IMM : A64_MC_SUB_IMM;
            put_immediate(&inst->operands[2], imm);
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

    if ((strcmp(op, "ldr") == 0 || strcmp(op, "str") == 0 ||
         strcmp(op, "ldrb") == 0 || strcmp(op, "strb") == 0 ||
         strcmp(op, "ldrh") == 0 || strcmp(op, "strh") == 0) && n == 2U) {
        unsigned unit;
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) || spd ||
            !parse_a64_mem(t[1], &base, &imm, &mode) || mode != 0U)
            return false;
        unit = op[3] == 'b' ? 1U : op[3] == 'h' ? 2U : wd ? 8U : 4U;
        if ((unit == 1U || unit == 2U) && wd) return false;
        if (imm < 0 || imm % (int64_t)unit != 0 || imm / (int64_t)unit > 4095)
            return false;
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
    if ((strcmp(op, "stp") == 0 || strcmp(op, "ldp") == 0) &&
        (n == 3U || n == 4U)) {
        if (!parse_a64_reg(t[0], &rd, &wd, &spd) || spd ||
            !parse_a64_reg(t[1], &rn, &wn, &spn) || spn || wd != wn ||
            !parse_a64_mem(t[2], &base, &imm, &mode))
            return false;
        if (n == 4U) {
            if (mode != 0U || imm != 0 ||
                !parse_signed_number(t[3], &imm)) return false;
            mode = 2U;
        }
        if (imm % (wd ? 8 : 4) != 0 ||
            imm < -(int64_t)(wd ? 512 : 256) ||
            imm > (int64_t)(wd ? 504 : 252))
            return false;
        inst->opcode = strcmp(op, "stp") == 0 ? A64_MC_STP : A64_MC_LDP;
        inst->encoding = mode;
        inst->count = 4U;
        put_reg(&inst->operands[0], rd, wd);
        put_reg(&inst->operands[1], rn, wn);
        put_reg(&inst->operands[2], base, true);
        put_immediate(&inst->operands[3], imm / (wd ? 8 : 4));
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
       strcmp(op,"ldrh")!=0 && strcmp(op,"strh")!=0)
        return false;
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
    unit=op[3]=='b'?1U:op[3]=='h'?2U:wd?8U:4U;
    if((unit==1U||unit==2U) && wd) {
        free(inst->operands[2].symbol);
        inst->operands[2].symbol=NULL;
        return false;
    }
    inst->opcode=A64_MC_LDST_LO12;
    inst->count=3U;
    put_reg(&inst->operands[0],rd,wd);
    put_reg(&inst->operands[1],rn,true);
    inst->encoding=strcmp(op,"ldr")==0 ?
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
        {"tpidrro_el0",UINT32_C(0xd53bd060),0U}
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

static bool parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
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
    if(!strcmp(op,"cset") || !strcmp(op,"csetm") ||
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
    } else if(!strcmp(op,"dmb")) {
        unsigned barrier;
        if(!a64_barrier_option(args,&barrier)) {
            minias_set_error(as,"bad-aarch64-barrier:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_DMB;
        inst->encoding=UINT32_C(0xd50330bf)|(barrier<<8U);
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
    } else if (!strcmp(op,"hint")) {
        int64_t hint;
        if(!parse_signed_number(args,&hint) || hint<0 || hint>127) {
            minias_set_error(as,"bad-aarch64-hint:%s:line=%zu",
                             args,stmt->line);
            free(inst);return false;
        }
        inst->opcode=A64_MC_HINT;
        inst->count=1U;
        put_immediate(&inst->operands[0],hint);
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
    } else if (strcmp(op, "svc") == 0 && parse_imm16(args, &imm)) {
        inst->opcode = A64_MC_SVC;
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
            code=inst->encoding|inst->operands[0].reg;
            break;
        case A64_MC_DMB:
            code=inst->encoding;
            break;
        case A64_MC_HINT:
            /* A64 HINT imm7, including the architectural HINT #25
             * instruction used throughout Linux entry code. */
            code=UINT32_C(0xd503201f) |
                 ((uint32_t)inst->operands[0].immediate<<5U);
            break;
        case A64_MC_NOP:
            code = UINT32_C(0xd503201f);
            break;
        case A64_MC_RET:
            code = inst->count == 0U ? UINT32_C(0xd65f03c0) :
                UINT32_C(0xd65f0000) | (inst->operands[0].reg << 5U);
            break;
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
        case A64_MC_SVC:
            code = UINT32_C(0xd4000001) |
                ((uint32_t)inst->operands[0].immediate << 5U);
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
        case A64_MC_ADD_REG:
        case A64_MC_SUB_REG:
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
                inst->opcode == A64_MC_AND_REG ? UINT32_C(0x0a000000) :
                inst->opcode == A64_MC_ORR_REG ? UINT32_C(0x2a000000) :
                                                 UINT32_C(0x4a000000);
            code = base | (sf ? UINT32_C(0x80000000) : 0U) |
                   (rm << 16U) | (rn << 5U) | rd;
            break;
        }
        case A64_MC_ADD_IMM:
        case A64_MC_SUB_IMM: {
            bool sf = inst->operands[0].register_64;
            unsigned rd = inst->operands[0].reg;
            unsigned rn = inst->operands[1].reg;
            unsigned imm = (unsigned)inst->operands[2].immediate;
            code = (inst->opcode == A64_MC_ADD_IMM ?
                    UINT32_C(0x11000000) : UINT32_C(0x51000000)) |
                   (sf ? UINT32_C(0x80000000) : 0U) |
                   (imm << 10U) | (rn << 5U) | rd;
            break;
        }
        case A64_MC_CMP_REG:
        case A64_MC_CMN_REG: {
            bool sf = inst->operands[0].register_64;
            code = (inst->opcode==A64_MC_CMP_REG ?
                    UINT32_C(0x6b00001f):UINT32_C(0x2b00001f)) |
                   (sf ? UINT32_C(0x80000000) : 0U) |
                   (inst->operands[1].reg << 16U) |
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
        case A64_MC_LDP: {
            bool sf = inst->operands[0].register_64;
            uint32_t base = inst->opcode == A64_MC_STP ?
                (inst->encoding == 1U ? UINT32_C(0x29800000) :
                 inst->encoding == 2U ? UINT32_C(0x28800000) : UINT32_C(0x29000000)) :
                (inst->encoding == 1U ? UINT32_C(0x29c00000) :
                 inst->encoding == 2U ? UINT32_C(0x28c00000) : UINT32_C(0x29400000));
            if (sf) base |= UINT32_C(0x80000000);
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
    /* GCC emits .arch armv8-a before its first A64 instruction. Verify the
     * exact declared ISA here; never silently accept a richer architecture. */
    if (strcmp(op, ".arch") == 0) {
        *handled = true;
        if (strcmp(args, "armv8-a") != 0) {
            minias_set_error(as, "unsupported-aarch64-architecture:%s:line=%zu",
                             args, line);
            return false;
        }
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
        case MINIAS_FIXUP_A64_CALL26:
            return width == 4U ? AARCH64_R_CALL26 : 0U;
        case MINIAS_FIXUP_A64_JUMP26:
            return width == 4U ? AARCH64_R_JUMP26 : 0U;
        case MINIAS_FIXUP_A64_CONDBR19:
            return width == 4U ? AARCH64_R_CONDBR19 : 0U;
        case MINIAS_FIXUP_A64_TSTBR14:
            return width == 4U ? AARCH64_R_TSTBR14 : 0U;
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
    .object_attributes = attributes,
    .name = "aarch64",
    .resolve_profile = aarch64_resolve_profile,
    .parse_mcinst = parse_mcinst,
    .measure_mcinst = aarch64_measure_mcinst,
    .encode = encode,
    .append_code_alignment = code_alignment,
    .fixup_relocation_type = aarch64_fixup_relocation_type,
    .validate_output = validate,
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
