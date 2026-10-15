#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M179_TERMINATING_GUARD_FACTS"
if marker in text:
    print("MINIC_LOCAL_INTEGER_GUARD_FACTS_V0=ALREADY")
    raise SystemExit(0)

helper_anchor = "static void core_local_constants_clear_known(MinicCoreLowerContext *context) {\n"
helpers = r'''/* M179_TERMINATING_GUARD_FACTS: Core builds branch bodies before it emits
 * the condition block.  Keep one copy of the straight-line facts that reach an
 * uncertain if so condition lowering sees the correct incoming state.  When a
 * no-else true arm terminates (the common `if (err) return/goto` guard), the
 * only continuation is the false edge, so facts surviving condition evaluation
 * remain valid there instead of being discarded at a fictitious merge. */
static MinicCoreLocalIntegerConstant *core_local_constants_snapshot(
    const MinicCoreLowerContext *context) {
    MinicCoreLocalIntegerConstant *copy;
    size_t count;

    if (context == NULL || context->source_function == NULL) {
        return NULL;
    }
    count = context->source_function->local_count;
    if (count == 0U) {
        return NULL;
    }
    if (context->local_integer_constants == NULL ||
        count > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (MinicCoreLocalIntegerConstant *)malloc(count * sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy,
                 context->local_integer_constants,
                 count * sizeof(*copy));
    return copy;
}

static void core_local_constants_restore(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *snapshot) {
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || snapshot == NULL) {
        return;
    }
    count = context->source_function->local_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*snapshot)) {
        return;
    }
    (void)memcpy(context->local_integer_constants,
                 snapshot,
                 count * sizeof(*snapshot));
}

'''
count = text.count(helper_anchor)
if count != 1:
    raise SystemExit(f"guard fact helper anchor: expected one match, found {count}")
text = text.replace(helper_anchor, helpers + helper_anchor, 1)

vars_old = '''    MinicCoreBlockId continuation_block;
    MinicCoreLowerStatus status;
    bool else_terminated;
'''
vars_new = '''    MinicCoreBlockId continuation_block;
    MinicCoreLowerStatus status;
    MinicCoreLocalIntegerConstant *incoming_facts;
    bool else_terminated;
'''
count = text.count(vars_old)
if count != 1:
    raise SystemExit(f"guard fact lower_if vars: expected one match, found {count}")
text = text.replace(vars_old, vars_new, 1)

start_old = '''    core_local_constants_clear_known(context);
    condition_block = context->block_id;
'''
start_new = '''    incoming_facts = core_local_constants_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && incoming_facts == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    core_local_constants_clear_known(context);
    condition_block = context->block_id;
'''
count = text.count(start_old)
if count != 1:
    raise SystemExit(f"guard fact general-if start: expected one match, found {count}")
text = text.replace(start_old, start_new, 1)

condition_old = '''    continuation_block = context->block_id;
    context->block_id = condition_block;
    status = lower_condition_branch(
'''
condition_new = '''    continuation_block = context->block_id;
    context->block_id = condition_block;
    core_local_constants_restore(context, incoming_facts);
    status = lower_condition_branch(
'''
count = text.count(condition_old)
if count != 1:
    raise SystemExit(f"guard fact condition restore: expected one match, found {count}")
text = text.replace(condition_old, condition_new, 1)

error_old = '''                      statement->span.begin.column);
        return status;
    }
    context->block_id = continuation_block;
    core_local_constants_clear_known(context);
    *terminated = !needs_merge;
'''
error_new = '''                      statement->span.begin.column);
        free(incoming_facts);
        return status;
    }
    context->block_id = continuation_block;
    if (!(else_source == NULL && then_terminated)) {
        core_local_constants_clear_known(context);
    }
    free(incoming_facts);
    *terminated = !needs_merge;
'''
count = text.count(error_old)
if count != 1:
    raise SystemExit(f"guard fact lower_if finish: expected one match, found {count}")
text = text.replace(error_old, error_new, 1)

p.write_text(text)
print("MINIC_LOCAL_INTEGER_GUARD_FACTS_V0=APPLIED")
