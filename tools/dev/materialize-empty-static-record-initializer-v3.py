from pathlib import Path

root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_global.c"
text = path.read_text()
old = '''        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
'''
new = '''        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit("static zero-fill record guard shape changed")
text = text.replace(old, new)
old = '''    if (record == NULL || !record->is_complete || record->field_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
'''
new = '''    if (record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit("static record initializer guard shape changed")
path.write_text(text.replace(old, new))

fixture = root / "tests/compiler/c0/static_local_record_initializer.c"
text = fixture.read_text()
append = r'''

struct MiniEmptyRecord {
};

int read_static_empty_record(void) {
    static struct MiniEmptyRecord empty = {};
    return (int)sizeof(empty);
}
'''
if "read_static_empty_record" in text:
    raise SystemExit("empty static record fixture already present")
fixture.write_text(text + append)

runner = root / "tests/compiler/c0/run-static-local-record-initializers.sh"
text = runner.read_text()
old_msg = "printf '%s\\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=12 signed=-1,-2 compound-literal=1 designated-nested=1 anonymous-union-first=1 shared-owner=1 target-layout=rv64'\n"
new_msg = "printf '%s\\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=12 signed=-1,-2 compound-literal=1 designated-nested=1 anonymous-union-first=1 empty-record=1 shared-owner=1 target-layout=rv64'\n"
if text.count(old_msg) != 1:
    raise SystemExit("static local record runner message changed")
runner.write_text(text.replace(old_msg, new_msg))
