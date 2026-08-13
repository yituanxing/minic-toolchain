from pathlib import Path

root = Path('.')

# 1. Expand the shared static-object parser contract so suffix object attributes
#    can update the declaration-owned metadata accumulator before entity creation.
internal = root / 'src/frontend/parser_internal.h'
text = internal.read_text()
old = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType object_type,
                                                 MinicSourceSpan name_span);'''
new = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType object_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment);'''
if text.count(old) != 1:
    raise SystemExit(f'parser_internal static-global signature mismatch: {text.count(old)}')
internal.write_text(text.replace(old, new, 1))

# 2. Let inferred static char arrays consume post-array object attributes, while
#    preserving the existing incomplete-array + string-initializer completion.
global_c = root / 'src/frontend/parser_global.c'
text = global_c.read_text()
old = '''static bool parse_static_inferred_char_array(MinicParser *parser,
                                             MinicType element_type,
                                             MinicSourceSpan name_span) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || !minic_type_is_char_integer(element_type) ||
        !minic_type_is_const(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static character array") ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {'''
new = '''static bool parse_static_inferred_char_array(MinicParser *parser,
                                             MinicType element_type,
                                             MinicSourceSpan name_span,
                                             char *section_name,
                                             size_t section_capacity,
                                             size_t *section_name_length,
                                             bool *has_section,
                                             size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_char_integer(element_type) || !minic_type_is_const(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static character array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {'''
if text.count(old) != 1:
    raise SystemExit(f'inferred char-array helper mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span) {'''
new = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment) {'''
if text.count(old) != 1:
    raise SystemExit(f'global after-head signature mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_static_inferred_char_array(parser, element_type, name_span);
        }'''
new = '''        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_static_inferred_char_array(parser,
                                                    element_type,
                                                    name_span,
                                                    section_name,
                                                    section_capacity,
                                                    section_name_length,
                                                    has_section,
                                                    explicit_alignment);
        }'''
if text.count(old) != 1:
    raise SystemExit(f'inferred char-array call mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

# Preserve the direct legacy entry point by giving it the same metadata semantics.
old = '''bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;

    if (parser == NULL ||'''
new = '''bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (parser == NULL ||'''
if text.count(old) != 1:
    raise SystemExit(f'direct static-global prologue mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    return minic_parser_parse_static_global_after_head(parser, object_type, name_span);
}'''
new = '''    if (!minic_parser_parse_static_global_after_head(parser,
                                                     object_type,
                                                     name_span,
                                                     section_name,
                                                     sizeof(section_name),
                                                     &section_name_length,
                                                     &has_section,
                                                     &explicit_alignment)) {
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        (has_section && !minic_c0_global_object_set_section(
                            parser->program, object_id, section_name, section_name_length)) ||
        (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                         parser->program, object_id, explicit_alignment))) {
        minic_parser_error(parser, "cannot persist static object metadata");
        return false;
    }
    return true;
}'''
if text.count(old) != 1:
    raise SystemExit(f'direct static-global tail mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
global_c.write_text(text)

# 3. File-scope declaration routing now treats explicit alignment exactly like
#    section: parse through the shared OBJECT consumer, then persist on the
#    resolved GlobalObject after the bounded static-object parser succeeds.
func = root / 'src/frontend/parser_function.c'
text = func.read_text()
old = '''        if (has_visibility || object_explicit_alignment != 0U) {
            minic_parser_error(
                parser, "static object symbol/layout attributes require explicit object semantics");
            return false;
        }
        if (!minic_parser_parse_static_global_after_head(parser, return_type, name_span)) {
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            (has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length))) {
            minic_parser_error(parser, "cannot persist static object section metadata");
            return false;
        }'''
new = '''        if (has_visibility) {
            minic_parser_error(
                parser, "static object symbol attributes require explicit object semantics");
            return false;
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
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            (has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length)) ||
            (object_explicit_alignment != 0U &&
             !minic_c0_global_object_set_explicit_alignment(
                 parser->program, object_id, object_explicit_alignment))) {
            minic_parser_error(parser, "cannot persist static object metadata");
            return false;
        }'''
if text.count(old) != 1:
    raise SystemExit(f'static object routing mismatch: {text.count(old)}')
func.write_text(text.replace(old, new, 1))

# 4. Upgrade the existing focused gate instead of creating a parallel test owner.
fixture = root / 'tests/compiler/c0/static_global_object_section.c'
fixture.write_text('''static int __attribute__((__section__(".data.static.init"))) section_initialized = 7;\n'
                   'static int __attribute__((section(".data.static.zero"))) section_zero;\n'
                   'static void *__attribute__((__used__))\n'
                   '__attribute__((__section__(".discard.addressable"))) addressable_shape = (void *)0;\n'
                   'static int __attribute__((aligned(16))) aligned_static = 1;\n'
                   'static const char linux_setup_string[]\n'
                   '__attribute__((section(".init.rodata")))\n'
                   '__attribute__((__aligned__(1))) = "reset_devices";\n\n'
                   'int read_static_global_sections(void) {\n'
                   '    return section_initialized + section_zero + aligned_static + linux_setup_string[0] +\n'
                   '           (addressable_shape == (void *)0);\n'
                   '}\n'''.replace("'\n                   '", ''))

invalid = root / 'tests/compiler/c0/invalid_static_global_alignment.c'
invalid.write_text('static int __attribute__((aligned(3))) invalid_static_alignment = 1;\n')

runner = root / 'tests/compiler/c0/run-static-global-object-section.sh'
text = runner.read_text()
old = '''grep -F '.section .discard.addressable' "$work/static_global_object_section.s" >/dev/null
grep -F 'addressable_shape:' "$work/static_global_object_section.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_global_alignment.c" \\
    -o "$work/invalid-alignment.s" >"$work/invalid-alignment.stdout" 2>"$work/invalid-alignment.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/static-global-object-section: static alignment widened accidentally' >&2
    exit 1
fi
grep -F 'static object symbol/layout attributes require explicit object semantics' \\
    "$work/invalid-alignment.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/static-global-object-section section=global-metadata initialized+zero+used-composition alignment=fail-closed'
'''
new = '''grep -F '.section .discard.addressable' "$work/static_global_object_section.s" >/dev/null
grep -F 'addressable_shape:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .init.rodata' "$work/static_global_object_section.s" >/dev/null
grep -F 'linux_setup_string:' "$work/static_global_object_section.s" >/dev/null
grep -F 'aligned_static:' "$work/static_global_object_section.s" >/dev/null
awk '\n    /[.]type aligned_static, @object/ { seen = 1; next }\n    seen && /[.]align 4/ { aligned = 1; next }\n    seen && /aligned_static:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"

if "$minic" -S "$root/tests/compiler/c0/invalid_static_global_alignment.c" \\
    -o "$work/invalid-alignment.s" >"$work/invalid-alignment.stdout" 2>"$work/invalid-alignment.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/static-global-object-section: non-power-of-two static alignment accepted' >&2
    exit 1
fi
grep -F 'GNU object alignment must be a power of two' "$work/invalid-alignment.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/static-global-object-section section=global-metadata initialized+zero+used+post-array alignment=global-metadata+validated inferred-char-array=string-bound'
'''
if text.count(old) != 1:
    raise SystemExit(f'static section runner tail mismatch: {text.count(old)}')
runner.write_text(text.replace(old, new, 1))
