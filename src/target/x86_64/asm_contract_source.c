#include "target/x86_64/asm_contract_source.h"
#include "target/x86_64/asm_contract_internal.h"
#include "target/data_layout_source.h"
#include "frontend/ast.h"
#include <stdio.h>
#include <string.h>

/* C/GNU inline-ASM source admission owns C types, AST expression checks,
 * constraints and form selection. Machine template and ISA-form validation
 * remain in asm_contract.c and are shared without duplicate recognizers. */
static bool x86_linux_jump_label_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract);

static bool target_constraint_is(const MinicInlineAsmOperand *, const char *);
static bool clobber_is(const MinicInlineAsmRegisterClobber *, const char *);


static bool x86_linux_gs_testcc_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_linux_gs_testcc_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *flag, *memory, *index;
    MinicType plain;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->label_count ||
        src->has_memory_clobber || src->clobber_count != 0U ||
        src->register_clobber_count != 0U ||
        !target_constraint_is(&src->outputs[0], "=@ccc") ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "Ir")) return false;
    flag = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    index = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!flag || !memory || !index ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) ||
        !minic_type_unqualified(flag->type, &plain) ||
        !minic_type_equal(plain, minic_type_bool()) ||
        !minic_type_unqualified(memory->type, &plain) ||
        !minic_type_is_integer(plain) ||
        !minic_type_is_integer(index->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_testcc_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_linux_testcc_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *flag, *memory, *mask;
    MinicType type;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], form == 1U ? "=@ccnz" : "=@ccz") ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "i")) return false;
    flag = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    mask = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!flag || !memory || !mask ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) ||
        !minic_type_unqualified(flag->type, &type) ||
        !minic_type_equal(type, minic_type_bool()) ||
        !minic_type_unqualified(memory->type, &type) ||
        !minic_type_is_integer(type) ||
        !minic_type_is_integer(mask->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}
/* A source form is private to the target only when its GNU operands actually
 * request a memory transfer. Merely spelling e.g. "incq %0" is not proof:
 * the valid generic +r register variant must remain available to Core.
 * Keep the exact memory roles and ISA-form recognizers in the source owner. */
static bool x86_atomic_rmw_has_memory_role(const MinicInlineAsm *source) {
    size_t i;
    if (source == NULL) return false;
    for (i = 0U; source->outputs != NULL &&
                  i < source->output_count; ++i)
        if (target_constraint_is(&source->outputs[i], "+m") ||
            target_constraint_is(&source->outputs[i], "=m") ||
            target_constraint_is(&source->outputs[i], "m"))
            return true;
    for (i = 0U; source->inputs != NULL &&
                  i < source->input_count; ++i)
        if (target_constraint_is(&source->inputs[i], "+m") ||
            target_constraint_is(&source->inputs[i], "=m") ||
            target_constraint_is(&source->inputs[i], "m"))
            return true;
    return false;
}

/* Keep target-owned fixed-register/privileged source reserved; an output-only
 * asm is not permission for generic Core to bypass its exact GNU =a or
 * memory role when that target qualifier has rejected the constraints. */
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const reserved[] = {"movq %%gs:", "cmp",
        "outb ", "outw ", "outl ", "inb ", "inw ", "inl ", "lidt", "lgdt"};
    size_t i;
    /* GCC's x86 I is an ISA-specific 0..31 shift count, whereas lowercase
     * i is a generic compile-time constant. A valid I value is not license
     * for Core's generic ASM fallback to discard it, even for `nop`.
     * Also protect multi-alternative constraints such as "Ir". GNU C
     * constant-range validation is done earlier by the target data model;
     * this gate enforces the *machine form* ownership after that check. */
    if (src != NULL && src->inputs != NULL) {
        for (i = 0U; i < src->input_count; ++i) {
            const MinicInlineAsmOperand *input = &src->inputs[i];

            if (input->constraint_text != NULL &&
                memchr(input->constraint_text, 'I', input->constraint_length) != NULL) {
                return true;
            }
        }
    }
    /* A rejected C width/constraint in an exact Linux memory RMW must
     * fail at the target source boundary, not reach generic Core ASM.
     * The memory-role witness prevents hijacking legal register-only GNU
     * forms that happen to share a mnemonic such as incq, xaddq or cmpxchg. */
    if (src != NULL && src->template_text != NULL &&
        x86_atomic_rmw_has_memory_role(src) &&
        (minic_x86_64_gs_atomic_plan(src->template_text,
             src->template_length).kind != MINIC_X86_GS_ATOMIC_NONE ||
         minic_x86_64_atomic_rmw_form(src->template_text,
             src->template_length) != MINIC_X86_ATOMIC_RMW_NONE ||
         minic_x86_64_atomic_cc_form(src->template_text,
             src->template_length) != MINIC_X86_ATOMIC_CC_NONE ||
         minic_x86_64_raw_cmpxchg_form(src->template_text,
             src->template_length) != MINIC_X86_TRYCAS_NONE ||
         minic_x86_64_trycas_form(src->template_text,
             src->template_length) != MINIC_X86_TRYCAS_NONE ||
         minic_x86_64_linux_bitops_form(src->template_text,
             src->template_length) != MINIC_X86_BITOPS_NONE ||
         minic_x86_64_linux_lock_bitcc_form(src->template_text,
             src->template_length) != 0U ||
         minic_x86_64_linux_atomic_xchg_form(src->template_text,
             src->template_length) != 0U ||
         minic_x86_64_linux_cmpxchg128_form(src->template_text,
             src->template_length) != 0U))
        return true;
    if (src && src->template_text &&
        (minic_x86_64_linux_crash_segment_snapshot_form(
             src->template_text, src->template_length) != 0U ||
         minic_x86_64_linux_efi_kernel_transfer_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_legacy_fnsave_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_pv_indirect_call_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_kvm_hypercall_alt_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_exception_stack_call_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_vmware_stealclock_alt_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_pm_trace_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_crash_gpr_snapshot_form(
             src->template_text, src->template_length) != 0U ||
         minic_x86_64_linux_crash_misc_snapshot_form(
             src->template_text, src->template_length) != 0U ||
         minic_x86_64_linux_vmware_hypercall_form(
             src->template_text, src->template_length) != 0U ||
         minic_x86_64_linux_kexec_load_segments_form(
             src->template_text, src->template_length) ||
         minic_x86_64_linux_pci_mmconfig_form(
             src->template_text, src->template_length, false) != 0U ||
         minic_x86_64_linux_pci_mmconfig_form(
             src->template_text, src->template_length, true) != 0U ||
         minic_x86_64_linux_via_xstore_form(
             src->template_text, src->template_length) ||
         minic_x86_64_compact_asm_equals(
             src->template_text, src->template_length, "movb%1,%%al")))
        return true;
    return minic_source_backend_inline_asm_prefix_matches(
        src, reserved, sizeof(reserved) / sizeof(reserved[0]));
}

static bool x86_linux_crash_register_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int gpr = src ? minic_x86_64_linux_crash_gpr_snapshot_form(
        src->template_text, src->template_length) : 0U;
    const unsigned int misc = src ? minic_x86_64_linux_crash_misc_snapshot_form(
        src->template_text, src->template_length) : 0U;
    const bool memory_output = gpr != 0U || misc == 1U;
    const MinicExpression *out;
    MinicType plain;
    if (!program || !src || !contract || (!gpr && !misc) ||
        src->is_goto || src->is_volatile != memory_output ||
        src->output_count != 1U || src->input_count != 0U || !src->outputs ||
        src->label_count || src->has_memory_clobber || src->clobber_count ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], memory_output ? "=m" : "=r"))
        return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!out || out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) ||
        !minic_type_unqualified(out->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long())) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = memory_output ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->target_machine_form_variant = gpr != 0U ? gpr : 16U + misc;
    return true;
}

static bool x86_linux_pm_trace_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *out, *line, *filename;
    if (!program || !src || !contract ||
        !minic_x86_64_linux_pm_trace_form(
            src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto || src->output_count != 1U ||
        src->input_count != 2U || !src->outputs || !src->inputs ||
        src->label_count || src->has_memory_clobber || src->clobber_count ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "=r") ||
        !target_constraint_is(&src->inputs[0], "i") ||
        !target_constraint_is(&src->inputs[1], "i")) return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    line = minic_c0_program_expression(program, src->inputs[0].expression);
    filename = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!out || !line || !filename ||
        out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) || !minic_type_is_pointer(out->type) ||
        line->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(line->type) ||
        line->value.integer_value < 0 || line->value.integer_value > 65535 ||
        !minic_type_is_pointer(filename->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    /* The .tracedata record is emitted by GAS at build time, not a run-time
     * memory read/write. GCC's original template has no memory clobber. */
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_vmware_stealclock_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const regs[] = {"a", "b", "c", "d", "S", "D"};
    const MinicExpression *expr;
    MinicType plain;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_vmware_stealclock_alt_form(
            src->template_text, src->template_length) ||
        src->output_count != 2U || src->input_count != 6U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "=a") ||
        !target_constraint_is(&src->outputs[1], "=c")) return false;
    for (i = 0U; i < 8U; ++i) {
        const bool output = i < 2U;
        const MinicInlineAsmOperand *op = output ?
            &src->outputs[i] : &src->inputs[i - 2U];
        expr = minic_c0_program_expression(program, op->expression);
        if ((!output && !target_constraint_is(op, regs[i - 2U])) ||
            !expr || !minic_type_unqualified(expr->type, &plain) ||
            (output && expr->value_category != MINIC_VALUE_LVALUE) ||
            (output && minic_type_is_const(expr->type)) ||
            !(minic_type_equal(plain, minic_type_unsigned_int()) ||
              (!output && minic_type_equal(plain, minic_type_int()))))
            return false;
        contract->kinds[i] = output ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 8U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_kvm_hypercall_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const regs[] = {"a", "b", "c", "d", "S"};
    const MinicExpression *output, *arg;
    bool alt;
    size_t i;
    if (!src) return false;
    alt = minic_x86_64_linux_kvm_hypercall_alt_form(
        src->template_text, src->template_length);
    if (!program || !contract || !src->is_volatile || src->is_goto ||
        (!alt && !minic_x86_64_linux_kvm_hypercall_direct_form(
            src->template_text, src->template_length)) ||
        src->output_count != 1U || !src->outputs ||
        src->input_count < 1U || src->input_count > 5U ||
        (!alt && src->input_count > 4U) ||
        !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "=a")) return false;
    output = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!output || output->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(output->type, minic_type_long())) return false;
    for (i = 0U; i < src->input_count; ++i) {
        arg = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!arg || !target_constraint_is(&src->inputs[i], regs[i]) ||
            !minic_type_equal(arg->type, i == 0U
                ? minic_type_unsigned_int() : minic_type_unsigned_long()))
            return false;
    }
    contract->operand_count = 1U + src->input_count;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 1U; i < contract->operand_count; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_exception_stack_call_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const outputs[] = {"+r", "+r"};
    static const char *const inputs[] = {"i", "r", "r", "r", "r"};
    static const char *const clobbers[] = {
        "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10"
    };
    const MinicExpression *tos, *sp, *fn, *arg, *tos_input;
    const MinicFixedRegisterBinding *sp_fixed;
    size_t i;
    if (!program || !src || !contract || src->is_goto || !src->is_volatile ||
        !minic_x86_64_linux_exception_stack_call_form(
            src->template_text, src->template_length) ||
        src->output_count != 2U || src->input_count != 5U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 10U ||
        src->register_clobber_count != 9U || !src->register_clobbers)
        return false;
    for (i = 0U; i < 2U; ++i)
        if (!target_constraint_is(&src->outputs[i], outputs[i])) return false;
    for (i = 0U; i < 5U; ++i)
        if (!target_constraint_is(&src->inputs[i], inputs[i])) return false;
    for (i = 0U; i < 9U; ++i)
        if (!clobber_is(&src->register_clobbers[i], clobbers[i])) return false;
    tos = minic_c0_program_expression(program, src->outputs[0].expression);
    sp = minic_c0_program_expression(program, src->outputs[1].expression);
    fn = minic_c0_program_expression(program, src->inputs[0].expression);
    tos_input = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!tos || !sp || !fn ||
        tos->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(tos->type) ||
        !minic_type_equal(sp->type, minic_type_unsigned_long()) ||
        fn->kind != MINIC_EXPRESSION_FUNCTION ||
        !minic_type_is_pointer(fn->type) ||
        tos->kind != MINIC_EXPRESSION_LOCAL ||
        !tos_input || tos_input->kind != MINIC_EXPRESSION_LOCAL ||
        tos->value.local_id != tos_input->value.local_id ||
        sp->kind != MINIC_EXPRESSION_FIXED_REGISTER) return false;
    sp_fixed = minic_c0_program_fixed_register_binding(
        program, sp->value.fixed_register_binding_id);
    if (!sp_fixed || !sp_fixed->register_name ||
        strcmp(sp_fixed->register_name, "rsp")) return false;
    for (i = 1U; i < 5U; ++i) {
        arg = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!arg || (!minic_type_is_pointer(arg->type) &&
            !minic_type_equal(arg->type, minic_type_unsigned_long())))
            return false;
    }
    contract->operand_count = 7U;
    /* Both GNU +r outputs keep the original values unchanged after this
     * balanced stack operation; Core keeps their scalar input identities. */
    for (i = 0U; i < 7U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_alt_reloc_selftest_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                                 const MinicInlineAsm *src,
                                                 MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value;
    if (!src || !program || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_alt_reloc_selftest_form(src->template_text,
                                                     src->template_length) ||
        src->output_count || src->input_count != 1U || !src->inputs ||
        src->label_count || src->clobber_count != 1U ||
        src->has_memory_clobber || src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "rdi") ||
        !target_constraint_is(&src->inputs[0], "m")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || value->value_category != MINIC_VALUE_LVALUE) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_real_mode_far_jump_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *destination, *restart_type;
    MinicType type;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_real_mode_far_jump_form(src->template_text,
                                                    src->template_length) ||
        src->output_count || src->input_count != 2U || !src->inputs ||
        src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "D")) return false;
    destination = minic_c0_program_expression(program, src->inputs[0].expression);
    restart_type = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!destination || destination->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(destination->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int()) ||
        !restart_type || !minic_type_unqualified(restart_type->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

bool minic_x86_64_linux_mmio_type_matches(MinicType type, unsigned int width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain)) return false;
    switch (width) {
    case 1U: return minic_type_equal(plain, minic_type_unsigned_char());
    case 2U: return minic_type_equal(plain, minic_type_unsigned_short());
    case 4U: return minic_type_equal(plain, minic_type_unsigned_int());
    case 8U: return minic_type_equal(plain, minic_type_unsigned_long_long()) ||
                    minic_type_equal(plain, minic_type_unsigned_long());
    default: return false;
    }
}

static bool x86_linux_mmio_write_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                        const MinicInlineAsm *src,
                                        MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value, *memory;
    const unsigned int width = src ? minic_x86_64_linux_mmio_write_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile || src->is_goto ||
        src->output_count || src->input_count != 2U || !src->inputs ||
        src->label_count || src->register_clobber_count ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        !target_constraint_is(&src->inputs[0], width == 1U ? "q" : "r") ||
        !target_constraint_is(&src->inputs[1], "m")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!value || !memory || memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_x86_64_linux_mmio_type_matches(value->type, width) ||
        !minic_x86_64_linux_mmio_type_matches(memory->type, width)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = width;
    return true;
}

static bool x86_linux_mmio_read_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                       const MinicInlineAsm *src,
                                       MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output, *memory;
    const unsigned int width = src ? minic_x86_64_linux_mmio_read_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 1U || !src->outputs ||
        !src->inputs || src->label_count || src->register_clobber_count ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        !target_constraint_is(&src->outputs[0],
            minic_x86_64_compact_asm_equals(
                src->template_text, src->template_length, "movb%1,%%al")
                ? "=a" : (width == 1U ? "=q" : "=r")) ||
        !target_constraint_is(&src->inputs[0], "m")) return false;
    output = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!output || !memory || output->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_x86_64_linux_mmio_type_matches(output->type, width) ||
        !minic_x86_64_linux_mmio_type_matches(memory->type, width)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = width;
    return true;
}

static bool x86_linux_pci_mmconfig_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const bool read = source && source->output_count == 1U;
    const unsigned int width = source ? minic_x86_64_linux_pci_mmconfig_form(
        source->template_text, source->template_length, !read) : 0U;
    const MinicExpression *address, *value;
    if (!program || !source || !contract || !width ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (read ? 1U : 0U) ||
        source->input_count != (read ? 1U : 2U) || !source->inputs ||
        (read && !source->outputs) || source->register_clobber_count ||
        source->clobber_count != (read ? 0U : 1U) ||
        source->has_memory_clobber != !read ||
        !target_constraint_is(read ? &source->outputs[0] : &source->inputs[0],
              read ? "=a" : "a") ||
        !target_constraint_is(&source->inputs[read ? 0U : 1U], "r")) return false;
    address = minic_c0_program_expression(program,
        source->inputs[read ? 0U : 1U].expression);
    value = minic_c0_program_expression(program, read ?
        source->outputs[0].expression : source->inputs[0].expression);
    if (!address || !value || !minic_type_is_pointer(address->type) ||
        !minic_x86_64_linux_mmio_type_matches(value->type, width) ||
        (read && (value->value_category != MINIC_VALUE_LVALUE ||
                  minic_type_is_const(value->type)))) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_rep_string_io_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *address, *count, *port;
    const unsigned int form = src ? minic_x86_64_linux_rep_string_io_form(
        src->template_text, src->template_length) : 0U;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 2U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], form <= 3U ? "+S" : "+D") ||
        !target_constraint_is(&src->outputs[1], "+c") ||
        !target_constraint_is(&src->inputs[0], "d")) return false;
    address = minic_c0_program_expression(program, src->outputs[0].expression);
    count = minic_c0_program_expression(program, src->outputs[1].expression);
    port = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!address || !count || !port ||
        address->value_category != MINIC_VALUE_LVALUE ||
        count->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(address->type) ||
        !minic_type_equal(count->type, minic_type_unsigned_long()) ||
        !minic_type_equal(port->type, minic_type_unsigned_short())) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_i915_movntdqa_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_i915_movntdqa_form(
        source->template_text, source->template_length) : 0U;
    size_t i;
    if (!program || !source || !contract || !form || source->is_volatile ||
        source->is_goto || source->label_count || source->output_count ||
        !source->inputs || source->input_count != 2U ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count)
        return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        const MinicExpression *value = minic_c0_program_expression(
            program, input->expression);
        if (!target_constraint_is(input, "r") || !value ||
            !minic_type_is_pointer(value->type))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 2U;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}
/* Linux lib/raid6 fixed XMM/YMM/ZMM register statements. The exact opcode
 * syntax is owned by raid6_simd.c, never parsed by target-neutral Core.
 * Both GNU basic asm (zero operands) and extended asm with up to eight real
 * memory addresses have explicit hardware side effects. */
static bool x86_linux_raid6_simd_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    size_t i, count;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->label_count ||
        source->register_clobber_count ||
        (source->clobber_count != 0U && !source->has_memory_clobber))
        return false;
    count = source->output_count + source->input_count;
    if (count > 8U || !minic_x86_64_raid6_simd_template(
            source->template_text, source->template_length, count))
        return false;
    for (i = 0U; i < count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *operand = output
            ? &source->outputs[i]
            : &source->inputs[i - source->output_count];
        const MinicExpression *value = minic_c0_program_expression(
            program, operand->expression);
        if (!target_constraint_is(operand, output ? "=m" : "m") ||
            !value || value->value_category != MINIC_VALUE_LVALUE)
            return false;
        contract->kinds[i] = output
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    }
    contract->operand_count = count;
    contract->target_machine_form_variant = (uint32_t)count;
    contract->implicit_memory_effect = true;
    return true;
}
/* GNU C vector-size/rank/sign qualification belongs to source ingress.
 * The machine emitter independently verifies the sealed Core vector shape
 * and never receives a source MinicType. */
bool minic_x86_64_kvm_movdqa_u32x4_type(MinicType type) {
    return minic_type_is_gnu_fixed_vector(type) &&
        type.gnu_vector_bytes == 16U &&
        type.gnu_vector_element_kind == MINIC_TYPE_BASE_INT &&
        type.integer_rank == MINIC_INTEGER_RANK_INT &&
        type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED;
}
/* KVM's `sse128_t` is a genuine GNU `u32 vector_size(16)` object: x86's
 * data layout owns sizeof/alignof=16, while Linux -mno-sse forbids implicit
 * SIMD code. This admission permits only the fixed-register MOVDQA memory
 * load/store, not arithmetic or vector by-value ABI. */
