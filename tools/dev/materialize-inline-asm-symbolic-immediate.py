#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()

if '#include "frontend/const_eval.h"' not in text:
    text = text.replace('#include "target/riscv64/codegen_internal.h"\n', '#include "target/riscv64/codegen_internal.h"\n#include "frontend/const_eval.h"\n', 1)

old_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I")) {
        return false;
    }
'''
new_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i")) {
        return false;
    }
'''
if old_validate in text:
    text = text.replace(old_validate, new_validate, 1)
elif new_validate not in text:
    raise SystemExit("inline asm input validation anchor not found")

old_emit = '''static bool emit_immediate_operand(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand,
                                   MinicInlineAsmId inline_asm_id,
                                   size_t operand_index) {
    const MinicExpression *expression;

    if (file == NULL || program == NULL || operand == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER && minic_type_is_integer(expression->type)) {
        return fprintf(file, "%" PRId64, expression->value.integer_value) >= 0;
    }
    return fprintf(file,
                   "__minic_deferred_asm_immediate_%zu_%zu",
                   (size_t)inline_asm_id,
                   operand_index) >= 0;
}
'''
new_emit = '''static const MinicExpression *strip_symbolic_immediate_wrappers(const MinicC0Program *program,
                                                               const MinicExpression *expression) {
    while (expression != NULL &&
           (expression->kind == MINIC_EXPRESSION_CAST ||
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
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT && minic_type_is_array(expression->type)) {
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
           minic_const_eval_integer(program, minic_default_target_info(), expression_id, &constant) &&
           minic_const_value_as_int64(
               program, minic_default_target_info(), &constant, value);
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
'''
if old_emit in text:
    text = text.replace(old_emit, new_emit, 1)
elif new_emit not in text:
    raise SystemExit("immediate emitter anchor not found")

old_call = '''                if (!emit_immediate_operand(file, program, operand, inline_asm_id, operand_index)) {
                    return false;
                }
'''
new_call = '''                if (!emit_immediate_operand(file,
                                            program,
                                            operand,
                                            inline_asm_id,
                                            operand_index,
                                            inline_asm->is_goto)) {
                    return false;
                }
'''
if old_call in text:
    text = text.replace(old_call, new_call, 1)
elif new_call not in text:
    raise SystemExit("immediate template call anchor not found")

old_deferred = '''        if (expression->kind != MINIC_EXPRESSION_INTEGER &&
            fprintf(file,
                    "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\\n"
                    "  .extern __minic_deferred_asm_immediate_%zu_%zu\\n",
                    (size_t)statement->inline_asm_id,
                    operand_index) < 0) {
            return false;
        }
'''
new_deferred = '''        if (!immediate_operand_is_resolved(program, operand)) {
            if (!inline_asm->is_goto ||
                fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_%zu\\n",
                        (size_t)statement->inline_asm_id,
                        operand_index) < 0) {
                return false;
            }
        }
'''
if old_deferred in text:
    text = text.replace(old_deferred, new_deferred, 1)
elif new_deferred not in text:
    raise SystemExit("deferred immediate anchor not found")

path.write_text(text)
