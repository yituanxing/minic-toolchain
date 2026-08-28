#include "core/core_lower_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool core_inline_asm_constraint_is(const MinicInlineAsmOperand *operand,
                                              const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand) {
    return core_inline_asm_constraint_is(operand, "=r") ||
           core_inline_asm_constraint_is(operand, "=&r");
}

/* M105_FIXED_REGISTER_STRUCTURED_ASM: preflight locates a frontend binding
   without mutating Core. The commit phase imports register spelling/type into
   Core-owned opaque metadata before the instruction is appended. */
static bool core_inline_asm_local_fixed_binding_id(const MinicC0Program *program,
                                                   const MinicExpression *expression,
                                                   size_t *binding_id) {
    const MinicFixedRegisterBinding *binding;
    size_t index;

    if (program == NULL || expression == NULL || binding_id == NULL ||
        expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    binding = minic_c0_program_local_fixed_register_binding(program, expression->value.local_id);
    if (binding == NULL) {
        return false;
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        if (&program->fixed_register_bindings[index] == binding) {
            *binding_id = index;
            return true;
        }
    }
    return false;
}

/* M61_IMMEDIATE_ONLY_INLINE_ASM: GNU "i" operands are compile-time
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

/* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: a value-producing
   asm may have one runtime register output plus compile-time-only immediate
   inputs. Preserve %0 for the output and bake %1..%9 into target text using
   the existing i/I constant/symbol resolver. No target instruction meaning is
   introduced into Core. */
static bool core_inline_asm_specialize_register_output_immediates(
    const MinicCoreLowerContext *context,
    const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out) {
    char integer_text[MINIC_CORE_IMMEDIATE_ASM_LIMIT][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *replacements[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t replacement_lengths[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t input_index;
    size_t cursor;
    size_t output_length;
    size_t output_cursor;
    char *specialized;

    if (context == NULL || source == NULL || template_out == NULL ||
        template_length_out == NULL || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 1U ||
        source->input_count == 0U || source->input_count > 9U ||
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

    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) return false;
        if (source->template_text[cursor + 1U] == '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] < '0' ||
            source->template_text[cursor + 1U] > '9') {
            return false;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            if (output_length > SIZE_MAX - 2U) return false;
            output_length += 2U;
        } else {
            input_index = operand_index - 1U;
            if (input_index >= source->input_count ||
                output_length > SIZE_MAX - replacement_lengths[input_index]) {
                return false;
            }
            output_length += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    if (output_length == SIZE_MAX) return false;
    specialized = (char *)malloc(output_length + 1U);
    if (specialized == NULL) return false;

    cursor = 0U;
    output_cursor = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            specialized[output_cursor++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor + 1U] == '%') {
            specialized[output_cursor++] = '%';
            cursor += 2U;
            continue;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            specialized[output_cursor++] = '%';
            specialized[output_cursor++] = '0';
        } else {
            input_index = operand_index - 1U;
            (void)memcpy(specialized + output_cursor,
                         replacements[input_index],
                         replacement_lengths[input_index]);
            output_cursor += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    specialized[output_cursor] = '\0';
    if (output_cursor != output_length) {
        free(specialized);
        return false;
    }
    *template_out = specialized;
    *template_length_out = output_length;
    return true;
}

/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: normalize GNU named operand
   references to Core's compact numeric operand indices. Constraint semantics
   stay at the lowering boundary; Core itself only retains operand roles. */
static bool core_inline_asm_named_operand_index(const MinicInlineAsm *source,
                                                const char *name,
                                                size_t name_length,
                                                size_t *operand_index) {
    size_t index;

    if (source == NULL || name == NULL || name_length == 0U || operand_index == NULL) {
        return false;
    }
    for (index = 0U; index < source->output_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->outputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = index;
            return true;
        }
    }
    for (index = 0U; index < source->input_count; ++index) {
        const MinicInlineAsmOperand *operand = &source->inputs[index];
        if (operand->name != NULL && operand->name_length == name_length &&
            memcmp(operand->name, name, name_length) == 0) {
            *operand_index = source->output_count + index;
            return true;
        }
    }
    return false;
}

/* M69_STRUCTURED_ASM_REGISTER_OR_ZERO: normalize named operands while
   preserving a single GNU operand print modifier (for example RISC-V %z).
   Core does not interpret the modifier; the target backend owns that dialect. */
static bool core_inline_asm_numeric_template(const MinicInlineAsm *source,
                                             char **template_out,
                                             size_t *template_length_out) {
    size_t cursor;
    size_t output_length;
    char *normalized;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->template_length == 0U ||
        source->output_count + source->input_count > 10U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;
        char modifier;

        if (source->template_text[cursor] != '%') {
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            free(normalized);
            return false;
        }
        normalized[output_length++] = '%';
        cursor += 1U;
        if (source->template_text[cursor] == '%') {
            normalized[output_length++] = '%';
            cursor += 1U;
            continue;
        }
        modifier = '\0';
        if ((source->template_text[cursor] >= 'A' && source->template_text[cursor] <= 'Z') ||
            (source->template_text[cursor] >= 'a' && source->template_text[cursor] <= 'z')) {
            modifier = source->template_text[cursor++];
            if (cursor >= source->template_length) {
                free(normalized);
                return false;
            }
        }
        if (source->template_text[cursor] >= '0' && source->template_text[cursor] <= '9') {
            operand_index = (size_t)(source->template_text[cursor] - '0');
            if (operand_index >= source->output_count + source->input_count) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor] == '[') {
            size_t name_begin = cursor + 1U;
            size_t name_end = name_begin;
            while (name_end < source->template_length && source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                !core_inline_asm_named_operand_index(source,
                                                     source->template_text + name_begin,
                                                     name_end - name_begin,
                                                     &operand_index) ||
                operand_index > 9U) {
                free(normalized);
                return false;
            }
            if (modifier != '\0') {
                normalized[output_length++] = modifier;
            }
            normalized[output_length++] = (char)('0' + operand_index);
            cursor = name_end + 1U;
            continue;
        }
        free(normalized);
        return false;
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

/* M76_SINGLE_LABEL_ASM_GOTO: admit the common GNU asm-goto seam without
   teaching Core any Linux/static-key meaning. Keep the initial contract narrow:
   one label, one read-only "i" operand whose value requires the existing
   deferred-immediate mechanism, no outputs/clobbers, and only %0/%l[label]/%%
   template references. */
static bool core_inline_asm_single_label_goto_supported(
    const MinicCoreLowerContext *context, const MinicInlineAsm *source) {
    const MinicExpression *input_expression;
    const MinicInlineAsmLabel *label;
    const MinicStatement *target_statement;
    char immediate_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *resolved_text;
    size_t resolved_length;
    size_t cursor;
    bool saw_input;
    bool saw_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        source == NULL || !source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL || source->label_count != 1U ||
        source->labels == NULL || source->register_clobber_count != 0U ||
        source->clobber_count != 0U || source->has_memory_clobber) {
        return false;
    }
    if (source->inputs[0].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        !core_inline_asm_constraint_is(&source->inputs[0], "i")) {
        return false;
    }
    input_expression =
        minic_c0_program_expression(context->body->program, source->inputs[0].expression);
    if (input_expression == NULL ||
        (!minic_type_is_integer(input_expression->type) &&
         !minic_type_is_pointer(input_expression->type))) {
        return false;
    }
    /* Resolved immediates already have the M61 path. M76 is deliberately the
       deferred-immediate asm-goto seam exposed by always-inline helpers. */
    if (core_inline_asm_immediate_text(context,
                                      &source->inputs[0],
                                      immediate_text,
                                      sizeof(immediate_text),
                                      &resolved_text,
                                      &resolved_length)) {
        return false;
    }
    label = &source->labels[0];
    if (label->name == NULL || label->name_length == 0U ||
        label->target_statement == MINIC_STATEMENT_INVALID) {
        return false;
    }
    target_statement =
        minic_c0_program_statement(context->body->program, label->target_statement);
    if (target_statement == NULL || target_statement->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }

    cursor = 0U;
    saw_input = false;
    saw_label = false;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] != '%') {
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) {
            return false;
        }
        if (source->template_text[cursor + 1U] == '%') {
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '0') {
            saw_input = true;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                return false;
            }
            saw_input = true;
            cursor = name_end + 1U;
            continue;
        }
        if (cursor + 3U < source->template_length &&
            source->template_text[cursor + 1U] == 'l' &&
            source->template_text[cursor + 2U] == '[') {
            size_t name_begin = cursor + 3U;
            size_t name_end = name_begin;
            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != label->name_length ||
                memcmp(source->template_text + name_begin, label->name, label->name_length) != 0) {
                return false;
            }
            saw_label = true;
            cursor = name_end + 1U;
            continue;
        }
        return false;
    }
    return saw_input && saw_label;
}

