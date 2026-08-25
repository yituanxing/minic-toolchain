#!/usr/bin/env python3
from pathlib import Path

marker = "M154_SIGNED_BIT_FIELD_UPDATE_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M154 signed bit-field update owner already staged")
    raise SystemExit(0)
if "M153_SIGNED_BIT_FIELD_WRITE_OWNER" not in text:
    raise SystemExit("M154 requires productized M153")

start = text.find("/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE:")
end = text.find("\nstatic MinicCoreLowerStatus lower_scalar_update(", start)
if start < 0 or end < 0:
    raise SystemExit("M154 could not locate bit-field update owner")
body = text[start:end]

body = body.replace(
    "/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE: prefix/postfix ++/-- on a\n"
    "   bit-field cannot use the ordinary addressable-lvalue update path. Evaluate\n"
    "   the member base once, extract the unsigned field from its storage unit,\n"
    "   apply the integer promotion and +/-1, convert the result back to the field\n"
    "   type, then merge it into the original storage unit with one RMW. */\n"
    "static MinicCoreLowerStatus lower_unsigned_bit_field_update(",
    "/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE / M154_SIGNED_BIT_FIELD_UPDATE_OWNER:\n"
    "   prefix/postfix ++/-- on an integer bit-field cannot use the ordinary\n"
    "   addressable-lvalue update path.  Keep the allocation-unit RMW unsigned,\n"
    "   but reconstruct signed field values from their declared width before\n"
    "   promotion and after truncating the updated value. */\n"
    "static MinicCoreLowerStatus lower_integer_bit_field_update(",
    1,
)

old_gate = """        !minic_type_unqualified(operand->type, &value_type) ||\n        !minic_type_is_integer(value_type) || !minic_type_is_unsigned_integer(value_type) ||\n        minic_type_is_bool_integer(value_type) ||\n"""
new_gate = """        !minic_type_unqualified(operand->type, &value_type) ||\n        !minic_type_is_integer(value_type) ||\n        (!core_unsigned_bit_field_semantic_type(context, value_type) &&\n         !minic_type_is_signed_integer(value_type)) ||\n        minic_type_is_bool_integer(value_type) ||\n"""
if body.count(old_gate) != 1:
    raise SystemExit("M154 could not locate bit-field update type gate")
body = body.replace(old_gate, new_gate, 1)

current_anchor = """    current_field = shifted_current;\n    if (!minic_type_equal(storage_type, value_type)) {\n        status = append_integer_conversion(\n            context, operand->span, value_type, current_field, &current_field);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    }\n    status = append_integer_conversion(\n        context, operand->span, promoted_type, current_field, &current_promoted);\n"""
current_new = r'''    current_field = shifted_current;
    if (!minic_type_equal(storage_type, value_type)) {
        status = append_integer_conversion(
            context, operand->span, value_type, current_field, &current_field);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    /* M154_SIGNED_BIT_FIELD_UPDATE_OWNER: the extracted storage bits are an
       unsigned bit pattern.  Reconstruct the signed field value before integer
       promotion, matching M103 signed bit-field reads and preserving postfix
       result semantics. */
    if (minic_type_is_signed_integer(value_type) && field->bit_width < storage_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(storage_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    status = append_integer_conversion(
        context, operand->span, promoted_type, current_field, &current_promoted);
'''
if body.count(current_anchor) != 1:
    raise SystemExit("M154 could not locate extracted-field promotion seam")
body = body.replace(current_anchor, current_new, 1)

mask_anchor = """        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &instruction, &field_storage)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n    }\n    if (bit_offset != 0U) {\n"""
mask_new = r'''        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &field_storage)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    /* Prefix update yields the value actually stored in the bit-field, not an
       untruncated arithmetic temporary.  Rebuild that value from the masked
       storage bits; signed fields then use the same width sign extension. */
    if (minic_type_equal(storage_type, value_type)) {
        updated_value = field_storage;
    } else {
        status = append_integer_conversion(
            context, expression->span, value_type, field_storage, &updated_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (minic_type_is_signed_integer(value_type) && field->bit_width < storage_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(storage_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    if (bit_offset != 0U) {
'''
if body.count(mask_anchor) != 1:
    raise SystemExit("M154 could not locate masked updated-field seam")
body = body.replace(mask_anchor, mask_new, 1)

text = text[:start] + body + text[end:]
old_call = "return lower_unsigned_bit_field_update(\n                context, expression, operand, increment, prefix, value_id);"
new_call = "return lower_integer_bit_field_update(\n                context, expression, operand, increment, prefix, value_id);"
if text.count(old_call) != 1:
    raise SystemExit("M154 could not locate scalar-update bit-field dispatch")
text = text.replace(old_call, new_call, 1)
path.write_text(text)

Path("tests/compiler/c0/m154_signed_bit_field_update.c").write_text(r'''struct signed_bits {
    unsigned int tag : 2;
    int depth : 30;
};

int post_inc(struct signed_bits *p) { return p->depth++; }
int pre_inc(struct signed_bits *p) { return ++p->depth; }
int post_dec(struct signed_bits *p) { return p->depth--; }
int pre_dec(struct signed_bits *p) { return --p->depth; }

struct unsigned_bits { unsigned int value : 3; };
unsigned int unsigned_pre(struct unsigned_bits *p) { return ++p->value; }
''')
print("staged M154 signed bit-field scalar update owner")
