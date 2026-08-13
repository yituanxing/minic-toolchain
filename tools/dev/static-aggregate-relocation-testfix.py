from pathlib import Path

p = Path('tests/programs/c0/static_record_compound_literal.c')
text = p.read_text()
old = '''static const char backing_name[] = "backing";
static const char *const relocation_names[] = { backing_name, "literal" };
'''
new = '''static const char *const relocation_names[] = { "backing", "literal" };
'''
if text.count(old) != 1:
    raise SystemExit(f'string relocation fixture declaration mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''                   value.inner.link.prev == &value.inner.link &&
                   relocation_names[0] == backing_name && relocation_names[0][0] == 'b' &&
                   relocation_names[1][0] == 'l'
'''
new = '''                   value.inner.link.prev == &value.inner.link &&
                   relocation_names[0][0] == 'b' && relocation_names[1][0] == 'l'
'''
if text.count(old) != 1:
    raise SystemExit(f'string relocation fixture return mismatch: {text.count(old)}')
p.write_text(text.replace(old, new, 1))
