#include "target/aarch64/asm_contract_source.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/data_layout_source.h"
#include "target/common/linux_jump_label_source.h"
#include "target/common/direct_linux_bug_asm.h"
#include "frontend/ast.h"
#include "frontend/const_eval.h"
#include <string.h>

static bool constraint(const MinicInlineAsmOperand *op, const char *word);

bool minic_aarch64_clone_safe_opaque_asm(const MinicInlineAsm *source) {
    MinicCoreInlineAsm form;
    if (source != NULL && source->is_volatile && !source->is_goto &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->clobber_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        /* An empty GNU asm volatile statement has no target instructions
         * or symbol definitions, but remains a compiler-ordered effect in
         * each specialization. */
        return true;
    }
    if (source != NULL && !source->is_volatile && !source->is_goto &&
        source->template_length == 0U && source->output_count == 1U &&
        source->input_count == 1U && source->label_count == 0U &&
        source->clobber_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber &&
        source->outputs != NULL && source->inputs != NULL &&
        source->outputs[0].constraint_text != NULL &&
        source->inputs[0].constraint_text != NULL &&
        source->outputs[0].constraint_length == 2U &&
        memcmp(source->outputs[0].constraint_text, "=r", 2U) == 0 &&
        source->inputs[0].constraint_length == 1U &&
        source->inputs[0].constraint_text[0] == '0') {
        /* Linux's RELOC_HIDE() barrier (`asm("" : "=r"(dst) : "0"(src))`)
         * is a register-only identity: a clone has no assembler labels,
         * instructions or externally observable machine effects to duplicate.
         * Preserve both operands in each cloned function; this is not a
         * license to clone arbitrary asm with outputs or symbolic labels. */
        return true;
    }
    if (source == NULL || !source->is_volatile || source->is_goto ||
        source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->clobber_count != 0U ||
        source->register_clobber_count != 0U || source->has_memory_clobber) {
        return false;
    }
    /* A clone emits the *whole* opaque Core ASM effect independently. The
     * existing machine qualifier proves this exact Linux BUG table's 14470,
     * 14471f and 14472b numeric labels are defined within that one template,
     * its section stack balances, and no symbolic/global labels escape.
     * All other ASM, even another valid zero-operand instruction, stays
     * blocked for canonical static specialization. */
    memset(&form, 0, sizeof(form));
    form.template_text = source->template_text;
    form.template_length = source->template_length;
    form.is_volatile = source->is_volatile;
    return minic_multi_direct_linux_bug_asm(
        &form, "14470", "14471", "14472", "brk0x800", true);
}

bool minic_aarch64_qualify_inline_asm_goto(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *source) {
    if (invocation == NULL) return false;
    return minic_multi_qualify_linux_jump_label_asm_goto(
        program, source, aarch64_linux_jump_label_form);
}

bool minic_aarch64_linux_cap_asm_goto_source(const MinicInlineAsm *s) {
    const bool callback_form = s != NULL &&
        aarch64_linux_cap_goto_text(s->template_text, s->template_length,
                                    true, true);
    return s != NULL && s->is_goto && !s->is_volatile &&
           s->output_count == 0U && s->input_count == 1U &&
           s->inputs != NULL && s->label_count == 1U && s->labels != NULL &&
           s->clobber_count == 0U && !s->has_memory_clobber &&
           s->register_clobber_count == 0U &&
           s->inputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
           s->inputs[0].constraint_text != NULL &&
           s->inputs[0].constraint_length == 1U &&
           s->inputs[0].constraint_text[0] == 'i' &&
           s->inputs[0].name != NULL &&
           s->inputs[0].name_length == sizeof("cpucap") - 1U &&
           memcmp(s->inputs[0].name, "cpucap", sizeof("cpucap") - 1U) == 0 &&
           s->labels[0].name != NULL &&
           s->labels[0].name_length ==
               (callback_form ? sizeof("l_no") - 1U : sizeof("l_yes") - 1U) &&
           memcmp(s->labels[0].name, callback_form ? "l_no" : "l_yes",
                  s->labels[0].name_length) == 0 &&
           (callback_form || aarch64_linux_cap_goto_text(
               s->template_text, s->template_length, true, false));
}

static bool aarch64_linux_sysreg_u64_type(MinicType type) {
    return minic_type_equal(type, minic_type_unsigned_long()) ||
           minic_type_equal(type, minic_type_unsigned_long_long());
}

static bool aarch64_stlr_type(MinicType type, size_t width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain) ||
        plain.base_kind != MINIC_TYPE_BASE_INT) return false;
    return (width == 1U && plain.integer_rank == MINIC_INTEGER_RANK_CHAR) ||
           (width == 2U && plain.integer_rank == MINIC_INTEGER_RANK_SHORT) ||
           (width == 4U && plain.integer_rank == MINIC_INTEGER_RANK_INT) ||
           (width == 8U && (plain.integer_rank == MINIC_INTEGER_RANK_LONG ||
                            plain.integer_rank == MINIC_INTEGER_RANK_LONG_LONG));
}

static bool aarch64_stlr_scalar_source(
    const MinicC0Program *program, MinicType type, size_t width) {
    MinicType effective;
    return aarch64_stlr_type(type, width) ||
        (program && minic_type_is_enum(type) &&
         minic_c0_type_effective_integer_type(program, type, &effective) &&
         aarch64_stlr_type(effective, width));
}

static bool aarch64_stlr_memory_type(MinicType type,size_t width){
    MinicType plain;
    return aarch64_stlr_type(type,width)||
        (width==1U&&minic_type_unqualified(type,&plain)&&
         minic_type_is_bool_integer(plain))||
        (width==8U&&minic_type_unqualified(type,&plain)&&
         minic_type_is_pointer(plain));
}

static bool aarch64_stlr_memory_carrier(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, MinicType type, size_t width) {
    MinicType plain;
    size_t size, alignment;
    return aarch64_stlr_memory_type(type, width) ||
           (program && minic_type_unqualified(type, &plain) &&
            minic_type_is_record(plain) &&
            minic_data_layout_type(invocation, program,
                                   plain, &size, &alignment) &&
            size == width && alignment <= width);
}

bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const fixed[] = {
        "brk", "stlrb", "stlrh", "stlr", "ldarb", "ldarh", "ldar",
        "msr", "svc", "bic", ".if", ".irp", "dc", "ic",
        /* GNU .irp SYSREG metadata and D/I-cache operations are ISA-owned.
         * A rejected constraint must not reach the generic Core ASM path. */
        "strb", "strh", "str",
        /* Original Linux CMPXCHG begins with PRFM and named operands;
         * rejected constraints/modes must not escape into generic Core. */
        "prfm", "1:", ".arch_extension lse", "rbit"
    };
    char prefix[64];
    const char *const single[] = {prefix};
    size_t width, fetch, op;
    /* The exact Linux one-label alternative is a qualified *control-flow*
     * shape handled by Core's generic asm-goto lowering. Its target-owned
     * section/ISA contract is checked again at machine admission. */
    if (minic_aarch64_linux_cap_asm_goto_source(src)) return false;
    /* Exact Linux asm-goto table records are not ordinary scalar ASM input.
     * Core independently re-verifies the complete source/operand contract. */
    if (src != NULL && src->is_goto &&
        aarch64_linux_jump_label_form(src->template_text,
                                      src->template_length) != 0U) return false;
    if (minic_source_backend_inline_asm_prefix_matches(
            src, fixed, sizeof(fixed) / sizeof(fixed[0])))
        return true;
    for (width = 0U; width < 2U; ++width)
        for (fetch = 0U; fetch < 2U; ++fetch)
            for (op = 0U; op < sizeof(aarch64_atomic_operations) /
                                  sizeof(aarch64_atomic_operations[0]); ++op) {
                const int n = snprintf(prefix, sizeof(prefix),
                    "// atomic%s%s_%s", width ? "64" : "",
                    fetch ? "_fetch" : "", aarch64_atomic_operations[op].name);
                if (n <= 0 || (size_t)n >= sizeof(prefix))
                    return false;
                if (minic_source_backend_inline_asm_prefix_matches(src, single, 1U))
                    return true;
            }
    return false;
}

