#include "target/x86_64/asm_contract.h"
#include <stdio.h>
#include <string.h>

/* Target-owned GNU register/constraint admission: no Core lowering state,
 * no SSA mutation. Additional x86 forms extend this owner, not core_lower. */
/* Keep the GS per-CPU source reserved, without capturing ordinary MOV. */
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const reserved[] = {"movq %%gs:", "cmp"};
    return minic_backend_inline_asm_prefix_matches(
        src, reserved, sizeof(reserved) / sizeof(reserved[0]));
}

/* Exact original Linux cmp/sbb two-instruction target ISA shape. */
bool minic_x86_64_asm_nospec_template(const char *text, size_t len) {
    static const char exact[] = "cmp%1,%2;sbb%0,%0";
    char compact[96];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    /* Original GNU Linux 6.6.143 barrier.h ends SBB with a semicolon.
     * Permit exactly one trailing delimiter, not extra machine opcodes. */
    return strcmp(compact, exact) == 0 ||
           strcmp(compact, "cmp%1,%2;sbb%0,%0;") == 0;
}

static bool target_constraint_is(const MinicInlineAsmOperand *op, const char *s) {
    const size_t n = s ? strlen(s) : 0U;
    return op && s && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, s, n) == 0;
}

/* Original x86 Linux this_cpu_read_stable_8 is a nonvolatile GNU p
 * address computation followed by a REAL GS-segment hardware read.
 * Only the literal 64-bit instruction / two exact operands are admitted.
 * A Core address SSA value represents P[var]; x86 still owns the GS ISA. */
bool minic_x86_64_asm_percpu_stable_template(const char *s, size_t length) {
    static const char *const forms[] = {
        "movq%%gs:%P[var],%[val]", "movq%%gs:%P1,%0"
    };
    char compact[96]; size_t i, n = 0U;
    if (!s || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = s[i];
        if (c == ';' || c == '\r' || c == '\n') return false;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(forms[i], compact) == 0) return true;
    return false;
}
static bool x86_percpu_source(const MinicC0Program *program,
                              const MinicInlineAsm *src) {
    const MinicExpression *out, *in;
    MinicType type;
    if (!program || !src || src->is_goto || src->is_volatile ||
        src->output_count != 1U || src->input_count != 1U ||
        !src->outputs || !src->inputs ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber || src->label_count != 0U ||
        !minic_x86_64_asm_percpu_stable_template(
            src->template_text, src->template_length) ||
        !target_constraint_is(&src->outputs[0], "=r") ||
        !target_constraint_is(&src->inputs[0], "p"))
        return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    in = minic_c0_program_expression(program, src->inputs[0].expression);
    return out && in && out->value_category == MINIC_VALUE_LVALUE &&
           !minic_type_is_const(out->type) &&
           minic_type_unqualified(out->type, &type) &&
           (minic_type_equal(type, minic_type_unsigned_long_long()) ||
            minic_type_equal(type, minic_type_unsigned_long())) &&
           minic_type_is_pointer(in->type);
}
static bool c_is(const MinicInlineAsmOperand *op, const char *s) {
    return target_constraint_is(op, s);
}
static bool clobber_is(const MinicInlineAsmRegisterClobber *op, const char *s) {
    const size_t n = strlen(s);
    return op->name && op->name_length == n && memcmp(op->name, s, n) == 0;
}
/* Whitespace is irrelevant for these four specific instructions, whose
 * operand constraints and exact instruction sequence are checked together. */
/* Only integer operands of the same real x86 width; never silently
 * convert a user-supplied operand or drop the hardware condition flags. */
