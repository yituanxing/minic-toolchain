#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/parser_global.c')
data = path.read_bytes()
if data.count(b"'\x00'") != 1:
    raise SystemExit(f'publisher NUL repair: expected one source NUL, found {data.count(bytes([0]))}')
data = data.replace(b"'\x00'", b"'\\0'", 1)
text = data.decode()

old = '''        const MinicRecordField *field;\n        size_t element_index;\n        bool overwrite_materialized_field;\n'''
new = '''        const MinicRecordField *field;\n        bool overwrite_materialized_field;\n'''
if text.count(old) != 1:
    raise SystemExit(f'obsolete field-array cursor: expected one anchor, found {text.count(old)}')
text = text.replace(old, new, 1)

old = '''            if (!minic_type_equal(type, explicit_type)) {\n                minic_parser_error(parser, "static record compound literal type mismatch");\n                return false;\n            }\n'''
new = '''            if (!minic_type_is_record(explicit_type) ||\n                !minic_type_assignment_compatible(type, explicit_type)) {\n                minic_parser_error(parser, "static record compound literal type mismatch");\n                return false;\n            }\n'''
if text.count(old) != 1:
    raise SystemExit(f'compound literal compatibility: expected one anchor, found {text.count(old)}')
text = text.replace(old, new, 1)

path.write_text(text)
