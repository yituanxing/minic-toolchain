#!/usr/bin/env python3
from pathlib import Path

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
anchor = "static bool static_pointer_initializer_from_expression(MinicParser *parser,\n"
if text.count(anchor) != 1 or "static_pointer_initializer_equality_known" in text:
    raise SystemExit("unexpected static pointer initializer owner shape")
helper = r'''static bool static_object_relocation_target_same(
    const MinicStaticObjectRelocationTarget *left,
    const MinicStaticObjectRelocationTarget *right) {
    size_t depth;

    if (left == NULL || right == NULL || left->object_id != right->object_id ||
        left->member_depth != right->member_depth || left->byte_addend != right->byte_addend) {
        return false;
    }
    for (depth = 0U; depth < left->member_depth; ++depth) {
        if (left->member_indices[depth] != right->member_indices[depth]) {
            return false;
        }
    }
    return true;
}

static bool static_pointer_null_initializer_value(const MinicC0Program *program,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;
    size_t remaining;

    if (program == NULL || program->expression_count == SIZE_MAX) {
        return false;
    }
    remaining = program->expression_count + 1U;
    while (remaining-- != 0U) {
        const MinicExpression *operand;

        if (minic_c0_expression_is_null_pointer_constant_v0(program, expression_id)) {
            return true;
        }
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL || !minic_type_is_pointer(expression->type) ||
            (expression->kind != MINIC_EXPRESSION_CAST &&
             expression->kind != MINIC_EXPRESSION_BITCAST &&
             expression->kind != MINIC_EXPRESSION_CONVERSION)) {
            return false;
        }
        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL || !minic_type_is_pointer(operand->type)) {
            return false;
        }
        expression_id = expression->value.unary.operand;
    }
    return false;
}

static bool static_pointer_initializer_equality_known(
    const MinicParser *parser,
    const MinicStaticPointerInitializer *left,
    const MinicStaticPointerInitializer *right,
    bool *equal) {
    const MinicStaticPointerInitializer *relocation;
    const MinicStaticPointerInitializer *absolute;
    const MinicFunction *function;

    if (parser == NULL || left == NULL || right == NULL || equal == NULL) {
        return false;
    }
    if (!left->has_relocation && !right->has_relocation) {
        *equal = left->bits == right->bits;
        return true;
    }
    if (left->has_relocation && right->has_relocation) {
        if (left->relocation_is_function != right->relocation_is_function) {
            return false;
        }
        if (left->relocation_is_function) {
            if (left->function_id != right->function_id) {
                return false;
            }
            *equal = true;
            return true;
        }
        if (!static_object_relocation_target_same(
                &left->relocation_target, &right->relocation_target)) {
            return false;
        }
        *equal = true;
        return true;
    }

    relocation = left->has_relocation ? left : right;
    absolute = left->has_relocation ? right : left;
    if (!relocation->relocation_is_function || absolute->bits != 0U) {
        return false;
    }
    function = minic_c0_program_function(parser->program, relocation->function_id);
    if (function == NULL || !function->is_defined || function->is_weak) {
        return false;
    }
    *equal = false;
    return true;
}

'''
text = text.replace(anchor, helper + anchor, 1)

old_null = '''    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        return true;
    }
'''
new_null = '''    if (static_pointer_null_initializer_value(parser->program, expression_id)) {
        return true;
    }
'''
initializer_start = text.index(anchor)
initializer_tail = text[initializer_start:]
if initializer_tail.count(old_null) != 1:
    raise SystemExit("unexpected static pointer null initializer check")
initializer_tail = initializer_tail.replace(old_null, new_null, 1)
text = text[:initializer_start] + initializer_tail

old = '''    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value) {
        MinicConstValue condition_constant;
        int64_t condition_value;
        MinicExpressionId selected_id;

        if (!minic_const_eval_integer(parser->program,
                                      parser->target_info,
                                      expression->value.conditional.condition,
                                      &condition_constant) ||
            !minic_const_value_as_int64(
                parser->program, parser->target_info, &condition_constant, &condition_value)) {
            return false;
        }
        selected_id = condition_value != 0 ? expression->value.conditional.when_true
                                           : expression->value.conditional.when_false;
        return static_pointer_initializer_from_expression(parser, selected_id, initializer);
    }
'''
new = r'''    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value) {
        const MinicExpression *condition;
        MinicConstValue condition_constant;
        int64_t condition_value;
        MinicExpressionId selected_id;
        bool condition_known;

        condition_known =
            minic_const_eval_integer(parser->program,
                                     parser->target_info,
                                     expression->value.conditional.condition,
                                     &condition_constant) &&
            minic_const_value_as_int64(
                parser->program, parser->target_info, &condition_constant, &condition_value);
        condition = minic_c0_program_expression(
            parser->program, expression->value.conditional.condition);
        if (!condition_known && condition != NULL &&
            condition->kind == MINIC_EXPRESSION_BINARY &&
            (condition->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
             condition->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL)) {
            MinicStaticPointerInitializer left_initializer;
            MinicStaticPointerInitializer right_initializer;
            bool equal;

            (void)memset(&left_initializer, 0, sizeof(left_initializer));
            (void)memset(&right_initializer, 0, sizeof(right_initializer));
            if (static_pointer_initializer_from_expression(
                    parser, condition->value.binary.left, &left_initializer) &&
                static_pointer_initializer_from_expression(
                    parser, condition->value.binary.right, &right_initializer) &&
                static_pointer_initializer_equality_known(
                    parser, &left_initializer, &right_initializer, &equal)) {
                condition_value = condition->value.binary.operator_kind == MINIC_BINARY_EQUAL
                                      ? (equal ? 1 : 0)
                                      : (equal ? 0 : 1);
                condition_known = true;
            }
        }
        if (!condition_known) {
            return false;
        }
        selected_id = condition_value != 0 ? expression->value.conditional.when_true
                                           : expression->value.conditional.when_false;
        return static_pointer_initializer_from_expression(parser, selected_id, initializer);
    }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected static pointer conditional block")
parser.write_text(text.replace(old, new, 1))

case = Path("tests/compiler/c0/static_object_address_relocation.c")
text = case.read_text()
anchor = '''static int function_address_target(int value) {
    return value + 1;
}
'''
addition = anchor + r'''
typedef int (*FunctionAddressType)(int);

static FunctionAddressType conditional_function_address =
    function_address_target == (FunctionAddressType)((void *)0)
        ? (FunctionAddressType)((void *)0)
        : function_address_target;
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected function-address test anchor")
case.write_text(text.replace(anchor, addition, 1))

runner = Path("tests/compiler/c0/run-static-object-address-relocation.sh")
text = runner.read_text()
old = '''function_count=$(grep -F -c '.dword function_address_target' "$work/static_object_address_relocation.s")
test "$function_count" -eq 2
'''
new = '''function_count=$(grep -F -c '.dword function_address_target' "$work/static_object_address_relocation.s")
test "$function_count" -eq 3
'''
if text.count(old) != 1:
    raise SystemExit("unexpected function relocation count anchor")
runner.write_text(text.replace(old, new, 1))

print("materialized fail-closed static pointer equality for conditional selection")