static bool x86_nospec_source(const MinicC0Program *program,
                              const MinicInlineAsm *src) {
    const MinicExpression *out, *size, *index;
    MinicType out_type, size_type, index_type;
    if (!program || !src || !src->outputs || !src->inputs ||
        !src->is_volatile || src->is_goto || src->label_count ||
        src->output_count != 1U || src->input_count != 2U ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        !src->register_clobbers || src->has_memory_clobber ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !minic_x86_64_asm_nospec_template(src->template_text, src->template_length) ||
        !c_is(&src->outputs[0], "=r") ||
        !(c_is(&src->inputs[0], "g") || c_is(&src->inputs[0], "rm") ||
          c_is(&src->inputs[0], "r")) ||
        !c_is(&src->inputs[1], "r"))
        return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    size = minic_c0_program_expression(program, src->inputs[0].expression);
    index = minic_c0_program_expression(program, src->inputs[1].expression);
    return out && size && index &&
        out->value_category == MINIC_VALUE_LVALUE &&
        !minic_type_is_const(out->type) &&
        minic_type_unqualified(out->type, &out_type) &&
        minic_type_equal(out_type, minic_type_unsigned_long()) &&
        minic_type_unqualified(size->type, &size_type) &&
        minic_type_unqualified(index->type, &index_type) &&
        minic_type_equal(size_type, index_type) &&
        size_type.base_kind == MINIC_TYPE_BASE_INT &&
        (size_type.integer_rank == MINIC_INTEGER_RANK_INT ||
         size_type.integer_rank == MINIC_INTEGER_RANK_LONG ||
         size_type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
}

bool minic_x86_64_asm_mulacc_template(const char *template_text,
                                       size_t template_length) {
    static const char expected[] = "mulq%7addq%%rax,%0adcq%%rdx,%1adcq$0,%2";
    char text[128];
    size_t i, j = 0U;
    if (template_length >= sizeof(text)) return false;
    for (i = 0U; i < template_length; ++i) {
        const char c = template_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j >= sizeof(text)-1U) return false;
        text[j++] = c;
    }
    text[j] = '\0';
    return strcmp(text, expected) == 0;
}

/* Original Linux mul_u64_u64_div_u64:
 *   asm("mulq %2; divq %3" : "=a"(q)
 *       : "a"(a), "rm"(mul), "rm"(div) : "rdx");
 * Admit one exact machine sequence, never generic executable template
 * passthrough with unchecked physical clobbers. The machine emitter chooses
 * the register alternative for both rm inputs and keeps RDX out of inputs. */
bool minic_x86_64_asm_muldiv_template(const char *text, size_t length) {
    static const char expected[] = "mulq%2;divq%3";
    char normalized[64];
    size_t i, j = 0U;
    if (!text || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j + 1U >= sizeof(normalized)) return false;
        normalized[j++] = c;
    }
    normalized[j] = '\0';
    return strcmp(normalized, expected) == 0;
}

/* Exact original GNU Linux __raw_try_cmpxchg 8-case source.
 * Linux CC_SET(z) must remain a real hardware flag output, not C logic. */
