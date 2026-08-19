#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))

replace_once(
    "src/frontend/parser_typedef.c",
    "    bool is_function_declarator;\n\n"
    "    is_function_declarator = false;",
    "    bool is_function_declarator;\n\n"
    "    leading_attributes.count = 0U;\n"
    "    post_type_attributes.count = 0U;\n"
    "    is_function_declarator = false;",
)

replace_once(
    "src/frontend/parser_function.c",
    "        bool declarator_has_name;\n"
    "        bool is_function_pointer_parameter;\n\n"
    "        if (*parameter_count >= MINIC_MAX_FUNCTION_PARAMETERS) {",
    "        bool declarator_has_name;\n"
    "        bool is_function_pointer_parameter;\n\n"
    "        leading_attributes.count = 0U;\n"
    "        post_type_attributes.count = 0U;\n"
    "        if (*parameter_count >= MINIC_MAX_FUNCTION_PARAMETERS) {",
)

print("initialized declaration-head attribute collectors")
