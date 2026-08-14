#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()
old = r'''    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic) ||
            existing_function->is_internal != is_internal) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
    }
'''
new = r'''    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic) ||
            (!existing_function->is_internal && is_internal)) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
        if (existing_function->is_internal) {
            is_internal = true;
        }
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"function linkage inheritance: expected staged redeclaration block once, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged C function linkage inheritance: prior internal linkage survives later non-static declarations")
