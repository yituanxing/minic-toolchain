#!/usr/bin/env python3
from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    source = path.read_text()
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}")
    path.write_text(source.replace(old, new, 1))


replace_once(
    "src/compiler/compiler.c",
    """        candidates.statuses[function_index] =\n            minic_core_lower_function(&body, target, &candidates.functions[function_index]);\n        candidates.core_required[function_index] =\n            candidates.statuses[function_index] == MINIC_CORE_LOWER_OK;\n""",
    """        candidates.statuses[function_index] =\n            minic_core_lower_function(&body, target, &candidates.functions[function_index]);\n        if (function->name != NULL && strcmp(function->name, \"dump_kernel_instr\") == 0) {\n            (void)fprintf(stderr,\n                          \"M126A_ERROR_DIAG function=%s lower_status=%d instructions=%zu values=%zu blocks=%zu asms=%zu\\n\",\n                          function->name,\n                          (int)candidates.statuses[function_index],\n                          candidates.functions[function_index].instruction_count,\n                          candidates.functions[function_index].value_count,\n                          candidates.functions[function_index].block_count,\n                          candidates.functions[function_index].inline_asm_count);\n        }\n        candidates.core_required[function_index] =\n            candidates.statuses[function_index] == MINIC_CORE_LOWER_OK;\n""",
)

replace_once(
    "src/compiler/compiler.c",
    """        if (status == MINIC_CORE_LOWER_OK &&\n            !minic_core_function_verify(&candidates->functions[function_index])) {\n            status = MINIC_CORE_LOWER_ERROR;\n        }\n""",
    """        if (status == MINIC_CORE_LOWER_OK &&\n            !minic_core_function_verify(&candidates->functions[function_index])) {\n            if (function->name != NULL && strcmp(function->name, \"dump_kernel_instr\") == 0) {\n                (void)fprintf(stderr,\n                              \"M126A_ERROR_DIAG function=%s verifier=FAIL instructions=%zu values=%zu blocks=%zu asms=%zu\\n\",\n                              function->name,\n                              candidates->functions[function_index].instruction_count,\n                              candidates->functions[function_index].value_count,\n                              candidates->functions[function_index].block_count,\n                              candidates->functions[function_index].inline_asm_count);\n            }\n            status = MINIC_CORE_LOWER_ERROR;\n        } else if (status == MINIC_CORE_LOWER_OK && function->name != NULL &&\n                   strcmp(function->name, \"dump_kernel_instr\") == 0) {\n            (void)fprintf(stderr, \"M126A_ERROR_DIAG function=%s verifier=PASS\\n\", function->name);\n        }\n""",
)

replace_once(
    "src/core/core_lower.c",
    """    status = lower_parameter_ingress(&context);\n    terminated = false;\n    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);\n    free(statement_blocks); free(local_objects);\n""",
    """    status = lower_parameter_ingress(&context);\n    if (source_function->name != NULL && strcmp(source_function->name, \"dump_kernel_instr\") == 0) {\n        (void)fprintf(stderr,\n                      \"M126A_ERROR_DIAG function=%s stage=parameter_ingress status=%d instructions=%zu values=%zu blocks=%zu asms=%zu\\n\",\n                      source_function->name, (int)status, lowered.instruction_count, lowered.value_count,\n                      lowered.block_count, lowered.inline_asm_count);\n    }\n    terminated = false;\n    if (status == MINIC_CORE_LOWER_OK) {\n        status = lower_block(&context, source_block, &terminated);\n        if (source_function->name != NULL && strcmp(source_function->name, \"dump_kernel_instr\") == 0) {\n            (void)fprintf(stderr,\n                          \"M126A_ERROR_DIAG function=%s stage=lower_block status=%d terminated=%d instructions=%zu values=%zu blocks=%zu asms=%zu\\n\",\n                          source_function->name, (int)status, terminated ? 1 : 0, lowered.instruction_count,\n                          lowered.value_count, lowered.block_count, lowered.inline_asm_count);\n            (void)fprintf(stderr, \"M126A_CORE_DUMP_BEGIN function=%s\\n\", source_function->name);\n            (void)minic_core_function_dump(stderr, &lowered);\n            (void)fprintf(stderr, \"M126A_CORE_DUMP_END function=%s\\n\", source_function->name);\n        }\n    }\n    free(statement_blocks); free(local_objects);\n""",
)
