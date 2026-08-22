#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Direct-call arguments: preserve each already-evaluated scalar across later argument CFG.
replace_once(
    "src/core/core_lower.c",
    """    MinicCoreInstruction instruction;\n    MinicCoreValueId *arguments;\n    MinicCoreLowerStatus status;\n""",
    """    MinicCoreInstruction instruction;\n    MinicCoreValueId *arguments;\n    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];\n    MinicCoreLowerStatus status;\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        if (arguments[argument_index] >= context->function->value_count ||\n            !minic_type_equal(context->function->values[arguments[argument_index]].type,\n                              callee->parameter_types[argument_index])) {\n            free(arguments);\n            return MINIC_CORE_LOWER_ERROR;\n        }\n    }\n    if (!minic_core_function_add_callee(context->function,\n""",
    """        if (arguments[argument_index] >= context->function->value_count ||\n            !minic_type_equal(context->function->values[arguments[argument_index]].type,\n                              callee->parameter_types[argument_index])) {\n            free(arguments);\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = spill_scalar_value(context,\n                                    expression->span,\n                                    callee->parameter_types[argument_index],\n                                    arguments[argument_index],\n                                    &argument_objects[argument_index]);\n        if (status != MINIC_CORE_LOWER_OK) {\n            free(arguments);\n            return status;\n        }\n    }\n    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {\n        status = reload_scalar_value(context,\n                                     expression->span,\n                                     callee->parameter_types[argument_index],\n                                     argument_objects[argument_index],\n                                     &arguments[argument_index]);\n        if (status != MINIC_CORE_LOWER_OK) {\n            free(arguments);\n            return status;\n        }\n    }\n    if (!minic_core_function_add_callee(context->function,\n""",
)

# Pointer arithmetic base: preserve it across a CFG-producing integer index.
replace_once(
    "src/core/core_lower.c",
    """        MinicExpressionId pointer_id;\n        MinicExpressionId index_id;\n        MinicCoreValueId pointer_value;\n        MinicCoreValueId index_value;\n""",
    """        MinicExpressionId pointer_id;\n        MinicExpressionId index_id;\n        MinicCoreObjectId pointer_object;\n        MinicCoreValueId pointer_value;\n        MinicCoreValueId index_value;\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status = lower_expression(context, pointer_id, &pointer_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, index_id, &index_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (pointer_value >= context->function->value_count ||\n""",
    """        status = lower_expression(context, pointer_id, &pointer_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, pointer_expression->span, pointer_expression->type, pointer_value, &pointer_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, index_id, &index_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, pointer_expression->span, pointer_expression->type, pointer_object, &pointer_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (pointer_value >= context->function->value_count ||\n""",
)

# Shift left operand: preserve normalized value across a CFG-producing RHS.
replace_once(
    "src/core/core_lower.c",
    """        const MinicExpression *left_expression;\n        const MinicExpression *right_expression;\n        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n\n        if (!minic_type_is_integer(expression->type)) {\n""",
    """        const MinicExpression *left_expression;\n        const MinicExpression *right_expression;\n        MinicCoreObjectId left_object;\n        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId right;\n        MinicCoreLowerStatus status;\n\n        if (!minic_type_is_integer(expression->type)) {\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status = append_integer_conversion(\n            context, left_expression->span, expression->type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n""",
    """        status = append_integer_conversion(\n            context, left_expression->span, expression->type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, left_expression->span, expression->type, left, &left_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, left_expression->span, expression->type, left_object, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n""",
)

