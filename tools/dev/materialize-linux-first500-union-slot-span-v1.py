#!/usr/bin/env python3
"""Materialize explicit initializer spans for active union members."""
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Persist the semantic active member separately from the flattened initializer
# span inherited from already-materialized canonical storage.
path = Path("src/frontend/ast.h")
text = path.read_text()
marker = "size_t initializer_span;"
if marker not in text:
    text = replace_once(
        text,
        '''typedef struct MinicGlobalUnionSelection {\n    size_t initializer_slot;\n    MinicRecordId record_id;\n    size_t field_index;\n} MinicGlobalUnionSelection;\n''',
        '''typedef struct MinicGlobalUnionSelection {\n    size_t initializer_slot;\n    size_t initializer_span;\n    MinicRecordId record_id;\n    size_t field_index;\n} MinicGlobalUnionSelection;\n''',
        "union selection span field",
    )
    text = replace_once(
        text,
        '''bool minic_c0_global_object_select_union_member(MinicC0Program *program,\n                                                MinicGlobalObjectId global_object_id,\n                                                size_t initializer_slot,\n                                                MinicRecordId record_id,\n                                                size_t field_index);\nbool minic_c0_global_object_union_member_selection(const MinicC0Program *program,\n                                                   const MinicGlobalObject *object,\n                                                   size_t initializer_slot,\n                                                   MinicRecordId record_id,\n                                                   size_t *field_index);\n''',
        '''bool minic_c0_global_object_select_union_member(MinicC0Program *program,\n                                                MinicGlobalObjectId global_object_id,\n                                                size_t initializer_slot,\n                                                MinicRecordId record_id,\n                                                size_t field_index);\nbool minic_c0_global_object_select_union_member_with_span(MinicC0Program *program,\n                                                          MinicGlobalObjectId global_object_id,\n                                                          size_t initializer_slot,\n                                                          MinicRecordId record_id,\n                                                          size_t field_index,\n                                                          size_t initializer_span);\nbool minic_c0_global_object_union_member_selection(const MinicC0Program *program,\n                                                   const MinicGlobalObject *object,\n                                                   size_t initializer_slot,\n                                                   MinicRecordId record_id,\n                                                   size_t *field_index);\nbool minic_c0_global_object_union_member_initializer_span(const MinicC0Program *program,\n                                                          const MinicGlobalObject *object,\n                                                          size_t initializer_slot,\n                                                          MinicRecordId record_id,\n                                                          size_t *initializer_span);\n''',
        "union selection span declarations",
    )
    path.write_text(text)


