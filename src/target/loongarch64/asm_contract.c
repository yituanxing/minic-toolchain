#include "target/loongarch64/asm_contract.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/loongarch64/frame_emit.h"

#include <stdio.h>
#include <string.h>
/* Exact LoongArch64 kernel hardware zero-operand source forms.
 * ISA semantics, unlike compiler barriers, remain target-local. */
bool minic_loongarch64_qualified_opaque_asm(const MinicCoreInlineAsm *a) {
    static const char *const allowed[] = {
        "idle0", "tlbrd", "tlbclr", "tlbflush", "tlbsrch",
        "tlbwr", "tlbfill", "dbar0", "dbar0x700", "nop"
    };
    char compact[64];
    size_t i, n = 0U;
    if (minic_multi_direct_linux_bug_asm(
            a, "10000", "10001", "10002", "break1", false))
        return true;
    if (a == NULL || a->template_text == NULL || !a->is_volatile ||
        a->template_length == 0U || a->template_length >= sizeof(compact) ||
        a->is_goto || a->register_clobber_count != 0U)
        return false;
    for (i = 0U; i < a->template_length; ++i) {
        const char c = a->template_text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0')
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(compact, allowed[i]) == 0) return true;
    return false;
}

/* Native ISA reservation; rejected CSR/AMO shapes cannot bypass through
 * generic scalar Core inline assembly. */
bool minic_loongarch64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const reserved[] = {
        "dbar", "amand_db.w", "amor_db.w", "amxor_db.w", "amadd_db.w",
        "amand_db.d", "amor_db.d", "amxor_db.d", "amadd_db.d",
        "csrrd", "csrxchg", "1:ll.w", "1:ll.d", "sltu",
        "movgr2fcsr", "ldx.", "stx.", "amadd.", "amand.", "amor.",
        "amswap_db.w", "amswap_db.d"
    };
    return minic_backend_inline_asm_prefix_matches(
        src, reserved, sizeof(reserved) / sizeof(reserved[0]));
}

/* A legal LoongArch DBAR barrier is a hardware ordering operation, NOT a
 * compiler-only barrier. The original GNU I-constraint accepts a constant
 * unsigned 12-bit hint; hardware semantics are encoded by that hint.
 * Only one exact operand/template family is admitted, never arbitrary GNU
 * instruction-string passthrough. */
static bool loongarch64_dbar_hint(int64_t hint) {
    return hint >= 0 && hint <= 2047;
}
static bool loongarch64_dbar_template(const char *text, size_t length) {
    static const char prefix[] = "dbar %0";
    size_t i, n;
    if (text == NULL || length < sizeof(prefix) - 1U)
        return false;
    n = sizeof(prefix) - 1U;
    if (memcmp(text, prefix, n) != 0) return false;
    for (i = n; i < length; ++i) {
        if (text[i] != ' ' && text[i] != '\t')
            return false;
    }
    return true;
}


/* Original GNU no-operand DBAR is a literal hardware barrier. This is
 * independent from the existing "dbar %0" I-constraint immediate input.
 * Avoid treating arbitrary templates or unknown DBAR hints as executable. */
static const char *loongarch64_literal_dbar(const char *text,size_t length){
    static const char *const allowed[]={"dbar 0","dbar 0x700"};
    char norm[48];size_t i,n=0U;
    if(!text||!length||length>=sizeof(norm))return NULL;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==9||c==10||c==13||c==';')return NULL;
        if(n+1U>=sizeof(norm))return NULL;
        norm[n++]=c;
    }
    while(n!=0U&&norm[n-1U]==' ')--n;
    norm[n]=0;
    for(i=0U;i<sizeof(allowed)/sizeof(allowed[0]);++i)
        if(strcmp(norm,allowed[i])==0)return allowed[i];
    return NULL;
}

/* Typed GNU LoongArch AMO store-only operations from Linux atomic.h.
 * No target-neutral Core opcode is added: the existing structured ASM roles
 * carry the memory address and scalar input, target-owned machine code
 * determines atomicity/ordering. This exact family cannot be dispatched
 * by the generic text-substitution ASM path. */
static const struct MinicLoongAtomicTemplate {
    const char *text;
    const char *instruction;
    size_t width;
} loong_atomic[] = {
    {"amand_db.w$zero,%1,%0", "amand_db.w", 4U},
    {"amor_db.w$zero,%1,%0", "amor_db.w", 4U},
    {"amxor_db.w$zero,%1,%0", "amxor_db.w", 4U},
    {"amadd_db.w$zero,%1,%0", "amadd_db.w", 4U},
    {"amand_db.d$zero,%1,%0", "amand_db.d", 8U},
    {"amor_db.d$zero,%1,%0", "amor_db.d", 8U},
    {"amxor_db.d$zero,%1,%0", "amxor_db.d", 8U},
    {"amadd_db.d$zero,%1,%0", "amadd_db.d", 8U}
};
static const struct MinicLoongAtomicTemplate *loong_atomic_template(
    const char *text, size_t length, bool fetch_old_value) {
    char compact[96];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (used + 1U >= sizeof(compact)) return NULL;
        compact[used++] = c;
    }
    compact[used] = '\0';
    for (i = 0U; i < sizeof(loong_atomic) / sizeof(loong_atomic[0]); ++i) {
        char expected[64];
        if (fetch_old_value) {
            const int count = snprintf(expected, sizeof(expected),
                                       "%s%%1,%%2,%%0", loong_atomic[i].instruction);
            if (count <= 0 || (size_t)count >= sizeof(expected)) return NULL;
            if (strcmp(compact, expected) == 0) return &loong_atomic[i];
        } else if (strcmp(compact, loong_atomic[i].text) == 0) {
            return &loong_atomic[i];
        }
    }
    return NULL;
}
static bool loong_atomic_scalar_type(MinicType type, size_t width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain)) return false;
    /* Original Linux atomic64_t.counter is __s64, i.e. signed LONG LONG,
     * even under LoongArch LP64D where long and long long have equal
     * register/storage width but distinct C type identities. Both are
     * single 64-bit integer GPR operands; never admit FP or int128. */
    return width == 8U
        ? (minic_type_equal(plain, minic_type_long()) ||
           minic_type_equal(plain, minic_type_long_long()))
        : minic_type_equal(plain, minic_type_int());
}
static bool loong_atomic_qualified_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicExpression *dst, *arg;
    if (src == NULL || program == NULL || contract == NULL ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 1U ||
        src->outputs == NULL || src->inputs == NULL ||
        src->label_count != 0U || src->register_clobber_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U)
        return false;
    atom = loong_atomic_template(src->template_text, src->template_length, false);
    if (atom == NULL || src->outputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
        src->inputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        src->outputs[0].constraint_length != 3U ||
        src->outputs[0].constraint_text == NULL ||
        memcmp(src->outputs[0].constraint_text, "+ZB", 3U) != 0 ||
        src->inputs[0].constraint_length != 1U ||
        src->inputs[0].constraint_text == NULL ||
        src->inputs[0].constraint_text[0] != 'r')
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    arg = minic_c0_program_expression(program, src->inputs[0].expression);
    if (dst == NULL || arg == NULL ||
        dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !loong_atomic_scalar_type(dst->type, atom->width) ||
        !loong_atomic_scalar_type(arg->type, atom->width))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Linux irqflags.h uses only CSR_CRMD (CSR index zero): a true EL1
 * privileged operation, never an ordinary C load, barrier or passthrough.
 * Match exact GNU named operands and normalized numeric Core templates.
 * The two forms share the existing Core address/scalar constant roles. */
