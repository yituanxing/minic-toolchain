#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/ast_global.c"
text = path.read_text()

old_include = '''#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n'''
new_include = '''#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'''
if text.count(old_include) != 1:
    raise SystemExit(f"include anchor count={text.count(old_include)}")
text = text.replace(old_include, new_include, 1)

old_field_guard = '''            field = minic_c0_record_field(record, field_index);\n            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {\n                return false;\n            }\n'''
new_field_guard = '''            field = minic_c0_record_field(record, field_index);\n            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {\n                (void)fprintf(stderr,\n                              "SLOT_TRAVERSAL_STOP record=%zu field=%zu remaining=%zu "\n                              "field_null=%d element_count=%zu flexible=%d\\n",\n                              type.record_id,\n                              field_index,\n                              *slot_index,\n                              field == NULL ? 1 : 0,\n                              field == NULL ? 0U : field->element_count,\n                              field != NULL && field->is_flexible_array ? 1 : 0);\n                return false;\n            }\n'''
if text.count(old_field_guard) != 1:
    raise SystemExit(f"aggregate field guard count={text.count(old_field_guard)}")
text = text.replace(old_field_guard, new_field_guard, 1)

old_array_guard = '''        if (array_type == NULL || array_type->element_count == 0U) {\n            return false;\n        }\n'''
new_array_guard = '''        if (array_type == NULL || array_type->element_count == 0U) {\n            (void)fprintf(stderr,\n                          "SLOT_TRAVERSAL_ARRAY_STOP array=%zu remaining=%zu null=%d count=%zu\\n",\n                          type.array_type_id,\n                          *slot_index,\n                          array_type == NULL ? 1 : 0,\n                          array_type == NULL ? 0U : array_type->element_count);\n            return false;\n        }\n'''
if text.count(old_array_guard) != 1:
    raise SystemExit(f"aggregate array guard count={text.count(old_array_guard)}")
text = text.replace(old_array_guard, new_array_guard, 1)

old_record_tail = '''        for (field_index = 0U; field_index < field_limit; ++field_index) {\n            const MinicRecordField *field;\n            size_t element_index;\n\n            field = minic_c0_record_field(record, field_index);\n'''
if text.count(old_record_tail) != 1:
    raise SystemExit(f"record traversal anchor count={text.count(old_record_tail)}")
# The traversal body itself stays unchanged; add an exhaustion diagnostic at the
# unique end of the record branch.
old_end = '''            }\n        }\n    }\n    return false;\n}\n\nstatic bool global_object_member_path_type'''
new_end = '''            }\n        }\n        if (*slot_index != 0U) {\n            (void)fprintf(stderr,\n                          "SLOT_TRAVERSAL_EXHAUST record=%zu remaining=%zu fields=%zu union=%d\\n",\n                          type.record_id,\n                          *slot_index,\n                          record->field_count,\n                          record->is_union ? 1 : 0);\n        }\n    }\n    return false;\n}\n\nstatic bool global_object_member_path_type'''
if text.count(old_end) != 1:
    raise SystemExit(f"record exhaustion anchor count={text.count(old_end)}")
text = text.replace(old_end, new_end, 1)

anchor = '''    object = &program->global_objects[global_object_id];\n    if (!minic_c0_global_relocation_slot_type(\n            program, object, location_kind, location_index, &slot_type) ||\n'''
insert = '''    object = &program->global_objects[global_object_id];\n    {\n        bool trace_slot_ok;\n        bool trace_pointee_ok;\n        bool trace_path_ok = true;\n        bool trace_target_ok = true;\n        bool trace_count_ok;\n        size_t trace_slot_count = 0U;\n        MinicType trace_slot_type;\n        MinicType trace_pointee;\n        MinicType trace_target_type;\n\n        trace_slot_ok = minic_c0_global_relocation_slot_type(\n            program, object, location_kind, location_index, &trace_slot_type);\n        trace_count_ok = aggregate_scalar_slot_count(program, object->type, &trace_slot_count);\n        trace_pointee_ok = trace_slot_ok && minic_type_pointee(trace_slot_type, &trace_pointee);\n        if (trace_slot_ok && target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {\n            trace_path_ok = global_object_member_path_type(program,\n                                                           &program->global_objects[target_id],\n                                                           target_member_indices,\n                                                           target_member_depth,\n                                                           &trace_target_type);\n            trace_target_ok = trace_path_ok &&\n                              global_relocation_object_target_type_compatible(\n                                  program,\n                                  trace_slot_type,\n                                  trace_target_type,\n                                  has_explicit_pointer_cast);\n        }\n        if (!trace_slot_ok || !trace_pointee_ok || !trace_path_ok || !trace_target_ok ||\n            object->is_tentative) {\n            (void)fprintf(stderr,\n                          "RELOC_REJECT object=%zu target=%zu location_kind=%d location=%zu "\n                          "depth=%zu addend=%lld slot_ok=%d count_ok=%d slot_count=%zu "\n                          "pointee_ok=%d path_ok=%d target_ok=%d tentative=%d "\n                          "object_base=%d object_record=%zu slot_base=%d slot_ptr=%u\\n",\n                          global_object_id,\n                          target_id,\n                          (int)location_kind,\n                          location_index,\n                          target_member_depth,\n                          (long long)target_byte_addend,\n                          trace_slot_ok ? 1 : 0,\n                          trace_count_ok ? 1 : 0,\n                          trace_slot_count,\n                          trace_pointee_ok ? 1 : 0,\n                          trace_path_ok ? 1 : 0,\n                          trace_target_ok ? 1 : 0,\n                          object->is_tentative ? 1 : 0,\n                          (int)object->type.base_kind,\n                          object->type.record_id,\n                          trace_slot_ok ? (int)trace_slot_type.base_kind : -1,\n                          trace_slot_ok ? trace_slot_type.pointer_depth : 0U);\n        }\n    }\n    if (!minic_c0_global_relocation_slot_type(\n            program, object, location_kind, location_index, &slot_type) ||\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"relocation anchor count={text.count(anchor)}")
path.write_text(text.replace(anchor, insert, 1))
