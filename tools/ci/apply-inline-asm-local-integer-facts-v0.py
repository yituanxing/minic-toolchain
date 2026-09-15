#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''static bool core_inline_asm_immediate_text(\n'''
helper = r'''
static bool core_inline_asm_local_integer_fact_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || context->source_function == NULL || value == NULL ||
        depth > 16U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t local_index;
        const MinicCoreLocalIntegerConstant *fact;

        if (context->local_integer_constants == NULL ||
            local_id < context->source_function->local_begin) {
            return false;
        }
        local_index = local_id - context->source_function->local_begin;
        if (local_index >= context->source_function->local_count) {
            return false;
        }
        fact = &context->local_integer_constants[local_index];
        if (!fact->known || fact->escaped) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &fact->value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        MinicConstValue operand_value;
        if (!core_inline_asm_local_integer_fact_depth(context,
                                                      expression->value.unary.operand,
                                                      &operand_value,
                                                      depth + 1U)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    return false;
}

static bool core_inline_asm_local_integer_fact(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicConstValue *value) {
    return core_inline_asm_local_integer_fact_depth(context, expression_id, value, 0U);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("expected one immediate resolver anchor")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || operand == NULL || integer_text == NULL ||
        integer_capacity == 0U || text_out == NULL || length_out == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (!core_inline_asm_constraint_is(operand, "i") &&
         !core_inline_asm_constraint_is(operand, "I"))) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, operand->expression, &constant) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value)) {
'''
new = '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || operand == NULL || integer_text == NULL ||
        integer_capacity == 0U || text_out == NULL || length_out == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (!core_inline_asm_constraint_is(operand, "i") &&
         !core_inline_asm_constraint_is(operand, "I") &&
         !core_inline_asm_constraint_is(operand, "rK"))) {
        return false;
    }
    if ((minic_const_eval_integer(
             context->body->program, context->target, operand->expression, &constant) ||
         core_inline_asm_local_integer_fact(context, operand->expression, &constant)) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, &value) &&
        (!core_inline_asm_constraint_is(operand, "rK") ||
         (value >= 0 && value <= 31))) {
'''
if text.count(old) != 1:
    raise SystemExit("expected one immediate integer const-eval block")
text = text.replace(old, new, 1)

symbol_old = '''    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
'''
symbol_new = '''    if (core_inline_asm_constraint_is(operand, "rK")) {
        return false;
    }
    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
'''
if text.count(symbol_old) != 1:
    raise SystemExit("expected one symbolic immediate fallback")
text = text.replace(symbol_old, symbol_new, 1)

batch_old = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 0U && !source->has_memory_clobber) {
'''
batch_new = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
if text.count(batch_old) != 1:
    raise SystemExit("expected one register-output immediate specialization gate")
text = text.replace(batch_old, batch_new, 1)

batch_add_old = '''                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id)) {
'''
batch_add_new = '''                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
'''
if text.count(batch_add_old) != 1:
    raise SystemExit("expected one specialized register-output asm add")
text = text.replace(batch_add_old, batch_add_new, 1)

# The generic one-output/one-input structured path appears before BATCH_I and
# accepts both "r" and "rK".  Without this fast path it eagerly lowers an rK
# constant/local into Core SSA (and therefore a frame slot) before the immediate
# specialization below can ever see it.  Prefer the immediate alternative when
# it is legal; preserve the old register lowering as the fallback for rK values
# that cannot be encoded as a CSR immediate.
generic_anchor = '''            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
'''
generic_replacement = '''            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                if (core_inline_asm_constraint_is(input, "rK")) {
                    char *specialized_template = NULL;
                    size_t specialized_length = 0U;
                    if (core_inline_asm_specialize_register_output_immediates(
                            context, source, &specialized_template, &specialized_length)) {
                        bool added = minic_core_function_add_opaque_inline_asm(
                            context->function,
                            specialized_template,
                            specialized_length,
                            source->is_volatile,
                            source->has_memory_clobber,
                            &inline_asm_id);
                        free(specialized_template);
                        if (!added) {
                            return MINIC_CORE_LOWER_ERROR;
                        }
                        (void)memset(&instruction, 0, sizeof(instruction));
                        instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                        instruction.span = statement->span;
                        instruction.type = output_type;
                        instruction.result = MINIC_CORE_VALUE_INVALID;
                        instruction.value.inline_asm_id = inline_asm_id;
                        if (!minic_core_function_append_value_instruction(
                                context->function,
                                context->block_id,
                                &instruction,
                                &output_value)) {
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
                    free(specialized_template);
                }
                status = lower_expression(context, input->expression, &input_value);
'''
if text.count(generic_anchor) != 1:
    raise SystemExit("expected one generic register-output/input lowering anchor")
text = text.replace(generic_anchor, generic_replacement, 1)

p.write_text(text)
print("MINIC_INLINE_ASM_LOCAL_INTEGER_FACTS_V0=APPLIED")