static const MinicX86TryCasDesc x86_trycas_forms[] = {
    {"cmpxchgb","%r10b","movzbl (%r11), %eax","movb %al, (%r11)",1U,true},
    {"cmpxchgw","%r10w","movzwl (%r11), %eax","movw %ax, (%r11)",2U,true},
    {"cmpxchgl","%r10d","movl (%r11), %eax","movl %eax, (%r11)",4U,true},
    {"cmpxchgq","%r10","movq (%r11), %rax","movq %rax, (%r11)",8U,true},
    {"cmpxchgb","%r10b","movzbl (%r11), %eax","movb %al, (%r11)",1U,false},
    {"cmpxchgw","%r10w","movzwl (%r11), %eax","movw %ax, (%r11)",2U,false},
    {"cmpxchgl","%r10d","movl (%r11), %eax","movl %eax, (%r11)",4U,false},
    {"cmpxchgq","%r10","movq (%r11), %rax","movq %rax, (%r11)",8U,false}
};
const MinicX86TryCasDesc *minic_x86_64_trycas_desc(MinicX86TryCas form) {
    return form>=MINIC_X86_TRYCAS_B_LOCK&&form<=MINIC_X86_TRYCAS_Q_LOCAL ?
        &x86_trycas_forms[(size_t)form-1U]:NULL;
}
MinicX86TryCas minic_x86_64_trycas_form(const char *text,size_t len) {
    static const char smp[]=
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256],expected[256];
    size_t i,n=0U,mode,variant;
    if(!text||!len||len>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(mode=0U;mode<sizeof(x86_trycas_forms)/sizeof(x86_trycas_forms[0]);++mode)
        for(variant=0U;variant<2U;++variant) {
            const MinicX86TryCasDesc *d=&x86_trycas_forms[mode];
            const int written=snprintf(expected,sizeof(expected),
                "%s%s%s,%s/*outputconditioncodez*/",
                d->locked?smp:"",d->op,
                variant?"%3":"%[new]",variant?"%1":"%[ptr]");
            if(written>0&&(size_t)written<sizeof(expected)&&
               strcmp(compact,expected)==0)
                return (MinicX86TryCas)(mode+1U);
        }
    return MINIC_X86_TRYCAS_NONE;
}
static bool x86_trycas_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86TryCas form=src ?
        minic_x86_64_trycas_form(src->template_text,src->template_length):
        MINIC_X86_TRYCAS_NONE;
    const MinicX86TryCasDesc *d=minic_x86_64_trycas_desc(form);
    const MinicExpression *e[4];
    MinicType t[4];
    const unsigned int rank=d ?
        d->width==1U?MINIC_INTEGER_RANK_CHAR:
        d->width==2U?MINIC_INTEGER_RANK_SHORT:
        d->width==4U?MINIC_INTEGER_RANK_INT:MINIC_INTEGER_RANK_LONG_LONG:
        MINIC_INTEGER_RANK_NONE;
    size_t i;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       !src->outputs||!src->inputs||src->output_count!=3U||
       src->input_count!=1U||src->label_count||!src->has_memory_clobber||
       src->clobber_count!=1U||src->register_clobber_count||
       !target_constraint_is(&src->outputs[0],"=@ccz")||
       !target_constraint_is(&src->outputs[1],"+m")||
       !target_constraint_is(&src->outputs[2],"+a")||
       !target_constraint_is(&src->inputs[0],d->width==1U?"q":"r"))
        return false;
    for(i=0U;i<4U;++i){
        const MinicInlineAsmOperand *op=i<3U?
            &src->outputs[i]:&src->inputs[0];
        e[i]=minic_c0_program_expression(program,op->expression);
        if(!e[i]||(i<3U&&(e[i]->value_category!=MINIC_VALUE_LVALUE||
                             minic_type_is_const(e[i]->type)))||
           !minic_type_unqualified(e[i]->type,&t[i]))
            return false;
    }
    if(!minic_type_equal(t[0],minic_type_bool())||
       !minic_type_is_integer(t[1])||t[1].pointer_depth||
       (unsigned int)t[1].integer_rank!=rank||
       !minic_type_equal(t[1],t[2])||!minic_type_equal(t[1],t[3]))
        return false;
    contract->operand_count=4U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Target-only descriptor for all 8 GNU Linux atomic RMWcc forms.
 * This is the only source/machine ALU and flag-sign table. */
static const struct {
    MinicX86AtomicCCDesc desc;
    const char *flag;
} x86_atomic_cc_forms[] = {
    {{"decl", 4U, false, false}, "e"},
    {{"incl", 4U, false, false}, "e"},
    {{"decq", 8U, false, false}, "e"},
    {{"incq", 8U, false, false}, "e"},
    {{"subl", 4U, true, false}, "e"},
    {{"addl", 4U, true, true}, "s"},
    {{"subq", 8U, true, false}, "e"},
    {{"addq", 8U, true, true}, "s"}
};
const MinicX86AtomicCCDesc *minic_x86_64_atomic_cc_description(
    MinicX86AtomicCC form) {
    const size_t id = (size_t)form;
    return id >= MINIC_X86_ATOMIC_CC_DEC32 &&
           id <= MINIC_X86_ATOMIC_CC_ADD64_SIGN ?
        &x86_atomic_cc_forms[id - 1U].desc : NULL;
}
/* Exact original GNU SMP patchtable + LOCK + operand + CC_SET text,
 * whether source-named or numeric after Common inline ASM normalization.
 * Never forward source ASM or any unrecognized suffix to GAS. */