static unsigned int loong_csr_template(const char *text, size_t length) {
    static const char *const forms[] = {
        "csrrd%[val],%[reg]", "csrrd%0,%1",
        "csrxchg%[val],%[mask],%[reg]", "csrxchg%0,%1,%2"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        char c = text[i];
        if (c == ';') return 0U;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return i < 2U ? 1U : 2U;
    return 0U;
}
static bool loong_csr_word_type(MinicType type) {
    MinicType plain;
    return minic_type_unqualified(type, &plain) &&
           minic_type_equal(plain, minic_type_unsigned_int());
}
static bool loong_csr_output_type(MinicType type, size_t *width) {
    MinicType plain;
    if (!width || !minic_type_unqualified(type, &plain)) return false;
    if (minic_type_equal(plain, minic_type_unsigned_int())) {
        *width = 4U; return true;
    }
    if (minic_type_equal(plain, minic_type_unsigned_long())) {
        *width = 8U; return true;
    }
    return false;
}
static bool loong_csr_constraint(const MinicInlineAsmOperand *op, const char *word) {
    const size_t n = strlen(word);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, word, n) == 0;
}
static bool loong_csr_immediate_zero(const MinicC0Program *program,
                                      const MinicInlineAsmOperand *op) {
    const MinicExpression *e;
    if (!op || !loong_csr_constraint(op, "i")) return false;
    e = minic_c0_program_expression(program, op->expression);
    return e && e->kind == MINIC_EXPRESSION_INTEGER &&
           e->value.integer_value == 0;
}
static bool loong_csr_qualified_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    unsigned int form;
    const MinicExpression *dst, *mask;
    size_t width = 0U;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !src->outputs || !src->inputs ||
        src->output_count != 1U || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    form = loong_csr_template(src->template_text, src->template_length);
    if (!form || src->input_count != (form == 1U ? 1U : 2U) ||
        !loong_csr_constraint(&src->outputs[0], form == 1U ? "=r" : "+r") ||
        !loong_csr_immediate_zero(program, &src->inputs[form == 1U ? 0U : 1U]))
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!dst || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !loong_csr_output_type(dst->type, &width) ||
        (form == 1U && width != 4U)) return false;
    if (form == 2U) {
        if (!loong_csr_constraint(&src->inputs[0], "r")) return false;
        mask = minic_c0_program_expression(program, src->inputs[0].expression);
        if (!mask || !loong_csr_word_type(mask->type)) return false;
    }
    contract->operand_count = form == 1U ? 2U : 3U;
    contract->kinds[0] = form == 1U ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    if (form == 2U)
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[contract->operand_count - 1U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Authentic GNU Linux 6.6.143 LoongArch set_pte global-bit buddy LL/SC.
 * One exact source/machine ISA template; no Core architecture switches. */
static bool loong_pte_form(const char *text,size_t len) {
    static const char named[]=
        "1:ll.d%[tmp],%[buddy]bnez%[tmp],2for%[tmp],%[tmp],%[global]"
        "sc.d%[tmp],%[buddy]beqz%[tmp],1bnop2:dbar0x700";
    static const char numeric[]=
        "1:ll.d%1,%0bnez%1,2for%1,%1,%2"
        "sc.d%1,%0beqz%1,1bnop2:dbar0x700";
    char compact[240];
    size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return false;
    for(i=0U;i<len;++i){
        char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,named)==0||strcmp(compact,numeric)==0;
}
static bool loong_pte_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *e[3];
    MinicType t[3];
    size_t i;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !src->outputs||!src->inputs||src->output_count!=2U||
       src->input_count!=1U||src->label_count||src->clobber_count||
       src->register_clobber_count||src->has_memory_clobber||
       !loong_pte_form(src->template_text,src->template_length)||
       !loong_csr_constraint(&src->outputs[0],"+m")||
       !loong_csr_constraint(&src->outputs[1],"=&r")||
       !loong_csr_constraint(&src->inputs[0],"r"))
        return false;
    for(i=0U;i<3U;++i){
        const MinicInlineAsmOperand *o=i<2U?
            &src->outputs[i]:&src->inputs[0];
        e[i]=minic_c0_program_expression(program,o->expression);
        if(!e[i]||(i<2U&&(e[i]->value_category!=MINIC_VALUE_LVALUE||
           minic_type_is_const(e[i]->type)))||
           !minic_type_unqualified(e[i]->type,&t[i])||
           !minic_type_equal(t[i],minic_type_unsigned_long()))
            return false;
    }
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
/* Linux arch_atomic{,64}_fetch_{and,or,xor,add}_relaxed:
 * GNU +ZB memory RMW, =&r old-value output, r input. The SAME hardware
 * AMO template table owns both store-only ($zero) and fetch old-value forms.
 * Core already transports 3 typed roles, so no new Core instruction. */
static bool loong_atomic_fetch_qualified_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicExpression *memory, *output, *arg;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !src->outputs || !src->inputs || src->output_count != 2U ||
        src->input_count != 1U || src->label_count != 0U ||
        src->clobber_count != 1U || src->register_clobber_count != 0U ||
        !src->has_memory_clobber ||
        src->outputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
        src->outputs[1].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        src->inputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        !src->outputs[0].constraint_text ||
        src->outputs[0].constraint_length != 3U ||
        memcmp(src->outputs[0].constraint_text, "+ZB", 3U) != 0 ||
        !src->outputs[1].constraint_text ||
        src->outputs[1].constraint_length != 3U ||
        memcmp(src->outputs[1].constraint_text, "=&r", 3U) != 0 ||
        !src->inputs[0].constraint_text ||
        src->inputs[0].constraint_length != 1U ||
        src->inputs[0].constraint_text[0] != 'r') return false;
    atom = loong_atomic_template(src->template_text, src->template_length, true);
    if (!atom) return false;
    memory = minic_c0_program_expression(program, src->outputs[0].expression);
    output = minic_c0_program_expression(program, src->outputs[1].expression);
    arg = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!memory || !output || !arg ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(memory->type) || minic_type_is_const(output->type) ||
        !loong_atomic_scalar_type(memory->type, atom->width) ||
        !loong_atomic_scalar_type(output->type, atom->width) ||
        !loong_atomic_scalar_type(arg->type, atom->width)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* GNU Linux 6.6.143 cmpxchg.h: exact LL/SC hardware shapes. Keep
 * target-only constraint/type qualification separate from Common Core
 * operand address lowering; no C load/store emulation of atomics. */
static unsigned loong_linux_cmpxchg_form(const char *text,size_t length) {
    static const char *const exact[]={
        "1:ll.w%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.w$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.d%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.d$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.w%0,%3and%1,%0,%z4bne%1,%z5,2fandn%1,%0,%z4or%1,%1,%z6sc.w%1,%2beqz%1,1bb3f2:dbar0x7003:"
    };
    char compact[256];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<3U;++i)if(strcmp(compact,exact[i])==0)return (unsigned)i+1U;
    return 0U;
}
static bool loong_linux_cmpxchg_word(MinicType type,size_t width) {
    MinicType plain;
    if(!minic_type_unqualified(type,&plain)||
       !minic_type_is_integer(plain)||plain.pointer_depth||
       plain.base_kind!=MINIC_TYPE_BASE_INT)return false;
    return width==4U?plain.integer_rank==MINIC_INTEGER_RANK_INT:
        (plain.integer_rank==MINIC_INTEGER_RANK_LONG||
         plain.integer_rank==MINIC_INTEGER_RANK_LONG_LONG);
}
static bool loong_linux_cmpxchg_source(
    const MinicC0Program *p,const MinicInlineAsm *s,
    MinicTargetInlineAsmContract *contract){
    const unsigned form=s?loong_linux_cmpxchg_form(
        s->template_text,s->template_length):0U;
    const size_t width=form==2U?8U:4U;
    const size_t outputs=form==3U?3U:2U,inputs=form==3U?4U:3U;
    size_t i;
    if(!p||!s||!contract||!form||!s->is_volatile||s->is_goto||
       s->label_count||!s->outputs||!s->inputs||
       s->output_count!=outputs||s->input_count!=inputs||
       !s->has_memory_clobber||s->clobber_count!=(form==3U?1U:2U)||
       s->register_clobber_count!=(form==3U?0U:1U))return false;
    if(form!=3U&&(!s->register_clobbers||
       s->register_clobbers[0].name_length!=2U||
       memcmp(s->register_clobbers[0].name,"t0",2U)))return false;
    for(i=0U;i<outputs+inputs;++i){
        const bool output=i<outputs;
        const MinicInlineAsmOperand *o=output?
            &s->outputs[i]:&s->inputs[i-outputs];
        const MinicExpression *e=minic_c0_program_expression(p,o->expression);
        const bool memory=form==3U?(i==2U||i==3U):(i==1U||i==2U);
        const char *constraint;
        size_t length;
        if(form==3U){
            static const char *const out[]={"=&r","=&r","=ZC"};
            static const char *const in[]={"ZC","Jr","Jr","Jr"};
            constraint=output?out[i]:in[i-outputs];
        }else constraint=output?(i==0U?"=&r":"=ZB"):
            (i==2U?"ZB":"Jr");
        length=strlen(constraint);
        if(!o->constraint_text||o->constraint_length!=length||
           memcmp(o->constraint_text,constraint,length)||!e||
           (output&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type)))||
           (memory&&e->value_category!=MINIC_VALUE_LVALUE)||
           /* GNU Linux __cmpxchg(size=4) supplies unsigned long "new"
            * with %z4, i.e. low 32 GPR bits. This is a width-qualified
            * input, not an int-to-long ABI mismatch. */
           (!loong_linux_cmpxchg_word(e->type,width) &&
            !(form==1U && i==4U &&
              loong_linux_cmpxchg_word(e->type,8U))))return false;
        contract->kinds[i]=memory?
            (output?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
            (output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
    }
    contract->operand_count=outputs+inputs;
    return true;
}


/* Original GNU Linux 6.6.143 __xchg() amswap_db.w/d returns the old
 * memory value in a GPR and writes a replacement atomically with a
 * hardware DB fence. Common Core carries the same typed structured
 * memory/read-write, register-output and scalar-input operands. */
