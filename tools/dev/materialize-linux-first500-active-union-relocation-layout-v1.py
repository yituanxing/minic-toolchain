#!/usr/bin/env python3
"""Materialize active-union-aware aggregate relocation layout and regressions."""
from pathlib import Path

layout_path = Path("src/target/data_layout.c")
text = layout_path.read_text()

start_marker = "static bool aggregate_scalar_slot_layout("
end_marker = "\nbool minic_data_layout_global_relocation_offset("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("aggregate scalar layout helper anchors not found")

replacement = r'''static bool aggregate_scalar_slot_layout_for_object(
    const MinicDataLayout *layout,
    const MinicC0Program *program,
    const MinicGlobalObject *object,
    MinicType type,
    size_t base_offset,
    size_t *slot_cursor,
    size_t target_slot,
    MinicType *slot_type,
    size_t *slot_offset,
    bool *found) {
    if (layout == NULL || program == NULL || object == NULL || slot_cursor == NULL ||
        slot_type == NULL || slot_offset == NULL || found == NULL) {
        return false;
    }
    if (*found || *slot_cursor > target_slot) {
        return true;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) || minic_type_is_float(type) ||
        minic_type_is_double(type) || minic_type_is_enum(type)) {
        if (*slot_cursor == target_slot) {
            if (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) {
                return false;
            }
            *slot_type = type;
            *slot_offset = base_offset;
            *found = true;
            return true;
        }
        if (*slot_cursor == SIZE_MAX) {
            return false;
        }
        *slot_cursor += 1U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_alignment;
        size_t element_size;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL ||
            !minic_data_layout_type(
                layout, program, array_type->element_type, &element_size, &element_alignment)) {
            return false;
        }
        (void)element_alignment;
        if (array_type->is_zero_length) {
            return true;
        }
        element_index = 0U;
        while (array_type->element_count == 0U || element_index < array_type->element_count) {
            size_t before_cursor;
            size_t element_offset;

            if (array_type->element_count == 0U && *slot_cursor > target_slot) {
                break;
            }
            if (element_index > SIZE_MAX / element_size ||
                base_offset > SIZE_MAX - element_index * element_size) {
                return false;
            }
            element_offset = base_offset + element_index * element_size;
            before_cursor = *slot_cursor;
            if (!aggregate_scalar_slot_layout_for_object(layout,
                                                         program,
                                                         object,
                                                         array_type->element_type,
                                                         element_offset,
                                                         slot_cursor,
                                                         target_slot,
                                                         slot_type,
                                                         slot_offset,
                                                         found)) {
                return false;
            }
            if (*found) {
                return true;
            }
            if (*slot_cursor == before_cursor) {
                return false;
            }
            if (element_index == SIZE_MAX) {
                return false;
            }
            element_index += 1U;
        }
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_begin;
        size_t field_end;
        size_t field_index;
        size_t record_base_slot;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        record_base_slot = *slot_cursor;
        field_begin = 0U;
        field_end = record->field_count;
        if (record->is_union && record->field_count != 0U) {
            size_t selected;

            selected = 0U;
            (void)minic_c0_global_object_union_member_selection(
                program, object, record_base_slot, type.record_id, &selected);
            if (selected >= record->field_count) {
                return false;
            }
            field_begin = selected;
            field_end = selected + 1U;
        }
        for (field_index = field_begin; field_index < field_end; ++field_index) {
            const MinicRecordField *field;
            size_t element_alignment;
            size_t element_count;
            size_t element_index;
            size_t element_size;
            size_t field_offset;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U ||
                !minic_data_layout_record_field_offset(
                    layout, program, record, field_index, &field_offset) ||
                !minic_data_layout_type(
                    layout, program, field->type, &element_size, &element_alignment)) {
                return false;
            }
            (void)element_alignment;
            if (field->is_zero_length_array) {
                continue;
            }
            element_count = field->element_count;
            if (field->is_flexible_array) {
                element_count = base_offset == 0U && record_base_slot == 0U &&
                                        minic_type_equal(type, object->type)
                                    ? object->flexible_array_initializer_count
                                    : 0U;
            }
            for (element_index = 0U; element_index < element_count; ++element_index) {
                size_t element_offset;

                if (element_index > SIZE_MAX / element_size ||
                    field_offset > SIZE_MAX - element_index * element_size ||
                    base_offset > SIZE_MAX - field_offset - element_index * element_size) {
                    return false;
                }
                element_offset = base_offset + field_offset + element_index * element_size;
                if (!aggregate_scalar_slot_layout_for_object(layout,
                                                             program,
                                                             object,
                                                             field->type,
                                                             element_offset,
                                                             slot_cursor,
                                                             target_slot,
                                                             slot_type,
                                                             slot_offset,
                                                             found)) {
                    return false;
                }
                if (*found || *slot_cursor > target_slot) {
                    return true;
                }
            }
        }
        return true;
    }
    return false;
}
'''
text = text[:start] + replacement + text[end:]

