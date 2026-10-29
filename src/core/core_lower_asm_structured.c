#include "core/core_lower_asm_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "target/data_layout.h"
#include "target/backend.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GNU asm structured roles, fixed register contracts and CSR effect paths.
 * These handlers own Core mutations only after successful ordered preflight. */

/* The ordinary scalar assignment lowering is intentionally conservative:
   constant -> temporary object -> target object.  For Linux's csr_swap shape,
   a known local is used only as an rK immediate and is then overwritten by the
   asm output.  Once immediate specialization has consumed that fact, the
   immediately preceding assignment materialization is dead.  Remove it from
   the current block only when the exact seven-instruction tail is present and
   the target object is exactly the same output local.  Global instruction,
   value and object tables remain monotonic; unreachable tail entries are kept
   as harmless tombstones, preserving every global ID invariant. */
static bool core_inline_asm_rewind_dead_local_assignment(
    MinicCoreLowerContext *context,
    MinicLocalId local_id) {
    MinicCoreBlock *block;
    size_t local_index;
    MinicCoreObjectId local_object;
    const size_t candidate_counts[2] = {8U, 7U};
    size_t candidate_index;

    if (context == NULL || context->function == NULL || context->source_function == NULL ||
        context->local_objects == NULL || context->block_id >= context->function->block_count ||
        local_id < context->source_function->local_begin) {
        return false;
    }
    local_index = local_id - context->source_function->local_begin;
    if (local_index >= context->source_function->local_count) {
        return false;
    }
    local_object = context->local_objects[local_index];
    if (local_object == MINIC_CORE_OBJECT_INVALID) {
        return false;
    }
    block = &context->function->blocks[context->block_id];
    if (block->has_terminator) {
        return false;
    }

    /* Scalar local initialization is normally materialized through a temporary
       object.  When the source integer type differs from the local type, Core
       retains one INTEGER_CONVERSION between the constant and temporary store.
       Linux's csr_swap(CSR_SATP, 0ULL) has exactly that shape: unsigned long
       __v = (unsigned long)0ULL.  Accept both the converted eight-instruction
       tail and the same-type seven-instruction tail.  This remains fail-closed:
       the candidate must be both the block tail and exact global instruction /
       value tail before the transaction is rolled back. */
    for (candidate_index = 0U; candidate_index < 2U; ++candidate_index) {
        size_t remove_count = candidate_counts[candidate_index];
        MinicCoreInstructionId ids[8];
        size_t offset = remove_count == 8U ? 1U : 0U;
        const MinicCoreInstruction *constant;
        const MinicCoreInstruction *conversion = NULL;
        const MinicCoreInstruction *temp_address_0;
        const MinicCoreInstruction *temp_store;
        const MinicCoreInstruction *local_address;
        const MinicCoreInstruction *temp_address_1;
        const MinicCoreInstruction *temp_load;
        const MinicCoreInstruction *local_store;
        MinicCoreValueId materialized_value;
        size_t index;
        size_t global_begin;
        size_t produced_values = 0U;
        size_t expected_value;
        bool shape_ok = true;

        if (block->instruction_count < remove_count ||
            context->function->instruction_count < remove_count) {
            continue;
        }
        for (index = 0U; index < remove_count; ++index) {
            ids[index] = block->instructions[block->instruction_count - remove_count + index];
            if (ids[index] >= context->function->instruction_count) {
                shape_ok = false;
                break;
            }
        }
        if (!shape_ok) {
            continue;
        }
        constant = &context->function->instructions[ids[0]];
        if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            constant->result == MINIC_CORE_VALUE_INVALID) {
            continue;
        }
        materialized_value = constant->result;
        if (remove_count == 8U) {
            conversion = &context->function->instructions[ids[1]];
            if (conversion->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
                conversion->result == MINIC_CORE_VALUE_INVALID ||
                conversion->value.operand != constant->result) {
                continue;
            }
            materialized_value = conversion->result;
        }
        temp_address_0 = &context->function->instructions[ids[1U + offset]];
        temp_store = &context->function->instructions[ids[2U + offset]];
        local_address = &context->function->instructions[ids[3U + offset]];
        temp_address_1 = &context->function->instructions[ids[4U + offset]];
        temp_load = &context->function->instructions[ids[5U + offset]];
        local_store = &context->function->instructions[ids[6U + offset]];

        if (temp_address_0->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            temp_address_0->result == MINIC_CORE_VALUE_INVALID ||
            temp_address_0->value.object_id == local_object ||
            temp_store->kind != MINIC_CORE_INSTRUCTION_STORE ||
            temp_store->value.store.is_volatile ||
            temp_store->value.store.address != temp_address_0->result ||
            temp_store->value.store.stored_value != materialized_value ||
            local_address->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            local_address->result == MINIC_CORE_VALUE_INVALID ||
            local_address->value.object_id != local_object ||
            temp_address_1->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            temp_address_1->result == MINIC_CORE_VALUE_INVALID ||
            temp_address_1->value.object_id != temp_address_0->value.object_id ||
            temp_load->kind != MINIC_CORE_INSTRUCTION_LOAD ||
            temp_load->result == MINIC_CORE_VALUE_INVALID ||
            temp_load->value.load.is_volatile ||
            temp_load->value.load.address != temp_address_1->result ||
            local_store->kind != MINIC_CORE_INSTRUCTION_STORE ||
            local_store->value.store.is_volatile ||
            local_store->value.store.address != local_address->result ||
            local_store->value.store.stored_value != temp_load->result) {
            continue;
        }
        for (index = 1U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
            if (candidate->span.begin.line != constant->span.begin.line) {
                shape_ok = false;
                break;
            }
        }
        if (!shape_ok) {
            continue;
        }

        global_begin = context->function->instruction_count - remove_count;
        for (index = 0U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate;
            if ((size_t)ids[index] != global_begin + index) {
                shape_ok = false;
                break;
            }
            candidate = &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                produced_values += 1U;
            }
        }
        if (!shape_ok || produced_values > context->function->value_count) {
            continue;
        }
        expected_value = context->function->value_count - produced_values;
        for (index = 0U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                if ((size_t)candidate->result != expected_value ||
                    context->function->values[candidate->result].definition != ids[index]) {
                    shape_ok = false;
                    break;
                }
                expected_value += 1U;
            }
        }
        if (!shape_ok || expected_value != context->function->value_count) {
            continue;
        }

        block->instruction_count -= remove_count;
        context->function->instruction_count -= remove_count;
        context->function->value_count -= produced_values;
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RK_EARLY_GLOBAL_ROLLBACK function=%s removed_insts=%zu removed_values=%zu converted=%d\n",
                          context->source_function->name != NULL ? context->source_function->name : "?",
                          remove_count,
                          produced_values,
                          conversion != NULL ? 1 : 0);
            (void)fprintf(stderr,
                          "RK_EARLY_REWIND function=%s local=%zu object=%u removed=%zu\n",
                          context->source_function->name != NULL ? context->source_function->name : "?",
                          (size_t)local_id,
                          (unsigned int)local_object,
                          remove_count);
        }
        return true;
    }
    return false;
}

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_immediate_output_csr(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    /* Linux RISC-V CSR helpers use one write-only register output plus an rK
       input and a memory clobber.  M126A below intentionally accepts rK as a
       scalar-register alternative, but doing so first materializes a constant
       input (and the output address) in the frame.  Prefer the immediate
       alternative before generic structured lowering whenever every input can
       be baked into the template.  If specialization fails, M126A retains the
       original register fallback semantics. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U &&
        core_inline_asm_constraint_is(&source->inputs[0], "rK") &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;
        bool register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));

        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY && register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                free(specialized_template);
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                bool same_local_rk =
                    source->input_count == 1U &&
                    core_inline_asm_constraint_is(&source->inputs[0], "rK") &&
                    core_inline_asm_expression_is_local(
                        context,
                        source->inputs[0].expression,
                        output_expression->value.local_id);
                if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL &&
                    context->block_id < context->function->block_count) {
                    const MinicCoreBlock *trace_block =
                        &context->function->blocks[context->block_id];
                    size_t trace_begin = trace_block->instruction_count > 20U
                                             ? trace_block->instruction_count - 20U
                                             : 0U;
                    size_t trace_index;
                    (void)fprintf(stderr,
                                  "RK_EARLY_TAIL function=%s local=%zu block=%u block_insts=%zu function_insts=%zu values=%zu objects=%zu same_local_rk=%d\n",
                                  context->source_function != NULL &&
                                          context->source_function->name != NULL
                                      ? context->source_function->name
                                      : "?",
                                  (size_t)output_expression->value.local_id,
                                  (unsigned int)context->block_id,
                                  trace_block->instruction_count,
                                  context->function->instruction_count,
                                  context->function->value_count,
                                  context->function->object_count,
                                  same_local_rk ? 1 : 0);
                    for (trace_index = trace_begin;
                         trace_index < trace_block->instruction_count;
                         ++trace_index) {
                        MinicCoreInstructionId trace_id =
                            trace_block->instructions[trace_index];
                        const MinicCoreInstruction *trace_instruction =
                            trace_id < context->function->instruction_count
                                ? &context->function->instructions[trace_id]
                                : NULL;
                        if (trace_instruction == NULL) {
                            continue;
                        }
                        (void)fprintf(stderr,
                                      "RK_EARLY_INST pos=%zu id=%u kind=%d result=%u line=%zu col=%zu",
                                      trace_index,
                                      (unsigned int)trace_id,
                                      (int)trace_instruction->kind,
                                      (unsigned int)trace_instruction->result,
                                      trace_instruction->span.begin.line,
                                      trace_instruction->span.begin.column);
                        if (trace_instruction->kind == MINIC_CORE_INSTRUCTION_STORE) {
                            (void)fprintf(stderr,
                                          " store_addr=%u store_value=%u volatile=%d",
                                          (unsigned int)trace_instruction->value.store.address,
                                          (unsigned int)trace_instruction->value.store.stored_value,
                                          trace_instruction->value.store.is_volatile ? 1 : 0);
                        } else if (trace_instruction->kind ==
                                   MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS) {
                            (void)fprintf(stderr,
                                          " object=%u",
                                          (unsigned int)trace_instruction->value.object_id);
                        } else if (trace_instruction->kind == MINIC_CORE_INSTRUCTION_LOAD) {
                            (void)fprintf(stderr,
                                          " load_addr=%u volatile=%d",
                                          (unsigned int)trace_instruction->value.load.address,
                                          trace_instruction->value.load.is_volatile ? 1 : 0);
                        }
                        (void)fprintf(stderr, "\n");
                    }
                }
                {
                    bool added = minic_core_function_add_opaque_inline_asm(
                        context->function,
                        specialized_template,
                        specialized_length,
                        source->is_volatile,
                        source->has_memory_clobber,
                        &inline_asm_id);
                    free(specialized_template);
                    specialized_template = NULL;
                    if (!added) {
                        if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                            (void)fprintf(stderr,
                                          "ASM_OUTPUT_STAGE function=%s stage=add-opaque status=ERROR\n",
                                          context->source_function != NULL &&
                                                  context->source_function->name != NULL
                                              ? context->source_function->name
                                              : "?");
                        }
                        { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                    }
                }
                if (same_local_rk) {
                    (void)core_inline_asm_rewind_dead_local_assignment(
                        context, output_expression->value.local_id);
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.inline_asm_id = inline_asm_id;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=append-output-value status=ERROR "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                {
                    MinicCoreLowerStatus address_status =
                        lower_address(context, output->expression, &address_id);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=lower-output-address status=%d "
                                      "address=%u block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      (int)address_status,
                                      (unsigned int)address_id,
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    if (address_status != MINIC_CORE_LOWER_OK) {
                        { *handled = true; return address_status; }
                    }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
            }
        }
        free(specialized_template);
    }

    return MINIC_CORE_LOWER_OK;
}

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_structured_roles_and_asm_goto(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    /* Target-qualified asm-goto is a genuine CFG effect. The same path owns
     * the one-label Linux static-key table and the two-label CPU-feature
     * alternative. Preserve the complete source template, resolve all `i`
     * operands into *link-time* text before committing, and transport C
     * label destinations as Core blocks. Neither form may silently lower
     * a GNU immediate into a runtime SSA register. */
    if (minic_backend_qualify_inline_asm_goto(
            minic_backend_for_target(context->target),
            context->body->program, source)) {
        char immediate_buffers[3][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
        const char *resolved[3] = {NULL, NULL, NULL};
        size_t lengths[3] = {0U, 0U, 0U};
        MinicCoreBlockId targets[2] = {
            MINIC_CORE_BLOCK_INVALID, MINIC_CORE_BLOCK_INVALID
        };
        MinicCoreInlineAsm *stored;
        MinicCoreInstruction effect;
        MinicCoreLowerStatus status;
        size_t i;
        *handled = true;
        if (source->input_count == 0U || source->input_count > 3U ||
            source->label_count == 0U || source->label_count > 2U ||
            source->inputs == NULL || source->labels == NULL)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        for (i = 0U; i < source->input_count; ++i) {
            if (!core_inline_asm_immediate_text(
                    context, &source->inputs[i], immediate_buffers[i],
                    sizeof(immediate_buffers[i]), &resolved[i], &lengths[i]) ||
                !resolved[i] || lengths[i] == 0U || lengths[i] > 128U) {
                if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "CORE_ASM_GOTO_IMMEDIATE_FAILURE function=%s "
                        "operand=%zu expression=%zu symbolic=%d integer=%d\n",
                        context->source_function && context->source_function->name
                            ? context->source_function->name : "?",
                        i, (size_t)source->inputs[i].expression,
                        context->source_function &&
                            context->source_function->is_integer_specialization &&
                            context->source_function->specialization_symbolic_known[i] ? 1 : 0,
                        context->source_function &&
                            context->source_function->is_integer_specialization &&
                            context->source_function->specialization_integer_known[i] ? 1 : 0);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
        for (i = 0U; i < source->label_count; ++i) {
            status = ensure_statement_block(
                context, source->labels[i].target_statement, &targets[i]);
            if (status != MINIC_CORE_LOWER_OK) {
                if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "CORE_ASM_GOTO_BLOCK_FAILURE function=%s label=%zu status=%d\n",
                        context->source_function && context->source_function->name
                            ? context->source_function->name : "?",
                        i, (int)status);
                return status;
            }
        }
        if (!minic_core_function_add_opaque_inline_asm(
                context->function, source->template_text,
                source->template_length, true, false, &inline_asm_id))
            return MINIC_CORE_LOWER_ERROR;
        stored = &context->function->inline_asms[inline_asm_id];
        stored->is_goto = true;
        stored->source_inline_asm_id = SIZE_MAX;
        stored->goto_target = targets[0];
        stored->goto_second_target = targets[1];
        for (i = 0U; i < source->input_count; ++i) {
            char *owned = (char *)malloc(lengths[i] + 1U);
            if (!owned) return MINIC_CORE_LOWER_ERROR;
            (void)memcpy(owned, resolved[i], lengths[i]);
            owned[lengths[i]] = '\0';
            stored->resolved_immediates[i] = owned;
            stored->resolved_immediate_count = i + 1U;
        }
        (void)memset(&effect, 0, sizeof(effect));
        effect.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        effect.span = statement->span;
        effect.type = minic_type_void();
        effect.result = MINIC_CORE_VALUE_INVALID;
        effect.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
            context->function, context->block_id, &effect)
            ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
       extended asm. Preflight is deliberately side-effect free: an asm that
       ultimately belongs to an older/specialized path must not leave partial
       Core values, objects, or instructions behind. Only after every operand
       role and the numeric template are proven do we materialize operands. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        /* A zero-byte GNU template must use the existing empty effect
         * semantics, not StructuredInlineAsm (Core verifier and machine
         * emitters correctly require a real hardware instruction).
         * This is the common source of Linux BPF empty volatile +r
         * lowering failures: early general dispatch captured the form. */
        source->template_length != 0U && source->label_count == 0U &&
        source->output_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
        source->input_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT - source->output_count &&
        source->output_count + source->input_count != 0U &&
        (source->output_count == 0U || source->outputs != NULL) &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->clobber_count == source->register_clobber_count +
                                     (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;
        bool implicit_memory_effect = false;

        (void)memset(&structured, 0, sizeof(structured));
        structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        structured.span = statement->span;
        structured.type = minic_type_void();
        structured.result = MINIC_CORE_VALUE_INVALID;
        structured.value.structured_inline_asm.operand_count =
            source->output_count + source->input_count;

        /* Phase 1: pure classification only. No Core mutation is permitted. */
        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[output_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            binding->operand_index = output_index;
            binding->early_clobber =
                operand->constraint_text != NULL &&
                memchr(operand->constraint_text, '&', operand->constraint_length) != NULL;
            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                (core_inline_asm_constraint_is(operand, "=r") ||
                 core_inline_asm_constraint_is(operand, "=&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+r") ||
                        core_inline_asm_constraint_is(operand, "+&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(operand, "=m")) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                binding->early_clobber = false;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+m") ||
                        core_inline_asm_constraint_is(operand, "+A"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                binding->early_clobber = false;
                /* The `+m` constraint itself declares an in-place memory
                 * read/write effect, even without a separate GNU "memory"
                 * clobber. Preserve that fact in the Core effect summary so
                 * verification and reordering agree with GNU semantics. */
                implicit_memory_effect = true;
            } else {
                supported_shape = false;
                break;
            }
            if ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                core_inline_asm_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_id)) {
                binding->fixed_register_binding_id = fixed_binding_id;
                binding->has_fixed_register_binding = true;
            }
        }

        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            size_t operand_index = source->output_count + input_index;
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[operand_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || expression == NULL) {
                supported_shape = false;
                break;
            }
            binding->operand_index = operand_index;
            if (core_inline_asm_constraint_is(operand, "m")) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            } else if (core_inline_asm_constraint_is(operand, "r") ||
                       core_inline_asm_constraint_is(operand, "rJ") ||
                       core_inline_asm_constraint_is(operand, "Jr") ||
                       core_inline_asm_constraint_is(operand, "rK")) {
                if (!core_scalar_expression_value_type(context->body, expression, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                if (core_inline_asm_fixed_binding_id(
                        context->body->program, expression, &fixed_binding_id)) {
                    binding->fixed_register_binding_id = fixed_binding_id;
                    binding->has_fixed_register_binding = true;
                }
            } else {
                supported_shape = false;
                break;
            }
        }

        /* Template normalization is also part of preflight. A failed probe
           falls through with the Core function exactly unchanged. */
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            size_t binding_index;
            size_t clobber_index;

            /* Phase 2: commit operand materialization. Any failure from here
               aborts this function lowering, so partial state is destroyed by
               minic_core_lower_function rather than leaking into another path. */
            for (binding_index = 0U;
                 binding_index < structured.value.structured_inline_asm.operand_count;
                 ++binding_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[binding_index];

                if (binding->has_fixed_register_binding) {
                    size_t core_binding_id;

                    if (!core_import_fixed_register_binding(
                            context, binding->fixed_register_binding_id, &core_binding_id)) {
                        free(numeric_template);
                        { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                    }
                    binding->fixed_register_binding_id = core_binding_id;
                }
            }
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    { *handled = true; return status; }
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                size_t operand_index = source->output_count + input_index;
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[operand_index];
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
                    status = lower_address(
                        context, source->inputs[input_index].expression, &binding->value);
                } else {
                    status = lower_expression(
                        context, source->inputs[input_index].expression, &binding->value);
                }
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    { *handled = true; return status; }
                }
            }

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                            numeric_template,
                                                            numeric_template_length,
                                                            true,
                                                            source->has_memory_clobber ||
                                                                implicit_memory_effect,
                                                            &inline_asm_id)) {
                free(numeric_template);
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            free(numeric_template);
            numeric_template = NULL;
            for (clobber_index = 0U; clobber_index < source->register_clobber_count;
                 ++clobber_index) {
                const MinicInlineAsmRegisterClobber *clobber =
                    &source->register_clobbers[clobber_index];
                if (clobber->name == NULL || clobber->name_length == 0U ||
                    !minic_core_function_add_inline_asm_register_clobber(
                        context->function,
                        inline_asm_id,
                        clobber->name,
                        clobber->name_length)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
            }
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
        free(numeric_template);
    }

    if (core_inline_asm_single_label_goto_supported(context, source)) {
        char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
        const char *resolved_text;
        size_t resolved_length;
        char *numeric_template;
        size_t numeric_template_length;
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;
        bool input_resolved;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        input_resolved = core_inline_asm_immediate_text(context,
                                                        &source->inputs[0],
                                                        immediate_text,
                                                        sizeof(immediate_text),
                                                        &resolved_text,
                                                        &resolved_length);
        numeric_template = NULL;
        numeric_template_length = 0U;
        if (!core_inline_asm_single_label_goto_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (input_resolved) {
            char *baked_template = NULL;
            size_t baked_length = 0U;

            if (!core_inline_asm_single_label_goto_bake_input(numeric_template,
                                                               numeric_template_length,
                                                               resolved_text,
                                                               resolved_length,
                                                               &baked_template,
                                                               &baked_length)) {
                free(numeric_template);
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            free(numeric_template);
            numeric_template = baked_template;
            numeric_template_length = baked_length;
        }
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       numeric_template,
                                                       numeric_template_length,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            free(numeric_template);
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        free(numeric_template);
        stored = &context->function->inline_asms[inline_asm_id];
        stored->is_goto = true;
        stored->source_inline_asm_id = input_resolved
                                           ? SIZE_MAX
                                           : (size_t)statement->inline_asm_id;
        stored->goto_target = target_block;

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        { *handled = true; return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }

    return MINIC_CORE_LOWER_OK;
}

