#include "target/riscv64/codegen_internal.h"
#include "frontend/const_eval.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U

typedef struct MinicRiscv64InlineAsmRegisterCandidate {
    const char *name;
    bool is_callee_saved;
} MinicRiscv64InlineAsmRegisterCandidate;

static const MinicRiscv64InlineAsmRegisterCandidate minic_riscv64_inline_asm_registers[] = {
    {"t0", false},
    {"t1", false},
    {"t3", false},
    {"t4", false},
    {"t5", false},
    {"t6", false},
    {"s1", true},
    {"s2", true},
    {"s3", true},
    {"s4", true},
    {"s5", true},
    {"s6", true},
};

#define MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT                                                    \
    (sizeof(minic_riscv64_inline_asm_registers) / sizeof(minic_riscv64_inline_asm_registers[0]))

static bool constraint_is(const MinicInlineAsmOperand *operand, const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool constraint_is_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "i") || constraint_is(operand, "I");
}

static bool constraint_matching_output(const MinicInlineAsm *inline_asm,
                                       const MinicInlineAsmOperand *operand,
                                       size_t *output_index) {
    unsigned char ch;
    size_t index;

    if (inline_asm == NULL || operand == NULL || output_index == NULL ||
        operand->constraint_text == NULL || operand->constraint_length != 1U) {
        return false;
    }
    ch = (unsigned char)operand->constraint_text[0];
    if (ch < '0' || ch > '9') {
        return false;
    }
    index = (size_t)(ch - '0');
    if (index >= inline_asm->output_count) {
        return false;
    }
    *output_index = index;
    return true;
}

static bool matching_output_is_register(const MinicInlineAsm *inline_asm, size_t output_index) {
    const MinicInlineAsmOperand *output;

    if (inline_asm == NULL || output_index >= inline_asm->output_count) {
        return false;
    }
    output = &inline_asm->outputs[output_index];
    return constraint_is(output, "=r") || constraint_is(output, "=&r") ||
           constraint_is(output, "+r");
}

static const MinicInlineAsmOperand *operand_at(const MinicInlineAsm *inline_asm,
                                               size_t operand_index) {
    if (inline_asm == NULL) {
        return NULL;
    }
    if (operand_index < inline_asm->output_count) {
        return &inline_asm->outputs[operand_index];
    }
    operand_index -= inline_asm->output_count;
    if (operand_index >= inline_asm->input_count) {
        return NULL;
    }
    return &inline_asm->inputs[operand_index];
}

static bool
operand_name_matches(const MinicInlineAsmOperand *operand, const char *name, size_t name_length) {
    return operand != NULL && operand->name != NULL && name != NULL &&
           operand->name_length == name_length && memcmp(operand->name, name, name_length) == 0;
}

static bool find_named_operand(const MinicInlineAsm *inline_asm,
                               const char *name,
                               size_t name_length,
                               size_t *operand_index) {
    size_t index;
    size_t operand_count;

    if (inline_asm == NULL || name == NULL || name_length == 0U || operand_index == NULL ||
        inline_asm->output_count > SIZE_MAX - inline_asm->input_count) {
        return false;
    }
    operand_count = inline_asm->output_count + inline_asm->input_count;
    for (index = 0U; index < operand_count; ++index) {
        if (operand_name_matches(operand_at(inline_asm, index), name, name_length)) {
            *operand_index = index;
            return true;
        }
    }
    return false;
}

static const MinicInlineAsmLabel *
find_named_label(const MinicInlineAsm *inline_asm, const char *name, size_t name_length) {
    size_t index;

    if (inline_asm == NULL || name == NULL || name_length == 0U) {
        return NULL;
    }
    for (index = 0U; index < inline_asm->label_count; ++index) {
        const MinicInlineAsmLabel *label;

        label = &inline_asm->labels[index];
        if (label->name != NULL && label->name_length == name_length &&
            memcmp(label->name, name, name_length) == 0) {
            return label;
        }
    }
    return NULL;
}