static bool x86_linux_kvm_movdqa_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_kvm_movdqa_form(
        src->template_text, src->template_length) : 0U;
    const bool read = form <= 16U;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *expr;
    MinicType type;
    if (!program || !src || !contract || !form || src->is_volatile ||
        src->is_goto || src->label_count || src->has_memory_clobber ||
        src->clobber_count || src->register_clobber_count ||
        src->input_count != (read ? 1U : 0U) ||
        src->output_count != (read ? 0U : 1U) ||
        (read ? !src->inputs : !src->outputs)) return false;
    operand = read ? &src->inputs[0] : &src->outputs[0];
    if (!target_constraint_is(operand, read ? "m" : "=m"))
        return false;
    expr = minic_c0_program_expression(program, operand->expression);
    if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
        (!read && minic_type_is_const(expr->type)) ||
        !minic_type_unqualified(expr->type, &type) ||
        !minic_x86_64_kvm_movdqa_u32x4_type(type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = read
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->target_machine_form_variant = form;
    contract->implicit_memory_effect = !read;
    return true;
}

static bool x86_linux_iomem_copy_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_linux_iomem_copy_form(
        src->template_text, src->template_length) : 0U;
    static const char *const rep_outputs[] = {"=&c", "=&D", "=&S"};
    static const char *const rep_inputs[] = {"0", "q", "1", "2"};
    static const char *const one_outputs[] = {"=&D", "=&S"};
    static const char *const one_inputs[] = {"0", "1"};
    size_t i, outputs, inputs;

    if (!program || !src || !contract || !form || !src->is_volatile || src->is_goto ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count || src->label_count || !src->outputs || !src->inputs)
        return false;
    outputs = form == 3U ? 3U : 2U;
    inputs = form == 3U ? 4U : 2U;
    if (src->output_count != outputs || src->input_count != inputs) return false;
    for (i = 0U; i < outputs; ++i) {
        const MinicInlineAsmOperand *op = &src->outputs[i];
        const MinicExpression *value = minic_c0_program_expression(program, op->expression);
        if (!target_constraint_is(op, form == 3U ? rep_outputs[i] : one_outputs[i]) ||
            !value || value->value_category != MINIC_VALUE_LVALUE ||
            (form == 3U ? !minic_type_is_integer(value->type) :
                          !minic_type_is_pointer(value->type))) return false;
    }
    for (i = 0U; i < inputs; ++i) {
        const MinicInlineAsmOperand *op = &src->inputs[i];
        const MinicExpression *value = minic_c0_program_expression(program, op->expression);
        if (!target_constraint_is(op, form == 3U ? rep_inputs[i] : one_inputs[i]) ||
            !value || (!minic_type_is_integer(value->type) &&
                       !minic_type_is_pointer(value->type))) return false;
    }
    contract->operand_count = outputs + inputs;
    for (i = 0U; i < contract->operand_count; ++i)
        contract->kinds[i] = i < outputs
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_flushcache_copy_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_linux_flushcache_copy_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *expr;
    size_t i;
    if (!program || !src || !contract || !form || src->is_goto ||
        src->label_count || src->output_count != (form == 4U ? 1U : 0U) ||
        src->input_count != (form == 4U ? 1U : 2U) || !src->inputs ||
        (form == 4U && !src->outputs) ||
        src->has_memory_clobber != (form != 4U) ||
        src->clobber_count != (form == 4U ? 0U : (form == 1U ? 5U : 2U)) ||
        src->register_clobber_count != (form == 4U ? 0U : (form == 1U ? 4U : 1U)) ||
        src->is_volatile != (form == 4U)) return false;
    if (form == 4U) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (!target_constraint_is(&src->outputs[0], "+m") || !expr ||
            expr->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(expr->type)) return false;
    }
    for (i = 0U; i < src->input_count; ++i) {
        expr = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!target_constraint_is(&src->inputs[i], form == 4U ? "a" : "r") ||
            !expr || (!minic_type_is_pointer(expr->type) &&
                      !minic_type_is_integer(expr->type)))
            return false;
    }
    if (form != 4U) {
        static const char *const clobbers[] = {"r8", "r9", "r10", "r11"};
        for (i = 0U; i < src->register_clobber_count; ++i)
            if (!clobber_is(&src->register_clobbers[i], clobbers[i])) return false;
    }
    contract->operand_count = src->input_count + src->output_count;
    for (i = 0U; i < contract->operand_count; ++i)
        contract->kinds[i] = form == 4U && i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_atomic_xchg_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *reg, *mem;
    MinicType reg_type, mem_type;
    const unsigned int width = src ? minic_x86_64_linux_atomic_xchg_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile ||
        src->is_goto || src->output_count != 2U || src->input_count ||
        !src->outputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 2U || src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !target_constraint_is(&src->outputs[0], width == 1U ? "+q" : "+r") ||
        !target_constraint_is(&src->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, src->outputs[0].expression);
    mem = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!reg || !mem || reg->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(mem->type, &mem_type) ||
        !minic_type_equal(reg_type, mem_type) ||
        !(minic_type_is_integer(reg_type) || minic_type_is_pointer(reg_type)))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = width;
    return true;
}

static bool x86_linux_pvclock_scale_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                            const MinicInlineAsm *src,
                                            MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const output_constraints[] = {"=a", "=d"};
    const MinicExpression *expressions[4];
    size_t i;
    if (!src || !program || !contract ||
        !minic_x86_64_linux_pvclock_scale_form(src->template_text, src->template_length) ||
        src->is_volatile || src->is_goto || src->output_count != 2U ||
        src->input_count != 2U || !src->outputs || !src->inputs ||
        src->label_count || src->clobber_count || src->has_memory_clobber ||
        src->register_clobber_count ||
        !target_constraint_is(&src->inputs[0], "0") ||
        !target_constraint_is(&src->inputs[1], "rm")) return false;
    for (i = 0U; i < 2U; ++i) {
        if (!target_constraint_is(&src->outputs[i], output_constraints[i])) return false;
        expressions[i] = minic_c0_program_expression(program, src->outputs[i].expression);
        expressions[i + 2U] = minic_c0_program_expression(program, src->inputs[i].expression);
    }
    for (i = 0U; i < 4U; ++i)
        if (!expressions[i] || !minic_type_is_integer(expressions[i]->type) ||
            (i < 2U && expressions[i]->value_category != MINIC_VALUE_LVALUE)) return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_pv_indirect_call_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                               const MinicInlineAsm *src,
                                               MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const outputs[] = {"=D", "=S", "=d", "=c", "+r"};
    static const char *const input_regs[] = {"D", "S", "d", "c"};
    static const char *const clobbers[] = {"cc", "rax", "r8", "r9", "r10", "r11"};
    const MinicExpression *address, *type_number, *stack;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_pv_indirect_call_form(src->template_text,
                                                  src->template_length) ||
        src->output_count != 5U || !src->outputs || src->input_count < 2U ||
        src->input_count > 6U ||
        5U + src->input_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        !src->inputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 7U || src->register_clobber_count != 6U ||
        !src->register_clobbers ||
        !target_constraint_is(&src->inputs[0], "i") ||
        !target_constraint_is(&src->inputs[1], "m")) return false;
    for (i = 0U; i < 5U; ++i)
        if (!target_constraint_is(&src->outputs[i], outputs[i])) return false;
    /* PVOP_VCALL0..4 pass arguments via the *fixed* SysV integer register
     * sequence, even though the asm's four output registers are separately
     * listed. The callback may rely on D/S/d/c at the CALL boundary; these
     * are not arbitrary register inputs and cannot be omitted or reordered. */
    for (i = 2U; i < src->input_count; ++i) {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[i].expression);
        if (!target_constraint_is(&src->inputs[i], input_regs[i - 2U]) ||
            !arg || !minic_type_equal(arg->type, minic_type_unsigned_long()))
            return false;
    }
    for (i = 0U; i < 6U; ++i)
        if (!clobber_is(&src->register_clobbers[i], clobbers[i])) return false;
    stack = minic_c0_program_expression(program, src->outputs[4].expression);
    type_number = minic_c0_program_expression(program, src->inputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!stack || !type_number || !address ||
        !minic_type_equal(stack->type, minic_type_unsigned_long()) ||
        !minic_type_is_integer(type_number->type) ||
        address->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(address->type)) return false;
    contract->operand_count = 5U + src->input_count;
    for (i = 0U; i < 4U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[5] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[6] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    for (i = 7U; i < contract->operand_count; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_task_control_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                           const MinicInlineAsm *src,
                                           MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *expr;
    const unsigned int form = src ? minic_x86_64_linux_task_control_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || src->is_goto || !src->is_volatile ||
        src->clobber_count || src->label_count || src->has_memory_clobber ||
        src->register_clobber_count || !form ||
        (form == 1U
            ? (src->output_count != 0U || src->input_count != 1U ||
               !src->inputs || !target_constraint_is(&src->inputs[0], "q"))
            : (src->output_count != 1U || src->input_count != 0U ||
               !src->outputs || !target_constraint_is(&src->outputs[0], "=r"))))
        return false;
    expr = minic_c0_program_expression(program,
        form == 1U ? src->inputs[0].expression : src->outputs[0].expression);
    if (!expr || !minic_type_is_integer(expr->type) ||
        (form == 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expr->type)))) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool x86_linux_iret_to_self_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                          const MinicInlineAsm *src,
                                          MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *tmp, *stack;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_iret_to_self_form(src->template_text, src->template_length) ||
        src->output_count != 2U || !src->outputs || src->input_count ||
        !src->has_memory_clobber || src->clobber_count != 2U ||
        src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "cc") || src->label_count ||
        !target_constraint_is(&src->outputs[0], "=&r") ||
        !target_constraint_is(&src->outputs[1], "+r")) return false;
    tmp = minic_c0_program_expression(program, src->outputs[0].expression);
    stack = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!tmp || !stack || tmp->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(tmp->type, minic_type_unsigned_int()) ||
        !minic_type_equal(stack->type, minic_type_unsigned_long())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_int3_selftest_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                            const MinicInlineAsm *src,
                                            MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *stack, *address;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_int3_selftest_form(src->template_text, src->template_length) ||
        src->output_count != 1U || !src->outputs || src->input_count != 1U ||
        !src->inputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 1U || src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->inputs[0], "D")) return false;
    stack = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!stack || !address ||
        !minic_type_equal(stack->type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(address->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_lidt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *descriptor;
    const MinicRecord *record;
    MinicType unqualified;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_descriptor_table_form(
            src->template_text, src->template_length) ||
        (src->output_count != 0U && src->input_count != 0U) ||
        (src->output_count + src->input_count != 1U) ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        (minic_x86_64_linux_descriptor_table_form(
             src->template_text, src->template_length) >= 3U
             ? (!src->outputs || !target_constraint_is(&src->outputs[0], "=m"))
             : (!src->inputs || !target_constraint_is(&src->inputs[0], "m")))) return false;
    descriptor = minic_c0_program_expression(
        program, src->input_count ? src->inputs[0].expression : src->outputs[0].expression);
    if (!descriptor || descriptor->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(descriptor->type, &unqualified) ||
        !minic_type_is_record(unqualified)) return false;
    record = minic_c0_program_record(program, unqualified.record_id);
    if (!record || !record->is_complete || record->is_union ||
        !record->is_packed || record->field_count != 2U ||
        !record->fields || record->fields[0].is_bit_field ||
        record->fields[1].is_bit_field ||
        !record->fields[0].name || !record->fields[1].name ||
        strcmp(record->fields[0].name, "size") != 0 ||
        strcmp(record->fields[1].name, "address") != 0 ||
        !minic_type_equal(record->fields[0].type, minic_type_unsigned_short()) ||
        !minic_type_equal(record->fields[1].type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = src->output_count
                             ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
                             : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_boot_segments_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value;
    MinicType type;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_boot_segments_template(
            src->template_text, src->template_length) ||
        src->output_count != 0U || src->input_count != 1U || !src->inputs ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || src->label_count != 0U ||
        !target_constraint_is(&src->inputs[0], "a")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &type) ||
        !minic_type_is_integer(type) ||
        (type.integer_rank != MINIC_INTEGER_RANK_INT &&
         type.integer_rank != MINIC_INTEGER_RANK_SHORT &&
         type.integer_rank != MINIC_INTEGER_RANK_LONG)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_is_u64_type(MinicType type) {
    MinicType unqualified;
    return minic_type_unqualified(type, &unqualified) &&
           (minic_type_equal(unqualified, minic_type_unsigned_long()) ||
            minic_type_equal(unqualified, minic_type_unsigned_long_long()));
}
/* Linux include/linux/math64.h: mul_u64_u64_div_u64 uses one RAX result,
 * three genuine input values and the explicit RDX clobber. A 128-bit
 * intermediate is mandatory: (a * mul) / div cannot be truncated to u64. */
static bool x86_linux_muldiv_u64_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const char *const constraints[] = {"=a", "a", "rm", "rm"};
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_muldiv_u64_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 1U || source->input_count != 3U ||
        !source->outputs || !source->inputs || source->has_memory_clobber ||
        source->clobber_count != 1U || source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        source->register_clobbers[0].name_length != 3U ||
        memcmp(source->register_clobbers[0].name, "rdx", 3U) != 0)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U ?
            &source->outputs[0] : &source->inputs[i - 1U];
        const MinicExpression *expr;
        if (!target_constraint_is(op, constraints[i]) ||
            op->access != (i == 0U ? MINIC_INLINE_ASM_OPERAND_WRITE_ONLY :
                           MINIC_INLINE_ASM_OPERAND_READ_ONLY)) return false;
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || (i == 0U && expr->value_category != MINIC_VALUE_LVALUE) ||
            (i == 0U && minic_type_is_const(expr->type)) ||
            !x86_linux_is_u64_type(expr->type)) return false;
        contract->kinds[i] = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    return true;
}

static bool x86_linux_crash_segment_snapshot_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_crash_segment_snapshot_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 1U || source->input_count || !source->outputs ||
        source->clobber_count || source->has_memory_clobber ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)
        return false;
    output = minic_c0_program_expression(program, source->outputs[0].expression);
    if (!output || output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type, &type) ||
        !(minic_type_equal(type, minic_type_unsigned_short()) ||
          minic_type_equal(type, minic_type_unsigned_long()))) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool x86_linux_via_xstore_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const char *const constraints[] = {"=m", "=a", "+d", "+D"};
    const MinicExpression *expressions[4], *mem_base;
    MinicType types[4], pointee;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_via_xstore_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 4U || source->input_count ||
        !source->outputs || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = &source->outputs[i];
        if (!target_constraint_is(op, constraints[i]) ||
            op->access != (i < 2U ? MINIC_INLINE_ASM_OPERAND_WRITE_ONLY :
                           MINIC_INLINE_ASM_OPERAND_READ_WRITE)) return false;
        expressions[i] = minic_c0_program_expression(program, op->expression);
        if (!expressions[i] ||
            expressions[i]->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expressions[i]->type) ||
            !minic_type_unqualified(expressions[i]->type, &types[i]))
            return false;
    }
    if (!minic_type_equal(types[0], minic_type_unsigned_int()) ||
        !minic_type_equal(types[1], minic_type_unsigned_int()) ||
        !minic_type_equal(types[2], minic_type_unsigned_int()) ||
        !minic_type_pointee(types[3], &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_int()) ||
        expressions[0]->kind != MINIC_EXPRESSION_DEREFERENCE ||
        expressions[3]->kind != MINIC_EXPRESSION_LOCAL)
        return false;
    mem_base = minic_c0_program_expression(
        program, expressions[0]->value.unary.operand);
    if (!mem_base || mem_base->kind != MINIC_EXPRESSION_LOCAL ||
        mem_base->value.local_id != expressions[3]->value.local_id)
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_kaslr_mul_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const char *const names[] = {"=a", "=d", "a", "rm"};
    unsigned int i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_kaslr_mul_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 2U || source->input_count != 2U ||
        !source->outputs || !source->inputs || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i < 2U ?
            &source->outputs[i] : &source->inputs[i - 2U];
        const MinicExpression *expr;
        MinicType type;
        if (!target_constraint_is(op, names[i]) ||
            op->access != (i < 2U ? MINIC_INLINE_ASM_OPERAND_WRITE_ONLY :
                        MINIC_INLINE_ASM_OPERAND_READ_ONLY)) return false;
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || (i < 2U && expr->value_category != MINIC_VALUE_LVALUE) ||
            (i < 2U && minic_type_is_const(expr->type)) ||
        !minic_type_unqualified(expr->type, &type) ||
        !x86_linux_is_u64_type(type)) return false;
        contract->kinds[i] = i < 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    return true;
}

static bool x86_linux_gen_endbr_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *expression;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_gen_endbr_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 1U || source->input_count != 0U ||
        !source->outputs || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=&r") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        !source->outputs[0].name || source->outputs[0].name_length != 5U ||
        memcmp(source->outputs[0].name, "endbr", 5U) != 0)
        return false;
    expression = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expression->type) ||
        !minic_type_unqualified(expression->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int())) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool x86_linux_preempt_gs_dec_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *memory, *zero;
    MinicType mem_type, flag_type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_preempt_gs_dec_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 2U || source->input_count ||
        !source->outputs || !source->has_memory_clobber ||
        source->clobber_count != 1U || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "+m") ||
        !target_constraint_is(&source->outputs[1], "=@cce") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
        source->outputs[1].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        !source->outputs[0].name || source->outputs[0].name_length != 3U ||
        memcmp(source->outputs[0].name, "var", 3U) != 0)
        return false;
    memory = minic_c0_program_expression(
        program, source->outputs[0].expression);
    zero = minic_c0_program_expression(
        program, source->outputs[1].expression);
    if (!memory || !zero ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        zero->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(memory->type) || minic_type_is_const(zero->type) ||
        !minic_type_unqualified(memory->type, &mem_type) ||
        !minic_type_unqualified(zero->type, &flag_type) ||
        !minic_type_equal(mem_type, minic_type_int()) ||
        !minic_type_equal(flag_type, minic_type_bool())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_preempt_static_call_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_preempt_static_call_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *stack;
    const MinicFixedRegisterBinding *fixed;
    MinicType type;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 1U || source->input_count ||
        !source->outputs || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "+r") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)
        return false;
    stack = minic_c0_program_expression(program,
                                        source->outputs[0].expression);
    /* RSP is deliberately read-only to ordinary C expressions. GNU +r here
     * expresses CALL-frame liveness, not permission to write the stack
     * pointer, so the exact fixed RSP register is the sufficient gate. */
    if (!stack || !minic_type_unqualified(stack->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long())) return false;
    fixed = stack->kind == MINIC_EXPRESSION_FIXED_REGISTER ?
        minic_c0_program_fixed_register_binding(
            program, stack->value.fixed_register_binding_id) :
        stack->kind == MINIC_EXPRESSION_LOCAL ?
        minic_c0_program_local_fixed_register_binding(
            program, stack->value.local_id) : NULL;
    if (!fixed || !fixed->register_name ||
        strcmp(fixed->register_name, "rsp") != 0) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    /* Like an ordinary externally visible call, the trampoline can modify
     * arbitrary memory even without a GNU `memory` compiler clobber. */
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

bool minic_x86_64_qualify_inline_asm_goto(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *source) {
    if (invocation == NULL) return false;
    static const char *const names[] = {"feature", "bitnum", "cap_byte"};
    static const char *const labels[] = {"t_yes", "t_no"};
    const MinicExpression *expression;
    MinicTargetInlineAsmContract contract;
    size_t i;
    /* Both target-qualified shapes must pass through the one Core symbolic
     * immediate resolver; it is not valid for the static-key pointer to be
     * materialized as a runtime register in StructuredInlineAsm. */
    (void)memset(&contract, 0, sizeof(contract));
    if (x86_linux_jump_label_source(program, source, &contract)) return true;
    if (!program || !source || !source->is_goto || source->is_volatile ||
        !minic_x86_64_linux_cpu_has_goto_form(
            source->template_text, source->template_length) ||
        source->output_count != 0U || source->input_count != 3U ||
        !source->inputs || source->label_count != 2U || !source->labels ||
        source->clobber_count != 0U || source->register_clobber_count != 0U ||
        source->has_memory_clobber) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        if (!target_constraint_is(input, "i") ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !input->name || input->name_length != strlen(names[i]) ||
            memcmp(input->name, names[i], input->name_length) != 0)
            return false;
        expression = minic_c0_program_expression(program, input->expression);
        if (!expression || (i == 2U
            ? !minic_type_is_pointer(expression->type)
            : !minic_type_is_integer(expression->type))) return false;
    }
    for (i = 0U; i < 2U; ++i) {
        const MinicInlineAsmLabel *label = &source->labels[i];
        const MinicStatement *target_statement;
        if (!label->name || label->name_length != strlen(labels[i]) ||
            memcmp(label->name, labels[i], label->name_length) != 0 ||
            label->target_statement == MINIC_STATEMENT_INVALID) return false;
        target_statement = minic_c0_program_statement(
            program, label->target_statement);
        if (!target_statement || target_statement->kind != MINIC_STATEMENT_LABEL)
            return false;
    }
    return true;
}
static bool x86_linux_three_call_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract);


