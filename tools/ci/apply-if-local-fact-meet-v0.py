#!/usr/bin/env python3
from pathlib import Path

# Preserve integer-local facts across an unknown if only by a real data-flow
# meet.  Each branch starts from incoming facts that the condition cannot
# modify; branch-local assignments update those facts normally.  At a merge,
# retain a fact only when every continuing predecessor agrees on the same
# value.  Escaped/condition-modified locals remain unknown.  This is strictly
# compile-time CFG state and does not alter runtime Core control flow.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M192_IF_LOCAL_FACT_MEET"
if marker in text:
    print("MINIC_IF_LOCAL_FACT_MEET_V0=ALREADY")
    raise SystemExit(0)

lower_if_anchor = '''static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
'''
pos = text.find(lower_if_anchor)
if pos < 0:
    raise SystemExit("if fact meet: lower_if definition missing")

helpers = r'''
/* M192_IF_LOCAL_FACT_MEET */
static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth);

static void core_local_constants_prepare_if_branch(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    MinicExpressionId condition_expression) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        context->local_integer_constants[index] = incoming[index];
        if (incoming[index].escaped ||
            core_expression_may_modify_local(
                context, condition_expression, local_id, 0U)) {
            context->local_integer_constants[index].known = false;
            /* Structural branch lowering happens before condition lowering.
               Treat a condition that may expose/modify this local as escaped
               for branch fact purposes; this is conservative and prevents a
               later branch assignment from manufacturing an unsafe fact. */
            context->local_integer_constants[index].escaped = true;
        }
    }
}

static void core_local_constants_meet_if_paths(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *then_facts,
    bool then_continues,
    const MinicCoreLocalIntegerConstant *else_facts,
    bool else_continues) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return;
    }
    if (then_continues && !else_continues && then_facts != NULL) {
        core_local_constants_restore(context, then_facts);
        return;
    }
    if (!then_continues && else_continues && else_facts != NULL) {
        core_local_constants_restore(context, else_facts);
        return;
    }
    if (!then_continues && !else_continues) {
        core_local_constants_clear_known(context);
        return;
    }
    if (then_facts == NULL || else_facts == NULL) {
        core_local_constants_clear_known(context);
        return;
    }

    for (index = 0U; index < context->source_function->local_count; ++index) {
        const MinicCoreLocalIntegerConstant *left = &then_facts[index];
        const MinicCoreLocalIntegerConstant *right = &else_facts[index];
        MinicCoreLocalIntegerConstant merged = *left;
        bool same_value =
            left->known && right->known &&
            !left->escaped && !right->escaped &&
            minic_type_equal(left->value.type, right->value.type) &&
            left->value.bits == right->value.bits;

        merged.escaped = left->escaped || right->escaped;
        merged.specialization_modified =
            left->specialization_modified || right->specialization_modified;
        merged.specialization_known =
            left->specialization_known && right->specialization_known &&
            minic_type_equal(left->specialization_value.type,
                             right->specialization_value.type) &&
            left->specialization_value.bits == right->specialization_value.bits;
        merged.known = same_value && !merged.escaped;
        context->local_integer_constants[index] = merged;
    }
}

'''
text = text[:pos] + helpers + text[pos:]

old = '''    MinicCoreLowerStatus status;
    MinicCoreLocalIntegerConstant *incoming_facts;
    bool else_terminated;
'''
new = '''    MinicCoreLowerStatus status;
    MinicCoreLocalIntegerConstant *incoming_facts;
    MinicCoreLocalIntegerConstant *branch_entry_facts;
    MinicCoreLocalIntegerConstant *then_facts;
    MinicCoreLocalIntegerConstant *else_facts;
    bool else_terminated;
'''
if text.count(old) != 1:
    raise SystemExit(f"if fact meet declaration anchor: expected one, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
    }
    condition_block = context->block_id;
'''
new = '''    branch_entry_facts = NULL;
    then_facts = NULL;
    else_facts = NULL;
    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
        core_local_constants_prepare_if_branch(
            context, incoming_facts, statement->expression);
    }
    branch_entry_facts = core_local_constants_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && branch_entry_facts == NULL) {
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_block = context->block_id;
'''
if text.count(old) != 1:
    raise SystemExit(f"if fact meet branch-entry anchor: expected one, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    then_continuation_block = context->block_id;
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        core_local_constants_clear_known(context);
        context->block_id = else_block;
'''
new = '''    then_continuation_block = context->block_id;
    then_facts = core_local_constants_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && then_facts == NULL) {
        free(branch_entry_facts);
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        core_local_constants_restore(context, branch_entry_facts);
        context->block_id = else_block;
'''
if text.count(old) != 1:
    raise SystemExit(f"if fact meet then snapshot anchor: expected one, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''        }
        else_continuation_block = context->block_id;
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
'''
new = '''        }
        else_continuation_block = context->block_id;
        else_facts = core_local_constants_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U && else_facts == NULL) {
            free(then_facts);
            free(branch_entry_facts);
            free(incoming_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
'''
# This anchor can occur in other functions only very unlikely; scope to lower_if region.
start = text.find(lower_if_anchor)
end = text.find('\nstatic bool internal_while_label_pair', start)
region = text[start:end]
if region.count(old) != 1:
    raise SystemExit(f"if fact meet else snapshot anchor: expected one in lower_if, found {region.count(old)}")
region = region.replace(old, new, 1)
text = text[:start] + region + text[end:]

old = '''    context->block_id = continuation_block;
    if (!(else_source == NULL && then_terminated)) {
        core_local_constants_clear_known(context);
    }
    free(incoming_facts);
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
'''
new = '''    context->block_id = continuation_block;
    if (!(else_source == NULL && then_terminated)) {
        core_local_constants_meet_if_paths(
            context,
            then_facts,
            !then_terminated,
            else_source == NULL ? branch_entry_facts : else_facts,
            else_source == NULL ? true : !else_terminated);
    }
    free(else_facts);
    free(then_facts);
    free(branch_entry_facts);
    free(incoming_facts);
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
'''
start = text.find(lower_if_anchor)
end = text.find('\nstatic bool internal_while_label_pair', start)
region = text[start:end]
if region.count(old) != 1:
    raise SystemExit(f"if fact meet final merge anchor: expected one, found {region.count(old)}")
region = region.replace(old, new, 1)
text = text[:start] + region + text[end:]

p.write_text(text)
print("MINIC_IF_LOCAL_FACT_MEET_V0=APPLIED predecessor_agreement=1")
