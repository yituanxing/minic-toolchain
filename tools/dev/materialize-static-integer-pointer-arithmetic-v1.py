#!/usr/bin/env python3
from pathlib import Path

# One target-layout owner for the stride used by pointer arithmetic. Language
# acceptance remains in frontend semantics; this helper only answers the byte
# stride for an already-accepted pointee type.
header = Path("src/target/data_layout.h")
text = header.read_text()
anchor = '''bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment);
'''
addition = anchor + '''bool minic_data_layout_pointer_arithmetic_stride(const MinicDataLayout *layout,
                                                 const MinicC0Program *program,
                                                 MinicType pointee_type,
                                                 size_t *stride);
'''
if text.count(anchor) != 1 or "minic_data_layout_pointer_arithmetic_stride" in text:
    raise SystemExit("unexpected DataLayout header shape")
header.write_text(text.replace(anchor, addition, 1))

layout = Path("src/target/data_layout.c")
text = layout.read_text()
anchor = '''const MinicDataLayout *minic_default_data_layout(void) {
    return &minic_rv64_data_layout;
}
'''
addition = anchor + r'''
bool minic_data_layout_pointer_arithmetic_stride(const MinicDataLayout *layout,
                                                 const MinicC0Program *program,
                                                 MinicType pointee_type,
                                                 size_t *stride) {
    size_t alignment;

    if (layout == NULL || program == NULL || stride == NULL) {
        return false;
    }
    if (minic_type_is_void(pointee_type) || minic_type_is_function(pointee_type)) {
        *stride = 1U;
        return true;
    }
    return minic_data_layout_type(layout, program, pointee_type, stride, &alignment) &&
           *stride != 0U;
}
'''
if text.count(anchor) != 1 or "minic_data_layout_pointer_arithmetic_stride" in text:
    raise SystemExit("unexpected DataLayout implementation shape")
layout.write_text(text.replace(anchor, addition, 1))

# Route RV64 runtime pointer arithmetic through the shared stride owner.
codegen = Path("src/target/riscv64/codegen_expression.c")
text = codegen.read_text()
start_marker = "static bool minic_riscv64_pointer_element_size("
end_marker = "static bool minic_riscv64_emit_normalize_integer("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("unexpected RV64 pointer stride owner shape")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''static bool minic_riscv64_pointer_element_size(const MinicC0Program *program,
                                               MinicType pointer_type,
                                               size_t *element_size) {
    MinicType pointee;

    return program != NULL && element_size != NULL &&
           minic_type_pointee(pointer_type, &pointee) &&
           minic_data_layout_pointer_arithmetic_stride(
               minic_default_data_layout(), program, pointee, element_size);
}

'''
codegen.write_text(text[:start] + replacement + text[end:])

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
# Symbolic object relocation keeps a signed addend, but obtains the byte stride
# from the same owner as runtime lowering.
old = '''    size_t size;
    size_t alignment;
    uint64_t magnitude;
'''
new = '''    size_t size;
    uint64_t magnitude;
'''
if text.count(old) != 1:
    raise SystemExit("unexpected static symbolic offset locals")
text = text.replace(old, new, 1)
old = '''        !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                parser->program,
                                pointee_type,
                                &size,
                                &alignment) ||
        size == 0U) {
        return false;
    }
    (void)alignment;
'''
new = '''        !minic_data_layout_pointer_arithmetic_stride(
            minic_target_info_data_layout(parser->target_info),
            parser->program,
            pointee_type,
            &size)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected static symbolic offset layout query")
text = text.replace(old, new, 1)

# Integer-derived pointer constants are bit patterns, not symbolic relocations.
# Evaluate their integer operands once through ConstEval, extend according to
# the source integer type, and perform pointer +/- modulo the target width.
start_marker = "static bool static_pointer_integer_constant_bits("
end_marker = "static bool static_pointer_initializer_from_expression("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("unexpected static integer-pointer owner shape")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''static bool static_pointer_width(const MinicParser *parser,
                                 unsigned int *pointer_bits,
                                 uint64_t *pointer_mask) {
    const MinicDataLayout *layout;

    if (parser == NULL || pointer_bits == NULL || pointer_mask == NULL) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    *pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *pointer_mask = *pointer_bits == 64U
                        ? UINT64_MAX
                        : (UINT64_C(1) << *pointer_bits) - UINT64_C(1);
    return true;
}

static bool static_integer_constant_pointer_width_bits(const MinicParser *parser,
                                                       MinicExpressionId expression_id,
                                                       uint64_t *bits) {
    MinicConstValue constant;
    MinicType effective_type;
    unsigned int source_bits;
    unsigned int pointer_bits;
    uint64_t pointer_mask;
    uint64_t value_bits;

    if (parser == NULL || bits == NULL ||
        !minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant) ||
        !minic_c0_type_effective_integer_type(parser->program, constant.type, &effective_type) ||
        !minic_target_info_integer_width(
            parser->target_info, parser->program, effective_type, &source_bits) ||
        source_bits == 0U || source_bits > 64U ||
        !static_pointer_width(parser, &pointer_bits, &pointer_mask)) {
        return false;
    }
    (void)pointer_bits;

    value_bits = constant.bits;
    if (source_bits < 64U) {
        const uint64_t source_mask = (UINT64_C(1) << source_bits) - UINT64_C(1);

        value_bits &= source_mask;
        if (minic_type_is_signed_integer(effective_type) &&
            (value_bits & (UINT64_C(1) << (source_bits - 1U))) != 0U) {
            value_bits |= ~source_mask;
        }
    }
    *bits = value_bits & pointer_mask;
    return true;
}

