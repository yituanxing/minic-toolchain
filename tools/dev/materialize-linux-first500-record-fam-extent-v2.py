#!/usr/bin/env python3
"""Expose a known static FAM extent while its aggregate tail is being parsed."""
from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()
marker = "Publish the inspected FAM extent before parsing its elements"
if marker not in text:
    old = '''                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
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
                    parser, object_id, field->type, flexible_element_count, false, NULL);
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
'''
    new = '''                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            /* Publish the inspected FAM extent before parsing its elements. Aggregate relocation
             * validation resolves a scalar slot through the complete top-level object shape; the
             * flexible tail therefore has to be visible while its transaction is in progress. */
            if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
                parsed_flexible_tail = parse_static_scalar_array_transaction(
                    parser, object_id, field->type, flexible_element_count, false);
            } else {
                parsed_flexible_tail = parse_static_forward_array_initializer(
                    parser, object_id, field->type, flexible_element_count, false, NULL);
            }
            if (!parsed_flexible_tail) {
                /* Parsing the translation unit will fail, but restore the object invariant so
                 * diagnostics and cleanup never observe a committed extent without its payload. */
                parser->program->global_objects[object_id].flexible_array_initializer_count = 0U;
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"FAM in-progress extent anchor: expected 1 match, found {count}")
    path.write_text(text.replace(old, new, 1))