static bool aarch64_linux_kvm_va_mask_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_kvm_va_mask_form(
            src->template_text, src->template_length) ||
        !src->outputs || src->output_count != 1U || src->input_count ||
        src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !constraint(&src->outputs[0], "+r")) return false;
    expr = minic_c0_program_expression(program,src->outputs[0].expression);
    if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expr->type) ||
        !minic_type_equal(expr->type,minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    return true;
}

static bool aarch64_linux_kvm_at_fixup_source(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const output_constraint[] = {"+r", "=&r", "=&r"};
    static const char *const input_constraint[] = {"r", "i"};
    MinicConstValue immediate;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !src->outputs || !src->inputs || src->output_count != 3U ||
        src->input_count != 2U || src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !aarch64_linux_kvm_at_fixup_template(
            src->template_text, src->template_length)) return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i < 3U;
        const MinicInlineAsmOperand *operand = output ?
            &src->outputs[i] : &src->inputs[i - 3U];
        const MinicExpression *expr = minic_c0_program_expression(
            program, operand->expression);
        MinicType plain;
        if (!constraint(operand, output ? output_constraint[i] :
                        input_constraint[i - 3U]) || !expr ||
            !minic_type_unqualified(expr->type, &plain) ||
            (output && (expr->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expr->type))) ||
            (i == 0U || i == 4U ?
                !aarch64_stlr_type(plain, 4U) :
                !aarch64_linux_sysreg_u64_type(plain))) return false;
        contract->kinds[i] = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    /* The `i` operand in Linux's hyp exception fixup is -EFAULT (-14).
     * Do not turn any run-time scalar into a GNU compile-time immediate. */
    if (!invocation || !minic_const_eval_integer(
            program, invocation, src->inputs[1].expression, &immediate) ||
        (uint32_t)immediate.bits != UINT32_C(0xfffffff2)) return false;
    contract->operand_count = 5U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_at_s1e1r_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 1U ||
        !source->inputs || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !constraint(&source->inputs[0], "r") ||
        !aarch64_linux_at_s1e1r_template(
            source->template_text, source->template_length))
        return false;
    address = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!address || !minic_type_unqualified(address->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool constraint(const MinicInlineAsmOperand *op, const char *word) {
    size_t n = strlen(word);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, word, n) == 0;
}

