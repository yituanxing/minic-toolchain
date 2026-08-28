#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()

old = '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
'''
new = '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type) &&
             !minic_type_is_array(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M170 array object: expected 1 preflight seam, got {count}")
text = text.replace(old, new, 1)
PATH.write_text(text)
print("M170_ARRAY_OBJECT_APPLIED")
