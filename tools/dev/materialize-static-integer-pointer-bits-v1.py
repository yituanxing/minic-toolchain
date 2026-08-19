#!/usr/bin/env python3
from pathlib import Path

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
start_marker = "static bool static_pointer_integer_constant_bits("
end_marker = "static bool static_pointer_initializer_from_expression("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("unexpected static pointer integer constant owner shape")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicConstValue constant;
    MinicType effective_type;
    const MinicDataLayout *layout;
    unsigned int source_bits;
    unsigned int pointer_bits;
    uint64_t value_bits;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    while (expression != NULL && expression->kind == MINIC_EXPRESSION_CONVERSION) {
        expression = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_is_pointer(expression->type)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (operand == NULL || !minic_type_is_integer(operand->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression->value.unary.operand, &constant) ||
        !minic_c0_type_effective_integer_type(parser->program, constant.type, &effective_type) ||
        !minic_target_info_integer_width(
            parser->target_info, parser->program, effective_type, &source_bits) ||
        source_bits == 0U || source_bits > 64U) {
        return false;
    }

    value_bits = constant.bits;
    if (source_bits < 64U) {
        const uint64_t source_mask = (UINT64_C(1) << source_bits) - UINT64_C(1);

        value_bits &= source_mask;
        if (minic_type_is_signed_integer(effective_type) &&
            (value_bits & (UINT64_C(1) << (source_bits - 1U))) != 0U) {
            value_bits |= ~source_mask;
        }
    }

    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *bits = value_bits;
    if (pointer_bits < 64U) {
        *bits &= (UINT64_C(1) << pointer_bits) - UINT64_C(1);
    }
    return true;
}

'''
parser.write_text(text[:start] + replacement + text[end:])

case = Path("tests/compiler/c0/static_object_address_relocation.c")
text = case.read_text()
anchor = '''static char *nested_member_array_element_address = &subobject_address_target.nested.bytes[3];
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected static pointer bit-pattern test anchor")
addition = r'''
static void *signed_minus_one_pointer = (void *)-1;
static void *unsigned_32_pointer = (void *)0xffffffffU;
static void *high_unsigned_pointer = (void *)(0xdead000000000000UL + 0x300UL);
'''
case.write_text(text.replace(anchor, anchor + addition, 1))

runner = Path("tests/compiler/c0/run-static-object-address-relocation.sh")
text = runner.read_text()
anchor = '''grep -F '.dword subobject_address_target+24' "$work/static_object_address_relocation.s" >/dev/null
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected static pointer bit-pattern runner anchor")
addition = '''grep -F '.dword -1' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword 4294967295' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword -2401263026318605568' "$work/static_object_address_relocation.s" >/dev/null
'''
runner.write_text(text.replace(anchor, anchor + addition, 1))

print("materialized target-width static integer pointer bit patterns")
