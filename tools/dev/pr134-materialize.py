from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# One typed initializer-to-current-payload seam. It deliberately keeps the
# existing int GlobalObject payload boundary explicit instead of truncating.
anchor = '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);
bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value);
'''
addition = anchor + '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value);
'''
replace_once('src/frontend/parser_internal.h', anchor, addition, 'typed initializer declaration')

path = root / 'src/frontend/parser_core.c'
text = path.read_text()
anchor = '''static bool minic_parser_parse_typed_integer_constant_expression(MinicParser *parser,
                                                                 int64_t *value) {
    MinicConstValue constant;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {
        minic_parser_error(parser, "expected integer constant expression");
        return false;
    }
    if (!minic_const_value_as_int64(parser->program, parser->target_info, &constant, value)) {
        minic_parser_error(parser, "integer constant expression exceeds supported 64-bit range");
        return false;
    }
    return true;
}
'''
addition = anchor + '''
bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value) {
    MinicConstValue constant;
    MinicExpressionId expression_id;
    int64_t signed_value;

    if (parser == NULL || value == NULL || !minic_type_is_integer(target_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "integer initializer requires an integer target type");
        }
        return false;
    }
    if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "integer initializer type mismatch");
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {
        minic_parser_error(parser, "integer initializer requires an integer constant expression");
        return false;
    }
    if (!minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &signed_value) ||
        signed_value < INT_MIN || signed_value > INT_MAX) {
        minic_parser_error(parser, "integer initializer exceeds current global payload range");
        return false;
    }
    *value = (int)signed_value;
    return true;
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'typed integer parser anchor mismatch: {text.count(anchor)}')
path.write_text(text.replace(anchor, addition, 1))

# External-linkage scalar definitions stop using literal-only parsing.
replace_once(
    'src/frontend/parser_function.c',
    '''    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }
''',
    '''    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, object_type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }
''',
    'external integer typed initializer')

# File-scope static scalar definitions converge on the same typed seam.
replace_once(
    'src/frontend/parser_global.c',
    '''    if (minic_type_is_integer(type)) {
        int64_t constant_value;
        int value;

        if (!minic_parser_parse_integer_constant_expression(parser, &constant_value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(
                    parser, "static integer initializer requires an integer constant expression");
            }
            return false;
        }
        if (constant_value < INT_MIN || constant_value > INT_MAX) {
            minic_parser_error(parser, "static integer initializer is out of supported range");
            return false;
        }
        value = (int)constant_value;
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "cannot record static integer initializer");
            return false;
        }
''',
    '''    if (minic_type_is_integer(type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record static integer initializer");
            }
            return false;
        }
''',
    'static scalar typed initializer')

# Extend the existing external-scalar owner gate rather than creating a parallel suite.
fixture = root / 'tests/compiler/c0/external_scalar_definition.c'
text = fixture.read_text()
old = '''long long external_wide = 11LL;

int main(void) {
    return external_count == 7 && external_wide == 11LL ? 0 : 1;
}
'''
new = '''long long external_wide = 11LL;
unsigned long loops_per_jiffy = (1 << 12);
static int internal_folded = (3 + 5) * 2;

int main(void) {
    return external_count == 7 && external_wide == 11LL && loops_per_jiffy == 4096UL &&
                   internal_folded == 16
               ? 0
               : 1;
}
'''
if text.count(old) != 1:
    raise SystemExit(f'external scalar fixture anchor mismatch: {text.count(old)}')
fixture.write_text(text.replace(old, new, 1))

(root / 'tests/compiler/c0/invalid_external_integer_nonconstant.c').write_text(r'''int runtime_source;
int external_nonconstant = runtime_source + 1;

int main(void)
{
    return external_nonconstant;
}
''')
(root / 'tests/compiler/c0/invalid_external_integer_payload_range.c').write_text(r'''unsigned long external_too_wide = (1UL << 40);

int main(void)
{
    return external_too_wide != 0;
}
''')

script = root / 'tests/compiler/c0/run-external-scalar-definitions.sh'
text = script.read_text()
old = '''grep -F '.globl external_wide' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 11' "$work/external_scalar_definition.s" >/dev/null
test "$(grep -c '^external_count:$' "$work/external_scalar_definition.s")" -eq 1
printf '%s\\n' 'PASS compiler/c0/external_scalar_definition extern-merge=1 int=.word long-long=.dword linkage=external'
'''
new = '''grep -F '.globl external_wide' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 11' "$work/external_scalar_definition.s" >/dev/null
grep -F '.globl loops_per_jiffy' "$work/external_scalar_definition.s" >/dev/null
grep -F 'loops_per_jiffy:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 4096' "$work/external_scalar_definition.s" >/dev/null
grep -F 'internal_folded:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .word 16' "$work/external_scalar_definition.s" >/dev/null
if grep -F '.globl internal_folded' "$work/external_scalar_definition.s" >/dev/null; then
    printf '%s\\n' 'FAIL compiler/c0/external_scalar_definition: internal scalar exported' >&2
    exit 1
fi
test "$(grep -c '^external_count:$' "$work/external_scalar_definition.s")" -eq 1

expect_failure() {
    name=$1
    message=$2

    if "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s" \\
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\\n' "PASS compiler/c0/$name"
}

expect_failure invalid_external_integer_nonconstant \\
    'integer initializer requires an integer constant expression'
expect_failure invalid_external_integer_payload_range \\
    'integer initializer exceeds current global payload range'

printf '%s\\n' \\
    'PASS compiler/c0/external_scalar_definition extern-merge=1 typed-consteval=1 int=.word long=.dword static=shared payload=int-bounded'
'''
if text.count(old) != 1:
    raise SystemExit(f'external scalar gate anchor mismatch: {text.count(old)}')
script.write_text(text.replace(old, new, 1))
