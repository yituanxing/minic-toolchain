#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


helpers = r'''
static MinicCoreLowerStatus spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || object_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || value_id >= context->function->value_count ||
        !minic_type_equal(context->function->values[value_id].type, type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_object(context->function, span, type, object_id) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = false;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''

replace_once(
    "src/core/core_lower.c",
    '''static MinicCoreLowerStatus lower_scalar_equality_operands(MinicCoreLowerContext *context,
''',
    helpers + '''static MinicCoreLowerStatus lower_scalar_equality_operands(MinicCoreLowerContext *context,
''',
    "Core M19 scalar preservation helpers",
)

replace_once(
    "src/core/core_lower.c",
    '''    MinicCoreValueId left_source;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;
''',
    '''    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;
''',
    "Core M19 equality preservation declarations",
)

replace_once(
    "src/core/core_lower.c",
    '''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (left_source >= context->function->value_count ||
        right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type) ||
        !minic_type_equal(context->function->values[right_source].type, right_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!pointer_comparison) {
        *left_value = left_source;
        *right_value = right_source;
        return MINIC_CORE_LOWER_OK;
    }
    status = append_scalar_bitcast(
        context, left_expression->span, comparison_type, left_source, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    return append_scalar_bitcast(
        context, right_expression->span, comparison_type, right_source, right_value);
''',
    '''    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (left_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        left_normalized = left_source;
    }
    status = spill_scalar_value(
        context, left_expression->span, comparison_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[right_source].type, right_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        right_normalized = right_source;
    }
    status = reload_scalar_value(
        context, left_expression->span, comparison_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
''',
    "Core M19 equality operand preservation",
)

replace_once(
    "tests/compiler/c0/core_logical_and_value.c",
    '''int core_m19_list_empty_careful_shape(const struct core_m19_node *head) {
''',
    '''int core_m19_equality_cfg_rhs(const struct core_m19_node *left,
                              const struct core_m19_node *right,
                              int gate) {
    return left == ({
        do {
            if (gate == 0)
                gate = 1;
        } while (0);
        right;
    });
}

int core_m19_list_empty_careful_shape(const struct core_m19_node *head) {
''',
    "Core M19 equality CFG regression",
)

replace_once(
    "tests/compiler/c0/run-core-logical-and-value.sh",
    "              core_m19_cfg_initializer core_m19_list_empty_careful_shape; do\n",
    "              core_m19_cfg_initializer core_m19_equality_cfg_rhs core_m19_list_empty_careful_shape; do\n",
    "Core M19 equality CFG symbol contract",
)

print("staged M19 Core scalar preservation across CFG")