static bool x86_linux_irq_stack_call_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const expected_clobbers[] = {
        "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10"
    };
    const MinicExpression *tos, *stack, *callee, *input, *regs;
    const MinicFixedRegisterBinding *binding;
    MinicType type;
    const unsigned int form = src ? minic_x86_64_linux_irq_stack_call_form(
        src->template_text, src->template_length) : 0U;
    size_t i;
    if (!program || !src || !contract || form == 0U ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 2U || !src->outputs ||
        src->input_count != (form == 1U ? 3U : form == 2U ? 4U : 2U) ||
        !src->inputs ||
        src->label_count || !src->has_memory_clobber ||
        src->register_clobber_count != 9U || !src->register_clobbers ||
        src->clobber_count != 10U ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->outputs[1], "+r") ||
        !target_constraint_is(&src->inputs[0], "i")) return false;
    for (i = 1U; i < src->input_count; ++i)
        if (!target_constraint_is(&src->inputs[i], "r")) return false;
    for (i = 0U; i < 9U; ++i)
        if (!clobber_is(&src->register_clobbers[i], expected_clobbers[i])) {
            return false;
        }
    tos = minic_c0_program_expression(program, src->outputs[0].expression);
    stack = minic_c0_program_expression(program, src->outputs[1].expression);
    callee = minic_c0_program_expression(program, src->inputs[0].expression);
    input = minic_c0_program_expression(program, src->inputs[1].expression);
    regs = form == 3U ? NULL : minic_c0_program_expression(
        program, src->inputs[2].expression);
    if (!tos || !stack || !callee || !input || (form != 3U && !regs) ||
        tos->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(tos->type) ||
        !minic_type_unqualified(stack->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()) ||
        callee->kind != MINIC_EXPRESSION_FUNCTION ||
        !minic_type_is_pointer(callee->type) ||
        !minic_type_equal(tos->type, input->type) ||
        (form != 3U && !minic_type_is_pointer(regs->type))) return false;
    binding = stack->kind == MINIC_EXPRESSION_FIXED_REGISTER ?
        minic_c0_program_fixed_register_binding(
            program, stack->value.fixed_register_binding_id) :
        stack->kind == MINIC_EXPRESSION_LOCAL ?
        minic_c0_program_local_fixed_register_binding(
            program, stack->value.local_id) : NULL;
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0) return false;
    binding = tos->kind == MINIC_EXPRESSION_FIXED_REGISTER ?
        minic_c0_program_fixed_register_binding(
            program, tos->value.fixed_register_binding_id) :
        tos->kind == MINIC_EXPRESSION_LOCAL ?
        minic_c0_program_local_fixed_register_binding(
            program, tos->value.local_id) : NULL;
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "r11") != 0) return false;
    if (form == 2U) {
        input = minic_c0_program_expression(program, src->inputs[3].expression);
        if (!input || !minic_type_is_integer(input->type)) return false;
    }
    contract->operand_count = 2U + src->input_count;
    /* ASM never writes the R11 `tos` value and restores RSP before exit.
     * GNU's +r modes are register-liveness contracts, not C stores. Keep
     * both in their live SSA form so address-of a register local is avoided. */
    for (i = 0U; i < contract->operand_count; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_three_call_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value;
    const MinicExpression *address;
    const MinicFixedRegisterBinding *fixed;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_three_call_alternative_form(
            src->template_text, src->template_length) ||
        src->output_count != 2U || src->input_count != 4U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 4U ||
        src->register_clobber_count != 3U || !src->register_clobbers ||
        !target_constraint_is(&src->outputs[0], "=D") ||
        !target_constraint_is(&src->outputs[1], "+r") ||
        !target_constraint_is(&src->inputs[0], "i") ||
        !target_constraint_is(&src->inputs[1], "i") ||
        !target_constraint_is(&src->inputs[2], "i") ||
        !target_constraint_is(&src->inputs[3], "0") ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !clobber_is(&src->register_clobbers[1], "rax") ||
        !clobber_is(&src->register_clobbers[2], "rcx")) return false;
    value = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!value || value->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(value->type) ||
        !address ||
        !minic_type_is_integer(address->type)) return false;
    /* +r on current_stack_pointer must truly bind RSP, never a freely
     * allocated general register or synthesized read/write C temporary. */
    if (address->kind != MINIC_EXPRESSION_FIXED_REGISTER) return false;
    fixed = minic_c0_program_fixed_register_binding(
        program, address->value.fixed_register_binding_id);
    if (!fixed || !fixed->register_name ||
        strcmp(fixed->register_name, "rsp") != 0) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *fn = minic_c0_program_expression(
            program, src->inputs[i].expression);
        if (!fn || fn->kind != MINIC_EXPRESSION_FUNCTION ||
            !minic_type_is_pointer(fn->type)) return false;
    }
    value = minic_c0_program_expression(program, src->inputs[3].expression);
    if (!value || !minic_type_is_pointer(value->type)) return false;
    contract->operand_count = 6U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    /* A CALL restores RSP before the instruction ends. Preserve its fixed
     * identity as an input Core value: there is no addressable C lvalue for
     * a hardware stack pointer and no post-asm register write to commit. */
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    for (i = 2U; i < 6U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_control_register_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_control_register_form(
        source->template_text, source->template_length) : 0U;
    const bool read_cr = form != 0U && form <= 4U;
    const bool plain_read_cr = read_cr && source->input_count == 0U;
    const bool tied_write_control = (form == 7U || form == 8U) &&
        source->output_count == 1U;
    const bool plain_write_control = (form == 7U || form == 8U) &&
        source->output_count == 0U;
    const MinicExpression *value, *fake_memory;
    MinicType plain;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count != 0U ||
        source->register_clobber_count != 0U ||
        source->output_count != (read_cr || tied_write_control ? 1U : 0U) ||
        source->input_count != (tied_write_control || plain_read_cr ? 0U : 1U) ||
        (!tied_write_control && !plain_read_cr && !source->inputs) ||
        ((read_cr || tied_write_control) && !source->outputs) ||
        source->clobber_count != (read_cr || plain_write_control ? 0U : 1U) ||
        source->has_memory_clobber == (read_cr || plain_write_control))
        return false;
    if (read_cr) {
        if (!target_constraint_is(&source->outputs[0], "=r") ||
            (!plain_read_cr &&
             !target_constraint_is(&source->inputs[0], "m"))) return false;
        value = minic_c0_program_expression(
            program, source->outputs[0].expression);
        fake_memory = plain_read_cr ? NULL : minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!value || value->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(value->type) ||
            !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            (!plain_read_cr &&
             (!fake_memory || fake_memory->value_category != MINIC_VALUE_LVALUE ||
              !minic_type_unqualified(fake_memory->type, &plain) ||
              !minic_type_equal(plain, minic_type_unsigned_int())))) return false;
        contract->operand_count = plain_read_cr ? 1U : 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (!plain_read_cr)
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    } else if (tied_write_control) {
        if (!target_constraint_is(&source->outputs[0], "+r")) return false;
        value = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!value || value->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(value->type) ||
            !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long())) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    } else {
        if (!target_constraint_is(&source->inputs[0], "r")) return false;
        value = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!value || !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long())) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_tlb_cache_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicInlineAsm *src, MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = src ? minic_x86_64_linux_tlb_cache_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *expr;
    MinicType plain;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->label_count != 0U ||
        src->register_clobber_count != 0U) return false;
    if (form == 1U) {
        if (src->output_count != 0U || src->input_count != 1U ||
            !src->inputs || src->clobber_count != 1U ||
            !src->has_memory_clobber ||
            !target_constraint_is(&src->inputs[0], "r")) return false;
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (!expr ||
            (!minic_type_is_pointer(expr->type) &&
             (!minic_type_unqualified(expr->type, &plain) ||
              !minic_type_equal(plain, minic_type_unsigned_long()))))
            return false;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    } else {
        if (src->output_count != 1U || src->input_count != 0U ||
            !src->outputs || src->clobber_count != 0U ||
            src->has_memory_clobber ||
            !target_constraint_is(&src->outputs[0], "+m")) return false;
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type)) return false;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    }
    contract->operand_count = 1U;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_alternative_wrmsr_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value;
    MinicType plain;
    size_t i;
    static const char *const constraints[] = {"c", "a", "d", "i"};
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_alternative_wrmsr_form(
            src->template_text, src->template_length) ||
        src->output_count != 0U || src->input_count != 4U ||
        !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < 4U; ++i) {
        if (!target_constraint_is(&src->inputs[i], constraints[i]))
            return false;
        value = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!value || !minic_type_unqualified(value->type, &plain) ||
            !minic_type_is_integer(plain)) return false;
        if (i < 3U &&
            !minic_type_equal(plain, minic_type_unsigned_int())) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool target_constraint_is(const MinicInlineAsmOperand *op, const char *s) {
    const size_t n = s ? strlen(s) : 0U;
    return op && s && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, s, n) == 0;
}

static bool x86_linux_ip_here_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *dst;
    MinicType plain;
    if (program == NULL || src == NULL || contract == NULL ||
        src->is_goto || src->output_count != 1U ||
        src->input_count != 0U || src->outputs == NULL ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber || src->label_count != 0U ||
        src->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        !target_constraint_is(&src->outputs[0], "=r") ||
        !minic_x86_64_linux_ip_here_template(
            src->template_text, src->template_length))
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    if (dst == NULL || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !minic_type_unqualified(dst->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long()) &&
         !minic_type_equal(plain, minic_type_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->target_machine_form_variant = 1U;
    return true;
}
static bool c_is(const MinicInlineAsmOperand *op, const char *s) {
    return target_constraint_is(op, s);
}
static bool clobber_is(const MinicInlineAsmRegisterClobber *op, const char *s) {
    const size_t n = strlen(s);
    return op->name && op->name_length == n && memcmp(op->name, s, n) == 0;
}

static bool x86_linux_zero_barrier_source(
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    unsigned int form;
    bool mem;
    if (source == NULL || contract == NULL || !source->is_volatile ||
        source->is_goto || source->input_count != 0U ||
        source->output_count != 0U || source->label_count != 0U)
        return false;
    form = minic_x86_64_linux_zero_barrier_form(
        source->template_text, source->template_length);
    if (form == 0U) return false;
    mem = source->has_memory_clobber;
    if (form <= 4U && !mem) return false;
    if (form == 1U) {
        if (source->clobber_count != 2U ||
            source->register_clobber_count != 1U ||
            source->register_clobbers == NULL ||
            !clobber_is(&source->register_clobbers[0], "cc"))
            return false;
    } else if (source->register_clobber_count != 0U ||
               source->clobber_count != (mem ? 1U : 0U)) {
        return false;
    }
    contract->operand_count = 0U;
    contract->target_machine_form_variant = form;
    return true;
}
static bool x86_counter_source(const MinicTargetInvocation *invocation, const MinicC0Program *p,const MinicInlineAsm *s,
                               MinicTargetInlineAsmContract *ct) {
    (void)invocation;
    const unsigned int f=s?minic_x86_64_linux_counter_form(
        s->template_text,s->template_length):0U;
    const MinicExpression *e0,*e1,*v;
    MinicType t0,t1,ti;
    if(!p||!s||!ct||!f||!s->is_volatile||s->is_goto||
       !s->outputs||s->output_count!=2U||
       s->input_count!=((f==2U||f==5U)?1U:0U)||
       ((f==2U||f==5U)&&!s->inputs)||
       s->label_count||s->has_memory_clobber||
       s->clobber_count!=((f==3U||f==4U)?1U:0U)||
       s->register_clobber_count!=((f==3U||f==4U)?1U:0U)||
       ((f==3U||f==4U)&&(!s->register_clobbers||
        !clobber_is(&s->register_clobbers[0],"ecx")))||
       !c_is(&s->outputs[0],"=a")||!c_is(&s->outputs[1],"=d")||
       ((f==2U||f==5U)&&!c_is(&s->inputs[0],"c")))return false;
    e0=minic_c0_program_expression(p,s->outputs[0].expression);
    e1=minic_c0_program_expression(p,s->outputs[1].expression);
    if(!e0||!e1||e0->value_category!=MINIC_VALUE_LVALUE||
       e1->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(e0->type)||minic_type_is_const(e1->type)||
       !minic_type_unqualified(e0->type,&t0)||
       !minic_type_unqualified(e1->type,&t1)||
       !minic_type_equal(t0,t1)||t0.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(t0)||t0.pointer_depth||
       (t0.integer_rank!=MINIC_INTEGER_RANK_INT&&
        t0.integer_rank!=MINIC_INTEGER_RANK_LONG&&
        t0.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG))return false;
    if(f==2U||f==5U){
        v=minic_c0_program_expression(p,s->inputs[0].expression);
        if(!v||!minic_type_unqualified(v->type,&ti)||
           ti.base_kind!=MINIC_TYPE_BASE_INT||
           !minic_type_is_integer(ti)||ti.pointer_depth||
           ti.integer_rank!=MINIC_INTEGER_RANK_INT)return false;
    }
    ct->operand_count=(f==2U||f==5U)?3U:2U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    if(f==2U||f==5U)
        ct->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
/* XCR0/XSS write is architecturally privileged and requires all three
 * physical 32-bit input registers. A plain C call or memory store would not
 * update extended processor state, so keep its effect in the x86 target. */
static bool x86_xsetbv_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                             const MinicInlineAsm *source,
                             MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const constraints[] = {"a", "d", "c"};
    size_t i;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->template_length != 6U ||
        memcmp(source->template_text, "xsetbv", 6U) != 0 ||
        source->output_count != 0U || source->input_count != 3U ||
        !source->inputs || source->label_count != 0U ||
        source->clobber_count != 0U || source->has_memory_clobber ||
        source->register_clobber_count != 0U) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        MinicType type;
        if (!target_constraint_is(&source->inputs[i], constraints[i]) ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !expression || !minic_type_unqualified(expression->type, &type) ||
            !minic_type_is_integer(type) || type.pointer_depth != 0U ||
            type.integer_rank != MINIC_INTEGER_RANK_INT) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 3U;
    return true;
}

static bool x86_fxsave_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                             const MinicInlineAsm *source,
                             MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_fxsave_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *destination;
    size_t size, alignment;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        !source->outputs || source->output_count != 1U ||
        source->input_count != 0U || source->label_count != 0U ||
        source->has_memory_clobber || source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        !target_constraint_is(&source->outputs[0], form == 1U ? "+m" : "=m") ||
        source->outputs[0].access !=
            (form == 1U ? MINIC_INLINE_ASM_OPERAND_READ_WRITE :
                          MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)) return false;
    if (form == 2U &&
        (!source->outputs[0].name || source->outputs[0].name_length != 2U ||
         memcmp(source->outputs[0].name, "fx", 2U) != 0)) return false;
    destination = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!destination || destination->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(destination->type) ||
        !minic_data_layout_type(
            invocation,
            program, destination->type, &size, &alignment) ||
        size < 512U || alignment < 16U) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 1U ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_x87_probe_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
                                 const MinicInlineAsm *source,
                                 MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    size_t i;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || !minic_x86_64_linux_x87_probe_form(
            source->template_text, source->template_length) ||
        !source->outputs || source->output_count != 2U ||
        source->input_count != 0U || source->label_count != 0U ||
        source->has_memory_clobber || source->clobber_count != 0U ||
        source->register_clobber_count != 0U) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicExpression *destination = minic_c0_program_expression(
            program, source->outputs[i].expression);
        MinicType value_type;
        if (!target_constraint_is(&source->outputs[i], "+m") ||
            source->outputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
            !destination || destination->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(destination->type) ||
            !minic_type_unqualified(destination->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_short()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    }
    contract->operand_count = 2U;
    contract->implicit_memory_effect = true;
    return true;
}
static bool x86_hweight_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *ct){
    (void)invocation;
    const unsigned int w=s?minic_x86_64_hweight_alternative_form(
       s->template_text,s->template_length):0U;
    const MinicExpression *out,*arg;MinicType to,ti;
    if(!program||!s||!ct||!w||s->is_volatile||s->is_goto||
       s->has_memory_clobber||s->label_count||s->clobber_count||
       s->register_clobber_count||s->output_count!=1U||
       s->input_count!=1U||!s->outputs||!s->inputs||
       !c_is(&s->outputs[0],"=a")||!c_is(&s->inputs[0],"D"))return false;
    out=minic_c0_program_expression(program,s->outputs[0].expression);
    arg=minic_c0_program_expression(program,s->inputs[0].expression);
    if(!out||!arg||out->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(out->type)||
       !minic_type_unqualified(out->type,&to)||
       !minic_type_unqualified(arg->type,&ti)||
       to.base_kind!=MINIC_TYPE_BASE_INT||
       ti.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(to)||!minic_type_is_integer(ti)||
       to.pointer_depth||ti.pointer_depth)return false;
    if(w==4U?
       (to.integer_rank!=MINIC_INTEGER_RANK_INT||
        ti.integer_rank!=MINIC_INTEGER_RANK_INT):
       ((to.integer_rank!=MINIC_INTEGER_RANK_LONG&&
         to.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)||
        (ti.integer_rank!=MINIC_INTEGER_RANK_LONG&&
         ti.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)))return false;
    ct->operand_count=2U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_linux_vmware_hypercall_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const regs[] = {"a", "c", "d", "b"};
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_vmware_hypercall_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 4U || source->input_count != 4U ||
        !source->outputs || !source->inputs ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count) return false;
    for (i = 0U; i < 8U; ++i) {
        const bool output = i < 4U;
        const MinicInlineAsmOperand *operand = output ?
            &source->outputs[i] : &source->inputs[i - 4U];
        const MinicExpression *expression = minic_c0_program_expression(
            program, operand->expression);
        MinicType type;
        char expected[4];
        expected[0] = '='; expected[1] = regs[i % 4U][0];
        expected[2] = '\0';
        if (!c_is(operand, output ? expected : regs[i % 4U]) ||
            !expression || !minic_type_unqualified(expression->type, &type) ||
            !(minic_type_equal(type, minic_type_unsigned_int()) ||
              (!output && minic_type_equal(type, minic_type_int()))) ||
            (output && (expression->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expression->type)))) return false;
        contract->kinds[i] = output ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 8U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_legacy_fnsave_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *image;
    size_t size, alignment;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_legacy_fnsave_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 1U || source->input_count || !source->outputs ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count ||
        !c_is(&source->outputs[0], "=m") ||
        !source->outputs[0].name ||
        source->outputs[0].name_length != 2U ||
        memcmp(source->outputs[0].name, "fp", 2U)) return false;
    image = minic_c0_program_expression(program,
        source->outputs[0].expression);
    if (!image || image->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(image->type) ||
        !minic_data_layout_type(
            invocation, program, image->type,
            &size, &alignment) || size < 108U) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    return true;
}

