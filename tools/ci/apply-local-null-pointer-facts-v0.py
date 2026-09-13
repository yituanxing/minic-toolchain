#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    text = text.replace(old, new, 1)


# Keep pointer facts deliberately narrower than integer facts: the only pointer
# value admitted here is a proven null pointer.  Reuse the existing per-local
# known/escaped lifetime so all CFG/escape barriers stay conservative.
helper_anchor = '''static bool core_const_signed_value(const MinicCoreLowerContext *context,
                                    const MinicConstValue *value,
                                    int64_t *result) {
'''
helpers = r'''static void core_local_null_pointer_set(MinicCoreLowerContext *context,
                                        MinicLocalId local_id) {
    const MinicLocal *local;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        !core_local_constant_index(context, local_id, &index) ||
        context->local_integer_constants[index].escaped) {
        return;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || local->is_array || minic_type_is_volatile(local->type) ||
        !minic_type_is_pointer(local->type)) {
        context->local_integer_constants[index].known = false;
        return;
    }
    context->local_integer_constants[index].value.type = minic_type_int();
    context->local_integer_constants[index].value.bits = 0U;
    context->local_integer_constants[index].known = true;
}

static bool core_local_null_pointer_get(const MinicCoreLowerContext *context,
                                        MinicLocalId local_id) {
    const MinicLocal *local;
    size_t index;

    if (!core_local_constant_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    return local != NULL && !local->is_array && !minic_type_is_volatile(local->type) &&
           minic_type_is_pointer(local->type) &&
           minic_type_is_integer(context->local_integer_constants[index].value.type) &&
           context->local_integer_constants[index].value.bits == 0U;
}

static bool core_expression_known_null_pointer_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicLocalId local_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 8U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(
            context->body->program, expression_id)) {
        return true;
    }
    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_BITCAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_pointer(expression->type)) {
        return core_expression_known_null_pointer_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (core_direct_local_read(context, expression_id, &local_id) &&
        core_local_null_pointer_get(context, local_id)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        const MinicFunction *callee;
        const MinicBlock *callee_body;
        const MinicStatement *return_statement;

        callee = minic_c0_program_function(
            context->body->program, expression->value.call.function_id);
        if (callee == NULL || !callee->is_defined || !minic_type_is_pointer(callee->return_type)) {
            return false;
        }
        callee_body = minic_c0_program_block(context->body->program, callee->body_block);
        if (callee_body == NULL || callee_body->statement_count != 1U) {
            return false;
        }
        return_statement = minic_c0_program_statement(
            context->body->program, callee_body->statements[0]);
        if (return_statement == NULL || return_statement->kind != MINIC_STATEMENT_RETURN ||
            return_statement->expression == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        return core_expression_known_null_pointer_depth(
            context, return_statement->expression, depth + 1U);
    }
    return false;
}

static bool core_expression_known_null_pointer(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id) {
    return core_expression_known_null_pointer_depth(context, expression_id, 0U);
}

'''
replace_once(helper_anchor, helpers + helper_anchor, "null-pointer helpers")

# Record a null fact after an ordinary direct-local assignment.  The source
# expression is classified from semantic AST state; the actual call/arguments
# are still lowered normally, so this does not erase argument side effects.
assignment_old = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        bool fact_known;
        core_local_constant_invalidate(context, target->value.local_id);
        fact_known = core_const_eval_integer_with_locals(context, source_id, &stored_constant);
        if (fact_known) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_ASSIGN function=%s target=%zu source=%zu source_kind=%d known=%d bits=%" PRIu64 "\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)target->value.local_id,
                          (size_t)source_id,
                          source != NULL ? (int)source->kind : -1,
                          fact_known ? 1 : 0,
                          fact_known ? stored_constant.bits : UINT64_C(0));
        }
    }
'''
assignment_new = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        bool fact_known;
        bool null_pointer_known;
        null_pointer_known = core_expression_known_null_pointer(context, source_id);
        core_local_constant_invalidate(context, target->value.local_id);
        fact_known = core_const_eval_integer_with_locals(context, source_id, &stored_constant);
        if (fact_known) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        } else if (null_pointer_known) {
            core_local_null_pointer_set(context, target->value.local_id);
        }
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_ASSIGN function=%s target=%zu source=%zu source_kind=%d known=%d null=%d bits=%" PRIu64 "\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)target->value.local_id,
                          (size_t)source_id,
                          source != NULL ? (int)source->kind : -1,
                          fact_known ? 1 : 0,
                          null_pointer_known ? 1 : 0,
                          fact_known ? stored_constant.bits : UINT64_C(0));
        }
    }
'''
replace_once(assignment_old, assignment_new, "assignment null fact")

# Source-level if pruning already admits local integer facts. Extend only that
# reachability seam to a proven-null pointer condition; all unknown pointers
# stay on the ordinary CFG path and therefore clear facts at the existing join.
if_old = '''    if (statement->cleanup_context == statement->cleanup_stop_context &&
        minic_type_is_integer(condition_expression->type)) {
        MinicConstValue condition_value;
        bool condition_is_zero;

        if (core_const_eval_integer_with_locals(
                context, statement->expression, &condition_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_value,
                                      &condition_is_zero)) {
            const MinicBlock *discarded_source;
            const MinicBlock *selected_source;
'''
if_new = '''    if (statement->cleanup_context == statement->cleanup_stop_context) {
        bool condition_is_zero = false;
        bool condition_known = false;

        if (minic_type_is_pointer(condition_expression->type) &&
            core_expression_known_null_pointer(context, statement->expression)) {
            condition_known = true;
            condition_is_zero = true;
        } else if (minic_type_is_integer(condition_expression->type)) {
            MinicConstValue condition_value;
            bool integer_condition_is_zero;

            if (core_const_eval_integer_with_locals(
                    context, statement->expression, &condition_value) &&
                minic_const_value_is_zero(context->body->program,
                                          context->target,
                                          &condition_value,
                                          &integer_condition_is_zero)) {
                condition_known = true;
                condition_is_zero = integer_condition_is_zero;
            }
        }
        if (condition_known) {
            const MinicBlock *discarded_source;
            const MinicBlock *selected_source;
'''
replace_once(if_old, if_new, "pointer condition pruning")

p.write_text(text)
print("MINIC_LOCAL_NULL_POINTER_FACTS_V0=APPLIED")
