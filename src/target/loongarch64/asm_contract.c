#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/frame_emit.h"

#include <stdio.h>
#include <string.h>

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
    const char *text, size_t length) {
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
    for (i = 0U; i < sizeof(loong_atomic) / sizeof(loong_atomic[0]); ++i)
        if (strcmp(compact, loong_atomic[i].text) == 0)
            return &loong_atomic[i];
    return NULL;
}
static bool loong_atomic_scalar_type(MinicType type, size_t width) {
    MinicType plain;
    return minic_type_unqualified(type, &plain) &&
           minic_type_equal(plain, width == 8U ?
               minic_type_long() : minic_type_int());
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
    atom = loong_atomic_template(src->template_text, src->template_length);
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

bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicInlineAsmOperand *op;
    const MinicExpression *expr;
    if (loong_atomic_qualified_source(program, src, contract))
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
    atom = loong_atomic_template(info->template_text, info->template_length);
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
