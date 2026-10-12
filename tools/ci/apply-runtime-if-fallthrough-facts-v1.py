#!/usr/bin/env python3
"""Retain only proven local facts at a pure dynamic if/fallthrough merge.

The Runtime profile clears facts before lowering dynamic if bodies. This
correctly prevents speculative use across a join, but also loses a fact
that agrees on BOTH reachable paths (e.g. a zero-initialized local which
remains zero under the optional then branch). In particular, the Linux
DMA-disabled-config path must not retain an unresolved arch_* reference.

This pass is deliberately limited to side-effect-free local reads and
their logical negation in a no-else if. It never changes Core CFG or
runtime expression evaluation. It does not run on label-reentry bodies.
"""
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
marker = "RUNTIME_IF_FALLTHROUGH_FACTS_V1"
if marker in text:
    print("MINIC_RUNTIME_IF_FALLTHROUGH_FACTS_V1=ALREADY")
    raise SystemExit(0)

begin = "static MinicCoreLowerStatus\nlower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {"
end = "\nstatic bool internal_while_label_pair("
if text.count(begin) != 1:
    raise SystemExit(f"IF_FACTS=ERROR lower_if_count={text.count(begin)}")
start = text.index(begin)
stop = text.index(end, start)
part = text[start:stop]

helper = r'''
/* RUNTIME_IF_FALLTHROUGH_FACTS_V1: a local read cannot update other local
   objects. Restrict to a simple guard, rather than guessing that a call,
   volatile load, assignment expression or inline asm is side-effect-free. */
static bool core_runtime_if_pure_local_guard(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    const MinicExpression *expression;
    const MinicLocal *local;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        return core_runtime_if_pure_local_guard(
            context, expression->value.unary.operand);
    }
    if (expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    local = minic_c0_program_local(
        context->body->program, expression->value.local_id);
    return local != NULL && !minic_type_is_volatile(local->type);
}

static MinicCoreLocalIntegerConstant *core_runtime_if_copy_facts(
    const MinicCoreLowerContext *context) {
    MinicCoreLocalIntegerConstant *copy;
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return NULL;
    }
    count = context->source_function->local_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (MinicCoreLocalIntegerConstant *)malloc(count * sizeof(*copy));
    if (copy != NULL) {
        (void)memcpy(copy, context->local_integer_constants,
                     count * sizeof(*copy));
    }
    return copy;
}

static void core_runtime_if_merge_false_fallthrough(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicCoreLocalIntegerConstant *then_exit,
    bool then_terminated) {
    size_t i;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (i = 0U; i < context->source_function->local_count; ++i) {
        const MinicCoreLocalIntegerConstant *before = &incoming[i];
        const MinicCoreLocalIntegerConstant *after =
            then_exit == NULL ? NULL : &then_exit[i];
        /* The false edge always continues with incoming local values.
           If the true edge also continues, it must prove the SAME value. */
        if (!before->known || before->escaped ||
            (!then_terminated &&
             (after == NULL || !after->known || after->escaped ||
              !minic_type_equal(before->value.type, after->value.type) ||
              before->value.bits != after->value.bits))) {
            continue;
        }
        context->local_integer_constants[i] = *before;
        if (!then_terminated && after != NULL) {
            context->local_integer_constants[i].specialization_modified =
                before->specialization_modified ||
                after->specialization_modified;
            context->local_integer_constants[i].escaped =
                before->escaped || after->escaped;
            if (context->local_integer_constants[i].specialization_modified) {
                context->local_integer_constants[i].specialization_known = false;
            }
        }
    }
}
'''
text = text[:start] + helper + text[start:]
start = text.index(begin)
stop = text.index(end, start)
part = text[start:stop]

def replace_one(old, new, why):
    global part
    n = part.count(old)
    if n != 1:
        raise SystemExit(f"IF_FACTS=ERROR {why}_count={n}")
    part = part.replace(old, new, 1)

replace_one(
    "    MinicCoreLowerStatus status;\n",
    "    MinicCoreLowerStatus status;\n"
    "    MinicCoreLocalIntegerConstant *fallthrough_facts;\n"
    "    MinicCoreLocalIntegerConstant *then_exit_facts;\n",
    "locals")
replace_one(
    "    core_local_constants_clear_known(context);\n"
    "    condition_block = context->block_id;\n",
    "    fallthrough_facts = NULL;\n"
    "    then_exit_facts = NULL;\n"
    "    if (else_source == NULL &&\n"
    "        core_runtime_if_pure_local_guard(context, statement->expression) &&\n"
    "        !core_block_contains_reentry_label(context, then_source)) {\n"
    "        fallthrough_facts = core_runtime_if_copy_facts(context);\n"
    "    }\n"
    "    core_local_constants_clear_known(context);\n"
    "    condition_block = context->block_id;\n",
    "entry")
replace_one(
    "    then_continuation_block = context->block_id;\n",
    "    then_continuation_block = context->block_id;\n"
    "    if (fallthrough_facts != NULL) {\n"
    "        then_exit_facts = core_runtime_if_copy_facts(context);\n"
    "    }\n",
    "then_exit")
replace_one(
    "    context->block_id = continuation_block;\n"
    "    core_local_constants_clear_known(context);\n",
    "    context->block_id = continuation_block;\n"
    "    core_local_constants_clear_known(context);\n"
    "    if (else_source == NULL) {\n"
    "        core_runtime_if_merge_false_fallthrough(\n"
    "            context, fallthrough_facts, then_exit_facts, then_terminated);\n"
    "    }\n"
    "    free(then_exit_facts);\n"
    "    free(fallthrough_facts);\n",
    "merge")
text = text[:start] + part + text[stop:]
path.write_text(text)
print("MINIC_RUNTIME_IF_FALLTHROUGH_FACTS_V1=APPLIED strict_pure_guard=1")
