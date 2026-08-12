#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/target/riscv64/layout.c"
text = path.read_text()
old = """        object = &program->global_objects[object_index];\n        if (object->is_extern && minic_type_is_record(object->type)) {\n"""
new = """        object = &program->global_objects[object_index];\n        if (object->is_extern && minic_type_is_void(object->type)) {\n            object->storage_size = 0U;\n            object->alignment = 0U;\n            continue;\n        }\n        if (object->is_extern && minic_type_is_record(object->type)) {\n"""
if text.count(old) != 1:
    raise SystemExit(f"layout.c: expected exactly one global-layout anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