static unsigned loong_linux_xchg_form(const char *text,size_t length){
    static const char *const forms[]={
        /* arch/loongarch/include/asm/cmpxchg.h __xchg_asm */
        "amswap_db.w%1,%z2,%0","amswap_db.d%1,%z2,%0",
        /* arch/loongarch/include/asm/barrier.h __smp_store_mb.
         * The GNU named form has an unsigned-long scratch and NO explicit
         * clobber; Core supplies the implicit +ZB memory effect. */
        "amswap_db.w%[tmp],%[val],%[mem]",
        "amswap_db.d%[tmp],%[val],%[mem]",
        /* Checked numeric representation after Core name resolution. */
        "amswap_db.w%1,%2,%0","amswap_db.d%1,%2,%0",
        /* Linux relaxed xchg is an atomic swap without the DB fence. */
        "amswap.w%1,%z2,%0","amswap.d%1,%z2,%0",
        /* The local atomic fetch operations have the SAME Core operand
         * roles as xchg: +ZB memory, old GPR output, scalar input. Only
         * the hardware AMO operation differs; never emulate a RMW in C. */
        "amadd.w%1,%2,%0","amadd.d%1,%2,%0",
        "amand.w%1,%2,%0","amand.d%1,%2,%0",
        "amor.w%1,%2,%0","amor.d%1,%2,%0",
        "amxor.w%1,%2,%0","amxor.d%1,%2,%0"
    };
    char compact[96];size_t n=0U,i;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i)
        if(strcmp(compact,forms[i])==0)return (unsigned)i+1U;
    return 0U;
}
static bool loong_linux_xchg_source(const MinicC0Program *p,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *c){
    const unsigned form=s?loong_linux_xchg_form(
        s->template_text,s->template_length):0U;
    const size_t width=form&&form%2U==0U?8U:4U;
    const bool named=form>=3U&&form<=6U;
    const MinicExpression *mem,*res,*val;
    if(!p||!s||!c||!form||!s->is_volatile||s->is_goto||
       s->output_count!=2U||s->input_count!=1U||!s->outputs||!s->inputs||
       s->register_clobber_count||s->label_count||
       s->clobber_count!=(named?0U:1U)||
       s->has_memory_clobber==named||
       !loong_csr_constraint(&s->outputs[0],"+ZB")||
       !loong_csr_constraint(&s->outputs[1],"=&r")||
       !loong_csr_constraint(&s->inputs[0],named||form>=9U?"r":"Jr"))return false;
    mem=minic_c0_program_expression(p,s->outputs[0].expression);
    res=minic_c0_program_expression(p,s->outputs[1].expression);
    val=minic_c0_program_expression(p,s->inputs[0].expression);
    if(!mem||!res||!val||
       mem->value_category!=MINIC_VALUE_LVALUE||
       res->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(mem->type)||minic_type_is_const(res->type)||
       !loong_linux_cmpxchg_word(mem->type,width)||
       !loong_linux_cmpxchg_word(res->type,named?8U:width)||
       !loong_linux_cmpxchg_word(val->type,width))return false;
    c->operand_count=3U;
    c->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    c->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    c->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Linux __xchg_small() does NOT use AMSWAP_DB: byte/halfword exchange
 * reserves the naturally aligned enclosing u32 and masks just the selected
 * subword. Match its exact GNU LL/SC template and three independent early
 * clobber / memory outputs. A change in the mask, branch, or LL/SC status
 * choreography is a different program and must be rejected. */
static bool loong_linux_xchg_small_form(const char *text,size_t length){
    static const char exact[]=
        "1:ll.w%0,%3andn%1,%0,%z4or%1,%1,%z5sc.w%1,%2beqz%1,1b";
    char compact[160];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return false;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||ch==';'||n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,exact)==0;
}
static bool loong_linux_xchg_small_source(const MinicC0Program *p,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *c){
    static const char *const outputs[]={"=&r","=&r","=ZC"};
    static const char *const inputs[]={"ZC","Jr","Jr"};
    size_t i;
    if(!p||!s||!c||!loong_linux_xchg_small_form(
           s->template_text,s->template_length)||!s->is_volatile||
       s->is_goto||s->label_count||s->output_count!=3U||
       s->input_count!=3U||!s->outputs||!s->inputs||
       s->clobber_count!=1U||!s->has_memory_clobber||
       s->register_clobber_count)return false;
    for(i=0U;i<6U;++i){
        const bool output=i<3U;
        const MinicInlineAsmOperand *op=output?
            &s->outputs[i]:&s->inputs[i-3U];
        const char *expected=output?outputs[i]:inputs[i-3U];
        const MinicExpression *e=minic_c0_program_expression(p,op->expression);
        if(!loong_csr_constraint(op,expected)||!e||
           (output&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type)))||
           ((i==2U||i==3U)&&e->value_category!=MINIC_VALUE_LVALUE)||
           !loong_linux_cmpxchg_word(e->type,4U))return false;
        c->kinds[i]=i==2U||i==3U?
            (output?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
            (output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
    }
    c->operand_count=6U;
    return true;
}

/* Linux 6.6.143 arch/loongarch/include/asm/barrier.h:
 * array_index_mask_nospec has exactly these two physical instructions.
 * GCC GNU constraints =r, r, r are transported as Core operand roles.
 * The compare computes the unsigned flag; sub.d produces the 0/~0 mask.
 * No invented portable instruction or open-ended inline ASM passthrough. */
static bool loong_mask_nospec_form(const char *text, size_t length) {
    static const char exact[] = "sltu%0,%1,%2sub.d%0,$zero,%0";
    char compact[128];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == ';' || c == '\0' || used + 1U >= sizeof(compact))
            return false;
        compact[used++] = c;
    }
    compact[used] = '\0';
    return strcmp(compact, exact) == 0;
}

