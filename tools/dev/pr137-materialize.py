from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# Extend only the nested static-record constant serializer with direct member
# designators. It keeps the existing positional cursor and zero-fill model.
path = root / 'src/frontend/parser_global.c'
text = path.read_text()
old = '''    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t element_index;

        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
'''
new = '''    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t element_index;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicRecordFieldPath field_path;
            MinicSourceSpan designator_span;
            size_t designator_index;

            if (record->is_union) {
                minic_parser_error(parser,
                                   "nested static union designators are not supported yet");
                return false;
            }
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "expected member name after '.' in initializer");
                }
                return false;
            }
            designator_span = parser->current.span;
            if (!minic_parser_find_record_field_path(parser, record, designator_span, &field_path) ||
                !field_path.found || field_path.ambiguous || field_path.depth != 1U) {
                minic_parser_error(parser,
                                   "static record designator requires a direct unambiguous member");
                return false;
            }
            designator_index = field_path.field_indices[0];
            if (designator_index < field_index) {
                minic_parser_error(parser,
                                   "static record designator cannot move backward in v0");
                return false;
            }
            while (field_index < designator_index) {
                if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                    minic_parser_error(parser,
                                       "cannot zero-fill skipped static record designator fields");
                    return false;
                }
                field_index += 1U;
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_EQUAL,
                                     "expected '=' after static record designator")) {
                return false;
            }
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
'''
if text.count(old) != 1:
    raise SystemExit(f'nested record loop anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))

# Permanent positive and boundary fixtures.
(root / 'tests/programs/c0/static_nested_record_designator.c').write_text(r'''struct Inner {
    int a;
    int b;
};

struct Outer {
    int prefix;
    struct Inner inner;
    int suffix;
};

static struct Outer state = {3, {.b = 7}, 9};

int main(void)
{
    return state.prefix == 3 && state.inner.a == 0 && state.inner.b == 7 && state.suffix == 9 ? 0
                                                                                              : 1;
}
''')
(root / 'tests/compiler/c0/invalid_static_nested_record_designator_backward.c').write_text(r'''struct Pair {
    int a;
    int b;
};

static struct Pair pair = {.b = 1, .a = 2};

int main(void)
{
    return pair.a + pair.b;
}
''')
(root / 'tests/compiler/c0/run-static-nested-record-designator.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-nested-record-designator"
mkdir -p "$work"

"$minic" -S \
    "$root/tests/programs/c0/static_nested_record_designator.c" \
    -o "$work/positive.s"
for value in 3 7 9; do
    grep -F "  .word $value" "$work/positive.s" >/dev/null
done
grep -F '  .word 0' "$work/positive.s" >/dev/null

if "$minic" -S \
    "$root/tests/compiler/c0/invalid_static_nested_record_designator_backward.c" \
    -o "$work/backward.s" >"$work/backward.stdout" 2>"$work/backward.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-nested-record-designator-backward: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'static record designator cannot move backward in v0' "$work/backward.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static-nested-record-designator direct-member=1 skipped-fields=zero continuation=next-field backward=fail-closed'
''')

manifest = root / 'tests/programs/c0/manifest.txt'
text = manifest.read_text()
if '\nstatic_nested_record_designator\n' not in '\n' + text:
    if not text.endswith('\n'):
        text += '\n'
    text += 'static_nested_record_designator\n'
manifest.write_text(text)

# Keep this static-initializer semantic in its own focused formal gate.
gate = root / '.github/scripts/compiler-c0-full-gate.sh'
text = gate.read_text()
anchor = '''predefined_func_name_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-predefined-func-name" \\
        sh tests/compiler/c0/run-predefined-func-name.sh
}
'''
addition = anchor + '''
static_nested_record_designator_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-nested-record-designator" \\
        sh tests/compiler/c0/run-static-nested-record-designator.sh
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'formal gate function anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition, 1)
old = 'start_gate predefined-func-name-focused predefined_func_name_focused\n'
new = old + 'start_gate static-nested-record-designator-focused static_nested_record_designator_focused\n'
if text.count(old) != 1:
    raise SystemExit(f'formal gate start anchor mismatch: {text.count(old)}')
gate.write_text(text.replace(old, new, 1))
