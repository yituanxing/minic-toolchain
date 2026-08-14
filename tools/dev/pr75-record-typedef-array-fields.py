#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


# A typedef may denote an array type. When such a typedef is used directly as a record member,
# preserve its array identity instead of rejecting or decaying it to a pointer.
replace_once(
    "src/frontend/parser_record.c",
    """    if (minic_type_is_array(field_type)) {
        minic_parser_error(parser, "record field typedef array is unsupported");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
""",
    """    if (!minic_parser_require_complete_object_type(
""",
)
replace_once(
    "src/frontend/parser_record.c",
    """    element_count = 1U;
    is_array = false;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
""",
    """    element_count = 1U;
    is_array = false;
    is_flexible_array = false;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET && minic_type_is_array(field_type)) {
        const MinicArrayType *typedef_array;

        typedef_array = minic_c0_program_array_type(parser->program, field_type.array_type_id);
        if (typedef_array == NULL || typedef_array->element_count == 0U) {
            minic_parser_error(parser, "record field requires a complete typedef array type");
            return false;
        }
        field_type = typedef_array->element_type;
        element_count = typedef_array->element_count;
        is_array = true;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
""",
)

print("staged typedef array types as first-class record fields")
