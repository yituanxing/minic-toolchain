#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M132_FUNCTION_PHASE_CENSUS'
if marker in text:
    print('M132 function phase census already staged')
    raise SystemExit(0)

needle = '''    context.statement_block_count = body->program->statement_count;
    status = lower_parameter_ingress(&context);
    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);
    free(statement_blocks); free(local_objects);
'''
if needle not in text:
    raise SystemExit('minic_core_lower_function phase seam changed')

replacement = '''    context.statement_block_count = body->program->statement_count;
    status = lower_parameter_ingress(&context);
    /* M132_FUNCTION_PHASE_CENSUS: ephemeral CI-only phase attribution for
       unsupported functions that fail before the existing statement/expression
       trace points. Do not productize this diagnostic marker. */
    if (status != MINIC_CORE_LOWER_OK && getenv("CORE_PHASE_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "CORE_FUNCTION_PHASE function=%s phase=parameter-ingress status=%d\\n",
                      source_function->name,
                      (int)status);
    }
    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) {
        status = lower_block(&context, source_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK && getenv("CORE_PHASE_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "CORE_FUNCTION_PHASE function=%s phase=body status=%d\\n",
                          source_function->name,
                          (int)status);
        }
    }
    free(statement_blocks); free(local_objects);
'''

text = text.replace(needle, replacement, 1)
path.write_text(text)
print('M132 Core function phase census staged')
