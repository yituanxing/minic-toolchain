#!/usr/bin/env python3
from pathlib import Path

script_path = Path('tools/dev/stage-external-record-constant-initializer-v1.py')
source = script_path.read_text()
old = '''old = \'\'\'static bool
minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                    MinicGlobalObjectId object_id,
                                                    MinicType type);
\'\'\'
new = \'\'\'bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);
\'\'\'
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = \'\'\'static bool
minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                    MinicGlobalObjectId object_id,
                                                    MinicType type) {
\'\'\'
new = \'\'\'bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type) {
\'\'\'
assert text.count(old) == 1
text = text.replace(old, new, 1)
path.write_text(text)
'''
new = '''owner_prefix = 'static bool\\nminic_parser_parse_static_storage_initializer_value'
assert text.count(owner_prefix) == 2
text = text.replace(
    owner_prefix,
    'bool\\nminic_parser_parse_static_storage_initializer_value',
    2,
)
path.write_text(text)
'''
if source.count(old) != 1:
    raise SystemExit('cannot adapt static-storage initializer owner promotion block')
source = source.replace(old, new, 1)
exec(compile(source, str(script_path), 'exec'), {'__name__': '__main__'})
