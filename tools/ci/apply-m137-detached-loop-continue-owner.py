#!/usr/bin/env python3
from pathlib import Path

# M137_PRODUCTIZER_TRIGGER_V1: keep this apply script as the synchronize trigger
# after registering the productizer; semantic source is still staged only in CI.
core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M137_DETACHED_LOOP_CONTINUE_OWNER'
if marker in core:
    print('M137 detached loop continue owner already staged')
    raise SystemExit(0)

needle = '''static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
'''
helper = '''/* M137_DETACHED_LOOP_CONTINUE_OWNER: Core occasionally lowers a normalized
   block view in which a parser-owned while continue label is no longer adjacent
   to its WHILE statement.  The source AST still carries the exact semantic
   relation: a direct GOTO inside the loop subtree targets an otherwise-empty
   internal label whose source position is identical to the loop.  Recover that
   unique relation before lowering the body so CORE_LOOP_CONTINUE_TARGET_V1 can
   bind the label to the real condition block instead of letting the first
   continue manufacture an orphan label block.  Ambiguous candidates remain
   fail-closed. */
static bool core_resolve_detached_while_continue_label(
    const MinicCoreLowerContext *context,
    const MinicStatement *loop,
    MinicStatementId *continue_label_statement) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    MinicStatementId resolved;
    size_t source_index;

    if (continue_label_statement == NULL) {
        return false;
    }
    *continue_label_statement = MINIC_STATEMENT_INVALID;
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || loop->kind != MINIC_STATEMENT_WHILE) {
        return false;
    }
    program = context->body->program;
    if (program->statement_count == 0U ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership) ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks)) {
        return false;
    }
    visited_blocks = program->block_count == 0U
                         ? NULL
                         : (bool *)calloc(program->block_count, sizeof(*visited_blocks));
    statement_membership =
        (bool *)calloc(program->statement_count, sizeof(*statement_membership));
    if ((program->block_count != 0U && visited_blocks == NULL) ||
        statement_membership == NULL) {
        free(visited_blocks);
        free(statement_membership);
        return false;
    }
    if (!core_mark_block_statement_membership(context,
                                              loop->then_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count) ||
        !core_mark_block_statement_membership(context,
                                              loop->else_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count)) {
        free(visited_blocks);
        free(statement_membership);
        return false;
    }

    resolved = MINIC_STATEMENT_INVALID;
    for (source_index = 0U; source_index < program->statement_count; ++source_index) {
        const MinicStatement *source;
        const MinicStatement *target;

        if (!statement_membership[source_index]) {
            continue;
        }
        source = minic_c0_program_statement(program, source_index);
        if (source == NULL) {
            free(visited_blocks);
            free(statement_membership);
            return false;
        }
        if (source->kind != MINIC_STATEMENT_GOTO ||
            source->target_expression != MINIC_EXPRESSION_INVALID ||
            source->expression != MINIC_EXPRESSION_INVALID ||
            source->target_statement == MINIC_STATEMENT_INVALID ||
            source->target_statement >= program->statement_count) {
            continue;
        }
        target = minic_c0_program_statement(program, source->target_statement);
        if (target == NULL || !internal_while_label_pair(target, loop)) {
            continue;
        }
        if (resolved != MINIC_STATEMENT_INVALID && resolved != source->target_statement) {
            free(visited_blocks);
            free(statement_membership);
            return false;
        }
        resolved = source->target_statement;
    }

    free(visited_blocks);
    free(statement_membership);
    *continue_label_statement = resolved;
    return true;
}

static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
'''
if core.count(needle) != 1:
    raise SystemExit(f'expected one lower_block definition, found {core.count(needle)}')
core = core.replace(needle, helper, 1)

needle = '''            case MINIC_STATEMENT_WHILE:
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                break;
'''
replacement = '''            case MINIC_STATEMENT_WHILE: {
                MinicStatementId detached_continue_label;

                if (!core_resolve_detached_while_continue_label(
                        context, statement, &detached_continue_label)) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                status = lower_while(
                    context, statement, detached_continue_label, &statement_terminated);
                break;
            }
'''
if core.count(needle) != 1:
    raise SystemExit(f'expected one standalone while lowering seam, found {core.count(needle)}')
core = core.replace(needle, replacement, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m137_detached_loop_continue.c')
regression.write_text(r'''static int nested_while_continue(int x) {
    for (;;) {
        while (x > 0) {
            x -= 1;
            if (x & 1)
                continue;
            if (x == 0)
                return 7;
        }
        return 3;
    }
}

static int double_nested_continue(int x) {
    while (x > 0) {
        while (x > 1) {
            x -= 2;
            if (x > 3)
                continue;
            return x;
        }
        return x;
    }
    return 0;
}

int main(void) {
    return nested_while_continue(4) + double_nested_continue(6) == 10 ? 0 : 1;
}
''')

print('M137 detached loop continue owner and regression staged')