MinicX86AtomicCC minic_x86_64_atomic_cc_form(const char *text, size_t len) {
    static const char prefix[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i, n = 0U, id, variant;
    if (!text || len == 0U || len >= sizeof(compact))
        return MINIC_X86_ATOMIC_CC_NONE;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_ATOMIC_CC_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (id = 0U; id < sizeof(x86_atomic_cc_forms) /
                       sizeof(x86_atomic_cc_forms[0]); ++id) {
        const MinicX86AtomicCCDesc *d = &x86_atomic_cc_forms[id].desc;
        for (variant = 0U; variant < 2U; ++variant) {
            const char *mem = variant ? "%0" : "%[var]";
            const char *value = variant ? "%2" : "%[val]";
            const int written = d->has_value ?
                snprintf(expected, sizeof(expected),
                    "%s%s%s,%s/*outputconditioncode%s*/",
                    prefix, d->opcode, value, mem, x86_atomic_cc_forms[id].flag) :
                snprintf(expected, sizeof(expected),
                    "%s%s%s/*outputconditioncode%s*/",
                    prefix, d->opcode, mem, x86_atomic_cc_forms[id].flag);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (MinicX86AtomicCC)(id + 1U);
        }
    }
    return MINIC_X86_ATOMIC_CC_NONE;
}
static bool x86_linux_atomic_cc_source(
    const MinicC0Program *program, const MinicInlineAsm *src) {
    const MinicExpression *mem, *flag, *value;
    MinicType mt, ft, vt;
    const MinicX86AtomicCC form = src ?
        minic_x86_64_atomic_cc_form(src->template_text, src->template_length) :
        MINIC_X86_ATOMIC_CC_NONE;
    const MinicX86AtomicCCDesc *d =
        minic_x86_64_atomic_cc_description(form);
    if (!program || !src || !d || !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U ||
        src->input_count != (d->has_value ? 1U : 0U) ||
        (d->has_value && !src->inputs) || src->label_count ||
        src->clobber_count != 1U || !src->has_memory_clobber ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "+m") ||
        !target_constraint_is(&src->outputs[1],
                              d->flag_sign ? "=@ccs" : "=@cce") ||
        (d->has_value && !target_constraint_is(&src->inputs[0], "er")))
        return false;
    mem = minic_c0_program_expression(program, src->outputs[0].expression);
    flag = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!mem || !flag || mem->value_category != MINIC_VALUE_LVALUE ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(mem->type) || minic_type_is_const(flag->type) ||
        !minic_type_unqualified(mem->type, &mt) ||
        !minic_type_unqualified(flag->type, &ft) ||
        !minic_type_equal(ft, minic_type_bool()))
        return false;
    if (d->width == 4U ?
        !minic_type_equal(mt, minic_type_int()) :
        (!minic_type_equal(mt, minic_type_long()) &&
         !minic_type_equal(mt, minic_type_long_long())))
        return false;
    if (d->has_value) {
        value = minic_c0_program_expression(program, src->inputs[0].expression);
        if (!value || !minic_type_unqualified(value->type, &vt) ||
            !minic_type_equal(vt, mt)) return false;
    }
    return true;
}
/* Original GNU Linux 6.6.143 x86 bitops: exact source templates, not
 * a generic inline assembly passthrough. The same table is the ONLY
 * authoritative ISA/memory/flag descriptor used by source and machine.
 * Names become canonical numeric operands in shared Core. */
