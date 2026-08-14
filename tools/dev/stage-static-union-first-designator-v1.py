#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
old = '''            if (record->is_union) {
                minic_parser_error(parser, "nested static union designators are not supported yet");
                return false;
            }
'''
assert text.count(old) == 1
text = text.replace(old, '', 1)
old = '''            designator_index = field_path.field_indices[0];
            if (designator_index < field_index) {
'''
new = '''            designator_index = field_path.field_indices[0];
            if (record->is_union && designator_index != 0U) {
                minic_parser_error(
                    parser,
                    "nested static union designator requires the representable first member");
                return false;
            }
            if (designator_index < field_index) {
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# Freeze the representation boundary: a non-first union member remains unsupported.
runner_path = Path('tests/compiler/c0/run-external-record-compound-literal.sh')
runner = runner_path.read_text()
anchor = '''grep -F 'main:' "$work/output.s" >/dev/null

printf '%s\\n' 'PASS compiler/c0/external-record-compound-literal linkage=external storage=static-duration initializer=shared-constant-owner compound-literal=record designated+nested-union=1'
'''
replacement = '''grep -F 'main:' "$work/output.s" >/dev/null

cat >"$work/nonfirst-union.c" <<'EOF'
union payload { unsigned long first; void *second; };
struct wrapper { union payload value; };
struct wrapper rejected = (struct wrapper){ .value = { .second = (void *)0 } };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonfirst-union.c" -o "$work/nonfirst-union.i"
if "$minic" -S "$work/nonfirst-union.i" -o "$work/nonfirst-union.s" \\
    >"$work/nonfirst-union.out" 2>"$work/nonfirst-union.err"; then
    printf '%s\\n' 'FAIL compiler/c0/external-record-compound-literal: non-first union designator exceeded v0 representation' >&2
    exit 1
fi
grep -F 'nested static union designator requires the representable first member' \\
    "$work/nonfirst-union.err" >/dev/null

printf '%s\\n' 'PASS compiler/c0/external-record-compound-literal linkage=external storage=static-duration initializer=shared-constant-owner compound-literal=record anonymous-union-first-designator=1 nonfirst=fail-closed'
'''
assert runner.count(anchor) == 1
runner_path.write_text(runner.replace(anchor, replacement, 1))