static bool loong_mask_nospec_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    MinicType plain;
    size_t i;
    if (program == NULL || src == NULL || contract == NULL ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 2U ||
        src->outputs == NULL || src->inputs == NULL ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber || src->label_count != 0U ||
        !loong_mask_nospec_form(src->template_text, src->template_length) ||
        !loong_csr_constraint(&src->outputs[0], "=r") ||
        !loong_csr_constraint(&src->inputs[0], "r") ||
        !loong_csr_constraint(&src->inputs[1], "r"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *o = i == 0U
            ? &src->outputs[0] : &src->inputs[i - 1U];
        const MinicExpression *e =
            minic_c0_program_expression(program, o->expression);
        if (e == NULL ||
            (i == 0U &&
             (e->value_category != MINIC_VALUE_LVALUE ||
              minic_type_is_const(e->type))) ||
            !minic_type_unqualified(e->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool loong_mask_nospec_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFunction *fn = ctx->fn;
    MinicType plain;
    size_t i;
    if (!info->is_volatile || info->is_goto ||
        info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !loong_mask_nospec_form(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        MinicType type;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(type, &type))
            return false;
        if (!minic_type_unqualified(type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()))
            return false;
    }
    /* Frame helper owns large/static/dynamic offsets; $t3 is reserved for
     * address materialization. No temporary overlap with $t0/$t1/$t2. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t0", (size_t)b[1].value * 8U,
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", (size_t)b[2].value * 8U,
               ctx->dynamic_frame) &&
           fputs("  sltu $t2, $t0, $t1\n"
                 "  sub.d $t2, $zero, $t2\n", out) != EOF &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", (size_t)b[0].value * 8U,
               ctx->dynamic_frame) &&
           fputs("  st.d $t2, $t1, 0\n", out) != EOF;
}

/* Original 6.6.143 LoongArch generic per-CPU and FPU-state hardware ASM.
 * All forms are exact qualified target contracts, NEVER a raw text escape.
 * The Core representation remains typed output-lvalue and scalar inputs.
 * $r21 is the kernel per-CPU base register, not a generic C pointer. */
typedef enum MinicLoongPerCpuIndexedForm {
    LOONG_PERCPU_NONE = 0, LOONG_PERCPU_READ_B, LOONG_PERCPU_READ_H,
    LOONG_PERCPU_READ_W, LOONG_PERCPU_READ_D,
    LOONG_PERCPU_WRITE_B, LOONG_PERCPU_WRITE_H,
    LOONG_PERCPU_WRITE_W, LOONG_PERCPU_WRITE_D
} MinicLoongPerCpuIndexedForm;
static MinicLoongPerCpuIndexedForm loong_percpu_indexed_form(
    const char *text, size_t length) {
    static const char *const words[] = {
        "ldx.b%[ret],$r21,%[ptr]", "ldx.h%[ret],$r21,%[ptr]",
        "ldx.w%[ret],$r21,%[ptr]", "ldx.d%[ret],$r21,%[ptr]",
        "stx.b%[val],$r21,%[ptr]", "stx.h%[val],$r21,%[ptr]",
        "stx.w%[val],$r21,%[ptr]", "stx.d%[val],$r21,%[ptr]"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return LOONG_PERCPU_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return LOONG_PERCPU_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    /* Shared Core canonicalizes GNU named slots to %0/%1, but the
     * target must also qualify ORIGINAL named source templates. Both
     * forms name exactly the same two typed operand roles. */
    for (i = 0U; i < sizeof(words)/sizeof(words[0]); ++i) {
        char numeric[48];
        const bool read = i < 4U;
        const char *const name = i < 4U ?
            (i == 0U ? "ldx.b" : i == 1U ? "ldx.h" :
             i == 2U ? "ldx.w" : "ldx.d") :
            (i == 4U ? "stx.b" : i == 5U ? "stx.h" :
             i == 6U ? "stx.w" : "stx.d");
        if (strcmp(compact, words[i]) == 0) return (MinicLoongPerCpuIndexedForm)(i + 1U);
        if (snprintf(numeric,sizeof(numeric), "%s%%0,$r21,%%1",name) > 0 &&
            strcmp(compact,numeric) == 0)
            return (MinicLoongPerCpuIndexedForm)(i + 1U);
        (void)read;
    }
    return LOONG_PERCPU_NONE;
}
static bool loong_percpu_indexed_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicLoongPerCpuIndexedForm form = src ?
        loong_percpu_indexed_form(src->template_text, src->template_length)
        : LOONG_PERCPU_NONE;
    const bool read = form >= LOONG_PERCPU_READ_B && form <= LOONG_PERCPU_READ_D;
    const MinicExpression *value, *address;
    MinicType plain;
    if (!program || !src || !contract || form == LOONG_PERCPU_NONE ||
        !src->is_volatile || src->is_goto ||
        src->output_count != (read ? 1U : 0U) ||
        src->input_count != (read ? 1U : 2U) ||
        (read && (!src->outputs || !loong_csr_constraint(&src->outputs[0], "=&r"))) ||
        !src->inputs ||
        !loong_csr_constraint(&src->inputs[read ? 0U : 1U], "r") ||
        (!read && !loong_csr_constraint(&src->inputs[0], "r")) ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || src->label_count != 0U)
        return false;
    value = minic_c0_program_expression(program,
        read ? src->outputs[0].expression : src->inputs[0].expression);
    address = minic_c0_program_expression(program,
        src->inputs[read ? 0U : 1U].expression);
    if (!value || !address ||
        (read && (value->value_category != MINIC_VALUE_LVALUE ||
                  minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(address->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = read
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool loong_percpu_indexed_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicLoongPerCpuIndexedForm form =
        loong_percpu_indexed_form(info->template_text, info->template_length);
    const bool read = form >= LOONG_PERCPU_READ_B && form <= LOONG_PERCPU_READ_D;
    static const char *const mnemonics[] = {
        "ldx.b", "ldx.h", "ldx.w", "ldx.d",
        "stx.b", "stx.h", "stx.w", "stx.d"
    };
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType value_type;
    size_t i;
    if (!out || !ctx || !fn || !info || form == LOONG_PERCPU_NONE ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            (b[i].early_clobber && !(read && i == 0U)) ||
            b[i].has_fixed_register_binding ||
            b[i].kind != (read && i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    value_type = fn->values[b[0].value].type;
    if (read && !minic_type_pointee(value_type, &value_type))
        return false;
    if (!minic_type_equal(value_type, minic_type_unsigned_long()))
        return false;
    if (!minic_type_is_pointer(fn->values[b[1].value].type))
        return false;
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t1", (size_t)b[1].value*8U, ctx->dynamic_frame))
        return false;
    if (read)
        return fprintf(out, "  %s $t0, $r21, $t1\n",
                       mnemonics[(size_t)form-1U]) >= 0 &&
               minic_loongarch64_emit_frame_mem(
                   out, "ld.d", "$t1", (size_t)b[0].value*8U,
                   ctx->dynamic_frame) &&
               fputs("  st.d $t0, $t1, 0\n", out) != EOF;
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t0", (size_t)b[0].value*8U,ctx->dynamic_frame) &&
           fprintf(out, "  %s $t0, $r21, $t1\n",
                   mnemonics[(size_t)form-1U]) >= 0;
}

/* Exact GNU per-CPU am{add,and,or}.w/.d: the first output is OLD
 * memory, second +ZB output is updated memory, third argument is VAL.
 * Unlike _db forms this is intentionally non-barrier and GNU declares
 * no memory clobber. A spelling or operand-order mismatch must reject. */
static const struct { const char *compact, *mnemonic; size_t width; }
loong_percpu_amo[] = {
    {"amadd.w%[ret],%[val],%[ptr]", "amadd.w", 4U},
    {"amadd.d%[ret],%[val],%[ptr]", "amadd.d", 8U},
    {"amand.w%[ret],%[val],%[ptr]", "amand.w", 4U},
    {"amand.d%[ret],%[val],%[ptr]", "amand.d", 8U},
    {"amor.w%[ret],%[val],%[ptr]", "amor.w", 4U},
    {"amor.d%[ret],%[val],%[ptr]", "amor.d", 8U}
};
static size_t loong_percpu_amo_form(const char *text, size_t length) {
    char compact[96];
    size_t i,n=0U;
    if (!text || !length || length >= sizeof(compact)) return SIZE_MAX;
    for(i=0U;i<length;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(compact))return SIZE_MAX;
        compact[n++]=c;
    }
    compact[n]='\0';
    for (i=0U;i<sizeof(loong_percpu_amo)/sizeof(loong_percpu_amo[0]);++i) {
        char numeric[64];
        if(strcmp(compact,loong_percpu_amo[i].compact)==0)return i;
        if(snprintf(numeric,sizeof(numeric),"%s%%0,%%2,%%1",
                    loong_percpu_amo[i].mnemonic)>0 &&
           strcmp(compact,numeric)==0)return i;
    }
    return SIZE_MAX;
}
static bool loong_percpu_amo_memory_type(MinicType type,size_t width) {
    /* AMO operates on width-sized bit patterns; signedness does not affect
     * the hardware RMW, and Linux percpu/local atomics use signed counters
     * as well as unsigned storage. Keep the exact primitive-width rule. */
    return loong_linux_cmpxchg_word(type, width);
}
static bool loong_percpu_amo_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const size_t form=src?
        loong_percpu_amo_form(src->template_text,src->template_length):SIZE_MAX;
    const MinicExpression *old,*memory,*value;
    const MinicInlineAsmOperand *output;
    if (!program||!src||!contract||form==SIZE_MAX||
        !src->is_volatile||src->is_goto||!src->outputs||!src->inputs||
        src->output_count!=2U||src->input_count!=1U||
        src->has_memory_clobber||src->clobber_count!=0U||
        src->register_clobber_count!=0U||src->label_count!=0U||
        !loong_csr_constraint(&src->outputs[0],"=&r")||
        !loong_csr_constraint(&src->outputs[1],"+ZB")||
        !loong_csr_constraint(&src->inputs[0],"r"))
        return false;
    old=minic_c0_program_expression(program,src->outputs[0].expression);
    memory=minic_c0_program_expression(program,src->outputs[1].expression);
    value=minic_c0_program_expression(program,src->inputs[0].expression);
    output=&src->outputs[0];
    if (!old||!memory||!value||
        old->value_category!=MINIC_VALUE_LVALUE||
        memory->value_category!=MINIC_VALUE_LVALUE||
        minic_type_is_const(old->type)||minic_type_is_const(memory->type)||
        !loong_percpu_amo_memory_type(
            memory->type,loong_percpu_amo[form].width)||
        !loong_percpu_amo_memory_type(old->type,8U)||
        !loong_percpu_amo_memory_type(value->type,8U)||
        output->access!=MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        src->outputs[1].access!=MINIC_INLINE_ASM_OPERAND_READ_WRITE)
        return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool loong_percpu_amo_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const size_t form=loong_percpu_amo_form(
        info->template_text,info->template_length);
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType old_type,mem_type;
    size_t i;
    /* The ORIGINAL GNU source has NO compiler-wide memory clobber, only
     * the +ZB memory operand. Shared Core deliberately upgrades the ABI-
     * neutral structured effect to has_memory_clobber=true whenever any
     * operand is MEMORY_READWRITE. This is semantic memory-effect metadata,
     * not evidence that source added an extra clobber. Source admission
     * already rejected an explicit \"memory\" clobber or other mutations. */
    if(!out||!ctx||!fn||!info||form==SIZE_MAX||
       !info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count!=0U||
       ins->value.structured_inline_asm.operand_count!=3U)
        return false;
    for(i=0U;i<3U;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           (b[i].early_clobber && i!=0U)||
           b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                              MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if(!minic_type_pointee(fn->values[b[0].value].type,&old_type)||
       !minic_type_pointee(fn->values[b[1].value].type,&mem_type)||
       !loong_percpu_amo_memory_type(old_type,8U)||
       !loong_percpu_amo_memory_type(
           mem_type,loong_percpu_amo[form].width)||
       !loong_percpu_amo_memory_type(fn->values[b[2].value].type,8U))
        return false;
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",(size_t)b[1].value*8U,ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t0",(size_t)b[2].value*8U,ctx->dynamic_frame)&&
           fprintf(out,"  %s $t2, $t0, $t1\n",
                   loong_percpu_amo[form].mnemonic)>=0&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",(size_t)b[0].value*8U,ctx->dynamic_frame)&&
           fputs("  st.d $t2, $t1, 0\n",out)!=EOF;
}
static bool loong_fcsr0_write_form(const char *text,size_t len){
    static const char exact[]="movgr2fcsr$fcsr0,%0";
    char norm[64];size_t i,n=0U;
    if(!text||len==0U||len>=sizeof(norm))return false;
    for(i=0U;i<len;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(norm))return false;
        norm[n++]=c;
    }
    norm[n]='\0';
    return strcmp(norm,exact)==0;
}
static bool loong_fcsr0_write_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *e;
    MinicType plain;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !loong_fcsr0_write_form(src->template_text,src->template_length)||
       src->output_count!=0U||src->input_count!=1U||!src->inputs||
       src->has_memory_clobber||src->clobber_count!=0U||
       src->register_clobber_count!=0U||src->label_count!=0U||
       !loong_csr_constraint(&src->inputs[0],"r"))return false;
    e=minic_c0_program_expression(program,src->inputs[0].expression);
    if(!e||!minic_type_unqualified(e->type,&plain)||
       !minic_type_is_integer(plain)||
       !(minic_type_equal(plain,minic_type_int())||
         minic_type_equal(plain,minic_type_unsigned_int())))
        return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool loong_fcsr0_write_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType plain;
    if(!out||!ctx||!fn||!info||!info->is_volatile||info->is_goto||
       info->has_memory_clobber||info->register_clobber_count!=0U||
       !loong_fcsr0_write_form(info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=1U||
       b[0].operand_index!=0U||b[0].value>=fn->value_count||
       b[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       b[0].early_clobber||b[0].has_fixed_register_binding||
       !minic_type_unqualified(fn->values[b[0].value].type,&plain)||
       !(minic_type_equal(plain,minic_type_int())||
         minic_type_equal(plain,minic_type_unsigned_int())))
        return false;
    return minic_loongarch64_emit_frame_mem(
             out,"ld.w","$t0",(size_t)b[0].value*8U,ctx->dynamic_frame)&&
           fputs("  movgr2fcsr $fcsr0, $t0\n",out)!=EOF;
}

/* Linux LoongArch __get_user/__put_user has a hardware faulting load/store
 * coupled to an exception-table register fixup. The generated .equ/.irp
 * register map is part of the ABI, not optional decoration. Match its full
 * GNU source form and then emit with known, target-owned physical registers.
 * Unrecognized templates remain unsupported instead of being passed through
 * as an untracked opaque machine side effect. */
/* Architectural INVTLB has three GNU operand shapes: ASID+VA, VA-only,
 * and ASID-only. Both zero-register variants are real TLB operations, not
 * CPU fences. Keep the immediate opcode range and Linux-defined encodings
 * in this target instead of allowing an arbitrary textual instruction. */
static unsigned int loong_linux_invtlb_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "invtlb%0,%1,%2", "invtlb%0,$zero,%1",
        "invtlb%0,%1,$zero", "invtlb%0,$zero,$zero"
    };
    char compact[90];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool loong_linux_invtlb_opcode(int64_t value) {
    return (value >= 0 && value <= 6) ||
           (value >= 9 && value <= 14) ||
           (value >= 16 && value <= 22);
}

static bool loong_linux_invtlb_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? loong_linux_invtlb_form(
        src->template_text, src->template_length) : 0U;
    size_t i;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 0U ||
        src->input_count != (form == 1U ? 3U : form == 4U ? 1U : 2U) ||
        !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < src->input_count; ++i) {
        const MinicInlineAsmOperand *op = &src->inputs[i];
        const MinicExpression *expr = minic_c0_program_expression(
            program, op->expression);
        MinicType plain;
        if (!loong_csr_constraint(op, i == 0U ? "i" : "r") ||
            !expr || !minic_type_unqualified(expr->type, &plain) ||
            !minic_type_is_integer(plain) ||
            !loong_linux_cmpxchg_word(
                plain, i == 0U || (form == 1U && i == 1U) ||
                               (form == 3U && i == 1U) ? 4U : 8U))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = src->input_count;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_invtlb_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *def;
    MinicCoreInstructionId definition;
    size_t i;
    if (!out || !ctx || !fn || !info || !form || form > 4U ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 3U : form == 4U ? 1U : 2U)) return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[i].has_fixed_register_binding || b[i].early_clobber)
            return false;
    definition = fn->values[b[0].value].definition;
    if (definition >= fn->instruction_count) return false;
    def = &fn->instructions[definition];
    if (def->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        def->result != b[0].value ||
        !loong_linux_invtlb_opcode(def->value.integer_value)) return false;
    if (form == 1U || form == 3U) {
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", "$t0", (size_t)b[1].value * 8U,
                ctx->dynamic_frame)) return false;
    } else if (fputs("  or $t0, $zero, $zero\n", out) == EOF) return false;
    if (form == 1U || form == 2U) {
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", "$t1",
                (size_t)b[form == 1U ? 2U : 1U].value * 8U,
                ctx->dynamic_frame)) return false;
    } else if (fputs("  or $t1, $zero, $zero\n", out) == EOF) return false;
    return fprintf(out, "  invtlb %lld, $t0, $t1\n",
                   (long long)def->value.integer_value) >= 0;
}

