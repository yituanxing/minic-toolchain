from pathlib import Path

root = Path('.')
parser_path = root / 'src/frontend/parser_global.c'
text = parser_path.read_text()

old = '''static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t declared_count;
    bool inferred_bound;

    record = minic_c0_program_record(parser->program, element_type.record_id);'''
new = '''static bool parse_static_record_array(MinicParser *parser,
                                      MinicType element_type,
                                      MinicSourceSpan name_span,
                                      char *section_name,
                                      size_t section_capacity,
                                      size_t *section_name_length,
                                      bool *has_section,
                                      size_t *explicit_alignment) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t declared_count;
    bool inferred_bound;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);'''
if text.count(old) != 1:
    raise SystemExit(f'record-array signature anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||'''
if text.count(old) != 1:
    raise SystemExit(f'record-array attribute seam mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    const MinicRecord *record;'''
new = '''static bool parse_static_record(MinicParser *parser,
                                MinicType type,
                                MinicSourceSpan name_span,
                                char *section_name,
                                size_t section_capacity,
                                size_t *section_name_length,
                                bool *has_section,
                                size_t *explicit_alignment) {
    const MinicRecord *record;'''
if text.count(old) != 1:
    raise SystemExit(f'record signature anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
    }'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser,
                                         type,
                                         name_span,
                                         section_name,
                                         section_capacity,
                                         section_name_length,
                                         has_section,
                                         explicit_alignment);
    }'''
if text.count(old) != 1:
    raise SystemExit(f'record-array call anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old_call = 'return parse_static_record(parser, element_type, name_span);'
new_call = '''return parse_static_record(parser,
                                   element_type,
                                   name_span,
                                   section_name,
                                   section_capacity,
                                   section_name_length,
                                   has_section,
                                   explicit_alignment);'''
if text.count(old_call) != 2:
    raise SystemExit(f'static record dispatch count mismatch: {text.count(old_call)}')
text = text.replace(old_call, new_call)
parser_path.write_text(text)

fixture_path = root / 'tests/compiler/c0/static_global_object_section.c'
text = fixture_path.read_text()
anchor = '''static struct static_metadata_record record_suffix_metadata __attribute__((__used__))
__attribute__((section(".data.static.record"))) __attribute__((aligned(8))) = {3};
static int scalar_suffix_metadata'''
replacement = '''static struct static_metadata_record record_suffix_metadata __attribute__((__used__))
__attribute__((section(".data.static.record"))) __attribute__((aligned(8))) = {3};
static struct static_metadata_record inferred_record_array_suffix_metadata[]
__attribute__((section(".data.static.record.array"))) __attribute__((aligned(16))) = {{4}, {5}};
static int scalar_suffix_metadata'''
if text.count(anchor) != 1:
    raise SystemExit(f'fixture anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
old_return = '''    return section_initialized + section_zero + aligned_static + record_suffix_metadata.value +
           scalar_suffix_metadata + linux_setup_string[0] + (addressable_shape == (void *)0);'''
new_return = '''    return section_initialized + section_zero + aligned_static + record_suffix_metadata.value +
           inferred_record_array_suffix_metadata[0].value +
           inferred_record_array_suffix_metadata[1].value + scalar_suffix_metadata +
           linux_setup_string[0] + (addressable_shape == (void *)0);'''
if text.count(old_return) != 1:
    raise SystemExit(f'fixture return anchor mismatch: {text.count(old_return)}')
fixture_path.write_text(text.replace(old_return, new_return, 1))

runner_path = root / 'tests/compiler/c0/run-static-global-object-section.sh'
text = runner_path.read_text()
anchor = '''grep -F '.section .data.static.record' "$work/static_global_object_section.s" >/dev/null
grep -F 'record_suffix_metadata:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.scalar' "$work/static_global_object_section.s" >/dev/null'''
replacement = '''grep -F '.section .data.static.record' "$work/static_global_object_section.s" >/dev/null
grep -F 'record_suffix_metadata:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.record.array' "$work/static_global_object_section.s" >/dev/null
grep -F 'inferred_record_array_suffix_metadata:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.scalar' "$work/static_global_object_section.s" >/dev/null'''
if text.count(anchor) != 1:
    raise SystemExit(f'runner grep anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
anchor = '''awk '\n    /[.]type record_suffix_metadata, @object/ { seen = 1; next }\n    seen && /[.]align 3/ { aligned = 1; next }\n    seen && /record_suffix_metadata:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"
'''
replacement = anchor + '''awk '\n    /[.]type inferred_record_array_suffix_metadata, @object/ { seen = 1; next }\n    seen && /[.]align 4/ { aligned = 1; next }\n    seen && /inferred_record_array_suffix_metadata:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"
'''
if text.count(anchor) != 1:
    raise SystemExit(f'runner alignment anchor mismatch: {text.count(anchor)}')
runner_path.write_text(text.replace(anchor, replacement, 1))
