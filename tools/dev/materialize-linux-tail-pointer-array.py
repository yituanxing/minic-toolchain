#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_global.c"
source = parser_path.read_text()


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


source = replace_once(
    source,
    '''static bool
parse_static_pointer_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    MinicGlobalObjectId *targets;
    MinicType object_type;
    MinicType string_pointer_type;
    MinicGlobalObjectId object_id;
''',
    '''static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span,
                                       char *section_name,
                                       size_t section_capacity,
                                       size_t *section_name_length,
                                       bool *has_section,
                                       size_t *explicit_alignment) {
    MinicStaticPointerInitializer *initializers;
    MinicType object_type;
    MinicGlobalObjectId object_id;
''',
    "pointer-array signature",
)
source = replace_once(source, "    targets = NULL;\n", "    initializers = NULL;\n", "initializer storage")

source = replace_once(
    source,
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
''',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
''',
    "post-array attributes",
)

source = replace_once(
    source,
    '''    if (!minic_type_pointer_to(minic_type_char(), &string_pointer_type)) {
        minic_parser_error(parser, "cannot build string pointer type");
        goto done;
    }

''',
    "",
    "remove string-only pointer type",
)

pattern = re.compile(
    r'''    while \(parser->current.kind != MINIC_TOKEN_RBRACE\) \{\n        MinicGlobalObjectId target_id;\n.*?\n\n        if \(!inferred_bound && target_count >= element_count\) \{''',
    re.S,
)
replacement = '''    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticPointerInitializer initializer;

        if (!parse_static_pointer_initializer(parser, element_type, &initializer)) {
            goto done;
        }
        if (!initializer.has_relocation && initializer.bits != 0U) {
            minic_parser_error(
                parser, "static pointer array nonzero integer pointer constants are unsupported");
            goto done;
        }

        if (!inferred_bound && target_count >= element_count) {'''
source, count = pattern.subn(replacement, source, count=1)
if count != 1:
    raise SystemExit(f"pointer-array initializer parser: expected one match, found {count}")

source = replace_once(
    source,
    "            MinicGlobalObjectId *resized;\n",
    "            MinicStaticPointerInitializer *resized;\n",
    "resize type",
)
source = replace_once(
    source,
    "            if (new_capacity < target_capacity || new_capacity > SIZE_MAX / sizeof(*targets)) {\n",
    "            if (new_capacity < target_capacity ||\n                new_capacity > SIZE_MAX / sizeof(*initializers)) {\n",
    "resize bound",
)
source = replace_once(
    source,
    '''            resized = (MinicGlobalObjectId *)realloc(targets, new_capacity * sizeof(*targets));
''',
    '''            resized = (MinicStaticPointerInitializer *)realloc(
                initializers, new_capacity * sizeof(*initializers));
''',
    "resize allocation",
)
source = replace_once(source, "            targets = resized;\n", "            initializers = resized;\n", "resize assign")
source = replace_once(
    source,
    "        targets[target_count] = target_id;\n",
    "        initializers[target_count] = initializer;\n",
    "store initializer",
)

source = replace_once(
    source,
    '''        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                    index,
                    targets[index])) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
''',
    '''        for (index = 0U; index < target_count; ++index) {
            const MinicStaticPointerInitializer *initializer;

            initializer = &initializers[index];
            if (initializer->has_relocation &&
                !minic_c0_global_object_add_object_relocation_path(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                    index,
                    initializer->relocation_target.object_id,
                    initializer->relocation_target.member_indices,
                    initializer->relocation_target.member_depth)) {
                minic_parser_error(parser, "cannot record static pointer-array relocation");
                goto done;
            }
        }
''',
    "relocation emission",
)
source = replace_once(source, "    free(targets);\n", "    free(initializers);\n", "free initializer storage")

source = replace_once(
    source,
    '''    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser, element_type, name_span);
    }
''',
    '''    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
''',
    "pointer-array caller",
)

parser_path.write_text(source)

(root / "tests/compiler/c0/static_pointer_array.c").write_text(
    '''extern int linker_start[];\n\n'''
    '''static char *names[] __attribute__((section(".init.data"))) = {\n'''
    '''    "alpha", "beta", ((void *)0),\n'''
    '''};\n\n'''
    '''static int *levels[] __attribute__((section(".init.data"))) = {\n'''
    '''    linker_start,\n'''
    '''};\n\n'''
    '''int main(void) {\n'''
    '''    return 0;\n'''
    '''}\n'''
)

(root / "tests/compiler/c0/run-static-pointer-arrays.sh").write_text(
    '''#!/bin/sh\n'''
    '''set -eu\n\n'''
    '''root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\n'''
    '''minic=${MINIC:-"$root/build/debug/bin/minic"}\n'''
    '''host_cc=${HOST_CC:-${CC:-cc}}\n'''
    '''work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-pointer-arrays\n\n'''
    '''rm -rf "$work"\n'''
    '''mkdir -p "$work"\n\n'''
    '''"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_pointer_array.c" -o "$work/static_pointer_array.i"\n'''
    '''"$minic" -S "$work/static_pointer_array.i" -o "$work/static_pointer_array.s"\n\n'''
    '''grep -F '.section .init.data' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F 'names:' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F '  .dword .Lminic_string_0' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F '  .dword .Lminic_string_1' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F '.size names, 24' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F 'levels:' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F '  .dword linker_start' "$work/static_pointer_array.s" >/dev/null\n'''
    '''grep -F '.size levels, 8' "$work/static_pointer_array.s" >/dev/null\n'''
    '''printf '%s\\n' 'PASS compiler/c0/static_pointer_array suffix-section=1 inferred=string+array-decay symbolic-reloc=3 null=1'\n'''
)
