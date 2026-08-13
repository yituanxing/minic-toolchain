from pathlib import Path

root = Path('.')

# The first materialization intentionally replaces the two specialized emitters.
# Close the remaining generic-global scalar guard that still used the old
# function-only relocation counter.
codegen = root / 'src/target/riscv64/codegen_function.c'
text = codegen.read_text()
old = '''        if (object->function_relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||'''
new = '''        if (object->relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||'''
if text.count(old) != 1:
    raise SystemExit(f'RV64 scalar relocation guard mismatch: {text.count(old)}')
codegen.write_text(text.replace(old, new, 1))

# field_index was only the location encoding for the old function-relocation
# schema. The unified model consumes the semantic field storage offset directly.
parser = root / 'src/frontend/parser_global.c'
text = parser.read_text()
old = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t field_index,
                                                  const MinicRecordField *field) {'''
new = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecordField *field) {'''
if text.count(old) != 1:
    raise SystemExit(f'static record initializer signature mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '        if (!parse_static_record_field_initializer(parser, object_id, field_index, field)) {'
new = '        if (!parse_static_record_field_initializer(parser, object_id, field)) {'
if text.count(old) != 1:
    raise SystemExit(f'static record initializer call mismatch: {text.count(old)}')
parser.write_text(text.replace(old, new, 1))

# Migration invariant: no production source may still depend on either old
# relocation container or old location unit after this refactor.
legacy = (
    'function_relocation_count',
    'object_relocation_count',
    'function_relocations',
    'object_relocations',
    'MinicGlobalFunctionRelocation',
    'MinicGlobalObjectRelocation',
)
for path in (root / 'src').rglob('*'):
    if path.suffix not in {'.c', '.h'}:
        continue
    source = path.read_text()
    for token in legacy:
        if token in source:
            raise SystemExit(f'legacy relocation token {token!r} remains in {path}')
