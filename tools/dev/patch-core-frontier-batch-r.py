#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
anchor = '''            *value_id = current;
            return MINIC_CORE_LOWER_OK;
        }
    }
    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
'''
insert = '''            *value_id = current;
            return MINIC_CORE_LOWER_OK;
        }
    }
    /* BATCH_R_RECORD_CALL_MEMBER_VALUE: projecting a scalar field from a
       direct aggregate-returning call consumes the call's existing result
       object. Form its address, project the field, then load the scalar value.
       This is a generic aggregate-rvalue projection; indirect aggregate calls
       and bit-field projections remain fail-closed until their own seams exist. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        core_memory_scalar_type(expression->type)) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreObjectId result_object;
        MinicCoreValueId base_address;
        MinicCoreValueId field_address;
        MinicCoreLowerStatus status;
        MinicType pointer_type;
        MinicType value_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base != NULL && base->kind == MINIC_EXPRESSION_CALL &&
            base->value.call.function_id != MINIC_FUNCTION_INVALID &&
            minic_type_is_record(base->type) &&
            base->type.record_id == expression->value.member.record_id &&
            record != NULL && field != NULL && !field->is_bit_field &&
            minic_type_unqualified(expression->type, &value_type) &&
            core_memory_scalar_type(value_type)) {
            status = lower_direct_record_call_object(context, base, &result_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (!minic_type_pointer_to(base->type, &pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = base->span;
            instruction.type = pointer_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = result_object;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &base_address)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = append_field_address(context,
                                          expression->span,
                                          base_address,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          expression->type,
                                          &field_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = expression->span;
            instruction.type = value_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = field_address;
            instruction.value.load.is_volatile = minic_type_is_volatile(expression->type);
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
'''
if anchor not in text:
    if "BATCH_R_RECORD_CALL_MEMBER_VALUE" in text:
        print("CORE_BATCH_R_ALREADY_PATCHED")
        raise SystemExit(0)
    raise SystemExit("Batch R anchor not found")
text = text.replace(anchor, insert, 1)
path.write_text(text)
print("CORE_BATCH_R_PATCHED direct record-call member projection")
