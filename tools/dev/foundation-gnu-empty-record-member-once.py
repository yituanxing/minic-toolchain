#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_record.c"
text = path.read_text()
old = '''    record = minic_c0_program_record(parser->program, record_id);\n    if (record == NULL) {\n        minic_parser_error(parser, "invalid record while adding field");\n        return false;\n    }\n    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {\n'''
new = '''    record = minic_c0_program_record(parser->program, record_id);\n    if (record == NULL) {\n        minic_parser_error(parser, "invalid record while adding field");\n        return false;\n    }\n    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {\n        return minic_parser_advance(parser);\n    }\n    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {\n'''
text = replace_once(text, old, new, "gnu-empty-record-member")
path.write_text(text)

path = root / "tests/compiler/c0/gnu_empty_records.c"
path.write_text('''struct EmptyStruct {\n    ;\n};\n\nunion EmptyUnion {\n    ;\n};\n\nstruct EmptyMemberRecord {\n    void *lock;\n    ;\n};\n\nunsigned long empty_struct_size(void) {\n    return sizeof(struct EmptyStruct);\n}\n\nunsigned long empty_union_size(void) {\n    return sizeof(union EmptyUnion);\n}\n\nunsigned long empty_member_record_size(void) {\n    return sizeof(struct EmptyMemberRecord);\n}\n\nstruct EmptyStruct *empty_identity(struct EmptyStruct *value) {\n    return value;\n}\n''')

path = root / "tests/compiler/c0/run-gnu-empty-records.sh"
text = path.read_text()
text = replace_once(
    text,
    '''for symbol in empty_struct_size empty_union_size empty_identity; do\n''',
    '''for symbol in empty_struct_size empty_union_size empty_member_record_size empty_identity; do\n''',
    "gnu-empty-record-symbols",
)
text = replace_once(
    text,
    '''test "$size0" -ge 2\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 complete=1 layout-sentinel=alignment'\n''',
    '''test "$size0" -ge 2\ngrep -F '  li a0, 8' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 layout-sentinel=alignment'\n''',
    "gnu-empty-record-summary",
)
path.write_text(text)
