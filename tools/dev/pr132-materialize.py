from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# Shared typed null-pointer expression parser seam.
replace_once(
    'src/frontend/parser_internal.h',
    'bool minic_parser_parse_zero_pointer_constant(MinicParser *parser);\n',
    'bool minic_parser_parse_zero_pointer_constant(MinicParser *parser);\n'
    'bool minic_parser_parse_null_pointer_constant_expression(MinicParser *parser,\n'
    '                                                         MinicType target_type);\n',
    'parser internal typed null declaration')

path = root / 'src/frontend/parser_expression.c'
text = path.read_text()
anchor = '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    return parse_expression_internal(parser, expression_id, minimum_precedence, true);
}
'''
addition = anchor + '''
bool minic_parser_parse_null_pointer_constant_expression(MinicParser *parser,
                                                         MinicType target_type) {
    MinicExpressionId expression_id;

    if (parser == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    return minic_c0_assignment_compatible(parser->program, target_type, expression_id) &&
           minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id);
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'parser expression tail anchor mismatch: {text.count(anchor)}')
path.write_text(text.replace(anchor, addition, 1))

# External-linkage pointer arrays: replace integer-only zero parsing with typed null semantics.
replace_once(
    'src/frontend/parser_function.c',
    '''            } else {
                int64_t parsed;

                if (!minic_parser_parse_integer_constant_expression(parser, &parsed) ||
                    parsed != 0) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                        minic_parser_error(
                            parser, "external pointer array scalar initializer must be null");
                    }
                    return false;
                }
            }
''',
    '''            } else if (!minic_parser_parse_null_pointer_constant_expression(
                           parser, element_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser,
                                       "external pointer array scalar initializer must be null");
                }
                return false;
            }
''',
    'external pointer array typed null fallback')

# Static-local inferred pointer arrays share the same typed null semantics.
replace_once(
    'src/frontend/parser_statement.c',
    '''                } else {
                    int64_t parsed;

                    if (!minic_parser_parse_integer_constant_expression(parser, &parsed) ||
                        parsed != 0) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                            minic_parser_error(
                                parser,
                                "static local pointer array scalar initializer must be null");
                        }
                        return false;
                    }
                }
''',
    '''                } else if (!minic_parser_parse_null_pointer_constant_expression(
                               parser, element_type)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                        minic_parser_error(
                            parser,
                            "static local pointer array scalar initializer must be null");
                    }
                    return false;
                }
''',
    'static-local pointer array typed null fallback')

# File-scope static pointer arrays used the older token-shaped null parser.
replace_once(
    'src/frontend/parser_global.c',
    '''        } else if (!minic_parser_parse_zero_pointer_constant(parser)) {
            goto done;
        }
''',
    '''        } else if (!minic_parser_parse_null_pointer_constant_expression(parser, element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "static pointer array scalar initializer must be null");
            }
            goto done;
        }
''',
    'static pointer array typed null fallback')

# Permanent focused fixtures.
(root / 'tests/compiler/c0/pointer_array_typed_null.c').write_text(r'''const char *envp_init[4] = {"HOME=/", "TERM=linux", ((void *)0), 0};
static const char *argv_init[3] = {"init", ((void *)0), 0};

static const char *local_name(int index)
{
    static const char *const names[] = {"first", ((void *)0), 0};
    return names[index];
}

int main(void)
{
    return envp_init[0][0] == 'H' && argv_init[0][0] == 'i' && local_name(0)[0] == 'f' ? 0 : 1;
}
''')
(root / 'tests/compiler/c0/invalid_pointer_array_nonnull_cast.c').write_text(r'''const char *bad[1] = { (void *)1 };

int main(void)
{
    return bad[0] != 0;
}
''')
(root / 'tests/compiler/c0/run-pointer-array-typed-null.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-pointer-array-typed-null"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/pointer_array_typed_null.c" -o "$work/positive.s"
grep -F 'envp_init:' "$work/positive.s" >/dev/null
grep -F '__minic_static_local_' "$work/positive.s" >/dev/null
relocations=$(grep -c '^  \.dword ' "$work/positive.s")
if [ "$relocations" -lt 3 ]; then
    printf '%s\n' "FAIL compiler/c0/pointer-array-typed-null: missing string relocations" >&2
    exit 1
fi

if "$minic" -S "$root/tests/compiler/c0/invalid_pointer_array_nonnull_cast.c" \
    -o "$work/invalid.s" >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid-pointer-array-nonnull-cast: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'external pointer array scalar initializer must be null' "$work/invalid.stderr" >/dev/null

printf '%s\n' \
    "PASS compiler/c0/pointer-array-typed-null linkage=static+external+static-local null='0,(void*)0' semantic=typed"
''')

# Make it part of the formal C0 gate next to the existing pointer-array owner.
gate = root / '.github/scripts/compiler-c0-full-gate.sh'
text = gate.read_text()
old = '''        run-unnamed-prototype-parameters.sh \\
        run-static-pointer-arrays.sh \\
        run-static-zero-definitions.sh \\
'''
new = '''        run-unnamed-prototype-parameters.sh \\
        run-static-pointer-arrays.sh \\
        run-pointer-array-typed-null.sh \\
        run-static-zero-definitions.sh \\
'''
if text.count(old) != 1:
    raise SystemExit(f'full gate pointer-array insertion mismatch: {text.count(old)}')
gate.write_text(text.replace(old, new, 1))
