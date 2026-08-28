from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M61_IMMEDIATE_ONLY_INLINE_ASM'
if marker in text:
    print('M61 immediate-only inline asm already applied')
    raise SystemExit(0)

include_anchor = '#include "frontend/expression_semantics.h"\n#include <stdlib.h>\n'
if text.count(include_anchor) != 1:
    raise SystemExit(f'M61 include anchor count={text.count(include_anchor)}')
text = text.replace(
    include_anchor,
    '#include "frontend/expression_semantics.h"\n#include <inttypes.h>\n#include <stdio.h>\n#include <stdlib.h>\n',
    1,
)

helper_anchor = '''static bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand) {
    return core_inline_asm_constraint_is(operand, "=r") ||
           core_inline_asm_constraint_is(operand, "=&r");
}

'''
if text.count(helper_anchor) != 1:
    raise SystemExit(f'M61 helper anchor count={text.count(helper_anchor)}')
helpers = r'''/* M61_IMMEDIATE_ONLY_INLINE_ASM: GNU "i" operands are compile-time
   textual operands. Specialize an all-immediate asm template while Core still
   has access to the semantic program, then transport the resulting target
   text through the existing opaque-asm instruction. This keeps Core unaware
   of RISC-V BUG/WARN semantics and avoids runtime SSA values for constants. */
#define MINIC_CORE_IMMEDIATE_ASM_LIMIT 8U
#define MINIC_CORE_IMMEDIATE_TEXT_LIMIT 64U

static const MinicExpression *core_inline_asm_strip_immediate_wrappers(
    const MinicC0Program *program, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (program == NULL) {
        return NULL;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL &&
           (expression->kind == MINIC_EXPRESSION_CAST ||
            expression->kind == MINIC_EXPRESSION_BITCAST ||
            expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression = minic_c0_program_expression(program, expression->value.unary.operand);
    }
    return expression;
}

static const char *core_inline_asm_symbolic_immediate_name(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = core_inline_asm_strip_immediate_wrappers(program, expression_id);
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function;

        function = minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return NULL;
        }
        if (function->assembler_name != NULL && function->assembler_name_length != 0U) {
            return function->assembler_name;
        }
        return function->name_length == 0U ? NULL : function->name;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object == NULL || object->name_length == 0U ? NULL : object->name;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *addressed;

        addressed = core_inline_asm_strip_immediate_wrappers(
            program, expression->value.unary.operand);
        if (addressed == NULL) {
            return NULL;
        }
        if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
            const MinicGlobalObject *object;

            object = minic_c0_program_global_object(program, addressed->value.global_object_id);
            return object == NULL || object->name_length == 0U ? NULL : object->name;
        }
        if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            const MinicExpression *base;
            MinicConstValue index_value;
            bool index_is_zero;

            base = core_inline_asm_strip_immediate_wrappers(
                program, addressed->value.subscript.base);
            if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT || target == NULL ||
                !minic_const_eval_integer(
                    program, target, addressed->value.subscript.index, &index_value) ||
                !minic_const_value_is_zero(
                    program, target, &index_value, &index_is_zero) ||
                !index_is_zero) {
                return NULL;
            }
            {
                const MinicGlobalObject *object;

                object = minic_c0_program_global_object(program, base->value.global_object_id);
                return object == NULL || object->name_length == 0U ? NULL : object->name;
            }
        }
    }
    return NULL;
}

static bool core_inline_asm_immediate_text(
    const MinicCoreLowerContext *context,
    const MinicInlineAsmOperand *operand,
    char *integer_text,
    size_t integer_capacity,
    const char **text_out,
    size_t *length_out) {
    MinicConstValue constant;
    int64_t value;
    const char *symbol;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
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
        int written;

        written = snprintf(integer_text, integer_capacity, "%" PRId64, value);
        if (written < 0 || (size_t)written >= integer_capacity) {
            return false;
        }
        *text_out = integer_text;
        *length_out = (size_t)written;
        return true;
    }
    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        return false;
    }
    *text_out = symbol;
    *length_out = strlen(symbol);
    return *length_out != 0U;
}

static bool core_inline_asm_named_input_index(const MinicInlineAsm *source,
                                              const char *name,
                                              size_t name_length,
                                              size_t *input_index) {
    size_t index;

    if (source == NULL || source->inputs == NULL || name == NULL || name_length == 0U ||
        input_index == NULL) {
        return false;
    }
    for (index = 0U; index < source->input_count; ++index) {
        const MinicInlineAsmOperand *operand;

        operand = &source->inputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *input_index = index;
            return true;
        }
    }
    return false;
}

static bool core_inline_asm_specialized_length(const MinicInlineAsm *source,
                                               const size_t *replacement_lengths,
                                               size_t *specialized_length) {
    size_t cursor;
    size_t output_length;

    if (source == NULL || source->template_text == NULL || replacement_lengths == NULL ||
        specialized_length == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t replacement_index;
        size_t consumed;

        if (source->template_text[cursor] != '%') {
            if (output_length == SIZE_MAX) {
                return false;
            }
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%') {
            if (output_length == SIZE_MAX) {
                return false;
            }
            output_length += 1U;
            cursor += 2U;
            continue;
        }
        replacement_index = SIZE_MAX;
        consumed = 0U;
        if (source->template_text[cursor + 1U] >= '0' &&
            source->template_text[cursor + 1U] <= '9') {
            replacement_index = (size_t)(source->template_text[cursor + 1U] - '0');
            consumed = 2U;
        } else if (source->template_text[cursor + 1U] == '[') {
            size_t name_begin;
            size_t name_end;

            name_begin = cursor + 2U;
            name_end = name_begin;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_input_index(source,
                                                   source->template_text + name_begin,
                                                   name_end - name_begin,
                                                   &replacement_index)) {
                return false;
            }
            consumed = name_end - cursor + 1U;
        } else {
            return false;
        }
        if (replacement_index >= source->input_count ||
            output_length > SIZE_MAX - replacement_lengths[replacement_index]) {
            return false;
        }
        output_length += replacement_lengths[replacement_index];
        cursor += consumed;
    }
    *specialized_length = output_length;
    return true;
}

static bool core_inline_asm_specialize_immediates(const MinicCoreLowerContext *context,
                                                  const MinicInlineAsm *source,
                                                  char **template_out,
                                                  size_t *template_length_out) {
    char integer_text[MINIC_CORE_IMMEDIATE_ASM_LIMIT][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *replacements[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t replacement_lengths[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t input_index;
    size_t specialized_length;
    size_t cursor;
    size_t output_cursor;
    char *specialized;

    if (context == NULL || source == NULL || template_out == NULL ||
        template_length_out == NULL || source->input_count == 0U ||
        source->input_count > MINIC_CORE_IMMEDIATE_ASM_LIMIT || source->inputs == NULL) {
        return false;
    }
    for (input_index = 0U; input_index < source->input_count; ++input_index) {
        if (!core_inline_asm_immediate_text(context,
                                            &source->inputs[input_index],
                                            integer_text[input_index],
                                            sizeof(integer_text[input_index]),
                                            &replacements[input_index],
                                            &replacement_lengths[input_index])) {
            return false;
        }
    }
    if (!core_inline_asm_specialized_length(source, replacement_lengths, &specialized_length) ||
        specialized_length == SIZE_MAX) {
        return false;
    }
    specialized = (char *)malloc(specialized_length + 1U);
    if (specialized == NULL) {
        return false;
    }
    cursor = 0U;
    output_cursor = 0U;
    while (cursor < source->template_length) {
        size_t replacement_index;
        size_t consumed;

        if (source->template_text[cursor] != '%') {
            specialized[output_cursor++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor + 1U] == '%') {
            specialized[output_cursor++] = '%';
            cursor += 2U;
            continue;
        }
        replacement_index = SIZE_MAX;
        consumed = 0U;
        if (source->template_text[cursor + 1U] >= '0' &&
            source->template_text[cursor + 1U] <= '9') {
            replacement_index = (size_t)(source->template_text[cursor + 1U] - '0');
            consumed = 2U;
        } else {
            size_t name_begin;
            size_t name_end;

            name_begin = cursor + 2U;
            name_end = name_begin;
            while (source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (!core_inline_asm_named_input_index(source,
                                                   source->template_text + name_begin,
                                                   name_end - name_begin,
                                                   &replacement_index)) {
                free(specialized);
                return false;
            }
            consumed = name_end - cursor + 1U;
        }
        (void)memcpy(specialized + output_cursor,
                     replacements[replacement_index],
                     replacement_lengths[replacement_index]);
        output_cursor += replacement_lengths[replacement_index];
        cursor += consumed;
    }
    specialized[output_cursor] = '\0';
    if (output_cursor != specialized_length) {
        free(specialized);
        return false;
    }
    *template_out = specialized;
    *template_length_out = specialized_length;
    return true;
}

'''
text = text.replace(helper_anchor, helper_anchor + helpers, 1)

lower_anchor = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

'''
if text.count(lower_anchor) != 1:
    raise SystemExit(f'M61 lower anchor count={text.count(lower_anchor)}')
lower_block = r'''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        char *specialized_template;
        size_t specialized_length;

        specialized_template = NULL;
        specialized_length = 0U;
        if (core_inline_asm_specialize_immediates(
                context, source, &specialized_template, &specialized_length)) {
            bool added;

            added = specialized_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              specialized_template,
                                                              specialized_length,
                                                              source->is_volatile,
                                                              false,
                                                              &inline_asm_id);
            free(specialized_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

'''
text = text.replace(lower_anchor, lower_anchor + lower_block, 1)
path.write_text(text)
print('M61 immediate-only inline asm applied')
