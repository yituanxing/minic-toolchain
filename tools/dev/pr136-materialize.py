from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# Parser state owns only the current function's lazy semantic cache. Expressions
# retain the resulting GlobalObjectId, so this mapping need not enter the AST.
replace_once(
    'src/frontend/parser_internal.h',
    '''    MinicBlockId current_block;
    MinicFunctionId current_function;
    size_t local_begin;
''',
    '''    MinicBlockId current_block;
    MinicFunctionId current_function;
    MinicGlobalObjectId current_function_name_object;
    size_t local_begin;
''',
    'parser function-name cache field')

replace_once(
    'src/frontend/parser_internal.h',
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
''',
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
bool minic_parser_get_predefined_function_name_object(MinicParser *parser,
                                                       MinicGlobalObjectId *object_id);
''',
    'predefined function-name helper declaration')

# Build one true const-char array object per parsed function. Use the lexical C
# function name, never assembler_name, and keep the object internal/read-only.
string_c = root / 'src/frontend/parser_string.c'
text = string_c.read_text()
anchor = '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
'''
helper = r'''bool minic_parser_get_predefined_function_name_object(MinicParser *parser,
                                                       MinicGlobalObjectId *object_id) {
    const MinicFunction *function;
    MinicGlobalObjectId created_id;
    MinicType array_type;
    MinicType const_char_type;
    char object_name[64];
    int object_name_length;
    size_t index;

    if (parser == NULL || object_id == NULL ||
        parser->current_function == MINIC_FUNCTION_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "__func__ is only available inside a function");
        }
        return false;
    }
    if (parser->current_function_name_object != MINIC_GLOBAL_OBJECT_INVALID) {
        *object_id = parser->current_function_name_object;
        return true;
    }
    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL || function->name == NULL || function->name_length == 0U ||
        function->name_length == SIZE_MAX) {
        minic_parser_error(parser, "cannot determine predefined function name");
        return false;
    }
    if (!minic_type_add_const(minic_type_char(), &const_char_type) ||
        !minic_c0_program_add_array_type(
            parser->program, const_char_type, function->name_length + 1U, &array_type)) {
        minic_parser_error(parser, "cannot build __func__ array type");
        return false;
    }
    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_func_name_%zu",
                                  (size_t)parser->current_function);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            array_type,
                                            true,
                                            true,
                                            &created_id)) {
        minic_parser_error(parser, "cannot create __func__ backing object");
        return false;
    }
    for (index = 0U; index < function->name_length; ++index) {
        if (!minic_c0_global_object_add_initializer(
                parser->program, created_id, (int)(unsigned char)function->name[index])) {
            minic_parser_error(parser, "cannot store __func__ name bytes");
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, created_id, 0)) {
        minic_parser_error(parser, "cannot terminate __func__ name object");
        return false;
    }
    parser->current_function_name_object = created_id;
    *object_id = created_id;
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'string object insertion anchor mismatch: {text.count(anchor)}')
string_c.write_text(text.replace(anchor, helper + anchor, 1))

# Identifier resolution lowers __func__ to the ordinary GlobalObject expression
# path before generic namespace lookup, then all postfix/decay rules stay shared.
expr = root / 'src/frontend/parser_expression.c'
text = expr.read_text()
old = '''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (current_is_builtin_offsetof(parser)) {
            return parse_builtin_offsetof(parser, expression_id);
        }
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (current_is_builtin_offsetof(parser)) {
            return parse_builtin_offsetof(parser, expression_id);
        }
        if (current_identifier_is(parser, "__func__")) {
            name_span = parser->current.span;
            if (!minic_parser_get_predefined_function_name_object(parser, &global_object_id) ||
                !minic_parser_advance(parser) ||
                !parse_global_reference(parser, name_span, global_object_id, true, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
'''
if text.count(old) != 1:
    raise SystemExit(f'identifier __func__ dispatch anchor mismatch: {text.count(old)}')
expr.write_text(text.replace(old, new, 1))

