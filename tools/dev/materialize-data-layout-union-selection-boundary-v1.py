#!/usr/bin/env python3
"""Keep DataLayout self-contained when reading active-union initializer metadata."""
from pathlib import Path

path = Path("src/target/data_layout.c")
text = path.read_text()
old_helper = '''static bool data_layout_global_object_union_member_selection(const MinicC0Program *program,\n                                                             const MinicGlobalObject *object,\n                                                             size_t initializer_slot,\n                                                             MinicRecordId record_id,\n                                                             size_t *field_index) {\n    size_t index;\n\n    if (program == NULL || object == NULL || field_index == NULL ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        const MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            *field_index = selection->field_index;\n            return true;\n        }\n    }\n    return false;\n}\n'''
new_helper = '''static bool data_layout_global_object_union_member_selection(const MinicC0Program *program,\n                                                             const MinicGlobalObject *object,\n                                                             size_t initializer_slot,\n                                                             MinicRecordId record_id,\n                                                             size_t *field_index,\n                                                             size_t *initializer_span) {\n    size_t index;\n\n    if (program == NULL || object == NULL || field_index == NULL ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        const MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            *field_index = selection->field_index;\n            if (initializer_span != NULL) {\n                *initializer_span = selection->initializer_span;\n            }\n            return true;\n        }\n    }\n    return false;\n}\n'''
if new_helper not in text:
    if text.count(old_helper) != 1:
        raise SystemExit("unexpected DataLayout union-selection helper shape")
    text = text.replace(old_helper, new_helper, 1)
old_call = '''            (void)data_layout_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);\n            (void)minic_c0_global_object_union_member_initializer_span(\n                program, object, record_base_slot, type.record_id, &initializer_span);\n'''
new_call = '''            (void)data_layout_global_object_union_member_selection(program,\n                                                                   object,\n                                                                   record_base_slot,\n                                                                   type.record_id,\n                                                                   &selected,\n                                                                   &initializer_span);\n'''
if new_call not in text:
    if text.count(old_call) != 1:
        raise SystemExit("unexpected DataLayout union-selection call shape")
    text = text.replace(old_call, new_call, 1)
# Adjust any other local helper calls to the widened signature without introducing frontend linkage.
text = text.replace('''data_layout_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);''',
                    '''data_layout_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected, NULL);''')
path.write_text(text)
print("DATA_LAYOUT_UNION_SELECTION_BOUNDARY_V1")
