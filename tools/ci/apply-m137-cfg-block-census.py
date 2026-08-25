#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M137_CFG_BLOCK_CENSUS'
if marker in text:
    print('M137 CFG block census already staged')
    raise SystemExit(0)

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
