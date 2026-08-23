#!/usr/bin/env python3
"""Lower parser-normalized for-loops whose source condition is omitted."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M78_OMITTED_FOR_CONDITION"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M78 {name} anchor count={count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M78 omitted for condition already applied")
        return 0

    update_anchor = '''static bool normalized_for_update_tail(const MinicCoreLowerContext *context,\n'''
    helper = '''/* M78_OMITTED_FOR_CONDITION: parse_for represents a missing source\n   condition as MINIC_EXPRESSION_INVALID and appends its synthetic continue\n   label to the loop body. Recognize the no-update variant explicitly so Core\n   can distinguish `for (;;)` from an invalid source while statement. */\nstatic bool normalized_for_continue_tail(const MinicCoreLowerContext *context,\n                                         const MinicStatement *loop,\n                                         const MinicBlock *body,\n                                         MinicBlock *iteration_body) {\n    const MinicStatement *continue_label;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        loop == NULL || body == NULL || iteration_body == NULL || body->statement_count < 1U) {\n        return false;\n    }\n    continue_label = minic_c0_program_statement(\n        context->body->program, body->statements[body->statement_count - 1U]);\n    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||\n        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||\n        continue_label->expression != MINIC_EXPRESSION_INVALID ||\n        continue_label->target_statement != MINIC_STATEMENT_INVALID ||\n        !source_position_equal(continue_label->span.begin, loop->span.begin)) {\n        return false;\n    }\n    *iteration_body = *body;\n    iteration_body->statement_count -= 1U;\n    return true;\n}\n\nstatic bool normalized_for_update_tail(const MinicCoreLowerContext *context,\n'''
    text = replace_once(text, update_anchor, helper, "continue-helper")

    decl_anchor = '''    MinicCoreLowerStatus status;\n    bool body_terminated;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n'''
    decl_replacement = '''    MinicCoreLowerStatus status;\n    bool body_terminated;\n    bool normalized_for;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n'''
    text = replace_once(text, decl_anchor, decl_replacement, "decl")

    guard_anchor = '''        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||\n        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||\n        statement->expression == MINIC_EXPRESSION_INVALID ||\n        statement->then_block == MINIC_BLOCK_INVALID ||\n'''
    guard_replacement = '''        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||\n        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||\n        statement->then_block == MINIC_BLOCK_INVALID ||\n'''
    text = replace_once(text, guard_anchor, guard_replacement, "guard")

    condition_anchor = '''    condition_expression =\n        minic_c0_program_expression(context->body->program, statement->expression);\n    body_source = minic_c0_program_block(context->body->program, statement->then_block);\n    if (condition_expression == NULL || body_source == NULL ||\n        !minic_type_is_integer(condition_expression->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n'''
    condition_replacement = '''    body_source = minic_c0_program_block(context->body->program, statement->then_block);\n    if (body_source == NULL) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    condition_expression = NULL;\n    if (statement->expression != MINIC_EXPRESSION_INVALID) {\n        condition_expression =\n            minic_c0_program_expression(context->body->program, statement->expression);\n        if (condition_expression == NULL || !minic_type_is_integer(condition_expression->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n    }\n'''
    text = replace_once(text, condition_anchor, condition_replacement, "condition-load")

    for_anchor = '''    iteration_source = body_source;\n    for_update = NULL;\n    if (normalized_for_update_tail(\n            context, statement, body_source, &normalized_for_body, &for_update)) {\n        iteration_source = &normalized_for_body;\n    }\n\n    preheader_block = context->block_id;\n'''
    for_replacement = '''    iteration_source = body_source;\n    for_update = NULL;\n    normalized_for = false;\n    if (normalized_for_update_tail(\n            context, statement, body_source, &normalized_for_body, &for_update)) {\n        iteration_source = &normalized_for_body;\n        normalized_for = true;\n    } else if (normalized_for_continue_tail(\n                   context, statement, body_source, &normalized_for_body)) {\n        iteration_source = &normalized_for_body;\n        normalized_for = true;\n    }\n    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n\n    preheader_block = context->block_id;\n'''
    text = replace_once(text, for_anchor, for_replacement, "for-shape")

    branch_anchor = '''    context->block_id = condition_block;\n    status = lower_expression(context, statement->expression, &condition);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    if (!minic_type_is_integer(context->function->values[condition].type)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    (void)memset(&terminator, 0, sizeof(terminator));\n    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;\n    terminator.span = statement->span;\n    terminator.return_value = MINIC_CORE_VALUE_INVALID;\n    terminator.conditional.condition = condition;\n    terminator.conditional.when_true = body_block;\n    terminator.conditional.when_false = exit_block;\n    if (!minic_core_function_set_terminator(context->function, condition_block, &terminator)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n\n    context->block_id = body_block;\n'''
    branch_replacement = '''    context->block_id = condition_block;\n    if (statement->expression == MINIC_EXPRESSION_INVALID) {\n        /* C defines an omitted for-condition as true. Keep an explicit Core\n           condition block so break/backedge ownership remains identical to\n           the conditional-loop path. */\n        status = set_branch(context, condition_block, statement->span, body_block);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n    } else {\n        status = lower_expression(context, statement->expression, &condition);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (!minic_type_is_integer(context->function->values[condition].type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&terminator, 0, sizeof(terminator));\n        terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;\n        terminator.span = statement->span;\n        terminator.return_value = MINIC_CORE_VALUE_INVALID;\n        terminator.conditional.condition = condition;\n        terminator.conditional.when_true = body_block;\n        terminator.conditional.when_false = exit_block;\n        if (!minic_core_function_set_terminator(\n                context->function, condition_block, &terminator)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n    }\n\n    context->block_id = body_block;\n'''
    text = replace_once(text, branch_anchor, branch_replacement, "condition-branch")

    PATH.write_text(text)
    print("M78 omitted for condition lowering applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
