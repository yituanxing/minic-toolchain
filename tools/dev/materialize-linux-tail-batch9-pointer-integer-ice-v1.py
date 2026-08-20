from pathlib import Path

path = Path("src/frontend/const_eval.c")
text = path.read_text()

old_helper = '''static bool integer_cast_operand_is_null_pointer(const MinicC0Program *program,
                                                 const MinicExpression *expression) {
    const MinicExpression *pointer_cast;
    const MinicExpression *zero;

    if (program == NULL || expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    pointer_cast = minic_c0_program_expression(program, expression->value.unary.operand);
    if (pointer_cast == NULL || !minic_type_is_pointer(pointer_cast->type) ||
        (pointer_cast->kind != MINIC_EXPRESSION_CAST &&
         pointer_cast->kind != MINIC_EXPRESSION_BITCAST)) {
        return false;
    }
    zero = minic_c0_program_expression(program, pointer_cast->value.unary.operand);
    return zero != NULL && zero->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(zero->type) && zero->value.integer_value == 0;
}

static bool eval_expression(const MinicC0Program *program,
                            const MinicTargetInfo *target,
                            MinicExpressionId expression_id,
                            unsigned int depth,
                            MinicConstValue *value);
'''
new_helper = '''static bool eval_expression(const MinicC0Program *program,
                            const MinicTargetInfo *target,
                            MinicExpressionId expression_id,
                            unsigned int depth,
                            MinicConstValue *value);

static bool integer_cast_operand_is_pointer_roundtrip_constant(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicExpression *expression,
    unsigned int depth,
    uint64_t *bits) {
    const MinicExpression *pointer_cast;
    const MinicExpression *integer_operand;
    MinicConstValue operand;
    uint64_t operand_bits;
    size_t pointer_size;
    unsigned int pointer_width;

    if (program == NULL || target == NULL || expression == NULL || bits == NULL ||
        !minic_type_is_integer(expression->type) || depth > MINIC_CONST_EVAL_MAX_DEPTH - 2U) {
        return false;
    }
    pointer_cast = minic_c0_program_expression(program, expression->value.unary.operand);
    if (pointer_cast == NULL || !minic_type_is_pointer(pointer_cast->type) ||
        (pointer_cast->kind != MINIC_EXPRESSION_CAST &&
         pointer_cast->kind != MINIC_EXPRESSION_BITCAST)) {
        return false;
    }
    integer_operand =
        minic_c0_program_expression(program, pointer_cast->value.unary.operand);
    if (integer_operand == NULL || !minic_type_is_integer(integer_operand->type) ||
        !eval_expression(program,
                         target,
                         pointer_cast->value.unary.operand,
                         depth + 2U,
                         &operand) ||
        !minic_target_info_sizeof_type(
            target, program, pointer_cast->type, &pointer_size) ||
        pointer_size == 0U || pointer_size > sizeof(uint64_t)) {
        return false;
    }
    if (integer_type_is_signed(program, operand.type)) {
        int64_t signed_value;

        if (!value_signed(program, target, &operand, &signed_value) || signed_value < 0) {
            return false;
        }
        operand_bits = (uint64_t)signed_value;
    } else if (!normalize_bits(program, target, operand.type, operand.bits, &operand_bits)) {
        return false;
    }
    pointer_width = (unsigned int)(pointer_size * (size_t)CHAR_BIT);
    if (pointer_width == 0U || pointer_width > 64U ||
        (pointer_width < 64U && operand_bits > width_mask(pointer_width))) {
        return false;
    }
    return normalize_bits(program, target, expression->type, operand_bits, bits);
}
'''

old_cast = '''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;

        if (expression->kind == MINIC_EXPRESSION_CAST &&
            integer_cast_operand_is_null_pointer(program, expression)) {
            value->type = expression->type;
            return normalize_bits(program, target, expression->type, 0U, &value->bits);
        }
        return eval_expression(
                   program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
'''
new_cast = '''    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;
        uint64_t pointer_roundtrip_bits;

        if (expression->kind == MINIC_EXPRESSION_CAST &&
            integer_cast_operand_is_pointer_roundtrip_constant(
                program, target, expression, depth, &pointer_roundtrip_bits)) {
            value->type = expression->type;
            value->bits = pointer_roundtrip_bits;
            return true;
        }
        return eval_expression(
                   program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
'''

if text.count(old_helper) != 1:
    raise SystemExit(f"expected one null-pointer ICE helper, found {text.count(old_helper)}")
text = text.replace(old_helper, new_helper, 1)
if text.count(old_cast) != 1:
    raise SystemExit(f"expected one integer cast ConstEval block, found {text.count(old_cast)}")
text = text.replace(old_cast, new_cast, 1)
path.write_text(text)