static bool validate_output(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (constraint_is(operand, "+A")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;
    }
    if (constraint_is(operand, "=m")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    }
    if (constraint_is(operand, "+r")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
               expression->kind == MINIC_EXPRESSION_LOCAL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    if (constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    return false;
}

static bool validate_input(const MinicInlineAsm *inline_asm,
                           const MinicC0Program *program,
                           const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;
    size_t matching_output_index;

    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
        return false;
    }
    if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
        if (!matching_output_is_register(inline_asm, matching_output_index)) {
            return false;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        return expression != NULL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&
               !constraint_is(operand, "rK") && !constraint_is(operand, "m")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL) {
        return false;
    }
    if (constraint_is(operand, "m")) {
        return expression->value_category == MINIC_VALUE_LVALUE;
    }
    if (constraint_is(operand, "rJ")) {
        return minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type);
    }
    return minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type);
}

static bool operand_constant_zero(const MinicC0Program *program,
                                  const MinicInlineAsmOperand *operand) {
    MinicConstValue constant;
    bool is_zero;

    return program != NULL && operand != NULL &&
           minic_const_eval_integer(
               program, minic_default_target_info(), operand->expression, &constant) &&
           minic_const_value_is_zero(program, minic_default_target_info(), &constant, &is_zero) &&
           is_zero;
}

static bool operand_constant_u5(const MinicC0Program *program,
                                const MinicInlineAsmOperand *operand) {
    MinicConstValue constant;
    int64_t value;

    return program != NULL && operand != NULL &&
           minic_const_eval_integer(
               program, minic_default_target_info(), operand->expression, &constant) &&
           minic_const_value_as_int64(program, minic_default_target_info(), &constant, &value) &&
           value >= 0 && value <= 31;
}

static bool operand_uses_immediate(const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand) {
    if (constraint_is_immediate(operand)) {
        return true;
    }
    if (constraint_is(operand, "rJ")) {
        return operand_constant_zero(program, operand);
    }
    return constraint_is(operand, "rK") && operand_constant_u5(program, operand);
}

