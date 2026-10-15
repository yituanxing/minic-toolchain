#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER'
if marker in core:
    print('M148 normalized-for update continue CFG already staged')
    raise SystemExit(0)
for required in ('M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER',
                 'M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER'):
    if required not in core:
        raise SystemExit(f'M148 requires staged {required}')

old_decl = '''    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
'''
new_decl = '''    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreBlockId update_block;
'''
if core.count(old_decl) != 1:
    raise SystemExit(f'M148 expected one loop block declaration seam, found {core.count(old_decl)}')
core = core.replace(old_decl, new_decl, 1)

old_alloc = '''    preheader_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
new_alloc = '''    preheader_block = context->block_id;
    update_block = MINIC_CORE_BLOCK_INVALID;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        (normalized_for && for_update != NULL &&
         continue_label_statement != MINIC_STATEMENT_INVALID &&
         !minic_core_function_add_block(context->function, &update_block)) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
if core.count(old_alloc) != 1:
    raise SystemExit(f'M148 expected one loop block allocation seam, found {core.count(old_alloc)}')
core = core.replace(old_alloc, new_alloc, 1)

old_binding = '''    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: M145/M146 proved that the
       same-span detached-label heuristic must not mutate an ordinary source
       WHILE CFG.  Keep binding at the established post-block-allocation point,
       but require the body-shape recognizers above to have independently proven
       parser-normalized `for` provenance first.  This preserves the valid
       condition_block lifetime/order required by unbounded `for (;;)` while
       leaving plain while/do-while lowering on the baseline path. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID && normalized_for) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->statement_blocks[continue_label_statement] = condition_block;
    }
'''
new_binding = '''    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: M145/M146 proved that the
       same-span detached-label heuristic must not mutate an ordinary source
       WHILE CFG.  Keep binding at the established post-block-allocation point,
       but require parser-normalized `for` provenance first. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID && normalized_for) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: C `continue` in a
           source for-loop executes the iteration expression before condition
           re-evaluation.  A no-update/unbounded for therefore targets the
           condition block directly, while an update-bearing for with a proven
           continue gets a dedicated update block.  Loops without a recovered
           continue keep the historical inline-update lowering unchanged. */
        context->statement_blocks[continue_label_statement] =
            for_update != NULL ? update_block : condition_block;
    }
'''
if core.count(old_binding) != 1:
    raise SystemExit(f'M148 expected one M147 binding seam, found {core.count(old_binding)}')
core = core.replace(old_binding, new_binding, 1)

old_tail = '''    if (!body_terminated && for_update != NULL) {
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id < context->function->block_count &&
            context->function->blocks[context->block_id].has_terminator) {
            body_terminated = true;
        }
    }
    if (!body_terminated) {
        status = set_branch(context, context->block_id, statement->span, condition_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
new_tail = '''    if (for_update != NULL && update_block != MINIC_CORE_BLOCK_INVALID) {
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: converge both natural
           body fallthrough and every continue edge at one update owner, then
           evaluate the update exactly once before the condition backedge. */
        if (!body_terminated) {
            status = set_branch(context, context->block_id, statement->span, update_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        context->block_id = update_block;
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id < context->function->block_count &&
            context->function->blocks[context->block_id].has_terminator) {
            body_terminated = true;
        } else {
            body_terminated = false;
            status = set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    } else {
        if (!body_terminated && for_update != NULL) {
            status = lower_expression_statement(context, for_update);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (context->block_id < context->function->block_count &&
                context->function->blocks[context->block_id].has_terminator) {
                body_terminated = true;
            }
        }
        if (!body_terminated) {
            status = set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
'''
if core.count(old_tail) != 1:
    raise SystemExit(f'M148 expected one inline update/backedge seam, found {core.count(old_tail)}')
core = core.replace(old_tail, new_tail, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m148_for_update_continue_cfg.c')
regression.write_text(r'''static int update_calls;

static int next_i(int i) {
    update_calls += 1;
    return i + 1;
}

static int update_continue(int limit) {
    int i;
    int sum = 0;
    update_calls = 0;
    for (i = 0; i < limit; i = next_i(i)) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum + update_calls * 100;
}

static int no_update_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

int main(void) {
    return update_continue(5) == 506 && no_update_continue(4) == 4 ? 0 : 1;
}
''')

print('M148 normalized-for update continue CFG staged')
