from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_global.c"
source_path = root / "tests/compiler/c0/static_pointer_constant_conditional.c"
runner_path = root / "tests/compiler/c0/run-static-pointer-constant-conditional.sh"
invalid_path = root / "tests/compiler/c0/invalid_static_pointer_nonconstant_conditional.c"

text = parser_path.read_text()
start = text.index("static bool static_pointer_integer_constant_bits(")
end = text.index("static bool static_pointer_initializer_from_expression(", start)
helpers = r'''static bool static_pointer_mask_bits(const MinicParser *parser,
                                     uint64_t value,
                                     uint64_t *bits) {
    const MinicDataLayout *layout;
    unsigned int pointer_bits;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *bits = value;
    if (pointer_bits < 64U) {
        *bits &= (UINT64_C(1) << pointer_bits) - UINT64_C(1);
    }
    return true;
}

static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicConstValue constant;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_is_pointer(expression->type)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (operand == NULL || !minic_type_is_integer(operand->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression->value.unary.operand, &constant)) {
        return false;
    }
    return static_pointer_mask_bits(parser, constant.bits, bits);
}

static bool static_pointer_absolute_offset_bits(const MinicParser *parser,
                                                MinicType pointee_type,
                                                MinicExpressionId offset_expression_id,
                                                bool subtract,
                                                uint64_t *byte_offset_bits) {
    MinicConstValue constant;
    int64_t signed_count;
    uint64_t count_bits;
    uint64_t product;
    size_t size;

    if (parser == NULL || byte_offset_bits == NULL ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, offset_expression_id, &constant) ||
        !minic_target_info_sizeof_type(
            parser->target_info, parser->program, pointee_type, &size) ||
        size == 0U) {
        return false;
    }
    if (minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &signed_count)) {
        count_bits = (uint64_t)signed_count;
    } else {
        unsigned int width;

        if (!minic_target_info_integer_width(
                parser->target_info, parser->program, constant.type, &width) ||
            width == 0U || width > 64U) {
            return false;
        }
        count_bits = constant.bits;
    }
    product = count_bits * (uint64_t)size;
    if (subtract) {
        product = UINT64_C(0) - product;
    }
    return static_pointer_mask_bits(parser, product, byte_offset_bits);
}

static void static_pointer_initializer_reset(MinicStaticPointerInitializer *initializer) {
    if (initializer == NULL) {
        return;
    }
    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
}

static bool static_pointer_initializers_same_value(const MinicStaticPointerInitializer *left,
                                                   const MinicStaticPointerInitializer *right) {
    size_t index;

    if (left == NULL || right == NULL || left->has_relocation != right->has_relocation) {
        return false;
    }
    if (!left->has_relocation) {
        return left->bits == right->bits;
    }
    if (left->relocation_is_function != right->relocation_is_function) {
        return false;
    }
    if (left->relocation_is_function) {
        return left->function_id == right->function_id;
    }
    if (left->relocation_target.object_id != right->relocation_target.object_id ||
        left->relocation_target.member_depth != right->relocation_target.member_depth ||
        left->relocation_target.byte_addend != right->relocation_target.byte_addend) {
        return false;
    }
    for (index = 0U; index < left->relocation_target.member_depth; ++index) {
        if (left->relocation_target.member_indices[index] !=
            right->relocation_target.member_indices[index]) {
            return false;
        }
    }
    return true;
}

'''
text = text[:start] + helpers + text[end:]