static bool inline_asm_clobbers_register(const MinicInlineAsm *inline_asm,
                                         const char *register_name) {
    size_t index;
    size_t register_length;

    if (inline_asm == NULL || register_name == NULL) {
        return false;
    }
    register_length = strlen(register_name);
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        const MinicInlineAsmRegisterClobber *clobber;

        clobber = &inline_asm->register_clobbers[index];
        if (clobber->name != NULL && clobber->name_length == register_length &&
            memcmp(clobber->name, register_name, register_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool inline_asm_register_is_callee_saved(const char *register_name) {
    size_t index;

    if (register_name == NULL) {
        return false;
    }
    for (index = 0U; index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT; ++index) {
        if (strcmp(minic_riscv64_inline_asm_registers[index].name, register_name) == 0) {
            return minic_riscv64_inline_asm_registers[index].is_callee_saved;
        }
    }
    return false;
}

static bool append_saved_operand_register(const char *register_name,
                                          const char **saved_registers,
                                          size_t *saved_register_count) {
    size_t index;

    if (register_name == NULL || saved_registers == NULL || saved_register_count == NULL ||
        *saved_register_count >= MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS) {
        return false;
    }
    if (!inline_asm_register_is_callee_saved(register_name)) {
        return true;
    }
    for (index = 0U; index < *saved_register_count; ++index) {
        if (strcmp(saved_registers[index], register_name) == 0) {
            return true;
        }
    }
    saved_registers[*saved_register_count] = register_name;
    *saved_register_count += 1U;
    return true;
}

static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const MinicC0Program *program,
                                     const char **operand_registers,
                                     size_t operand_count) {
    size_t candidate_index;
    size_t operand_index;

    if (inline_asm == NULL || program == NULL || operand_registers == NULL) {
        return false;
    }
    candidate_index = 0U;
    for (operand_index = 0U; operand_index < operand_count; ++operand_index) {
        const MinicInlineAsmOperand *operand;

        operand = operand_at(inline_asm, operand_index);
        if (operand == NULL) {
            return false;
        }
        if (operand_index >= inline_asm->output_count) {
            size_t matching_output_index;

            if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
                if (!matching_output_is_register(inline_asm, matching_output_index) ||
                    operand_registers[matching_output_index] == NULL) {
                    return false;
                }
                operand_registers[operand_index] = operand_registers[matching_output_index];
                continue;
            }
        }
        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
        while (candidate_index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT &&
               (inline_asm_clobbers_register(
                    inline_asm, minic_riscv64_inline_asm_registers[candidate_index].name) ||
                (inline_asm->is_goto &&
                 minic_riscv64_inline_asm_registers[candidate_index].is_callee_saved))) {
            candidate_index += 1U;
        }
        if (candidate_index >= MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT) {
            return false;
        }
        operand_registers[operand_index] = minic_riscv64_inline_asm_registers[candidate_index].name;
        candidate_index += 1U;
    }
    return true;
}

static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent,
                                       bool *zero_modifier) {
    size_t index;
    unsigned char ch;

    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || zero_modifier == NULL ||
        *template_index >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%') {
        return false;
    }
    index = *template_index + 1U;
    if (index >= inline_asm->template_length) {
        return false;
    }
    ch = (unsigned char)inline_asm->template_text[index];
    if (ch == '%') {
        *template_index = index;
        *literal_percent = true;
        *zero_modifier = false;
        *operand_index = 0U;
        return true;
    }
    *literal_percent = false;
    *zero_modifier = false;
    if (ch == 'z') {
        *zero_modifier = true;
        index += 1U;
        if (index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
    }
    if (ch >= '0' && ch <= '9') {
        *operand_index = (size_t)(ch - '0');
        *template_index = index;
        return *operand_index < operand_count;
    }
    if (ch == '[') {
        size_t name_begin;
        size_t name_end;

        name_begin = index + 1U;
        name_end = name_begin;
        while (name_end < inline_asm->template_length &&
               inline_asm->template_text[name_end] != ']') {
            name_end += 1U;
        }
        if (name_end == name_begin || name_end >= inline_asm->template_length ||
            !find_named_operand(inline_asm,
                                inline_asm->template_text + name_begin,
                                name_end - name_begin,
                                operand_index)) {
            return false;
        }
        *template_index = name_end;
        return true;
    }
    return false;
}

static bool resolve_label_reference(const MinicInlineAsm *inline_asm,
                                    size_t *template_index,
                                    MinicStatementId *target_statement) {
    size_t index;
    size_t name_begin;
    size_t name_end;
    const MinicInlineAsmLabel *label;

    if (inline_asm == NULL || template_index == NULL || target_statement == NULL ||
        *template_index + 3U >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%' ||
        inline_asm->template_text[*template_index + 1U] != 'l' ||
        inline_asm->template_text[*template_index + 2U] != '[') {
        return false;
    }
    index = *template_index + 3U;
    name_begin = index;
    while (index < inline_asm->template_length && inline_asm->template_text[index] != ']') {
        index += 1U;
    }
    name_end = index;
    if (name_end == name_begin || name_end >= inline_asm->template_length) {
        return false;
    }
    label =
        find_named_label(inline_asm, inline_asm->template_text + name_begin, name_end - name_begin);
    if (label == NULL || label->target_statement == MINIC_STATEMENT_INVALID) {
        return false;
    }
    *target_statement = label->target_statement;
    *template_index = name_end;
    return true;
}

static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {
    size_t index;

    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        bool literal_percent;
        bool zero_modifier;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 2U < inline_asm->template_length &&
            inline_asm->template_text[index + 1U] == 'l' &&
            inline_asm->template_text[index + 2U] == '[') {
            MinicStatementId target_statement;

            if (!resolve_label_reference(inline_asm, &index, &target_statement)) {
                return false;
            }
            (void)target_statement;
            continue;
        }
        if (!resolve_template_reference(inline_asm,
                                        operand_count,
                                        &index,
                                        &operand_index,
                                        &literal_percent,
                                        &zero_modifier)) {
            return false;
        }
        (void)operand_index;
        (void)literal_percent;
        (void)zero_modifier;
    }
    return true;
}

