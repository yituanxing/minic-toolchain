#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


path = Path("src/frontend/parser_function.c")
text = path.read_text()
text = replace_once(
    text,
    """    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||\n        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&\n         !minic_type_is_record(object_type))) {\n""",
    """    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||\n        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&\n         !minic_type_is_record(object_type) && !minic_type_is_array(object_type))) {\n""",
    "external object type gate",
)
text = replace_once(
    text,
    """    if (minic_type_is_record(object_type)) {\n        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {\n""",
    """    if (minic_type_is_record(object_type) || minic_type_is_array(object_type)) {\n        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {\n""",
    "external aggregate initializer",
)

begin = text.index("static bool parse_visible_external_array(MinicParser *parser,")
end = text.index("\ntypedef struct MinicParsedDeclarationPrefix {", begin)
replacement = r'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         char *section_name,
                                         size_t section_name_capacity,
                                         size_t *section_name_length,
                                         bool *has_section,
                                         size_t *explicit_alignment,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {
    MinicParser probe;
    MinicType array_type;
    bool is_array;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        section_name == NULL || section_name_length == NULL || has_section == NULL ||
        explicit_alignment == NULL) {
        return false;
    }

    /* Incomplete top-level array definitions still need the legacy bound-inference owner.
     * Keep that special case bounded until initializer semantics owns inferred aggregate shape. */
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser,
                               "incomplete external tentative array is not implemented yet");
            return false;
        }
        return parse_external_integer_array_definition(parser, element_type, name_span);
    }

    /* A complete array is an ordinary complete object. Materialize the full declarator first,
     * then collect suffix attributes before deciding tentative-definition vs definition. */
    if (!minic_parser_parse_array_declarator_suffix(
            parser, element_type, true, &array_type, &is_array) ||
        !is_array ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_name_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_external_tentative_object(parser,
                                               array_type,
                                               name_span,
                                               section_name,
                                               *section_name_length,
                                               *has_section,
                                               *explicit_alignment,
                                               visibility,
                                               has_visibility);
    }
    return parse_external_object_definition(parser,
                                            array_type,
                                            name_span,
                                            section_name,
                                            *section_name_length,
                                            *has_section,
                                            *explicit_alignment,
                                            visibility,
                                            has_visibility);
}
'''
text = text[:begin] + replacement + text[end:]
text = replace_once(
    text,
    """            return parse_visible_external_array(parser,\n                                                return_type,\n                                                name_span,\n                                                section_name,\n                                                section_name_length,\n                                                has_section,\n                                                object_explicit_alignment,\n                                                visibility,\n                                                has_visibility);\n""",
    """            return parse_visible_external_array(parser,\n                                                return_type,\n                                                name_span,\n                                                section_name,\n                                                sizeof(section_name),\n                                                &section_name_length,\n                                                &has_section,\n                                                &object_explicit_alignment,\n                                                visibility,\n                                                has_visibility);\n""",
    "visible external array callsite",
)
path.write_text(text)

runner = Path("tests/compiler/c0/run-foundation-focused.sh")
data = runner.read_text()
anchor = "    run-external-tentative-definitions.sh \\\n"
addition = anchor + "    run-external-array-declarator-routing.sh \\\n"
if "run-external-array-declarator-routing.sh" not in data:
    data = replace_once(data, anchor, addition, "foundation runner")
    runner.write_text(data)

Path("tests/compiler/c0/external_array_declarator_routing.c").write_text(r'''typedef unsigned char u8;

struct cpu_operations {
    int state;
};

const struct cpu_operations *cpu_ops[4]
    __attribute__((__section__(".data..ro_after_init")));

unsigned long empty_zero_page[8]
    __attribute__((__section__(".bss..page_aligned")))
    __attribute__((__aligned__(64)));

u8 purgatory_sha256_digest[32]
    __attribute__((__section__(".kexec-purgatory")));
u8 purgatory_sha_regions[2][4]
    __attribute__((__section__(".kexec-purgatory")));

unsigned long initialized_map[4]
    __attribute__((__section__(".data..ro_after_init"))) = {1, 2, 3, 4};
''')

Path("tests/compiler/c0/run-external-array-declarator-routing.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-array-declarator-routing

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c \
  "$root/tests/compiler/c0/external_array_declarator_routing.c"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/external_array_declarator_routing.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
for symbol in cpu_ops empty_zero_page purgatory_sha256_digest purgatory_sha_regions initialized_map; do
  grep -F "$symbol:" "$work/output.s" >/dev/null
done
grep -F '.data..ro_after_init' "$work/output.s" >/dev/null
grep -F '.bss..page_aligned' "$work/output.s" >/dev/null
grep -F '.kexec-purgatory' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/external_array_declarator_routing complete-array=generic-object suffix-attrs=1 multidim=1 initialized=1'
''')

print("staged complete external array declarator routing")
