#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()

old_guard = '''        field_index = field_path.field_indices[0];\n        if (current_record->is_union && field_index != 0U) {\n            minic_parser_error(\n                parser, "nested static union designator requires the representable first member");\n            return false;\n        }\n        designator->field_indices[designator->depth++] = field_index;\n'''
new_guard = '''        field_index = field_path.field_indices[0];\n        /* Keep the selected union member in the semantic designator path. The\n         * static initializer owner decides below whether that member has a byte\n         * representation our compact flattened storage can preserve. */\n        designator->field_indices[designator->depth++] = field_index;\n'''
if text.count(old_guard) != 1:
    raise SystemExit(f"union designator guard anchor count={text.count(old_guard)}")
text = text.replace(old_guard, new_guard, 1)

old_head = '''    field_limit = record->is_union ? 1U : record->field_count;\n    selected_index = field_indices[0];\n    if (selected_index >= field_limit) {\n        return false;\n    }\n'''
new_head = '''    selected_index = field_indices[0];\n    if (selected_index >= record->field_count) {\n        return false;\n    }\n    if (record->is_union && selected_index != 0U) {\n        const MinicRecordField *selected_field;\n        uint64_t bits;\n\n        selected_field = &record->fields[selected_index];\n        /* The current static-storage representation flattens a union through its\n         * canonical first member and deliberately carries no active-member tag.\n         * A non-first scalar integer initialized to zero is nevertheless exactly\n         * representable: every byte of static union storage is zero regardless of\n         * which member supplied that value. Preserve that byte semantics without\n         * pretending arbitrary non-first union values are representable. */\n        if (depth != 1U || selected_field->element_count != 1U || selected_field->is_array ||\n            selected_field->is_bit_field || selected_field->is_flexible_array ||\n            !minic_type_is_integer(selected_field->type) ||\n            !minic_parser_parse_integer_initializer_bits(\n                parser, selected_field->type, &bits) ||\n            bits != 0U) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(\n                    parser,\n                    "noncanonical static union member requires a representable zero scalar");\n            }\n            return false;\n        }\n        return record->field_count != 0U &&\n               append_static_field_zeros(parser, object_id, &record->fields[0]);\n    }\n    field_limit = record->is_union ? 1U : record->field_count;\n'''
if text.count(old_head) != 1:
    raise SystemExit(f"union designator materialization anchor count={text.count(old_head)}")
text = text.replace(old_head, new_head, 1)

path.write_text(text)
