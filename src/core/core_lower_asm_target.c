#include "core/core_lower_asm_internal.h"
#include "frontend/const_eval.h"
#include "target/backend.h"
#include <stdlib.h>
#include <string.h>

/* Only Core values, addressability and effect construction belong here.
 * The selected target classifies GNU constraints without changing Core IR. */
MinicCoreLowerStatus core_asm_try_target_register_contract(
    MinicCoreLowerContext *ctx, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    MinicCoreInstruction ins;
    MinicCoreInlineAsmId id;
    char *normalized = NULL;
    size_t length, i;
    const MinicBackend *backend;
    MinicTargetInlineAsmContract contract;
    if (!handled) return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    if (!ctx || !ctx->target || !ctx->body || !ctx->body->program ||
        !ctx->function || !statement) return MINIC_CORE_LOWER_OK;
    backend = minic_backend_for_target(ctx->target);
    if (!minic_backend_qualify_inline_asm(
            backend, ctx->body->program, source, &contract))
        return MINIC_CORE_LOWER_OK;
    *handled = true;
    if (!core_inline_asm_numeric_template(source, &normalized, &length))
        return MINIC_CORE_LOWER_ERROR;
    memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.span = statement->span;
    ins.type = minic_type_void();
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.structured_inline_asm.operand_count = contract.operand_count;
    for (i = 0U; i < ins.value.structured_inline_asm.operand_count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *op = output ? &source->outputs[i]
            : &source->inputs[i - source->output_count];
        MinicCoreStructuredInlineAsmOperand *bind =
            &ins.value.structured_inline_asm.operands[i];
        const MinicExpression *source_expression = minic_c0_program_expression(
            ctx->body->program, op->expression);
        size_t source_fixed_binding_id;
        MinicCoreLowerStatus status;
        bind->operand_index = i;
        bind->kind = contract.kinds[i];
        /* A target-qualified GNU register operand may name a local
         * `register T v asm("rN")`. This is an instruction operand's
         * physical placement constraint, not a reservation of the GPR for
         * the C variable's entire lifetime. Transport the imported local
         * binding without exposing register names or ISA policy to Core.
         * Every backend must then prove its own complete physical operand
         * assignment or reject the form. This shared path runs BEFORE the
         * legacy fixed-register special case on all three Linux targets. */
        if ((bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
             bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
             bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
            core_inline_asm_fixed_binding_id(
                ctx->body->program, source_expression,
                &source_fixed_binding_id)) {
            if (!core_import_fixed_register_binding(
                    ctx, source_fixed_binding_id,
                    &bind->fixed_register_binding_id)) {
                free(normalized);
                return MINIC_CORE_LOWER_ERROR;
            }
            bind->has_fixed_register_binding = true;
        }
        if (bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            const MinicExpression *expr = source_expression;
            MinicConstValue folded;
            /* Target GNU immediate operands can be full integer constant
             * expressions (Linux uaccess bit55 uses 1UL << 55), not only
             * a literal token. Keep C constexpr evaluation wholly in
             * Common Core, emitting the SAME typed constant value as the
             * normal expression path. The machine backend still verifies
             * the actual Core definition and exact ISA encoding. This
             * rule is generic for all target-qualified scalar inputs. */
            /* A Linux GNU `i` operand often names a parameter of an
             * integer-specialized always-inline function. The source AST
             * remains a local read, but Core has an exact, flow-sensitive
             * constant fact for that local. Use the same Common oracle as
             * ordinary expression lowering; targets must still reject
             * runtime-varying ISA opcode/immediate operands. */
            if (expr && minic_type_is_integer(expr->type) &&
                core_const_eval_integer_with_locals(
                    ctx, op->expression, &folded)) {
                MinicCoreInstruction constant_ins;
                (void)memset(&constant_ins, 0, sizeof(constant_ins));
                constant_ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                constant_ins.span = expr->span;
                constant_ins.type = expr->type;
                constant_ins.result = MINIC_CORE_VALUE_INVALID;
                constant_ins.value.integer_value = (int64_t)folded.bits;
                status = minic_core_function_append_value_instruction(
                    ctx->function, ctx->block_id, &constant_ins, &bind->value)
                    ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
            } else {
                status = lower_expression(ctx, op->expression, &bind->value);
            }
        } else if (bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
                 bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
                 bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
                 bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)
            status = lower_address(ctx, op->expression, &bind->value);
        else status = MINIC_CORE_LOWER_UNSUPPORTED;
        if (status != MINIC_CORE_LOWER_OK) {
            free(normalized);
            return status;
        }
    }
    /* The observable output makes even a GNU nonvolatile CPUID semantically
     * effectful. Conservatively do not optimize or discard it. */
    /* A target-qualified +Q/+ZB memory RMW is a hardware side effect even
     * when GNU lists no explicit "memory" clobber. The existing Core verifier
     * requires its memory-readwrite effect to be represented explicitly;
     * derive that property from the TARGET-QUALIFIED operand role, not from
     * an architecture-specific template string or new Core opcode. */
    {
        bool memory_effect =
            source->has_memory_clobber || contract.implicit_memory_effect;
        for (i = 0U; i < contract.operand_count; ++i)
            if (contract.kinds[i] ==
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE)
                memory_effect = true;
        if (!minic_core_function_add_opaque_inline_asm(
                ctx->function, normalized, length, true,
                memory_effect, &id)) {
            free(normalized);
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    free(normalized);
    for (i = 0U; i < source->register_clobber_count; ++i) {
        const MinicInlineAsmRegisterClobber *c = &source->register_clobbers[i];
        if (!minic_core_function_add_inline_asm_register_clobber(
                ctx->function, id, c->name, c->name_length))
            return MINIC_CORE_LOWER_ERROR;
    }
    if (source->is_goto) {
        MinicCoreBlockId target_block;
        MinicCoreLowerStatus status;
        MinicCoreInlineAsm *stored;
        if (source->label_count != 1U || !source->labels ||
            source->labels[0].target_statement == MINIC_STATEMENT_INVALID)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        status = ensure_statement_block(
            ctx, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) return status;
        stored = &ctx->function->inline_asms[id];
        stored->is_goto = true;
        stored->goto_target = target_block;
        stored->source_inline_asm_id = SIZE_MAX;
    }
    ins.value.structured_inline_asm.inline_asm_id = id;
    return minic_core_function_append_effect_instruction(
        ctx->function, ctx->block_id, &ins)
        ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}
