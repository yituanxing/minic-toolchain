#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count == 1:
        text = text.replace(old, new, 1)
        return
    if count == 0 and new in text:
        return
    raise SystemExit(f"{label}: expected one old occurrence, found {count}")


# lower_if's dead-subtree safety check is intentionally conservative, but a
# parser-internal loop label with no function-level incoming edge must not keep
# an otherwise compile-time-dead branch alive. Reuse the existing reentry query
# that already understands goto, asm-goto, and label-address references.
prototype_marker = """static bool core_block_contains_reentry_label_impl(const MinicCoreLowerContext *context,
"""
prototype_replacement = """static bool core_switch_label_has_function_reentry(
    const MinicCoreLowerContext *context, MinicStatementId label_id);

static bool core_block_contains_reentry_label_impl(const MinicCoreLowerContext *context,
"""
replace_once(prototype_marker, prototype_replacement, "reentry prototype")

old_label_gate = """        if (nested->kind == MINIC_STATEMENT_LABEL ||
            nested->kind == MINIC_STATEMENT_CASE ||
            nested->kind == MINIC_STATEMENT_DEFAULT) {
            return true;
        }
"""
new_label_gate = """        if (nested->kind == MINIC_STATEMENT_CASE ||
            nested->kind == MINIC_STATEMENT_DEFAULT) {
            return true;
        }
        if (nested->kind == MINIC_STATEMENT_LABEL &&
            core_switch_label_has_function_reentry(
                context, block->statements[index])) {
            return true;
        }
"""
replace_once(old_label_gate, new_label_gate, "dead-subtree label gate")

# Short-circuit lowering used to create and lower the RHS block even when the
# left operand was a target-known constant. Runtime CFG skipped the RHS, but its
# emitted calls still created relocations inside the live function section.
old_and = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, right_block, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
"""
new_and = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        const MinicExpression *left_expression;
        MinicConstValue left_constant;
        bool left_is_zero;
        MinicCoreBlockId right_block;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (left_expression != NULL && minic_type_is_integer(left_expression->type) &&
            minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.binary.left,
                                     &left_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &left_constant,
                                      &left_is_zero)) {
            if (left_is_zero) {
                return set_branch(context, context->block_id, span, when_false);
            }
            return lower_condition_branch(
                context, expression->value.binary.right, span, when_true, when_false);
        }
        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, right_block, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
"""
replace_once(old_and, new_and, "logical-and short-circuit")

old_or = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
"""
new_or = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        const MinicExpression *left_expression;
        MinicConstValue left_constant;
        bool left_is_zero;
        MinicCoreBlockId right_block;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (left_expression != NULL && minic_type_is_integer(left_expression->type) &&
            minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.binary.left,
                                     &left_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &left_constant,
                                      &left_is_zero)) {
            if (!left_is_zero) {
                return set_branch(context, context->block_id, span, when_true);
            }
            return lower_condition_branch(
                context, expression->value.binary.right, span, when_true, when_false);
        }
        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
"""
replace_once(old_or, new_or, "logical-or short-circuit")

path.write_text(text)