/* The 32/64-bit Linux fetch-add-unless primitives are a single LL/SC
 * semantic family. Their memory output is marked =ZB by GNU, although the
 * hardware both reads and writes that address. Core must retain the actual
 * RMW effect; the target owns branch, reservation and barrier sequencing. */
static unsigned int loong_linux_add_unless_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "0:ll.w%[p],%[c]beq%[p],%[u],1fadd.w%[rc],%[p],%[a]sc.w%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.d%[p],%[c]beq%[p],%[u],1fadd.d%[rc],%[p],%[a]sc.d%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.w%0,%2beq%0,%4,1fadd.w%1,%0,%3sc.w%1,%2beqz%1,0bb2f1:dbar0x7002:",
        "0:ll.d%0,%2beq%0,%4,1fadd.d%1,%0,%3sc.d%1,%2beqz%1,0bb2f1:dbar0x7002:"
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)(i % 2U) + 1U;
    return 0U;
}

static bool loong_linux_add_unless_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const outputs[] = {"=&r", "=&r", "=ZB"};
    static const char *const inputs[] = {"r", "r"};
    const unsigned int form = src ? loong_linux_add_unless_form(
        src->template_text, src->template_length) : 0U;
    const size_t width = form == 1U ? 4U : 8U;
    size_t i;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 3U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i < 3U;
        const MinicInlineAsmOperand *op = output
            ? &src->outputs[i] : &src->inputs[i - 3U];
        const MinicExpression *expr = minic_c0_program_expression(
            program, op->expression);
        MinicType plain;
        if (!loong_csr_constraint(op, output ? outputs[i] : inputs[i - 3U]) ||
            !expr || (output && (expr->value_category != MINIC_VALUE_LVALUE ||
                                 minic_type_is_const(expr->type))) ||
            !minic_type_unqualified(expr->type, &plain) ||
            !loong_linux_cmpxchg_word(plain, width)) return false;
        contract->kinds[i] = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                     : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 5U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_add_unless_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t width = form == 1U ? 4U : 8U;
    const char *suffix = form == 1U ? "w" : "d";
    MinicType plain;
    size_t i;
    if (!out || !ctx || !fn || !info || (form != 1U && form != 2U) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 5U) return false;
    for (i = 0U; i < 5U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i < 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                     : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != expected || b[i].has_fixed_register_binding ||
            (i < 3U && !minic_type_pointee(
                fn->values[b[i].value].type, &plain))) return false;
        if (i < 3U && !loong_linux_cmpxchg_word(plain, width)) return false;
        if (i >= 3U && !loong_linux_cmpxchg_word(
                fn->values[b[i].value].type, width)) return false;
    }
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", (size_t)b[2].value * 8U,
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", (size_t)b[3].value * 8U,
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t5", (size_t)b[4].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out,
            "  or $t1, $zero, $zero\n"
            "0: ll.%s $t0, $t2, 0\n"
            "  beq $t0, $t5, 1f\n"
            "  add.%s $t1, $t0, $t4\n"
            "  sc.%s $t1, $t2, 0\n"
            "  beqz $t1, 0b\n"
            "  b 2f\n"
            "1: dbar 0x700\n"
            "2:\n", suffix, suffix, suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[0].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t0, $t7, 0\n", suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[1].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t1, $t7, 0\n", suffix) < 0) return false;
    return true;
}

/* Linux atomic{,64}_sub_if_positive: the immediate and register variants
 * share the same reservation loop and negative-result exit. The original
 * GNU input I holds the negated decrement (ADD); r holds the positive
 * decrement (SUB). Preserve that distinction in a target-owned descriptor. */
typedef struct LoongLinuxSubPositive {
    size_t width;
    bool add_negated;
} LoongLinuxSubPositive;