static bool x86_linux_kexec_load_segments_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *selector;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_kexec_load_segments_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count || source->input_count != 1U || !source->inputs ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count || !c_is(&source->inputs[0], "a"))
        return false;
    selector = minic_c0_program_expression(program,
        source->inputs[0].expression);
    if (!selector || !minic_type_unqualified(selector->type, &type) ||
        !minic_type_is_integer(type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_efi_kernel_transfer_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *entry, *boot;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_efi_kernel_transfer_form(
            source->template_text, source->template_length) ||
        source->is_goto || source->label_count || source->output_count ||
        source->input_count != 2U || !source->inputs ||
        source->clobber_count || source->has_memory_clobber ||
        source->register_clobber_count ||
        !c_is(&source->inputs[0], "r") ||
        !c_is(&source->inputs[1], "S")) return false;
    entry = minic_c0_program_expression(program,
        source->inputs[0].expression);
    boot = minic_c0_program_expression(program,
        source->inputs[1].expression);
    if (!entry || !boot ||
        !minic_type_unqualified(entry->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(boot->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_linux_msr_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form=src?minic_x86_64_linux_msr_form(
        src->template_text,src->template_length):0U;
    const MinicExpression *e;MinicType type;
    size_t i;
    if(!program||!src||!contract||!form)return false;
    if(form==3U || form==4U) {
        const size_t outputs=form==3U?3U:1U;
        const size_t inputs=form==3U?1U:3U;
        if(!src->is_volatile || src->is_goto || src->label_count ||
           !src->outputs || !src->inputs ||
           src->output_count!=outputs || src->input_count!=inputs ||
           src->clobber_count!=(form==4U?1U:0U) ||
           src->has_memory_clobber!=(form==4U) ||
           src->register_clobber_count ||
           !c_is(&src->outputs[0],form==3U?"=r":"=a") ||
           (form==3U && (!c_is(&src->outputs[1],"=a") ||
                          !c_is(&src->outputs[2],"=d"))) ||
           !c_is(&src->inputs[0],"c") ||
           (form==4U && (!c_is(&src->inputs[1],"0") ||
                          !c_is(&src->inputs[2],"d"))))return false;
        for(i=0U;i<outputs+inputs;++i) {
            const bool output=i<outputs;
            const MinicInlineAsmOperand *op=output ? &src->outputs[i] :
                &src->inputs[i-outputs];
            const MinicExpression *v=minic_c0_program_expression(program,op->expression);
            MinicType vt;
            const bool wide=form==3U && output && i!=0U;
            if(!v || !minic_type_unqualified(v->type,&vt) ||
               !minic_type_is_integer(vt) || vt.pointer_depth ||
               vt.base_kind!=MINIC_TYPE_BASE_INT ||
               vt.integer_rank!=(wide?MINIC_INTEGER_RANK_LONG:MINIC_INTEGER_RANK_INT) ||
               (output && (v->value_category!=MINIC_VALUE_LVALUE ||
                           minic_type_is_const(v->type))))return false;
        }
        contract->operand_count=outputs+inputs;
        for(i=0U;i<contract->operand_count;++i)
            contract->kinds[i]=i<outputs?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if(!src->is_volatile||src->is_goto||src->label_count||
       src->output_count!=((form==1U||form==5U)?2U:0U)||
       src->input_count!=((form==1U||form==5U)?1U:3U)||
       ((form==1U||form==5U)&&!src->outputs)||!src->inputs||
       src->clobber_count!=((form==1U||form==5U)?0U:1U)||
       src->has_memory_clobber!=(form==2U||form==6U)||
       src->register_clobber_count)return false;
    if(form==1U || form==5U) {
        if(!c_is(&src->outputs[0],"=a")||
           !c_is(&src->outputs[1],"=d")||
           !c_is(&src->inputs[0],"c"))return false;
    }else if(!c_is(&src->inputs[0],"c")||
             !c_is(&src->inputs[1],"a")||
             !c_is(&src->inputs[2],"d"))return false;
    for(i=0U;i<src->output_count+src->input_count;++i){
        const bool output=i<src->output_count;
        const MinicInlineAsmOperand *op=output?
            &src->outputs[i]:&src->inputs[i-src->output_count];
        e=minic_c0_program_expression(program,op->expression);
        if(!e||!minic_type_unqualified(e->type,&type)||
           type.base_kind!=MINIC_TYPE_BASE_INT||
           !minic_type_is_integer(type)||type.pointer_depth||
           (output&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type)))||
           ((form==1U||form==5U)&&output?
              (type.integer_rank!=MINIC_INTEGER_RANK_LONG&&
               type.integer_rank!=MINIC_INTEGER_RANK_INT):
              type.integer_rank!=MINIC_INTEGER_RANK_INT))
            return false;
    }
    contract->operand_count=src->output_count+src->input_count;
    for(i=0U;i<contract->operand_count;++i)
        contract->kinds[i]=i<src->output_count?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_ip_fast_csum_source(const MinicTargetInvocation *invocation, const MinicC0Program *p,
    const MinicInlineAsm *s, MinicTargetInlineAsmContract *ct) {
    (void)invocation;
    const MinicExpression *expr;
    MinicType type;
    size_t i;
    if (!p || !s || !ct || s->is_volatile || s->is_goto ||
        !minic_x86_64_ip_fast_csum_template(s->template_text,s->template_length) ||
        !s->outputs || !s->inputs || s->output_count!=3U ||
        s->input_count!=2U || !s->has_memory_clobber ||
        s->clobber_count!=1U || s->register_clobber_count || s->label_count)
        return false;
    for(i=0U;i<3U;++i) {
        if(!c_is(&s->outputs[i],"=r"))return false;
        expr=minic_c0_program_expression(p,s->outputs[i].expression);
        if(!expr || expr->value_category!=MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type) ||
            !minic_type_unqualified(expr->type,&type) ||
            (i==1U ? !minic_type_is_pointer(type) :
                (!minic_type_is_integer(type) ||
                 type.integer_rank!=MINIC_INTEGER_RANK_INT))) return false;
    }
    if (!c_is(&s->inputs[0],"1") || !c_is(&s->inputs[1],"2"))return false;
    for(i=0U;i<2U;++i) {
        expr=minic_c0_program_expression(p,s->inputs[i].expression);
        if(!expr || !minic_type_unqualified(expr->type,&type) ||
           (i==0U ? !minic_type_is_pointer(type) :
             (!minic_type_is_integer(type) ||
              type.integer_rank!=MINIC_INTEGER_RANK_INT)))return false;
    }
    ct->operand_count=5U;
    for(i=0U;i<5U;++i)
        ct->kinds[i]=i<3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_checksum_source(const MinicTargetInvocation *invocation, const MinicC0Program *p,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *ct){
    (void)invocation;
    size_t i;
    if(!p||!src||!ct||src->is_volatile||src->is_goto||
       src->output_count!=1U||src->input_count!=2U||
       !src->outputs||!src->inputs||src->label_count||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_checksum_fold_template(
           src->template_text,src->template_length)||
       !c_is(&src->outputs[0],"=r")||
       !c_is(&src->inputs[0],"r")||!c_is(&src->inputs[1],"0"))
        return false;
    for(i=0U;i<3U;++i){
        const MinicInlineAsmOperand *op=i==0U?
            &src->outputs[0]:&src->inputs[i-1U];
        const MinicExpression *e=minic_c0_program_expression(
            p,op->expression);
        MinicType ty;
        if(!e||!minic_type_unqualified(e->type,&ty)||
           ty.base_kind!=MINIC_TYPE_BASE_INT||
           ty.integer_rank!=MINIC_INTEGER_RANK_INT||
           (i==0U&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type))))
            return false;
    }
    ct->operand_count=3U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    ct->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_percpu_gs_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract){
    (void)invocation;
    unsigned int op;size_t width,i;
    const size_t stable_width = src ? minic_x86_64_asm_percpu_stable_width(
        src->template_text,src->template_length) : 0U;
    const bool stable = stable_width != 0U;
    const MinicExpression *e;
    if(!program||!src||!contract||src->is_goto||src->label_count||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_linux_percpu_form(
         src->template_text,src->template_length,&op,&width)||
       src->output_count!=1U||
       src->input_count!=((op==2U||op==3U)?0U:1U)||
       !src->outputs||(src->input_count&&!src->inputs))
        return false;
    if(op==0U){
        if(!c_is(&src->outputs[0],width==1U?"=q":"=r")||
           !c_is(&src->inputs[0],stable?"p":"m"))return false;
    }else if(!c_is(&src->outputs[0],"+m")||
             (src->input_count&&!c_is(&src->inputs[0],
                width==1U?"qi":width==8U?"re":"ri")))
        return false;
    for(i=0U;i<1U+src->input_count;++i){
        const MinicInlineAsmOperand *a=i==0U?
            &src->outputs[0]:&src->inputs[0];
        MinicType ty;
        e=minic_c0_program_expression(program,a->expression);
        if(!e||!minic_type_unqualified(e->type,&ty)||
           /* A GNU per-CPU pointer slot is eight bytes, despite its
            * declared lvalue type not being integer. Other widths
            * and operations retain strict original type qualification. */
           (!(width==1U && minic_type_equal(ty,minic_type_bool())) &&
            !(width==4U && minic_type_is_enum(ty)) &&
            !(stable && i==1U && minic_type_is_pointer(ty)) &&
            !(width==8U && minic_type_is_pointer(ty) &&
              ((op==0U && i==1U) || (op==1U && i==0U))) &&
            (ty.base_kind!=MINIC_TYPE_BASE_INT||
             (width==1U?ty.integer_rank!=MINIC_INTEGER_RANK_CHAR:
              width==2U?ty.integer_rank!=MINIC_INTEGER_RANK_SHORT:
              width==4U?ty.integer_rank!=MINIC_INTEGER_RANK_INT:
                 (ty.integer_rank!=MINIC_INTEGER_RANK_LONG &&
                  ty.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG))))||
           (i==0U&&(e->value_category!=MINIC_VALUE_LVALUE||
                     (op==0U&&minic_type_is_const(e->type)))))
            return false;
    }
    contract->operand_count=1U+src->input_count;
    contract->kinds[0]=op==0U?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    if(src->input_count)
        contract->kinds[1]=op==0U && !stable?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_gs_tied_alu_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output, *mem, *seed;
    MinicType out_type, mem_type, seed_type;
    size_t opcode_width=0U, width;
    unsigned int op;
    if (program == NULL || src == NULL || contract == NULL ||
        src->is_goto || src->is_volatile ||
        src->output_count != 1U || src->input_count != 2U ||
        src->outputs == NULL || src->inputs == NULL ||
        src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !c_is(&src->outputs[0],"=r") ||
        !c_is(&src->inputs[0],"m") ||
        !c_is(&src->inputs[1],"0"))
        return false;
    op=minic_x86_64_gs_tied_alu_form(
        src->template_text, src->template_length, &opcode_width);
    if (!op) return false;
    output=minic_c0_program_expression(program,src->outputs[0].expression);
    mem=minic_c0_program_expression(program,src->inputs[0].expression);
    seed=minic_c0_program_expression(program,src->inputs[1].expression);
    if (!output || !mem || !seed ||
        output->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type,&out_type) ||
        !minic_type_unqualified(mem->type,&mem_type) ||
        !minic_type_unqualified(seed->type,&seed_type) ||
        !minic_type_is_integer(out_type) ||
        (out_type.integer_rank != MINIC_INTEGER_RANK_INT &&
         out_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         out_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    width = out_type.integer_rank==MINIC_INTEGER_RANK_INT ? 4U : 8U;
    if (opcode_width != 0U && opcode_width != width)
        return false;
    if (!minic_type_is_integer(mem_type) ||
        (width==4U
          ? mem_type.integer_rank != MINIC_INTEGER_RANK_INT
          : mem_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
            mem_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    if (minic_type_is_pointer(seed_type)) {
        if (width != 8U) return false;
    } else if (!minic_type_is_integer(seed_type) ||
        (width==4U
          ? seed_type.integer_rank != MINIC_INTEGER_RANK_INT
          : seed_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
            seed_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant=op;
    return true;
}

static bool x86_apic_amd_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form=src?minic_x86_64_linux_apic_amd_alt_form(
        src->template_text,src->template_length):0U;
    size_t i;
    if(!program||!src||!contract||!form||!src->is_volatile||src->is_goto||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||src->input_count!=3U||!src->inputs||
       src->output_count!=(form==1U?2U:0U)||
       (form==1U&&!src->outputs))return false;
    if(form==1U){
        const MinicExpression *reg,*mem,*seed,*memory_input,*zero;
        if(!c_is(&src->outputs[0],"=r")||
           !c_is(&src->outputs[1],"=m")||
           !c_is(&src->inputs[0],"i")||
           !c_is(&src->inputs[1],"0")||
           !c_is(&src->inputs[2],"m"))return false;
        reg=minic_c0_program_expression(program,src->outputs[0].expression);
        mem=minic_c0_program_expression(program,src->outputs[1].expression);
        zero=minic_c0_program_expression(program,src->inputs[0].expression);
        seed=minic_c0_program_expression(program,src->inputs[1].expression);
        memory_input=minic_c0_program_expression(program,src->inputs[2].expression);
        if(!reg||!mem||!seed||!zero||!memory_input||
           reg->value_category!=MINIC_VALUE_LVALUE||
           mem->value_category!=MINIC_VALUE_LVALUE||
           memory_input->value_category!=MINIC_VALUE_LVALUE||
           minic_type_is_const(reg->type)||minic_type_is_const(mem->type)||
           !minic_type_is_integer(reg->type)||
           reg->type.integer_rank!=MINIC_INTEGER_RANK_INT||
           !minic_type_is_integer(mem->type)||
           mem->type.integer_rank!=MINIC_INTEGER_RANK_INT||
           !minic_type_equal(reg->type,seed->type)||
           !minic_type_equal(mem->type,memory_input->type)||
           zero->kind!=MINIC_EXPRESSION_INTEGER||
           zero->value.integer_value!=0)return false;
        contract->operand_count=5U;
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[4]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->implicit_memory_effect=true;
        return true;
    }
    if(!c_is(&src->inputs[0],"a")||
       !c_is(&src->inputs[1],"d")||
       !c_is(&src->inputs[2],"r"))return false;
    for(i=0U;i<3U;++i){
        const MinicExpression *e=minic_c0_program_expression(
            program,src->inputs[i].expression);
        if(!e||e->kind!=MINIC_EXPRESSION_INTEGER||
           e->value.integer_value!=(i==2U?1:0))return false;
    }
    contract->operand_count=3U;
    for(i=0U;i<3U;++i)
        contract->kinds[i]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_prefetchw_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *imm,*memory;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       src->output_count!=0U||src->input_count!=2U||src->inputs==NULL||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_linux_prefetchw_alt_form(
           src->template_text,src->template_length)||
       !c_is(&src->inputs[0],"i")||!c_is(&src->inputs[1],"m"))
        return false;
    imm=minic_c0_program_expression(program,src->inputs[0].expression);
    memory=minic_c0_program_expression(program,src->inputs[1].expression);
    if(!imm||imm->kind!=MINIC_EXPRESSION_INTEGER||
       imm->value.integer_value!=0||
       !memory||memory->value_category!=MINIC_VALUE_LVALUE)
        return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->target_machine_form_variant=1U;
    return true;
}

static bool x86_linux_tcpudp_nofold_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *p, const MinicInlineAsm *s,
    MinicTargetInlineAsmContract *c) {
    (void)invocation;
    size_t i;
    if (!p || !s || !c || s->is_volatile || s->is_goto ||
        s->has_memory_clobber || s->label_count || s->clobber_count ||
        s->register_clobber_count || s->output_count != 1U ||
        s->input_count != 4U || !s->outputs || !s->inputs ||
        !minic_x86_64_linux_tcpudp_nofold_template(
            s->template_text, s->template_length) ||
        !target_constraint_is(&s->outputs[0], "=r") ||
        !target_constraint_is(&s->inputs[0], "g") ||
        !target_constraint_is(&s->inputs[1], "g") ||
        !target_constraint_is(&s->inputs[2], "g") ||
        !target_constraint_is(&s->inputs[3], "0")) return false;
    for (i = 0U; i < 5U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U ?
            &s->outputs[0] : &s->inputs[i - 1U];
        const MinicExpression *expr = minic_c0_program_expression(p, op->expression);
        MinicType plain;
        if (!expr || !minic_type_unqualified(expr->type, &plain) ||
            plain.base_kind != MINIC_TYPE_BASE_INT ||
            plain.integer_rank != MINIC_INTEGER_RANK_INT ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type)))) return false;
    }
    c->operand_count = 5U;
    c->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 1U; i < 5U; ++i)
        c->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_add32_carry_source(const MinicTargetInvocation *invocation, const MinicC0Program *p,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *c){
    (void)invocation;
    size_t i;
    if(!p||!s||!c||s->is_volatile||s->is_goto||
       s->has_memory_clobber||s->label_count||s->clobber_count||
       s->register_clobber_count||s->output_count!=1U||
       s->input_count!=2U||!s->outputs||!s->inputs||
       !minic_x86_64_add32_carry_template(
            s->template_text,s->template_length)||
       !c_is(&s->outputs[0],"=r")||
       !c_is(&s->inputs[0],"0")||!c_is(&s->inputs[1],"rm"))
        return false;
    for(i=0U;i<3U;++i){
        const MinicInlineAsmOperand *op=i==0U?
            &s->outputs[0]:&s->inputs[i-1U];
        const MinicExpression *expr=minic_c0_program_expression(
            p,op->expression);
        MinicType type;
        if(!expr||!minic_type_unqualified(expr->type,&type)||
           type.base_kind!=MINIC_TYPE_BASE_INT||
           type.integer_rank!=MINIC_INTEGER_RANK_INT||
           (i==0U&&(expr->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(expr->type))))
            return false;
    }
    c->operand_count=3U;
    c->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    c->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    c->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_nospec_source(const MinicC0Program *program,
    const MinicInlineAsm *src, uint32_t *width_out) {
    const MinicExpression *out, *size, *index;
    MinicType out_type, size_type, index_type;
    if (!program || !src || width_out == NULL ||
        !src->outputs || !src->inputs ||
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
    if (!out || !size || !index ||
        out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) ||
        !minic_type_unqualified(out->type, &out_type) ||
        !minic_type_equal(out_type, minic_type_unsigned_long()) ||
        !minic_type_unqualified(size->type, &size_type) ||
        !minic_type_unqualified(index->type, &index_type) ||
        !minic_type_equal(size_type, index_type) ||
        size_type.base_kind != MINIC_TYPE_BASE_INT ||
        (size_type.integer_rank != MINIC_INTEGER_RANK_INT &&
         size_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         size_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    /* Source owns C integer ranks; Machine receives only the byte width. */
    *width_out = size_type.integer_rank == MINIC_INTEGER_RANK_INT ? 4U : 8U;
    return true;
}
static bool x86_linux_raw_cmpxchg_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicX86TryCas form = src ? minic_x86_64_raw_cmpxchg_form(
        src->template_text, src->template_length) : MINIC_X86_TRYCAS_NONE;
    const MinicX86TryCasDesc *d = minic_x86_64_trycas_desc(form);
    size_t i;
    if (!program || !src || !contract || !d || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 2U || src->input_count != 2U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || src->label_count != 0U ||
        !target_constraint_is(&src->outputs[0], "=a") ||
        !target_constraint_is(&src->outputs[1], "+m") ||
        !target_constraint_is(&src->inputs[0], d->width == 1U ? "q" : "r") ||
        !target_constraint_is(&src->inputs[1], "0"))
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i < 2U
            ? &src->outputs[i] : &src->inputs[i - 2U];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        MinicType type;
        const unsigned int rank = d->width == 1U ? MINIC_INTEGER_RANK_CHAR :
            d->width == 2U ? MINIC_INTEGER_RANK_SHORT :
            d->width == 4U ? MINIC_INTEGER_RANK_INT : MINIC_INTEGER_RANK_NONE;
        if (e == NULL || (i < 2U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            !minic_type_unqualified(e->type, &type) ||
            (!(d->width == 8U && minic_type_is_pointer(type)) &&
             (type.base_kind != MINIC_TYPE_BASE_INT ||
             !minic_type_is_integer(type) || type.pointer_depth != 0U ||
             (d->width == 8U
                 ? (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                    type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)
                 : (unsigned int)type.integer_rank != rank))))
            return false;
    }
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant = (uint32_t)form;
    return true;
}
static bool x86_port_io_source(const MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract){
    (void)invocation;
    const MinicX86PortIo form=src?minic_x86_64_port_io_form(
        src->template_text,src->template_length):MINIC_X86_PORT_IO_NONE;
    const MinicX86PortIoDesc *d=minic_x86_64_port_io_description(form);
    const MinicExpression *data,*port;
    MinicType dt,pt;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||src->label_count||
       src->output_count!=(d->is_input?1U:0U)||
       src->input_count!=(d->is_input?1U:2U)||
       !src->inputs||(d->is_input&&!src->outputs))return false;
    if(d->is_input){
        if(!target_constraint_is(&src->outputs[0],"=a")||
           !target_constraint_is(&src->inputs[0],"Nd"))return false;
        data=minic_c0_program_expression(program,src->outputs[0].expression);
        port=minic_c0_program_expression(program,src->inputs[0].expression);
        if(!data||data->value_category!=MINIC_VALUE_LVALUE||
           minic_type_is_const(data->type))return false;
    }else{
        if(!target_constraint_is(&src->inputs[0],"a")||
           !target_constraint_is(&src->inputs[1],"Nd"))return false;
        data=minic_c0_program_expression(program,src->inputs[0].expression);
        port=minic_c0_program_expression(program,src->inputs[1].expression);
    }
    if(!data||!port||!minic_type_unqualified(data->type,&dt)||
       !minic_type_unqualified(port->type,&pt)||
       !minic_type_equal(pt,minic_type_unsigned_short())||
       !(d->width==1U?minic_type_equal(dt,minic_type_unsigned_char()):
         d->width==2U?minic_type_equal(dt,minic_type_unsigned_short()):
                      minic_type_equal(dt,minic_type_unsigned_int())))return false;
    contract->operand_count=2U;
    contract->kinds[0]=d->is_input?
       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant=(uint32_t)form;
    return true;
}

static bool x86_trycas_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
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
    /* Original Linux __raw_try_cmpxchg casts the memory carrier to
     * volatile u{8,16,32,64}, while __old/__new retain the source
     * operand's possibly SIGNED C type (e.g. atomic_t.counter is int).
     * CMPXCHG compares and exchanges bit patterns of an exact width; the
     * source signedness is not a register/ISA semantic constraint.
     * Admit only real primitive integer types of the SAME machine rank,
     * never bool, enum, pointer or widened/narrowed value operands.
     * This also matches the existing machine emitter's independent width
     * verification; no raw GNU template fallback is introduced. */
    if(!minic_type_equal(t[0],minic_type_bool()))
        return false;
    for(i=1U;i<4U;++i)
        if (!(d->width == 8U && minic_type_is_pointer(t[i])) &&
            (t[i].base_kind!=MINIC_TYPE_BASE_INT||
             !minic_type_is_integer(t[i])||t[i].pointer_depth!=0U||
             (d->width == 8U
                 ? (t[i].integer_rank != MINIC_INTEGER_RANK_LONG &&
                    t[i].integer_rank != MINIC_INTEGER_RANK_LONG_LONG)
                 : (unsigned int)t[i].integer_rank!=rank)))
            return false;
    contract->operand_count=4U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant=(uint32_t)form;
    return true;
}
static bool x86_linux_atomic_rmw_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicX86AtomicRmw form=src?
        minic_x86_64_atomic_rmw_form(src->template_text,src->template_length):
        MINIC_X86_ATOMIC_RMW_NONE;
    const MinicX86AtomicRmwDesc *d=
        minic_x86_64_atomic_rmw_description(form);
    const MinicExpression *mem,*value=NULL,*dependency=NULL;
    MinicType mt,vt,dependency_type;
    size_t width;
    bool legacy,unary;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       src->output_count!=1U||!src->outputs||src->label_count||
       (src->has_memory_clobber ? src->clobber_count!=1U :
        (src->clobber_count!=0U || d->locked))||
       src->register_clobber_count)return false;
    unary=d->unary;
    legacy=target_constraint_is(&src->outputs[0],"=m");
    if(legacy &&
       (!d->locked||d->width!=8U||
        (!unary && strcmp(d->opcode,"addq")!=0 &&
                   strcmp(d->opcode,"subq")!=0)))return false;
    if(!legacy && !target_constraint_is(&src->outputs[0],"+m"))
        return false;
    if(src->input_count!=(unary?0U:1U)+(legacy?1U:0U) ||
       (src->input_count&&!src->inputs))return false;
    if(!unary){
        if(!(target_constraint_is(&src->inputs[0],"ir")||
             target_constraint_is(&src->inputs[0],"er")||
             target_constraint_is(&src->inputs[0],"r")))return false;
        value=minic_c0_program_expression(program,src->inputs[0].expression);
    }
    if(legacy){
        const MinicInlineAsmOperand *op=&src->inputs[unary?0U:1U];
        if(!target_constraint_is(op,"m"))return false;
        dependency=minic_c0_program_expression(program,op->expression);
        if(!dependency||dependency->value_category!=MINIC_VALUE_LVALUE||
           !minic_type_unqualified(dependency->type,&dependency_type))
            return false;
    }
    mem=minic_c0_program_expression(program,src->outputs[0].expression);
    if(!mem||mem->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(mem->type)||
       !minic_type_unqualified(mem->type,&mt)||
       mt.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(mt)||mt.pointer_depth)return false;
    width=mt.integer_rank==MINIC_INTEGER_RANK_INT?4U:
          (mt.integer_rank==MINIC_INTEGER_RANK_LONG ||
           mt.integer_rank==MINIC_INTEGER_RANK_LONG_LONG?8U:0U);
    if(width!=d->width)return false;
    /* Original x86 Linux local.h: local_add(long i) uses LONG input into
     * atomic_long_t=atomic64_t with LONG_LONG counter. Distinct C ranks
     * are the same 64-bit x86 hardware register/memory operand, and the
     * machine emitter already checks exact physical width. */
    if(value &&
       (!minic_type_unqualified(value->type,&vt)||
        vt.base_kind!=MINIC_TYPE_BASE_INT||
        !minic_type_is_integer(vt)||vt.pointer_depth||
        (d->width==8U
           ? (vt.integer_rank!=MINIC_INTEGER_RANK_LONG &&
              vt.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)
           : vt.integer_rank!=MINIC_INTEGER_RANK_INT)))return false;
    if(legacy &&
       (dependency_type.base_kind!=MINIC_TYPE_BASE_INT||
        !minic_type_is_integer(dependency_type)||
        dependency_type.pointer_depth||
        (d->width==8U
           ? (dependency_type.integer_rank!=MINIC_INTEGER_RANK_LONG &&
              dependency_type.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)
           : dependency_type.integer_rank!=MINIC_INTEGER_RANK_INT)))
        return false;
    contract->operand_count=1U+(unary?0U:1U)+(legacy?1U:0U);
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    if(!unary)contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if(legacy)contract->kinds[unary?1U:2U]=
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->target_machine_form_variant=(uint32_t)form;
    return true;
}
/* For x86-64 XADD the GNU +r/+m operands are sized by the instruction,
 * not by equal C type ranks. In particular `long` and `long long` both
 * represent a valid 64-bit register or memory operand, independently.
 * Linux local_t wraps atomic_long_t whose counter can use the other rank.
 * Keep one source-side size contract for SMP, local and GS/percpu XADD;
 * the target Core emitter independently rechecks signedness and byte width. */
