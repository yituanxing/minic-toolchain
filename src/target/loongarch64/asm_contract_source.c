#include "target/loongarch64/asm_contract_source.h"
#include "frontend/ast.h"
#include "target/loongarch64/simd_source_policy.h"
#include "target/common/linux_jump_label_source.h"
#include "target/loongarch64/asm_contract_internal.h"
#include <string.h>
#include <stdint.h>

/* C/GNU input admission owns C language ranks, constraints and normalization.
 * Machine consumers never depend on these predicates or the AST. */

static bool loong_csr_constraint(const MinicInlineAsmOperand *, const char *);
static bool loong_linux_descriptor_template_matches(const MinicInlineAsm *);
static bool loong_linux_descriptor_qualify(const MinicC0Program *,
    const MinicInlineAsm *, MinicTargetInlineAsmContract *);

bool minic_loongarch64_qualify_inline_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source) {
    return minic_multi_qualify_linux_jump_label_asm_goto(
        program, source, loong_linux_jump_label_form);
}

bool minic_loongarch64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const reserved[] = {
        "dbar", "cacop", "amand_db.w", "amor_db.w", "amxor_db.w", "amadd_db.w",
        "amand_db.d", "amor_db.d", "amxor_db.d", "amadd_db.d",
        "csrrd", "csrxchg", "1:ll.w", "1:ll.d", "sltu",
        "movgr2fcsr", "ldx.", "stx.", "amadd.", "amand.", "amor.",
        "amswap_db.w", "amswap_db.d", "rdtime.d", "la.pcrel"
    };
    /* The positive exact-form registry must also reserve its spelling when
     * constraints fail: otherwise a newly registered hardware form could
     * escape into generic raw inline assembly if this older prefix list
     * overlooked its mnemonic. The prefix list still rejects *mutated*
     * source templates that intentionally do not match an exact form. */
    return minic_source_backend_inline_asm_prefix_matches(
               src, reserved, sizeof(reserved) / sizeof(reserved[0])) ||
           loong_linux_descriptor_template_matches(src);
}

static bool loong_linux_clock_output_type(MinicType type, bool word32) {
    MinicType plain;
    return minic_type_unqualified(type, &plain) &&
           (word32 ? minic_type_equal(plain, minic_type_int())
                   : (minic_type_equal(plain, minic_type_unsigned_long()) ||
                      minic_type_equal(plain, minic_type_unsigned_long_long())));
}

static bool loong_linux_clock_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const LoongLinuxClockAsm form = src ? loong_linux_clock_form(
        src->template_text, src->template_length) : LOONG_CLOCK_NONE;
    const size_t count = form == LOONG_CLOCK_VDSO_BASE ||
                         form == LOONG_CLOCK_CPU_ID ||
                         form == LOONG_CLOCK_COUNTER_ONLY ? 1U : 2U;
    size_t i;
    if (!program || !src || !contract || form == LOONG_CLOCK_NONE ||
        src->is_goto || src->output_count != count || src->input_count ||
        !src->outputs || src->label_count || src->clobber_count ||
        src->register_clobber_count ||
        src->has_memory_clobber != (form == LOONG_CLOCK_CPU_ID) ||
        src->is_volatile != (form != LOONG_CLOCK_VDSO_BASE)) return false;
    for (i = 0U; i < count; ++i) {
        const MinicExpression *expr = minic_c0_program_expression(
            program, src->outputs[i].expression);
        const bool named = form == LOONG_CLOCK_RDTIME_NAMED;
        const bool word32 = form == LOONG_CLOCK_CPU_ID ||
            (form == LOONG_CLOCK_RDTIME_NUMERIC && i == 1U);
        if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type) ||
            !loong_linux_clock_output_type(expr->type, word32) ||
            !loong_csr_constraint(&src->outputs[i], named ? "=&r" : "=r"))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    contract->operand_count = count;
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_CLOCK;
    contract->target_machine_form_variant = (uint32_t)form;
    return true;
}

