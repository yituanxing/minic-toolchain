#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_global.c"
text = path.read_text()
old = '''    if (minic_type_is_integer(type)) {\n        int value;\n\n        if (!minic_parser_parse_integer_value(parser, &value) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static integer initializer");\n            }\n            return false;\n        }\n'''
new = '''    if (minic_type_is_integer(type)) {\n        int64_t constant_value;\n        int value;\n\n        if (!minic_parser_parse_integer_constant_expression(parser, &constant_value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser,\n                                   "static integer initializer requires an integer constant expression");\n            }\n            return false;\n        }\n        if (constant_value < INT_MIN || constant_value > INT_MAX) {\n            minic_parser_error(parser, "static integer initializer is out of supported range");\n            return false;\n        }\n        value = (int)constant_value;\n        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n            minic_parser_error(parser, "cannot record static integer initializer");\n            return false;\n        }\n'''
text = replace_once(text, old, new, "static-global-shared-ice")
path.write_text(text)

path = root / "tests/compiler/c0/static_prefix_object_attributes.c"
path.write_text('''typedef _Bool bool;\n\nenum {\n    false = 0,\n    true = 1\n};\n\nstatic __attribute__((__unused__)) const bool class_irq_is_conditional = false;\n\nstatic inline __attribute__((__always_inline__)) int class_irq_add(int value) {\n    return value + 1;\n}\n\nint main(void) {\n    return class_irq_is_conditional == false && class_irq_add(6) == 7 ? 0 : 1;\n}\n''')

path = root / "tests/compiler/c0/run-static-prefix-object-attributes.sh"
text = path.read_text()
text = replace_once(
    text,
    "prefix=unused object=static-const function=static-inline single-pass-head=1 semantic-probe=none",
    "prefix=unused object=static-const-bool initializer=shared-ice-enum-false function=static-inline single-pass-head=1 semantic-probe=none",
    "static-global-ice-summary",
)
path.write_text(text)