path = Path("src/frontend/ast_global.c")
text = path.read_text()
marker = "minic_c0_global_object_select_union_member_with_span"
if marker not in text:
    old = '''bool minic_c0_global_object_union_member_selection(const MinicC0Program *program,\n                                                   const MinicGlobalObject *object,\n                                                   size_t initializer_slot,\n                                                   MinicRecordId record_id,\n                                                   size_t *field_index) {\n    size_t index;\n\n    if (program == NULL || object == NULL || field_index == NULL ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        const MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            *field_index = selection->field_index;\n            return true;\n        }\n    }\n    return false;\n}\n\nbool minic_c0_global_object_select_union_member(MinicC0Program *program,\n                                                MinicGlobalObjectId global_object_id,\n                                                size_t initializer_slot,\n                                                MinicRecordId record_id,\n                                                size_t field_index) {\n    MinicGlobalObject *object;\n    const MinicRecord *record;\n    size_t index;\n\n    if (program == NULL || global_object_id >= program->global_object_count ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    record = minic_c0_program_record(program, record_id);\n    if (record == NULL || !record->is_complete || !record->is_union ||\n        field_index >= record->field_count) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            selection->field_index = field_index;\n            return true;\n        }\n    }\n    if (!grow_array((void **)&object->union_selections,\n                    &object->union_selection_capacity,\n                    object->union_selection_count,\n                    sizeof(*object->union_selections))) {\n        return false;\n    }\n    object->union_selections[object->union_selection_count].initializer_slot = initializer_slot;\n    object->union_selections[object->union_selection_count].record_id = record_id;\n    object->union_selections[object->union_selection_count].field_index = field_index;\n    object->union_selection_count += 1U;\n    return true;\n}\n'''
    new = '''bool minic_c0_global_object_union_member_selection(const MinicC0Program *program,\n                                                   const MinicGlobalObject *object,\n                                                   size_t initializer_slot,\n                                                   MinicRecordId record_id,\n                                                   size_t *field_index) {\n    size_t index;\n\n    if (program == NULL || object == NULL || field_index == NULL ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        const MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            *field_index = selection->field_index;\n            return true;\n        }\n    }\n    return false;\n}\n\nbool minic_c0_global_object_union_member_initializer_span(const MinicC0Program *program,\n                                                          const MinicGlobalObject *object,\n                                                          size_t initializer_slot,\n                                                          MinicRecordId record_id,\n                                                          size_t *initializer_span) {\n    size_t index;\n\n    if (program == NULL || object == NULL || initializer_span == NULL ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        const MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            *initializer_span = selection->initializer_span;\n            return true;\n        }\n    }\n    return false;\n}\n\nbool minic_c0_global_object_select_union_member_with_span(MinicC0Program *program,\n                                                          MinicGlobalObjectId global_object_id,\n                                                          size_t initializer_slot,\n                                                          MinicRecordId record_id,\n                                                          size_t field_index,\n                                                          size_t initializer_span) {\n    MinicGlobalObject *object;\n    const MinicRecord *record;\n    const MinicRecordField *field;\n    size_t element_slots;\n    size_t selected_slots;\n    size_t index;\n\n    if (program == NULL || global_object_id >= program->global_object_count ||\n        record_id >= program->record_count) {\n        return false;\n    }\n    record = minic_c0_program_record(program, record_id);\n    field = record != NULL ? minic_c0_record_field(record, field_index) : NULL;\n    if (record == NULL || !record->is_complete || !record->is_union || field == NULL ||\n        field->element_count == 0U ||\n        !minic_c0_global_initializer_slot_count(program, field->type, &element_slots) ||\n        (element_slots != 0U && field->element_count > SIZE_MAX / element_slots)) {\n        return false;\n    }\n    selected_slots = field->element_count * element_slots;\n    object = &program->global_objects[global_object_id];\n    if (initializer_span != 0U &&\n        (selected_slots > initializer_span || initializer_slot > object->initializer_count ||\n         initializer_span > object->initializer_count - initializer_slot)) {\n        return false;\n    }\n    for (index = 0U; index < object->union_selection_count; ++index) {\n        MinicGlobalUnionSelection *selection;\n\n        selection = &object->union_selections[index];\n        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {\n            selection->field_index = field_index;\n            selection->initializer_span = initializer_span;\n            return true;\n        }\n    }\n    if (!grow_array((void **)&object->union_selections,\n                    &object->union_selection_capacity,\n                    object->union_selection_count,\n                    sizeof(*object->union_selections))) {\n        return false;\n    }\n    object->union_selections[object->union_selection_count].initializer_slot = initializer_slot;\n    object->union_selections[object->union_selection_count].initializer_span = initializer_span;\n    object->union_selections[object->union_selection_count].record_id = record_id;\n    object->union_selections[object->union_selection_count].field_index = field_index;\n    object->union_selection_count += 1U;\n    return true;\n}\n\nbool minic_c0_global_object_select_union_member(MinicC0Program *program,\n                                                MinicGlobalObjectId global_object_id,\n                                                size_t initializer_slot,\n                                                MinicRecordId record_id,\n                                                size_t field_index) {\n    return minic_c0_global_object_select_union_member_with_span(\n        program, global_object_id, initializer_slot, record_id, field_index, 0U);\n}\n'''
    text = replace_once(text, old, new, "union selection API")

    old = '''        field_begin = 0U;\n        field_end = record->field_count;\n        if (record->is_union) {\n            size_t selected;\n\n            selected = 0U;\n            (void)minic_c0_global_object_union_member_selection(\n                program, object, base_slot, type.record_id, &selected);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_end = selected + 1U;\n        }\n        total = 0U;\n'''
    new = '''        field_begin = 0U;\n        field_end = record->field_count;\n        if (record->is_union) {\n            size_t selected;\n\n            selected = 0U;\n            (void)minic_c0_global_object_union_member_selection(\n                program, object, base_slot, type.record_id, &selected);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_end = selected + 1U;\n        }\n        total = 0U;\n'''
    # Keep this anchor for the post-loop span adjustment below.
    if old not in text:
        raise SystemExit("object-aware union slot-count anchor not found")
    old_tail = '''        *slot_count = total;\n        return true;\n    }\n    return false;\n}\n\nstatic bool aggregate_scalar_slot_type_for_object'''
    new_tail = '''        if (record->is_union) {\n            size_t initializer_span;\n\n            initializer_span = 0U;\n            if (minic_c0_global_object_union_member_initializer_span(\n                    program, object, base_slot, type.record_id, &initializer_span) &&\n                initializer_span != 0U) {\n                if (total > initializer_span) {\n                    return false;\n                }\n                total = initializer_span;\n            }\n        }\n        *slot_count = total;\n        return true;\n    }\n    return false;\n}\n\nstatic bool aggregate_scalar_slot_type_for_object'''
    text = replace_once(text, old_tail, new_tail, "object-aware union slot span")
    path.write_text(text)