static const MinicExpression *strip_symbolic_immediate_wrappers(const MinicC0Program *program,
                                                                const MinicExpression *expression) {
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression = minic_c0_program_expression(program, expression->value.unary.operand);
    }
    return expression;
}

static const char *symbolic_immediate_name(const MinicC0Program *program,
                                           MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicFunction *function;
    const MinicGlobalObject *object;

    expression = strip_symbolic_immediate_wrappers(
        program, minic_c0_program_expression(program, expression_id));
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        function = minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return NULL;
        }
        return function->assembler_name != NULL ? function->assembler_name : function->name;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
        minic_type_is_array(expression->type)) {
        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object == NULL ? NULL : object->name;
    }
    if (expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return NULL;
    }
    expression = strip_symbolic_immediate_wrappers(
        program, minic_c0_program_expression(program, expression->value.unary.operand));
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        function = minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return NULL;
        }
        return function->assembler_name != NULL ? function->assembler_name : function->name;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object == NULL ? NULL : object->name;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;

        base = strip_symbolic_immediate_wrappers(
            program, minic_c0_program_expression(program, expression->value.subscript.base));
        index = strip_symbolic_immediate_wrappers(
            program, minic_c0_program_expression(program, expression->value.subscript.index));
        if (base == NULL || index == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
            !minic_type_is_array(base->type) || index->kind != MINIC_EXPRESSION_INTEGER ||
            index->value.integer_value != 0) {
            return NULL;
        }
        object = minic_c0_program_global_object(program, base->value.global_object_id);
        return object == NULL ? NULL : object->name;
    }
    return NULL;
}

static bool immediate_integer_value(const MinicC0Program *program,
                                    MinicExpressionId expression_id,
                                    int64_t *value) {
    MinicConstValue constant;

    return value != NULL &&
           minic_const_eval_integer(
               program, minic_default_target_info(), expression_id, &constant) &&
           minic_const_value_as_int64(program, minic_default_target_info(), &constant, value);
}

static bool immediate_operand_is_resolved(const MinicC0Program *program,
                                          const MinicInlineAsmOperand *operand) {
    int64_t value;

    if (program == NULL || operand == NULL) {
        return false;
    }
    if (immediate_integer_value(program, operand->expression, &value)) {
        return true;
    }
    return constraint_is(operand, "i") &&
           symbolic_immediate_name(program, operand->expression) != NULL;
}

static bool emit_immediate_operand(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand,
                                   MinicInlineAsmId inline_asm_id,
                                   size_t operand_index,
                                   bool allow_deferred) {
    const char *symbol_name;
    int64_t value;

    if (file == NULL || program == NULL || operand == NULL) {
        return false;
    }
    if (immediate_integer_value(program, operand->expression, &value)) {
        return fprintf(file, "%" PRId64, value) >= 0;
    }
    if (constraint_is(operand, "rJ") && operand_constant_zero(program, operand)) {
        return fputc('0', file) != EOF;
    }
    if (constraint_is(operand, "i") &&
        (symbol_name = symbolic_immediate_name(program, operand->expression)) != NULL) {
        return fputs(symbol_name, file) != EOF;
    }
    if (!allow_deferred) {
        return false;
    }
    return fprintf(file,
                   "__minic_deferred_asm_immediate_%zu_%zu",
                   (size_t)inline_asm_id,
                   operand_index) >= 0;
}

