from pathlib import Path

root = Path('.')

global_c = root / 'src/frontend/parser_global.c'
text = global_c.read_text()
old = '''    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {'''
new = '''    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
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

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {'''
if text.count(old) != 1:
    raise SystemExit(f'static after-head attribute seam mismatch: {text.count(old)}')
global_c.write_text(text.replace(old, new, 1))

fixture = root / 'tests/compiler/c0/static_global_object_section.c'
text = fixture.read_text()
anchor = '''static int __attribute__((aligned(16))) aligned_static = 1;
static const char linux_setup_string[]
'''
replacement = '''static int __attribute__((aligned(16))) aligned_static = 1;
struct static_metadata_record {
    int value;
};
static struct static_metadata_record record_suffix_metadata
__attribute__((__used__))
__attribute__((section(".data.static.record")))
__attribute__((aligned(8))) = {3};
static int scalar_suffix_metadata __attribute__((section(".data.static.scalar")))
__attribute__((aligned(8))) = 2;
static const char linux_setup_string[]
'''
if text.count(anchor) != 1:
    raise SystemExit(f'static metadata fixture anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
old_return = '''    return section_initialized + section_zero + aligned_static + linux_setup_string[0] +
           (addressable_shape == (void *)0);'''
new_return = '''    return section_initialized + section_zero + aligned_static + record_suffix_metadata.value +
           scalar_suffix_metadata + linux_setup_string[0] +
           (addressable_shape == (void *)0);'''
if text.count(old_return) != 1:
    raise SystemExit(f'static metadata fixture return mismatch: {text.count(old_return)}')
fixture.write_text(text.replace(old_return, new_return, 1))

runner = root / 'tests/compiler/c0/run-static-global-object-section.sh'
text = runner.read_text()
anchor = '''grep -F 'aligned_static:' "$work/static_global_object_section.s" >/dev/null
awk '\n    /[.]type aligned_static, @object/ { seen = 1; next }\n    seen && /[.]align 4/ { aligned = 1; next }\n    seen && /aligned_static:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"
'''
replacement = '''grep -F 'aligned_static:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.record' "$work/static_global_object_section.s" >/dev/null
grep -F 'record_suffix_metadata:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.scalar' "$work/static_global_object_section.s" >/dev/null
grep -F 'scalar_suffix_metadata:' "$work/static_global_object_section.s" >/dev/null
awk '\n    /[.]type aligned_static, @object/ { seen = 1; next }\n    seen && /[.]align 4/ { aligned = 1; next }\n    seen && /aligned_static:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"
awk '\n    /[.]type record_suffix_metadata, @object/ { seen = 1; next }\n    seen && /[.]align 3/ { aligned = 1; next }\n    seen && /record_suffix_metadata:/ { exit(aligned ? 0 : 1) }\n    END { if (!seen || !aligned) exit 1 }\n' "$work/static_global_object_section.s"
'''
if text.count(anchor) != 1:
    raise SystemExit(f'static metadata runner anchor mismatch: {text.count(anchor)}')
runner.write_text(text.replace(anchor, replacement, 1))
