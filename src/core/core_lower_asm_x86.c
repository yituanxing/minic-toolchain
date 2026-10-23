#include "core/core_lower_asm_internal.h"
#include <stdlib.h>
#include <string.h>

/* Source-independent x86 register-constraint ownership. A complete input
 * contract is checked without Core mutations; machine register allocation
 * remains in the x86 emitter. The currently qualified forms are a four-way
 * matched fixed-register instruction, and a 192-bit carry-chain with
 * register-or-memory outputs plus an explicit memory input. */
static bool c_is(const MinicInlineAsmOperand *op, const char *s) {
    return core_inline_asm_constraint_is(op, s);
}
static bool clobber_is(const MinicInlineAsmRegisterClobber *op, const char *s) {
    const size_t n = strlen(s);
    return op->name && op->name_length == n && memcmp(op->name, s, n) == 0;
}
/* Whitespace is irrelevant for these four specific instructions, whose
 * operand constraints and exact instruction sequence are checked together. */
static bool mulacc_text(const MinicInlineAsm *src) {
    static const char expected[] = "mulq%7addq%%rax,%0adcq%%rdx,%1adcq$0,%2";
    char text[128];
    size_t i, j = 0U;
    if (src->template_length >= sizeof(text)) return false;
    for (i = 0U; i < src->template_length; ++i) {
        const char c = src->template_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j >= sizeof(text)-1U) return false;
        text[j++] = c;
    }
    text[j] = '\0';
    return strcmp(text, expected) == 0;
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc) return false;
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
               mulacc_text(src)) {
        *is_mulacc = true;
        n = 3U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=rm") ||
                !c_is(&src->inputs[i], mul_in[i])) return false;
        if (!c_is(&src->inputs[3], "a") ||
            !c_is(&src->inputs[4], "m")) return false;
    } else return false;

    for (i = 0U; i < src->output_count + src->input_count; ++i) {
        const bool is_output = i < src->output_count;
        const MinicInlineAsmOperand *op = is_output
            ? &src->outputs[i] : &src->inputs[i-src->output_count];
        const MinicExpression *expr =
            minic_c0_program_expression(program, op->expression);
        const MinicType target_type =
            *is_mulacc ? minic_type_unsigned_long() : minic_type_unsigned_int();
        if (!expr || !minic_type_unqualified(expr->type, &normalized) ||
            !minic_type_equal(normalized, target_type) ||
            (is_output && (expr->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(expr->type))) ||
            (*is_mulacc && i == 7U &&
             expr->value_category != MINIC_VALUE_LVALUE))
            return false;
    }
    return true;
}

MinicCoreLowerStatus core_asm_try_x86_register_contract(
    MinicCoreLowerContext *ctx, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    MinicCoreInstruction ins;
    MinicCoreInlineAsmId id;
    char *normalized = NULL;
    size_t length, i;
    bool mulacc;
    if (!handled) return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    if (!ctx || !ctx->target ||
        ctx->target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        !ctx->body || !ctx->body->program || !ctx->function ||
        !statement || !qualified(ctx->body->program, source, &mulacc))
        return MINIC_CORE_LOWER_OK;
    *handled = true;
    if (!core_inline_asm_numeric_template(source, &normalized, &length))
        return MINIC_CORE_LOWER_ERROR;
    memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.span = statement->span;
    ins.type = minic_type_void();
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.structured_inline_asm.operand_count =
        source->output_count + source->input_count;
    for (i = 0U; i < ins.value.structured_inline_asm.operand_count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *op = output ? &source->outputs[i]
            : &source->inputs[i - source->output_count];
        MinicCoreStructuredInlineAsmOperand *bind =
            &ins.value.structured_inline_asm.operands[i];
        MinicCoreLowerStatus status;
        bind->operand_index = i;
        bind->kind = output
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : (mulacc && i == 7U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
        status = (output || (mulacc && i == 7U))
            ? lower_address(ctx, op->expression, &bind->value)
            : lower_expression(ctx, op->expression, &bind->value);
        if (status != MINIC_CORE_LOWER_OK) {
            free(normalized);
            return status;
        }
    }
    /* The observable output makes even a GNU nonvolatile CPUID semantically
     * effectful. Conservatively do not optimize or discard it. */
    if (!minic_core_function_add_opaque_inline_asm(
            ctx->function, normalized, length, true, false, &id)) {
        free(normalized);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(normalized);
    for (i = 0U; i < source->register_clobber_count; ++i) {
        const MinicInlineAsmRegisterClobber *c = &source->register_clobbers[i];
        if (!minic_core_function_add_inline_asm_register_clobber(
                ctx->function, id, c->name, c->name_length))
            return MINIC_CORE_LOWER_ERROR;
    }
    ins.value.structured_inline_asm.inline_asm_id = id;
    return minic_core_function_append_effect_instruction(
        ctx->function, ctx->block_id, &ins)
        ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}