# Compound &= / |=: preserve lvalue address and current common value across RHS CFG.
replace_once(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {\n""",
    """    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||\n         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId current_common;\n""",
    """        MinicCoreObjectId address_object;\n        MinicCoreObjectId current_object;\n        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId current_common;\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        MinicCoreLowerStatus status;\n        MinicType common_type;\n        MinicType stored_type;\n""",
    """        MinicCoreLowerStatus status;\n        MinicType address_type;\n        MinicType common_type;\n        MinicType stored_type;\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status =\n            append_integer_conversion(context, target->span, common_type, current, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n""",
    """        status =\n            append_integer_conversion(context, target->span, common_type, current, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (address >= context->function->value_count) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        address_type = context->function->values[address].type;\n        status = spill_scalar_value(\n            context, target->span, address_type, address, &address_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, target->span, common_type, current_common, &current_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status =\n            append_integer_conversion(context, source->span, common_type, right, &right_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;\n""",
    """        status =\n            append_integer_conversion(context, source->span, common_type, right, &right_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, target->span, common_type, current_object, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, target->span, address_type, address_object, &address);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind =\n            expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND\n                ? MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND\n                : MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;\n""",
)

# Plain assignment: RHS must survive a later lvalue-address CFG.
replace_once(
    "src/core/core_lower.c",
    """    MinicCoreInstruction instruction;\n    MinicCoreValueId address_id;\n    MinicCoreValueId stored_value;\n    MinicCoreLowerStatus status;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL) {\n""",
    """    MinicCoreInstruction instruction;\n    MinicCoreObjectId stored_object;\n    MinicCoreValueId address_id;\n    MinicCoreValueId stored_value;\n    MinicCoreLowerStatus status;\n    MinicType stored_type;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL) {\n""",
)
replace_once(
    "src/core/core_lower.c",
    """    {\n        MinicType stored_type;\n\n        if (!minic_type_unqualified(target->type, &stored_type) ||\n            !core_memory_scalar_type(stored_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);\n    }\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = lower_address(context, target_id, &address_id);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n""",
    """    if (!minic_type_unqualified(target->type, &stored_type) ||\n        !core_memory_scalar_type(stored_type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = lower_address(context, target_id, &address_id);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n""",
)

# Overflow builtin: both arithmetic operands must survive later CFG and result-pointer lowering.
replace_once(
    "src/core/core_lower.c",
    """        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId result_address;\n        MinicCoreValueId right;\n        MinicCoreValueId right_source;\n""",
    """        MinicCoreObjectId left_object;\n        MinicCoreObjectId right_object;\n        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId result_address;\n        MinicCoreValueId right;\n        MinicCoreValueId right_source;\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status = append_integer_conversion(\n            context, left_expression->span, result_type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.overflow.right, &right_source);\n""",
    """        status = append_integer_conversion(\n            context, left_expression->span, result_type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, left_expression->span, result_type, left, &left_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.overflow.right, &right_source);\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        status = append_integer_conversion(\n            context, right_expression->span, result_type, right_source, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status =\n            lower_expression(context, expression->value.overflow.result_pointer, &result_address);\n""",
    """        status = append_integer_conversion(\n            context, right_expression->span, result_type, right_source, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, right_expression->span, result_type, right, &right_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status =\n            lower_expression(context, expression->value.overflow.result_pointer, &result_address);\n""",
)
replace_once(
    "src/core/core_lower.c",
    """        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n            result_address >= context->function->value_count ||\n""",
    """        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, left_expression->span, result_type, left_object, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, right_expression->span, result_type, right_object, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n            result_address >= context->function->value_count ||\n""",
)

# Permanent focused contract registration.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """core_integer_less_m26_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-integer-less-m26\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-integer-less-m26.sh\n}\n\nruntime_record_fam_prefix_focused() {\n""",
    """core_integer_less_m26_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-integer-less-m26\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-integer-less-m26.sh\n}\n\ncore_integer_foundation_m26b_focused() {\n    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n    BUILD_DIR=\"$root/build/ci-core-integer-foundation-m26b\" \\\n    RISCV_CC=riscv64-linux-gnu-gcc \\\n    QEMU_RISCV64=qemu-riscv64 \\\n        sh tests/compiler/c0/run-core-integer-foundation-m26b.sh\n}\n\nruntime_record_fam_prefix_focused() {\n""",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate core-integer-less-m26-focused core_integer_less_m26_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n""",
    """start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate core-integer-less-m26-focused core_integer_less_m26_focused\nstart_gate core-integer-foundation-m26b-focused core_integer_foundation_m26b_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n""",
)
