#include "compiler/c_lower/core_lower_asm_internal.h"
#include "frontend/const_eval.h"
#include "target/backend_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only Core values, addressability and effect construction belong here.
 * The selected target classifies GNU constraints without changing Core IR. */
MinicCoreLowerStatus core_asm_try_target_register_contract(
    MinicCoreLowerContext *ctx, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    MinicCoreInstruction ins;
    MinicType ins_source_type = minic_type_void();
    MinicCoreInlineAsmId id;
    char *normalized = NULL;
    size_t length, i;
    const MinicSourceBackend *backend;
    MinicTargetInlineAsmContract contract;
    MinicCoreInlineAsmImmediate typed_immediates[
        MINIC_CORE_INLINE_ASM_IMMEDIATE_LIMIT] = {{0}};
    size_t typed_immediate_count = 0U;
    if (!handled) return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    if (!ctx || !ctx->target || !ctx->body || !ctx->body->program ||
        !ctx->function || !statement) return MINIC_CORE_LOWER_OK;
    backend = minic_source_backend_for_target(&ctx->target->machine);
    /* A fully target-qualified asm goto with GNU `i` inputs must take the
     * Core source-constant path below, not the earlier scalar-register
     * contract path. The latter would silently turn a symbolic relocation
     * into a runtime SSA value (x86 Linux __jump_table's static-key owner).
     * This precedence is target-independent: backends opt in through the
     * existing exact-template qualifier, and the goto lowering checks every
     * immediate before committing any Core effects. */
    if (source && source->is_goto &&
        minic_source_backend_qualify_inline_asm_goto(
            backend, ctx->target, ctx->body->program, source))
        return MINIC_CORE_LOWER_OK;
    if (!minic_source_backend_qualify_inline_asm(
            backend, ctx->target, ctx->body->program, source, &contract))
        return MINIC_CORE_LOWER_OK;
    *handled = true;
    if (!core_inline_asm_numeric_template(source, &normalized, &length)) {
        if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_TARGET_ASM_FAILURE function=%s stage=normalize\n",
                ctx->source_function ? ctx->source_function->name : "?");
        return MINIC_CORE_LOWER_ERROR;
    }
    /* The source qualifier has admitted GNU constraints, but named-operand
     * normalization can still alter a target instruction's machine effects.
     * Seal a target-owned proof only after seeing BOTH spellings; a source
     * fingerprint alone is not authority to emit a machine instruction. */
    if (!minic_source_backend_seal_normalized_inline_asm(
            backend, ctx->target, ctx->body->program, source, normalized, length,
            &contract)) {
        free(normalized);
        return MINIC_CORE_LOWER_ERROR;
    }
    /* Source-qualified GNU immediate-only instructions (e.g. Linux's
     * four-operand BUG table) must cross the language/Core boundary as
     * immutable integer or ELF symbol+byte-addend witnesses. Only the
     * exact source qualifier may opt in: existing scalar register-operand
     * machine forms are intentionally unaffected. GNU operand names remain
     * owned identities, not ad-hoc strings in a machine opcode. */
    if (contract.capture_typed_immediate_inputs) {
        if (contract.owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
            contract.target_machine_form_id == 0U ||
            source->output_count != 0U || source->input_count == 0U ||
            source->input_count > MINIC_CORE_INLINE_ASM_IMMEDIATE_LIMIT ||
            source->input_count != contract.operand_count ||
            source->inputs == NULL) {
            free(normalized);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        for (i = 0U; i < source->input_count; ++i) {
            const MinicInlineAsmOperand *input = &source->inputs[i];
            bool is_symbol = false;
            int64_t integer = 0, byte_addend = 0;
            const char *symbol = NULL;
            MinicCoreInlineAsmImmediate *typed = &typed_immediates[i];
            if (contract.kinds[i] != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                !core_inline_asm_constraint_is(input, "i") ||
                !core_inline_asm_immediate_fact(ctx, input, &is_symbol,
                    &integer, &symbol, &byte_addend)) {
                free(normalized);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            typed->kind = is_symbol
                ? MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL
                : MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
            typed->integer = integer;
            typed->symbol = (char *)symbol;
            typed->byte_addend = byte_addend;
            if (is_symbol && !minic_core_inline_asm_relocation_valid(typed)) {
                free(normalized);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
        typed_immediate_count = source->input_count;
    }
    memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.span = statement->span;
    ins_source_type = minic_type_void();
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
        if (getenv("MINIC_GNU_ASM_CONSTRAINT_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_GNU_ASM_CONSTRAINT function=%s operand=%zu "
                "kind=%d gnu_early=%d core_early=%d\n",
                ctx->source_function ? ctx->source_function->name : "?",
                i, (int)bind->kind, (int)contract.gnu_early_clobber[i],
                (int)bind->early_clobber);
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
             bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
             bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE) &&
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
        if (bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE) {
            /* A fixed machine register is a scalar location, not a C
             * addressable object. Keep the source read and physical +r
             * output role separate; the target later proves exact placement. */
            if (source_expression == NULL ||
                source_expression->kind != MINIC_EXPRESSION_FIXED_REGISTER ||
                !bind->has_fixed_register_binding) {
                free(normalized);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(ctx, op->expression, &bind->value);
        } else if (bind->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
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
                MinicType constant_ins_source_type = minic_type_void();
                (void)memset(&constant_ins, 0, sizeof(constant_ins));
                constant_ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                constant_ins.span = expr->span;
                constant_ins_source_type = expr->type;
                constant_ins.result = MINIC_CORE_VALUE_INVALID;
                constant_ins.value.integer_value = (int64_t)folded.bits;
                status = minic_core_function_append_source_value_instruction(
                    ctx->source, ctx->block_id, &constant_ins, constant_ins_source_type, &bind->value)
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
            if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL)
                (void)fprintf(stderr,
                    "CORE_TARGET_ASM_FAILURE function=%s stage=operand "
                    "index=%zu kind=%d status=%d expr_kind=%d\n",
                    ctx->source_function ? ctx->source_function->name : "?",
                    i, (int)bind->kind, (int)status,
                    source_expression ? (int)source_expression->kind : -1);
            free(normalized);
            return status;
        }
    }
    /* GNU operands are lowered left-to-right, but one operand may contain
     * control flow (for example a conditional lvalue address). That moves the
     * eventual ASM effect to a successor block. A prior local-object address
     * is a pure frame-relative value; it must be rematerialized in the effect
     * block instead of transporting its SSA ID across a CFG edge where Core
     * does not promise it is available. Do not replay loads, arithmetic,
     * volatile accesses or an arbitrary pointer expression: their evaluation
     * order and side effects are observable. */
    for (i = 0U; i < ins.value.structured_inline_asm.operand_count; ++i) {
        MinicCoreStructuredInlineAsmOperand *bind =
            &ins.value.structured_inline_asm.operands[i];
        MinicCoreInstructionId definition;
        const MinicCoreInstruction *producer;
        const MinicCoreBlock *block;
        size_t j;
        bool already_in_effect_block = false;
        if (bind->value >= ctx->function->value_count)
            continue;
        definition = ctx->function->values[bind->value].definition;
        if (definition >= ctx->function->instruction_count) {
            free(normalized);
            return MINIC_CORE_LOWER_ERROR;
        }
        producer = &ctx->function->instructions[definition];
        if (producer->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS)
            continue;
        block = &ctx->function->blocks[ctx->block_id];
        for (j = 0U; j < block->instruction_count; ++j) {
            if (block->instructions[j] == definition) {
                already_in_effect_block = true;
                break;
            }
        }
        if (!already_in_effect_block) {
            MinicCoreInstruction local_address = *producer;
            MinicType local_address_source_type = ctx->source->source_draft->instruction_types[definition];
            local_address.result = MINIC_CORE_VALUE_INVALID;
            if (!minic_core_function_append_source_value_instruction(
                    ctx->source, ctx->block_id, &local_address, local_address_source_type, &bind->value)) {
                free(normalized);
                return MINIC_CORE_LOWER_ERROR;
            }
        }
    }
    /* The observable output makes even a GNU nonvolatile CPUID semantically
     * effectful. Conservatively do not optimize or discard it. */
    /* A target-qualified +Q/+ZB memory RMW is a hardware side effect even
     * when GNU lists no explicit "memory" clobber. The existing Core verifier
     * requires its memory-readwrite effect to be represented explicitly;
     * derive that property from the TARGET-QUALIFIED operand role, not from
     * an architecture-specific template string or new Core opcode. */
    if (!minic_core_function_add_opaque_inline_asm(
            ctx->function, normalized, length, true,
            contract.effective_memory_effect, &id)) {
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
    }
    ins.value.structured_inline_asm.inline_asm_id = id;
    if (!core_asm_publish_operand_plan(
            &ins, &ctx->function->inline_asms[id], &contract))
        return MINIC_CORE_LOWER_ERROR;
    if (typed_immediate_count != 0U) {
        MinicCoreInlineAsm *stored = &ctx->function->inline_asms[id];
        for (i = 0U; i < typed_immediate_count; ++i) {
            MinicCoreInlineAsmImmediate *dest = &stored->resolved_immediates[i];
            const MinicCoreInlineAsmImmediate *typed = &typed_immediates[i];
            const MinicInlineAsmOperand *source_input = &source->inputs[i];
            *dest = *typed;
            dest->symbol = NULL;
            dest->asm_name = NULL;
            stored->resolved_immediate_count = i + 1U;
            if (typed->kind == MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL) {
                const size_t symbol_length = strlen(typed->symbol);
                dest->symbol = (char *)malloc(symbol_length + 1U);
                if (dest->symbol == NULL) return MINIC_CORE_LOWER_ERROR;
                memcpy(dest->symbol, typed->symbol, symbol_length + 1U);
            }
            if (source_input->name != NULL) {
                if (source_input->name_length == 0U ||
                    source_input->name_length > 128U)
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                dest->asm_name = (char *)malloc(source_input->name_length + 1U);
                if (dest->asm_name == NULL) return MINIC_CORE_LOWER_ERROR;
                memcpy(dest->asm_name, source_input->name,
                       source_input->name_length);
                dest->asm_name[source_input->name_length] = '\0';
            }
        }
    }
    return minic_core_function_append_source_effect_instruction(
        ctx->source, ctx->block_id, &ins, ins_source_type)
        ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}
