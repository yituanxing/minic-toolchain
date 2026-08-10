#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# Add the shared semantic array-declarator suffix builder next to the function declarator kernel.
path = Path("src/frontend/parser_declarator.c")
text = path.read_text()
anchor = "bool minic_parser_build_function_declarator_type(MinicParser *parser,\n"
helper = r'''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array) {
    size_t bounds[8];
    size_t bound_count;
    size_t dimension;
    bool outermost_incomplete;
    MinicType type;

    if (parser == NULL || declarator_type == NULL || is_array == NULL) {
        return false;
    }
    *declarator_type = element_type;
    *is_array = false;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return true;
    }

    bound_count = 0U;
    outermost_incomplete = false;
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "array declarator supports at most eight dimensions");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (!allow_incomplete_outermost || bound_count != 0U) {
                minic_parser_error(parser, "only the outermost array dimension may be incomplete");
                return false;
            }
            outermost_incomplete = true;
            bounds[bound_count] = 0U;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }

    type = element_type;
    dimension = bound_count;
    while (dimension > 0U) {
        dimension -= 1U;
        if (dimension == 0U && outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {
                minic_parser_error(parser, "cannot build incomplete array declarator type");
                return false;
            }
        } else if (!minic_c0_program_add_array_type(
                       parser->program, type, bounds[dimension], &type)) {
            minic_parser_error(parser, "cannot build array declarator type");
            return false;
        }
    }
    *declarator_type = type;
    *is_array = true;
    return true;
}

'''
text = replace_once(text, anchor, helper + anchor, "array-declarator-helper")
path.write_text(text)

# Publish the internal migration seam.
path = Path("src/frontend/parser_internal.h")
text = path.read_text()
anchor = "bool minic_parser_build_function_declarator_type(MinicParser *parser,\n"
prototype = r'''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array);
'''
text = replace_once(text, anchor, prototype + anchor, "array-declarator-prototype")
path.write_text(text)

# Replace the one-dimensional extern-only suffix parser with the shared builder.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
text = replace_once(
    text,
    "        bool declarator_has_section;\n        bool is_array;\n",
    "        bool declarator_has_section;\n        bool is_array;\n        MinicType declarator_element_type;\n",
    "extern-array-declaration-state",
)
text = replace_once(
    text,
    "        if (!minic_parser_parse_gnu_section_attribute(parser,\n"
    "                                                      declarator_section_name,\n"
    "                                                      sizeof(declarator_section_name),\n"
    "                                                      &declarator_section_name_length,\n"
    "                                                      &declarator_has_section)) {\n"
    "            return false;\n"
    "        }\n"
    "        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||\n"
    "            minic_type_is_array(object_type)) {\n",
    "        declarator_element_type = object_type;\n"
    "        if (!minic_parser_parse_gnu_section_attribute(parser,\n"
    "                                                      declarator_section_name,\n"
    "                                                      sizeof(declarator_section_name),\n"
    "                                                      &declarator_section_name_length,\n"
    "                                                      &declarator_has_section)) {\n"
    "            return false;\n"
    "        }\n"
    "        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||\n"
    "            minic_type_is_array(object_type)) {\n",
    "extern-array-leaf-type",
)
start_marker = "        is_array = false;\n        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n"
end_marker = "\n        if (!minic_c0_program_add_global_object(\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("extern-array-suffix: cannot find legacy one-dimensional range")
replacement = r'''        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_section_attribute(parser,
                                                      declarator_section_name,
                                                      sizeof(declarator_section_name),
                                                      &declarator_section_name_length,
                                                      &declarator_has_section)) {
            return false;
        }
'''
text = text[:start] + replacement + text[end:]
old_const = r'''                is_array ? minic_type_is_const(
                               parser->program->array_types[object_type.array_type_id].element_type)
                         : minic_type_is_const(object_type),
'''
text = replace_once(
    text,
    old_const,
    "                minic_type_is_const(declarator_element_type),\n",
    "extern-array-constness",
)
path.write_text(text)

# Add a Linux-shaped focused fixture that exercises declaration, nested subscripting and row decay.
Path("tests/compiler/c0/extern_multidimensional_array.c").write_text(
r'''extern const unsigned long
cpu_bit_bitmap[64 + 1][(((64) + ((sizeof(long) * 8)) - 1) / ((sizeof(long) * 8)))];

unsigned long read_cpu_bit(unsigned int cpu, unsigned int word) {
    return cpu_bit_bitmap[cpu][word];
}

const unsigned long *cpu_bit_row(unsigned int cpu) {
    const unsigned long *row = cpu_bit_bitmap[1 + cpu % 64];
    return row;
}
''')
Path("tests/compiler/c0/run-extern-multidimensional-array.sh").write_text(
r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-multidimensional-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/extern_multidimensional_array.c" \
    -o "$work/extern_multidimensional_array.i"
"$minic" -S "$work/extern_multidimensional_array.i" \
    -o "$work/extern_multidimensional_array.s"

test -s "$work/extern_multidimensional_array.s"
grep -F 'read_cpu_bit:' "$work/extern_multidimensional_array.s" >/dev/null
grep -F 'cpu_bit_row:' "$work/extern_multidimensional_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
''')

# Keep the new regression in the frozen Linux-focused set.
path = Path("tools/dev/pr76-focused.sh")
text = path.read_text()
anchor = 'sh tests/compiler/c0/run-extern-multi-declarators.sh\n'
text = replace_once(
    text,
    anchor,
    anchor + 'sh tests/compiler/c0/run-extern-multidimensional-array.sh\n',
    "extern-multidimensional-focused-gate",
)
path.write_text(text)
