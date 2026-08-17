#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/ast_global.c"
text = path.read_text()

old_array = '''        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {\n            size_t before = *slot_index;\n            if (aggregate_scalar_slot_type(\n                    program, array_type->element_type, slot_index, slot_type)) {\n                return true;\n            }\n            if (*slot_index == before) {\n                return false;\n            }\n        }\n'''
new_array = '''        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {\n            size_t before = *slot_index;\n            size_t element_slots;\n\n            if (aggregate_scalar_slot_type(\n                    program, array_type->element_type, slot_index, slot_type)) {\n                return true;\n            }\n            if (*slot_index == before &&\n                (!aggregate_scalar_slot_count(\n                     program, array_type->element_type, &element_slots) ||\n                 element_slots != 0U)) {\n                return false;\n            }\n        }\n'''

old_record = '''            for (element_index = 0U; element_index < field->element_count; ++element_index) {\n                size_t before = *slot_index;\n                if (aggregate_scalar_slot_type(program, field->type, slot_index, slot_type)) {\n                    return true;\n                }\n                if (*slot_index == before) {\n                    return false;\n                }\n            }\n'''
new_record = '''            for (element_index = 0U; element_index < field->element_count; ++element_index) {\n                size_t before = *slot_index;\n                size_t field_slots;\n\n                if (aggregate_scalar_slot_type(program, field->type, slot_index, slot_type)) {\n                    return true;\n                }\n                if (*slot_index == before &&\n                    (!aggregate_scalar_slot_count(program, field->type, &field_slots) ||\n                     field_slots != 0U)) {\n                    return false;\n                }\n            }\n'''

for old, new, label in [
    (old_array, new_array, "array traversal"),
    (old_record, new_record, "record traversal"),
]:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label} anchor count={count}")
    text = text.replace(old, new, 1)

path.write_text(text)
