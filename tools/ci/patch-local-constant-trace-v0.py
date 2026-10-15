#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

def repl(old, new):
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"expected one trace anchor, got {n}: {old[:100]!r}")
    text = text.replace(old, new, 1)

repl(
'''static void core_local_constant_invalidate(MinicCoreLowerContext *context,
                                           MinicLocalId local_id) {
    size_t index;
    if (core_local_constant_index(context, local_id, &index)) {
        context->local_integer_constants[index].known = false;
    }
}
''',
'''static void core_local_constant_invalidate(MinicCoreLowerContext *context,
                                           MinicLocalId local_id) {
    size_t index;
    if (core_local_constant_index(context, local_id, &index)) {
        if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL) {
            (void)fprintf(stderr, "LOCAL_CONST invalidate fn=%s local=%zu known=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)local_id,
                          context->local_integer_constants[index].known ? 1 : 0);
        }
        context->local_integer_constants[index].known = false;
    }
}
''')

repl(
'''    if (value == NULL || !core_local_constant_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
''',
'''    if (value == NULL || !core_local_constant_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL) {
            (void)fprintf(stderr, "LOCAL_CONST get-miss fn=%s local=%zu\\n",
                          context != NULL && context->source_function != NULL
                              ? context->source_function->name : "?",
                          (size_t)local_id);
        }
        return false;
    }
''')
repl(
'''    *value = context->local_integer_constants[index].value;
    return true;
}

static void core_local_constant_set''',
'''    *value = context->local_integer_constants[index].value;
    if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL) {
        (void)fprintf(stderr, "LOCAL_CONST get-hit fn=%s local=%zu bits=%llu\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (size_t)local_id,
                      (unsigned long long)value->bits);
    }
    return true;
}

static void core_local_constant_set''')

repl(
'''    context->local_integer_constants[index].value = converted;
    context->local_integer_constants[index].known = true;
}
''',
'''    context->local_integer_constants[index].value = converted;
    context->local_integer_constants[index].known = true;
    if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL) {
        (void)fprintf(stderr, "LOCAL_CONST set fn=%s local=%zu bits=%llu\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (size_t)local_id,
                      (unsigned long long)converted.bits);
    }
}
''')

repl(
'''static void core_local_constants_clear_known(MinicCoreLowerContext *context) {
    size_t index;
''',
'''static void core_local_constants_clear_known(MinicCoreLowerContext *context) {
    size_t index;

    if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL && context != NULL) {
        (void)fprintf(stderr, "LOCAL_CONST clear-all fn=%s\\n",
                      context->source_function != NULL ? context->source_function->name : "?");
    }
''')

repl(
'''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        if (core_value_integer_constant(context, stored_value, &stored_constant)) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
''',
'''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        bool stored_is_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        stored_is_constant = core_value_integer_constant(context, stored_value, &stored_constant);
        if (getenv("MINIC_LOCAL_CONSTANT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_CONST assignment fn=%s local=%zu source_kind=%d constant=%d value=%llu\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)target->value.local_id,
                          source != NULL ? (int)source->kind : -1,
                          stored_is_constant ? 1 : 0,
                          stored_is_constant ? (unsigned long long)stored_constant.bits : 0ULL);
        }
        if (stored_is_constant) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
''')

repl(
'''        if (core_const_eval_integer_with_locals(
                context, statement->expression, &condition_value) &&
            minic_const_value_is_zero(context->body->program,
''',
'''        if (core_const_eval_integer_with_locals(
                context, statement->expression, &condition_value) &&
            minic_const_value_is_zero(context->body->program,
''')
# Add trace immediately after the complete constant-if predicate block is hard to anchor
# portably; set/get/assignment/clear events are sufficient to identify fact loss.

p.write_text(text)
print("MINIC_LOCAL_CONSTANT_TRACE_PATCH=APPLIED")
