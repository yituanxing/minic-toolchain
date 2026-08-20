#!/usr/bin/env python3
"""Materialize aggregate-valued static flexible-array member initializers."""
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


parser_path = Path("src/frontend/parser_global.c")
text = parser_path.read_text()
marker = "GNU static flexible array initializer supports scalar and aggregate element types"
if marker not in text:
    old = '''        if (field->is_flexible_array) {
            const MinicGlobalObject *object;
            size_t flexible_element_count;

            object = minic_c0_program_global_object(parser->program, object_id);
            flexible_element_count = 0U;
            if (object == NULL || !minic_type_is_record(object->type) ||
                minic_c0_program_record(parser->program, object->type.record_id) != record ||
                record->is_union || field_index + 1U != record->field_count ||
                field_index != materialized_field_limit || field->is_bit_field ||
                !minic_type_is_integer(field->type) ||
                (has_designator && (designator.depth != 1U || designator.has_array_index)) ||
                parser->current.kind != MINIC_TOKEN_LBRACE ||
                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U ||
                !parse_static_scalar_array_transaction(
                    parser, object_id, field->type, flexible_element_count, false) ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a direct integer tail");
                }
                return false;
            }
            materialized_field_limit += 1U;
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' after static flexible array initializer");
                return false;
            }
            continue;
        }
'''
    new = '''        if (field->is_flexible_array) {
            const MinicGlobalObject *object;
            size_t flexible_element_count;
            bool parsed_flexible_tail;

            object = minic_c0_program_global_object(parser->program, object_id);
            flexible_element_count = 0U;
            if (object == NULL || !minic_type_is_record(object->type) ||
                minic_c0_program_record(parser->program, object->type.record_id) != record ||
                record->is_union || field_index + 1U != record->field_count ||
                field_index != materialized_field_limit || field->is_bit_field ||
                (has_designator && (designator.depth != 1U || designator.has_array_index)) ||
                parser->current.kind != MINIC_TOKEN_LBRACE ||
                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            /* GNU static flexible array initializer supports scalar and aggregate element types.
             * The regular fixed-array transactions already own last-writer semantics, relocation
             * capture, active-union selection, and recursive aggregate materialization. */
            if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
                parsed_flexible_tail = parse_static_scalar_array_transaction(
                    parser, object_id, field->type, flexible_element_count, false);
            } else {
                parsed_flexible_tail = parse_static_forward_array_initializer(
                    parser,
                    object_id,
                    field->type,
                    flexible_element_count,
                    false,
                    NULL);
            }
            if (!parsed_flexible_tail ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            materialized_field_limit += 1U;
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' after static flexible array initializer");
                return false;
            }
            continue;
        }
'''
    text = replace_once(text, old, new, "static flexible-array parser")
    parser_path.write_text(text)


test_path = Path("tests/compiler/c0/static_record_flexible_array_initializer.c")
if not test_path.exists():
    test_path.write_text('''struct namespace_stub {\n    int marker;\n};\n\nstruct id_slot {\n    int nr;\n    struct namespace_stub *ns;\n};\n\nstruct id_table {\n    int level;\n    struct id_slot numbers[];\n};\n\nstruct namespace_stub init_namespace = { .marker = 7 };\n\nstruct id_table init_table = {\n    .level = 0,\n    .numbers = { {\n        .nr = 0,\n        .ns = &init_namespace,\n    }, },\n};\n\nint static_record_fam_probe(void) {\n    return init_table.numbers[0].ns == &init_namespace;\n}\n''')

run_path = Path("tests/compiler/c0/run-flexible-array-members.sh")
run = run_path.read_text()
marker = "PASS compiler/c0/static_record_flexible_array_initializer aggregate-tail+relocation"
if marker not in run:
    anchor = '''printf '%s\\n' 'PASS compiler/c0/static_nested_flexible_array_initializer linux-wrapper zero-slot-fam + relocation'\n\n'''
    addition = anchor + '''"$host_cc" -E -P -x c \\
    "$root/tests/compiler/c0/static_record_flexible_array_initializer.c" \\
    -o "$work/static_record_flexible_array_initializer.i"\n"$minic" -S \\
    "$work/static_record_flexible_array_initializer.i" \\
    -o "$work/static_record_flexible_array_initializer.s"\ngrep -F '.dword init_namespace' \\
    "$work/static_record_flexible_array_initializer.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/static_record_flexible_array_initializer aggregate-tail+relocation'\n\n'''
    if anchor not in run:
        raise SystemExit("flexible-array regression anchor not found")
    run_path.write_text(run.replace(anchor, addition, 1))
