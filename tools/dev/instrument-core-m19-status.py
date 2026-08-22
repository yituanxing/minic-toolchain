#!/usr/bin/env python3
from pathlib import Path

path = Path("src/compiler/compiler.c")
text = path.read_text()
old = '''        status = candidates->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&candidates->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
'''
new = '''        status = candidates->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK) {
            bool verified = minic_core_function_verify(&candidates->functions[function_index]);
            (void)fprintf(stderr,
                          "CORE_M19_DIAG function=%s lower_status=%d verify=%d\\n",
                          function->name,
                          (int)status,
                          verified ? 1 : 0);
            if (!verified) {
                status = MINIC_CORE_LOWER_ERROR;
            }
        } else {
            (void)fprintf(stderr,
                          "CORE_M19_DIAG function=%s lower_status=%d verify=na\\n",
                          function->name,
                          (int)status);
        }
'''
if text.count(old) != 1:
    raise SystemExit("Core validation diagnostic anchor changed")
path.write_text(text.replace(old, new, 1))
print("instrumented Core M19 lowering/verifier status")
