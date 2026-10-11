#!/usr/bin/env python3
"""Do not use speculative local constants to eliminate branches with inline asm.

GNU WARN_ON macros embed volatile 'ebreak' inside a guarded do/while(0)
and a statement expression. The control-flow guard must be preserved if
the condition is not a pure expression constant. This is a narrow,
conservative correctness barrier: semantically constant if(0)/if(1)
can still be lowered by the existing strict source const-evaluator.
"""
from pathlib import Path

p = Path("src/core/core_lower.c")
s = p.read_text()

anchor = """static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
"""
helper = r'''/* INLINE_ASM_IF_FACT_BARRIER: a side-effecting GNU asm block must
   not be made unconditional by a guessed straight-line local fact. */
static bool core_block_contains_inline_asm_impl(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited) {
    size_t index;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || visited == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, block->statements[index]);
        const MinicBlock *nested;
        if (statement == NULL) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_INLINE_ASM) {
            return true;
        }
        if (statement->then_block != MINIC_BLOCK_INVALID) {
            if (statement->then_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->then_block]) {
                visited[statement->then_block] = true;
                nested = minic_c0_program_block(program, statement->then_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
        if (statement->else_block != MINIC_BLOCK_INVALID) {
            if (statement->else_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->else_block]) {
                visited[statement->else_block] = true;
                nested = minic_c0_program_block(program, statement->else_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool core_block_contains_inline_asm(
    const MinicCoreLowerContext *context, const MinicBlock *block) {
    bool *visited;
    bool contains;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || program->block_count == 0U) {
        return true;
    }
    visited = (bool *)calloc(program->block_count, sizeof(*visited));
    if (visited == NULL) {
        return true;
    }
    contains = core_block_contains_inline_asm_impl(context, block, visited);
    free(visited);
    return contains;
}

'''
if s.count(anchor) != 1:
    raise SystemExit(f"asm if helper anchor count={s.count(anchor)}")
s = s.replace(anchor, helper + anchor, 1)
old = """            if (core_const_eval_integer_with_locals(
                    context, statement->expression, &condition_value) &&
                minic_const_value_is_zero("""
new = """            if ((minic_const_eval_integer(context->body->program,
                                          context->target,
                                          statement->expression, &condition_value) ||
                 (!core_block_contains_inline_asm(context, then_source) &&
                  (else_source == NULL ||
                   !core_block_contains_inline_asm(context, else_source)) &&
                  core_const_eval_integer_with_locals(
                      context, statement->expression, &condition_value))) &&
                minic_const_value_is_zero("""
if s.count(old) != 1:
    raise SystemExit(f"asm if fact predicate final-profile anchor count={s.count(old)}")
s = s.replace(old, new, 1)
p.write_text(s)
print("MINIC_LOCAL_ASM_IF_FACT_BARRIER=APPLIED dynamic_asm_guard=true")
