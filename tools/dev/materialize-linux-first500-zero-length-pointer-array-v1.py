#!/usr/bin/env python3
"""Materialize GNU zero-length static pointer-array declarations and empty initializers."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


path = Path("src/frontend/parser_global.c")
replace_once(
    path,
    """    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, \"cannot build static pointer array type\");
        }
        return false;
    }
""",
    """    } else {
        size_t parsed_element_count;
        bool is_zero_length;

        parsed_element_count = 0U;
        is_zero_length = false;
        if (!minic_parser_parse_record_array_bound(
                parser, &parsed_element_count, &is_zero_length)) {
            return false;
        }
        element_count = is_zero_length ? 0U : parsed_element_count;
        if ((is_zero_length &&
             !minic_c0_program_add_zero_length_array_type(
                 parser->program, element_type, &object_type)) ||
            (!is_zero_length &&
             !minic_c0_program_add_array_type(
                 parser->program, element_type, element_count, &object_type))) {
            minic_parser_error(parser, \"cannot build static pointer array type\");
            return false;
        }
    }
""",
)
replace_once(
    path,
    """    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, \"expected '=' after static pointer array\") ||
        !parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, inferred_bound)) {
        return false;
    }
""",
    """    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, \"expected '=' after static pointer array\")) {
        return false;
    }
    {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (array_type == NULL) {
            minic_parser_error(parser, \"invalid static pointer array type\");
            return false;
        }
        if (array_type->is_zero_length) {
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_LBRACE, \"expected '{' for zero-length array initializer\") ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RBRACE, \"zero-length array initializer must be empty\")) {
                return false;
            }
        } else if (!parse_static_scalar_array_transaction(
                       parser, object_id, element_type, element_count, inferred_bound)) {
            return false;
        }
    }
""",
)
