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

print("CORE_BATCH_C_PATCHED zero-distance-cleanup direct-record-return")
