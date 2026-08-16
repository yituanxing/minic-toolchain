#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
old = '''        const MinicRecordField *field;\n        size_t element_index;\n        bool overwrite_materialized_field;\n'''
new = '''        const MinicRecordField *field;\n        bool overwrite_materialized_field;\n'''
if text.count(old) != 1:
    raise SystemExit(f'obsolete field-array cursor: expected one anchor, found {text.count(old)}')
path.write_text(text.replace(old, new, 1))