static bool loong_linux_sub_positive_form(
    const char *text, size_t length, LoongLinuxSubPositive *result) {
    static const struct {
        const char *compact;
        size_t width;
        bool add_negated;
    } forms[] = {
        {"1:ll.w%1,%2#atomic_sub_if_positiveaddi.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,true},
        {"1:ll.w%1,%2#atomic_sub_if_positivesub.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,false},
        {"1:ll.d%1,%2#atomic64_sub_if_positiveaddi.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,true},
        {"1:ll.d%1,%2#atomic64_sub_if_positivesub.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,false}
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || !result || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].compact) == 0) {
            result->width = forms[i].width;
            result->add_negated = forms[i].add_negated;
            return true;
        }
    return false;
}

static bool loong_linux_sub_positive_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    LoongLinuxSubPositive form;
    static const char *const output_constraints[] = {"=&r", "=&r", "+ZC"};
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !loong_linux_sub_positive_form(src->template_text,
                                       src->template_length, &form) ||
        src->output_count != 3U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U ||
        !loong_csr_constraint(&src->inputs[0], form.add_negated ? "I" : "r"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *expr = minic_c0_program_expression(
            program, src->outputs[i].expression);
        if (!loong_csr_constraint(&src->outputs[i], output_constraints[i]) ||
            !expr || expr->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type) ||
            !loong_linux_cmpxchg_word(expr->type, form.width)) return false;
        contract->kinds[i] = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (!arg || !loong_linux_cmpxchg_word(arg->type, form.width))
            return false;
    }
    contract->operand_count = 4U;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_sub_positive_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const LoongLinuxSubPositive *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const char *suffix = form && form->width == 4U ? "w" : "d";
    MinicType plain;
    size_t i;
    if (!out || !ctx || !fn || !info || !form ||
        (form->width != 4U && form->width != 8U) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 4U) return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
                      : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != expected || b[i].has_fixed_register_binding ||
            (i < 3U && !minic_type_pointee(
                fn->values[b[i].value].type, &plain))) return false;
        if (i < 3U && !loong_linux_cmpxchg_word(plain, form->width)) return false;
        if (i == 3U && !loong_linux_cmpxchg_word(
                fn->values[b[i].value].type, form->width)) return false;
    }
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", (size_t)b[2].value * 8U,
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t5", (size_t)b[3].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out,
            "1: ll.%s $t0, $t4, 0\n"
            "  %s.%s $t1, $t0, $t5\n"
            "  or $t2, $t1, $zero\n"
            "  bltz $t1, 2f\n"
            "  sc.%s $t2, $t4, 0\n"
            "  beqz $t2, 1b\n"
            "2: dbar 0x700\n", suffix,
            form->add_negated ? "add" : "sub", suffix, suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[0].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t1, $t7, 0\n", suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[1].value * 8U,
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t2, $t7, 0\n", suffix) < 0) return false;
    return true;
}

typedef struct LoongLinuxUaccess {
    bool load;
    unsigned int width;
    const char *opcode;
} LoongLinuxUaccess;

static bool loong_linux_uaccess_form(
    const char *text, size_t length, LoongLinuxUaccess *result) {
    static const struct {
        const char *prefix;
        bool load;
        unsigned int width;
        const char *opcode;
    } forms[] = {
        {"1:ld.b%1,%22:", true, 1U, "ld.b"},
        {"1:ld.h%1,%22:", true, 2U, "ld.h"},
        {"1:ld.w%1,%22:", true, 4U, "ld.w"},
        {"1:ld.d%1,%22:", true, 8U, "ld.d"},
        {"1:st.b%z2,%1#__put_user_asm2:", false, 1U, "st.b"},
        {"1:st.h%z2,%1#__put_user_asm2:", false, 2U, "st.h"},
        {"1:st.w%z2,%1#__put_user_asm2:", false, 4U, "st.w"},
        {"1:st.d%z2,%1#__put_user_asm2:", false, 8U, "st.d"}
    };
    static const char common[] =
        ".equ.L__gpr_num_zero,0"
        ".irpnum,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31"
        ".equ.L__gpr_num_$r\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_$ra,1"
        ".equ.L__gpr_num_$tp,2"
        ".equ.L__gpr_num_$sp,3"
        ".irpnum,0,1,2,3,4,5,6,7"
        ".equ.L__gpr_num_$a\\num,4+\\num"
        ".endr"
        ".irpnum,0,1,2,3,4,5,6,7,8"
        ".equ.L__gpr_num_$t\\num,12+\\num"
        ".endr"
        ".equ.L__gpr_num_$s9,22"
        ".equ.L__gpr_num_$fp,22"
        ".irpnum,0,1,2,3,4,5,6,7,8"
        ".equ.L__gpr_num_$s\\num,23+\\num"
        ".endr"
        ".pushsection__ex_table,\"a\""
        ".balign4"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(2)";
    static const char load_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_%1)<<5)))"
        ".popsection";
    static const char store_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[1500];
    size_t i, n = 0U;

    if (!text || !length || length >= sizeof(compact) || !result) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t prefix = strlen(forms[i].prefix);
        const size_t middle = sizeof(common) - 1U;
        const char *tail = forms[i].load ? load_tail : store_tail;
        const size_t suffix = strlen(tail);
        if (n != prefix + middle + suffix ||
            memcmp(compact, forms[i].prefix, prefix) != 0 ||
            memcmp(compact + prefix, common, middle) != 0 ||
            memcmp(compact + prefix + middle, tail, suffix) != 0) continue;
        result->load = forms[i].load;
        result->width = forms[i].width;
        result->opcode = forms[i].opcode;
        return true;
    }
    return false;
}

static bool loong_linux_uaccess_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    LoongLinuxUaccess form;
    const MinicExpression *err, *data, *memory;
    MinicType err_type, data_type;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != 2U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber ||
        !loong_linux_uaccess_form(src->template_text,
                                 src->template_length, &form) ||
        !loong_csr_constraint(&src->outputs[0], "+r") ||
        !loong_csr_constraint(&src->outputs[1], form.load ? "=r" : "=m") ||
        !loong_csr_constraint(&src->inputs[0], form.load ? "m" : "Jr"))
        return false;
    err = minic_c0_program_expression(program, src->outputs[0].expression);
    data = minic_c0_program_expression(program,
                                      form.load ? src->outputs[1].expression
                                                : src->inputs[0].expression);
    memory = minic_c0_program_expression(program,
                                        form.load ? src->inputs[0].expression
                                                  : src->outputs[1].expression);
    if (!err || !data || !memory ||
        err->value_category != MINIC_VALUE_LVALUE ||
        (form.load && data->value_category != MINIC_VALUE_LVALUE) ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(err->type) ||
        (form.load && minic_type_is_const(data->type)) ||
        !minic_type_unqualified(err->type, &err_type) ||
        !minic_type_unqualified(data->type, &data_type) ||
        !minic_type_equal(err_type, minic_type_int()) ||
        (!minic_type_is_integer(data_type) &&
         !(form.width == 8U && minic_type_is_pointer(data_type)))) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = form.load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = form.load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_uaccess_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const LoongLinuxUaccess *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;

    if (!out || !ctx || !fn || !info || !form || !info->is_volatile ||
        info->is_goto || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !loong_linux_uaccess_form(info->template_text,
                                 info->template_length, &(LoongLinuxUaccess){0}))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : i == 1U
                ? (form->load ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                              : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT)
                : (form->load ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                              : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
        if (b[i].operand_index != i || b[i].kind != kind ||
            b[i].value >= fn->value_count || b[i].early_clobber ||
            b[i].has_fixed_register_binding) return false;
    }
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_pointee(fn->values[b[1].value].type, &type) ||
        (form->load && !minic_type_is_integer(type)) ||
        (form->load &&
         !minic_type_pointee(fn->values[b[2].value].type, &type)))
        return false;

    /* $t0 (physical r12) is the error register and $t1 (r13) is the
     * faulting-load result. GNU's symbolic register-number .equ table is
     * replaced with those exact physical indexes only after we have
     * selected them. $t4 carries the user address; $t7 the C lvalue
     * address. The shared frame helper handles large and dynamic frames. */
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[0].value * 8U,
            ctx->dynamic_frame) ||
        fputs("  ld.w $t0, $t7, 0\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", (size_t)b[form->load ? 2U : 1U].value * 8U,
            ctx->dynamic_frame)) return false;
    if (!form->load &&
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", (size_t)b[2].value * 8U,
            ctx->dynamic_frame)) return false;
    if (fprintf(out,
            "1: %s %s, $t4, 0\n"
            "2:\n"
            "  .pushsection __ex_table, \"a\"\n"
            "  .balign 4\n"
            "  .long (1b - .)\n"
            "  .long (2b - .)\n"
            "  .short 2\n"
            "  .short ((12 << 0) | (%u << 5))\n"
            "  .popsection\n",
            form->opcode, form->load ? "$t1" : "$t2",
            form->load ? 13U : 0U) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", (size_t)b[0].value * 8U,
            ctx->dynamic_frame) ||
        fputs("  st.w $t0, $t7, 0\n", out) == EOF) return false;
    if (form->load &&
        (!minic_loongarch64_emit_frame_mem(
             out, "ld.d", "$t7", (size_t)b[1].value * 8U,
             ctx->dynamic_frame) ||
         fputs("  st.d $t1, $t7, 0\n", out) == EOF)) return false;
    return true;
}

bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicInlineAsmOperand *op;
    const MinicExpression *expr;
    if(program&&src&&contract&&src->is_volatile&&!src->is_goto&&
       src->output_count==0U&&src->input_count==0U&&
       src->clobber_count==1U&&src->has_memory_clobber&&
       src->register_clobber_count==0U&&src->label_count==0U&&
       loongarch64_literal_dbar(src->template_text,src->template_length)){
        contract->operand_count=0U;
        return true;
    }
    if (loong_linux_invtlb_source(program, src, contract) ||
        loong_linux_sub_positive_source(program, src, contract) ||
        loong_linux_add_unless_source(program, src, contract) ||
        loong_linux_uaccess_source(program, src, contract) ||
        loong_fcsr0_write_source(program, src, contract) ||
        loong_percpu_indexed_source(program, src, contract) ||
        loong_percpu_amo_source(program, src, contract))
        return true;
    if (loong_linux_cmpxchg_source(program,src,contract)) return true;
    if (loong_linux_xchg_source(program,src,contract)) return true;
    if (loong_linux_xchg_small_source(program,src,contract)) return true;
    if (loong_mask_nospec_source(program, src, contract))
        return true;
    if(loong_pte_source(program,src,contract))
        return true;
    if (loong_atomic_qualified_source(program, src, contract) ||
        loong_atomic_fetch_qualified_source(program, src, contract) ||
        loong_csr_qualified_source(program, src, contract))
        return true;
    if (program == NULL || src == NULL || contract == NULL ||
        !src->is_volatile || src->is_goto || src->output_count != 0U ||
        src->input_count != 1U || src->inputs == NULL ||
        src->label_count != 0U || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || !src->has_memory_clobber ||
        !loongarch64_dbar_template(src->template_text, src->template_length))
        return false;
    op = &src->inputs[0];
    if (op->constraint_text == NULL || op->constraint_length != 1U ||
        op->constraint_text[0] != 'I')
        return false;
    expr = minic_c0_program_expression(program, op->expression);
    if (expr == NULL || expr->kind != MINIC_EXPRESSION_INTEGER ||
        !loongarch64_dbar_hint(expr->value.integer_value))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