static const struct {
    const char *literal;
    const char *canonical;
    MinicX86BitopsDesc desc;
} x86_linux_bitops[] = {
    {"btq%2,%1/*outputconditioncodec*/",
     "btq%2,%1/*outputconditioncodec*/",
     {"btq",0,1,0,0}},
    {"btsq%2,%1/*outputconditioncodec*/",
     "btsq%2,%1/*outputconditioncodec*/",
     {"btsq",0,1,0,1}},
    {"btrq%2,%1/*outputconditioncodec*/",
     "btrq%2,%1/*outputconditioncodec*/",
     {"btrq",0,1,0,1}},
    {"btcq%2,%1/*outputconditioncodec*/",
     "btcq%2,%1/*outputconditioncodec*/",
     {"btcq",0,1,0,1}},
    {"btsq%1,%0","btsq%1,%0",{"btsq",0,0,0,1}},
    {"btrq%1,%0","btrq%1,%0",{"btrq",0,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%1,%0",
     {"btsq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%1,%0",
     {"btrq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%2,%0/*outputconditioncodec*/",
     {"btsq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%2,%0/*outputconditioncodec*/",
     {"btrq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;orb%b1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;orb%b1,%0",
     {"orb",1,0,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;andb%b1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;andb%b1,%0",
     {"andb",1,0,1,1}}
};
MinicX86BitopsForm minic_x86_64_linux_bitops_form(
    const char *text,size_t length) {
    char compact[256];
    size_t i,j=0U;
    if (!text||!length||length>=sizeof(compact))
        return MINIC_X86_BITOPS_NONE;
    for(i=0U;i<length;++i) {
        const char ch=text[i];
        if (ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_BITOPS_NONE;
        compact[j++]=ch;
    }
    compact[j]='\0';
    for(i=0U;i<sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]);++i)
        if(strcmp(compact,x86_linux_bitops[i].literal)==0||
           strcmp(compact,x86_linux_bitops[i].canonical)==0)
            return (MinicX86BitopsForm)(i+1U);
    return MINIC_X86_BITOPS_NONE;
}
const MinicX86BitopsDesc *minic_x86_64_bitops_description(
    MinicX86BitopsForm form) {
    const size_t index = (size_t)form;
    if(index==0U||index>sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]))
        return NULL;
    return &x86_linux_bitops[index-1U].desc;
}
static bool x86_linux_bitops_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86BitopsForm form=src ?
        minic_x86_64_linux_bitops_form(
            src->template_text,src->template_length):MINIC_X86_BITOPS_NONE;
    const MinicX86BitopsDesc *desc=minic_x86_64_bitops_description(form);
    const bool byte_rmw = form==MINIC_X86_BITOPS_LOCK_OR_BYTE ||
                          form==MINIC_X86_BITOPS_LOCK_AND_BYTE;
    const bool needs_memory_clobber = !byte_rmw ||
                                      form==MINIC_X86_BITOPS_LOCK_OR_BYTE;
    const MinicInlineAsmOperand *mem,*bit,*flag;
    const MinicExpression *m,*b,*f;
    MinicType mem_type,bit_type,flag_type;
    if(!program||!src||!contract||!desc||src->is_goto||
       src->label_count||src->register_clobber_count||
       src->has_memory_clobber!=needs_memory_clobber||
       src->clobber_count!=(needs_memory_clobber?1U:0U)||
       src->output_count!=(desc->has_flag?(desc->memory_output?2U:1U):
                           (desc->memory_output?1U:0U))||
       src->input_count!=(desc->memory_output?1U:2U)||
       (src->output_count&&!src->outputs)||!src->inputs||
       (desc->is_locked&&!src->is_volatile))
        return false;
    flag=desc->has_flag?(desc->memory_output?&src->outputs[1]:
                                         &src->outputs[0]):NULL;
    mem=desc->memory_output?&src->outputs[0]:&src->inputs[0];
    bit=desc->memory_output?&src->inputs[0]:&src->inputs[1];
    if(!target_constraint_is(mem,desc->memory_output?"+m":"m")||
       !target_constraint_is(bit,byte_rmw?"iq":"Ir")||
       (flag&&!target_constraint_is(flag,"=@ccc")))
        return false;
    m=minic_c0_program_expression(program,mem->expression);
    b=minic_c0_program_expression(program,bit->expression);
    f=flag?minic_c0_program_expression(program,flag->expression):NULL;
    if(!m||!b||m->value_category!=MINIC_VALUE_LVALUE||
       (desc->writes_memory&&minic_type_is_const(m->type))||
       !minic_type_unqualified(m->type,&mem_type)||
       !minic_type_unqualified(b->type,&bit_type)||
       (byte_rmw ?
         /* GNU Linux WBYTE_ADDR may be volatile char, signed char, or
          * unsigned char. Char *RANK*, rather than three exact type
          * constructors, is the true 8-bit C-memory width contract:
          * the original parsed type can carry extra source qualifiers.
          * Do not broaden to _Bool or enum or int, or widen to 64-bit. */
         (!minic_type_is_integer(mem_type) ||
          mem_type.pointer_depth != 0U ||
          mem_type.integer_rank != MINIC_INTEGER_RANK_CHAR) :
         (!minic_type_equal(mem_type,minic_type_long())&&
          !minic_type_equal(mem_type,minic_type_unsigned_long())))||
       (byte_rmw?
         (!minic_type_equal(bit_type,minic_type_int())&&
          !minic_type_equal(bit_type,minic_type_unsigned_int())):
         (!minic_type_equal(bit_type,minic_type_long())&&
          !minic_type_equal(bit_type,minic_type_unsigned_long())))||
       (flag&&(!f||f->value_category!=MINIC_VALUE_LVALUE||
          minic_type_is_const(f->type)||
          !minic_type_unqualified(f->type,&flag_type)||
          !minic_type_equal(flag_type,minic_type_bool()))))
        return false;
    contract->operand_count=src->output_count+src->input_count;
    if(desc->memory_output) {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        if(desc->has_flag) {
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        } else {
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        }
    } else if(desc->has_flag) {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1]=desc->writes_memory ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    } else {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    return true;
}
/* Exact x86 Linux word-sized GNU BSF/BSR spelling. The REP prefix on
 * Linux __ffs is deliberate: it becomes TZCNT where available, and
 * preserves BSF bit-index semantics for nonzero inputs on older CPUs.
 * An all-zero input has no defined output; callers must not depend on it. */
/* Decode complete GNU source/machine bit-scan templates once.
 * Tied input #2 is materialized explicitly as the pre-BSR destination,
 * so a zero source is not silently changed to an uninitialized register. */
MinicX86BitScanForm minic_x86_64_asm_bit_scan_form(
    const char *text, size_t length) {
    static const struct { const char *text; MinicX86BitScanForm form; } forms[] = {
        {"rep;bsf%1,%0", MINIC_X86_BIT_SCAN_BSF64},
        {"bsr%1,%0", MINIC_X86_BIT_SCAN_BSR64},
        {"bsfl%1,%0", MINIC_X86_BIT_SCAN_BSF32_TIED},
        {"bsrl%1,%0", MINIC_X86_BIT_SCAN_BSR32_TIED},
        {"bsrq%1,%q0", MINIC_X86_BIT_SCAN_BSR64_READWRITE}
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact))
        return MINIC_X86_BIT_SCAN_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_BIT_SCAN_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].text) == 0)
            return forms[i].form;
    return MINIC_X86_BIT_SCAN_NONE;
}
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse) {
    const MinicX86BitScanForm form = minic_x86_64_asm_bit_scan_form(text, length);
    if (!is_reverse ||
        (form != MINIC_X86_BIT_SCAN_BSF64 && form != MINIC_X86_BIT_SCAN_BSR64))
        return false;
    *is_reverse = form == MINIC_X86_BIT_SCAN_BSR64;
    return true;
}

