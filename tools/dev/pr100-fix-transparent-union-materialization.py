#!/usr/bin/env python3
from pathlib import Path
import re

p = Path("src/frontend/parser_typedef.c")
data = p.read_bytes()
if b"\x00" in data:
    data = data.replace(b"\x00", b"\\0")
p.write_bytes(data)

text = p.read_text()
text, count = re.subn(
    r'''static bool typedef_token_text_equals\(const MinicParser \*parser, const char \*text\) \{.*?\n}\n\n''',
    "",
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit(f"unused typedef token helper removal count={count}")

old = '''        if (!minic_type_is_record(*context->aliased_type)) {\n            minic_parser_error(parser, "GNU transparent_union requires a union type");\n            return false;\n        }\n        record = &parser->program->records[context->aliased_type->record_id];\n        if (!record->is_complete || !record->is_union || record->field_count == 0U) {\n            minic_parser_error(parser, "GNU transparent_union requires a complete non-empty union");\n            return false;\n        }\n'''
new = '''        if (!minic_type_is_record(*context->aliased_type)) {\n            minic_parser_error(parser, "GNU transparent_union requires a union type");\n            return false;\n        }\n        record = &parser->program->records[context->aliased_type->record_id];\n        if (!record->is_union) {\n            minic_parser_error(parser, "GNU transparent_union requires a union type");\n            return false;\n        }\n        if (!record->is_complete || record->field_count == 0U) {\n            minic_parser_error(parser, "GNU transparent_union requires a complete non-empty union");\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f"transparent union diagnostic anchor count={text.count(old)}")
text = text.replace(old, new, 1)
p.write_text(text)