static bool minic_loongarch64_emit_amo(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    if (ins->value.structured_inline_asm.operand_count != 2U ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        !info->has_memory_clobber)
        return false;
    atom = loong_atomic_template(info->template_text, info->template_length, false);
    if (atom == NULL || b[0].operand_index != 0U ||
        b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !loong_atomic_scalar_type(pointee, atom->width) ||
        !loong_atomic_scalar_type(fn->values[b[1].value].type, atom->width))
        return false;
    /* Existing LP64D machine frame transport and scratch policy; the
     * LoongArch atomic *DB instruction performs both the RMW and hardware
     * memory ordering. Never lower +ZB to C load/compute/store. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", (size_t)b[0].value * 8U, ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, atom->width == 8U ? "ld.d" : "ld.w",
               "$t0", (size_t)b[1].value * 8U, ctx->dynamic_frame) &&
           fprintf(out, "  %s $zero, $t0, $t1\n",
                   atom->instruction) >= 0;
}

static bool minic_loongarch64_emit_csr(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = loong_csr_template(
        info->template_text, info->template_length);
    size_t width = 0U;
    MinicType pointee;
    const MinicCoreInstruction *imm;
    MinicCoreInstructionId def;
    size_t imm_index, i;
    if (!form || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 2U : 3U)) return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i) {
        /* Linux arch_local_irq_save binds its unsigned mask as GNU local
         * `register u32 mask asm("t0")`. The actual instruction below loads
         * that operand into PHYSICAL $t0 before CSRXCHG. Core now preserves
         * the local hardreg name instead of discarding it. Only this exact
         * matching binding is safe; a different fixed register cannot be
         * silently reallocated to $t0. All other CSR operands must remain
         * ordinary spill-backed values or addresses. */
        if (b[i].has_fixed_register_binding) {
            const MinicCoreFixedRegisterBinding *fixed;
            if (form != 2U || i != 1U ||
                b[i].fixed_register_binding_id >= fn->fixed_register_binding_count)
                return false;
            fixed = &fn->fixed_register_bindings[b[i].fixed_register_binding_id];
            if (!fixed->is_local || !fixed->register_name ||
                fixed->register_name_length != 2U ||
                memcmp(fixed->register_name, "t0", 2U) != 0 ||
                !minic_type_equal(fixed->type, minic_type_unsigned_int()))
                return false;
        }
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].value > MINIC_LARGE_FRAME_MAX / 8U)
            return false;
    }
    if (b[0].kind != (form == 1U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !loong_csr_output_type(pointee, &width) ||
        (form == 1U && width != 4U)) return false;
    imm_index = form == 1U ? 1U : 2U;
    if (b[imm_index].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
        return false;
    def = fn->values[b[imm_index].value].definition;
    if (def >= fn->instruction_count) return false;
    imm = &fn->instructions[def];
    if (imm->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        imm->result != b[imm_index].value || imm->value.integer_value != 0)
        return false;
    if (form == 2U && (b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !loong_csr_word_type(fn->values[b[1].value].type))) return false;
    /* $t1 carries the saved/restored CRMD bits. For CSR XCHG only the
     * hardware mask ($t0) selects IE; save former bits back to the C lvalue.
     * User-mode QEMU cannot execute privileged CSR operations. */
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", (size_t)b[0].value * 8U,
            ctx->dynamic_frame)) return false;
    if (form == 2U &&
        (!minic_loongarch64_emit_frame_mem(
             out, "ld.w", "$t0", (size_t)b[1].value * 8U,
             ctx->dynamic_frame) ||
         fprintf(out, "  %s $t1, $t2, 0\n"
                      "  csrxchg $t1, $t0, 0\n",
                 width == 8U ? "ld.d" : "ld.w") < 0))
        return false;
    if (form == 1U && fputs("  csrrd $t1, 0\n", out) == EOF)
        return false;
    return fprintf(out, "  %s $t1, $t2, 0\n",
        width == 8U ? "st.d" : "st.w") >= 0;
}
/* Result register of LoongArch AMO is the OLD pre-update memory value.
 * No CAS emulation, ordinary Core load/store, or guessed ISA permission. */
static bool minic_loongarch64_emit_amo_fetch(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType memory_type, output_type;
    if (ins->value.structured_inline_asm.operand_count != 3U ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 0U) return false;
    atom = loong_atomic_template(info->template_text, info->template_length, true);
    if (!atom) return false;
    if (b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[2].operand_index != 2U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber || b[2].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[2].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[2].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &memory_type) ||
        !minic_type_pointee(fn->values[b[1].value].type, &output_type) ||
        !loong_atomic_scalar_type(memory_type, atom->width) ||
        !loong_atomic_scalar_type(output_type, atom->width) ||
        !loong_atomic_scalar_type(fn->values[b[2].value].type, atom->width))
        return false;
    /* Target frame access uses $t3 scratch for large offsets. $t2 remains
     * stable across later frame accesses and holds hardware AMO old-value. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", (size_t)b[0].value * 8U,
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, atom->width == 8U ? "ld.d" : "ld.w", "$t0",
               (size_t)b[2].value * 8U, ctx->dynamic_frame) &&
           fprintf(out, "  %s $t2, $t0, $t1\n", atom->instruction) >= 0 &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", (size_t)b[1].value * 8U,
               ctx->dynamic_frame) &&
           fprintf(out, "  %s $t2, $t1, 0\n",
                   atom->width == 8U ? "st.d" : "st.w") >= 0;
}


/* Full original Linux LL/SC retry sequence and mandatory dbar 0x700.
 * Frame helper uses $t3 for large offsets; load all SSA operands BEFORE
 * loop. $t4 is the real LL/SC scratch and SC success result. */
static bool loong_pte_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType type,plain;
    size_t i;
    if(!out||!ctx||!fn||!b||!info||!info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count||
       !loong_pte_form(info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=3U)
        return false;
    for(i=0U;i<3U;++i){
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
             MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
             i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                   MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type=fn->values[b[i].value].type;
        if(i<2U&&!minic_type_pointee(type,&type))return false;
        if(!minic_type_unqualified(type,&plain)||
           !minic_type_equal(plain,minic_type_unsigned_long()))return false;
    }
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t2",(size_t)b[1].value*8U,ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t0",(size_t)b[2].value*8U,ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",(size_t)b[0].value*8U,ctx->dynamic_frame)&&
           fputs("1:\n"
                 "  ll.d $t4, $t1, 0\n"
                 "  bnez $t4, 2f\n"
                 "  or $t4, $t4, $t0\n"
                 "  sc.d $t4, $t1, 0\n"
                 "  beqz $t4, 1b\n"
                 "  nop\n"
                 "2:\n"
                 "  dbar 0x700\n"
                 "  st.d $t4, $t2, 0\n",out)!=EOF;
}

/* The exact Linux LL/SC compare-and-swap register choreography.
 * Frame helper may clobber $t3; keep live physical state in $t0-$t2,
 * $t4-$t8. Return both early-clobbered register outputs and the
 * memory output without converting volatile reads into scalar C stores. */
