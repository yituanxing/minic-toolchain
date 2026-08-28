#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M143_CLEANUP_LABEL_OWNER_TRACE'
if marker in text:
    print('M143 cleanup label owner trace already staged')
    raise SystemExit(0)
if 'M142_NONEDGE_CLEANUP_METADATA_OWNER' not in text:
    raise SystemExit('M143 requires staged M142 cleanup classification')

anchor = '''        /* M142_NONEDGE_CLEANUP_METADATA_OWNER: cleanup ids describe the
'''
trace = r'''        /* M143_CLEANUP_LABEL_OWNER_TRACE: diagnostics only. Determine whether
           a cleanup-bearing LABEL is parser loop-target metadata already owned
           by Core CFG, rather than an executable cleanup transition. */
        if (getenv("CORE_M143_TRACE") != NULL &&
            statement->kind == MINIC_STATEMENT_LABEL &&
            statement->cleanup_context != statement->cleanup_stop_context) {
            MinicStatementId m143_statement_id;
            MinicCoreBlockId m143_mapped;
            int m143_mapped_valid;
            int m143_mapped_terminated;
            int m143_next_kind;
            int m143_adjacent_pair;
            size_t m143_loop_matches;
            size_t m143_goto_targets;
            size_t m143_scan;

            m143_statement_id = source_block->statements[statement_index];
            m143_mapped = MINIC_CORE_BLOCK_INVALID;
            m143_mapped_valid = 0;
            m143_mapped_terminated = 0;
            if (context->statement_blocks != NULL &&
                m143_statement_id < context->statement_block_count) {
                m143_mapped = context->statement_blocks[m143_statement_id];
                if (m143_mapped != MINIC_CORE_BLOCK_INVALID &&
                    m143_mapped < context->function->block_count) {
                    m143_mapped_valid = 1;
                    m143_mapped_terminated =
                        context->function->blocks[m143_mapped].has_terminator ? 1 : 0;
                }
            }
            m143_next_kind = -1;
            m143_adjacent_pair = 0;
            if (statement_index + 1U < source_block->statement_count) {
                const MinicStatement *m143_next = minic_c0_program_statement(
                    context->body->program,
                    source_block->statements[statement_index + 1U]);
                if (m143_next != NULL) {
                    m143_next_kind = (int)m143_next->kind;
                    m143_adjacent_pair = internal_while_label_pair(statement, m143_next) ? 1 : 0;
                }
            }
            m143_loop_matches = 0U;
            m143_goto_targets = 0U;
            for (m143_scan = 0U;
                 m143_scan < context->body->program->statement_count;
                 ++m143_scan) {
                const MinicStatement *m143_candidate =
                    minic_c0_program_statement(context->body->program, m143_scan);
                if (m143_candidate == NULL) {
                    continue;
                }
                if (m143_candidate->kind == MINIC_STATEMENT_WHILE &&
                    internal_while_label_pair(statement, m143_candidate)) {
                    m143_loop_matches += 1U;
                }
                if (m143_candidate->kind == MINIC_STATEMENT_GOTO &&
                    m143_candidate->target_statement == m143_statement_id) {
                    m143_goto_targets += 1U;
                }
            }
            (void)fprintf(stderr,
                          "CORE_M143_LABEL function=%s statement=%llu block_index=%zu block_count=%zu cleanup=%llu stop=%llu mapped=%llu mapped_valid=%d mapped_terminated=%d next_kind=%d adjacent_pair=%d loop_matches=%zu goto_targets=%zu span=%zu:%zu\n",
                          context->source_function != NULL && context->source_function->name != NULL
                              ? context->source_function->name : "<unknown>",
                          (unsigned long long)m143_statement_id,
                          statement_index,
                          source_block->statement_count,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context,
                          (unsigned long long)m143_mapped,
                          m143_mapped_valid,
                          m143_mapped_terminated,
                          m143_next_kind,
                          m143_adjacent_pair,
                          m143_loop_matches,
                          m143_goto_targets,
                          statement->span.begin.line,
                          statement->span.begin.column);
        }

'''
if text.count(anchor) != 1:
    raise SystemExit(f'M143 expected one M142 guard anchor, found {text.count(anchor)}')
text = text.replace(anchor, trace + anchor, 1)
path.write_text(text)
print('M143 cleanup label owner trace staged')
