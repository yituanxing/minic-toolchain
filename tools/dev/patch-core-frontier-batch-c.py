#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}")
    p.write_text(text.replace(old, new, 1))


path = "src/core/core_lower.c"

# Batch C/1: cleanup_context == cleanup_stop_context is a zero-distance edge.
# Frontend validation and the legacy emitter both execute cleanup expressions
# only while current != stop, so accepting equal non-root ids cannot skip a
# destructor. Real cleanup-crossing control flow remains fail-closed.
old = '''        if (statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=statement reason=cleanup-context "
                          "function=%s kind=%d span=%zu:%zu cleanup=%llu stop=%llu\\n",
                          context->source_function->name,
                          (int)statement->kind,
                          statement->span.begin.line,
                          statement->span.begin.column,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
new = '''        /* BATCH_C_ZERO_DISTANCE_CLEANUP_EDGE: cleanup ids are semantic edge
           metadata. Equal ids mean the edge crosses no cleanup lifetime, even
           when both ids are non-root. Only an actual context transition needs
           cleanup-expression lowering, which remains fail-closed here. */
        if (statement->cleanup_context != statement->cleanup_stop_context) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=statement reason=cleanup-context "
                          "function=%s kind=%d span=%zu:%zu cleanup=%llu stop=%llu\\n",
                          context->source_function->name,
                          (int)statement->kind,
                          statement->span.begin.line,
                          statement->span.begin.column,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
replace_once(path, old, new)

# Keep every normalized CFG seam on the same semantic rule instead of requiring
# the arbitrary ROOT id. Equal current/stop ids mean there is no cleanup work.
old = '''static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

static bool normalized_do_while_zero_body'''
new = '''static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

static bool core_cleanup_edge_is_empty(const MinicStatement *statement) {
    return statement != NULL &&
           statement->cleanup_context == statement->cleanup_stop_context;
}

static bool normalized_do_while_zero_body'''
replace_once(path, old, new)

old = '''    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        break_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        break_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
'''
new = '''    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !core_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
'''
replace_once(path, old, new)

old = '''    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION ||
        update->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->expression == MINIC_EXPRESSION_INVALID) {
'''
new = '''    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION || !core_cleanup_edge_is_empty(update) ||
        update->expression == MINIC_EXPRESSION_INVALID) {
'''
replace_once(path, old, new)

old = '''        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_WHILE ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->then_block == MINIC_BLOCK_INVALID ||
'''
new = '''        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_WHILE || !core_cleanup_edge_is_empty(statement) ||
        statement->then_block == MINIC_BLOCK_INVALID ||
'''
replace_once(path, old, new)

# One-shot loop diagnostics: if the focused Linux guard shape still fails, say
# whether the nested body or the synthetic for-update is the actual blocker.
old = '''    status = lower_block(context, iteration_source, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!body_terminated && for_update != NULL) {
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
new = '''    status = lower_block(context, iteration_source, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=while reason=body function=%s status=%d "
                      "normalized_for=%d has_update=%d span=%zu:%zu\\n",
                      context->source_function->name,
                      (int)status,
                      normalized_for ? 1 : 0,
                      for_update != NULL ? 1 : 0,
                      statement->span.begin.line,
                      statement->span.begin.column);
        return status;
    }
    if (!body_terminated && for_update != NULL) {
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            const MinicExpression *update_expression = minic_c0_program_expression(
                context->body->program, for_update->expression);
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=while reason=update function=%s status=%d "
                          "expr_kind=%d span=%zu:%zu\\n",
                          context->source_function->name,
                          (int)status,
                          update_expression != NULL ? (int)update_expression->kind : -1,
                          for_update->span.begin.line,
                          for_update->span.begin.column);
            return status;
        }
    }
'''
replace_once(path, old, new)

# Batch C/2: record-return forwarding is already owned by M86's direct-record
# call result object. Reuse that object as the return terminator payload instead
# of requiring callers such as fdget() to materialize a redundant local copy.
old = '''            } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = lower_record_compound_literal_object(
                    context, expression, &terminator.return_object);
            } else {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
new = '''            } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
                       minic_c0_record_value_is_address_backed(
                           context->body->program, statement->expression)) {
                status = lower_record_compound_literal_object(
                    context, expression, &terminator.return_object);
            } else if (expression->kind == MINIC_EXPRESSION_CALL &&
                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
                status = lower_direct_record_call_object(
                    context, expression, &terminator.return_object);
            } else {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
replace_once(path, old, new)

print("CORE_BATCH_C_PATCHED zero-distance-cleanup normalized-cfg direct-record-return loop-trace")
