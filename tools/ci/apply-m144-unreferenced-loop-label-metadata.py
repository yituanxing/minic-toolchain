#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M144_UNREFERENCED_LOOP_LABEL_METADATA_OWNER'
if marker in text:
    print('M144 unreferenced loop label metadata owner already staged')
    raise SystemExit(0)
if 'M137_DETACHED_LOOP_CONTINUE_OWNER' not in text or \
   'M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER' not in text or \
   'M142_NONEDGE_CLEANUP_METADATA_OWNER' not in text:
    raise SystemExit('M144 requires staged M137/M141/M142 ownership')

needle = '''static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
'''
helper = r'''/* M144_UNREFERENCED_LOOP_LABEL_METADATA_OWNER: parser loop normalization can
   leave an otherwise-empty label at the condition tail even when no source
   continue/goto refers to it.  internal_while_label_pair() gives this label a
   strong identity: its source position is exactly that of one normalized WHILE.
   Treat it as non-executable parser metadata only when that owner is unique and
   the label has no direct goto, asm-goto, or &&label reference anywhere in the
   function program. Any real control-flow use remains owned by ordinary LABEL /
   GOTO lowering and therefore stays fail-closed here. */
static bool core_unreferenced_internal_loop_label(
    const MinicCoreLowerContext *context,
    const MinicStatement *label,
    MinicStatementId label_id) {
    const MinicC0Program *program;
    size_t loop_matches;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        label == NULL || label->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }
    program = context->body->program;
    if (label_id >= program->statement_count) {
        return false;
    }

    loop_matches = 0U;
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *candidate = minic_c0_program_statement(program, index);
        if (candidate == NULL) {
            return false;
        }
        if (candidate->kind == MINIC_STATEMENT_WHILE &&
            internal_while_label_pair(label, candidate)) {
            loop_matches += 1U;
            if (loop_matches > 1U) {
                return false;
            }
        }
        if (candidate->kind == MINIC_STATEMENT_GOTO &&
            candidate->target_statement == label_id) {
            return false;
        }
        if (candidate->kind == MINIC_STATEMENT_INLINE_ASM &&
            candidate->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm = &program->inline_asms[candidate->inline_asm_id];
            size_t label_index;
            if (inline_asm->is_goto) {
                for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                    if (inline_asm->labels[label_index].target_statement == label_id) {
                        return false;
                    }
                }
            }
        }
    }
    if (loop_matches != 1U) {
        return false;
    }
    for (index = 0U; index < program->expression_count; ++index) {
        const MinicExpression *expression = minic_c0_program_expression(program, index);
        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            expression->value.label_statement_id == label_id) {
            return false;
        }
    }
    return true;
}

static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
'''
if text.count(needle) != 1:
    raise SystemExit(f'M144 expected one lower_block definition, found {text.count(needle)}')
text = text.replace(needle, helper, 1)

old = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (block_terminated) {
'''
new = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (statement->kind == MINIC_STATEMENT_LABEL &&
            core_unreferenced_internal_loop_label(
                context, statement, source_block->statements[statement_index])) {
            continue;
        }
        if (block_terminated) {
'''
if text.count(old) != 1:
    raise SystemExit(f'M144 expected one lower_block statement entry, found {text.count(old)}')
text = text.replace(old, new, 1)
path.write_text(text)

regression = Path('tests/compiler/c0/m144_unreferenced_loop_label_metadata.c')
regression.write_text(r'''static void cleanup_int(int *value) {
    *value += 1;
}

static int cleanup_loop_tail(int seed) {
    int total = seed;
    int i;
    for (i = 0; i < 3; i += 1) {
        int guard __attribute__((cleanup(cleanup_int))) = i;
        while (guard < 1)
            guard += 1;
        total += guard;
    }
    return total;
}

static int ordinary_source_label(int x) {
    if (x > 0)
        goto done;
    x = 7;
done:
    return x;
}

int main(void) {
    return cleanup_loop_tail(2) == 6 &&
           ordinary_source_label(3) == 3 &&
           ordinary_source_label(0) == 7 ? 0 : 1;
}
''')

print('M144 unreferenced loop label metadata owner and regression staged')