static bool emit_template(FILE *file,
                          const MinicC0Program *program,
                          const MinicInlineAsm *inline_asm,
                          MinicInlineAsmId inline_asm_id,
                          const char *const *operand_registers) {
    size_t operand_count;
    size_t index;

    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (!template_operands_are_valid(inline_asm, operand_count) || fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        bool literal_percent;
        bool zero_modifier;

        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 2U < inline_asm->template_length &&
            inline_asm->template_text[index + 1U] == 'l' &&
            inline_asm->template_text[index + 2U] == '[') {
            MinicStatementId target_statement;

            if (!resolve_label_reference(inline_asm, &index, &target_statement) ||
                fprintf(file, ".Luser_%zu", (size_t)target_statement) < 0) {
                return false;
            }
            continue;
        }
        if (!resolve_template_reference(inline_asm,
                                        operand_count,
                                        &index,
                                        &operand_index,
                                        &literal_percent,
                                        &zero_modifier)) {
            return false;
        }
        if (literal_percent) {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        {
            const MinicInlineAsmOperand *operand;
            const char *register_name;

            operand = operand_at(inline_asm, operand_index);
            if (operand == NULL) {
                return false;
            }
            register_name = operand_registers[operand_index];
            if (zero_modifier && operand_uses_immediate(program, operand) &&
                operand_constant_zero(program, operand)) {
                if (fputs("zero", file) == EOF) {
                    return false;
                }
            } else if (operand_uses_immediate(program, operand)) {
                if (!emit_immediate_operand(file,
                                            program,
                                            operand,
                                            inline_asm_id,
                                            operand_index,
                                            inline_asm->is_goto)) {
                    return false;
                }
            } else if (register_name == NULL) {
                return false;
            } else if (constraint_is(operand, "+A")) {
                if (fprintf(file, "(%s)", register_name) < 0) {
                    return false;
                }
            } else if (constraint_is(operand, "=m") || constraint_is(operand, "m")) {
                if (fprintf(file, "0(%s)", register_name) < 0) {
                    return false;
                }
            } else if (fputs(register_name, file) == EOF) {
                return false;
            }
        }
    }
    return fputc('\n', file) != EOF;
}

