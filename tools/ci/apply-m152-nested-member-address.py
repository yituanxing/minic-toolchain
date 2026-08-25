#!/usr/bin/env python3
from pathlib import Path

marker = "M153_SIGNED_BIT_FIELD_WRITE_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M153 signed bit-field write owner already staged")
    raise SystemExit(0)
if "M152_UNSIGNED_ENUM_BIT_FIELD_OWNER" not in text:
    raise SystemExit("M153 requires productized M152")

function_start = text.find("static MinicCoreLowerStatus lower_assignment_pair(")
function_end = text.find("\nstatic MinicCoreLowerStatus lower_assignment(", function_start)
if function_start < 0 or function_end < 0:
    raise SystemExit("M153 could not locate lower_assignment_pair")
body = text[function_start:function_end]

gate_old = """                !minic_type_is_integer(value_type) ||\n                !core_unsigned_bit_field_semantic_type(context, value_type) ||\n                minic_type_is_const(target->type) ||\n"""
gate_new = """                !minic_type_is_integer(value_type) ||\n                (!core_unsigned_bit_field_semantic_type(context, value_type) &&\n                 !minic_type_is_signed_integer(value_type)) ||\n                minic_type_is_const(target->type) ||\n"""
if body.count(gate_old) != 1:
    raise SystemExit("M153 could not locate simple bit-field gate")
body = body.replace(gate_old, gate_new, 1)

assigned_old = """            if (minic_type_equal(storage_type, value_type)) {\n                assigned_value = field_storage;\n            } else {\n                bit_status = append_integer_conversion(\n                    context, span, value_type, field_storage, &assigned_value);\n                if (bit_status != MINIC_CORE_LOWER_OK) {\n                    return bit_status;\n                }\n            }\n"""
assigned_new = r'''            if (minic_type_equal(storage_type, value_type)) {
                assigned_value = field_storage;
            } else {
                bit_status = append_integer_conversion(
                    context, span, value_type, field_storage, &assigned_value);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            /* M153_SIGNED_BIT_FIELD_WRITE_OWNER: storage is merged through an
               unsigned allocation unit.  For a signed field, reconstruct the
               assignment-expression value from the truncated field bits using
               the same shift-left/arithmetic-shift-right sign extension as the
               established M103 read path.  The stored bits themselves remain
               the masked two's-complement representation. */
            if (minic_type_is_signed_integer(value_type) &&
                field->bit_width < storage_width) {
                MinicCoreValueId shift;
                uint64_t shift_bits = (uint64_t)(storage_width - field->bit_width);

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = minic_type_unsigned_int();
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &shift)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
'''
if body.count(assigned_old) != 1:
    raise SystemExit("M153 could not locate assignment-result conversion")
body = body.replace(assigned_old, assigned_new, 1)
text = text[:function_start] + body + text[function_end:]
path.write_text(text)

regression = Path("tests/compiler/c0/m153_signed_bit_field_write.c")
regression.write_text(r'''struct signed_bits {
    unsigned int tag : 2;
    int depth : 30;
};

void clear_depth(struct signed_bits *p) {
    p->depth = 0;
}

void set_negative_depth(struct signed_bits *p) {
    p->depth = -1;
}

int assigned_signed_depth(struct signed_bits *p) {
    return (p->depth = -3);
}
''')
print("staged M153 signed bit-field write owner")
