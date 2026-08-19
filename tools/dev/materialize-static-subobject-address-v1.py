#!/usr/bin/env python3
from pathlib import Path

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
start_marker = "static bool static_object_address_relocation_path("
end_marker = "static bool static_pointer_integer_constant_bits("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("unexpected static object relocation owner shape")
if "static_object_subobject_relocation_path" in text:
    raise SystemExit("static subobject relocation helper already present")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''static bool static_object_subobject_relocation_path(
    const MinicParser *parser,
    MinicExpressionId expression_id,
    MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        if (expression->value.global_object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            minic_c0_program_global_object(parser->program, expression->value.global_object_id) ==
                NULL) {
            return false;
        }
        (void)memset(target, 0, sizeof(*target));
        target->object_id = expression->value.global_object_id;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        MinicExpressionId base_id;

        base_id = expression->value.member.base;
        base = minic_c0_program_expression(parser->program, base_id);
        if (base == NULL || base->kind != MINIC_EXPRESSION_ADDRESS_OF) {
            return false;
        }
        base_id = base->value.unary.operand;
        if (!static_object_subobject_relocation_path(parser, base_id, target) ||
            target->member_depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        target->member_indices[target->member_depth++] = expression->value.member.field_index;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        MinicArrayObjectInfo array_info;
        int64_t delta;

        base = minic_c0_program_expression(parser->program, expression->value.subscript.base);
        if (base == NULL ||
            !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
            !static_object_subobject_relocation_path(
                parser, expression->value.subscript.base, target) ||
            !static_pointer_offset_bytes(parser,
                                         array_info.element_type,
                                         expression->value.subscript.index,
                                         false,
                                         &delta) ||
            !static_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return false;
        }
        return true;
    }
    return false;
}

static bool static_object_address_relocation_path(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    program = parser->program;
    if (static_object_address_relocation_target(
            parser, expression_id, &target->object_id, &target->byte_addend)) {
        target->member_depth = 0U;
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        MinicType pointee_type;
        int64_t delta;

        left = minic_c0_program_expression(program, expression->value.binary.left);
        if (left != NULL && minic_type_pointee(left->type, &pointee_type) &&
            static_object_address_relocation_path(
                parser, expression->value.binary.left, target) &&
            static_pointer_offset_bytes(parser,
                                        pointee_type,
                                        expression->value.binary.right,
                                        expression->value.binary.operator_kind ==
                                            MINIC_BINARY_SUBTRACT,
                                        &delta) &&
            static_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
            const MinicExpression *right;

            right = minic_c0_program_expression(program, expression->value.binary.right);
            if (right != NULL && minic_type_pointee(right->type, &pointee_type) &&
                static_object_address_relocation_path(
                    parser, expression->value.binary.right, target) &&
                static_pointer_offset_bytes(parser,
                                            pointee_type,
                                            expression->value.binary.left,
                                            false,
                                            &delta) &&
                static_add_pointer_offset(
                    target->byte_addend, delta, &target->byte_addend)) {
                return true;
            }
        }
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return static_object_subobject_relocation_path(
            parser, expression->value.unary.operand, target);
    }
    {
        MinicArrayObjectInfo array_info;

        if (minic_c0_expression_array_object_info(program, expression, &array_info)) {
            return static_object_subobject_relocation_path(parser, expression_id, target);
        }
    }
    return false;
}

'''
parser.write_text(text[:start] + replacement + text[end:])

case = Path("tests/compiler/c0/static_object_address_relocation.c")
text = case.read_text()
anchor = '''static struct FunctionAddressHolder aggregate_array_nine = {
    (void *)&global_address_array[9],
};
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected static object address test anchor")
addition = r'''
struct NestedSubobjectAddressTarget {
    char tag;
    char bytes[5];
};

struct SubobjectAddressTarget {
    int prefix;
    int values[4];
    struct NestedSubobjectAddressTarget nested;
};

static struct SubobjectAddressTarget subobject_address_target;
static int *member_array_decay_address = subobject_address_target.values;
static int *member_array_element_address = &subobject_address_target.values[2];
static char *nested_member_array_decay_address = subobject_address_target.nested.bytes;
static char *nested_member_array_element_address = &subobject_address_target.nested.bytes[3];
'''
case.write_text(text.replace(anchor, anchor + addition, 1))

runner = Path("tests/compiler/c0/run-static-object-address-relocation.sh")
text = runner.read_text()
anchor = '''external_count=$(grep -F -c '.dword external_address_target' "$work/static_object_address_relocation.s")
test "$external_count" -eq 2
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected static object address runner anchor")
addition = '''grep -F '.dword subobject_address_target+4' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+12' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+21' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+24' "$work/static_object_address_relocation.s" >/dev/null
'''
runner.write_text(text.replace(anchor, anchor + addition, 1))

print("materialized canonical static subobject address relocation")