# Backward selection keeps the already-materialized canonical slot coordinates;
# only the selected member's semantic leaves are overwritten.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
marker = "selected union member needs more initializer slots than materialized storage"
if marker not in text:
    text = replace_once(
        text,
        '''            canonical_slots = canonical_field->element_count * canonical_element_slots;\n            selected_slots = field->element_count * selected_element_slots;\n            if (canonical_slots != selected_slots) {\n                minic_parser_error(parser,\n                                   "backward static union member changes flattened storage shape");\n                return false;\n            }\n''',
        '''            canonical_slots = canonical_field->element_count * canonical_element_slots;\n            selected_slots = field->element_count * selected_element_slots;\n            if (selected_slots > canonical_slots) {\n                minic_parser_error(\n                    parser,\n                    "selected union member needs more initializer slots than materialized storage");\n                return false;\n            }\n''',
        "backward union shape check",
    )
    text = replace_once(
        text,
        '''            union_record_id = (MinicRecordId)(current_record - parser->program->records);\n            if (!minic_c0_global_object_select_union_member(\n                    parser->program, object_id, slot_begin, union_record_id, field_index) ||\n                !overwrite_static_zero_field_value(parser, object_id, field, slot_begin)) {\n''',
        '''            union_record_id = (MinicRecordId)(current_record - parser->program->records);\n            if (!minic_c0_global_object_select_union_member_with_span(parser->program,\n                                                                     object_id,\n                                                                     slot_begin,\n                                                                     union_record_id,\n                                                                     field_index,\n                                                                     canonical_slots) ||\n                !overwrite_static_zero_field_value(parser, object_id, field, slot_begin)) {\n''',
        "backward union span selection",
    )
    path.write_text(text)


