#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'M32 {label}: expected one anchor, found {count}')
    p.write_text(text.replace(old, new, 1))


replace_once(
    'src/target/riscv64/core_codegen.c',
    '''    return operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY &&
           core_inline_asm_constraint_is(operand, "r") &&
           (minic_type_is_integer(function->values[operand->value].type) ||
            minic_type_is_pointer(function->values[operand->value].type));
''',
    '''    return operand->access == MINIC_CORE_INLINE_ASM_OPERAND_READ_ONLY &&
           (core_inline_asm_constraint_is(operand, "r") ||
            core_inline_asm_constraint_is(operand, "rK") ||
            core_inline_asm_constraint_is(operand, "rJ") ||
            core_inline_asm_constraint_is(operand, "Jr")) &&
           (minic_type_is_integer(function->values[operand->value].type) ||
            minic_type_is_pointer(function->values[operand->value].type));
''',
    'alternative-register constraints',
)

replace_once(
    'src/core/core_lower.c',
    '''        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_block = minic_c0_program_block(context->body->program,
                                                 expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
''',
    '''        statement_block = minic_c0_program_block(context->body->program,
                                                 expression->value.statement_expression.block);
        if (statement_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            if (!minic_type_is_void(expression->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_block(context, statement_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            *value_id = MINIC_CORE_VALUE_INVALID;
            return MINIC_CORE_LOWER_OK;
        }
        if (!core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_result == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
''',
    'void statement expression',
)

replace_once(
    'src/core/core_lower.c',
    '''        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !minic_type_is_integer(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
''',
    '''        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_memory_scalar_type(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
''',
    'conditional result scalar family',
)

replace_once(
    'src/core/core_lower.c',
    '''        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||
            !minic_type_is_integer(true_type) || !minic_type_is_integer(false_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
    '''        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||
            !((minic_type_is_integer(expression->type) && minic_type_is_integer(true_type) &&
               minic_type_is_integer(false_type)) ||
              (minic_type_is_pointer(expression->type) && minic_type_equal(true_type, expression->type) &&
               minic_type_equal(false_type, expression->type)))) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
    'conditional scalar type family',
)

for arm in ('true', 'false'):
    label = 'true_expression' if arm == 'true' else 'false_expression'
    old = f'''        status = append_integer_conversion(
            context, {label}->span, expression->type, arm_value, &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {{
            return status;
        }}
        status = store_scalar_value(
'''
    new = f'''        if (minic_type_is_integer(expression->type)) {{
            status = append_integer_conversion(
                context, {label}->span, expression->type, arm_value, &arm_value);
            if (status != MINIC_CORE_LOWER_OK) {{
                return status;
            }}
        }} else if (arm_value >= context->function->value_count ||
                   !minic_type_equal(context->function->values[arm_value].type, expression->type)) {{
            return MINIC_CORE_LOWER_ERROR;
        }}
        status = store_scalar_value(
'''
    replace_once('src/core/core_lower.c', old, new, f'conditional {arm} arm')

print('M32_PATCH_APPLIED')
