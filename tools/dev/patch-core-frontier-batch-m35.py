#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


old = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        return lower_postfix_scalar_update(context, expression, value_id);
    }
'''

new = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        const MinicExpression *operand;
        MinicCoreInstruction update_instruction;
        MinicCoreValueId address_value;
        MinicCoreValueId old_value;
        MinicCoreValueId one_value;
        MinicCoreValueId updated_value;
        MinicCoreLowerStatus status;
        MinicType pointer_type;
        MinicType value_type;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(operand->type) || minic_type_is_bool_integer(operand->type) ||
            minic_type_is_const(operand->type) || minic_type_is_volatile(operand->type) ||
            !minic_type_unqualified(operand->type, &value_type) ||
            !minic_type_equal(value_type, operand->type) ||
            !minic_type_equal(expression->type, value_type) ||
            !minic_type_pointer_to(value_type, &pointer_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }

        status = lower_address(context, expression->value.unary.operand, &address_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (address_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[address_value].type, pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&update_instruction, 0, sizeof(update_instruction));
        update_instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        update_instruction.span = expression->span;
        update_instruction.type = value_type;
        update_instruction.result = MINIC_CORE_VALUE_INVALID;
        update_instruction.value.load.address = address_value;
        update_instruction.value.load.is_volatile = false;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &update_instruction, &old_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&update_instruction, 0, sizeof(update_instruction));
        update_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        update_instruction.span = expression->span;
        update_instruction.type = value_type;
        update_instruction.result = MINIC_CORE_VALUE_INVALID;
        update_instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &update_instruction, &one_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&update_instruction, 0, sizeof(update_instruction));
        update_instruction.kind =
            expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT
                ? MINIC_CORE_INSTRUCTION_INTEGER_ADD
                : MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
        update_instruction.span = expression->span;
        update_instruction.type = value_type;
        update_instruction.result = MINIC_CORE_VALUE_INVALID;
        update_instruction.value.binary.left = old_value;
        update_instruction.value.binary.right = one_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &update_instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&update_instruction, 0, sizeof(update_instruction));
        update_instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        update_instruction.span = expression->span;
        update_instruction.type = minic_type_void();
        update_instruction.result = MINIC_CORE_VALUE_INVALID;
        update_instruction.value.store.address = address_value;
        update_instruction.value.store.stored_value = updated_value;
        update_instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &update_instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = updated_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        return lower_postfix_scalar_update(context, expression, value_id);
    }
'''

replace_once("src/core/core_lower.c", old, new)
print("M35_PATCH_APPLIED")