/* The GNU =r + matched-digit family owns only explicit register operands.
 * No hidden target machine registers, jumps, labels, function calls or
 * directives. Every template instruction is made of a conservative x86
 * scalar carry/move mnemonic and references only %0..%{outputs-1}. */
/* An explicitly bounded grammar for the BusyBox/TLS P-256 register +
 * pointer-memory inline ASM. No source bytes may reach GNU AS except:
 *   %N, (%N), K*8(%N) for K=1..3, $0 and $1.
 * Core/machine independently verify operand roles and physical registers.
 * We retain the already-supported pointer forms; rejecting them as
 * register-only would cause a regression, and ignoring unchecked suffixes
 * permitted an injected second instruction. */
static bool x86_matched_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}
static bool x86_matched_ref(const char *text, size_t len, size_t *cursor,
                            size_t outputs, size_t *references) {
    size_t i = *cursor;
    if (i >= len || text[i] != '%' || i + 1U >= len ||
        text[i + 1U] < '0' ||
        (size_t)(text[i + 1U] - '0') >= outputs)
        return false;
    *cursor = i + 2U;
    ++*references;
    return true;
}
static bool x86_matched_operand(const char *text, size_t len, size_t *cursor,
                                size_t outputs, size_t *refs) {
    size_t i = *cursor;
    if (i >= len) return false;
    if (text[i] == '$') {
        ++i;
        if (i >= len || (text[i] != '0' && text[i] != '1'))
            return false;
        ++i;
    } else {
        bool memory = false;
        if (i + 4U < len &&
            text[i] >= '1' && text[i] <= '3' &&
            text[i + 1U] == '*' && text[i + 2U] == '8' &&
            text[i + 3U] == '(') {
            i += 4U;
            memory = true;
        } else if (text[i] == '(') {
            ++i;
            memory = true;
        }
        if (!x86_matched_ref(text, len, &i, outputs, refs))
            return false;
        if (memory) {
            if (i >= len || text[i] != ')')
                return false;
            ++i;
        }
    }
    *cursor = i;
    return true;
}
bool minic_x86_64_asm_matched_template(const char *text,
                                        size_t length, size_t outputs) {
    static const char *const binary_ops[] = {
        "movq", "addq", "adcq", "subq", "sbbq"
    };
    size_t i = 0U, lines = 0U, refs = 0U, k;
    if (text == NULL || length == 0U || length > 1024U ||
        outputs == 0U || outputs > 4U)
        return false;
    while (i < length) {
        bool matched = false;
        while (i < length &&
               (x86_matched_space(text[i]) || text[i] == '\n' ||
                text[i] == ';'))
            ++i;
        if (i == length) break;
        if (i + 3U <= length && memcmp(text + i, "cmc", 3U) == 0) {
            i += 3U;
        } else {
            for (k = 0U; k < sizeof(binary_ops) / sizeof(binary_ops[0]); ++k) {
                if (i + 4U <= length &&
                    memcmp(text + i, binary_ops[k], 4U) == 0) {
                    matched = true;
                    i += 4U;
                    break;
                }
            }
            if (!matched || i >= length || !x86_matched_space(text[i]))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (i == length || text[i] != ',') return false;
            ++i;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
        }
        while (i < length && x86_matched_space(text[i])) ++i;
        if (i < length && text[i] != '\n' && text[i] != ';')
            return false;
        ++lines;
        if (i < length) ++i;
    }
    return lines != 0U && refs != 0U;
}

