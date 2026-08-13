from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# Expose one bounded declaration-list owner: file-scope static objects without
# initializers. Each comma declarator is rebuilt from the original base type.
anchor = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment);
'''
addition = anchor + '''bool minic_parser_parse_static_zero_declaration_list_after_head(
    MinicParser *parser,
    MinicType base_type,
    MinicType first_object_type,
    MinicSourceSpan first_name_span,
    const char *shared_section_name,
    size_t shared_section_name_length,
    bool shared_has_section,
    size_t shared_explicit_alignment);
'''
replace_once('src/frontend/parser_internal.h', anchor, addition, 'static zero list declaration')

path = root / 'src/frontend/parser_global.c'
text = path.read_text()
insert_before = 'bool minic_parser_parse_static_global_after_head(MinicParser *parser,\n'
index = text.find(insert_before)
if index < 0:
    raise SystemExit('static global after-head definition anchor missing')
helper = r'''bool minic_parser_parse_static_zero_declaration_list_after_head(
    MinicParser *parser,
    MinicType base_type,
    MinicType first_object_type,
    MinicSourceSpan first_name_span,
    const char *shared_section_name,
    size_t shared_section_name_length,
    bool shared_has_section,
    size_t shared_explicit_alignment) {
    bool first_declarator;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_COMMA ||
        (shared_has_section &&
         (shared_section_name == NULL || shared_section_name_length == 0U ||
          shared_section_name_length >= 256U))) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        char section_name[256];
        size_t section_name_length;
        size_t explicit_alignment;
        bool has_section;

        section_name_length = shared_section_name_length;
        explicit_alignment = shared_explicit_alignment;
        has_section = shared_has_section;
        (void)memset(section_name, 0, sizeof(section_name));
        if (shared_has_section) {
            (void)memcpy(section_name, shared_section_name, shared_section_name_length);
            section_name[shared_section_name_length] = '\0';
        }

        if (first_declarator) {
            object_type = first_object_type;
            name_span = first_name_span;
            first_declarator = false;
        } else {
            if (!minic_parser_parse_pointer_declarator(parser, base_type, &object_type) ||
                parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "expected static object declarator after ','");
                }
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }

        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           sizeof(section_name),
                                                           &section_name_length,
                                                           &has_section,
                                                           &explicit_alignment)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA &&
            parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(
                parser,
                "static zero-definition declaration list currently supports declarations only");
            return false;
        }
        if ((!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
             !minic_type_is_record(object_type)) ||
            (minic_type_is_record(object_type) &&
             !minic_parser_require_complete_object_type(
                 parser, object_type, "static object requires a complete record type"))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported static zero-definition declarator type");
            }
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(object_type),
                                                &object_id) ||
            !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
            (has_section &&
             !minic_c0_global_object_set_section(
                 parser->program, object_id, section_name, section_name_length)) ||
            (explicit_alignment != 0U &&
             !minic_c0_global_object_set_explicit_alignment(
                 parser->program, object_id, explicit_alignment))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot create static zero-definition declarator");
            }
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return minic_parser_advance(parser);
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

'''
text = text[:index] + helper + text[index:]
path.write_text(text)

# Top-level declaration owner retains base_type and delegates only comma-shaped
# no-initializer static object lists to the bounded list helper.
old = '''        if (!minic_parser_parse_static_global_after_head(parser,
                                                         return_type,
                                                         name_span,
                                                         section_name,
                                                         sizeof(section_name),
                                                         &section_name_length,
                                                         &has_section,
                                                         &object_explicit_alignment)) {
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
'''
new = '''        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_static_zero_declaration_list_after_head(
                parser,
                base_type,
                return_type,
                name_span,
                section_name,
                section_name_length,
                has_section,
                object_explicit_alignment);
        }
        if (!minic_parser_parse_static_global_after_head(parser,
                                                         return_type,
                                                         name_span,
                                                         section_name,
                                                         sizeof(section_name),
                                                         &section_name_length,
                                                         &has_section,
                                                         &object_explicit_alignment)) {
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
'''
replace_once('src/frontend/parser_function.c', old, new, 'static list top-level dispatch')

(root / 'tests/compiler/c0/static_zero_declaration_list.c').write_text(r'''struct Pair {
    int left;
    int right;
};

static const char *panic_later, *panic_param;
static int **deep, *shallow;
static struct Pair first_pair, second_pair;

int main(void)
{
    if (panic_later != 0 || panic_param != 0 || deep != 0 || shallow != 0) {
        return 1;
    }
    return first_pair.left + first_pair.right + second_pair.left + second_pair.right;
}
''')
(root / 'tests/compiler/c0/invalid_static_zero_declaration_list_initializer.c').write_text(r'''static int first, second = 1;

int main(void)
{
    return first + second;
}
''')
(root / 'tests/compiler/c0/run-static-zero-declaration-list.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-zero-declaration-list"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_zero_declaration_list.c" -o "$work/positive.s"
for symbol in panic_later panic_param deep shallow first_pair second_pair; do
    grep -F "$symbol:" "$work/positive.s" >/dev/null
    if grep -F ".globl $symbol" "$work/positive.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/static-zero-declaration-list: exported $symbol" >&2
        exit 1
    fi
done

if "$minic" -S "$root/tests/compiler/c0/invalid_static_zero_declaration_list_initializer.c" \
    -o "$work/invalid.s" >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-zero-declaration-list-initializer: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'static zero-definition declaration list currently supports declarations only' \
    "$work/invalid.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static-zero-declaration-list linkage=internal base-type=reparsed scalar=integer+pointer+record initializer=fail-closed'
''')

# Freeze in full gate next to other static-object owner tests.
gate = root / '.github/scripts/compiler-c0-full-gate.sh'
text = gate.read_text()
old = '''        run-static-pointer-arrays.sh \\
        run-pointer-array-typed-null.sh \\
        run-static-zero-definitions.sh \\
'''
new = '''        run-static-pointer-arrays.sh \\
        run-pointer-array-typed-null.sh \\
        run-static-zero-definitions.sh \\
        run-static-zero-declaration-list.sh \\
'''
if text.count(old) != 1:
    raise SystemExit(f'full gate static list insertion mismatch: {text.count(old)}')
gate.write_text(text.replace(old, new, 1))