# DataLayout must advance its semantic slot cursor across the preserved span so
# relocations in fields following the union retain their canonical coordinates.
path = Path("src/target/data_layout.c")
text = path.read_text()
marker = "initializer_span != 0U && !*found"
if marker not in text:
    old = '''        const MinicRecord *record;\n        size_t field_begin;\n        size_t field_end;\n        size_t field_index;\n        size_t record_base_slot;\n'''
    new = '''        const MinicRecord *record;\n        size_t field_begin;\n        size_t field_end;\n        size_t field_index;\n        size_t initializer_span;\n        size_t record_base_slot;\n'''
    text = replace_once(text, old, new, "data-layout union span local")
    old = '''        record_base_slot = *slot_cursor;\n        field_begin = 0U;\n        field_end = record->field_count;\n        if (record->is_union && record->field_count != 0U) {\n            size_t selected;\n\n            selected = 0U;\n            (void)data_layout_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_end = selected + 1U;\n        }\n'''
    new = '''        record_base_slot = *slot_cursor;\n        initializer_span = 0U;\n        field_begin = 0U;\n        field_end = record->field_count;\n        if (record->is_union && record->field_count != 0U) {\n            size_t selected;\n\n            selected = 0U;\n            (void)data_layout_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);\n            (void)minic_c0_global_object_union_member_initializer_span(\n                program, object, record_base_slot, type.record_id, &initializer_span);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_end = selected + 1U;\n        }\n'''
    text = replace_once(text, old, new, "data-layout union span lookup")
    old = '''        }\n        return true;\n    }\n    return false;\n}\n\nbool minic_data_layout_global_relocation_offset'''
    new = '''        }\n        if (record->is_union && initializer_span != 0U && !*found) {\n            size_t consumed;\n\n            if (*slot_cursor < record_base_slot) {\n                return false;\n            }\n            consumed = *slot_cursor - record_base_slot;\n            if (consumed > initializer_span || record_base_slot > SIZE_MAX - initializer_span) {\n                return false;\n            }\n            *slot_cursor = record_base_slot + initializer_span;\n        }\n        return true;\n    }\n    return false;\n}\n\nbool minic_data_layout_global_relocation_offset'''
    text = replace_once(text, old, new, "data-layout union span advance")
    path.write_text(text)


# RV64 emits only the active member's physical bytes, then consumes any extra
# zero initializer slots that belong to the preserved flattened span.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "union_initializer_span"
if marker not in text:
    text = replace_once(
        text,
        '''        size_t field_limit;\n        size_t record_base_slot;\n        size_t record_storage_size;\n''',
        '''        size_t field_limit;\n        size_t record_base_slot;\n        size_t record_storage_size;\n        size_t union_initializer_span;\n''',
        "codegen union span local",
    )
    text = replace_once(
        text,
        '''        cursor = 0U;\n        field_begin = 0U;\n        field_limit = record->field_count;\n        if (record->is_union) {\n            size_t selected;\n\n            selected = 0U;\n            (void)minic_c0_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_limit = selected + 1U;\n        }\n''',
        '''        cursor = 0U;\n        union_initializer_span = 0U;\n        field_begin = 0U;\n        field_limit = record->field_count;\n        if (record->is_union) {\n            size_t selected;\n\n            selected = 0U;\n            (void)minic_c0_global_object_union_member_selection(\n                program, object, record_base_slot, type.record_id, &selected);\n            (void)minic_c0_global_object_union_member_initializer_span(\n                program, object, record_base_slot, type.record_id, &union_initializer_span);\n            if (selected >= record->field_count) {\n                return false;\n            }\n            field_begin = selected;\n            field_limit = selected + 1U;\n        }\n''',
        "codegen union span lookup",
    )
    text = replace_once(
        text,
        '''        if (cursor > record_storage_size ||\n            !minic_riscv64_emit_zero_bytes(file, record_storage_size - cursor)) {\n            return false;\n        }\n        *emitted_size = record_storage_size;\n        return true;\n''',
        '''        if (cursor > record_storage_size ||\n            !minic_riscv64_emit_zero_bytes(file, record_storage_size - cursor)) {\n            return false;\n        }\n        if (record->is_union && union_initializer_span != 0U) {\n            size_t span_end;\n            size_t slot;\n\n            if (record_base_slot > SIZE_MAX - union_initializer_span) {\n                return false;\n            }\n            span_end = record_base_slot + union_initializer_span;\n            if (*initializer_index > span_end || span_end > object->initializer_count) {\n                return false;\n            }\n            for (slot = *initializer_index; slot < span_end; ++slot) {\n                const MinicGlobalRelocation *relocation;\n\n                if (object->initializer_values[slot] != 0U) {\n                    return false;\n                }\n                relocation = *relocation_index < object->relocation_count\n                                 ? &object->relocations[*relocation_index]\n                                 : NULL;\n                if (relocation != NULL &&\n                    relocation->location_kind ==\n                        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&\n                    relocation->location_index < span_end) {\n                    return false;\n                }\n            }\n            *initializer_index = span_end;\n        }\n        *emitted_size = record_storage_size;\n        return true;\n''',
        "codegen union span consume",
    )
    path.write_text(text)