static bool x86_linux_xadd_integer_width(MinicType type, size_t width) {
    if (!minic_type_is_integer(type) ||
        type.base_kind != MINIC_TYPE_BASE_INT || type.pointer_depth)
        return false;
    switch (width) {
    case 1U: return type.integer_rank == MINIC_INTEGER_RANK_CHAR;
    case 2U: return type.integer_rank == MINIC_INTEGER_RANK_SHORT;
    case 4U: return type.integer_rank == MINIC_INTEGER_RANK_INT;
    case 8U: return type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                   type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG;
    default: return false;
    }
}

static bool x86_linux_smp_xadd_source(const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract,
    MinicX86GsAtomicPlan plan) {
    const size_t width=plan.kind==MINIC_X86_GS_ATOMIC_XADD_SMP?plan.width:0U;
    const MinicExpression *reg,*mem;
    MinicType rt,mt;
    if(!program||!src||!contract||!width||!src->is_volatile||src->is_goto||
        !src->outputs||src->output_count!=2U||src->input_count||
        src->label_count||!src->has_memory_clobber||src->clobber_count!=2U||
        src->register_clobber_count!=1U||!src->register_clobbers||
        !clobber_is(&src->register_clobbers[0],"cc")||
        !c_is(&src->outputs[0],width==1U?"+q":"+r")||
        !c_is(&src->outputs[1],"+m"))return false;
    reg=minic_c0_program_expression(program,src->outputs[0].expression);
    mem=minic_c0_program_expression(program,src->outputs[1].expression);
    if(!reg||!mem||reg->value_category!=MINIC_VALUE_LVALUE||
        mem->value_category!=MINIC_VALUE_LVALUE||
        minic_type_is_const(reg->type)||minic_type_is_const(mem->type)||
        !minic_type_unqualified(reg->type,&rt)||
        !minic_type_unqualified(mem->type,&mt)||
        !x86_linux_xadd_integer_width(rt,width) ||
        !x86_linux_xadd_integer_width(mt,width))return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}
static bool x86_linux_local_xadd_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract, MinicX86GsAtomicPlan plan) {
    const MinicExpression *reg, *mem;
    MinicType reg_type, mem_type;
    const size_t width = plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL ?
        plan.width : 0U;
    if (!program || !src || !contract || !width ||
        !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U ||
        src->input_count != 0U || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, src->outputs[0].expression);
    mem = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!reg || !mem ||
        reg->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(reg->type) || minic_type_is_const(mem->type) ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(mem->type, &mem_type) ||
        !x86_linux_xadd_integer_width(reg_type,width) ||
        !x86_linux_xadd_integer_width(mem_type,width))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}

static bool x86_linux_percpu_xadd_source(
    const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract, MinicX86GsAtomicPlan plan) {
    const MinicExpression *reg, *memory;
    MinicType reg_type, mem_type;
    size_t memory_bytes, memory_alignment;
    const size_t width = plan.kind == MINIC_X86_GS_ATOMIC_XADD_PERCPU ?
        plan.width : 0U;
    if (!program || !source || !contract || width == 0U ||
        source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count || source->label_count ||
        source->register_clobber_count ||
        source->clobber_count != (source->has_memory_clobber ? 1U : 0U) ||
        !(target_constraint_is(&source->outputs[0], "+r") ||
          (width == 1U && target_constraint_is(&source->outputs[0], "+q"))) ||
        !target_constraint_is(&source->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, source->outputs[0].expression);
    memory = minic_c0_program_expression(program, source->outputs[1].expression);
    if (!reg || !memory ||
        reg->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(reg->type) || minic_type_is_const(memory->type) ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(memory->type, &mem_type) ||
        !x86_linux_xadd_integer_width(reg_type,width) ||
        !(x86_linux_xadd_integer_width(mem_type,width) ||
          (minic_type_is_enum(mem_type) &&
           minic_data_layout_type(invocation, program, mem_type,
                                  &memory_bytes, &memory_alignment) &&
           memory_bytes == width)))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_gs_cmpxchg_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract, MinicX86GsAtomicPlan plan) {
    const size_t width = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG ||
        plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY ? plan.width : 0U;
    size_t i;
    const bool loop = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    if (!program || !source || !contract || !width || source->is_goto ||
        source->output_count != 2U || source->input_count != 1U ||
        !source->outputs || !source->inputs || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], loop ? "=&a" : "+a") ||
        !target_constraint_is(&source->outputs[1], "+m") ||
        !target_constraint_is(&source->inputs[0], width == 1U ? "q" : "r"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *operand = i < 2U
            ? &source->outputs[i] : &source->inputs[0];
        const MinicExpression *expression = minic_c0_program_expression(
            program, operand->expression);
        MinicType unqualified;
        unsigned int rank;
        if (!expression || (i < 2U &&
            (expression->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(expression->type))) ||
            !minic_type_unqualified(expression->type, &unqualified) ||
            !(minic_type_is_integer(unqualified) ||
              (i == 1U && width == 8U && minic_type_is_pointer(unqualified))) ||
            (width != 8U && unqualified.base_kind != MINIC_TYPE_BASE_INT))
            return false;
        if (width == 8U && i == 1U && minic_type_is_pointer(unqualified))
            continue;
        rank = width == 1U ? MINIC_INTEGER_RANK_CHAR :
               width == 2U ? MINIC_INTEGER_RANK_SHORT :
               width == 4U ? MINIC_INTEGER_RANK_INT :
               MINIC_INTEGER_RANK_LONG;
        if (unqualified.integer_rank != rank &&
            !(width == 8U && unqualified.integer_rank == MINIC_INTEGER_RANK_LONG_LONG))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = loop
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}
/* Dispatch once to the target-owned machine decision. Each variant still
 * verifies its original GNU operand ABI; no generic Core layer needs to
 * understand SMP locks, GS addressing or the CMPXCHG accumulator. */
static bool x86_linux_gs_atomic_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86GsAtomicPlan plan = source ? minic_x86_64_gs_atomic_plan(
        source->template_text, source->template_length) :
        (MinicX86GsAtomicPlan){0};
    switch (plan.kind) {
    case MINIC_X86_GS_ATOMIC_XADD_SMP:
        return x86_linux_smp_xadd_source(program, source, contract, plan);
    case MINIC_X86_GS_ATOMIC_XADD_LOCAL:
        return x86_linux_local_xadd_source(program, source, contract, plan);
    case MINIC_X86_GS_ATOMIC_XADD_PERCPU:
        return x86_linux_percpu_xadd_source(invocation, program, source,
                                            contract, plan);
    case MINIC_X86_GS_ATOMIC_CMPXCHG:
    case MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY:
        return x86_linux_gs_cmpxchg_source(program, source, contract, plan);
    case MINIC_X86_GS_ATOMIC_NONE:
        return false;
    }
    return false;
}

static bool x86_linux_rep_stos_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const size_t width = source ? minic_x86_64_linux_rep_stos_width(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *output_count, *output_dest, *value, *dest, *count;
    MinicType type, pointee;
    if (!program || !source || !contract || !width ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || source->input_count != 3U ||
        !source->outputs || !source->inputs || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=&c") ||
        !target_constraint_is(&source->outputs[1], "=&D") ||
        !target_constraint_is(&source->inputs[0], "a") ||
        !target_constraint_is(&source->inputs[1], "1") ||
        !target_constraint_is(&source->inputs[2], "0")) return false;
    output_count = minic_c0_program_expression(program, source->outputs[0].expression);
    output_dest = minic_c0_program_expression(program, source->outputs[1].expression);
    value = minic_c0_program_expression(program, source->inputs[0].expression);
    dest = minic_c0_program_expression(program, source->inputs[1].expression);
    count = minic_c0_program_expression(program, source->inputs[2].expression);
    if (!output_count || !output_dest || !value || !dest || !count ||
        output_count->value_category != MINIC_VALUE_LVALUE ||
        output_dest->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output_count->type) ||
        minic_type_is_const(output_dest->type) ||
        !minic_type_unqualified(value->type, &type) ||
        !minic_type_is_integer(type) ||
        (width == 2U && type.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
        (width == 4U && type.integer_rank != MINIC_INTEGER_RANK_INT) ||
        (width == 8U && type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                         type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG) ||
        !minic_type_pointee(dest->type, &pointee) ||
        !minic_type_is_integer(pointee) ||
        (width == 2U && pointee.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
        (width == 4U && pointee.integer_rank != MINIC_INTEGER_RANK_INT) ||
        (width == 8U && pointee.integer_rank != MINIC_INTEGER_RANK_LONG &&
                         pointee.integer_rank != MINIC_INTEGER_RANK_LONG_LONG) ||
        !minic_type_is_integer(count->type) ||
        !minic_type_is_integer(output_count->type) ||
        !minic_type_is_integer(output_dest->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_lowlevel_scalar_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_lowlevel_scalar_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *operand;
    MinicType plain;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != (source->has_memory_clobber ? 1U : 0U))
        return false;
    if (form == 1U) {
        if (source->output_count || source->input_count) return false;
        contract->operand_count = 0U;
        contract->implicit_memory_effect = source->has_memory_clobber;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 2U || form == 4U) {
        if (!source->is_volatile || source->output_count ||
            source->input_count != 1U || !source->inputs ||
            !target_constraint_is(&source->inputs[0], form == 2U ? "q" : "rm"))
            return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 3U) {
        if (source->output_count != 1U || !source->outputs ||
            source->input_count ||
            !target_constraint_is(&source->outputs[0], "=m")) return false;
        operand = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(operand->type) ||
            !minic_type_unqualified(operand->type, &plain) ||
            plain.integer_rank != MINIC_INTEGER_RANK_SHORT ||
            !minic_type_is_integer(plain)) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 5U) {
        if (!source->is_volatile || !source->has_memory_clobber ||
            source->output_count || source->input_count != 2U ||
            !source->inputs ||
            !target_constraint_is(&source->inputs[0], "m") ||
            !target_constraint_is(&source->inputs[1], "r")) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(operand->type)) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[1].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 6U) {
        if (!source->is_volatile || !source->has_memory_clobber ||
            source->output_count || source->input_count != 2U ||
            !source->inputs ||
            !target_constraint_is(&source->inputs[0], "m") ||
            !target_constraint_is(&source->inputs[1], "Ir")) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(operand->type)) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[1].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        /* GNU input-only "m" is an input location, even though the
         * selected BTC hardware operation mutates memory. Keep the GNU
         * binding role separate from the target Machine write effect. */
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 7U || form == 8U) {
        if (source->output_count != 1U || source->input_count != 1U ||
            !source->outputs || !source->inputs ||
            !target_constraint_is(&source->outputs[0], "=m") ||
            !target_constraint_is(&source->inputs[0], "r")) return false;
        operand = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(operand->type) ||
            !minic_type_unqualified(operand->type, &plain) ||
            !minic_type_is_integer(plain) ||
            (plain.integer_rank != (form == 7U ? MINIC_INTEGER_RANK_INT
                                                  : MINIC_INTEGER_RANK_LONG) &&
             !(form == 8U && plain.integer_rank == MINIC_INTEGER_RANK_LONG_LONG)))
            return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    return false;
}

static bool x86_linux_delay_wait_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const tpause_constraints[] = {"c", "d", "a"};
    const unsigned int form = source ? minic_x86_64_linux_delay_wait_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count || source->has_memory_clobber ||
        source->clobber_count || source->register_clobber_count ||
        source->output_count != (form == 1U ? 1U : 0U) ||
        source->input_count != (form == 1U ? 0U : 3U) ||
        (form == 1U ? !source->outputs : !source->inputs)) return false;
    if (form == 1U) {
        if (!target_constraint_is(&source->outputs[0], "+a")) return false;
        expr = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expr->type, minic_type_unsigned_long()))
            return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
        contract->target_machine_form_variant = form;
        return true;
    }
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->inputs[i], tpause_constraints[i]))
            return false;
        expr = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expr || !minic_type_equal(expr->type,
                                       minic_type_unsigned_int())) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 3U;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_identity_load_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *expr;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_identity_load_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count || source->input_count != 1U ||
        !source->inputs || !target_constraint_is(&source->inputs[0], "g") ||
        source->clobber_count != 2U || !source->has_memory_clobber ||
        source->register_clobber_count != 1U ||
        !clobber_is(&source->register_clobbers[0], "r9")) return false;
    expr = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(expr->type, minic_type_unsigned_long())) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_ipv6_checksum_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const constraints[] = {"=r", "[sum]", "r", "r"};
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_ipv6_checksum_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->output_count != 1U ||
        source->input_count != 3U || !source->outputs || !source->inputs)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *operand = i == 0U
            ? &source->outputs[0] : &source->inputs[i - 1U];
        if (!target_constraint_is(operand, constraints[i])) return false;
        expr = minic_c0_program_expression(program, operand->expression);
        if (!expr || (i >= 2U ? !minic_type_is_pointer(expr->type)
                             : !minic_type_is_integer(expr->type)) ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type)))) return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_csum_partial_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_csum_partial_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expression;
    const MinicArrayType *array;
    MinicType type;
    size_t i;
    const size_t count = form == 1U || form == 6U ? 2U : form == 2U ? 6U : 3U;
    if (program == NULL || source == NULL || contract == NULL || form == 0U ||
        source->is_volatile || source->is_goto || source->output_count != 1U ||
        source->input_count != count - 1U || !source->outputs || !source->inputs ||
        source->label_count || source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "+r")) return false;
    expression = minic_c0_program_expression(program, source->outputs[0].expression);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expression->type) ||
        !minic_type_unqualified(expression->type, &type) ||
        !minic_type_is_integer(type) ||
        (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)) return false;
    contract->operand_count = count;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    for (i = 1U; i < count; ++i) {
        const MinicInlineAsmOperand *operand = &source->inputs[i - 1U];
        const char *constraint = form == 1U || form == 6U ? "r" :
            form == 2U ? "m" : i == 1U ? "r" : "m";
        if (!target_constraint_is(operand, constraint)) return false;
        expression = minic_c0_program_expression(program, operand->expression);
        if (expression == NULL || !minic_type_unqualified(expression->type, &type))
            return false;
        if (form == 1U || form == 6U || (form >= 3U && i == 1U)) {
            if ((form == 1U || form == 6U) ? !minic_type_is_integer(type) :
                !minic_type_is_pointer(type)) return false;
            contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        } else {
            if (form == 2U) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_is_integer(type) ||
                    (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                     type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)) return false;
            } else {
                array = minic_type_is_array(type) ?
                    minic_c0_program_array_type(program, type.array_type_id) : NULL;
                if (array != NULL) {
                    MinicType element;
                    if (expression->value_category != MINIC_VALUE_LVALUE ||
                        array->element_count !=
                            (form == 3U ? 32U : form == 4U ? 16U : 8U) ||
                        !minic_type_unqualified(array->element_type, &element) ||
                        !minic_type_is_char_integer(element)) return false;
                } else {
                    MinicType element;
                    /* Array-qualified GNU m operands may be stored in the
                     * AST as ADDRESS_OF of the lvalue array's first char.
                     * Core lower_address owns that canonical conversion. */
                    if (expression->kind != MINIC_EXPRESSION_ADDRESS_OF ||
                        !minic_type_is_pointer(type) ||
                        !minic_type_pointee(type, &element) ||
                        !minic_type_unqualified(element, &element) ||
                        !minic_type_is_char_integer(element)) return false;
                }
            }
            /* GNU char-array `m` operands are frontend-canonicalized into
             * ADDRESS_OF RVALUE pointers.  They name an alias region, not a
             * C scalar load; transport the address and mark an implicit
             * memory effect.  Actual qword reads use GNU's named `src`. */
            contract->kinds[i] = form == 2U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        }
    }
    contract->implicit_memory_effect = form != 1U && form != 6U;
    return true;
}

