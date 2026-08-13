from pathlib import Path

root = Path('.')

# Static-local pointer arrays materialize backing GlobalObjects and therefore
# share the same global relocation model as file-scope arrays.
path = root / 'src/frontend/parser_statement.c'
text = path.read_text()
old = '''                if (has_relocation &&
                    !minic_c0_global_object_add_object_relocation(
                        parser->program, object_id, initializer_count, target_id)) {
                    minic_parser_error(parser,
                                       "cannot record static local pointer array relocation");
                    return false;
                }'''
new = '''                if (has_relocation &&
                    !minic_c0_global_object_add_object_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                        initializer_count,
                        target_id)) {
                    minic_parser_error(parser,
                                       "cannot record static local pointer array relocation");
                    return false;
                }'''
if text.count(old) != 1:
    raise SystemExit(f'static-local relocation producer mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))

# Migration invariant: every declaration, definition, and call site of the two
# relocation APIs must use the explicit semantic-location signature (5 args).
def top_level_comma_count(source, open_index):
    depth = 0
    commas = 0
    i = open_index
    in_string = False
    in_char = False
    escaped = False
    while i < len(source):
        ch = source[i]
        if in_string or in_char:
            if escaped:
                escaped = False
            elif ch == '\\':
                escaped = True
            elif in_string and ch == '"':
                in_string = False
            elif in_char and ch == "'":
                in_char = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            i += 1
            continue
        if ch == "'":
            in_char = True
            i += 1
            continue
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return commas
        elif ch == ',' and depth == 1:
            commas += 1
        i += 1
    raise SystemExit('unterminated relocation API parentheses during staging audit')

names = (
    'minic_c0_global_object_add_object_relocation',
    'minic_c0_global_object_add_function_relocation',
)
for source_path in (root / 'src').rglob('*'):
    if source_path.suffix not in {'.c', '.h'}:
        continue
    source = source_path.read_text()
    for name in names:
        start = 0
        while True:
            pos = source.find(name, start)
            if pos < 0:
                break
            open_index = source.find('(', pos + len(name))
            if open_index < 0:
                raise SystemExit(f'missing parenthesis after {name} in {source_path}')
            commas = top_level_comma_count(source, open_index)
            if commas != 4:
                raise SystemExit(
                    f'legacy relocation API arity remains for {name} in {source_path}: commas={commas}'
                )
            start = open_index + 1
