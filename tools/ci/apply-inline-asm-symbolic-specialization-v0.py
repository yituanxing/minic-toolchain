#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''static bool core_inline_asm_immediate_text(\n'''
helper = r'''
static bool core_inline_asm_symbolic_address_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol_out,
    int64_t *addend_out,
    unsigned int depth) {
    const MinicExpression *expression;
    const MinicC0Program *program;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || context->source_function == NULL ||
        symbol_out == NULL || addend_out == NULL || depth > 24U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_asm_symbolic_address_depth(
            context, expression->value.unary.operand, symbol_out, addend_out, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function =
            minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return false;
        }
        *symbol_out = function->assembler_name != NULL && function->assembler_name_length != 0U
                          ? function->assembler_name
                          : function->name;
        *addend_out = 0;
        return *symbol_out != NULL && (*symbol_out)[0] != '\0';
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name == NULL || object->name_length == 0U) {
            return false;
        }
        *symbol_out = object->name;
        *addend_out = 0;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (!context->source_function->is_integer_specialization ||
            local_id < context->source_function->local_begin) {
            return false;
        }
        parameter_index = local_id - context->source_function->local_begin;
        if (parameter_index >= context->source_function->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !context->source_function->specialization_symbolic_known[parameter_index] ||
            context->source_function->specialization_symbolic_expression[parameter_index] ==
                MINIC_EXPRESSION_INVALID) {
            return false;
        }
        return core_inline_asm_symbolic_address_depth(
            context,
            context->source_function->specialization_symbolic_expression[parameter_index],
            symbol_out,
            addend_out,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF ||
        expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        return core_inline_asm_symbolic_address_depth(
            context, expression->value.unary.operand, symbol_out, addend_out, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;
        size_t field_offset;
        int64_t base_addend;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        if (record == NULL ||
            !minic_data_layout_record_field_offset(minic_target_info_data_layout(context->target),
                                                   program,
                                                   record,
                                                   expression->value.member.field_index,
                                                   &field_offset) ||
            field_offset > (size_t)INT64_MAX ||
            !core_inline_asm_symbolic_address_depth(context,
                                                    expression->value.member.base,
                                                    symbol_out,
                                                    &base_addend,
                                                    depth + 1U) ||
            base_addend > INT64_MAX - (int64_t)field_offset) {
            return false;
        }
        *addend_out = base_addend + (int64_t)field_offset;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base_expression;
        MinicConstValue index_value;
        int64_t index;
        size_t element_size;
        size_t element_alignment;
        int64_t base_addend;
        int64_t scaled;

        base_expression = minic_c0_program_expression(program, expression->value.subscript.base);
        if (base_expression == NULL ||
            (!minic_const_eval_integer(program,
                                       context->target,
                                       expression->value.subscript.index,
                                       &index_value) &&
             !core_inline_asm_local_integer_fact(
                 context, expression->value.subscript.index, &index_value)) ||
            !minic_const_value_as_int64(program, context->target, &index_value, &index) ||
            !minic_data_layout_type(minic_target_info_data_layout(context->target),
                                    program,
                                    expression->type,
                                    &element_size,
                                    &element_alignment) ||
            element_size > (size_t)INT64_MAX ||
            !core_inline_asm_symbolic_address_depth(context,
                                                    expression->value.subscript.base,
                                                    symbol_out,
                                                    &base_addend,
                                                    depth + 1U)) {
            return false;
        }
        (void)element_alignment;
        if (index != 0 &&
            ((index > 0 && index > INT64_MAX / (int64_t)element_size) ||
             (index < 0 && index < INT64_MIN / (int64_t)element_size))) {
            return false;
        }
        scaled = index * (int64_t)element_size;
        if ((scaled > 0 && base_addend > INT64_MAX - scaled) ||
            (scaled < 0 && base_addend < INT64_MIN - scaled)) {
            return false;
        }
        *addend_out = base_addend + scaled;
        return true;
    }
    return false;
}

static bool core_inline_asm_symbolic_specialization_text(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    char *buffer,
    size_t capacity,
    const char **text_out,
    size_t *length_out) {
    const char *symbol;
    int64_t addend;
    int written;

    if (buffer == NULL || capacity == 0U || text_out == NULL || length_out == NULL ||
        !core_inline_asm_symbolic_address_depth(
            context, expression_id, &symbol, &addend, 0U)) {
        return false;
    }
    if (addend == 0) {
        *text_out = symbol;
        *length_out = strlen(symbol);
        return *length_out != 0U;
    }
    written = addend > 0
                  ? snprintf(buffer, capacity, "%s+%" PRId64, symbol, addend)
                  : snprintf(buffer, capacity, "%s%" PRId64, symbol, addend);
    if (written < 0 || (size_t)written >= capacity) {
        return false;
    }
    *text_out = buffer;
    *length_out = (size_t)written;
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit("expected one immediate-text anchor")
text = text.replace(anchor, helper + anchor, 1)

old = '''    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        return false;
    }
    *text_out = symbol;
    *length_out = strlen(symbol);
    return *length_out != 0U;
'''
new = '''    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol != NULL) {
        *text_out = symbol;
        *length_out = strlen(symbol);
        return *length_out != 0U;
    }
    return core_inline_asm_symbolic_specialization_text(context,
                                                        operand->expression,
                                                        integer_text,
                                                        integer_capacity,
                                                        text_out,
                                                        length_out);
'''
if text.count(old) != 1:
    raise SystemExit("expected one symbolic immediate tail")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_ASM_SYMBOLIC_SPECIALIZATION_V0=APPLIED")
