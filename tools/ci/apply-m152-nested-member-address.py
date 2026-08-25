#!/usr/bin/env python3
from pathlib import Path

marker = "M152_NESTED_RECORD_MEMBER_ADDRESS_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M152 nested record member address already staged")
    raise SystemExit(0)
if "M151_INDIRECT_CALL_BATCH_OWNER" not in text:
    raise SystemExit("M152 requires the productized M151 Core baseline")

function_start = text.find("static MinicCoreLowerStatus lower_address(")
function_end = text.find("\nstatic MinicCoreLowerStatus append_integer_conversion(", function_start)
if function_start < 0 or function_end < 0:
    raise SystemExit("M152 could not locate lower_address bounds")
body = text[function_start:function_end]
member_start = body.find("    if (expression->kind == MINIC_EXPRESSION_MEMBER) {")
final_return = body.rfind("    return MINIC_CORE_LOWER_UNSUPPORTED;\n}")
if member_start < 0 or final_return < 0 or member_start >= final_return:
    raise SystemExit("M152 could not locate the lower_address MEMBER owner")

new_member = r'''    /* M152_NESTED_RECORD_MEMBER_ADDRESS_OWNER: `p->inner.field` reaches
       the outer field through an addressable record lvalue (`p->inner`), not
       through a scalar pointer value.  Preserve the existing pointer-base path
       for `p->field`, and recursively form the address of a record-lvalue base
       before applying the next FIELD_ADDRESS.  This keeps source-language
       member composition in Core lowering while DataLayout/backend continue to
       own concrete field offsets. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_id;
        MinicType base_value_type;
        MinicType record_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record =
            minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base == NULL || record == NULL || field == NULL || field->is_bit_field) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }

        if (base->value_category == MINIC_VALUE_LVALUE &&
            minic_type_is_record(base->type)) {
            if (base->type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.member.base, &base_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (base_id >= context->function->value_count ||
                !minic_type_is_pointer(context->function->values[base_id].type) ||
                !minic_type_pointee(context->function->values[base_id].type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else {
            /* M94_MEMBER_BASE_VALUE_TYPE: selecting a pointer member through
               `const struct *` qualifies the member lvalue storage, while
               evaluating it yields the unqualified pointer value. */
            if (!core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(context, expression->value.member.base, &base_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (base_id >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        return append_field_address(context,
                                    expression->span,
                                    base_id,
                                    expression->value.member.record_id,
                                    expression->value.member.field_index,
                                    expression->type,
                                    address_id);
    }
'''
body = body[:member_start] + new_member + body[final_return:]
text = text[:function_start] + body + text[function_end:]
path.write_text(text)

regression = Path("tests/compiler/c0/m152_nested_member_address.c")
regression.write_text(r'''struct inner_state {
    int state;
    unsigned long depth;
};

struct outer_state {
    int prefix;
    struct inner_state iter;
};

int nested_member_address(struct outer_state *st) {
    st->iter.state = 7;
    st->iter.depth = 9;
    return st->iter.state + (int)st->iter.depth;
}
''')
print("staged M152 nested record member address owner")
