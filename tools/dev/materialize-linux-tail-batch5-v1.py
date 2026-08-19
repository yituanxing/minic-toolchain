#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    p.write_text(text.replace(old, new, 1))


# Inferred-bound aggregate arrays are intentionally incomplete while their
# initializer is being materialized.  Their scalar-slot type repeats with the
# element layout even before the final bound is known, just as the existing
# ARRAY_ELEMENT relocation owner already permits incomplete arrays.
replace_once(
    "src/frontend/ast_global.c",
    '''        array_type = minic_c0_program_array_type(program, type.array_type_id);\n        if (array_type == NULL || array_type->element_count == 0U) {\n            return false;\n        }\n        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {''',
    '''        array_type = minic_c0_program_array_type(program, type.array_type_id);\n        if (array_type == NULL) {\n            return false;\n        }\n        if (array_type->element_count == 0U && !array_type->is_zero_length) {\n            size_t element_slots;\n\n            if (!minic_c0_type_initializer_slot_count(\n                    program, array_type->element_type, &element_slots) ||\n                element_slots == 0U) {\n                return false;\n            }\n            *slot_index %= element_slots;\n            return aggregate_scalar_slot_type(\n                program, array_type->element_type, slot_index, slot_type);\n        }\n        if (array_type->element_count == 0U) {\n            return false;\n        }\n        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {''',
)

# Static-local inferred arrays already route through the shared static-storage
# initializer owner.  Remove the stale scalar-only guard and admit complete
# record elements into that same owner; unsupported element categories remain
# fail-closed.
replace_once(
    "src/frontend/parser_statement.c",
    '''        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) {\n            minic_parser_error(\n                parser,\n                "brace-initialized inferred static array requires integer or pointer elements");\n            return false;\n        }''',
    '''        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&\n            !minic_type_is_record(element_type)) {\n            minic_parser_error(\n                parser,\n                "brace-initialized inferred static array requires scalar or record elements");\n            return false;\n        }\n        if (minic_type_is_record(element_type) &&\n            !minic_parser_require_complete_object_type(\n                parser,\n                element_type,\n                "inferred static record array requires a complete element type")) {\n            return false;\n        }''',
)

p = Path("tests/compiler/c0/run.sh")
text = p.read_text()
gate = '''\n\nMINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-linux-tail-batch5.sh"\n'''
if 'run-linux-tail-batch5.sh' not in text:
    p.write_text(text.rstrip() + gate)

print('materialized linux tail batch5')
