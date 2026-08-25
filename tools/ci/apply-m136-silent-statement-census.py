#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M136_SILENT_STATEMENT_CENSUS'
if marker in text:
    print('M136 silent statement census already staged')
    raise SystemExit(0)

needle = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (block_terminated) {
'''
replacement = '''        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M136_SILENT_STATEMENT_CENSUS: CI-only entry trace for the two
           post-M135 silent body failures. Hard-coded function selection is
           diagnostic only; semantic ownership remains function-agnostic. */
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            context->source_function != NULL && context->source_function->name != NULL &&
            (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
            (void)fprintf(stderr,
                          "CORE_M136_STMT_ENTRY function=%s statement_id=%llu kind=%d "
                          "span=%zu:%zu cleanup=%llu stop=%llu terminated=%d "
                          "expression=%llu target_expression=%llu then=%llu else=%llu\\n",
                          context->source_function->name,
                          (unsigned long long)source_block->statements[statement_index],
                          (int)statement->kind,
                          statement->span.begin.line,
                          statement->span.begin.column,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context,
                          block_terminated ? 1 : 0,
                          (unsigned long long)statement->expression,
                          (unsigned long long)statement->target_expression,
                          (unsigned long long)statement->then_block,
                          (unsigned long long)statement->else_block);
        }
        if (block_terminated) {
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one lower_block statement-entry seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)
path.write_text(text)
print('M136 silent statement census staged')