static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *left;
    const MinicExpression *right;
    unsigned int pointer_bits;
    uint64_t pointer_mask;

    if (parser == NULL || bits == NULL ||
        !static_pointer_width(parser, &pointer_bits, &pointer_mask)) {
        return false;
    }
    (void)pointer_bits;
    expression = minic_c0_program_expression(parser->program, expression_id);
    while (expression != NULL && expression->kind == MINIC_EXPRESSION_CONVERSION) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(parser->program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type)) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
        return operand != NULL && minic_type_is_integer(operand->type) &&
               static_integer_constant_pointer_width_bits(
                   parser, expression->value.unary.operand, bits);
    }
    if (expression->kind != MINIC_EXPRESSION_BINARY ||
        (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
         expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT)) {
        return false;
    }

    left = minic_c0_program_expression(parser->program, expression->value.binary.left);
    right = minic_c0_program_expression(parser->program, expression->value.binary.right);
    if (left == NULL || right == NULL) {
        return false;
    }
    if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type)) {
        MinicType pointee;
        size_t stride;
        uint64_t base_bits;
        uint64_t offset_bits;
        uint64_t scaled_bits;

        if (!minic_type_pointee(left->type, &pointee) ||
            !minic_data_layout_pointer_arithmetic_stride(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                pointee,
                &stride) ||
            !static_pointer_integer_constant_bits(
                parser, expression->value.binary.left, &base_bits) ||
            !static_integer_constant_pointer_width_bits(
                parser, expression->value.binary.right, &offset_bits)) {
            return false;
        }
        scaled_bits = offset_bits * (uint64_t)stride;
        *bits = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT
                    ? (base_bits - scaled_bits) & pointer_mask
                    : (base_bits + scaled_bits) & pointer_mask;
        return true;
    }
    if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
        minic_type_is_integer(left->type) && minic_type_is_pointer(right->type)) {
        MinicType pointee;
        size_t stride;
        uint64_t base_bits;
        uint64_t offset_bits;

        if (!minic_type_pointee(right->type, &pointee) ||
            !minic_data_layout_pointer_arithmetic_stride(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                pointee,
                &stride) ||
            !static_pointer_integer_constant_bits(
                parser, expression->value.binary.right, &base_bits) ||
            !static_integer_constant_pointer_width_bits(
                parser, expression->value.binary.left, &offset_bits)) {
            return false;
        }
        *bits = (base_bits + offset_bits * (uint64_t)stride) & pointer_mask;
        return true;
    }
    return false;
}

'''
parser.write_text(text[:start] + replacement + text[end:])

case = Path("tests/compiler/c0/static_object_address_relocation.c")
text = case.read_text()
anchor = '''static void *high_unsigned_pointer = (void *)(0xdead000000000000UL + 0x300UL);
'''
addition = anchor + r'''static void *gnu_void_pointer_poison = (void *)0x300UL + 0xdead000000000000UL;
static int *scaled_integer_pointer_add = (int *)0x1000UL + 3;
static int *scaled_integer_pointer_subtract = (int *)0x1000UL - 2;
static int *scaled_integer_pointer_reversed = 3 + (int *)0x1000UL;
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected static integer-pointer test anchor")
case.write_text(text.replace(anchor, addition, 1))

runner = Path("tests/compiler/c0/run-static-object-address-relocation.sh")
text = runner.read_text()
old = '''grep -F '.dword -2401263026318605568' "$work/static_object_address_relocation.s" >/dev/null
'''
new = '''high_pointer_count=$(grep -F -c '.dword -2401263026318605568' "$work/static_object_address_relocation.s")
test "$high_pointer_count" -eq 2
grep -F '.dword 4108' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword 4088' "$work/static_object_address_relocation.s" >/dev/null
scaled_add_count=$(grep -F -c '.dword 4108' "$work/static_object_address_relocation.s")
test "$scaled_add_count" -eq 2
'''
if text.count(old) != 1:
    raise SystemExit("unexpected static integer-pointer runner anchor")
text = text.replace(old, new, 1)
negative_anchor = '''if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_subscript_relocation.c" \\
    -o "$work/invalid-pointer-subscript.s" \\
    >"$work/invalid-pointer-subscript.stdout" 2>"$work/invalid-pointer-subscript.stderr"; then
'''
if text.count(negative_anchor) != 1:
    raise SystemExit("unexpected fail-closed pointer anchor")
negative = '''if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_arithmetic_base.c" \\
    -o "$work/invalid-pointer-arithmetic.s" \\
    >"$work/invalid-pointer-arithmetic.stdout" 2>"$work/invalid-pointer-arithmetic.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/static-object-address: runtime pointer base accepted as static arithmetic' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or static symbol address constant' \\
    "$work/invalid-pointer-arithmetic.stderr" >/dev/null

'''
runner.write_text(text.replace(negative_anchor, negative + negative_anchor, 1))

invalid = Path("tests/compiler/c0/invalid_static_pointer_arithmetic_base.c")
if invalid.exists():
    raise SystemExit("invalid static pointer arithmetic fixture already exists")
invalid.write_text('''extern void *runtime_pointer_base;\nstatic void *invalid_pointer_arithmetic = runtime_pointer_base + 1;\n''')

print("materialized shared pointer stride and static integer-derived pointer arithmetic")