static bool x86_linux_delay_mul_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const output_constraints[] = {"=d", "=&a"};
    static const char *const input_constraints[] = {"1", "0"};
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_delay_mul_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->output_count != 2U ||
        source->input_count != 2U || !source->outputs || !source->inputs)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i < 2U
            ? &source->outputs[i] : &source->inputs[i - 2U];
        if (!target_constraint_is(op, i < 2U
            ? output_constraints[i] : input_constraints[i - 2U])) return false;
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || !minic_type_is_integer(expr->type) ||
            (i < 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))) ||
            !minic_type_equal(expr->type, i == 1U
                ? minic_type_int() : minic_type_unsigned_long()))
            return false;
        contract->kinds[i] = i < 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_boot_flags_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_boot_flags_form(
        source->template_text, source->template_length) : 0U;
    static const char *const cpuid_outputs[] = {"=a", "=b", "=c", "=d"};
    size_t i, count;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->label_count) return false;
    if (form == 1U || form == 2U) {
        const MinicExpression *memory, *index;
        if (source->output_count != 1U || !source->outputs ||
            source->input_count != (form == 1U ? 2U : 1U) || !source->inputs ||
            !target_constraint_is(&source->outputs[0],
                form == 1U ? "=@ccc" : "+m") ||
            (form == 1U && !target_constraint_is(&source->inputs[0], "m")) ||
            !target_constraint_is(&source->inputs[form == 1U ? 1U : 0U], "Ir"))
            return false;
        memory = minic_c0_program_expression(program,
            form == 1U ? source->inputs[0].expression :
                         source->outputs[0].expression);
        index = minic_c0_program_expression(program,
            source->inputs[form == 1U ? 1U : 0U].expression);
        if (!memory || memory->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(memory->type) ||
            !index || !minic_type_is_integer(index->type)) return false;
        if (form == 1U) {
            const MinicExpression *flag = minic_c0_program_expression(
                program, source->outputs[0].expression);
            if (!flag || flag->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_equal(flag->type, minic_type_bool())) return false;
        }
        contract->operand_count = form == 1U ? 3U : 2U;
        contract->kinds[0] = form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1] = form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (form == 1U)
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        contract->target_machine_form_variant = form;
        return true;
    }
    if (form == 3U) {
        if (!source->is_volatile || source->output_count != 4U ||
            source->input_count != 2U || !source->outputs || !source->inputs ||
            !target_constraint_is(&source->inputs[0], "0") ||
            !target_constraint_is(&source->inputs[1], "2")) return false;
        for (i = 0U; i < 4U; ++i)
            if (!target_constraint_is(&source->outputs[i],cpuid_outputs[i]))
                return false;
        count = 6U;
    } else {
        if (!source->is_volatile || source->output_count != 2U ||
            source->input_count != 1U || !source->outputs || !source->inputs ||
            !target_constraint_is(&source->outputs[0], "=&r") ||
            !target_constraint_is(&source->outputs[1], "=&r") ||
            !target_constraint_is(&source->inputs[0], "ri")) return false;
        count = 3U;
    }
    for (i = 0U; i < count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *operand = output
            ? &source->outputs[i] : &source->inputs[i-source->output_count];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (!e || !minic_type_is_integer(e->type) ||
            (output && (e->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(e->type)))) return false;
        contract->kinds[i] = output
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = count;
    contract->implicit_memory_effect = form == 4U;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_lock_bitcc_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_lock_bitcc_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *mem, *flag, *input;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count || source->output_count != 2U ||
        source->input_count != 1U || !source->outputs || !source->inputs ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count != 0U ||
        !target_constraint_is(&source->outputs[form == 1U ? 0U : 1U], "+m") ||
        !target_constraint_is(&source->outputs[form == 1U ? 1U : 0U],
                              form == 1U ? "=@ccc" : "=@ccs") ||
        !target_constraint_is(&source->inputs[0],
                              form == 1U ? "I" : "ir")) return false;
    mem = minic_c0_program_expression(program,
        source->outputs[form == 1U ? 0U : 1U].expression);
    flag = minic_c0_program_expression(program,
        source->outputs[form == 1U ? 1U : 0U].expression);
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!mem || !flag || !input ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(mem->type) ||
        !minic_type_equal(flag->type, minic_type_bool()) ||
        !minic_type_is_integer(input->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_boot_string_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const cmps_out[] = {
        "=@ccnz", "+D", "+S", "+c"
    };
    static const char *const div_out[] = {"=a", "=d"};
    static const char *const div_in[] = {"rm", "0", "1"};
    static const char *const movs_out[] = {"=&c", "=&D", "=&S"};
    static const char *const movs_in[] = {"0", "g", "1", "2"};
    const unsigned int form = source ? minic_x86_64_linux_boot_string_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expr;
    const size_t outputs = form == 1U ? 4U : form == 2U ? 2U : 3U;
    const size_t inputs = form == 1U ? 0U : form == 2U ? 3U : 4U;
    size_t i;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->label_count || source->register_clobber_count ||
        source->output_count != outputs || source->input_count != inputs ||
        !source->outputs || (inputs && !source->inputs) ||
        source->clobber_count != (form == 3U ? 1U : 0U) ||
        source->has_memory_clobber != (form == 3U) ||
        source->is_volatile != (form == 3U)) return false;
    for (i = 0U; i < outputs + inputs; ++i) {
        const bool output = i < outputs;
        const size_t j = output ? i : i-outputs;
        const MinicInlineAsmOperand *operand = output
            ? &source->outputs[j] : &source->inputs[j];
        const char *constraint = form == 1U ? cmps_out[j] :
            form == 2U ? (output ? div_out[j] : div_in[j]) :
                         (output ? movs_out[j] : movs_in[j]);
        if (!target_constraint_is(operand, constraint)) return false;
        expr = minic_c0_program_expression(program, operand->expression);
        if (!expr || (output &&
            (expr->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(expr->type)))) return false;
        if (form == 1U) {
            if (!(i == 0U ? minic_type_equal(expr->type, minic_type_bool()) :
                  i == 3U ? minic_type_equal(expr->type,
                                              minic_type_unsigned_long()) :
                            minic_type_is_pointer(expr->type))) return false;
        } else if (form == 2U) {
            if (!minic_type_equal(expr->type,
                                  minic_type_unsigned_int())) return false;
        } else if (output) {
            if (!minic_type_equal(expr->type, minic_type_long())) return false;
        } else if (j < 2U ?
            !minic_type_equal(expr->type, minic_type_unsigned_long()) :
            !minic_type_is_pointer(expr->type)) return false;
        contract->kinds[i] = form == 1U ?
            (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) :
            output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = outputs + inputs;
    contract->implicit_memory_effect = form != 2U;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_vdso_syscall_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const bool clock_form = source && source->output_count == 2U;
    const MinicExpression *ret, *outmem, *number, *arg1, *arg2;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_vdso_syscall_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (clock_form ? 2U : 1U) ||
        source->input_count != 3U || !source->outputs || !source->inputs ||
        source->clobber_count != (clock_form ? 2U : 1U) ||
        source->has_memory_clobber == clock_form ||
        source->register_clobber_count != (clock_form ? 2U : 0U) ||
        (clock_form &&
         (!clobber_is(&source->register_clobbers[0], "rcx") ||
          !clobber_is(&source->register_clobbers[1], "r11"))) ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        (clock_form && !target_constraint_is(&source->outputs[1], "=m")) ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "D") ||
        !target_constraint_is(&source->inputs[2], "S")) return false;
    ret = minic_c0_program_expression(program,source->outputs[0].expression);
    outmem = clock_form ? minic_c0_program_expression(program,
        source->outputs[1].expression) : NULL;
    number = minic_c0_program_expression(program,source->inputs[0].expression);
    arg1 = minic_c0_program_expression(program,source->inputs[1].expression);
    arg2 = minic_c0_program_expression(program,source->inputs[2].expression);
    if (!ret || ret->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(ret->type,minic_type_long()) ||
        (clock_form && (!outmem ||
            outmem->value_category != MINIC_VALUE_LVALUE)) ||
        !number || !minic_type_is_integer(number->type) ||
        !arg1 || !(clock_form ? minic_type_is_integer(arg1->type) :
                                minic_type_is_pointer(arg1->type)) ||
        !arg2 || !minic_type_is_pointer(arg2->type)) return false;
    contract->operand_count = clock_form ? 5U : 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    if (clock_form)
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[clock_form ? 2U : 1U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[clock_form ? 3U : 2U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[clock_form ? 4U : 3U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_vdso_cpunode_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *p,*zero,*selector;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_vdso_cpunode_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count!=1U || source->input_count!=2U ||
        !source->outputs || !source->inputs ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0],"=a") ||
        !target_constraint_is(&source->inputs[0],"i") ||
        !target_constraint_is(&source->inputs[1],"r")) return false;
    p=minic_c0_program_expression(program,source->outputs[0].expression);
    zero=minic_c0_program_expression(program,source->inputs[0].expression);
    selector=minic_c0_program_expression(program,source->inputs[1].expression);
    if (!p || p->value_category!=MINIC_VALUE_LVALUE ||
        !minic_type_equal(p->type,minic_type_unsigned_int()) ||
        !zero || !minic_type_is_integer(zero->type) ||
        !selector || !minic_type_is_integer(selector->type)) return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect=true;
    contract->target_machine_form_variant=1U;
    return true;
}

static bool x86_linux_atomic_cc_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *mem, *flag, *value;
    MinicType mt, ft, vt;
    const MinicX86AtomicCC form = src ?
        minic_x86_64_atomic_cc_form(src->template_text, src->template_length) :
        MINIC_X86_ATOMIC_CC_NONE;
    const MinicX86AtomicCCDesc *d =
        minic_x86_64_atomic_cc_description(form);
    if (!program || !src || !contract || !d || !src->is_volatile || src->is_goto ||
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
            (!minic_type_equal(vt, mt) &&
             !(d->width == 8U &&
               (minic_type_equal(vt, minic_type_long()) ||
                minic_type_equal(vt, minic_type_long_long())) &&
               (minic_type_equal(mt, minic_type_long()) ||
                minic_type_equal(mt, minic_type_long_long()))))) return false;
    }
    contract->operand_count = d->has_value ? 3U : 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    if (d->has_value)
        contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant = (uint32_t)form |
        (minic_x86_64_atomic_cc_locked_form(src->template_text,
                                            src->template_length)
            ? MINIC_X86_ATOMIC_CC_LOCK_VARIANT : 0U);
    return true;
}
static bool x86_linux_bitops_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicX86BitopsForm form=src ?
        minic_x86_64_linux_bitops_form(
            src->template_text,src->template_length):MINIC_X86_BITOPS_NONE;
    const MinicX86BitopsDesc *desc=minic_x86_64_bitops_description(form);
    const bool byte_rmw = form==MINIC_X86_BITOPS_LOCK_OR_BYTE ||
                          form==MINIC_X86_BITOPS_LOCK_AND_BYTE ||
                          form==MINIC_X86_BITOPS_LOCK_XOR_BYTE;
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
        /* The source constraint is input-only "m", even when the selected
         * hardware bit operation mutates the pointed word. Memory writes
         * remain target Machine effects, not a fabricated C read/write role. */
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    } else {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->target_machine_form_variant=(uint32_t)form;
    return true;
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
/* Linux 6.6 lib/zstd/common/cpu.h uses three nonvolatile CPUID layouts,
 * rather than the four-output kernel native_cpuid convention. In particular,
 * its EAX/ECX *inputs* are ordinary 32-bit integers (not tied "0"/"2"
 * operands). The omitted registers are declared clobbered in the source;
 * the machine implementation still saves the ABI-preserved RBX. */
static bool x86_zstd_cpuid_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const form_out[][3] = {
        {"=a", NULL, NULL}, {"=a", "=c", "=d"}, {"=a", "=b", "=c"}
    };
    static const char *const form_in[][2] = {
        {"a", NULL}, {"a", NULL}, {"a", "c"}
    };
    static const char *const form_clobbers[][3] = {
        {"ebx", "ecx", "edx"}, {"ebx", NULL, NULL},
        {"edx", NULL, NULL}
    };
    static const size_t output_counts[] = {1U, 3U, 3U};
    static const size_t input_counts[] = {1U, 1U, 2U};
    static const size_t clobber_counts[] = {3U, 1U, 1U};
    size_t form, i;
    if (!program || !source || !contract || source->is_volatile ||
        source->is_goto || source->label_count || source->has_memory_clobber ||
        source->template_length != 5U || !source->template_text ||
        memcmp(source->template_text, "cpuid", 5U) != 0 ||
        !source->outputs || !source->inputs ||
        !source->register_clobbers)
        return false;
    for (form = 0U; form < 3U; ++form) {
        if (source->output_count != output_counts[form] ||
            source->input_count != input_counts[form] ||
            source->clobber_count != clobber_counts[form] ||
            source->register_clobber_count != clobber_counts[form])
            continue;
        for (i = 0U; i < clobber_counts[form]; ++i)
            if (!clobber_is(&source->register_clobbers[i],
                            form_clobbers[form][i]))
                break;
        if (i != clobber_counts[form]) continue;
        for (i = 0U; i < output_counts[form]; ++i) {
            const MinicExpression *expr = minic_c0_program_expression(
                program, source->outputs[i].expression);
            MinicType plain;
            if (!c_is(&source->outputs[i], form_out[form][i]) || !expr ||
                expr->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expr->type) ||
                !minic_type_unqualified(expr->type, &plain) ||
                (!minic_type_equal(plain, minic_type_unsigned_int()) &&
                 !minic_type_equal(plain, minic_type_int())))
                return false;
            contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        }
        for (i = 0U; i < input_counts[form]; ++i) {
            const MinicExpression *expr = minic_c0_program_expression(
                program, source->inputs[i].expression);
            MinicType plain;
            if (!c_is(&source->inputs[i], form_in[form][i]) || !expr ||
                !minic_type_unqualified(expr->type, &plain) ||
                (!minic_type_equal(plain, minic_type_unsigned_int()) &&
                 !minic_type_equal(plain, minic_type_int())))
                return false;
            contract->kinds[output_counts[form]+i] =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        }
        contract->operand_count = output_counts[form] + input_counts[form];
        return true;
    }
    return false;
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc,
                      MinicTargetInlineAsmContract *contract) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    bool generic = false;
    bool cpuid = false;
    bool bit_scan = false;
    bool is_muldiv = false;
    MinicX86BitScanForm bit_form = MINIC_X86_BIT_SCAN_NONE;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc || !contract) return false;
    *is_mulacc = false;
    /* Original Linux native_cpuid has six operands: four register outputs
     * and only two tied inputs (EAX/ECX), plus a memory compiler barrier.
     * Preserve the legacy eight-operand CPUID contract in the same target
     * implementation. RBX save/restore is always owned by x86 machine code. */
    if (src->output_count == 4U &&
        (src->input_count == 4U || src->input_count == 2U) &&
        src->register_clobber_count == 0U &&
        src->template_length == 5U &&
        memcmp(src->template_text, "cpuid", 5U) == 0 &&
        (src->input_count == 4U ?
         (!src->has_memory_clobber && src->clobber_count == 0U) :
         (src->is_volatile && src->has_memory_clobber &&
          src->clobber_count == 1U))) {
        cpuid = true;
        n = 4U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], cpuid_out[i])) return false;
        for (i = 0U; i < src->input_count; ++i)
            if (!c_is(&src->inputs[i],
                      cpuid_in[src->input_count == 2U && i == 1U ? 2U : i]))
                return false;
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
        is_muldiv = true;
    } else if (src->output_count == 1U && !src->has_memory_clobber &&
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
                      : (is_muldiv
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
    /* The original family checker admitted these exact source expressions,
     * but left a zero machine FormId.  A zero FormId cannot independently
     * seal native CPUID/ADC/BSR or matched-memory effects.  Publish the
     * complete family at Source, keeping semantic operand selection in the
     * target MachineIR producer instead of creating a transitional escape. */
    if (cpuid) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_CPUID_MATCHED;
        contract->target_machine_form_variant =
            (uint32_t)(src->output_count + src->input_count);
    } else if (*is_mulacc) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_MULACC_MATCHED;
        contract->target_machine_form_variant = 1U;
    } else if (is_muldiv) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_MULDIV_U64;
    } else if (bit_scan) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED;
        contract->target_machine_form_variant = (uint32_t)bit_form;
    } else if (generic) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_MATCHED_MEMORY;
        contract->target_machine_form_variant =
            (uint32_t)(src->output_count + src->input_count);
    } else return false;
    return true;
}

static bool x86_linux_monitor_wait_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const regs_acd[] = {"a", "c", "d"};
    static const char *const regs_abc[] = {"a", "b", "c"};
    const unsigned int form = source ? minic_x86_64_linux_monitor_wait_form(
        source->template_text, source->template_length) : 0U;
    const size_t count = form == 1U || form == 2U || form == 4U ? 3U : 2U;
    const char *const *regs = form == 4U ? regs_abc : regs_acd;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->output_count ||
        source->input_count != count || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicInlineAsmOperand *op = &source->inputs[i];
        const MinicExpression *value = minic_c0_program_expression(
            program, op->expression);
        MinicType plain;
        if (!c_is(op, regs[i]) || !value ||
            !minic_type_unqualified(value->type, &plain) ||
            (!minic_type_is_pointer(plain) &&
             !minic_type_is_integer(plain)))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = count;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_segment_read_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *dst;
    MinicType plain;
    const unsigned int form = source ? minic_x86_64_linux_segment_read_form(
        source->template_text, source->template_length) : 0U;
    const bool short_selector = form <= 2U || form >= 7U;
    if (!program || !source || !contract || source->is_volatile ||
        source->is_goto || !form ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != (short_selector ? 1U : 0U) ||
        source->has_memory_clobber != short_selector ||
        source->register_clobber_count ||
        !c_is(&source->outputs[0], "=r"))
        return false;
    dst = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!dst || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !minic_type_unqualified(dst->type, &plain) ||
        !(minic_type_equal(plain, short_selector ?
              minic_type_unsigned_short() : minic_type_unsigned_int()) ||
          /* The kernel's i387 regset reader uses bare `mov %%ds,%0`
           * with an unsigned 32-bit fsave->fos lvalue. GAS accepts the
           * 32-bit GPR destination and zero-extends the selector; the C
           * storage width, not the bare assembly mnemonic, owns the store. */
          (short_selector && minic_type_equal(
              plain, minic_type_unsigned_int()))))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_segment_load_extable_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *value;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_segment_load_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->output_count ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count != 1U ||
        !source->has_memory_clobber || source->register_clobber_count ||
        !c_is(&source->inputs[0], "rm"))
        return false;
    value = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_lkgs_extable_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *selector;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_lkgs_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !c_is(&source->outputs[0], "+D"))
        return false;
    selector = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(selector->type) ||
        !minic_type_unqualified(selector->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_fsgsbase_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_fsgsbase_form(
        source->template_text, source->template_length) : 0U;
    const bool reading = form <= 2U;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *value;
    MinicType type;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        source->output_count != (reading ? 1U : 0U) ||
        source->input_count != (reading ? 0U : 1U) ||
        source->label_count || source->clobber_count != 1U ||
        !source->has_memory_clobber || source->register_clobber_count)
        return false;
    operand = reading ? (source->outputs ? &source->outputs[0] : NULL) :
        (source->inputs ? &source->inputs[0] : NULL);
    if (!operand || !c_is(operand, reading ? "=r" : "r")) return false;
    value = minic_c0_program_expression(program, operand->expression);
    if (!value || (reading && (value->value_category != MINIC_VALUE_LVALUE ||
                              minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = reading ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_kernel_nofault_goto_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ?
        minic_x86_64_linux_kernel_nofault_goto_form(
            source->template_text, source->template_length) : 0U;
    const bool load = form <= 4U;
    const unsigned int bytes = form ? 1U << ((form - 1U) % 4U) : 0U;
    const MinicInlineAsmOperand *data;
    const MinicInlineAsmOperand *memory;
    const MinicExpression *data_expr, *memory_expr;
    MinicType type;
    if (!program || !source || !contract || !form || !source->is_goto ||
        source->output_count != (load ? 1U : 0U) ||
        source->input_count != (load ? 1U : 2U) ||
        source->label_count != 1U || !source->labels ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        (load && (!source->outputs || !source->is_volatile)) ||
        !source->inputs)
        return false;
    data = load ? &source->outputs[0] : &source->inputs[0];
    memory = &source->inputs[load ? 0U : 1U];
    if (!target_constraint_is(data, load ?
            (bytes == 1U ? "=q" : "=r") :
            (bytes == 1U ? "iq" : bytes == 8U ? "er" : "ir")) ||
        !target_constraint_is(memory, "m"))
        return false;
    data_expr = minic_c0_program_expression(program, data->expression);
    memory_expr = minic_c0_program_expression(program, memory->expression);
    if (!data_expr || !memory_expr ||
        (load && (data_expr->value_category != MINIC_VALUE_LVALUE ||
                  minic_type_is_const(data_expr->type))) ||
        memory_expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(memory_expr->type) ||
        !minic_type_unqualified(data_expr->type, &type) ||
        (!minic_type_is_integer(type) &&
         !(bytes == 8U && minic_type_is_pointer(type))) ||
        (minic_type_is_integer(type) &&
         ((bytes == 1U && type.integer_rank != MINIC_INTEGER_RANK_CHAR) ||
          (bytes == 2U && type.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
          (bytes == 4U && type.integer_rank != MINIC_INTEGER_RANK_INT) ||
          (bytes == 8U && type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                          type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = load ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_rep_stosb_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const constraints[] = {"+c", "+D", "+r"};
    const MinicExpression *expr;
    const MinicFixedRegisterBinding *stack;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_rep_stosb_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 3U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "a"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->outputs[i], constraints[i]))
            return false;
        expr = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expr ||
            (i != 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))) ||
            (i == 1U ? !minic_type_is_pointer(expr->type) :
                       !minic_type_equal(expr->type, minic_type_unsigned_long())))
            return false;
    }
    /* Physical +r RSP is a global fixed-register dependency, not an
     * addressable local or a scalar input masquerading as an output. */
    if (expr->kind != MINIC_EXPRESSION_FIXED_REGISTER) return false;
    stack = minic_c0_program_fixed_register_binding(
        program, expr->value.fixed_register_binding_id);
    if (!stack || !stack->register_name ||
        strcmp(stack->register_name, "rsp") != 0)
        return false;
    expr = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!expr || !minic_type_is_integer(expr->type)) return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_fpu_user_frame_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *error, *output, *input, *zero;
    const unsigned int form = source ?
        minic_x86_64_linux_fpu_user_frame_form(
            source->template_text, source->template_length) : 0U;
    MinicType plain;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->outputs[1], "=m") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "m"))
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    output = minic_c0_program_expression(
        program, source->outputs[1].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    input = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || !output || !zero || !input ||
        error->value_category != MINIC_VALUE_LVALUE ||
        output->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_is_record(output->type) ||
        !minic_type_equal(output->type, input->type) ||
        !minic_type_is_integer(zero->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}
/* Previously the two MXCSR memory forms were claimed by Core's generic
 * `m`/`=m` source classifier and only identified here at machine emission.
 * The hardware CSR side effect and 32-bit operand width belong to x86.
 * Preserve both numeric and named GNU operands, including optional stronger
 * compiler memory clobbers admitted by the old generic path. */
static bool x86_linux_mxcsr_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_mxcsr_form(
        source->template_text, source->template_length) : 0U;
    const bool reading_memory = form == 1U;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *expression;
    MinicType plain;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (reading_memory ? 0U : 1U) ||
        source->input_count != (reading_memory ? 1U : 0U) ||
        source->clobber_count != (source->has_memory_clobber ? 1U : 0U) ||
        source->register_clobber_count ||
        (reading_memory ? !source->inputs : !source->outputs))
        return false;
    operand = reading_memory ? &source->inputs[0] : &source->outputs[0];
    if (!target_constraint_is(operand, reading_memory ? "m" : "=m") ||
        operand->access != (reading_memory ?
            MINIC_INLINE_ASM_OPERAND_READ_ONLY :
            MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)) return false;
    expression = minic_c0_program_expression(program, operand->expression);
    if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
        (!reading_memory && minic_type_is_const(expression->type)) ||
        !minic_type_unqualified(expression->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_int())) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = reading_memory ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}