static bool loong_linux_cmpxchg_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const unsigned form=loong_linux_cmpxchg_form(
        info->template_text,info->template_length);
    const size_t n=form==3U?7U:5U,width=form==2U?8U:4U;
    const char *load=width==8U?"ld.d":"ld.w";
    const char *store=width==8U?"st.d":"st.w";
    size_t k;
    if(!out||!ctx||!fn||!info||!form||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count!=(form==3U?0U:1U)||
       ins->value.structured_inline_asm.operand_count!=n)return false;
    for(k=0U;k<n;++k){
        const bool mem=form==3U?(k==2U||k==3U):(k==1U||k==2U);
        const bool output=form==3U?k<3U:k<2U;
        MinicType type;
        if(b[k].operand_index!=k||b[k].value>=fn->value_count||
           b[k].has_fixed_register_binding||
           b[k].kind!=(mem?(output?
                 MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                 MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
                 output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type=fn->values[b[k].value].type;
        if((output||mem)&&!minic_type_pointee(type,&type))return false;
        if(!loong_linux_cmpxchg_word(type,width) &&
           !(form==1U && k==4U &&
             loong_linux_cmpxchg_word(type,8U)))return false;
    }
    if(form==3U){
        /* out0 old32, out1 temp, out2 destination; input3 source,
         * input4 mask, input5 expected and input6 replacement. */
        if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
              (size_t)b[3].value*8U,ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
              (size_t)b[2].value*8U,ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t0",
              (size_t)b[4].value*8U,ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t1",
              (size_t)b[5].value*8U,ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t2",
              (size_t)b[6].value*8U,ctx->dynamic_frame)||
           fputs("1:\n  ll.w $t5, $t4, 0\n"
                 "  and $t6, $t5, $t0\n"
                 "  bne $t6, $t1, 2f\n"
                 "  andn $t6, $t5, $t0\n"
                 "  or $t6, $t6, $t2\n"
                 "  sc.w $t6, $t7, 0\n"
                 "  beqz $t6, 1b\n"
                 "  b 3f\n"
                 "2:\n  dbar 0x700\n3:\n",out)==EOF)return false;
        for(k=0U;k<2U;++k)
            if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
                  (size_t)b[k].value*8U,ctx->dynamic_frame)||
               fprintf(out,"  st.w %s, $t7, 0\n",
                       k==0U?"$t5":"$t6")<0)return false;
        return true;
    }
    if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
          (size_t)b[2].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          (size_t)b[1].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,load,"$t1",
          (size_t)b[3].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,load,"$t2",
          (size_t)b[4].value*8U,ctx->dynamic_frame)||
       fprintf(out,"1:\n  ll.%c $t5, $t4, 0\n"
                   "  bne $t5, $t1, 2f\n"
                   "  or $t0, $t2, $zero\n"
                   "  sc.%c $t0, $t7, 0\n"
                   "  beqz $t0, 1b\n"
                   "2:\n  dbar 0x700\n",
                   width==8U?'d':'w',width==8U?'d':'w')<0||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          (size_t)b[0].value*8U,ctx->dynamic_frame)||
       fprintf(out,"  %s $t5, $t7, 0\n",store)<0)return false;
    return true;
}


/* Emit original hardware AMDB: old is observable, not optimized away;
 * the instruction itself enforces GNU Linux DB memory ordering. */
static bool loong_linux_xchg_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const unsigned form=loong_linux_xchg_form(
        info->template_text,info->template_length);
    const size_t width=form&&form%2U==0U?8U:4U;
    const bool named=form>=3U&&form<=6U;
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;
    if(!out||!ctx||!fn||!info||!form||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=3U)return false;
    for(i=0U;i<3U;++i){
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?
               MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
               i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type=fn->values[b[i].value].type;
        if(i<2U&&!minic_type_pointee(type,&type))return false;
        if(!loong_linux_cmpxchg_word(type,i==1U&&named?8U:width))return false;
    }
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",(size_t)b[0].value*8U,ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,width==8U?"ld.d":"ld.w","$t0",
             (size_t)b[2].value*8U,ctx->dynamic_frame)&&
           fprintf(out,"  %s.%c $t2, $t0, $t1\n",
                   form>=15U?"amxor":form>=13U?"amor":
                   form>=11U?"amand":form>=9U?"amadd":
                   form>=7U?"amswap":"amswap_db",
                   width==8U?'d':'w')>=0&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",(size_t)b[1].value*8U,ctx->dynamic_frame)&&
           fprintf(out,"  %s $t2, $t1, 0\n",
                   width==8U||named?"st.d":"st.w")>=0;
}

/* Keep both original GNU memory operands distinct in Core. The LL reads
 * input[3], while SC commits through output[2]; no untyped replacement
 * and no unsynchronized C load/store is permitted. */
static bool loong_linux_xchg_small_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    size_t i;
    if(!out||!ctx||!fn||!info||!loong_linux_xchg_small_form(
           info->template_text,info->template_length)||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=6U)return false;
    for(i=0U;i<6U;++i){
        const bool output=i<3U;
        const bool memory=i==2U||i==3U;
        MinicType type;
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||
           b[i].kind!=(memory?
               (output?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
               (output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)))
            return false;
        type=fn->values[b[i].value].type;
        if(output||memory){
            if(!minic_type_pointee(type,&type))return false;
        }
        if(!loong_linux_cmpxchg_word(type,4U))return false;
    }
    if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
          (size_t)b[3].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          (size_t)b[2].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.w","$t0",
          (size_t)b[4].value*8U,ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.w","$t1",
          (size_t)b[5].value*8U,ctx->dynamic_frame)||
       fputs("1:\n"
             "  ll.w $t5, $t4, 0\n"
             "  andn $t6, $t5, $t0\n"
             "  or $t6, $t6, $t1\n"
             "  sc.w $t6, $t7, 0\n"
             "  beqz $t6, 1b\n",out)==EOF)return false;
    for(i=0U;i<2U;++i){
        if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
              (size_t)b[i].value*8U,ctx->dynamic_frame)||
           fprintf(out,"  st.w %s, $t7, 0\n",
                   i==0U?"$t5":"$t6")<0)return false;
    }
    return true;
}

bool minic_loongarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *info;
    if (out == NULL || ctx == NULL || ctx->fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type) ||
        ins->value.structured_inline_asm.inline_asm_id >= ctx->fn->inline_asm_count)
        return false;
    info = &ctx->fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    {
        const unsigned int invtlb = loong_linux_invtlb_form(
            info->template_text, info->template_length);
        if (invtlb != 0U)
            return loong_linux_invtlb_emit(out, ctx, ins, info, invtlb);
    }
    {
        LoongLinuxSubPositive sub_positive;
        if (loong_linux_sub_positive_form(
                info->template_text, info->template_length, &sub_positive))
            return loong_linux_sub_positive_emit(
                out, ctx, ins, info, &sub_positive);
    }
    {
        const unsigned int add_unless = loong_linux_add_unless_form(
            info->template_text, info->template_length);
        if (add_unless != 0U)
            return loong_linux_add_unless_emit(out, ctx, ins, info, add_unless);
    }
    {
        LoongLinuxUaccess uaccess;
        if (loong_linux_uaccess_form(info->template_text,
                                    info->template_length, &uaccess))
            return loong_linux_uaccess_emit(out, ctx, ins, info, &uaccess);
    }
    if(ins->value.structured_inline_asm.operand_count==0U){
        const char *barrier=loongarch64_literal_dbar(
            info->template_text,info->template_length);
        if(!barrier||!info->is_volatile||info->is_goto||
           !info->has_memory_clobber||info->register_clobber_count)
            return false;
        return fprintf(out,"  %s\n",barrier)>=0;
    }
    if (loong_fcsr0_write_form(info->template_text, info->template_length))
        return loong_fcsr0_write_emit(out, ctx, ins, info);
    if (loong_percpu_indexed_form(info->template_text, info->template_length))
        return loong_percpu_indexed_emit(out, ctx, ins, info);
    if (loong_percpu_amo_form(info->template_text, info->template_length) != SIZE_MAX)
        return loong_percpu_amo_emit(out, ctx, ins, info);
    if (loong_linux_cmpxchg_form(info->template_text,info->template_length))
        return loong_linux_cmpxchg_emit(out,ctx,ins,info);
    if (loong_linux_xchg_form(info->template_text,info->template_length))
        return loong_linux_xchg_emit(out,ctx,ins,info);
    if (loong_linux_xchg_small_form(info->template_text,info->template_length))
        return loong_linux_xchg_small_emit(out,ctx,ins,info);
    if (loong_mask_nospec_form(info->template_text, info->template_length))
        return loong_mask_nospec_emit(out, ctx, ins, info);
    if(loong_pte_form(info->template_text,info->template_length))
        return loong_pte_emit(out,ctx,ins,info);
    if (loong_csr_template(info->template_text, info->template_length))
        return minic_loongarch64_emit_csr(out, ctx, ins, info);
    if (ins->value.structured_inline_asm.operand_count == 3U)
        return minic_loongarch64_emit_amo_fetch(out, ctx, ins, info);
    if (ins->value.structured_inline_asm.operand_count == 2U)
        return minic_loongarch64_emit_amo(out, ctx, ins, info);
    return minic_loongarch64_emit_dbar(out, ctx, ins);
}
bool minic_loongarch64_emit_dbar(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *b;
    const MinicCoreInstruction *def;
    MinicCoreInstructionId definition;
    int64_t hint;
    if (out == NULL || ctx == NULL || ctx->fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        ins->value.structured_inline_asm.inline_asm_id >= ctx->fn->inline_asm_count)
        return false;
    fn = ctx->fn;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    b = &ins->value.structured_inline_asm.operands[0];
    if (!info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        !info->has_memory_clobber ||
        !loongarch64_dbar_template(info->template_text, info->template_length) ||
        b->operand_index != 0U ||
        b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b->has_fixed_register_binding || b->early_clobber ||
        b->value >= fn->value_count)
        return false;
    definition = fn->values[b->value].definition;
    if (definition >= fn->instruction_count)
        return false;
    def = &fn->instructions[definition];
    if (def->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        def->result != b->value)
        return false;
    hint = def->value.integer_value;
    if (!loongarch64_dbar_hint(hint)) return false;
    /* DBAR executes in the user/kernel ISA, and carries hardware ordering.
     * No stack spill is needed because the operand is an exact constant. */
    return fprintf(out, "  dbar %lld\n", (long long)hint) >= 0;
}
