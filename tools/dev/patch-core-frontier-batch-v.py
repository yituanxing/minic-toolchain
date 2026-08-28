#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* BATCH_J_DIRECT_RECORD_CALL_ARGUMENT: a direct record-returning call
'''
new = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* BATCH_V_TRANSPARENT_UNION_ARGUMENT: GNU transparent-union legality is
       already owned by frontend/Sema.  When a fixed argument is accepted via
       one of the union's pointer members, materialize the semantic union as a
       private Core object and initialize the matching member.  The existing
       OBJECT call argument then preserves the declared Core signature and the
       ordinary aggregate ABI path; Core does not re-define the language rule. */
    if (!minic_type_is_record(expression->type)) {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId object_address;
        MinicCoreValueId field_address;
        MinicCoreValueId field_value;
        MinicType abi_type;
        size_t field_index;
        bool found;

        if (!minic_c0_fixed_call_argument_compatible(
                context->body->program, parameter_type, expression_id) ||
            !minic_c0_fixed_parameter_abi_type(
                context->body->program, parameter_type, &abi_type) ||
            !core_memory_scalar_type(abi_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        record = minic_c0_program_record(context->body->program, parameter_type.record_id);
        if (record == NULL || !record->is_complete || !record->is_union ||
            !record->is_transparent_union || record->field_count == 0U) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        field = NULL;
        field_index = 0U;
        found = false;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *candidate;

            candidate = minic_c0_record_field(record, field_index);
            if (candidate == NULL || candidate->is_array || candidate->is_bit_field ||
                !minic_type_is_pointer(candidate->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (minic_c0_assignment_compatible(
                    context->body->program, candidate->type, expression_id)) {
                field = candidate;
                found = true;
                break;
            }
        }
        if (!found || field == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(
            context, field->type, expression_id, &field_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, parameter_type, object_id) ||
            !minic_type_pointer_to(parameter_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = *object_id;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &object_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_field_address(context,
                                      expression->span,
                                      object_address,
                                      parameter_type.record_id,
                                      field_index,
                                      field->type,
                                      &field_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = field_address;
        instruction.value.store.stored_value = field_value;
        instruction.value.store.is_volatile = false;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* BATCH_J_DIRECT_RECORD_CALL_ARGUMENT: a direct record-returning call
'''

if "BATCH_V_TRANSPARENT_UNION_ARGUMENT" in text:
    print("CORE_BATCH_V_ALREADY_PATCHED")
    raise SystemExit(0)
if old not in text:
    raise SystemExit("Batch V record-call argument anchor not found")
text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_V_PATCHED transparent union fixed arguments")