/* ORC's live RIP/RSP/RBP snapshot has three *independent* register outputs.
 * Keep this x86-specific privileged register view out of generic Core ASM
 * admission; no memory operand, clobber or extra instruction is permitted. */
static bool x86_linux_unwind_context_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_unwind_context_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != 3U || !source->outputs ||
        source->input_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count)
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *operand = &source->outputs[i];
        const MinicExpression *expression = minic_c0_program_expression(
            program, operand->expression);
        MinicType plain;
        if (!target_constraint_is(operand, "=r") ||
            operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
            !expression || expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expression->type) ||
            !minic_type_unqualified(expression->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long())) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    contract->operand_count = 3U;
    return true;
}

static bool x86_linux_task_size_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output, *input;
    MinicType type;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_task_size_alt_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 3U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=r")) return false;
    output = minic_c0_program_expression(program, source->outputs[0].expression);
    if (!output || output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long())) return false;
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->inputs[i], "i")) return false;
        input = minic_c0_program_expression(program, source->inputs[i].expression);
        if (!input || !minic_type_is_integer(input->type)) return false;
    }
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 1U; i < 4U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_linux_x87_fild_prepare_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *input;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL ||
        source->label_count != 0U || source->clobber_count != 0U ||
        source->register_clobber_count != 0U || source->has_memory_clobber ||
        !minic_x86_64_linux_x87_fild_prepare_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->inputs[0], "m")) return false;
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!input || input->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(input->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_fpu_kernel_restore_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output, *input;
    const MinicExpression *out_base, *in_base;
    const unsigned int form = source ? minic_x86_64_linux_fpu_kernel_restore_form(
        source->template_text, source->template_length) : 0U;
    if (program == NULL || source == NULL || contract == NULL || form == 0U ||
        !source->is_volatile || source->is_goto || source->output_count != 1U ||
        source->input_count != 1U || source->outputs == NULL ||
        source->inputs == NULL || source->label_count != 0U ||
        source->clobber_count != 0U || source->register_clobber_count != 0U ||
        source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=m") ||
        !target_constraint_is(&source->inputs[0], "m")) return false;
    output = minic_c0_program_expression(program, source->outputs[0].expression);
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (output == NULL || input == NULL ||
        output->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(output->type) ||
        !minic_type_equal(output->type, input->type) ||
        output->kind != MINIC_EXPRESSION_DEREFERENCE ||
        input->kind != MINIC_EXPRESSION_DEREFERENCE) return false;
    /* GCC's `=m` operand and paired `m` input describe one and the same
     * state object. Do not silently emit a read from a different object. */
    out_base = minic_c0_program_expression(program, output->value.unary.operand);
    in_base = minic_c0_program_expression(program, input->value.unary.operand);
    if (out_base == NULL || in_base == NULL ||
        out_base->kind != MINIC_EXPRESSION_LOCAL ||
        in_base->kind != MINIC_EXPRESSION_LOCAL ||
        out_base->value.local_id != in_base->value.local_id) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_fxrstor_safe_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *error, *memory, *zero, *input;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_fxrstor_safe_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->outputs[1], "=m") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "m"))
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(
        program, source->outputs[1].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    input = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || !memory || !zero || !input ||
        error->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_equal(error->type, minic_type_int()) ||
        !minic_type_is_record(memory->type) ||
        !minic_type_equal(memory->type, input->type) ||
        !minic_type_is_integer(zero->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_xstate_fault_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *err, *ptr, *mem, *low, *high;
    MinicType pointee, plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xstate_fault_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 4U || !source->inputs ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    err = minic_c0_program_expression(program, source->outputs[0].expression);
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!err || !ptr || !mem || !low || !high ||
        err->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(err->type) ||
        !minic_type_unqualified(err->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_xrstor_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *ptr, *mem, *low, *high;
    MinicType pointee;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xrstor_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count || source->input_count != 4U ||
        !source->inputs || source->label_count ||
        source->register_clobber_count || source->clobber_count != 1U ||
        !source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!ptr || !mem || !low || !high ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_xsave_triple_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *error, *ptr, *mem, *low, *high;
    MinicType pointee, plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xsave_triple_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 4U || !source->inputs ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    error = minic_c0_program_expression(program, source->outputs[0].expression);
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!error || !ptr || !mem || !low || !high ||
        error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_jump_label_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *key, *flags;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_jump_label_form(
            source->template_text, source->template_length) ||
        !source->is_goto || source->is_volatile ||
        source->output_count || source->input_count != 2U ||
        !source->inputs || source->label_count != 1U || !source->labels ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "i") ||
        !target_constraint_is(&source->inputs[1], "i"))
        return false;
    key = minic_c0_program_expression(program, source->inputs[0].expression);
    flags = minic_c0_program_expression(program, source->inputs[1].expression);
    if (!key || !flags || !minic_type_is_pointer(key->type) ||
        !minic_type_is_integer(flags->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_pkru_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_pkru_form(
        source->template_text, source->template_length) : 0U;
    const bool read = form == 1U;
    static const char *const input_regs[] = {"a", "c", "d"};
    static const char *const output_regs[] = {"=a", "=d"};
    const MinicExpression *expression;
    MinicType value_type;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        source->output_count != (read ? 2U : 0U) ||
        source->input_count != (read ? 1U : 3U) ||
        (read && !source->outputs) || !source->inputs)
        return false;
    for (i = 0U; i < source->output_count; ++i) {
        if (!target_constraint_is(&source->outputs[i], output_regs[i]) ||
            source->outputs[i].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expression->type) ||
            !minic_type_unqualified(expression->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_int()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    for (i = 0U; i < source->input_count; ++i) {
        const size_t index = source->output_count + i;
        const char *expected = read ? "c" : input_regs[i];
        if (!target_constraint_is(&source->inputs[i], expected) ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expression ||
            !minic_type_unqualified(expression->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_int()))
            return false;
        contract->kinds[index] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = source->output_count + source->input_count;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_fdiv_bug_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *expression;
    MinicType type;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_fdiv_bug_form(
            source->template_text, source->template_length) ||
        source->is_goto || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        source->output_count != 1U || source->input_count != 2U ||
        !source->outputs || !source->inputs ||
        !target_constraint_is(&source->outputs[0], "=m") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)
        return false;
    expression = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expression->type) ||
        !minic_type_unqualified(expression->type, &type) ||
        !minic_type_equal(type, minic_type_int()))
        return false;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    for (i = 0U; i < 2U; ++i) {
        if (!target_constraint_is(&source->inputs[i], "m") ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(expression->type, &type) ||
            !minic_type_equal(type, minic_type_double()))
            return false;
        contract->kinds[i + 1U] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    }
    contract->operand_count = 3U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_load_unaligned_zeropad_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *output, *memory;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_load_unaligned_zeropad_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "m"))
        return false;
    output = minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!output || !memory ||
        output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_equal(output->type, minic_type_unsigned_long()) ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(memory->type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_lar_selector_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *rights, *selector;
    MinicType plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_lar_selector_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "rm"))
        return false;
    rights = minic_c0_program_expression(
        program, source->outputs[0].expression);
    selector = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!rights || !selector ||
        rights->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(rights->type) ||
        !minic_type_equal(rights->type, minic_type_unsigned_int()) ||
        !minic_type_unqualified(selector->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_random_seed_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *flag, *output;
    const unsigned int form = source ? minic_x86_64_linux_random_seed_form(
        source->template_text, source->template_length) : 0U;
    MinicType plain;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count || source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=@ccc") ||
        !target_constraint_is(&source->outputs[1], "=r"))
        return false;
    flag = minic_c0_program_expression(
        program, source->outputs[0].expression);
    output = minic_c0_program_expression(
        program, source->outputs[1].expression);
    if (!flag || !output ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) || minic_type_is_const(output->type) ||
        !minic_type_unqualified(flag->type, &plain) ||
        !minic_type_equal(plain, minic_type_bool()) ||
        !minic_type_unqualified(output->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_smap_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *expression;
    const unsigned int form = source ?
        minic_x86_64_linux_smap_alternative_form(
            source->template_text, source->template_length) : 0U;
    const MinicInlineAsmOperand *operand;
    if (!program || !source || !contract || (form != 3U && form != 4U) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 2U ||
        source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "cc") ||
        source->output_count != (form == 3U ? 1U : 0U) ||
        source->input_count != (form == 4U ? 1U : 0U))
        return false;
    operand = form == 3U ? &source->outputs[0] : &source->inputs[0];
    if (!target_constraint_is(operand, form == 3U ? "=rm" : "g"))
        return false;
    expression = minic_c0_program_expression(program, operand->expression);
    if (!expression ||
        (form == 3U && (expression->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expression->type))) ||
        !minic_type_equal(expression->type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 3U ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_rep_movsb_alternative_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    static const char *const constraints[] = {"+c", "+D", "+S", "+r"};
    const MinicExpression *expression;
    const MinicFixedRegisterBinding *stack_binding;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_rep_movsb_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 4U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != 2U || !source->has_memory_clobber ||
        source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "rax"))
        return false;
    for (i = 0U; i < 4U; ++i) {
        if (!target_constraint_is(&source->outputs[i], constraints[i]))
            return false;
        expression = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expression ||
            (i != 3U && expression->value_category != MINIC_VALUE_LVALUE) ||
            (i != 3U && minic_type_is_const(expression->type)) ||
            (i == 0U || i == 3U ?
                !minic_type_equal(expression->type, minic_type_unsigned_long()) :
                !minic_type_is_pointer(expression->type)))
            return false;
    }
    if (expression->kind != MINIC_EXPRESSION_FIXED_REGISTER) return false;
    stack_binding = minic_c0_program_fixed_register_binding(
        program, expression->value.fixed_register_binding_id);
    if (!stack_binding || !stack_binding->register_name ||
        strcmp(stack_binding->register_name, "rsp") != 0)
        return false;
    contract->operand_count = 4U;
    for (i = 0U; i < 4U; ++i)
        contract->kinds[i] =
            i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
                      MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_segment_register_extable_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *selector;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_segment_register_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        source->register_clobber_count ||
        !c_is(&source->outputs[0], "+r"))
        return false;
    selector = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(selector->type) ||
        !minic_type_unqualified(selector->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_cmpxchg128_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_cmpxchg128_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *cc, *memory, *low, *high, *new_low, *new_high;
    const bool alt = form == 2U;
    const size_t at = alt ? 0U : 1U;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (alt ? 3U : 4U) || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        !source->has_memory_clobber ||
        source->clobber_count != (alt ? 2U : 1U) ||
        source->register_clobber_count != (alt ? 1U : 0U) ||
        (alt && (!source->register_clobbers ||
                 !clobber_is(&source->register_clobbers[0], "rsi"))) ||
        (!alt && !target_constraint_is(&source->outputs[0], "=@cce")) ||
        !target_constraint_is(&source->outputs[at], "+m") ||
        !target_constraint_is(&source->outputs[at+1U], "+a") ||
        !target_constraint_is(&source->outputs[at+2U], "+d") ||
        !target_constraint_is(&source->inputs[0], "b") ||
        !target_constraint_is(&source->inputs[1], "c") ||
        !source->outputs[at].name ||
        source->outputs[at].name_length != 3U ||
        memcmp(source->outputs[at].name, alt ? "var" : "ptr", 3U) != 0)
        return false;
    cc = alt ? NULL : minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(program, source->outputs[at].expression);
    low = minic_c0_program_expression(program, source->outputs[at+1U].expression);
    high = minic_c0_program_expression(program, source->outputs[at+2U].expression);
    new_low = minic_c0_program_expression(program, source->inputs[0].expression);
    new_high = minic_c0_program_expression(program, source->inputs[1].expression);
    if ((!alt && (!cc || cc->value_category != MINIC_VALUE_LVALUE ||
                  !minic_type_is_integer(cc->type) ||
                  cc->type.integer_rank != MINIC_INTEGER_RANK_BOOL)) ||
        !memory || memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(memory->type) ||
        !minic_type_is_integer(memory->type) ||
        memory->type.integer_rank != MINIC_INTEGER_RANK_INT128)
        return false;
    {
        const MinicExpression *parts[] = {low, high, new_low, new_high};
        for (i=0U;i<4U;++i)
            if (!parts[i] || !minic_type_is_integer(parts[i]->type) ||
                (i<2U && (parts[i]->value_category != MINIC_VALUE_LVALUE ||
                          minic_type_is_const(parts[i]->type))) ||
                (parts[i]->type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                 parts[i]->type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
                return false;
    }
    contract->operand_count = alt ? 5U : 6U;
    if (!alt)
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[at] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[at+1U] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[at+2U] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[at+3U] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[at+4U] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_put_user_call_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_put_user_call_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *error, *stack, *user, *value, *size;
    const MinicFixedRegisterBinding *binding;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || (form != 0x100U && form != 0x400U) ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 3U || !source->inputs || source->label_count ||
        source->clobber_count != 1U || source->has_memory_clobber ||
        source->register_clobber_count != 1U || !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "ebx") ||
        !target_constraint_is(&source->outputs[0], "=c") ||
        !target_constraint_is(&source->outputs[1], "+r") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "r") ||
        !target_constraint_is(&source->inputs[2], "i") ||
        !source->inputs[2].name || source->inputs[2].name_length != 4U ||
        memcmp(source->inputs[2].name, "size", 4U) != 0)
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    stack = minic_c0_program_expression(
        program, source->outputs[1].expression);
    user = minic_c0_program_expression(
        program, source->inputs[0].expression);
    value = minic_c0_program_expression(
        program, source->inputs[1].expression);
    size = minic_c0_program_expression(
        program, source->inputs[2].expression);
    if (!error || error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !stack || stack->kind != MINIC_EXPRESSION_FIXED_REGISTER ||
        !minic_type_is_integer(stack->type) ||
        !user || !minic_type_is_pointer(user->type) ||
        /* GNU __put_user_8 stores either an integer payload or an address.
         * Linux's robust-list/sigaltstack writers load C object pointers
         * in the same mandatory RAX register and preserve all 64 bits. */
        !value || (!minic_type_is_integer(value->type) &&
                   !minic_type_is_pointer(value->type)) ||
        !size || !minic_type_is_integer(size->type))
        return false;
    binding = minic_c0_program_fixed_register_binding(
        program, stack->value.fixed_register_binding_id);
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0)
        return false;
    /* The source's explicit RAX binding is material: this private helper
     * does not use the ordinary System V argument register allocator. */
    if (value->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        binding = minic_c0_program_fixed_register_binding(
            program, value->value.fixed_register_binding_id);
    else if (value->kind == MINIC_EXPRESSION_LOCAL)
        binding = minic_c0_program_local_fixed_register_binding(
            program, value->value.local_id);
    else
        return false;
    if (!binding || !binding->register_name ||
        (strcmp(binding->register_name, "rax") != 0 &&
         strcmp(binding->register_name, "%rax") != 0))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    /* Numeric-template normalization rewrites %[size] to Core operand 4.
     * The production selector independently proves that exact normalized
     * spelling before selecting a private helper CALL. */
    contract->target_machine_form_variant = form == 0x100U ? 0x200U : 0x800U;
    return true;
}

static bool x86_linux_get_user_call_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_get_user_call_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *error, *value, *stack, *pointer, *size;
    const MinicFixedRegisterBinding *binding;
    MinicType plain;
    if (!program || !source || !contract || (form != 1U && form != 2U) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 3U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->outputs[1], "=r") ||
        !target_constraint_is(&source->outputs[2], "+r") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "i") ||
        !source->inputs[1].name || source->inputs[1].name_length != 4U ||
        memcmp(source->inputs[1].name, "size", 4U) != 0)
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    value = minic_c0_program_expression(
        program, source->outputs[1].expression);
    stack = minic_c0_program_expression(
        program, source->outputs[2].expression);
    pointer = minic_c0_program_expression(
        program, source->inputs[0].expression);
    size = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !value || value->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(value->type) || !minic_type_is_integer(value->type) ||
        !stack || stack->kind != MINIC_EXPRESSION_FIXED_REGISTER ||
        !minic_type_is_integer(stack->type) ||
        !pointer || !minic_type_is_pointer(pointer->type) ||
        !size || !minic_type_is_integer(size->type))
        return false;
    binding = minic_c0_program_fixed_register_binding(
        program, stack->value.fixed_register_binding_id);
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0)
        return false;
    if (value->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        binding = minic_c0_program_fixed_register_binding(
            program, value->value.fixed_register_binding_id);
    else if (value->kind == MINIC_EXPRESSION_LOCAL)
        binding = minic_c0_program_local_fixed_register_binding(
            program, value->value.local_id);
    else
        return false;
    if (!binding || !binding->register_name ||
        (strcmp(binding->register_name, "rdx") != 0 &&
         strcmp(binding->register_name, "%rdx") != 0))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form == 1U ? 3U : 4U;
    return true;
}