bool minic_riscv64_emit_inline_asm(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicRiscv64FunctionLayout *function_layout,
                                   const MinicStatement *statement) {
    const MinicInlineAsm *inline_asm;
    const char *operand_registers[MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS];
    const char *saved_registers[MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS];
    size_t operand_count;
    size_t saved_register_count;
    size_t temporary_slot_count;
    size_t temporary_size;
    size_t index;

    if (file == NULL || program == NULL || function == NULL || statement == NULL) {
        return false;
    }
    inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    if (inline_asm->output_count == 0U && inline_asm->input_count == 0U &&
        inline_asm->label_count == 0U) {
        return fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
    }
    if (inline_asm->output_count == 1U && inline_asm->input_count == 0U &&
        inline_asm->template_length == 0U && constraint_is(&inline_asm->outputs[0], "+rm") &&
        inline_asm->outputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        return true;
    }

    if (inline_asm->output_count > SIZE_MAX - inline_asm->input_count) {
        return false;
    }
    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (operand_count > MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS ||
        !template_operands_are_valid(inline_asm, operand_count)) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (!validate_output(program, &inline_asm->outputs[index])) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (!validate_input(inline_asm, program, &inline_asm->inputs[index])) {
            return false;
        }
    }
    if (!assign_operand_registers(inline_asm, program, operand_registers, operand_count)) {
        return false;
    }
    saved_register_count = 0U;
    for (index = 0U; index < operand_count; ++index) {
        if (operand_registers[index] != NULL &&
            !append_saved_operand_register(
                operand_registers[index], saved_registers, &saved_register_count)) {
            return false;
        }
    }
    if (inline_asm->is_goto && saved_register_count != 0U) {
        return false;
    }

    for (index = 0U; index < inline_asm->input_count; ++index) {
        const MinicInlineAsmOperand *operand;
        const MinicExpression *expression;
        size_t operand_index;

        operand = &inline_asm->inputs[index];
        if (!constraint_is_immediate(operand)) {
            continue;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        operand_index = inline_asm->output_count + index;
        if (expression == NULL) {
            return false;
        }
        if (!immediate_operand_is_resolved(program, operand)) {
            if (!inline_asm->is_goto ||
                fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_%zu\n",
                        (size_t)statement->inline_asm_id,
                        operand_index) < 0) {
                return false;
            }
        }
    }

    if (operand_count > SIZE_MAX - saved_register_count) {
        return false;
    }
    temporary_slot_count = operand_count + saved_register_count;
    if (temporary_slot_count > (SIZE_MAX - 15U) / 8U) {
        return false;
    }
    temporary_size = inline_asm->is_goto ? 0U : (temporary_slot_count * 8U + 15U) & ~(size_t)15U;
    if (!minic_riscv64_emit_stack_allocate(file, temporary_size)) {
        return false;
    }
    for (index = 0U; index < saved_register_count; ++index) {
        if (!minic_riscv64_emit_sp_store64(
                file, saved_registers[index], (operand_count + index) * 8U)) {
            return false;
        }
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;

        operand = &inline_asm->outputs[index];
        if (constraint_is(operand, "+A") || constraint_is(operand, "=m") ||
            constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {
            if (!minic_riscv64_emit_lvalue_address(
                    file, program, function, function_layout, operand->expression) ||
                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {
                return false;
            }
        } else if (constraint_is(operand, "+r")) {
            const MinicExpression *expression;

            expression = minic_c0_program_expression(program, operand->expression);
            if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                !minic_riscv64_emit_object_load(
                    file, program, function, function_layout, expression->value.local_id) ||
                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {
                return false;
            }
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        const MinicInlineAsmOperand *operand;
        size_t operand_index;

        operand = &inline_asm->inputs[index];
        operand_index = inline_asm->output_count + index;
        if (operand_uses_immediate(program, operand)) {
            continue;
        }
        if (constraint_is(operand, "m")) {
            if (!minic_riscv64_emit_lvalue_address(
                    file, program, function, function_layout, operand->expression) ||
                !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {
                return false;
            }
            continue;
        }
        if (!minic_riscv64_emit_expression(
                file, program, function, function_layout, operand->expression) ||
            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {
            return false;
        }
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        if ((constraint_is(&inline_asm->outputs[index], "+A") ||
             constraint_is(&inline_asm->outputs[index], "=m") ||
             constraint_is(&inline_asm->outputs[index], "+r")) &&
            !minic_riscv64_emit_sp_load64(file, operand_registers[index], index * 8U)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        size_t operand_index;

        operand_index = inline_asm->output_count + index;
        if (operand_uses_immediate(program, &inline_asm->inputs[index])) {
            continue;
        }
        if (!minic_riscv64_emit_sp_load64(
                file, operand_registers[operand_index], operand_index * 8U)) {
            return false;
        }
    }
    if (!emit_template(file, program, inline_asm, statement->inline_asm_id, operand_registers)) {
        return false;
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;
        const MinicExpression *expression;

        operand = &inline_asm->outputs[index];
        if (!constraint_is(operand, "=r") && !constraint_is(operand, "=&r") &&
            !constraint_is(operand, "+r")) {
            continue;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        if (expression == NULL) {
            return false;
        }
        if (constraint_is(operand, "+r")) {
            if (expression->kind != MINIC_EXPRESSION_LOCAL ||
                !minic_riscv64_emit_object_store_register(file,
                                                          program,
                                                          function,
                                                          function_layout,
                                                          expression->value.local_id,
                                                          operand_registers[index])) {
                return false;
            }
        } else if (!minic_riscv64_emit_sp_load64(file, "a0", index * 8U) ||
                   !minic_riscv64_emit_scalar_store(
                       file, expression->type, operand_registers[index], "a0")) {
            return false;
        }
    }
    for (index = 0U; index < saved_register_count; ++index) {
        if (!minic_riscv64_emit_sp_load64(
                file, saved_registers[index], (operand_count + index) * 8U)) {
            return false;
        }
    }
    return minic_riscv64_emit_stack_release(file, temporary_size);
}
