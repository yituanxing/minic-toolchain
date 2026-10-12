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
/* RUNTIME_IF_FALLTHROUGH_FACTS_V1: preserve unescaped locals through a
   precisely constrained read-only-local guard or direct call whose arguments
   cannot provide an address of a local being propagated. */
/* A direct function call can mutate global memory, but cannot write a
   currently unescaped automatic local unless its address is supplied.
   Accept only direct function calls whose arguments contain no address-of,
   assignments, increments, volatile reads, or arbitrary expressions.
   The Core fact table independently tracks local address escapes. */
static bool core_runtime_if_guard_preserves_locals_depth(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicLocal *local;
    size_t i;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || depth > 24U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        local = minic_c0_program_local(program, expression->value.local_id);
        return local != NULL && !minic_type_is_volatile(local->type);
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        /* Direct calls store the function_id, not a source callee expr.
           Indirect calls have function_id == MINIC_FUNCTION_INVALID. */
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
            minic_c0_program_function(
                program, expression->value.call.function_id) == NULL) {
            return false;
        }
        for (i = 0U; i < expression->value.call.argument_count; ++i) {
            if (!core_runtime_if_guard_preserves_locals_depth(
                    context, expression->value.call.arguments[i], depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool core_runtime_if_pure_local_guard(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    return core_runtime_if_guard_preserves_locals_depth(context, id, 0U);
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

# Scoped observation of the actual preprocessed DMA guard, before the
# constant-if special case. Evaluator is read-only: this does not alter IR.
if "MINIC_DMA_IF_TRACE_V0" not in part:
    dma_trace = r'''    /* MINIC_DMA_IF_TRACE_V0 */
    if (getenv("MINIC_DMA_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "dma_direct_alloc") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = core_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            core_local_constant_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "DMA_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
'''
    marker = "    /* BusyBox and ordinary GNU C intentionally leave impossible references in"
    if part.count(marker) != 1:
        raise SystemExit(f"IF_FACTS=ERROR dma_trace_anchor_count={part.count(marker)}")
    part = part.replace(marker, dma_trace + marker, 1)

    shmem_trace = r'''    /* MINIC_SHMEM_IF_TRACE_V0 */
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = core_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            core_local_constant_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "SHMEM_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
'''
    part = part.replace(marker, shmem_trace + marker, 1)

def replace_one(old, new, why):
    global part
    n = part.count(old)
    if n != 1:
        raise SystemExit(f"IF_FACTS=ERROR {why}_count={n}")
    part = part.replace(old, new, 1)

replace_one(
    "    MinicCoreLowerStatus status;\n",
    "    MinicCoreLowerStatus status;\n"
    "    MinicCoreLocalIntegerConstant *fallthrough_facts = NULL;\n"
    "    MinicCoreLocalIntegerConstant *then_exit_facts = NULL;\n",
    "locals")
# The older Runtime transforms are allowed to insert extra guards between
# this fact invalidation and condition_block. The FIRST invalidation in the
# dynamic lower_if body is the reachability boundary.
entry_anchor = "    core_local_constants_clear_known(context);\n"
if entry_anchor not in part:
    raise SystemExit("IF_FACTS=ERROR entry_clear_missing")
part = part.replace(
    entry_anchor,
    "    fallthrough_facts = NULL;\n"
    "    then_exit_facts = NULL;\n"
    "    if (else_source == NULL &&\n"
    "        core_runtime_if_pure_local_guard(context, statement->expression) &&\n"
    "        !core_block_contains_reentry_label(context, then_source)) {\n"
    "        fallthrough_facts = core_runtime_if_copy_facts(context);\n"
    "    }\n"
    "    if (getenv(\"MINIC_SHMEM_IF_TRACE\") != NULL &&\n"
    "        context->source_function != NULL &&\n"
    "        strcmp(context->source_function->name, \"shmem_alloc_and_acct_folio\") == 0) {\n"
    "        size_t dbg_idx;\n"
    "        if (core_local_constant_index(context, (MinicLocalId)10022U, &dbg_idx)) {\n"
    "            (void)fprintf(stderr, \"SHMEM_IF_FLOW line=%zu edge=entry huge_known=%d val=%llu snap=%d pure=%d reentry=%d\\n\",\n"
    "               statement->span.begin.line,\n"
    "               context->local_integer_constants[dbg_idx].known ? 1 : 0,\n"
    "               (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,\n"
    "               fallthrough_facts != NULL ? 1 : 0,\n"
    "               core_runtime_if_pure_local_guard(context, statement->expression) ? 1 : 0,\n"
    "               core_block_contains_reentry_label(context, then_source) ? 1 : 0);\n"
    "        }\n"
    "    }\n"
    + entry_anchor, 1)
replace_one(
    "    then_continuation_block = context->block_id;\n",
    "    then_continuation_block = context->block_id;\n"
    "    if (fallthrough_facts != NULL) {\n"
    "        then_exit_facts = core_runtime_if_copy_facts(context);\n"
    "    }\n",
    "then_exit")
# At the dynamic if exit, there are two *different* sources of facts:
# 1. the pre-branch incoming snapshot, which reaches the false successor;
# 2. the then-body exit snapshot, which reaches the join only if it did not
#    terminate.  Meet at the unconditional continuation after the legacy
#    dominance barrier, NOT inside its conditional clear branch.
#
# In particular "if (err) goto failed;" has no continuing true edge.
# The old rfind("    core_local_constants_clear_known") matched the LAST
# four spaces of an eight-space indented call inside
# "if (!(else_source == NULL && then_terminated))".  That skipped our
# restoration precisely when the true branch terminated, leaking the
# snapshots as well as losing the known huge=false fact in Linux shmem.
exit_anchor = "    free(incoming_facts);\\n    *terminated = !needs_merge;\\n"
if part.count(exit_anchor) != 1:
    raise SystemExit(f"IF_FACTS=ERROR continuation_count={part.count(exit_anchor)}")
exit_merge = (
    "    if (else_source == NULL) {\\n"
    "        core_runtime_if_merge_false_fallthrough(\\n"
    "            context, fallthrough_facts, then_exit_facts, then_terminated);\\n"
    "    }\\n"
    "    free(then_exit_facts);\\n"
    "    free(fallthrough_facts);\\n"
    "    if (getenv(\\\"MINIC_SHMEM_IF_TRACE\\\") != NULL &&\\n"
    "        context->source_function != NULL &&\\n"
    "        strcmp(context->source_function->name, \\\"shmem_alloc_and_acct_folio\\\") == 0) {\\n"
    "        size_t dbg_idx;\\n"
    "        if (core_local_constant_index(context, (MinicLocalId)10022U, &dbg_idx)) {\\n"
    "            (void)fprintf(stderr, \\\"SHMEM_IF_FLOW line=%zu edge=merge huge_known=%d val=%llu terminated=%d\\\\n\\\",\\n"
    "                statement->span.begin.line,\\n"
    "                context->local_integer_constants[dbg_idx].known ? 1 : 0,\\n"
    "                (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,\\n"
    "                then_terminated ? 1 : 0);\\n"
    "        }\\n"
    "    }\\n"
)
part = part.replace(exit_anchor, exit_merge + exit_anchor, 1)
text = text[:start] + part + text[stop:]
path.write_text(text)
print("MINIC_RUNTIME_IF_FALLTHROUGH_FACTS_V1=APPLIED strict_pure_guard=1")