static bool x86_linux_pat_clflushopt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *address, *zero;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !minic_x86_64_linux_pat_clflushopt_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->outputs[0], "+m") ||
        !target_constraint_is(&source->inputs[0], "i")) return false;
    address = minic_c0_program_expression(
        program, source->outputs[0].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!address || address->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(address->type) ||
        !minic_type_unqualified(address->type, &plain) ||
        plain.base_kind != MINIC_TYPE_BASE_INT ||
        plain.integer_rank != MINIC_INTEGER_RANK_CHAR ||
        plain.pointer_depth != 0U ||
        !zero || !minic_type_is_integer(zero->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Two original Linux BUG/WARN GNU %c shapes.  The C ingress captures each
 * source `i` as a typed immediate/symbol; no immediate is ever inferred from
 * raw asm text by machine selection. */
static bool x86_linux_bug_table_c_source(
    const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const names[4] = {"path","ln","fl","sz"};
    const MinicExpression *expression;
    unsigned int variant;
    size_t i;
    (void)invocation;
    if (program == NULL || source == NULL || contract == NULL ||
        (variant = minic_x86_64_linux_bug_table_c_form(
            source->template_text,source->template_length)) == 0U ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 0U || source->input_count != 4U ||
        source->inputs == NULL || source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        source->has_memory_clobber || source->label_count != 0U)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        if (!target_constraint_is(input,"i") ||
            (variant == 1U ? input->name != NULL :
                (input->name == NULL ||
                 input->name_length != strlen(names[i]) ||
                 memcmp(input->name,names[i],input->name_length) != 0)))
            return false;
        expression = minic_c0_program_expression(program,input->expression);
        if (expression == NULL || (i == 0U ?
                (!minic_type_is_pointer(expression->type) ||
                 (expression->kind != MINIC_EXPRESSION_GLOBAL_OBJECT &&
                  expression->kind != MINIC_EXPRESSION_ADDRESS_OF &&
                  expression->kind != MINIC_EXPRESSION_FUNCTION &&
                  expression->kind != MINIC_EXPRESSION_CAST &&
                  expression->kind != MINIC_EXPRESSION_BITCAST &&
                  expression->kind != MINIC_EXPRESSION_CONVERSION)) :
                (expression->kind != MINIC_EXPRESSION_INTEGER ||
                 (i == 3U ? expression->value.integer_value != 12 :
                    expression->value.integer_value < 0 ||
                    expression->value.integer_value > 65535))))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->target_machine_form_variant = variant;
    contract->capture_typed_immediate_inputs = true;
    return true;
}

static bool x86_linux_futex_extable_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_futex_extable_form(
        source->template_text, source->template_length) : 0U;
    const bool loop = form >= 4U;
    const size_t output_count = loop ? 4U : 3U;
    const MinicExpression *expression;
    MinicType plain;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        !source->outputs || !source->inputs ||
        source->output_count != output_count || source->input_count != 2U ||
        source->label_count || source->register_clobber_count ||
        source->has_memory_clobber != (form == 1U) ||
        source->clobber_count != (form == 1U ? 1U : 0U) ||
        !target_constraint_is(&source->outputs[0],
            form == 1U ? "+r" : loop ? "=&a" : "=r") ||
        !target_constraint_is(&source->outputs[1],
            form == 1U ? "=a" : loop ? "=&r" : "=r") ||
        !target_constraint_is(&source->outputs[2], "+m") ||
        (loop && !target_constraint_is(&source->outputs[3], "=&r")) ||
        !target_constraint_is(&source->inputs[0],
            loop || form == 1U ? "r" : "0") ||
        !target_constraint_is(&source->inputs[1], "1"))
        return false;
    for (i = 0U; i < output_count + 2U; ++i) {
        const MinicInlineAsmOperand *op = i < output_count ?
            &source->outputs[i] : &source->inputs[i - output_count];
        expression = minic_c0_program_expression(program, op->expression);
        if (!expression || (i < output_count &&
                (expression->value_category != MINIC_VALUE_LVALUE ||
                 minic_type_is_const(expression->type))) ||
            !minic_type_unqualified(expression->type, &plain) ||
            !minic_type_is_integer(plain) ||
            plain.base_kind != MINIC_TYPE_BASE_INT ||
            plain.integer_rank != MINIC_INTEGER_RANK_INT ||
            plain.pointer_depth != 0U) return false;
        contract->kinds[i] = i == 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < output_count ?
                (i == 0U && form == 1U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = output_count + 2U;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = form;
    return true;
}

static bool x86_linux_idle_clflush_alt_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *zero, *address;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 2U ||
        !source->inputs || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !minic_x86_64_linux_idle_clflush_alt_form(
            source->template_text, source->template_length, false) ||
        !target_constraint_is(&source->inputs[0], "i") ||
        !target_constraint_is(&source->inputs[1], "a") ||
        !source->inputs[1].name || source->inputs[1].name_length != 4U ||
        memcmp(source->inputs[1].name, "addr", 4U) != 0)
        return false;
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    address = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!zero || zero->kind != MINIC_EXPRESSION_INTEGER ||
        zero->value.integer_value != 0 ||
        !address || !minic_type_is_pointer(address->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_verw_clear_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const MinicExpression *selector;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 1U ||
        !source->inputs || source->label_count || source->clobber_count != 1U ||
        source->has_memory_clobber || source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "cc") ||
        !minic_x86_64_linux_verw_clear_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->inputs[0], "m"))
        return false;
    selector = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(selector->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    contract->target_machine_form_variant = 1U;
    return true;
}

static bool x86_linux_debug_register_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    const unsigned int form = source ? minic_x86_64_linux_debug_register_form(
        source->template_text, source->template_length) : 0U;
    const bool read = form >= 1U && form <= 6U;
    const bool dr7 = form == 6U || form == 12U;
    const MinicExpression *value, *dummy;
    MinicType plain;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->is_volatile != dr7 || source->output_count != (read ? 1U : 0U) ||
        source->input_count != (read ? (dr7 ? 1U : 0U) : (dr7 ? 2U : 1U)) ||
        (read && !source->outputs) ||
        (source->input_count && !source->inputs) ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count || source->has_memory_clobber ||
        (read ? !target_constraint_is(&source->outputs[0], "=r") :
                !target_constraint_is(&source->inputs[0], "r")) ||
        (dr7 && !target_constraint_is(&source->inputs[read ? 0U : 1U], "m")))
        return false;
    value = minic_c0_program_expression(program, read ?
        source->outputs[0].expression : source->inputs[0].expression);
    if (!value || (read && (value->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    if (dr7) {
        dummy = minic_c0_program_expression(
            program, source->inputs[read ? 0U : 1U].expression);
        if (!dummy || dummy->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(dummy->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_int()))
            return false;
    }
    contract->operand_count = dr7 ? 2U : 1U;
    contract->kinds[0] = read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (dr7)
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}
/* The source-level ABI recognizers share one admission signature. This
 * adapter keeps the no-operand memory barrier in the same ordered inventory
 * instead of making it a hidden exceptional dispatch branch. */
static bool x86_linux_zero_barrier_qualifier(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    (void)invocation;
    (void)program;
    return x86_linux_zero_barrier_source(source, contract);
}
/* GNU lowercase `i` is generic constant input, distinct from x86 ISA `I`.
 * Capture a source-validated typed integer instead of silently dropping its
 * C operand on a zero-operand NOP machine path. */
static bool x86_nop_const_i_source(const MinicTargetInvocation *invocation,
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expression;
    (void)invocation;
    if (program == NULL || source == NULL || contract == NULL ||
        !source->is_volatile || source->is_goto ||
        source->template_text == NULL || source->template_length != 3U ||
        memcmp(source->template_text,"nop",3U) != 0 ||
        source->output_count != 0U || source->input_count != 1U ||
        source->inputs == NULL || source->label_count != 0U ||
        source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0],"i") ||
        source->inputs[0].name != NULL) return false;
    expression = minic_c0_program_expression(program,
        source->inputs[0].expression);
    if (expression == NULL ||
        expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(expression->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->capture_typed_immediate_inputs = true;
    contract->target_machine_form_variant = 1U;
    return true;
}
typedef bool (*MinicX86SourceQualifier)(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);

/* Source matching is a policy table, not a machine emitter or Core opcode.
 * Order is part of the existing qualification behavior: the first complete
 * source contract wins, with the generic arithmetic forms only at the end.
 * No new Linux template or ISA subset is implicitly admitted here. */
typedef struct MinicX86SourceQualifierEntry {
    MinicX86SourceQualifier qualify;
    MinicX86MachineAsmForm machine_form;
} MinicX86SourceQualifierEntry;

#define X86_SOURCE_TYPED(fn, id) {fn, MINIC_X86_MACHINE_ASM_##id}
static const MinicX86SourceQualifierEntry minic_x86_source_qualifiers[] = {
    X86_SOURCE_TYPED(x86_linux_crash_register_source, CRASH_REGISTER),
    X86_SOURCE_TYPED(x86_linux_pm_trace_source, PM_TRACE),
    X86_SOURCE_TYPED(x86_linux_vmware_hypercall_source, VMWARE_HYPERCALL),
    X86_SOURCE_TYPED(x86_linux_vmware_stealclock_alt_source, VMWARE_STEALCLOCK_ALT),
    X86_SOURCE_TYPED(x86_linux_kvm_hypercall_alt_source, KVM_HYPERCALL_ALT),
    X86_SOURCE_TYPED(x86_linux_exception_stack_call_source, EXCEPTION_STACK_CALL),
    X86_SOURCE_TYPED(x86_linux_legacy_fnsave_source, LEGACY_FNSAVE),
    X86_SOURCE_TYPED(x86_linux_kexec_load_segments_source, KEXEC_LOAD_SEGMENTS),
    X86_SOURCE_TYPED(x86_linux_efi_kernel_transfer_source, EFI_KERNEL_TRANSFER),
    X86_SOURCE_TYPED(x86_linux_pci_mmconfig_source, PCI_MMCONFIG),
    X86_SOURCE_TYPED(x86_linux_cmpxchg128_source, CMPXCHG128),
    X86_SOURCE_TYPED(x86_linux_task_size_alt_source, TASK_SIZE_ALT),
    X86_SOURCE_TYPED(x86_linux_unwind_context_source, UNWIND_CONTEXT),
    X86_SOURCE_TYPED(x86_linux_mxcsr_source, MXCSR),
    X86_SOURCE_TYPED(x86_linux_irq_stack_call_source, IRQ_STACK_CALL),
    X86_SOURCE_TYPED(x86_linux_preempt_static_call_source, PREEMPT_STATIC_CALL),
    X86_SOURCE_TYPED(x86_linux_preempt_gs_dec_source, PREEMPT_GS_DEC),
    X86_SOURCE_TYPED(x86_linux_gen_endbr_source, GEN_ENDBR),
    X86_SOURCE_TYPED(x86_linux_kaslr_mul_source, KASLR_MUL),
    X86_SOURCE_TYPED(x86_linux_muldiv_u64_source, MULDIV_U64),
    X86_SOURCE_TYPED(x86_linux_via_xstore_source, VIA_XSTORE),
    X86_SOURCE_TYPED(x86_linux_crash_segment_snapshot_source, CRASH_SEGMENT_SNAPSHOT),
    X86_SOURCE_TYPED(x86_linux_task_control_source, TASK_CONTROL),
    X86_SOURCE_TYPED(x86_linux_iret_to_self_source, IRET_TO_SELF),
    X86_SOURCE_TYPED(x86_linux_int3_selftest_source, INT3_SELFTEST),
    X86_SOURCE_TYPED(x86_linux_pv_indirect_call_source, PV_INDIRECT_CALL),
    X86_SOURCE_TYPED(x86_linux_alt_reloc_selftest_source, ALT_RELOC_SELFTEST),
    X86_SOURCE_TYPED(x86_linux_real_mode_far_jump_source, REAL_MODE_FAR_JUMP),
    X86_SOURCE_TYPED(x86_linux_mmio_read_source, MMIO_READ),
    X86_SOURCE_TYPED(x86_linux_mmio_write_source, MMIO_WRITE),
    X86_SOURCE_TYPED(x86_linux_rep_string_io_source, REP_STRING_IO),
    X86_SOURCE_TYPED(x86_linux_iomem_copy_source, IOMEM_COPY),
    X86_SOURCE_TYPED(x86_linux_i915_movntdqa_source, I915_MOVNTDQA),
    X86_SOURCE_TYPED(x86_linux_raid6_simd_source, RAID6_SIMD),
    X86_SOURCE_TYPED(x86_linux_kvm_movdqa_source, KVM_MOVDQA),
    X86_SOURCE_TYPED(x86_linux_flushcache_copy_source, FLUSHCACHE_COPY),
    X86_SOURCE_TYPED(x86_linux_atomic_xchg_source, ATOMIC_XCHG),
    X86_SOURCE_TYPED(x86_linux_pvclock_scale_source, PVCLOCK_SCALE),
    X86_SOURCE_TYPED(x86_linux_pkru_source, PKRU),
    X86_SOURCE_TYPED(x86_linux_fdiv_bug_source, FDIV_BUG),
    X86_SOURCE_TYPED(x86_linux_monitor_wait_source, MONITOR_WAIT),
    X86_SOURCE_TYPED(x86_zstd_cpuid_source, ZSTD_CPUID),
    X86_SOURCE_TYPED(x86_linux_segment_read_source, SEGMENT_READ),
    X86_SOURCE_TYPED(x86_linux_segment_load_extable_source, SEGMENT_LOAD_EXTABLE),
    X86_SOURCE_TYPED(x86_linux_lkgs_extable_source, LKGS_EXTABLE),
    X86_SOURCE_TYPED(x86_linux_load_unaligned_zeropad_source, LOAD_UNALIGNED_ZEROPAD),
    X86_SOURCE_TYPED(x86_linux_lar_selector_source, LAR_SELECTOR),
    X86_SOURCE_TYPED(x86_linux_random_seed_source, RANDOM_SEED),
    X86_SOURCE_TYPED(x86_linux_fsgsbase_source, FSGSBASE),
    X86_SOURCE_TYPED(x86_linux_kernel_nofault_goto_source, KERNEL_NOFAULT_GOTO),
    X86_SOURCE_TYPED(x86_linux_rep_movsb_alternative_source, REP_MOVSB_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_rep_stosb_alternative_source, REP_STOSB_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_fpu_user_frame_source, FPU_USER_FRAME),
    X86_SOURCE_TYPED(x86_linux_x87_fild_prepare_source, X87_FILD_PREPARE),
    X86_SOURCE_TYPED(x86_linux_fpu_kernel_restore_source, FPU_KERNEL_RESTORE),
    X86_SOURCE_TYPED(x86_linux_fxrstor_safe_source, FXRSTOR_SAFE),
    X86_SOURCE_TYPED(x86_linux_xstate_fault_source, XSTATE_FAULT),
    X86_SOURCE_TYPED(x86_linux_xrstor_alternative_source, XRSTOR_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_xsave_triple_alternative_source, XSAVE_TRIPLE_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_smap_alternative_source, SMAP_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_segment_register_extable_source, SEGMENT_REGISTER_EXTABLE),
    X86_SOURCE_TYPED(x86_linux_put_user_call_source, PUT_USER_CALL),
    X86_SOURCE_TYPED(x86_linux_get_user_call_source, GET_USER_CALL),
    X86_SOURCE_TYPED(x86_linux_idle_clflush_alt_source, IDLE_CLFLUSH_ALT),
    X86_SOURCE_TYPED(x86_linux_pat_clflushopt_source, PAT_CLFLUSHOPT),
    X86_SOURCE_TYPED(x86_linux_futex_extable_source, FUTEX_EXTABLE),
    X86_SOURCE_TYPED(x86_linux_verw_clear_source, VERW_CLEAR),
    X86_SOURCE_TYPED(x86_linux_debug_register_source, DEBUG_REGISTER),
    X86_SOURCE_TYPED(x86_x87_probe_source, X87_PROBE),
    X86_SOURCE_TYPED(x86_fxsave_source, FXSAVE),
    X86_SOURCE_TYPED(x86_xsetbv_source, XSETBV),
    X86_SOURCE_TYPED(x86_linux_alternative_wrmsr_source, ALTERNATIVE_WRMSR),
    X86_SOURCE_TYPED(x86_linux_control_register_source, CONTROL_REGISTER),
    X86_SOURCE_TYPED(x86_linux_tlb_cache_source, TLB_CACHE),
    X86_SOURCE_TYPED(x86_linux_three_call_alternative_source, THREE_CALL_ALTERNATIVE),
    X86_SOURCE_TYPED(x86_linux_boot_segments_source, BOOT_SEGMENTS),
    X86_SOURCE_TYPED(x86_linux_lidt_source, LIDT),
    X86_SOURCE_TYPED(x86_linux_gs_testcc_source, GS_TESTCC),
    X86_SOURCE_TYPED(x86_linux_testcc_source, TESTCC),
    X86_SOURCE_TYPED(x86_prefetchw_alt_source, PREFETCHW_ALT),
    X86_SOURCE_TYPED(x86_apic_amd_alt_source, APIC_AMD_ALT),
    X86_SOURCE_TYPED(x86_gs_tied_alu_source, GS_TIED_ALU),
    X86_SOURCE_TYPED(x86_linux_bug_table_c_source, BUG_TABLE_C_IMMEDIATE),
    X86_SOURCE_TYPED(x86_nop_const_i_source, NOP_CONST_I),
    X86_SOURCE_TYPED(x86_linux_zero_barrier_qualifier, ZERO_BARRIER),
    X86_SOURCE_TYPED(x86_linux_ip_here_source, IP_HERE),
    X86_SOURCE_TYPED(x86_add32_carry_source, ADD32_CARRY),
    X86_SOURCE_TYPED(x86_linux_tcpudp_nofold_source, TCPUDP_NOFOLD),
    X86_SOURCE_TYPED(x86_percpu_gs_source, PERCPU_GS),
    X86_SOURCE_TYPED(x86_ip_fast_csum_source, IP_FAST_CSUM),
    X86_SOURCE_TYPED(x86_checksum_source, CHECKSUM),
    X86_SOURCE_TYPED(x86_linux_msr_source, MSR),
    X86_SOURCE_TYPED(x86_hweight_source, HWEIGHT),
    X86_SOURCE_TYPED(x86_counter_source, COUNTER),
    X86_SOURCE_TYPED(x86_port_io_source, PORT_IO),
    X86_SOURCE_TYPED(x86_linux_gs_atomic_source, GS_ATOMIC),
    X86_SOURCE_TYPED(x86_linux_rep_stos_source, REP_STOS),
    X86_SOURCE_TYPED(x86_linux_lowlevel_scalar_source, LOWLEVEL_SCALAR),
    X86_SOURCE_TYPED(x86_linux_boot_flags_source, BOOT_FLAGS),
    X86_SOURCE_TYPED(x86_linux_delay_mul_source, DELAY_MUL),
    X86_SOURCE_TYPED(x86_linux_delay_wait_source, DELAY_WAIT),
    X86_SOURCE_TYPED(x86_linux_identity_load_source, IDENTITY_LOAD),
    X86_SOURCE_TYPED(x86_linux_ipv6_checksum_source, IPV6_CHECKSUM),
    X86_SOURCE_TYPED(x86_linux_lock_bitcc_source, LOCK_BITCC),
    X86_SOURCE_TYPED(x86_linux_boot_string_source, BOOT_STRING),
    X86_SOURCE_TYPED(x86_linux_vdso_syscall_source, VDSO_SYSCALL),
    X86_SOURCE_TYPED(x86_linux_vdso_cpunode_source, VDSO_CPUNODE),
    X86_SOURCE_TYPED(x86_linux_csum_partial_source, CSUM_PARTIAL),
    X86_SOURCE_TYPED(x86_linux_atomic_rmw_source, ATOMIC_RMW),
    X86_SOURCE_TYPED(x86_linux_raw_cmpxchg_source, RAW_CMPXCHG),
    X86_SOURCE_TYPED(x86_trycas_source, TRYCAS),
    X86_SOURCE_TYPED(x86_linux_bitops_source, BITOPS),
    X86_SOURCE_TYPED(x86_linux_atomic_cc_source, ATOMIC_CC),
};
#undef X86_SOURCE_TYPED


bool minic_x86_64_qualify_inline_asm(
    const MinicTargetInvocation *invocation, const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    bool is_mulacc = false;
    uint32_t nospec_width = 0U;
    size_t i, count;
    if (!invocation || !contract) return false;
    for (i = 0U;
         i < sizeof(minic_x86_source_qualifiers) /
                 sizeof(minic_x86_source_qualifiers[0]);
         ++i) {
        if (minic_x86_source_qualifiers[i].qualify(invocation, program, source, contract)) {
            contract->target_machine_form_id =
                (uint32_t)minic_x86_source_qualifiers[i].machine_form;
            return true;
        }
    }
    if (x86_nospec_source(program, source, &nospec_width)) {
        contract->target_machine_form_id = MINIC_X86_MACHINE_ASM_NOSPEC;
        contract->target_machine_form_variant = nospec_width;
        contract->operand_count = 3U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (!qualified(program, source, &is_mulacc, contract)) return false;
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