static bool core_inline_asm_single_label_goto_numeric_template(
    const MinicInlineAsm *source, char **template_out, size_t *template_length_out) {
    char *normalized;
    size_t cursor;
    size_t output_length;

    if (source == NULL || template_out == NULL || template_length_out == NULL ||
        source->template_text == NULL || source->inputs == NULL || source->input_count != 1U) {
        return false;
    }
    normalized = (char *)malloc(source->template_length + 1U);
    if (normalized == NULL) {
        return false;
    }
    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        if (source->template_text[cursor] == '%' && cursor + 1U < source->template_length &&
            source->template_text[cursor + 1U] == '[') {
            const MinicInlineAsmOperand *input = &source->inputs[0];
            size_t name_begin = cursor + 2U;
            size_t name_end = name_begin;

            while (name_end < source->template_length &&
                   source->template_text[name_end] != ']') {
                name_end += 1U;
            }
            if (input->name == NULL || input->name_length == 0U ||
                name_end >= source->template_length || name_end == name_begin ||
                name_end - name_begin != input->name_length ||
                memcmp(source->template_text + name_begin, input->name, input->name_length) != 0) {
                free(normalized);
                return false;
            }
            normalized[output_length++] = '%';
            normalized[output_length++] = '0';
            cursor = name_end + 1U;
            continue;
        }
        normalized[output_length++] = source->template_text[cursor++];
    }
    normalized[output_length] = '\0';
    *template_out = normalized;
    *template_length_out = output_length;
    return true;
}

MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        statement->inline_asm_id == MINIC_INLINE_ASM_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
       extended asm. Preflight is deliberately side-effect free: an asm that
       ultimately belongs to an older/specialized path must not leave partial
       Core values, objects, or instructions behind. Only after every operand
       role and the numeric template are proven do we materialize operands. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->label_count == 0U &&
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
            } else {
                supported_shape = false;
                break;
            }
            if ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                core_inline_asm_local_fixed_binding_id(
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
                if (core_inline_asm_local_fixed_binding_id(
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
                        return MINIC_CORE_LOWER_ERROR;
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
                    return status;
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
                    return status;
                }
            }

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                            numeric_template,
                                                            numeric_template_length,
                                                            true,
                                                            source->has_memory_clobber,
                                                            &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
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
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (core_inline_asm_single_label_goto_supported(context, source)) {
        char *numeric_template;
        size_t numeric_template_length;
        MinicCoreBlockId target_block;
        MinicCoreInlineAsm *stored;
        MinicCoreLowerStatus status;

        status = ensure_statement_block(context, source->labels[0].target_statement, &target_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        numeric_template = NULL;
        numeric_template_length = 0U;
        if (!core_inline_asm_single_label_goto_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       numeric_template,
                                                       numeric_template_length,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            free(numeric_template);
            return MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
        stored = &context->function->inline_asms[inline_asm_id];
        stored->is_goto = true;
        stored->source_inline_asm_id = (size_t)statement->inline_asm_id;
        stored->goto_target = target_block;

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

    /* M105_FIXED_REGISTER_STRUCTURED_ASM: Linux SBI-style extended asm uses
       two +r outputs and six r inputs, all backed by GNU local fixed-register
       variables. Import each frontend binding into Core-owned opaque metadata;
       the RV64 backend alone interprets names such as a0..a7. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 6U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t fixed_binding_ids[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            const MinicLocal *local;
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
                (!core_inline_asm_constraint_is(operand, "+r") &&
                 !core_inline_asm_constraint_is(operand, "+&r")) ||
                expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                !core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_ids[output_index])) {
                supported_shape = false;
                break;
            }
            local = minic_c0_program_local(context->body->program, expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (local->is_array || !minic_type_equal(local->type, expression->type)) {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;
            size_t operand_index = source->output_count + input_index;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") || expression == NULL ||
                expression->kind != MINIC_EXPRESSION_LOCAL ||
                !core_scalar_expression_value_type(context->body, expression, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                !core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_ids[operand_index])) {
                supported_shape = false;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count =
                source->output_count + source->input_count;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
                binding->operand_index = output_index;
                if (!core_import_fixed_register_binding(
                        context,
                        fixed_binding_ids[output_index],
                        &binding->fixed_register_binding_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                binding->has_fixed_register_binding = true;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                size_t operand_index = source->output_count + input_index;
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[operand_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = operand_index;
                if (!core_import_fixed_register_binding(
                        context,
                        fixed_binding_ids[operand_index],
                        &binding->fixed_register_binding_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                binding->has_fixed_register_binding = true;
                status = lower_expression(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M110_PURE_REGISTER_OUTPUT_ASM: ordinary volatile extended asm
       with 1..5 write-only register outputs and no inputs/clobbers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count >= 1U && source->output_count <= 5U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
                !core_inline_asm_register_output_constraint(operand) ||
                output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, output_expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->output_count;
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus output_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                binding->operand_index = output_index;
                output_status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (output_status != MINIC_CORE_LOWER_OK) {
                    return output_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M111_PURE_REGISTER_INPUT_ASM: 1..4 read-only scalar register inputs,
       no outputs/clobbers. This is the input-side dual of M110. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->inputs != NULL && source->input_count >= 1U && source->input_count <= 4U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !core_inline_asm_constraint_is(operand, "r") ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    false,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = source->input_count;
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus input_status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                input_status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (input_status != MINIC_CORE_LOWER_OK) {
                    return input_status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured
       operand model is variable-sized. Admit the same proven output/memory
       shape with 0..2 scalar register inputs instead of hard-coding two. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->output_count == 3U && source->input_count <= 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        size_t register_output_count = 0U;
        size_t memory_output_count = 0U;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                core_inline_asm_register_output_constraint(operand)) {
                const MinicLocal *local;
                if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                    expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) || minic_type_is_volatile(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                local = minic_c0_program_local(context->body->program, expression->value.local_id);
                if (local == NULL) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (local->is_array ||
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, expression->value.local_id) != NULL ||
                    !minic_type_equal(local->type, expression->type)) {
                    supported_shape = false;
                    break;
                }
                register_output_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+A")) {
                if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                    minic_type_is_const(expression->type) ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                memory_output_count += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "r") &&
                 !core_inline_asm_constraint_is(operand, "rJ")) || expression == NULL ||
                !core_scalar_expression_value_type(context->body, expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_output_count == 2U && memory_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count =
                source->output_count + source->input_count;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                MinicCoreLowerStatus status;

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[source->output_count + input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = source->output_count + input_index;
                status = lower_expression(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (!source->is_goto && source->template_text != NULL &&
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
                                                              true,
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

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    /* M113_MIXED_ATOMIC_STRUCTURED_ASM: preserve a four-operand
       volatile extended-asm shape consisting of one +r register, one =r/=&r
       register, one +m memory lvalue, and one r/Jr/rJ scalar input with a
       compiler memory clobber. The operand-role model is already generic; this
       only admits the previously unlisted combination. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 3U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression;
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrite_index = SIZE_MAX;
        size_t register_output_index = SIZE_MAX;
        size_t memory_readwrite_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (register_readwrite_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_readwrite_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (register_output_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_output_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                if (memory_readwrite_index != SIZE_MAX) {
                    supported_shape = false;
                    break;
                }
                memory_readwrite_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (!supported_shape || register_readwrite_index == SIZE_MAX ||
            register_output_index == SIZE_MAX || memory_readwrite_index == SIZE_MAX ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "Jr") &&
             !core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            input_expression == NULL ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    true,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 4U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                binding->operand_index = output_index;
                binding->kind = output_index == register_readwrite_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                : output_index == register_output_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[3].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[3].operand_index = 3U;
            status = lower_expression(
                context, input->expression, &structured.value.structured_inline_asm.operands[3].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: preserve a six-operand
       volatile extended-asm shape consisting of one +r register, two =r/=&r
       registers, one +m memory lvalue, and two r/Jr/rJ scalar inputs with a
       compiler memory clobber. Core preserves operand roles; target register
       assignment remains backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 4U && source->input_count == 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrites = 0U;
        size_t register_outputs = 0U;
        size_t memory_readwrites = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_readwrites += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_outputs += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                memory_readwrites += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "Jr") &&
                 !core_inline_asm_constraint_is(operand, "rJ") &&
                 !core_inline_asm_constraint_is(operand, "r")) ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrites == 1U && register_outputs == 2U &&
            memory_readwrites == 1U &&
            core_inline_asm_numeric_template(source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    true,
                                                                    &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 6U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                    core_inline_asm_constraint_is(operand, "+r")) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
                } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                           core_inline_asm_register_output_constraint(operand)) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                } else {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                }
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[4U + input_index];
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = 4U + input_index;
                status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M125_STRUCTURED_MEMORY_INPUT_ASM: one register read/write output,
       one write-only register output, and one read-only memory input. `m` is
       address-backed in Core; the backend materializes only its address and
       never writes the referenced object after the asm. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t register_output_count = 0U;
        size_t register_readwrite_count = 0U;
        bool supported_shape = true;

        if (input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !core_inline_asm_constraint_is(input, "m") || input_expression == NULL ||
            input_expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(input_expression->type, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        for (output_index = 0U; supported_shape && output_index < source->output_count;
             ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                register_readwrite_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                register_output_count += 1U;
            } else {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrite_count == 1U && register_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 3U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_address(
                context, input->expression, &structured.value.structured_inline_asm.operands[2].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one
       register read/write output with one write-only memory output and a
       scalar register/immediate input. Preserve those access roles in Core;
       target register allocation and template interpretation remain backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *memory_output = NULL;
        const MinicInlineAsmOperand *register_output = NULL;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreInstruction structured;
        MinicType input_type;
        MinicType memory_type;
        MinicType register_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t memory_index = SIZE_MAX;
        size_t register_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *candidate = &source->outputs[output_index];

            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+r")) {
                if (register_output != NULL) {
                    supported_shape = false;
                    break;
                }
                register_output = candidate;
                register_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(candidate, "=m")) {
                if (memory_output != NULL) {
                    supported_shape = false;
                    break;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }

        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        memory_expression = memory_output == NULL
                                ? NULL
                                : minic_c0_program_expression(context->body->program,
                                                              memory_output->expression);
        register_expression = register_output == NULL
                                  ? NULL
                                  : minic_c0_program_expression(context->body->program,
                                                                register_output->expression);
        register_local = register_expression == NULL ||
                                 register_expression->kind != MINIC_EXPRESSION_LOCAL
                             ? NULL
                             : minic_c0_program_local(context->body->program,
                                                      register_expression->value.local_id);
        if (!supported_shape || register_output == NULL || memory_output == NULL ||
            register_index == SIZE_MAX || memory_index == SIZE_MAX || input_expression == NULL ||
            memory_expression == NULL || register_expression == NULL || register_local == NULL ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            register_expression->value_category != MINIC_VALUE_LVALUE ||
            memory_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(register_expression->type) ||
            minic_type_is_volatile(register_expression->type) ||
            minic_type_is_const(memory_expression->type) || register_local->is_array ||
            minic_c0_program_local_fixed_register_binding(
                context->body->program, register_expression->value.local_id) != NULL ||
            !minic_type_equal(register_local->type, register_expression->type) ||
            !minic_type_unqualified(register_expression->type, &register_type) ||
            !minic_type_unqualified(memory_expression->type, &memory_type) ||
            !core_memory_scalar_type(register_type) || !core_memory_scalar_type(memory_type) ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }

        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           numeric_template,
                                                           numeric_template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = NULL;
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 3U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                binding->kind = output_index == register_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_expression(
                context, input->expression, &structured.value.structured_inline_asm.operands[2].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicInlineAsmOperand *register_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;
        MinicType output_type;
        size_t memory_index;
        size_t register_index;

        input = &source->inputs[0];
        memory_output = NULL;
        register_output = NULL;
        memory_index = SIZE_MAX;
        register_index = SIZE_MAX;
        for (size_t output_index = 0U; output_index < 2U; ++output_index) {
            const MinicInlineAsmOperand *candidate;

            candidate = &source->outputs[output_index];
            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+A")) {
                if (memory_output != NULL) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(candidate)) {
                if (register_output != NULL) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                register_output = candidate;
                register_index = output_index;
            } else {
                memory_output = NULL;
                register_output = NULL;
                break;
            }
        }
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        memory_expression = memory_output == NULL
                                ? NULL
                                : minic_c0_program_expression(context->body->program,
                                                              memory_output->expression);
        register_expression = register_output == NULL
                                  ? NULL
                                  : minic_c0_program_expression(context->body->program,
                                                                register_output->expression);
        if (memory_output != NULL && register_output != NULL &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "r") && input_expression != NULL &&
            memory_expression != NULL && register_expression != NULL &&
            memory_expression->value_category == MINIC_VALUE_LVALUE &&
            register_expression->kind == MINIC_EXPRESSION_LOCAL &&
            register_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            !minic_type_is_const(register_expression->type) &&
            !minic_type_is_volatile(register_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            minic_type_unqualified(register_expression->type, &output_type) &&
            core_memory_scalar_type(memory_type) && core_memory_scalar_type(output_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type) && minic_type_equal(output_type, memory_type)) {
            register_local = minic_c0_program_local(
                context->body->program, register_expression->value.local_id);
            if (register_local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!register_local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, register_expression->value.local_id) == NULL &&
                minic_type_equal(register_local->type, register_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                status = lower_address(context, memory_output->expression, &memory_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    memory_address >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                    inline_asm_id;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                    memory_address;
                instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index =
                    memory_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                    register_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                    2U;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                status = lower_address(context, register_output->expression, &output_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = output_address;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;

        memory_output = &source->outputs[0];
        input = &source->inputs[0];
        memory_expression =
            minic_c0_program_expression(context->body->program, memory_output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (memory_output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(memory_output, "+A") &&
            core_inline_asm_constraint_is(input, "r") && memory_expression != NULL &&
            input_expression != NULL && memory_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            core_memory_scalar_type(memory_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_address(context, memory_output->expression, &memory_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (input_value >= context->function->value_count ||
                memory_address >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                inline_asm_id;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                memory_address;
            instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index = 0U;
            instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                SIZE_MAX;
            instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                1U;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *output;
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId input_value;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType output_type;
        bool input_register_constraint;
        bool output_register_constraint;

        output = &source->outputs[0];
        input = &source->inputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        output_register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        input_register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U && memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U && memcmp(input->constraint_text, "rK", 2U) == 0));
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            output_register_constraint && input_register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.register_output_input_inline_asm.inline_asm_id = inline_asm_id;
                instruction.value.register_output_input_inline_asm.operand = input_value;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
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
    }

    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: after compile-time i/I inputs
       are specialized into target text, preserve a +r operand as one
       address-backed read/write register binding. Register-clobber names stay
       opaque in Core and are interpreted only by the target backend when it
       chooses operand registers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == source->register_clobber_count) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;

        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            core_inline_asm_constraint_is(output, "+r") &&
            output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            MinicCoreInstruction structured;
            MinicCoreStructuredInlineAsmOperand *binding;
            MinicCoreLowerStatus status;
            size_t clobber_index;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id);
            free(specialized_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
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
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 1U;
            binding = &structured.value.structured_inline_asm.operands[0];
            binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            binding->operand_index = 0U;
            status = lower_address(context, output->expression, &binding->value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(specialized_template);
    }

    /* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: after all i/I
       inputs are baked into the template, the runtime shape is exactly the
       existing one-register-output instruction. Core has no optimizer that can
       discard value-producing asm, so retain the specialized instruction in the
       existing execution-effect table; this does not add source-level volatile
       semantics or target-specific IR. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        char *specialized_template;
        size_t specialized_length;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        specialized_template = NULL;
        specialized_length = 0U;
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
                /* The specialized text contains only the runtime output %0 and
                   literal %% escapes. Retain it as an execution effect because
                   its SSA result is semantically required. */
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id)) {
                    free(specialized_template);
                    return MINIC_CORE_LOWER_ERROR;
                }
                free(specialized_template);
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
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
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

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count == 1U && source->input_count == 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
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
            core_memory_scalar_type(output_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    return MINIC_CORE_LOWER_ERROR;
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
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
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
    }

    /* M77_EMPTY_TIED_ASM_COPY: an empty, nonvolatile GNU asm with one
       register output tied to input 0 carries no target instruction semantics.
       It preserves the input register bit-pattern in the output. Model that
       target-neutrally as scalar bitcast/copy plus the output store. */
    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->label_count == 0U &&
        source->clobber_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        MinicCoreInstruction store;
        MinicCoreLowerStatus status;
        MinicCoreValueId input_value;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicType input_type;
        MinicType output_type;

        input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            core_inline_asm_register_output_constraint(output) &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "0") && output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, statement->span, output_type, input_value, &output_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_address(context, output->expression, &output_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&store, 0, sizeof(store));
            store.kind = MINIC_CORE_INSTRUCTION_STORE;
            store.span = statement->span;
            store.type = minic_type_void();
            store.result = MINIC_CORE_VALUE_INVALID;
            store.value.store.address = output_address;
            store.value.store.stored_value = output_value;
            store.value.store.is_volatile = minic_type_is_volatile(output_expression->type);
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &store)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&
        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            output->constraint_text != NULL && output->constraint_length == 3U &&
            memcmp(output->constraint_text, "+rm", 3U) == 0 && output_expression != NULL &&
            output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(output_expression->type) &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type)) {
            local =
                minic_c0_program_local(context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array && !local->is_register_storage &&
                minic_type_equal(local->type, output_expression->type) &&
                !minic_type_is_const(local->type) && !minic_type_is_volatile(local->type)) {
                return MINIC_CORE_LOWER_OK;
            }
        }
    }

    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: `asm volatile("")` carries a
       sequencing/volatile effect but intentionally emits no target text. Keep
       the effect explicitly in Core; do not invent a memory clobber. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->clobber_count == 0U &&
        !source->has_memory_clobber) {
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       source->template_text,
                                                       0U,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
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

    /* M59_EMPTY_SCALAR_INPUT_BARRIER: GNU barrier_data() is an empty
       volatile asm with one scalar register input and a memory clobber. The
       operand must still be evaluated, but an empty target template needs no
       target instruction. Represent the ordering effect with the existing
       target-neutral compiler barrier rather than inventing an empty opaque
       asm encoding. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId discarded_input;
        MinicCoreLowerStatus input_status;
        MinicType input_type;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            (core_inline_asm_constraint_is(input, "r") ||
             core_inline_asm_constraint_is(input, "rK")) &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &discarded_input);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (discarded_input >= context->function->value_count ||
                !minic_type_equal(context->function->values[discarded_input].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY: outputless GNU asm is
       effectively volatile (Batch F).  The two-register-input structured form
       is valid both for ordering-sensitive asm carrying a memory clobber and
       for MMIO-style asm whose template itself performs the access.  Preserve
       the actual memory effect flag rather than requiring one to exist. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < 2U; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression = minic_c0_program_expression(
                context->body->program, operand->expression);
            MinicType input_type;
            bool register_constraint;

            register_constraint =
                operand->constraint_text != NULL &&
                ((operand->constraint_length == 1U &&
                  memcmp(operand->constraint_text, "r", 1U) == 0) ||
                 (operand->constraint_length == 2U &&
                  memcmp(operand->constraint_text, "rK", 2U) == 0));
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !register_constraint || input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = numeric_template_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              source->has_memory_clobber,
                                                              &inline_asm_id);
            free(numeric_template);
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 2U;
            for (input_index = 0U; input_index < 2U; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                status = lower_expression(context, source->inputs[input_index].expression,
                                          &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId input_value;
        MinicCoreLowerStatus input_status;
        MinicType input_type;
        bool register_constraint;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U &&
              memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U &&
              memcmp(input->constraint_text, "rK", 2U) == 0));
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY && register_constraint &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &input_value);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (input_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           true,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.scalar_input_inline_asm.inline_asm_id = inline_asm_id;
            instruction.value.scalar_input_inline_asm.operand = input_value;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* M87_IMMEDIATE_ASM_FRONTIER_TRACE: report details only after every
       supported inline-asm path above has declined the statement. This keeps
       frontier observability from becoming a false first-error locator. */
    if (source->is_volatile && !source->is_goto && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U) {
        size_t trace_input_index;

        (void)fprintf(stderr,
                      "CORE_ASM_DETAIL reason=unclaimed function=%s inputs=%zu "
                      "reg_clobbers=%zu clobbers=%zu memory=%d template_length=%zu\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      source->input_count,
                      source->register_clobber_count,
                      source->clobber_count,
                      source->has_memory_clobber ? 1 : 0,
                      source->template_length);
        for (trace_input_index = 0U; trace_input_index < source->input_count; ++trace_input_index) {
            const MinicInlineAsmOperand *trace_operand = &source->inputs[trace_input_index];
            const MinicExpression *trace_expression = minic_c0_program_expression(
                context->body->program, trace_operand->expression);
            char trace_integer_text[MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
            const char *trace_resolved_text = NULL;
            size_t trace_resolved_length = 0U;
            bool trace_resolved = core_inline_asm_immediate_text(
                context,
                trace_operand,
                trace_integer_text,
                sizeof(trace_integer_text),
                &trace_resolved_text,
                &trace_resolved_length);
            (void)trace_resolved_text;
            (void)fprintf(stderr,
                          "CORE_ASM_DETAIL input function=%s index=%zu constraint=%.*s "
                          "access=%d expr_kind=%d immediate_resolved=%d resolved_length=%zu\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          trace_input_index,
                          (int)trace_operand->constraint_length,
                          trace_operand->constraint_text != NULL ? trace_operand->constraint_text : "",
                          (int)trace_operand->access,
                          trace_expression != NULL ? (int)trace_expression->kind : -1,
                          trace_resolved ? 1 : 0,
                          trace_resolved_length);
        }
    }

    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
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

