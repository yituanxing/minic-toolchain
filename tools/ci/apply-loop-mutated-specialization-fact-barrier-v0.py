#!/usr/bin/env python3
"""Invalidate mutable specialized facts before lowering a loop condition.

A specialized integer parameter such as jhash(key, 16, seed) length begins
as a known value, but length -= 12 in its loop body invalidates the fact
along the back edge. Before lowering an uncertain loop, find only those
specialized parameter facts that this loop may modify, and invalidate them.
"""
from pathlib import Path
p=Path("src/core/core_lower.c")
s=p.read_text()
anchor="static void core_restore_loop_invariant_facts(\n"
helper=r'''/* MUTABLE_SPECIALIZATION_LOOP_BARRIER: do not substitute an incoming
   specialized parameter constant for a variable updated within the loop. */
static void core_invalidate_loop_mutated_specialization_facts(
    MinicCoreLowerContext *context, const MinicStatement *loop_statement) {
    const MinicBlock *body;
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || context->body == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    body = minic_c0_program_block(context->body->program, loop_statement->then_block);
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        const MinicCoreLocalIntegerConstant *fact =
            &context->local_integer_constants[index];
        bool may_mutate;
        if (!fact->specialization_known || fact->specialization_modified) {
            continue;
        }
        may_mutate =
            body == NULL ||
            (loop_statement->expression != MINIC_EXPRESSION_INVALID &&
             core_expression_may_modify_local(
                 context, loop_statement->expression, local_id, 0U)) ||
            core_block_may_modify_local(context, body, local_id, 0U);
        if (may_mutate) {
            core_local_constant_invalidate(context, local_id);
            if (getenv("MINIC_LOOP_MUTATED_FACT_TRACE") != NULL) {
                (void)fprintf(stderr,
                              "LOOP_SPECIALIZATION_FACT_KILL function=%s local=%zu\n",
                              context->source_function->name != NULL
                                  ? context->source_function->name : "?",
                              (size_t)local_id);
            }
        }
    }
}

'''
if s.count(anchor)!=1:
    raise SystemExit(f"loop helper anchor={s.count(anchor)}")
s=s.replace(anchor,helper+anchor,1)
old='''                if (!local_fact_single_pass) {
                    core_local_constants_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                status = lower_while(
'''
new='''                if (!local_fact_single_pass) {
                    /* Loop-body changes invalidate specialized parameters
                       before the initial condition and later backedges. */
                    core_invalidate_loop_mutated_specialization_facts(context, statement);
                    core_local_constants_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                status = lower_while(
'''
if s.count(old)!=1: raise SystemExit(f"loop entry anchor={s.count(old)}")
s=s.replace(old,new,1)
p.write_text(s)
print("MINIC_MUTABLE_SPECIALIZATION_LOOP_BARRIER=APPLIED")
