#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/target/riscv64/codegen_function.c"
text = path.read_text()

old_global = '''        success =\n            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);\n    }\n'''
new_global = '''        success =\n            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);\n        if (!success) {\n            const MinicGlobalObject *failed_object = &program->global_objects[global_index];\n            (void)fprintf(stderr,\n                          \"RV64_WRITER_OWNER global index=%zu name=%s\\n\",\n                          global_index,\n                          failed_object->name != NULL ? failed_object->name : \"<unnamed>\");\n        }\n    }\n'''
if text.count(old_global) != 1:
    raise SystemExit(f"global writer anchor count={text.count(old_global)}")
text = text.replace(old_global, new_global, 1)

old_function = '''        } else {\n            success = minic_riscv64_emit_function(file, program, function, &label_counter);\n        }\n    }\n\n    if (!success) {\n'''
new_function = '''        } else {\n            success = minic_riscv64_emit_function(file, program, function, &label_counter);\n        }\n        if (!success) {\n            (void)fprintf(stderr,\n                          \"RV64_WRITER_OWNER function index=%zu name=%s core=%s\\n\",\n                          function_index,\n                          function->name != NULL ? function->name : \"<unnamed>\",\n                          core_function != NULL &&\n                                  minic_riscv64_core_function_can_emit_basic_v0_for_program(\n                                      program, core_function)\n                              ? \"yes\"\n                              : \"no\");\n        }\n    }\n\n    if (!success) {\n'''
if text.count(old_function) != 1:
    raise SystemExit(f"function writer anchor count={text.count(old_function)}")
text = text.replace(old_function, new_function, 1)

path.write_text(text)
