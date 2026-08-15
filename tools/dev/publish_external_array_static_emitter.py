#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()

# The old helper only answered "is this an array of records?". Once all complete
# initialized arrays use the recursive constant emitter, keeping that classifier
# would preserve a dead type-specific fork.
begin = text.index("static bool minic_riscv64_record_array_info(")
end = text.index("\nstatic bool minic_riscv64_emit_record_array_values(", begin)
text = text[:begin] + text[end + 1 :]

old = '''static bool minic_riscv64_emit_record_array_values(FILE *file,\n                                                   const MinicC0Program *program,\n                                                   const MinicGlobalObject *object) {\n'''
new = '''static bool minic_riscv64_emit_array_values(FILE *file,\n                                            const MinicC0Program *program,\n                                            const MinicGlobalObject *object) {\n'''
if text.count(old) != 1:
    raise SystemExit("record-array emitter definition seam not found")
text = text.replace(old, new, 1)

old = '''    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||\n        !minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||\n        !minic_data_layout_global_object(\n'''
new = '''    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||\n        !minic_type_is_array(object->type) ||\n        !minic_data_layout_global_object(\n'''
if text.count(old) != 1:
    raise SystemExit("record-array emitter guard seam not found")
text = text.replace(old, new, 1)

old = '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (object->initializer_count == 0U) {\n            return false;\n        }\n'''
new = '''    } else if (minic_type_is_array(object->type)) {\n        if (object->initializer_count == 0U) {\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit("global array validation seam not found")
text = text.replace(old, new, 1)

old = '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL) &&\n               object->initializer_count != 0U) {\n        if (!minic_riscv64_emit_record_array_values(file, program, object)) {\n            return false;\n        }\n'''
new = '''    } else if (minic_type_is_array(object->type) && object->initializer_count != 0U) {\n        if (!minic_riscv64_emit_array_values(file, program, object)) {\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit("global array emission seam not found")
text = text.replace(old, new, 1)

path.write_text(text)
print("staged generic top-level array static-data emission")