static bool aarch64_linux_io_dependency_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract){
    const MinicExpression *dst,*arg;
    MinicType plain;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !src->outputs||!src->inputs||src->output_count!=1U||
       src->input_count!=1U||src->label_count!=0U||
       !src->has_memory_clobber||src->clobber_count!=1U||
       src->register_clobber_count!=0U||
       !aarch64_linux_io_dependency_form(
           src->template_text,src->template_length)||
       !constraint(&src->outputs[0],"=r")||
       !constraint(&src->inputs[0],"r"))return false;
    dst=minic_c0_program_expression(program,src->outputs[0].expression);
    arg=minic_c0_program_expression(program,src->inputs[0].expression);
    if(!dst||!arg||dst->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(dst->type)||
       !minic_type_unqualified(dst->type,&plain)||
       !minic_type_equal(plain,minic_type_unsigned_long())||
       !minic_type_unqualified(arg->type,&plain)||
       !minic_type_equal(plain,minic_type_unsigned_long()))return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_encoded_sysreg_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst;
    MinicType plain;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !src->outputs||src->output_count!=1U||src->input_count!=0U||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||!constraint(&src->outputs[0],"=r")||
       !aarch64_linux_encoded_sysreg_form(
           src->template_text,src->template_length))return false;
    dst=minic_c0_program_expression(program,src->outputs[0].expression);
    if(!dst||dst->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(dst->type)||
       !minic_type_unqualified(dst->type,&plain)||
       !aarch64_linux_sysreg_u64_type(plain))return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_encoded_sysreg_write_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || src->output_count != 0U || src->input_count != 1U ||
        !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count || src->has_memory_clobber ||
        src->label_count != 0U || !constraint(&src->inputs[0], "rZ") ||
        !aarch64_linux_encoded_sysreg_write_form(
            src->template_text, src->template_length)) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_vhe_sysreg_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64LinuxVheSysregAlternative form;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *value;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->label_count || src->clobber_count || src->register_clobber_count ||
        src->has_memory_clobber ||
        !aarch64_linux_vhe_sysreg_alternative_form(
            src->template_text, src->template_length, &form) ||
        src->output_count != (form.is_read ? 1U : 0U) ||
        src->input_count != (form.is_read ? 0U : 1U)) return false;
    operand = form.is_read ? src->outputs : src->inputs;
    if (!operand || !constraint(operand, form.is_read ? "=r" : "rZ"))
        return false;
    value = minic_c0_program_expression(program, operand->expression);
    if (!value || (form.is_read &&
        (value->value_category != MINIC_VALUE_LVALUE ||
         minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !aarch64_linux_sysreg_u64_type(plain)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form.is_read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_tlbi_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form=src?aarch64_linux_tlbi_form(
        src->template_text,src->template_length):0U;
    const bool has_address=form>=1U&&
        form<=AARCH64_LINUX_TLBI_ADDR_FORM_COUNT;
    const MinicExpression *arg;
    if(!program||!src||!contract||!form||src->is_goto||
       src->output_count!=0U||src->input_count!=(has_address?1U:0U)||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       (has_address&&(!src->inputs||!constraint(&src->inputs[0],"rZ"))))
        return false;
    if(has_address){
        arg=minic_c0_program_expression(program,src->inputs[0].expression);
        if(!arg||!minic_type_is_integer(arg->type)||
           minic_type_is_int128_integer(arg->type))return false;
        contract->operand_count=1U;
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }else contract->operand_count=0U;
    /* Original GNU asm without output is implicitly volatile even if not
     * spelled volatile. Hardware TLB invalidation must not be DCE'd. */
    contract->implicit_memory_effect=true;
    return true;
}

static bool aarch64_linux_hyp_spin_type(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, MinicType type,
    size_t width, bool record) {
    size_t size, alignment;
    MinicType plain;
    return program && minic_type_unqualified(type, &plain) &&
           (record ? minic_type_is_record(plain) :
                     minic_type_is_unsigned_integer(plain)) &&
           minic_data_layout_type(invocation, program,
                                  plain, &size, &alignment) && size == width;
}

static bool aarch64_linux_hyp_spin_source(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const Aarch64LinuxHypSpinForm form = src ? aarch64_linux_hyp_spin_form(
        src->template_text, src->template_length) : AARCH64_LINUX_HYP_SPIN_NONE;
    const bool lock = form == AARCH64_LINUX_HYP_SPIN_LOCK;
    size_t i, count;
    if (!program || !src || !contract || form == AARCH64_LINUX_HYP_SPIN_NONE ||
        !src->is_volatile || src->is_goto || src->label_count ||
        src->clobber_count != 1U || !src->has_memory_clobber ||
        src->register_clobber_count || !src->outputs ||
        src->output_count != (lock ? 4U : 2U) ||
        src->input_count != (lock ? 1U : 0U) ||
        (lock && !src->inputs)) return false;
    count = src->output_count + src->input_count;
    for (i = 0U; i < count; ++i) {
        const bool output = i < src->output_count;
        const MinicInlineAsmOperand *operand = output ? &src->outputs[i] :
            &src->inputs[i - src->output_count];
        const MinicExpression *expr = minic_c0_program_expression(
            program, operand->expression);
        const char *expected = lock ?
            (i < 3U ? "=&r" : i == 3U ? "+Q" : "Q") :
            (i == 0U ? "=Q" : "=&r");
        const size_t width = lock ? (i == 4U ? 2U : 4U) :
            (i == 0U ? 2U : 8U);
        const bool record = lock && (i == 0U || i == 1U || i == 3U);
        if (!constraint(operand, expected) || !expr ||
            expr->value_category != MINIC_VALUE_LVALUE ||
            (output && minic_type_is_const(expr->type)) ||
            !aarch64_linux_hyp_spin_type(invocation, program, expr->type,
                                         width, record)) return false;
        /* GNU can return a 32-bit union through one GPR; Core's ordinary
         * register-output kind assumes scalar pointees.  Transport this
         * source-verified union via an addressable output, then this target
         * alone assigns w9/w10 and stores their full bit patterns. */
        contract->kinds[i] = lock ?
            (i < 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
             i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
             i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
            (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT);
    }
    contract->operand_count = count;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_zeropad_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *result, *base, *memory;
    MinicType result_type, memory_type;
    if (!program || !src || !contract || src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U ||
        !constraint(&src->outputs[0], "=&r") ||
        !constraint(&src->inputs[0], "r") ||
        !constraint(&src->inputs[1], "Q") ||
        !aarch64_linux_zeropad_form(
            src->template_text, src->template_length)) return false;
    result = minic_c0_program_expression(
        program, src->outputs[0].expression);
    base = minic_c0_program_expression(
        program, src->inputs[0].expression);
    memory = minic_c0_program_expression(
        program, src->inputs[1].expression);
    if (!result || !base || !memory ||
        result->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(result->type) ||
        !minic_type_unqualified(result->type, &result_type) ||
        !minic_type_unqualified(memory->type, &memory_type) ||
        !minic_type_equal(result_type, minic_type_unsigned_long()) ||
        !minic_type_equal(memory_type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(base->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    /* A faulting load is a hardware memory read even without an explicit
     * GNU "memory" clobber. Keep ordering conservative in Core. */
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_uaccess_source(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64UaccessForm form;
    const MinicExpression *err, *data, *pointer;
    MinicType err_type, data_type;
    size_t data_bytes, data_alignment;

    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        (src->output_count != 1U && src->output_count != 2U) ||
        src->output_count + src->input_count != 3U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U ||
        !aarch64_linux_uaccess_form(src->template_text,
                                   src->template_length, &form))
        return false;
    if (src->output_count != (form.is_load ? 2U : 1U) ||
        !constraint(&src->outputs[0], "+r") ||
        !constraint(form.is_load ? &src->outputs[1] : &src->inputs[0],
                    form.is_load ? "=r" : "rZ") ||
        !constraint(&src->inputs[form.is_load ? 0U : 1U], "r"))
        return false;
    err = minic_c0_program_expression(program, src->outputs[0].expression);
    data = minic_c0_program_expression(
        program, form.is_load ? src->outputs[1].expression : src->inputs[0].expression);
    pointer = minic_c0_program_expression(
        program, src->inputs[form.is_load ? 0U : 1U].expression);
    if (!err || !data || !pointer ||
        err->value_category != MINIC_VALUE_LVALUE ||
        (form.is_load && data->value_category != MINIC_VALUE_LVALUE) ||
        minic_type_is_const(err->type) ||
        (form.is_load && minic_type_is_const(data->type)) ||
        !minic_type_unqualified(err->type, &err_type) ||
        !minic_type_unqualified(data->type, &data_type) ||
        !minic_type_equal(err_type, minic_type_int()) ||
        (!minic_type_is_integer(data_type) &&
         !(form.width == 8U && minic_type_is_pointer(data_type))) ||
        !minic_data_layout_type(invocation, program,
                                data_type, &data_bytes, &data_alignment) ||
        (data_bytes != 1U && data_bytes != 2U &&
         data_bytes != 4U && data_bytes != 8U) ||
        !minic_type_is_pointer(pointer->type))
        return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = form.is_load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_fixed_gpr_local_binding(
    const MinicC0Program *program, const MinicExpression *e,
    unsigned int register_number, bool allow_pointer,
    bool allow_const_input) {
    const MinicFixedRegisterBinding *binding;
    const MinicLocal *local;
    MinicType plain;
    if (!program || !e || e->kind != MINIC_EXPRESSION_LOCAL ||
        e->value_category != MINIC_VALUE_LVALUE ||
        (!allow_const_input && minic_type_is_const(e->type)) ||
        minic_type_is_volatile(e->type))
        return false;
    local = minic_c0_program_local(program, e->value.local_id);
    binding = minic_c0_program_local_fixed_register_binding(
        program, e->value.local_id);
    if (!local || !binding ||
        minic_c0_local_array_object_info(program, e->value.local_id, NULL) ||
        !minic_type_equal(local->type, e->type) ||
        !minic_type_equal(binding->type, e->type) ||
        !minic_type_unqualified(e->type, &plain) ||
        binding->register_name_length != 2U ||
        (binding->register_name[0] != 'r' &&
         binding->register_name[0] != 'x') ||
        binding->register_name[1] != (char)('0' + register_number))
        return false;
    return (allow_pointer && minic_type_is_pointer(plain)) ||
           minic_type_is_enum(plain) ||
           (minic_type_is_integer(plain) &&
           (minic_type_equal(plain, minic_type_unsigned_long()) ||
            minic_type_equal(plain, minic_type_long()) ||
            minic_type_equal(plain, minic_type_unsigned_long_long()) ||
            minic_type_equal(plain, minic_type_long_long()) ||
            minic_type_equal(plain, minic_type_unsigned_int()) ||
            minic_type_equal(plain, minic_type_int())));
}

static bool aarch64_linux_xpaclri_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicInlineAsmOperand *op;
    const MinicExpression *expr;
    const MinicFixedRegisterBinding *fixed;
    const MinicLocal *local;
    if (!program || !src || !contract || src->is_goto ||
        !aarch64_linux_xpaclri_form(src->template_text, src->template_length) ||
        src->output_count != 1U || !src->outputs || src->input_count != 0U ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber)
        return false;
    op = &src->outputs[0];
    if (!constraint(op, "+r") || op->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)
        return false;
    expr = minic_c0_program_expression(program, op->expression);
    if (!expr || expr->kind != MINIC_EXPRESSION_LOCAL ||
        expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(expr->type, minic_type_unsigned_long()) ||
        minic_type_is_const(expr->type) || minic_type_is_volatile(expr->type))
        return false;
    local = minic_c0_program_local(program, expr->value.local_id);
    fixed = minic_c0_program_local_fixed_register_binding(
        program, expr->value.local_id);
    if (!local || !fixed ||
        minic_c0_local_array_object_info(program, expr->value.local_id, NULL) ||
        !minic_type_equal(local->type, expr->type) ||
        !minic_type_equal(fixed->type, expr->type) ||
        fixed->register_name_length != 3U ||
        memcmp(fixed->register_name, "x30", 3U) != 0)
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    return true;
}

static bool aarch64_linux_smccc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!program || !src || !contract ||
        !aarch64_linux_smccc_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 4U ||
        src->input_count == 0U || src->input_count > 8U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < src->output_count; ++i) {
        const MinicInlineAsmOperand *op = &src->outputs[i];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        if (!constraint(op, "=r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, e,
                                                  (unsigned int)i, false, false) ||
            !minic_type_equal(e->type, minic_type_unsigned_long()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    for (i = 0U; i < src->input_count; ++i) {
        const MinicInlineAsmOperand *op = &src->inputs[i];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        if (!constraint(op, "r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, e,
                                                  (unsigned int)i, true, true))
            return false;
        contract->kinds[4U + i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U + src->input_count;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_svc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const unsigned int registers[] = {0U, 1U, 8U};
    size_t i;
    const MinicExpression *ret;
    if (!program || !src || !contract ||
        !aarch64_linux_svc_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto || src->output_count != 1U ||
        src->input_count != 3U || !src->outputs || !src->inputs ||
        src->label_count != 0U || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || !src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=r")) return false;
    ret = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!aarch64_linux_fixed_gpr_local_binding(program, ret, 0U, false, false) ||
        !minic_type_equal(ret->type, minic_type_long())) return false;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[i].expression);
        if (!constraint(&src->inputs[i], "r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, arg,
                                                   registers[i], true, false) ||
            (i == 2U && !minic_type_equal(arg->type, minic_type_long())))
            return false;
        contract->kinds[i + 1U] =
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_wfxt_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    if (!program||!src||!contract||!src->is_volatile||src->is_goto||
        !aarch64_linux_wfxt_form(src->template_text,src->template_length)||
        src->output_count!=0U||src->input_count!=1U||!src->inputs||
        src->label_count||src->clobber_count!=1U||
        !src->has_memory_clobber||src->register_clobber_count||
        !constraint(&src->inputs[0],"r"))return false;
    value=minic_c0_program_expression(program,src->inputs[0].expression);
    if(!value||!aarch64_stlr_type(value->type,8U))return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect=true;
    return true;
}

static bool aarch64_linux_atomic_dec_positive_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != 3U || !src->outputs || src->input_count != 0U ||
        src->label_count != 0U || src->clobber_count != 2U ||
        !src->has_memory_clobber || src->register_clobber_count != 1U ||
        !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "=&r") ||
        !constraint(&src->outputs[1], "=&r") ||
        !constraint(&src->outputs[2], "+Q") ||
        !aarch64_linux_atomic_dec_positive_form(
            src->template_text, src->template_length)) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *e = minic_c0_program_expression(
            program, src->outputs[i].expression);
        if (!e || e->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(e->type) ||
            !aarch64_stlr_type(e->type, 8U)) return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}

static bool aarch64_linux_cmpxchg128_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int mode = src ? aarch64_linux_cmpxchg128_form(
        src->template_text, src->template_length) : 0U;
    size_t i;
    if (!program || !src || !contract || !mode || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 4U || src->input_count != 4U ||
        src->label_count != 0U ||
        src->clobber_count != (mode == 1U ? 1U : 2U) ||
        src->has_memory_clobber != (mode == 2U) ||
        src->register_clobber_count != 1U || !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "+Q")) return false;
    for (i = 0U; i < 8U; ++i) {
        const MinicInlineAsmOperand *operand = i < 4U
            ? &src->outputs[i] : &src->inputs[i - 4U];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (i > 0U && i < 4U &&
            !constraint(operand, "=&r")) return false;
        if (i >= 4U && !constraint(operand, "r")) return false;
        if (!e || (i < 4U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            (i == 0U ? !minic_type_is_int128_integer(e->type)
             : i == 3U ? !aarch64_stlr_type(e->type, 4U)
             : !aarch64_stlr_type(e->type, 8U))) return false;
    }
    contract->operand_count = 8U;
    for (i = 0U; i < 8U; ++i)
        contract->kinds[i] = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_lse_casp128_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    unsigned int mode;
    size_t i;
    if (!program || !src || !contract ||
        !(mode = aarch64_linux_lse_casp128_form(
            src->template_text, src->template_length)) ||
        !src->is_volatile || src->is_goto || src->output_count != 3U ||
        src->input_count != 5U || !src->outputs || !src->inputs ||
        src->label_count || src->register_clobber_count ||
        src->clobber_count != (mode == 2U ? 1U : 0U) ||
        src->has_memory_clobber != (mode == 2U) ||
        !constraint(&src->outputs[0], "+&r") ||
        !constraint(&src->outputs[1], "+&r") ||
        !constraint(&src->outputs[2], "+Q")) return false;
    for (i = 0U; i < 8U; ++i) {
        const bool is_output = i < 3U;
        const MinicInlineAsmOperand *operand = is_output
            ? &src->outputs[i] : &src->inputs[i - 3U];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (!e || (is_output &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            (i == 2U ? !minic_type_is_int128_integer(e->type)
                     : !aarch64_stlr_type(e->type, 8U))) return false;
        if (i >= 3U && !constraint(operand, "r")) return false;
        if ((i < 2U || (i >= 3U && i <= 5U)) &&
            !aarch64_linux_fixed_gpr_local_binding(
                program, e, (unsigned int)(i < 2U ? i : i - 1U),
                false, false))
            return false;
        contract->kinds[i] = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i < 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                     : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 8U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_lse_dec_positive_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *ret, *memory, *tmp;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_lse_dec_positive_form(
            src->template_text, src->template_length) ||
        !src->outputs || src->output_count != 3U || src->input_count ||
        src->label_count || src->clobber_count != 2U ||
        !src->has_memory_clobber || src->register_clobber_count != 1U ||
        !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "+&r") ||
        !constraint(&src->outputs[1], "+Q") ||
        !constraint(&src->outputs[2], "=&r")) return false;
    ret = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->outputs[1].expression);
    tmp = minic_c0_program_expression(program, src->outputs[2].expression);
    if (!ret || !memory || !tmp ||
        ret->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        tmp->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(ret->type) ||
        minic_type_is_const(memory->type) ||
        minic_type_is_const(tmp->type) ||
        !minic_type_is_pointer(ret->type) ||
        !aarch64_stlr_type(memory->type, 8U) ||
        !aarch64_stlr_type(tmp->type, 8U)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_futex_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int mode = src ? aarch64_linux_futex_form(
        src->template_text, src->template_length) : 0U;
    const size_t operand_count = mode == 6U ? 8U : 7U;
    size_t i;
    if (!program || !src || !contract || !mode || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 5U ||
        src->input_count != operand_count - 5U ||
        src->label_count != 0U || src->clobber_count != 1U ||
        !src->has_memory_clobber || src->register_clobber_count != 0U ||
        !constraint(&src->outputs[0], mode == 6U ? "+r" : "=&r") ||
        !constraint(&src->outputs[1], "=&r") ||
        !constraint(&src->outputs[2], "+Q") ||
        !constraint(&src->outputs[3], "=&r") ||
        !constraint(&src->outputs[4], "+r")) return false;
    for (i = 0U; i < operand_count; ++i) {
        const MinicInlineAsmOperand *operand = i < 5U
            ? &src->outputs[i] : &src->inputs[i - 5U];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (i >= 5U && !constraint(operand,
                i == operand_count - 1U ? "Ir" : "r")) return false;
        if (!e || (i < 5U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            !aarch64_stlr_type(e->type, 4U)) return false;
    }
    contract->operand_count = operand_count;
    for (i = 0U; i < operand_count; ++i)
        contract->kinds[i] = i == 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            (i == 0U && mode == 6U) || i == 4U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            i < 5U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_disabled_hw_pan_source(
    const MinicInlineAsm *src, MinicTargetInlineAsmContract *contract) {
    if (!src || !contract || src->is_volatile || src->is_goto ||
        src->output_count || src->input_count || src->label_count ||
        src->clobber_count || src->has_memory_clobber ||
        src->register_clobber_count ||
        !aarch64_linux_disabled_hw_pan_form(
            src->template_text, src->template_length)) return false;
    contract->operand_count = 0U;
    return true;
}

static bool aarch64_linux_timer_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *result;
    if (!program || !src || !contract ||
        !aarch64_linux_timer_alternative_form(
            src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 0U ||
        !src->outputs || src->label_count != 0U ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        src->register_clobber_count != 0U ||
        !constraint(&src->outputs[0], "=r"))
        return false;
    result = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!result || result->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(result->type) ||
        !aarch64_linux_sysreg_u64_type(result->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_counter_ordering_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *out, *input;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_counter_ordering_form(
            src->template_text, src->template_length) ||
        src->output_count != 1U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U || !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r")) return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    input = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!out || !input || out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) ||
        !aarch64_linux_sysreg_u64_type(out->type) ||
        !aarch64_linux_sysreg_u64_type(input->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_rng_sysreg_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *val, *ok;
    if (!program || !src || !contract ||
        !aarch64_linux_rng_sysreg_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U || src->input_count != 0U ||
        src->label_count != 0U || src->has_memory_clobber ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->outputs[1], "=r")) return false;
    val = minic_c0_program_expression(program, src->outputs[0].expression);
    ok = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!val || !ok || val->value_category != MINIC_VALUE_LVALUE ||
        ok->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(val->type) || minic_type_is_const(ok->type) ||
        !minic_type_equal(val->type, minic_type_unsigned_long()) ||
        !minic_type_equal(ok->type, minic_type_bool())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_qcom_return_stack_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *tmp;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_qcom_return_stack_form(
            src->template_text, src->template_length) ||
        src->output_count != 1U || src->input_count != 0U || !src->outputs ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=&r")) return false;
    tmp = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!tmp || tmp->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(tmp->type) ||
        !aarch64_linux_sysreg_u64_type(tmp->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_cache_op_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address;
    const char *mnemonic;
    MinicType plain;
    if (!program || !src || !contract ||
        src->is_goto || src->output_count != 0U || src->input_count != 1U ||
        !src->inputs || src->register_clobber_count != 0U ||
        src->label_count != 0U || !constraint(&src->inputs[0], "r"))
        return false;
    mnemonic = aarch64_linux_cache_op_form(
        src->template_text, src->template_length);
    if (!mnemonic ||
        !((src->is_volatile && src->clobber_count == 1U &&
           src->has_memory_clobber) ||
          (src->clobber_count == 0U && !src->has_memory_clobber &&
           aarch64_linux_efi_unclobbered_cache_form(mnemonic))))
        return false;
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!address || !minic_type_unqualified(address->type, &plain) ||
        (!minic_type_is_pointer(plain) &&
         !minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_user_cache_fault_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *status, *address;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != 1U || !src->outputs ||
        src->input_count != 1U || !src->inputs ||
        src->label_count || src->clobber_count ||
        src->has_memory_clobber || src->register_clobber_count ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r") ||
        !aarch64_linux_user_cache_fault_form(
            src->template_text, src->template_length))
        return false;
    status = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!status || !address ||
        status->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(status->type) ||
        !minic_type_equal(status->type, minic_type_int()) ||
        !minic_type_unqualified(address->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_is_pointer(plain)))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_bitrev_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst, *arg;
    static const char exact[] = "rbit %w0, %w1";
    if (!program || !src || !contract || src->is_volatile || src->is_goto ||
        src->template_length != sizeof(exact) - 1U ||
        memcmp(src->template_text, exact, sizeof(exact) - 1U) != 0 ||
        !src->outputs || !src->inputs || src->output_count != 1U ||
        src->input_count != 1U || src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r"))
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    arg = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!dst || !arg || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !aarch64_stlr_type(dst->type, 4U) ||
        !aarch64_stlr_type(arg->type, 4U))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_lse_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64LseForm form;
    size_t i, count;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->label_count || src->register_clobber_count || !src->outputs ||
        !src->inputs ||
        !aarch64_lse_form(src->template_text, src->template_length,
                          false, &form))
        return false;
    count = form.kind == AARCH64_LSE_STORE ? 2U : 3U;
    if (src->output_count != count - 1U || src->input_count != 1U ||
        src->clobber_count != (form.mode != 0U ? 1U : 0U) ||
        src->has_memory_clobber != (form.mode != 0U) ||
        !constraint(&src->outputs[0], "+Q") ||
        (form.kind == AARCH64_LSE_FETCH &&
         !constraint(&src->outputs[1], "=r")) ||
        (form.kind == AARCH64_LSE_CAS &&
         !constraint(&src->outputs[1], "+r")) ||
        !constraint(&src->inputs[0],
                    form.kind == AARCH64_LSE_CAS ? "rZ" : "r"))
        return false;
    for (i = 0U; i < count; ++i) {
        const bool output = i < src->output_count;
        const MinicInlineAsmOperand *op = output ? &src->outputs[i] :
            &src->inputs[i - src->output_count];
        const MinicExpression *value = minic_c0_program_expression(
            program, op->expression);
        if (!value || (output &&
            (value->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(value->type))) ||
            !aarch64_stlr_type(value->type, form.width))
            return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : output
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (form.kind == AARCH64_LSE_FETCH && i == 1U)
            contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    contract->operand_count = count;
    return true;
}

bool minic_aarch64_qualify_inline_asm(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    MinicType plain;
    size_t i;
    if (invocation == NULL) return false;
    /* AArch64 admits the GNU source roles only with exact types and
     * the original exclusive-load control-flow template. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count==2U &&
        src->input_count==1U && src->clobber_count==0U &&
        src->register_clobber_count==0U && !src->has_memory_clobber &&
        src->label_count==0U) {
        const unsigned int width=aarch64_linux_cmpwait_form(
            src->template_text,src->template_length);
        if (width!=0U && constraint(&src->outputs[0],"=&r") &&
            constraint(&src->outputs[1],"+Q") &&
            constraint(&src->inputs[0],"r")) {
            const MinicExpression *tmp=minic_c0_program_expression(
                program,src->outputs[0].expression);
            const MinicExpression *memory=minic_c0_program_expression(
                program,src->outputs[1].expression);
            const MinicExpression *value=minic_c0_program_expression(
                program,src->inputs[0].expression);
            if (tmp && memory && value &&
                tmp->value_category==MINIC_VALUE_LVALUE &&
                memory->value_category==MINIC_VALUE_LVALUE &&
                !minic_type_is_const(tmp->type) &&
                !minic_type_is_const(memory->type) &&
                aarch64_linux_sysreg_u64_type(tmp->type) &&
                aarch64_linux_sysreg_u64_type(value->type) &&
                aarch64_stlr_type(memory->type,width)) {
                contract->operand_count=3U;
                contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                return true;
            }
        }
    }
    if(aarch64_linux_bitrev_source(program,src,contract))return true;
    if(aarch64_linux_kvm_va_mask_source(program,src,contract))return true;
    if(aarch64_linux_hyp_spin_source(invocation, program,src,contract))return true;
    if(aarch64_linux_lse_source(program,src,contract))return true;
    if(aarch64_linux_lse_casp128_source(program,src,contract))return true;
    if(aarch64_linux_lse_dec_positive_source(program,src,contract))return true;
    if(aarch64_linux_xpaclri_source(program,src,contract))return true;
    if(aarch64_linux_smccc_source(program,src,contract))return true;
    if(aarch64_linux_svc_source(program,src,contract))return true;
    if(aarch64_linux_wfxt_source(program,src,contract))return true;
    if(aarch64_linux_atomic_dec_positive_source(program,src,contract))return true;
    if(aarch64_linux_cmpxchg128_source(program,src,contract))return true;
    if(aarch64_linux_futex_source(program,src,contract))return true;
    if(aarch64_linux_disabled_hw_pan_source(src,contract))return true;
    if(aarch64_linux_timer_alternative_source(program,src,contract))return true;
    if(aarch64_linux_counter_ordering_source(program,src,contract))return true;
    if(aarch64_linux_rng_sysreg_source(program,src,contract))return true;
    if(aarch64_linux_qcom_return_stack_source(program,src,contract))return true;
    if(aarch64_linux_uaccess_source(invocation, program,src,contract))return true;
    if(aarch64_linux_zeropad_source(program,src,contract))return true;
    if(aarch64_linux_user_cache_fault_source(program,src,contract))return true;
    if(aarch64_linux_cache_op_source(program,src,contract))return true;
    if(aarch64_linux_encoded_sysreg_write_source(program,src,contract))
        return true;
    if(aarch64_linux_vhe_sysreg_alternative_source(program,src,contract))
        return true;
    if(aarch64_linux_io_dependency_source(program,src,contract))
        return true;
    if(aarch64_linux_encoded_sysreg_source(program,src,contract))
        return true;
    if(aarch64_linux_tlbi_source(program,src,contract))return true;
    if (program && src && contract && !src->is_goto &&
        !src->output_count && !src->input_count && !src->label_count &&
        !src->clobber_count && !src->register_clobber_count &&
        !src->has_memory_clobber &&
        aarch64_linux_pstate_alternative(
            src->template_text, src->template_length).cap) {
        /* The original PAN helper is asm without 'volatile', but zero
         * GNU outputs make it implicitly volatile and architecturally
         * privileged; the target carries that hardware effect. */
        contract->operand_count = 0U;
        contract->implicit_memory_effect = true;
        return true;
    }
    if(aarch64_linux_kvm_at_fixup_source(invocation, program,src,contract))return true;
    if(aarch64_linux_at_s1e1r_source(program,src,contract))return true;
    if (src && contract && !src->is_goto &&
        !src->output_count && !src->input_count && !src->label_count &&
        !src->clobber_count && !src->register_clobber_count &&
        !src->has_memory_clobber &&
        aarch64_linux_barrier_alternative(
            src->template_text, src->template_length)) {
        contract->operand_count = 0U;
        contract->implicit_memory_effect = true;
        return true;
    }
    /* The exact native AArch64 zero-input ISA list is shared by source
     * qualification and emission. The original Linux DAIF helpers use
     * implicit volatile ASM with no explicit memory clobber. Both that
     * source and explicit clobber variants emit the SAME physical hardware
     * instruction; rejecting the former loses original kernel semantics. */
    if (program && src && contract &&
        !src->is_goto && src->output_count == 0U && src->input_count == 0U &&
        src->register_clobber_count == 0U && src->label_count == 0U) {
        const char *form=aarch64_barrier_template(
            src->template_text,src->template_length);
        /* GNU extended/basic asm without outputs is implicitly volatile.
         * The original Linux `asm("ic ialluis")` omits the keyword, but
         * still executes a privileged I-cache invalidate. Admit this
         * exact ISA opcode without weakening other nonvolatile forms. */
        if(form && (src->is_volatile || strcmp(form,"ic ialluis")==0 ||
                    strcmp(form,"ic iallu")==0) &&
           src->clobber_count <= 1U &&
           src->has_memory_clobber==(src->clobber_count==1U)) {
            contract->operand_count=0U;
            return true;
        }
    }
    if (program && src && contract && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_brk_template(src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "I")) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && expr->kind == MINIC_EXPRESSION_INTEGER &&
            aarch64_linux_brk_value(expr->value.integer_value)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && !src->is_volatile &&
        !src->is_goto && src->outputs && src->inputs &&
        src->output_count == 1U && src->input_count == 1U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_percpu_alternative_template(
            src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "Q")) {
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *dependency = minic_c0_program_expression(
            program, src->inputs[0].expression);
        MinicType plain_dst, plain_dependency;
        if (dst && dependency &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            dependency->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            minic_type_unqualified(dst->type, &plain_dst) &&
            minic_type_unqualified(dependency->type, &plain_dependency) &&
            minic_type_equal(plain_dst, minic_type_unsigned_long()) &&
            minic_type_equal(plain_dependency, minic_type_unsigned_long())) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->output_count == 0U &&
        src->input_count == 1U && src->inputs &&
        src->clobber_count == 1U && src->register_clobber_count == 0U &&
        src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_percpu_write_alternative_template(
            src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "r")) {
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (value && minic_type_unqualified(value->type, &plain) &&
            minic_type_equal(plain, minic_type_unsigned_long())) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->implicit_memory_effect = true;
            return true;
        }
    }
     /* Preserve the whole original Linux MMIO load alternative and its
     * replaceable acquire variant. Source uses =r and a pointer input.
     * This is NOT a Core-native portable volatile-load instruction. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 1U && src->input_count == 1U &&
        src->outputs && src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "r")) {
        const Aarch64RawIoRead *d = aarch64_raw_io_read(
            src->template_text, src->template_length);
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *ptr = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (d && dst && ptr &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            aarch64_stlr_type(dst->type, d->width) &&
            minic_type_is_pointer(ptr->type)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }

    /* Full Linux load-acquire C source pattern: =r output, Q memory
     * dependency, and GNU memory barrier. Not a plain Core LOAD. */
    if(program&&src&&contract&&src->is_volatile&&!src->is_goto&&
       src->output_count==1U&&src->input_count==1U&&
       src->outputs&&src->inputs&&!src->label_count&&
       src->clobber_count==1U&&src->has_memory_clobber&&
       !src->register_clobber_count&&
       constraint(&src->outputs[0],"=r")&&
       constraint(&src->inputs[0],"Q")) {
        const size_t width=aarch64_load_acquire_width(
            src->template_text,src->template_length);
        const MinicExpression *dst=minic_c0_program_expression(
            program,src->outputs[0].expression);
        const MinicExpression *mem=minic_c0_program_expression(
            program,src->inputs[0].expression);
        if(width&&dst&&mem&&dst->value_category==MINIC_VALUE_LVALUE&&
           mem->value_category==MINIC_VALUE_LVALUE&&
           !minic_type_is_const(dst->type)&&
           aarch64_stlr_scalar_source(program,dst->type,width)&&
           (aarch64_stlr_memory_carrier(invocation, program,mem->type,width) ||
            aarch64_stlr_scalar_source(program,mem->type,width))) {
            contract->operand_count=2U;
            contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            return true;
        }
    }

    /* Linux arm64 hardware prefetch is an explicit volatile ISA hint,
     * transported as a generic Core pointer scalar. No generic PRFM
     * passthrough or hardcoded function names in Frontend/Core. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U &&
        src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        aarch64_linux_prefetch_template(
            src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "p")) {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (arg && minic_type_is_pointer(arg->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* Linux's real GNU memory-address input is an r pointer, not Qo.
     * Keep original two scalar constraints and explicitly record the
     * hardware memory effect for Common Core. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 2U &&
        src->inputs != NULL && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        constraint(&src->inputs[0], "rZ") &&
        constraint(&src->inputs[1], "r")) {
        const Aarch64RawIoStore *store = aarch64_raw_io_store(
            src->template_text, src->template_length);
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        const MinicExpression *pointer = minic_c0_program_expression(
            program, src->inputs[1].expression);
        if (store && value && pointer &&
            minic_type_is_pointer(pointer->type) &&
            aarch64_stlr_type(value->type, store->width)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->implicit_memory_effect = true;
            return true;
        }
    }
    /* Existing Core roles: =Q address, rZ scalar input. ISA ownership stays
     * in this AArch64 callback, not in Frontend or generic Core scanning. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 1U && src->clobber_count == 1U &&
        src->has_memory_clobber && src->register_clobber_count == 0U &&
        src->label_count == 0U &&
        constraint(&src->outputs[0], "=Q") &&
        constraint(&src->inputs[0], "rZ")) {
        const size_t width = aarch64_stlr_width(
            src->template_text, src->template_length);
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (width && dst && value &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            (aarch64_stlr_memory_carrier(invocation, program,dst->type,width) ||
             aarch64_stlr_scalar_source(program,dst->type,width)) &&
            aarch64_stlr_scalar_source(program,value->type, width)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* CMPXCHG reuses 5 Core roles: tmp old-value output, observed output,
     * +Q memory, old expected, new replacement. The target alone owns the
     * LL/SC instructions, widths, GPR constraints and ordering. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 2U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        constraint(&src->inputs[1], "r")) {
        const unsigned int form = aarch64_linux_cmpxchg_form(
            src->template_text, src->template_length, false);
        const size_t width = aarch64_linux_cmpxchg_width(form);
        const unsigned int mode = form ? (form - 1U) % 4U : 0U;
        bool accepted = form != 0U &&
            src->clobber_count == (mode == 0U ? 0U : 1U) &&
            src->has_memory_clobber == (mode != 0U) &&
            constraint(&src->inputs[0], width == 8U ? "Lr" : "Kr");
        for (i = 0U; i < 5U && accepted; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[i - 3U];
            const MinicExpression *e =
                minic_c0_program_expression(program, op->expression);
            if (!e || (i < 3U &&
                (e->value_category != MINIC_VALUE_LVALUE ||
                 minic_type_is_const(e->type))) ||
                !aarch64_stlr_type(e->type, i == 0U || i == 3U ? 8U : width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 5U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q")) {
        Aarch64AtomicForm ret;
        bool accepted = aarch64_atomic_return_form(
            src->template_text, src->template_length, &ret);
        accepted = accepted &&
            src->clobber_count == (ret.mode == 1U ? 0U : 1U) &&
            src->has_memory_clobber == (ret.mode != 1U) &&
            constraint(&src->inputs[0], ret.op->rmw_constraint);
        for (i = 0U; i < 4U && accepted; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *arg =
                minic_c0_program_expression(program, op->expression);
            if (!arg ||
                (i < 3U && (arg->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(arg->type))) ||
                !aarch64_stlr_type(arg->type, i == 1U ? 8U : ret.width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* XCHG's 4 typed operands are identical Core roles to old atomic RMW,
     * but a distinct exact AArch64 ISA contract. No generic ASM fallback. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        constraint(&src->inputs[0], "r")) {
        Aarch64XchgForm xchg;
        bool eligible = aarch64_linux_xchg_form(
            src->template_text, src->template_length, &xchg);
        eligible = eligible &&
            src->clobber_count == (xchg.mode == 0U ? 0U : 1U) &&
            src->has_memory_clobber == (xchg.mode != 0U);
        for (i = 0U; i < 4U && eligible; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *value =
                minic_c0_program_expression(program, op->expression);
            if (!value ||
                (i < 3U && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
                !aarch64_stlr_type(value->type, i == 1U ? 8U : xchg.width))
                eligible = false;
        }
        if (eligible) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* One source contract for 4 widths × 3 RMW operators and a
     * fourth add_return family. LL/SC status is u32 while tmp/value/memory
     * have the REAL 8/16/32/64-bit C width; never conflate those types. */
    if(program && src && contract && src->is_volatile && !src->is_goto &&
       src->outputs && src->inputs && src->output_count==3U &&
       src->input_count==1U && src->label_count==0U &&
       src->clobber_count==0U && src->register_clobber_count==0U &&
       !src->has_memory_clobber &&
       constraint(&src->outputs[0],"=&r") &&
       constraint(&src->outputs[1],"=&r") &&
       constraint(&src->outputs[2],"+Q") &&
       constraint(&src->inputs[0],"r")) {
        Aarch64PerCpuForm form;
        bool eligible=aarch64_linux_percpu_rmw_form(
            src->template_text,src->template_length,&form);
        for(i=0U;i<4U&&eligible;++i) {
            const MinicInlineAsmOperand *operand=i<3U?
                &src->outputs[i]:&src->inputs[0];
            const MinicExpression *value=minic_c0_program_expression(
                program,operand->expression);
            if(!value ||
               (i<3U&&(value->value_category!=MINIC_VALUE_LVALUE||
                       minic_type_is_const(value->type))) ||
               !aarch64_stlr_type(value->type,i==0U?4U:form.width))
                eligible=false;
        }
        if(eligible) {
            contract->operand_count=4U;
            contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 3U && src->input_count == 1U &&
        src->outputs && src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        (constraint(&src->inputs[0], "Lr") ||
         constraint(&src->inputs[0], "Kr") ||
         constraint(&src->inputs[0], "r") ||
         constraint(&src->inputs[0], "Ir") ||
         constraint(&src->inputs[0], "Jr"))) {
        Aarch64AtomicForm atomic;
        bool accepted = aarch64_atomic_form(
            src->template_text, src->template_length, false, &atomic);
        const size_t width = accepted ? atomic.width : 0U;
        accepted = accepted &&
            constraint(&src->inputs[0],
                       width == 8U ? atomic.op->rmw_constraint :
                                     atomic.op->fetch32_constraint);
        for (i = 0U; i < 4U && accepted; ++i) {
            const MinicInlineAsmOperand *operand = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *arg =
                minic_c0_program_expression(program, operand->expression);
            if (!arg ||
                (i < 3U && (arg->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(arg->type))) ||
                !aarch64_stlr_type(arg->type, i == 1U ? 8U : width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 4U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "=&r") &&
        constraint(&src->outputs[3], "+Q")) {
        Aarch64AtomicForm atomic;
        bool eligible = aarch64_atomic_form(
            src->template_text, src->template_length, true, &atomic);
        const size_t width = eligible ? atomic.width : 0U;
        const unsigned int mode = eligible ? atomic.mode : 0U;
        eligible = eligible &&
            src->clobber_count == (mode == 1U ? 0U : 1U) &&
            src->has_memory_clobber == (mode != 1U) &&
            constraint(&src->inputs[0],
                       width == 8U ? atomic.op->fetch64_constraint :
                                     atomic.op->fetch32_constraint);
        for (i = 0U; i < 5U && eligible; ++i) {
            const MinicInlineAsmOperand *operand = i < 4U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *value = minic_c0_program_expression(
                program, operand->expression);
            if (!value ||
                (i < 4U && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
                !aarch64_stlr_type(
                    value->type, i == 2U ? 8U : width))
                eligible = false;
        }
        if (eligible) {
            contract->operand_count = 5U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_msr_register(src->template_text, src->template_length) != NULL &&
        (constraint(&src->inputs[0], "rZ") ||
         (constraint(&src->inputs[0], "r") &&
          strcmp(aarch64_linux_msr_register(
                     src->template_text, src->template_length),
                 "s3_0_c15_c0_4") == 0))) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            /* The *original* Linux sysreg.h write_sysreg() materializes
             * u64 (__u64 = unsigned long long), whereas the interrupt
             * caller's flags and AAPCS64 GPR are unsigned long/64-bit.
             * They have equal machine width, not identical C type. */
            (minic_type_equal(plain, minic_type_unsigned_long()) ||
             minic_type_equal(plain, minic_type_unsigned_long_long()))) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        /* Original GNU Linux read_sysreg() uses asm volatile for all
         * whitelisted hardware status registers, not only DAIF.
         * Keep the exact register allowlist and typed output contract. */
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type) &&
            minic_type_unqualified(expr->type, &plain) &&
            aarch64_linux_sysreg_u64_type(plain)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract &&
        !src->is_goto && !src->is_volatile &&
        src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        /* DAIF IRQ mask read is a volatile Linux sysreg operation; do not
         * expand the historical nonvolatile EL1 CPU-offset MRS allowlist. */
        strcmp(aarch64_linux_mrs_register(
                   src->template_text, src->template_length), "daif") != 0 &&
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            aarch64_linux_sysreg_u64_type(plain) &&
            expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 2U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_uaccess_bic_template(
            src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "r") &&
        constraint(&src->inputs[1], "i")) {
        const MinicExpression *out = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        const MinicExpression *mask = minic_c0_program_expression(
            program, src->inputs[1].expression);
        MinicConstValue constant;
            if (out && arg && mask &&
            out->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(out->type) &&
            minic_type_is_pointer(out->type) &&
            minic_type_is_pointer(arg->type) &&
            minic_type_is_integer(mask->type) &&
            minic_const_eval_integer(program, invocation,
                                    src->inputs[1].expression, &constant) &&
            constant.bits == (UINT64_C(1) << 55)) {
            contract->operand_count = 3U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 1U || src->input_count != 2U ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        src->has_memory_clobber || !src->register_clobbers ||
        !src->register_clobbers[0].name ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(src->template_text, src->template_length) ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r") ||
        !constraint(&src->inputs[1], "Ir"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U
            ? &src->outputs[0] : &src->inputs[i-1U];
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || !minic_type_unqualified(expr->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

bool minic_aarch64_seal_normalized_inline_asm(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *source,
    const char *normalized, size_t length,
    MinicTargetInlineAsmContract *contract) {
    const Aarch64RawIoRead *old_read, *new_read;
    const Aarch64RawIoStore *old_store, *new_store;
    size_t original_width, machine_width;
    uint32_t source_form, source_variant, machine_form, machine_variant;
    if (!invocation || !program || !source || !source->template_text ||
        !normalized || !length || !contract)
        return false;

    original_width = aarch64_load_acquire_width(
        source->template_text, source->template_length);
    if (original_width) {
        machine_width = aarch64_load_acquire_width(normalized, length);
        if (machine_width != original_width || contract->operand_count != 2U ||
            contract->kinds[0] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            contract->kinds[1] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT)
            return false;
        contract->target_machine_form_id = AARCH64_MACHINE_ASM_LDAR;
        contract->target_machine_form_variant = (uint32_t)original_width;
        return true;
    }
    original_width = aarch64_stlr_width(
        source->template_text, source->template_length);
    if (original_width) {
        machine_width = aarch64_stlr_width(normalized, length);
        if (machine_width != original_width || contract->operand_count != 2U ||
            contract->kinds[0] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            contract->kinds[1] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
            return false;
        contract->target_machine_form_id = AARCH64_MACHINE_ASM_STLR;
        contract->target_machine_form_variant = (uint32_t)original_width;
        return true;
    }
    old_read = aarch64_raw_io_read(
        source->template_text, source->template_length);
    if (old_read) {
        new_read = aarch64_raw_io_read(normalized, length);
        if (new_read != old_read || contract->operand_count != 2U ||
            contract->kinds[0] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            contract->kinds[1] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
            return false;
        contract->target_machine_form_id = AARCH64_MACHINE_ASM_RAW_IO_READ;
        contract->target_machine_form_variant = (uint32_t)old_read->width;
        return true;
    }
    old_store = aarch64_raw_io_store(
        source->template_text, source->template_length);
    if (old_store) {
        new_store = aarch64_raw_io_store(normalized, length);
        if (new_store != old_store || contract->operand_count != 2U ||
            contract->kinds[0] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            contract->kinds[1] !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !contract->implicit_memory_effect)
            return false;
        contract->target_machine_form_id = AARCH64_MACHINE_ASM_RAW_IO_STORE;
        contract->target_machine_form_variant = (uint32_t)old_store->width;
        return true;
    }
    /* All remaining source-approved structured forms must carry a target
     * identity before the C program can be discarded. Both spellings must
     * be the same ISA form; the target machine repeats this check after
     * lowering, independent of the GNU C source constraint qualification.
     * In particular an unrecognized form cannot silently acquire ID 0. */
    if (!aarch64_machine_asm_identify(
            source->template_text, source->template_length, false,
            &source_form, &source_variant) ||
        !aarch64_machine_asm_identify(normalized, length, true,
                                      &machine_form, &machine_variant) ||
        source_form != machine_form || source_variant != machine_variant)
        return false;
    contract->target_machine_form_id = source_form;
    contract->target_machine_form_variant = source_variant;
    return true;
}