start = text.index("static bool static_pointer_initializer_from_expression(")
end = text.index("static bool static_pointer_initializer_type_compatible(", start)
initializer = r'''static bool static_pointer_initializer_from_expression(MinicParser *parser,
                                                       MinicExpressionId expression_id,
                                                       MinicStaticPointerInitializer *initializer) {
    const MinicExpression *expression;

    if (parser == NULL || initializer == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    initializer->has_explicit_pointer_cast =
        initializer->has_explicit_pointer_cast ||
        static_pointer_expression_has_explicit_cast(parser->program, expression_id);
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        return true;
    }
    if (static_function_address_relocation_target(
            parser->program, expression_id, &initializer->function_id)) {
        initializer->has_relocation = true;
        initializer->relocation_is_function = true;
        return true;
    }
    if (static_object_address_relocation_path(
            parser, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
    if (static_pointer_integer_constant_bits(parser, expression_id, &initializer->bits)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        const MinicExpression *right;
        const MinicExpression *pointer_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId offset_id;
        MinicStaticPointerInitializer base_initializer;
        MinicType pointee_type;
        uint64_t delta_bits;
        uint64_t result_bits;
        bool subtract;

        left = minic_c0_program_expression(parser->program, expression->value.binary.left);
        right = minic_c0_program_expression(parser->program, expression->value.binary.right);
        pointer_expression = NULL;
        pointer_id = MINIC_EXPRESSION_INVALID;
        offset_id = MINIC_EXPRESSION_INVALID;
        subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (left != NULL && minic_type_is_pointer(left->type)) {
            pointer_expression = left;
            pointer_id = expression->value.binary.left;
            offset_id = expression->value.binary.right;
        } else if (!subtract && right != NULL && minic_type_is_pointer(right->type)) {
            pointer_expression = right;
            pointer_id = expression->value.binary.right;
            offset_id = expression->value.binary.left;
        }
        static_pointer_initializer_reset(&base_initializer);
        if (pointer_expression != NULL &&
            minic_type_pointee(pointer_expression->type, &pointee_type) &&
            static_pointer_initializer_from_expression(parser, pointer_id, &base_initializer) &&
            !base_initializer.has_relocation &&
            static_pointer_absolute_offset_bits(
                parser, pointee_type, offset_id, subtract, &delta_bits) &&
            static_pointer_mask_bits(parser, base_initializer.bits + delta_bits, &result_bits)) {
            *initializer = base_initializer;
            initializer->bits = result_bits;
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value) {
        MinicConstValue condition_constant;
        int64_t condition_value;
        MinicExpressionId selected_id;

        if (minic_const_eval_integer(parser->program,
                                     parser->target_info,
                                     expression->value.conditional.condition,
                                     &condition_constant) &&
            minic_const_value_as_int64(
                parser->program, parser->target_info, &condition_constant, &condition_value)) {
            selected_id = condition_value != 0 ? expression->value.conditional.when_true
                                               : expression->value.conditional.when_false;
            return static_pointer_initializer_from_expression(parser, selected_id, initializer);
        }
        {
            MinicStaticPointerInitializer when_true;
            MinicStaticPointerInitializer when_false;

            static_pointer_initializer_reset(&when_true);
            static_pointer_initializer_reset(&when_false);
            if (!static_pointer_initializer_from_expression(
                    parser, expression->value.conditional.when_true, &when_true) ||
                !static_pointer_initializer_from_expression(
                    parser, expression->value.conditional.when_false, &when_false) ||
                !static_pointer_initializers_same_value(&when_true, &when_false)) {
                return false;
            }
            when_true.has_explicit_pointer_cast = when_true.has_explicit_pointer_cast ||
                                                  when_false.has_explicit_pointer_cast;
            *initializer = when_true;
            return true;
        }
    }
    return false;
}

'''
text = text[:start] + initializer + text[end:]
old_reset = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
'''
if old_reset not in text:
    raise SystemExit("parse_static_pointer_initializer reset anchor missing")
text = text.replace(old_reset, "    static_pointer_initializer_reset(initializer);\n", 1)
parser_path.write_text(text)

source = source_path.read_text()
anchor = "int main(void) {\n"
if anchor not in source:
    raise SystemExit("static pointer conditional source anchor missing")
addition = r'''static void *absolute_pointer_poison = (void *)0x300UL + 0xdead000000000000UL;
static void *absolute_pointer_bits = (void *)(0xeB9FUL + 0xdead000000000000UL);
static void *same_function_conditional =
    main == (void *)0 ? (void *)&main : (void *)&main;

'''
source = source.replace(anchor, addition + anchor, 1)
source_path.write_text(source)

runner = runner_path.read_text()
anchor = "grep -F '  .dword 0' \"$work/output.s\" >/dev/null\n"
if anchor not in runner:
    raise SystemExit("static pointer conditional runner anchor missing")
addition = '''grep -F 'absolute_pointer_poison:' "$work/output.s" >/dev/null\ngrep -F 'absolute_pointer_bits:' "$work/output.s" >/dev/null\ngrep -F 'same_function_conditional:' "$work/output.s" >/dev/null\nsame_function_count=$(grep -F -c '  .dword main' "$work/output.s")\ntest "$same_function_count" -eq 1\n\nif "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_nonconstant_conditional.c" \\\n    -o "$work/invalid-nonconstant-conditional.s" \\\n    >"$work/invalid-nonconstant-conditional.stdout" \\\n    2>"$work/invalid-nonconstant-conditional.stderr"; then\n    printf '%s\\n' 'FAIL compiler/c0/static-pointer-constant-conditional: unequal nonconstant arms accepted' >&2\n    exit 1\nfi\ngrep -F 'static pointer initializer requires a null or static symbol address constant' \\\n    "$work/invalid-nonconstant-conditional.stderr" >/dev/null\n'''
runner = runner.replace(anchor, anchor + addition, 1)
runner = runner.replace(
    "PASS compiler/c0/static-pointer-constant-conditional symbol+null selected-by-ICE",
    "PASS compiler/c0/static-pointer-constant-conditional symbol+null selected-by-ICE absolute-bits=full-width absolute-pointer-arithmetic=target-width identical-arms=normalized unequal-arms=fail-closed",
)
runner_path.write_text(runner)

invalid_path.write_text(r'''static int left_value;
static int right_value;
static int *invalid_nonconstant_conditional =
    &left_value == (void *)0 ? &left_value : &right_value;
''')
print("materialized canonical static pointer constant expressions")