static bool loong_linux_vdso_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const constraints[] = {"+r", "r", "r", "r"};
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !loong_linux_vdso_syscall_form(src->template_text, src->template_length) ||
        src->output_count != 1U || src->input_count != 3U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 10U ||
        src->register_clobber_count != 9U || !src->register_clobbers)
        return false;
    for (i = 0U; i < 9U; ++i) {
        const MinicInlineAsmRegisterClobber *c = &src->register_clobbers[i];
        if (!c->name || c->name_length != 3U || c->name[0] != '$' ||
            c->name[1] != 't' || c->name[2] != (char)('0' + i)) return false;
    }
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U
            ? &src->outputs[0] : &src->inputs[i - 1U];
        const MinicExpression *expr = minic_c0_program_expression(
            program, op->expression);
        if (!loong_csr_constraint(op, constraints[i]) || !expr ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))) ||
            (i <= 1U && !minic_type_equal(expr->type, minic_type_long())) ||
            (i == 2U && !minic_type_is_pointer(expr->type) &&
              !minic_type_equal(expr->type, minic_type_int())) ||
            (i == 3U && !minic_type_is_pointer(expr->type)))
            return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_cacop_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *opcode, *address;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !loong_linux_cacop_form(src->template_text, src->template_length) ||
        src->output_count != 0U || src->input_count != 2U || !src->inputs ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        !loong_csr_constraint(&src->inputs[0], "i") ||
        !loong_csr_constraint(&src->inputs[1], "ZC"))
        return false;
    opcode = minic_c0_program_expression(program, src->inputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!opcode || !minic_type_is_integer(opcode->type) || !address ||
        address->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(address->type, minic_type_unsigned_char()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_AMO_STORE;
    contract->target_machine_form_variant =
        (uint32_t)(atom - loong_atomic) + 1U;
    return true;
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_CSR;
    contract->target_machine_form_variant = form;
    return true;
}

static bool loong_linux_cpucfg_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *index;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !loong_linux_cpucfg_form(
            source->template_text, source->template_length) ||
        !loong_csr_constraint(&source->outputs[0], "=&r") ||
        !loong_csr_constraint(&source->inputs[0], "r"))
        return false;
    output = minic_c0_program_expression(
        program, source->outputs[0].expression);
    index = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!output || output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_int()) ||
        !index || !minic_type_is_integer(index->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_AMO_FETCH;
    contract->target_machine_form_variant =
        (uint32_t)(atom - loong_atomic) + 1U;
    return true;
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
            memory->type,loong_percpu_amo_width(form))||
        !loong_percpu_amo_memory_type(old->type,8U)||
        !loong_percpu_amo_memory_type(value->type,8U)||
        output->access!=MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        src->outputs[1].access!=MINIC_INLINE_ASM_OPERAND_READ_WRITE)
        return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_id=MINIC_LOONG_MACHINE_ASM_PERCPU_AMO;
    contract->target_machine_form_variant=(uint32_t)form+1U;
    return true;
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
         minic_type_equal(plain,minic_type_unsigned_int())||
         minic_type_equal(plain,minic_type_long())||
         minic_type_equal(plain,minic_type_unsigned_long())))
        return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool loong_linux_fpr_transfer_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? loong_linux_fpr_transfer_form(
        src->template_text, src->template_length) : 0U;
    const bool read = form <= 32U;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *value;
    MinicType plain;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != (read ? 1U : 0U) ||
        src->input_count != (read ? 0U : 1U) ||
        src->clobber_count || src->register_clobber_count ||
        src->has_memory_clobber || src->label_count)
        return false;
    operand = read ? (src->outputs ? &src->outputs[0] : NULL) :
        (src->inputs ? &src->inputs[0] : NULL);
    if (!operand || !loong_csr_constraint(operand, read ? "=r" : "r"))
        return false;
    value = minic_c0_program_expression(program, operand->expression);
    if (!value || (read && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_INVTLB;
    contract->target_machine_form_variant = form;
    return true;
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_ADD_UNLESS;
    contract->target_machine_form_variant = form;
    return true;
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
        /* +ZC itself is the hardware read/write memory effect. Original
         * GNU Linux has no separate "memory" clobber for this LL/SC. */
        src->has_memory_clobber || src->clobber_count != 0U ||
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE;
    contract->target_machine_form_variant =
        (form.width == 8U ? 2U : 0U) + (form.add_negated ? 1U : 2U);
    return true;
}

static bool loong_linux_futex_cmpxchg_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const out[] = {"+r", "=&r", "=ZC"};
    static const char *const in[] = {"ZC", "Jr", "Jr"};
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !loong_linux_futex_cmpxchg_form(src->template_text, src->template_length) ||
        src->output_count != 3U || src->input_count != 3U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 2U ||
        src->register_clobber_count != 1U || !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "t0", 2U) != 0)
        return false;
    for (i = 0U; i < 6U; ++i) {
        const bool output = i < 3U;
        const MinicInlineAsmOperand *op = output
            ? &src->outputs[i] : &src->inputs[i - 3U];
        const MinicExpression *expr = minic_c0_program_expression(
            program, op->expression);
        MinicType plain;
        if (!loong_csr_constraint(op, output ? out[i] : in[i - 3U]) ||
            !expr ||
            (output && (expr->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expr->type))) ||
            ((i == 2U || i == 3U) &&
             expr->value_category != MINIC_VALUE_LVALUE) ||
            !minic_type_unqualified(expr->type, &plain) ||
            !minic_type_equal(plain, i == 0U
                ? minic_type_int() : minic_type_unsigned_int())) return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
            : i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 6U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_futex_rmw_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *err, *old, *mem_output, *mem_input, *value;
    const LoongLinuxFutexRmw form = src ? loong_linux_futex_rmw_form(
        src->template_text, src->template_length) : LOONG_FUTEX_NONE;
    MinicType plain;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        form == LOONG_FUTEX_NONE ||
        src->output_count != 3U || src->input_count != 3U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 2U ||
        src->register_clobber_count != 1U || !src->register_clobbers ||
        !loong_csr_constraint(&src->outputs[0], "=r") ||
        !loong_csr_constraint(&src->outputs[1], "=&r") ||
        !loong_csr_constraint(&src->outputs[2], "=ZC") ||
        !loong_csr_constraint(&src->inputs[0], "0") ||
        !loong_csr_constraint(&src->inputs[1], "ZC") ||
        !loong_csr_constraint(&src->inputs[2], "Jr") ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "t0", 2U) != 0)
        return false;
    err = minic_c0_program_expression(program, src->outputs[0].expression);
    old = minic_c0_program_expression(program, src->outputs[1].expression);
    mem_output = minic_c0_program_expression(program, src->outputs[2].expression);
    mem_input = minic_c0_program_expression(program, src->inputs[1].expression);
    value = minic_c0_program_expression(program, src->inputs[2].expression);
    if (!err || !old || !mem_output || !mem_input || !value ||
        err->value_category != MINIC_VALUE_LVALUE ||
        old->value_category != MINIC_VALUE_LVALUE ||
        mem_output->value_category != MINIC_VALUE_LVALUE ||
        mem_input->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(err->type) || minic_type_is_const(old->type) ||
        !minic_type_unqualified(err->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_unqualified(old->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_unqualified(mem_output->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_int()) ||
        !minic_type_unqualified(mem_input->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_int()) ||
        !minic_type_is_integer(value->type)) return false;
    contract->operand_count = 6U;
    for (i = 0U; i < 6U; ++i)
        contract->kinds[i] = i <= 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
            : i == 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_FUTEX_RMW;
    contract->target_machine_form_variant = (uint32_t)form;
    return true;
}

static bool loong_uaccess_scalar_width(MinicType type, size_t *width) {
    MinicType plain;
    if (!width || !minic_type_unqualified(type, &plain)) return false;
    if (minic_type_is_pointer(plain)) { *width = 8U; return true; }
    if (!minic_type_is_integer(plain)) return false;
    switch (plain.integer_rank) {
        case MINIC_INTEGER_RANK_CHAR: *width = 1U; return true;
        case MINIC_INTEGER_RANK_SHORT: *width = 2U; return true;
        case MINIC_INTEGER_RANK_INT: *width = 4U; return true;
        case MINIC_INTEGER_RANK_LONG:
        case MINIC_INTEGER_RANK_LONG_LONG: *width = 8U; return true;
        default: return false;
    }
}

static bool loong_uaccess_memory_width_matches(
    const MinicC0Program *program, MinicType type, unsigned int width) {
    MinicType plain;
    size_t actual_width;
    const MinicRecord *record;
    if (!program || !minic_type_unqualified(type, &plain)) return false;
    if (!minic_type_is_record(plain))
        return loong_uaccess_scalar_width(plain, &actual_width) &&
               actual_width == (size_t)width;
    record = minic_c0_program_record(program, plain.record_id);
    return record && record->name &&
           strcmp(record->name, "__large_struct") == 0;
}

static bool loong_linux_uaccess_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    LoongLinuxUaccess form;
    const MinicExpression *err, *data, *memory;
    MinicType err_type;
    size_t data_width;
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
        !minic_type_equal(err_type, minic_type_int()) ||
        !loong_uaccess_scalar_width(data->type, &data_width) ||
        !loong_uaccess_memory_width_matches(program, memory->type, form.width) ||
        data_width < form.width ||
        (minic_type_is_pointer(data->type) && form.width != 8U)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = form.load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = form.load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_UACCESS;
    contract->target_machine_form_variant =
        (form.load ? 0U : 4U) + (uint32_t)(form.width == 1U ? 1U :
            form.width == 2U ? 2U : form.width == 4U ? 3U : 4U);
    return true;
}

