#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M137_CFG_BLOCK_CENSUS'
if marker in text:
    print('M137 CFG block census already staged')
    raise SystemExit(0)

# Attribute blocks allocated for source labels/goto targets. If an orphan block
# came from ensure_statement_block(), its statement id and source label become
# immediately visible; otherwise the allocation belongs to a structured CFG
# builder such as if/while/switch.
needle = '''    if (mapped == MINIC_CORE_BLOCK_INVALID) {
        if (!minic_core_function_add_block(context->function, &mapped)) return MINIC_CORE_LOWER_ERROR;
        context->statement_blocks[statement_id] = mapped;
    }
'''
replacement = '''    if (mapped == MINIC_CORE_BLOCK_INVALID) {
        if (!minic_core_function_add_block(context->function, &mapped)) return MINIC_CORE_LOWER_ERROR;
        context->statement_blocks[statement_id] = mapped;
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            context->source_function != NULL && context->source_function->name != NULL &&
            (strcmp(context->source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(context->source_function->name, "membarrier_global_expedited") == 0)) {
            const MinicStatement *trace_statement = context->body != NULL &&
                                                    context->body->program != NULL
                                                        ? minic_c0_program_statement(
                                                              context->body->program, statement_id)
                                                        : NULL;
            (void)fprintf(stderr,
                          "CORE_M137_LABEL_BLOCK_ALLOC function=%s block=%llu statement_id=%llu kind=%d span=%zu:%zu\\n",
                          context->source_function->name,
                          (unsigned long long)mapped,
                          (unsigned long long)statement_id,
                          trace_statement != NULL ? (int)trace_statement->kind : -1,
                          trace_statement != NULL ? trace_statement->span.begin.line : 0U,
                          trace_statement != NULL ? trace_statement->span.begin.column : 0U);
        }
    }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one statement block allocation seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)

needle = '''    if (!minic_core_function_verify(&lowered)) {
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            (strcmp(source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(source_function->name, "membarrier_global_expedited") == 0)) {
            (void)fprintf(stderr, "CORE_M136_VERIFY_FAIL function=%s\\n", source_function->name);
        }
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
replacement = '''    /* M137_CFG_BLOCK_CENSUS: CI-only structural attribution. The verifier
       requires every allocated Core block to carry a terminator, including
       unreachable synthetic merge/exit blocks. Identify orphan blocks without
       changing the verifier or CFG semantics. */
    if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
        (strcmp(source_function->name, "get_nohz_timer_target") == 0 ||
         strcmp(source_function->name, "membarrier_global_expedited") == 0)) {
        size_t trace_block_index;
        for (trace_block_index = 0U; trace_block_index < lowered.block_count;
             ++trace_block_index) {
            const MinicCoreBlock *trace_block = &lowered.blocks[trace_block_index];
            if (!trace_block->has_terminator) {
                (void)fprintf(stderr,
                              "CORE_M137_ORPHAN_BLOCK function=%s block=%zu instructions=%zu current=%d\\n",
                              source_function->name,
                              trace_block_index,
                              trace_block->instruction_count,
                              context.block_id == (MinicCoreBlockId)trace_block_index ? 1 : 0);
            }
        }
    }
    if (!minic_core_function_verify(&lowered)) {
        if (getenv("CORE_M136_STATEMENT_TRACE") != NULL &&
            (strcmp(source_function->name, "get_nohz_timer_target") == 0 ||
             strcmp(source_function->name, "membarrier_global_expedited") == 0)) {
            (void)fprintf(stderr, "CORE_M136_VERIFY_FAIL function=%s\\n", source_function->name);
        }
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
if text.count(needle) != 1:
    raise SystemExit(f'expected one M136 verifier seam, found {text.count(needle)}')
text = text.replace(needle, replacement, 1)
path.write_text(text)
print('M137 CFG block census staged')
