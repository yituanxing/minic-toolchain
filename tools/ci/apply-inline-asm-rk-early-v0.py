#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

function_anchor = '''MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
'''
helpers = r'''static bool core_inline_asm_expression_is_local_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicLocalId local_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 8U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        return expression->value.local_id == local_id;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return core_inline_asm_expression_is_local_depth(
            context, expression->value.unary.operand, local_id, depth + 1U);
    }
    return false;
}

static bool core_inline_asm_expression_is_local(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicLocalId local_id) {
    return core_inline_asm_expression_is_local_depth(context, expression_id, local_id, 0U);
}

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
    MinicCoreInstructionId ids[7];
    const MinicCoreInstruction *constant;
    const MinicCoreInstruction *temp_address_0;
    const MinicCoreInstruction *temp_store;
    const MinicCoreInstruction *local_address;
    const MinicCoreInstruction *temp_address_1;
    const MinicCoreInstruction *temp_load;
    const MinicCoreInstruction *local_store;
    size_t index;

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
    if (block->terminated || block->instruction_count < 7U) {
        return false;
    }
    for (index = 0U; index < 7U; ++index) {
        ids[index] = block->instructions[block->instruction_count - 7U + index];
        if (ids[index] >= context->function->instruction_count) {
            return false;
        }
    }
    constant = &context->function->instructions[ids[0]];
    temp_address_0 = &context->function->instructions[ids[1]];
    temp_store = &context->function->instructions[ids[2]];
    local_address = &context->function->instructions[ids[3]];
    temp_address_1 = &context->function->instructions[ids[4]];
    temp_load = &context->function->instructions[ids[5]];
    local_store = &context->function->instructions[ids[6]];

    if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        constant->result == MINIC_CORE_VALUE_INVALID ||
        temp_address_0->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
        temp_address_0->result == MINIC_CORE_VALUE_INVALID ||
        temp_address_0->value.object_id == local_object ||
        temp_store->kind != MINIC_CORE_INSTRUCTION_STORE ||
        temp_store->value.store.is_volatile ||
        temp_store->value.store.address != temp_address_0->result ||
        temp_store->value.store.stored_value != constant->result ||
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
        return false;
    }
    /* All seven instructions must belong to the same source assignment. */
    for (index = 1U; index < 7U; ++index) {
        const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
        if (candidate->span.begin.line != constant->span.begin.line) {
            return false;
        }
    }
    block->instruction_count -= 7U;
    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "RK_EARLY_REWIND function=%s local=%zu object=%u removed=7\\n",
                      context->source_function->name != NULL ? context->source_function->name : "?",
                      (size_t)local_id,
                      (unsigned int)local_object);
    }
    return true;
}

'''
if text.count(function_anchor) != 1:
    raise SystemExit("expected one inline asm lowering function anchor")
text = text.replace(function_anchor, helpers + function_anchor, 1)

anchor = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
'''
replacement = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* Linux RISC-V CSR helpers use one write-only register output plus an rK
       input and a memory clobber.  M126A below intentionally accepts rK as a
       scalar-register alternative, but doing so first materializes a constant
       input (and the output address) in the frame.  Prefer the immediate
       alternative before generic structured lowering whenever every input can
       be baked into the template.  If specialization fails, M126A retains the
       original register fallback semantics. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
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
                return MINIC_CORE_LOWER_ERROR;
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
                                  "RK_EARLY_TAIL function=%s local=%zu block=%u block_insts=%zu function_insts=%zu values=%zu objects=%zu same_local_rk=%d\\n",
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
                        (void)fprintf(stderr, "\\n");
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
                        return MINIC_CORE_LOWER_ERROR;
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
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) !=
                    MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
        free(specialized_template);
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
'''
if text.count(anchor) != 1:
    raise SystemExit("expected one pre-M126A insertion anchor")
text = text.replace(anchor, replacement, 1)
p.write_text(text)
print("MINIC_INLINE_ASM_RK_EARLY_V0=APPLIED")