static bool loong_linux_irq_switch_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *stack, *regs;
    MinicType plain;
    size_t i;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 2U ||
        !source->inputs || source->label_count || !source->has_memory_clobber ||
        source->clobber_count != 19U || source->register_clobber_count != 18U ||
        !source->register_clobbers ||
        !loong_linux_irq_switch_template(
            source->template_text, source->template_length, false) ||
        !loong_csr_constraint(&source->inputs[0], "r") ||
        !loong_csr_constraint(&source->inputs[1], "r") ||
        !source->inputs[0].name || source->inputs[0].name_length != 3U ||
        memcmp(source->inputs[0].name, "stk", 3U) != 0 ||
        !source->inputs[1].name || source->inputs[1].name_length != 4U ||
        memcmp(source->inputs[1].name, "regs", 4U) != 0)
        return false;
    for (i = 0U; i < 18U; ++i)
        if (!loong_linux_irq_switch_clobber(
                source->register_clobbers[i].name,
                source->register_clobbers[i].name_length, i))
            return false;
    stack = minic_c0_program_expression(
        program, source->inputs[0].expression);
    regs = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!stack || !regs || !minic_type_unqualified(stack->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(regs->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool loong_linux_frametrace_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *out, *in;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs || source->label_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        source->register_clobber_count ||
        !loong_linux_frametrace_template(
            source->template_text, source->template_length) ||
        !loong_csr_constraint(&source->outputs[0], "=m") ||
        !loong_csr_constraint(&source->inputs[0], "r"))
        return false;
    out = minic_c0_program_expression(
        program, source->outputs[0].expression);
    in = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!out || !in || out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) ||
        !minic_type_unqualified(out->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(in->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool loong_linux_frametrace_csr_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *result;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        loong_linux_frametrace_csr_form(
            source->template_text, source->template_length) == UINT32_MAX ||
        !loong_csr_constraint(&source->outputs[0], "=r"))
        return false;
    result = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!result || result->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(result->type) ||
        !minic_type_unqualified(result->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_int())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

bool minic_loongarch64_seal_normalized_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *source,
    const char *normalized, size_t length,
    MinicTargetInlineAsmContract *contract) {
    MinicTargetInlineAsmContract admitted = {0};
    uint32_t id, variant;
    bool descriptor;
    size_t i;
    if (!minic_loongarch64_simd_seal_normalized(
            program, source, normalized, length, contract)) return false;
    if (contract == NULL) return false;
    id = contract->target_machine_form_id;
    variant = contract->target_machine_form_variant;
    descriptor = id != 0U && id < MINIC_LOONG_MACHINE_ASM_CLOCK;
    if (!descriptor && (id < MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR ||
                        id > MINIC_LOONG_MACHINE_ASM_AMO_FETCH))
        return true;
    /* Seal every registered Linux descriptor as well as typed scalars.
     * Source rank/constraints are rechecked before the Core witness is
     * published; a named-to-numeric operand rewrite must retain the same
     * ordinal, hardware effects, and physical operand ordering. */
    if (!program || !source || !normalized ||
        !minic_loongarch64_qualify_inline_asm(program, source, &admitted) ||
        !minic_source_backend_capture_asm_operand_source(
            source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &admitted) ||
        admitted.target_machine_form_id != id ||
        admitted.target_machine_form_variant != variant ||
        !(descriptor
              ? minic_loongarch64_typed_descriptor_matches_template(
                    id, variant, source->template_text, source->template_length) &&
                minic_loongarch64_typed_descriptor_matches_template(
                    id, variant, normalized, length)
              : minic_loongarch64_typed_scalar_form_matches_template(
                    id, variant, source->template_text, source->template_length) &&
                minic_loongarch64_typed_scalar_form_matches_template(
                    id, variant, normalized, length)) ||
        admitted.operand_count != contract->operand_count ||
        admitted.output_count != contract->output_count ||
        admitted.implicit_memory_effect != contract->implicit_memory_effect ||
        admitted.effective_memory_effect != contract->effective_memory_effect ||
        admitted.explicit_memory_clobber != contract->explicit_memory_clobber)
        return false;
    for (i = 0U; i < admitted.operand_count; ++i)
        if (admitted.kinds[i] != contract->kinds[i] ||
            admitted.gnu_early_clobber[i] != contract->gnu_early_clobber[i] ||
            admitted.gnu_readwrite[i] != contract->gnu_readwrite[i] ||
            admitted.has_tied_output[i] != contract->has_tied_output[i] ||
            admitted.tied_output_index[i] != contract->tied_output_index[i])
            return false;
    return true;
}

bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicInlineAsmOperand *op;
    const MinicExpression *expr;
    if (minic_loongarch64_simd_qualify_source(program, src, contract)) return true;
    if (loong_linux_descriptor_qualify(program, src, contract)) return true;
    if (loong_linux_clock_source(program, src, contract)) return true;
    if (loong_linux_irq_switch_source(program, src, contract)) return true;
    if (loong_linux_futex_rmw_source(program, src, contract)) return true;
    if(program&&src&&contract&&src->is_volatile&&!src->is_goto&&
       src->output_count==0U&&src->input_count==0U&&
       src->clobber_count==1U&&src->has_memory_clobber&&
       src->register_clobber_count==0U&&src->label_count==0U&&
       loongarch64_literal_dbar(src->template_text,src->template_length)){
        contract->operand_count=0U;
        contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR;
        contract->target_machine_form_variant = strcmp(loongarch64_literal_dbar(
            src->template_text, src->template_length), "dbar 0") == 0 ? 1U : 2U;
        return true;
    }
    if (loong_linux_invtlb_source(program, src, contract) ||
        loong_linux_sub_positive_source(program, src, contract) ||
        loong_linux_add_unless_source(program, src, contract) ||
        loong_linux_uaccess_source(program, src, contract) ||
        loong_percpu_amo_source(program, src, contract))
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
    contract->target_machine_form_id = MINIC_LOONG_MACHINE_ASM_HINT_DBAR;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool loong_linux_descriptor_template_matches(const MinicInlineAsm *source) {
    return source != NULL &&
        minic_loongarch64_descriptor_template_requires_typed_id(
            source->template_text, source->template_length);
}