old_head = '''    if (layout == NULL || program == NULL || object == NULL || relocation == NULL ||
        offset == NULL ||
        !minic_data_layout_type(layout, program, object->type, &object_size, &object_alignment)) {
        return false;
    }
'''
new_head = '''    if (layout == NULL || program == NULL || object == NULL || relocation == NULL ||
        offset == NULL ||
        !minic_data_layout_global_object(layout, program, object, &object_size, &object_alignment)) {
        return false;
    }
'''
if new_head not in text:
    if text.count(old_head) != 1:
        raise SystemExit("global relocation object-layout anchor not found uniquely")
    text = text.replace(old_head, new_head, 1)

old_aggregate = '''    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
        MinicType slot_type;
        size_t remaining;
        size_t slot_alignment;

        remaining = relocation->location_index;
        if (!aggregate_scalar_slot_layout(
                layout, program, object->type, 0U, &remaining, &slot_type, &resolved_offset) ||
            (!minic_type_is_pointer(slot_type) && !minic_type_is_integer(slot_type)) ||
            !minic_data_layout_type(
                layout, program, slot_type, &relocation_width, &slot_alignment)) {
            return false;
        }
        (void)slot_alignment;
'''
new_aggregate = '''    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
        MinicType slot_type;
        bool found;
        size_t slot_alignment;
        size_t slot_cursor;

        found = false;
        slot_cursor = 0U;
        if (!aggregate_scalar_slot_layout_for_object(layout,
                                                     program,
                                                     object,
                                                     object->type,
                                                     0U,
                                                     &slot_cursor,
                                                     relocation->location_index,
                                                     &slot_type,
                                                     &resolved_offset,
                                                     &found) ||
            !found || (!minic_type_is_pointer(slot_type) && !minic_type_is_integer(slot_type)) ||
            !minic_data_layout_type(
                layout, program, slot_type, &relocation_width, &slot_alignment)) {
            return false;
        }
        (void)slot_alignment;
'''
if new_aggregate not in text:
    if text.count(old_aggregate) != 1:
        raise SystemExit("aggregate relocation offset anchor not found uniquely")
    text = text.replace(old_aggregate, new_aggregate, 1)

layout_path.write_text(text)

active_case = Path("tests/compiler/c0/static_union_active_member_relocation.c")
active_case.write_text(
    '''static int target = 7;\n\n'''
    '''union payload {\n    long canonical;\n    struct {\n        int first;\n        int second;\n    } pair;\n};\n\n'''
    '''struct holder {\n    union payload payload;\n    int *pointer;\n};\n\n'''
    '''static struct holder state = {\n'''
    '''    .payload.pair = { .first = 1, .second = 2 },\n'''
    '''    .pointer = &target,\n'''
    '''};\n\n'''
    '''int main(void) {\n'''
    '''    return (state.payload.pair.first == 1 && state.payload.pair.second == 2 &&\n'''
    '''            state.pointer == &target && *state.pointer == 7)\n'''
    '''               ? 0\n'''
    '''               : 1;\n'''
    '''}\n'''
)

run_path = Path("tests/compiler/c0/run-static-union-zero-overlay.sh")
run_text = run_path.read_text()
old_run = '''"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo "expected nonzero noncanonical union overlay rejection" >&2
  exit 1
fi
cat "$work/invalid.err"
grep -Fq 'backward noncanonical static union member requires a zero initializer' "$work/invalid.err"
echo 'PASS compiler/c0/static-union-zero-overlay zero-noncanonical=accepted nonzero=fail-closed'
'''
new_run = '''"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/nonzero.i"
"$minic" -S "$work/nonzero.i" -o "$work/nonzero.s"
test -s "$work/nonzero.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_active_member_relocation.c" -o "$work/active-relocation.i"
"$minic" -S "$work/active-relocation.i" -o "$work/active-relocation.s"
test -s "$work/active-relocation.s"
grep -Fq 'target' "$work/active-relocation.s"
echo 'PASS compiler/c0/static-union-active-member zero+nonzero=accepted relocation=layout-aware'
'''
if new_run not in run_text:
    if run_text.count(old_run) != 1:
        raise SystemExit("static union regression anchor not found uniquely")
    run_text = run_text.replace(old_run, new_run, 1)
run_path.write_text(run_text)

runtime_path = Path("tests/compiler/c0/run-runtime.sh")
runtime_text = runtime_path.read_text()
runtime_line = "run_case static_union_active_member_relocation 0 static_union_active_member_relocation\n"
if runtime_line not in runtime_text:
    anchor = "run_case inferred_static_unsigned_char_list 0 inferred_static_unsigned_char_list\n"
    if runtime_text.count(anchor) != 1:
        raise SystemExit("runtime regression anchor not found uniquely")
    runtime_text = runtime_text.replace(anchor, anchor + runtime_line, 1)
runtime_path.write_text(runtime_text)
