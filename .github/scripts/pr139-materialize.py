from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_statement.c"
test_c_path = root / "tests/compiler/c0/static_local_scalar.c"
test_sh_path = root / "tests/compiler/c0/run-static-local-scalars.sh"

text = parser_path.read_text()

anchor = '''static bool
parse_static_local_integer_constant(MinicParser *parser, const char *range_message, int *value) {
'''
insert = '''typedef struct MinicStaticLocalAttributeContext {
    char section_name[256];
    size_t section_name_length;
    bool has_section;
} MinicStaticLocalAttributeContext;

static bool consume_static_local_interleaved_attribute(MinicParser *parser,
                                                       const MinicParsedAttribute *attribute,
                                                       void *opaque_context);

static bool
parse_static_local_integer_constant(MinicParser *parser, const char *range_message, int *value) {
'''
assert text.count(anchor) == 1
text = text.replace(anchor, insert, 1)

old = '''static bool parse_inferred_static_local_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span,
                                              MinicGlobalObjectId *out_object_id) {
'''
new = '''static bool parse_inferred_static_local_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span,
                                              MinicStaticLocalAttributeContext *attributes,
                                              MinicGlobalObjectId *out_object_id) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }
'''
new = '''    if (parser == NULL || attributes == NULL ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'") ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''static bool parse_static_local_array_declarator(MinicParser *parser,
                                                MinicType base_type,
                                                MinicGlobalObjectId *out_object_id) {
'''
new = '''static bool parse_static_local_array_declarator(MinicParser *parser,
                                                MinicType base_type,
                                                MinicStaticLocalAttributeContext *attributes,
                                                MinicGlobalObjectId *out_object_id) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''    if (out_object_id == NULL) {
        return false;
    }
'''
new = '''    if (attributes == NULL || out_object_id == NULL) {
        return false;
    }
'''
# This exact fragment occurs in more than one helper; replace only after the declarator signature.
pos = text.index('static bool parse_static_local_array_declarator')
sub = text[pos:]
assert sub.count(old) >= 1
sub = sub.replace(old, new, 1)
text = text[:pos] + sub

old = '''    if (!minic_parser_advance(parser)) {
        return false;
    }

    bound_count = 0U;
'''
new = '''    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes)) {
        return false;
    }

    bound_count = 0U;
'''
pos = text.index('static bool parse_static_local_array_declarator')
sub = text[pos:]
assert sub.count(old) >= 1
sub = sub.replace(old, new, 1)
text = text[:pos] + sub

old = '''            return parse_inferred_static_local_array(
                parser, declared_type, name_span, out_object_id);
'''
new = '''            return parse_inferred_static_local_array(
                parser, declared_type, name_span, attributes, out_object_id);
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''        bound_count += 1U;
    }
    if (bound_count == 0U) {
'''
new = '''        bound_count += 1U;
    }
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes)) {
        return false;
    }
    if (bound_count == 0U) {
'''
pos = text.index('static bool parse_static_local_array_declarator')
sub = text[pos:]
assert sub.count(old) >= 1
sub = sub.replace(old, new, 1)
text = text[:pos] + sub

duplicate = '''typedef struct MinicStaticLocalAttributeContext {
    char section_name[256];
    size_t section_name_length;
    bool has_section;
} MinicStaticLocalAttributeContext;

'''
# One copy was inserted before the consumers and one is the old definition.
assert text.count(duplicate) == 2
first = text.index(duplicate)
second = text.index(duplicate, first + len(duplicate))
text = text[:second] + text[second + len(duplicate):]

old = '''static bool parse_static_local_declaration(MinicParser *parser) {
    MinicStaticLocalAttributeContext attributes;
    MinicType base_type;

    (void)memset(&attributes, 0, sizeof(attributes));
    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &attributes)) {
        return false;
    }
'''
new = '''static bool parse_static_local_declaration(MinicParser *parser) {
    MinicStaticLocalAttributeContext declaration_attributes;
    MinicType base_type;

    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
        return false;
    }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''    for (;;) {
        MinicGlobalObjectId object_id;

        if (!parse_static_local_array_declarator(parser, base_type, &object_id)) {
            return false;
        }
        if (attributes.has_section &&
            !minic_c0_global_object_set_section(parser->program,
                                                object_id,
                                                attributes.section_name,
                                                attributes.section_name_length)) {
'''
new = '''    for (;;) {
        MinicStaticLocalAttributeContext attributes;
        MinicGlobalObjectId object_id;

        attributes = declaration_attributes;
        if (!parse_static_local_array_declarator(
                parser, base_type, &attributes, &object_id)) {
            return false;
        }
        if (attributes.has_section &&
            !minic_c0_global_object_set_section(parser->program,
                                                object_id,
                                                attributes.section_name,
                                                attributes.section_name_length)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

parser_path.write_text(text)

c_text = test_c_path.read_text()
append = '''

static int sectioned_static_locals(void) {
    static int scalar __attribute__((__section__(".minic.static.scalar"))) = 3;
    static char fixed[8] __attribute__((__section__(".minic.static.fixed")));
    static char inferred[] __attribute__((__section__(".minic.static.inferred"))) = "ok";
    static int first __attribute__((__section__(".minic.static.first"))), second;

    return scalar + fixed[0] + inferred[0] + first + second;
}

int read_sectioned_static_locals(void) {
    return sectioned_static_locals();
}
'''
assert 'sectioned_static_locals' not in c_text
c_text = c_text.rstrip() + append

test_c_path.write_text(c_text)

sh_text = test_sh_path.read_text()
old = '''if grep -F '.globl __minic_static_local_' "$work/static_local_scalar.s" >/dev/null; then
    echo 'static local scalar leaked external linkage' >&2
    exit 1
fi
printf '%s\\n' 'PASS compiler/c0/static_local_scalar integer=writable-null pointer=const-null storage=internal-global lifetime=static zero-width=8 addressable=1'
'''
new = '''if grep -F '.globl __minic_static_local_' "$work/static_local_scalar.s" >/dev/null; then
    echo 'static local scalar leaked external linkage' >&2
    exit 1
fi
for section in .minic.static.scalar .minic.static.fixed .minic.static.inferred .minic.static.first; do
    test "$(grep -c -F ".section $section" "$work/static_local_scalar.s")" -eq 1
done
# The second declarator must not inherit the first declarator's suffix section.
test "$(grep -c -F '.section .minic.static.first' "$work/static_local_scalar.s")" -eq 1
test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -ge 7
printf '%s\\n' 'PASS compiler/c0/static_local_scalar integer=writable-null pointer=const-null suffix-section=scalar,fixed,inferred initialized=1 per-declarator-isolation=1 storage=internal-global lifetime=static'
'''
assert sh_text.count(old) == 1
sh_text = sh_text.replace(old, new, 1)
test_sh_path.write_text(sh_text)