static bool loong_linux_descriptor_qualify(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    uint32_t id;
    bool accepted;
    if (source == NULL || contract == NULL) return false;
    /* The machine descriptor recognizer is the single template oracle. C
     * admission only dispatches the matching stable family to its source
     * type/constraint validator; there is no second form parser or table. */
    for (id = LOONG_DESC_CACOP; id <= LOONG_DESC_PTE; ++id) {
        if (!minic_loongarch64_typed_descriptor_matches_template(
                id, 0U, source->template_text, source->template_length))
            continue;
        switch (id) {
        case LOONG_DESC_CACOP: accepted = loong_linux_cacop_source(program, source, contract); break;
        case LOONG_DESC_VDSO: accepted = loong_linux_vdso_source(program, source, contract); break;
        case LOONG_DESC_FRAMETRACE: accepted = loong_linux_frametrace_source(program, source, contract); break;
        case LOONG_DESC_FRAMETRACE_CSR: accepted = loong_linux_frametrace_csr_source(program, source, contract); break;
        case LOONG_DESC_CPUCFG: accepted = loong_linux_cpucfg_source(program, source, contract); break;
        case LOONG_DESC_FPR_TRANSFER: accepted = loong_linux_fpr_transfer_source(program, source, contract); break;
        case LOONG_DESC_FUTEX_CMPXCHG: accepted = loong_linux_futex_cmpxchg_source(program, source, contract); break;
        case LOONG_DESC_FCSR0_WRITE: accepted = loong_fcsr0_write_source(program, source, contract); break;
        case LOONG_DESC_PERCPU_INDEXED: accepted = loong_percpu_indexed_source(program, source, contract); break;
        case LOONG_DESC_CMPXCHG: accepted = loong_linux_cmpxchg_source(program, source, contract); break;
        case LOONG_DESC_XCHG: accepted = loong_linux_xchg_source(program, source, contract); break;
        case LOONG_DESC_XCHG_SMALL: accepted = loong_linux_xchg_small_source(program, source, contract); break;
        case LOONG_DESC_MASK_NOSPEC: accepted = loong_mask_nospec_source(program, source, contract); break;
        case LOONG_DESC_PTE: accepted = loong_pte_source(program, source, contract); break;
        default: return false;
        }
        if (!accepted) return false;
        contract->target_machine_form_id = id;
        contract->target_machine_form_variant = 0U;
        return true;
    }
    return false;
}
