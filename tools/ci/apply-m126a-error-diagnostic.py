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
    """        candidates.statuses[function_index] =\n            minic_core_lower_function(&body, target, &candidates.functions[function_index]);\n        if (function->name != NULL && strcmp(function->name, \"dump_kernel_instr\") == 0) {\n            (void)fprintf(stderr,\n                          \"M126A_ERROR_DIAG function=%s phase=prepare index=%zu lower_status=%d instructions=%zu values=%zu blocks=%zu asms=%zu\\n\",\n                          function->name, function_index,\n                          (int)candidates.statuses[function_index],\n                          candidates.functions[function_index].instruction_count,\n                          candidates.functions[function_index].value_count,\n                          candidates.functions[function_index].block_count,\n                          candidates.functions[function_index].inline_asm_count);\n        }\n        candidates.core_required[function_index] =\n            candidates.statuses[function_index] == MINIC_CORE_LOWER_OK;\n""",
)

replace_once(
    "src/compiler/compiler.c",
    """        status = candidates->statuses[function_index];\n        if (status == MINIC_CORE_LOWER_OK &&\n            !minic_core_function_verify(&candidates->functions[function_index])) {\n""",
    """        status = candidates->statuses[function_index];\n        if (function->name != NULL && strcmp(function->name, \"dump_kernel_instr\") == 0) {\n            (void)fprintf(stderr,\n                          \"M126A_ERROR_DIAG function=%s phase=validate index=%zu stored_status=%d core_required=%d instructions=%zu values=%zu blocks=%zu\\n\",\n                          function->name, function_index, (int)status,\n                          candidates->core_required[function_index] ? 1 : 0,\n                          candidates->functions[function_index].instruction_count,\n                          candidates->functions[function_index].value_count,\n                          candidates->functions[function_index].block_count);\n        }\n        if (status == MINIC_CORE_LOWER_OK &&\n            !minic_core_function_verify(&candidates->functions[function_index])) {\n""",
)
