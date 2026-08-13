from pathlib import Path

root = Path('.')
path = root / 'src/frontend/parser_global.c'
text = path.read_text()
old = '''static bool\nparse_static_constant_value(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {\n    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {\n        return parse_static_scalar_constant(parser, object_id, type);\n    }\n    if (minic_type_is_array(type)) {\n        return parse_static_array_constant(\n            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));\n    }\n    if (minic_type_is_record(type)) {\n        return parse_static_record_constant(\n            parser, object_id, minic_c0_program_record(parser->program, type.record_id));\n    }\n    minic_parser_error(parser, "unsupported nested static aggregate initializer type");\n    return false;\n}\n'''
new = '''static bool\nparse_static_constant_value(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {\n    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {\n        return parse_static_scalar_constant(parser, object_id, type);\n    }\n    if (minic_type_is_array(type)) {\n        return parse_static_array_constant(\n            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));\n    }\n    if (minic_type_is_record(type)) {\n        if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n            MinicType explicit_type;\n\n            if (!minic_parser_advance(parser) ||\n                !minic_parser_parse_type_name(parser, &explicit_type) ||\n                !minic_parser_expect(\n                    parser, MINIC_TOKEN_RPAREN, "expected ')' after static compound literal type")) {\n                return false;\n            }\n            if (!minic_type_equal(type, explicit_type)) {\n                minic_parser_error(parser, "static record compound literal type mismatch");\n                return false;\n            }\n            if (parser->current.kind != MINIC_TOKEN_LBRACE) {\n                minic_parser_error(parser, "static record compound literal requires initializer list");\n                return false;\n            }\n        }\n        return parse_static_record_constant(\n            parser, object_id, minic_c0_program_record(parser->program, type.record_id));\n    }\n    minic_parser_error(parser, "unsupported nested static aggregate initializer type");\n    return false;\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f'parse_static_constant_value anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))

(root / 'tests/compiler/c0/static_record_compound_literal.c').write_text(r'''typedef struct Inner {
    int first;
    int second;
} Inner;

typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

static Outer value = { 3, (Inner) { .second = 7 } };

int main(void)
{
    return value.tag == 3 && value.inner.first == 0 && value.inner.second == 7 ? 0 : 1;
}
''')

(root / 'tests/compiler/c0/invalid_static_record_compound_literal_type.c').write_text(r'''typedef struct Left { int value; } Left;
typedef struct Right { int value; } Right;
typedef struct Holder { Left left; } Holder;

static Holder holder = { (Right) { 1 } };

int main(void) { return 0; }
''')

(root / 'tests/compiler/c0/run-static-aggregate-family-discovery.sh').write_text(r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/static-aggregate-family-discovery"}
mkdir -p "$build_dir"

"$minic" -S "$root/tests/compiler/c0/static_record_compound_literal.c" \
    -o "$build_dir/static_record_compound_literal.s"
if "$minic" -S "$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c" \
    -o "$build_dir/invalid.s" >"$build_dir/invalid.stdout" 2>"$build_dir/invalid.stderr"; then
    echo 'FAIL static aggregate discovery: mismatched record compound literal accepted' >&2
    exit 1
fi
grep -F 'static record compound literal type mismatch' "$build_dir/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/static-aggregate-family compound-literal=record designated-inner=shared mismatch=fail-closed'
''')