# Tie the parser cache exactly to current_function lifetime.
func = root / 'src/frontend/parser_function.c'
text = func.read_text()
old = '''    parser->current_function = function_id;
    if (is_main) {
'''
new = '''    parser->current_function = function_id;
    parser->current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    if (is_main) {
'''
if text.count(old) != 1:
    raise SystemExit(f'function cache entry anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''    minic_parser_end_scope(parser);
    parser->current_function = MINIC_FUNCTION_INVALID;
    return true;
'''
new = '''    minic_parser_end_scope(parser);
    parser->current_function = MINIC_FUNCTION_INVALID;
    parser->current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    return true;
'''
if text.count(old) != 1:
    raise SystemExit(f'function cache exit anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''    parser.current_block = MINIC_BLOCK_INVALID;
    parser.current_function = MINIC_FUNCTION_INVALID;
    parser.continue_target_statement = MINIC_STATEMENT_INVALID;
'''
new = '''    parser.current_block = MINIC_BLOCK_INVALID;
    parser.current_function = MINIC_FUNCTION_INVALID;
    parser.current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    parser.continue_target_statement = MINIC_STATEMENT_INVALID;
'''
if text.count(old) != 1:
    raise SystemExit(f'parser cache initialization anchor mismatch: {text.count(old)}')
func.write_text(text.replace(old, new, 1))

# Permanent focused/runtime coverage.
(root / 'tests/programs/c0/predefined_func_name.c').write_text(r'''static int check_func(void)
{
    if (sizeof(__func__) != 11U) {
        return 1;
    }
    if (__func__[0] != 'c' || __func__[9] != 'c') {
        return 2;
    }
    if (&__func__[0] != &__func__[0]) {
        return 3;
    }
    return 0;
}

int main(void)
{
    return check_func();
}
''')
(root / 'tests/compiler/c0/invalid_predefined_func_name_write.c').write_text(r'''int change_name(void)
{
    __func__[0] = 'x';
    return 0;
}
''')
(root / 'tests/compiler/c0/invalid_predefined_func_name_file_scope.c').write_text(r'''int file_scope_size = sizeof(__func__);

int main(void)
{
    return file_scope_size;
}
''')
(root / 'tests/compiler/c0/run-predefined-func-name.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-predefined-func-name"
mkdir -p "$work"

"$minic" -S "$root/tests/programs/c0/predefined_func_name.c" -o "$work/positive.s"
object_count=$(grep -c '^\.Lminic_func_name_[0-9][0-9]*:$' "$work/positive.s")
if [ "$object_count" -ne 1 ]; then
    printf '%s\n' "FAIL compiler/c0/predefined-func-name: expected one stable backing object, got $object_count" >&2
    exit 1
fi
# check_func is 10 characters; the backing array includes its terminating NUL.
grep -F '.size .Lminic_func_name_' "$work/positive.s" | grep -F ', 11' >/dev/null

expect_failure() {
    name=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_predefined_func_name_write \
    'assignment expression requires a modifiable object lvalue'
expect_failure invalid_predefined_func_name_file_scope \
    '__func__ is only available inside a function'

printf '%s\n' \
    'PASS compiler/c0/predefined-func-name object=stable type=static-const-char-array sizeof=array decay=shared lexical-name=1'
''')

manifest = root / 'tests/programs/c0/manifest.txt'
text = manifest.read_text()
if '\npredefined_func_name\n' not in '\n' + text:
    if not text.endswith('\n'):
        text += '\n'
    text += 'predefined_func_name\n'
manifest.write_text(text)

# Give this language semantic its own focused formal gate.
gate = root / '.github/scripts/compiler-c0-full-gate.sh'
text = gate.read_text()
anchor = '''static_local_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-static-local-focused" \\
        sh tests/compiler/c0/run-static-local-arrays.sh
}
'''
addition = anchor + '''
predefined_func_name_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-predefined-func-name" \\
        sh tests/compiler/c0/run-predefined-func-name.sh
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'full gate function insertion mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition, 1)
old = 'start_gate static-local-focused static_local_focused\n'
new = old + 'start_gate predefined-func-name-focused predefined_func_name_focused\n'
if text.count(old) != 1:
    raise SystemExit(f'full gate start insertion mismatch: {text.count(old)}')
gate.write_text(text.replace(old, new, 1))
