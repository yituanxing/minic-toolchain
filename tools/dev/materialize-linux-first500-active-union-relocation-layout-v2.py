#!/usr/bin/env python3
"""Keep DataLayout active-union aware without linking frontend implementation objects."""
from pathlib import Path

path = Path("src/target/data_layout.c")
text = path.read_text()

helper = r'''static bool data_layout_global_object_union_member_selection(
    const MinicC0Program *program,
    const MinicGlobalObject *object,
    size_t initializer_slot,
    MinicRecordId record_id,
    size_t *field_index) {
    size_t index;

    if (program == NULL || object == NULL || field_index == NULL ||
        record_id >= program->record_count) {
        return false;
    }
    for (index = 0U; index < object->union_selection_count; ++index) {
        const MinicGlobalUnionSelection *selection;

        selection = &object->union_selections[index];
        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {
            *field_index = selection->field_index;
            return true;
        }
    }
    return false;
}

'''
anchor = "static bool aggregate_scalar_slot_layout_for_object("
if helper not in text:
    if text.count(anchor) != 1:
        raise SystemExit("aggregate layout helper anchor not found uniquely")
    text = text.replace(anchor, helper + anchor, 1)

old_call = "minic_c0_global_object_union_member_selection("
new_call = "data_layout_global_object_union_member_selection("
if old_call in text:
    if text.count(old_call) != 1:
        raise SystemExit("union selection call is not unique")
    text = text.replace(old_call, new_call, 1)
elif new_call not in text:
    raise SystemExit("union selection call anchor not found")

path.write_text(text)
