#include "target/aarch64/asm_contract.h"
#include <string.h>
/* Linux 6.6 array_index_mask_nospec: compare two unsigned LP64 words,
 * consume NZCV immediately with SBC, store the all-zero/all-one mask. */
static bool mask_template(const char *text, size_t len) {
    static const char expected[] = "cmp%1,%2sbc%0,xzr,xzr";
    char norm[96];
    size_t i, n = 0U;
    if (!text || len >= sizeof(norm)) return false;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(norm)) return false;
        norm[n++] = c;
    }
    norm[n] = '\0';
    return strcmp(norm, expected) == 0;
}
/* Exact original Linux 6.6.143 MRS register read family. These privileged
 * system registers can be assembled for EL1; do not user-mode QEMU execute
 * them and never turn this into arbitrary template passthrough. */
static const char *aarch64_linux_mrs_register(const char *text, size_t len) {
    char normalized[96];
    size_t i, used = 0U;
    static const struct { const char *text; const char *reg; } allowed[] = {
        {"mrs%0,sp_el0", "sp_el0"},
        {"mrs%0,tpidr_el1", "tpidr_el1"},
        {"mrs%0,tpidr_el2", "tpidr_el2"}
    };
    if (text == NULL || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (used + 1U >= sizeof(normalized))
            return NULL;
        normalized[used++] = c;
    }
    normalized[used] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(allowed[i].text, normalized) == 0)
            return allowed[i].reg;
    return NULL;
}
/* The architectural DMB/DSB/ISB operations are hardware fences; they
 * cannot be silently downgraded into a compiler-only memory clobber.
 * The source and machine both use this target-owned exact template table. */
static const char *aarch64_barrier_template(const char *text, size_t len) {
    static const char *const allowed[] = {
        "dmb ish", "dmb ishld", "dmb ishst", "dmb sy",
        "dmb osh", "dmb oshld", "dmb oshst",
        "dsb sy", "dsb ish", "isb"
    };
    char normalized[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\t' || c == '\r' || c == '\n')
            return NULL; /* no hidden second hardware instruction */
        if (n + 1U >= sizeof(normalized))
            return NULL;
        normalized[n++] = c;
    }
    while (n != 0U && normalized[n - 1U] == ' ') --n;
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0)
            return allowed[i];
    return NULL;
}
static bool constraint(const MinicInlineAsmOperand *op, const char *word) {
    size_t n = strlen(word);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, word, n) == 0;
}
bool minic_aarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    MinicType plain;
    size_t i;
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->output_count == 0U && src->input_count == 0U &&
        src->clobber_count == 1U && src->register_clobber_count == 0U &&
        src->has_memory_clobber && src->label_count == 0U &&
        aarch64_barrier_template(src->template_text, src->template_length)) {
        contract->operand_count = 0U;
        return true;
    }
    if (program && src && contract &&
        !src->is_goto && !src->is_volatile &&
        src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            minic_type_equal(plain, minic_type_unsigned_long()) &&
            expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
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
bool minic_aarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *b;
    size_t i;
    if (!out || !ctx || !ctx->fn || !ins || ctx->dynamic_frame ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type))
        return false;
    fn = ctx->fn;
    if (ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
        return false;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (ins->value.structured_inline_asm.operand_count == 0U) {
        const char *barrier = aarch64_barrier_template(
            info->template_text, info->template_length);
        if (!barrier || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count != 0U)
            return false;
        return fprintf(out, "  %s\n", barrier) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 1U) {
        const MinicCoreStructuredInlineAsmOperand *operand =
            ins->value.structured_inline_asm.operands;
        const char *reg = aarch64_linux_mrs_register(
            info->template_text, info->template_length);
        MinicType pointee;
        if (reg == NULL || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count != 0U ||
            operand == NULL || operand[0].operand_index != 0U ||
            operand[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            operand[0].early_clobber || operand[0].has_fixed_register_binding ||
            operand[0].value >= fn->value_count ||
            operand[0].value > (size_t)4095U / 8U ||
            !minic_type_pointee(fn->values[operand[0].value].type, &pointee) ||
            !minic_type_equal(pointee, minic_type_unsigned_long()))
            return false;
        /* The output SSA operand is an address. The backend reads the real
         * privileged register into an internal GPR and stores the exact LP64
         * word back to the existing Core object. No external symbol/alias. */
        return fprintf(out, "  mrs x9, %s\n"
                            "  ldr x10, [sp, #%zu]\n"
                            "  str x9, [x10]\n",
                       reg, (size_t)operand[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    if (!info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !info->register_clobbers[0].name ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(info->template_text, info->template_length))
        return false;
    b = ins->value.structured_inline_asm.operands;
    for (i = 0U; i < 3U; ++i) {
        MinicType ty;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].value > (size_t)4095U / 8U)
            return false;
        ty = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(ty, &ty)) return false;
        if (!minic_type_equal(ty, minic_type_unsigned_long()))
            return false;
    }
    /* GNU Ir admits a register alternative: x11 holds the size. x9/x10/x11
     * are caller-saved scratch and all live C values are in existing SSA
     * stack slots. AArch64 NZCV from CMP survives until the following SBC.
     * No direct raw-template passthrough or unsafe physical clobber alias. */
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  cmp x10, x11\n"
        "  sbc x9, xzr, xzr\n"
        "  ldr x10, [sp, #%zu]\n"
        "  str x9, [x10]\n",
        (size_t)b[1].value * 8U,
        (size_t)b[2].value * 8U,
        (size_t)b[0].value * 8U) >= 0;
}
