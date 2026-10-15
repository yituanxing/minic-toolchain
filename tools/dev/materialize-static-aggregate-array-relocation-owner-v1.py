#!/usr/bin/env python3
"""Normalize and diagnose captured aggregate-array relocation replay ownership."""
from pathlib import Path

# A captured aggregate-array action stores relocation indexes relative to its
# flattened initializer payload. On replay the owner is the destination array,
# not the temporary record subobject that originally produced the relocation.
# Keep target metadata intact, but canonicalize the location to the array's
# aggregate-scalar slot namespace before calling the GlobalObject relocation API.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
start_marker = "    for (index = 0U; index < action->relocation_count; ++index) {\n"
start = text.find(start_marker)
if start < 0:
    raise SystemExit("cannot locate static aggregate array relocation replay block")
# Restrict normalization to this one relocation loop. Current permanent source
# replays union selections before relocations, so use the first closing marker
# that follows the loop rather than a later union-selection loop.
end_marker = "    return true;\n}\n"
end = text.find(end_marker, start)
if end < 0:
    raise SystemExit("cannot locate end of aggregate array materialization helper")
block = text[start:end]
old_location = "relocation->location_kind"
count = block.count(old_location)
if count != 0:
    block = block.replace(old_location, "MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR")
    text = text[:start] + block + text[end:]
    print(f"normalized {count} aggregate-array relocation replay call sites")
else:
    print("aggregate-array relocation owner already normalized")

# If a replay is rejected, expose the semantic state needed to distinguish
# invalid target identity, wrong active-union ownership, duplicate slot
# ownership, and slot-type resolution. This is generic compiler diagnostics,
# not Linux/index-specific handling.
old_diag = '''        if (!recorded) {
            minic_parser_error(parser,
                               "cannot materialize static aggregate array relocation "
                               "(captured-location=%u target=%u relative-slot=%zu "
                               "destination-slot=%zu)",
                               (unsigned int)relocation->location_kind,
                               (unsigned int)relocation->target_kind,
                               relocation->location_index,
                               location_index);
            return false;
        }
'''
new_diag = '''        if (!recorded) {
            const MinicGlobalObject *target_object;
            MinicType resolved_slot_type;
            size_t matching_union_count;
            size_t matching_union_field;
            size_t existing_at_slot;
            size_t probe_index;
            bool slot_type_ok;

            target_object = relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                                    relocation->target_id < parser->program->global_object_count
                                ? minic_c0_program_global_object(parser->program,
                                                                 relocation->target_id)
                                : NULL;
            slot_type_ok = minic_c0_global_relocation_slot_type(
                parser->program,
                object,
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                location_index,
                &resolved_slot_type);
            matching_union_count = 0U;
            matching_union_field = SIZE_MAX;
            for (probe_index = 0U; probe_index < action->union_selection_count; ++probe_index) {
                const MinicGlobalUnionSelection *selection;

                selection = &action->union_selections[probe_index];
                if (selection->initializer_slot <= SIZE_MAX - destination_begin &&
                    destination_begin + selection->initializer_slot == location_index) {
                    matching_union_count += 1U;
                    matching_union_field = selection->field_index;
                }
            }
            existing_at_slot = 0U;
            for (probe_index = 0U; probe_index < object->relocation_count; ++probe_index) {
                const MinicGlobalRelocation *existing;

                existing = &object->relocations[probe_index];
                if (existing->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                    existing->location_index == location_index) {
                    existing_at_slot += 1U;
                }
            }
            minic_parser_error(parser,
                               "cannot materialize static aggregate array relocation "
                               "(captured-location=%u target=%u target-id=%zu target-count=%zu "
                               "target-valid=%u target-name=%s relative-slot=%zu destination-slot=%zu "
                               "slot-ok=%u slot-pointer=%u slot-integer=%u init=%llu "
                               "action-unions=%zu matching-unions=%zu matching-field=%zu existing=%zu)",
                               (unsigned int)relocation->location_kind,
                               (unsigned int)relocation->target_kind,
                               relocation->target_id,
                               parser->program->global_object_count,
                               target_object != NULL ? 1U : 0U,
                               target_object != NULL ? target_object->name : "-",
                               relocation->location_index,
                               location_index,
                               slot_type_ok ? 1U : 0U,
                               slot_type_ok && minic_type_is_pointer(resolved_slot_type) ? 1U : 0U,
                               slot_type_ok && minic_type_is_integer(resolved_slot_type) ? 1U : 0U,
                               location_index < object->initializer_count
                                   ? (unsigned long long)object->initializer_values[location_index]
                                   : 0ULL,
                               action->union_selection_count,
                               matching_union_count,
                               matching_union_field,
                               existing_at_slot);
            return false;
        }
'''
if new_diag not in text:
    if old_diag not in text:
        # The pre-diagnostic generic form is accepted for idempotent rebuilds.
        generic = '''        if (!recorded) {
            minic_parser_error(parser, "cannot materialize static aggregate array relocation");
            return false;
        }
'''
        if generic not in text:
            raise SystemExit("aggregate-array relocation diagnostic anchor not found")
        text = text.replace(generic, new_diag, 1)
    else:
        text = text.replace(old_diag, new_diag, 1)
path.write_text(text)

# Make the permanent regression match the Linux shape precisely: an array of
# records whose nested field is a UNION, and the relocation selects a
# noncanonical pointer member of that union.
test_path = Path("tests/compiler/c0/static_aggregate_array_designators.c")
test = test_path.read_text()
old_shape = '''struct relocation_op {
    const char *lsm;
};

struct relocation_row {
    int *target;
    struct relocation_op op;
};
'''
new_shape = '''union relocation_op {
    int (*fallback)(void);
    const char *lsm;
};

struct relocation_row {
    int *target;
    union relocation_op op;
};
'''
if new_shape not in test:
    if old_shape not in test:
        raise SystemExit("aggregate-array union relocation regression shape not found")
    test = test.replace(old_shape, new_shape, 1)
test_path.write_text(test)

run_path = Path("tests/compiler/c0/run-static-aggregate-array-designators.sh")
run = run_path.read_text()
old_pass = "relocation-owner=aggregate-scalar"
new_pass = "relocation-owner=aggregate-scalar+nested-union"
if new_pass not in run:
    if old_pass not in run:
        raise SystemExit("aggregate-array regression pass marker not found")
    run = run.replace(old_pass, new_pass, 1)
run_path.write_text(run)
