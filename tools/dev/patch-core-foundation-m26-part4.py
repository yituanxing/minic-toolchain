from patch_core_m26_lib import insert_before, replace_once

# Shift left operand preservation across a CFG-producing RHS.
replace_once(
    'src/core/core_lower.c',
    '''        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId right;\n''',
    '''        MinicCoreObjectId left_object;\n        MinicCoreValueId left;\n        MinicCoreValueId left_source;\n        MinicCoreValueId right;\n''')
replace_once(
    'src/core/core_lower.c',
    '''        status = append_integer_conversion(\n            context, left_expression->span, expression->type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n''',
    '''        status = append_integer_conversion(\n            context, left_expression->span, expression->type, left_source, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, left_expression->span, expression->type, left, &left_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, left_expression->span, expression->type, left_object, &left);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (left >= context->function->value_count || right >= context->function->value_count ||\n''')

# Generalize the existing &= lowering to |= and preserve both address/current across RHS CFG.
replace_once(
    'src/core/core_lower.c',
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {\n''',
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||\n         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {\n''')
replace_once(
    'src/core/core_lower.c',
    '''        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId current_common;\n''',
    '''        MinicCoreObjectId address_object;\n        MinicCoreObjectId current_object;\n        MinicCoreValueId address;\n        MinicCoreValueId current;\n        MinicCoreValueId current_common;\n''')
replace_once(
    'src/core/core_lower.c',
    '''        MinicCoreLowerStatus status;\n        MinicType common_type;\n        MinicType stored_type;\n''',
    '''        MinicCoreLowerStatus status;\n        MinicType address_type;\n        MinicType common_type;\n        MinicType stored_type;\n''')
replace_once(
    'src/core/core_lower.c',
    '''        status =\n            append_integer_conversion(context, target->span, common_type, current, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n''',
    '''        status =\n            append_integer_conversion(context, target->span, common_type, current, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (address >= context->function->value_count) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        address_type = context->function->values[address].type;\n        status = spill_scalar_value(\n            context, target->span, address_type, address, &address_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = spill_scalar_value(\n            context, target->span, common_type, current_common, &current_object);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = lower_expression(context, expression->value.binary.right, &right);\n''')
replace_once(
    'src/core/core_lower.c',
    '''        status =\n            append_integer_conversion(context, source->span, common_type, right, &right_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;\n''',
    '''        status =\n            append_integer_conversion(context, source->span, common_type, right, &right_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, target->span, common_type, current_object, &current_common);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        status = reload_scalar_value(\n            context, target->span, address_type, address_object, &address);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind =\n            expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND\n                ? MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND\n                : MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;\n''')

# Persist assignment RHS before a potentially CFG-producing lvalue address calculation.
replace_once(
    'src/core/core_lower.c',
    '''    MinicCoreInstruction instruction;\n    MinicCoreValueId address_id;\n    MinicCoreValueId stored_value;\n    MinicCoreLowerStatus status;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL) {\n''',
    '''    MinicCoreInstruction instruction;\n    MinicCoreObjectId stored_object;\n    MinicCoreValueId address_id;\n    MinicCoreValueId stored_value;\n    MinicCoreLowerStatus status;\n    MinicType stored_type;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL) {\n''')
replace_once(
    'src/core/core_lower.c',
    '''    {\n        MinicType stored_type;\n\n        if (!minic_type_unqualified(target->type, &stored_type) ||\n            !core_memory_scalar_type(stored_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);\n    }\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = lower_address(context, target_id, &address_id);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n''',
    '''    if (!minic_type_unqualified(target->type, &stored_type) ||\n        !core_memory_scalar_type(stored_type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = lower_address(context, target_id, &address_id);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n''')

# Permanent C0 gate registration for the batched semantic/runtime contract.
replace_once(
    '.github/scripts/compiler-c0-full-gate.sh',
    '''core_integer_binary_preservation_m25b_focused() {\n    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-integer-binary-preservation-m25b" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh\n}\n\nruntime_record_fam_prefix_focused() {\n''',
    '''core_integer_binary_preservation_m25b_focused() {\n    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-integer-binary-preservation-m25b" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh\n}\n\ncore_integer_foundation_m26_focused() {\n    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-integer-foundation-m26" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-foundation-m26.sh\n}\n\nruntime_record_fam_prefix_focused() {\n''')
replace_once(
    '.github/scripts/compiler-c0-full-gate.sh',
    '''start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n''',
    '''start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused\nstart_gate core-integer-foundation-m26-focused core_integer_foundation_m26_focused\nstart_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n''')