/* Qualifiers are stripped once by caller. This is target-owned C
 * operand/ISA width matching, not a second generic Core type system. */
static bool x86_bit_scan_operand_type(
    MinicX86BitScanForm form, size_t operand_index, MinicType type) {
    if (form == MINIC_X86_BIT_SCAN_BSF64 ||
        form == MINIC_X86_BIT_SCAN_BSR64)
        return minic_type_equal(type, minic_type_unsigned_long());
    if (operand_index == 0U || operand_index == 2U)
        return minic_type_equal(type, minic_type_int());
    if (form == MINIC_X86_BIT_SCAN_BSR64_READWRITE)
        return minic_type_equal(type, minic_type_unsigned_long()) ||
               minic_type_equal(type, minic_type_unsigned_long_long());
    return minic_type_equal(type, minic_type_int()) ||
           minic_type_equal(type, minic_type_unsigned_int());
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc, bool *is_muldiv) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    bool generic = false;
    bool bit_scan = false;
    MinicX86BitScanForm bit_form = MINIC_X86_BIT_SCAN_NONE;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc || !is_muldiv) return false;
    *is_mulacc = false;
    *is_muldiv = false;
    if (src->output_count == 4U && src->input_count == 4U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->template_length == 5U &&
        memcmp(src->template_text, "cpuid", 5U) == 0) {
        *is_mulacc = false;
        n = 4U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], cpuid_out[i]) ||
                !c_is(&src->inputs[i], cpuid_in[i])) return false;
    } else if (src->is_volatile && src->output_count == 3U &&
               src->input_count == 5U && src->clobber_count == 2U &&
               src->register_clobber_count == 2U &&
               !src->has_memory_clobber && src->register_clobbers &&
               ((clobber_is(&src->register_clobbers[0], "cc") &&
                 clobber_is(&src->register_clobbers[1], "dx")) ||
                (clobber_is(&src->register_clobbers[0], "dx") &&
                 clobber_is(&src->register_clobbers[1], "cc"))) &&
               minic_x86_64_asm_mulacc_template(src->template_text, src->template_length)) {
        *is_mulacc = true;
        n = 3U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=rm") ||
                !c_is(&src->inputs[i], mul_in[i])) return false;
        if (!c_is(&src->inputs[3], "a") ||
            !c_is(&src->inputs[4], "m")) return false;
    } else if (src->output_count == 1U && src->input_count == 3U &&
               src->clobber_count == 1U && src->register_clobber_count == 1U &&
               !src->has_memory_clobber && src->register_clobbers &&
               clobber_is(&src->register_clobbers[0], "rdx") &&
               minic_x86_64_asm_muldiv_template(src->template_text, src->template_length) &&
               c_is(&src->outputs[0], "=a") &&
               c_is(&src->inputs[0], "a") &&
               c_is(&src->inputs[1], "rm") &&
               c_is(&src->inputs[2], "rm")) {
        *is_muldiv = true;
    } else if (src->output_count == 1U &&
               !src->is_goto && src->label_count == 0U &&
               !src->has_memory_clobber &&
               src->clobber_count == 0U && src->register_clobber_count == 0U &&
               (bit_form = minic_x86_64_asm_bit_scan_form(
                   src->template_text, src->template_length)) !=
                   MINIC_X86_BIT_SCAN_NONE &&
               (c_is(&src->inputs[0], "r") || c_is(&src->inputs[0], "rm")) &&
               (((bit_form == MINIC_X86_BIT_SCAN_BSF64 ||
                  bit_form == MINIC_X86_BIT_SCAN_BSR64) &&
                 src->input_count == 1U &&
                 c_is(&src->outputs[0], "=r")) ||
                ((bit_form == MINIC_X86_BIT_SCAN_BSF32_TIED ||
                  bit_form == MINIC_X86_BIT_SCAN_BSR32_TIED) &&
                 src->input_count == 2U &&
                 c_is(&src->outputs[0], "=r") &&
                 c_is(&src->inputs[1], "0")) ||
                (bit_form == MINIC_X86_BIT_SCAN_BSR64_READWRITE &&
                 src->input_count == 1U &&
                 c_is(&src->outputs[0], "+r")))) {
        bit_scan = true;
    } else if (src->is_volatile && src->has_memory_clobber &&
               src->clobber_count == 1U &&
               src->register_clobber_count == 0U &&
               ((src->output_count == 4U && src->input_count == 3U) ||
                (src->output_count == 3U && src->input_count == 2U) ||
                (src->output_count == 1U && src->input_count == 1U)) &&
               minic_x86_64_asm_matched_template(src->template_text, src->template_length, src->output_count)) {
        *is_mulacc = false;
        generic = true;
        n = src->output_count;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=r")) return false;
        for (i = 0U; i < src->input_count; ++i) {
            const MinicInlineAsmOperand *op = &src->inputs[i];
            if (!op->constraint_text || op->constraint_length != 1U ||
                op->constraint_text[0] != (char)('0' + i))
                return false;
        }
    } else return false;

    for (i = 0U; i < src->output_count + src->input_count; ++i) {
        const bool is_output = i < src->output_count;
        const MinicInlineAsmOperand *op = is_output
            ? &src->outputs[i] : &src->inputs[i-src->output_count];
        const MinicExpression *expr =
            minic_c0_program_expression(program, op->expression);
        const MinicType target_type =
            (*is_mulacc || bit_scan)
                ? minic_type_unsigned_long() : minic_type_unsigned_int();
        if (!expr || !minic_type_unqualified(expr->type, &normalized) ||
            (bit_scan ? !x86_bit_scan_operand_type(bit_form, i, normalized)
                      : (*is_muldiv
                           ? (!minic_type_equal(normalized, minic_type_unsigned_long()) &&
                              !minic_type_equal(normalized, minic_type_unsigned_long_long()))
                           : generic
                               ? (!minic_type_is_integer(normalized) &&
                                  !minic_type_is_pointer(normalized))
                               : !minic_type_equal(normalized, target_type))) ||
            (is_output && (expr->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(expr->type))) ||
            (*is_mulacc && i == 7U &&
             expr->value_category != MINIC_VALUE_LVALUE))
            return false;
    }
    return true;
}

bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    bool is_mulacc = false, is_muldiv = false;
    size_t i, count;
    if (!contract) return false;
    if (x86_trycas_source(program, source, contract))
        return true;
    if (x86_linux_bitops_source(program, source, contract))
        return true;
    if (x86_linux_atomic_cc_source(program, source)) {
        const MinicX86AtomicCCDesc *d = minic_x86_64_atomic_cc_description(
            minic_x86_64_atomic_cc_form(
                source->template_text, source->template_length));
        contract->operand_count = d && d->has_value ? 3U : 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (contract->operand_count == 3U)
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (x86_nospec_source(program, source)) {
        contract->operand_count = 3U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (x86_percpu_source(program, source)) {
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (!qualified(program, source, &is_mulacc, &is_muldiv)) return false;
    (void)is_muldiv; /* all four mul/div operand roles are scalar/register */
    count = source->output_count + source->input_count;
    if (count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) return false;
    contract->operand_count = count;
    for (i = 0U; i < count; ++i)
        contract->kinds[i] = i < source->output_count
            ? (minic_x86_64_asm_bit_scan_form(
                   source->template_text, source->template_length) ==
               MINIC_X86_BIT_SCAN_BSR64_READWRITE ?
                   MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                   MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT)
            : is_mulacc && i == 7U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
