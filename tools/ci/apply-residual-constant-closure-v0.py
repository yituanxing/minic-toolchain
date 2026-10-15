#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M181_RESIDUAL_CONSTANT_CLOSURE"
if marker in text:
    print("MINIC_RESIDUAL_CONSTANT_CLOSURE_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
start = text.find(fn)
if start < 0:
    raise SystemExit("local integer evaluator missing")

# A small target-aware constant pointer evaluator.  This is deliberately
# restricted to integer-origin constant pointers and constant pointer
# arithmetic.  It exists for Linux BUILD_BUG/type-classification idioms; no
# runtime pointer fold is introduced.
helper = r'''
/* M181_RESIDUAL_CONSTANT_CLOSURE: constant integer-origin pointer bits. */
static bool core_cfg_const_pointer_bits(const MinicCoreLowerContext *context,
                                        MinicExpressionId expression_id,
                                        unsigned int depth,
                                        uint64_t *bits) {
    const MinicExpression *expression;
    size_t pointer_size;
    unsigned int pointer_width;
    uint64_t mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || bits == NULL || depth > 64U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_pointer(expression->type) ||
        !minic_target_info_sizeof_type(context->target,
                                       context->body->program,
                                       expression->type,
                                       &pointer_size) ||
        pointer_size == 0U || pointer_size > sizeof(uint64_t)) {
        return false;
    }
    pointer_width = (unsigned int)(pointer_size * 8U);
    if (pointer_width == 0U || pointer_width > 64U) {
        return false;
    }
    mask = pointer_width == 64U ? UINT64_MAX
                                : ((UINT64_C(1) << pointer_width) - UINT64_C(1));

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (minic_type_is_pointer(operand->type)) {
            return core_cfg_const_pointer_bits(
                context, expression->value.unary.operand, depth + 1U, bits);
        }
        if (minic_type_is_integer(operand->type)) {
            MinicConstValue source;
            MinicConstValue widened;
            size_t ulong_size;

            if (!minic_const_eval_integer(context->body->program,
                                          context->target,
                                          expression->value.unary.operand,
                                          &source) ||
                !minic_target_info_sizeof_type(context->target,
                                               context->body->program,
                                               minic_type_unsigned_long(),
                                               &ulong_size) ||
                ulong_size != pointer_size ||
                !minic_const_value_convert_integer(context->body->program,
                                                   context->target,
                                                   &source,
                                                   minic_type_unsigned_long(),
                                                   &widened)) {
                return false;
            }
            *bits = widened.bits & mask;
            return true;
        }
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicExpressionId pointer_id;
        MinicExpressionId integer_id;
        MinicConstValue index;
        MinicConstValue index_wide;
        uint64_t base_bits;
        uint64_t byte_delta;
        size_t element_size;

        left = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left == NULL || right == NULL) {
            return false;
        }
        if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type)) {
            pointer_id = expression->value.binary.left;
            integer_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left->type) && minic_type_is_pointer(right->type)) {
            pointer_id = expression->value.binary.right;
            integer_id = expression->value.binary.left;
        } else {
            return false;
        }
        if (!core_cfg_const_pointer_bits(context, pointer_id, depth + 1U, &base_bits) ||
            !minic_const_eval_integer(context->body->program,
                                      context->target,
                                      integer_id,
                                      &index) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &index,
                                               minic_type_unsigned_long(),
                                               &index_wide) ||
            !minic_c0_pointer_arithmetic_element_size(
                context->body->program,
                minic_target_info_data_layout(context->target),
                minic_c0_program_expression(context->body->program, pointer_id)->type,
                &element_size) ||
            element_size == 0U) {
            return false;
        }
        byte_delta = index_wide.bits * (uint64_t)element_size;
        *bits = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT
                    ? (base_bits - byte_delta) & mask
                    : (base_bits + byte_delta) & mask;
        return true;
    }
    return false;
}

'''
text = text[:start] + helper + text[start:]
start = text.find(fn)

# Scalar compound literals such as (int){0} are represented by a hidden local
# and one initializer assignment.  They are constant for CFG purposes when the
# sole initializer is itself a constant integer expression.
cast_anchor = '''    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
'''
pos = text.find(cast_anchor, start)
if pos < 0:
    raise SystemExit("local evaluator cast anchor missing")
compound = r'''    {
        const MinicExpression *compound = expression;
        if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
            const MinicExpression *operand = minic_c0_program_expression(
                context->body->program, expression->value.unary.operand);
            if (operand != NULL && operand->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
                compound = operand;
            }
        }
        if (compound->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
            minic_type_is_integer(compound->type)) {
            const MinicBlock *initializer = minic_c0_program_block(
                context->body->program,
                compound->value.compound_literal.initializer_block);
            if (initializer != NULL && initializer->statement_count == 1U) {
                const MinicStatement *statement = minic_c0_program_statement(
                    context->body->program, initializer->statements[0]);
                const MinicExpression *target =
                    statement != NULL && statement->target_expression != MINIC_EXPRESSION_INVALID
                        ? minic_c0_program_expression(
                              context->body->program, statement->target_expression)
                        : NULL;
                MinicConstValue initializer_value;
                if (statement != NULL && statement->kind == MINIC_STATEMENT_ASSIGN &&
                    target != NULL && target->kind == MINIC_EXPRESSION_LOCAL &&
                    target->value.local_id == compound->value.compound_literal.local_id &&
                    statement->expression != MINIC_EXPRESSION_INVALID &&
                    core_const_eval_integer_with_locals(
                        context, statement->expression, &initializer_value)) {
                    return minic_const_value_convert_integer(context->body->program,
                                                             context->target,
                                                             &initializer_value,
                                                             expression->type,
                                                             value);
                }
            }
        }
    }

    /* Integer casts of a constant integer-origin pointer preserve target bits.
       This covers BUILD_BUG_ON((unsigned long)((void *)C + K) & mask). */
    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_integer(expression->type)) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        uint64_t pointer_bits;
        if (operand != NULL && minic_type_is_pointer(operand->type) &&
            core_cfg_const_pointer_bits(
                context, expression->value.unary.operand, 0U, &pointer_bits)) {
            MinicConstValue pointer_integer;
            pointer_integer.type = minic_type_unsigned_long();
            pointer_integer.bits = pointer_bits;
            return minic_const_value_convert_integer(context->body->program,
                                                     context->target,
                                                     &pointer_integer,
                                                     expression->type,
                                                     value);
        }
    }

'''
text = text[:pos] + compound + text[pos:]

# Fold pointer comparisons when both sides are target-constant integer-origin
# pointers.  Linux's min/max signedness classifier intentionally uses this GNU
# idiom for pointer types.
logical_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
'''
pos = text.find(logical_anchor, start)
if pos < 0:
    raise SystemExit("local logical anchor missing")
pointer_compare = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        const MinicExpression *right = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        uint64_t left_bits;
        uint64_t right_bits;
        bool predicate;
        if (left != NULL && right != NULL &&
            minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
            core_cfg_const_pointer_bits(
                context, expression->value.binary.left, 0U, &left_bits) &&
            core_cfg_const_pointer_bits(
                context, expression->value.binary.right, 0U, &right_bits)) {
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL: predicate = left_bits == right_bits; break;
            case MINIC_BINARY_NOT_EQUAL: predicate = left_bits != right_bits; break;
            case MINIC_BINARY_LESS: predicate = left_bits < right_bits; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_bits <= right_bits; break;
            case MINIC_BINARY_GREATER: predicate = left_bits > right_bits; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_bits >= right_bits; break;
            default: return false;
            }
            value->type = expression->type;
            value->bits = predicate ? 1U : 0U;
            return true;
        }
    }

    /* The left operand of &&/|| is evaluated in C, so reverse short-circuiting
       is safe only when that operand is side-effect free.  This closes CONFIG
       stubs such as mapping->host && (mapping->host->i_flags & 0). */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicConstValue right_value;
        bool right_is_zero;
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &right_value,
                                      &right_is_zero) &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U) &&
            ((expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND &&
              right_is_zero) ||
             (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR &&
              !right_is_zero))) {
            value->type = expression->type;
            value->bits = right_is_zero ? 0U : 1U;
            return true;
        }
    }

'''
text = text[:pos] + pointer_compare + text[pos:]

p.write_text(text)
print("MINIC_RESIDUAL_CONSTANT_CLOSURE_V0=APPLIED")