# Verify that explicit spans cover the selected semantic member and point only
# at already-materialized initializer storage.
path = Path("src/frontend/ast_verifier.c")
text = path.read_text()
marker = "selection->initializer_span != 0U"
if marker not in text:
    old = '''                selection = &object->union_selections[selection_index];\n                record = minic_c0_program_record(program, selection->record_id);\n                if (record == NULL || !record->is_complete || !record->is_union ||\n                    selection->field_index >= record->field_count ||\n                    selection->initializer_slot > object->initializer_count) {\n                    return false;\n                }\n'''
    new = '''                selection = &object->union_selections[selection_index];\n                record = minic_c0_program_record(program, selection->record_id);\n                if (record == NULL || !record->is_complete || !record->is_union ||\n                    selection->field_index >= record->field_count ||\n                    selection->initializer_slot > object->initializer_count) {\n                    return false;\n                }\n                if (selection->initializer_span != 0U) {\n                    const MinicRecordField *field;\n                    size_t element_slots;\n                    size_t selected_slots;\n\n                    field = minic_c0_record_field(record, selection->field_index);\n                    if (field == NULL || field->element_count == 0U ||\n                        !minic_c0_global_initializer_slot_count(\n                            program, field->type, &element_slots) ||\n                        (element_slots != 0U &&\n                         field->element_count > SIZE_MAX / element_slots)) {\n                        return false;\n                    }\n                    selected_slots = field->element_count * element_slots;\n                    if (selected_slots > selection->initializer_span ||\n                        selection->initializer_span >\n                            object->initializer_count - selection->initializer_slot) {\n                        return false;\n                    }\n                }\n'''
    text = replace_once(text, old, new, "union selection span verifier")
    path.write_text(text)


# Regression: materialize the canonical four-byte-member shape first, switch to
# a one-slot u32 active member, then emit a relocation after the union.
test_path = Path("tests/compiler/c0/static_union_shape_overlay.c")
if not test_path.exists():
    test_path.write_text('''struct target_node {\n    int value;\n};\n\nunion shape_union {\n    struct {\n        unsigned char a;\n        unsigned char b;\n        unsigned char c;\n        unsigned char d;\n    } bytes;\n    unsigned int word;\n};\n\nstruct holder {\n    union shape_union shape;\n    int after;\n    struct target_node *next;\n};\n\nstruct target_node target = { .value = 9 };\nstruct holder sample = {\n    .after = 7,\n    .shape.word = 0,\n    .next = &target,\n};\n\nint union_shape_overlay_probe(void) {\n    return sample.shape.word == 0 && sample.after == 7 && sample.next == &target;\n}\n''')

run_path = Path("tests/compiler/c0/run-static-union-zero-overlay.sh")
run = run_path.read_text()
marker = "static-union-shape-overlay span-preserved"
if marker not in run:
    anchor = '''grep -Fq 'target' "$work/active-relocation.s"\necho 'PASS compiler/c0/static-union-active-member zero+nonzero=accepted relocation=layout-aware'\n'''
    addition = anchor + '''"$host_cc" -E -P -std=gnu11 -x c \\
    "$root/tests/compiler/c0/static_union_shape_overlay.c" -o "$work/shape-overlay.i"\n"$minic" -S "$work/shape-overlay.i" -o "$work/shape-overlay.s"\ntest -s "$work/shape-overlay.s"\ngrep -Fq '.dword target' "$work/shape-overlay.s"\necho 'PASS compiler/c0/static-union-shape-overlay span-preserved relocation-after-union=correct'\n'''
    if anchor not in run:
        raise SystemExit("static union regression anchor not found")
    run_path.write_text(run.replace(anchor, addition, 1))
