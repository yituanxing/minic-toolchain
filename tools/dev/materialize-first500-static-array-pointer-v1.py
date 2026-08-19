#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:160]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# 1. Block-scope static pointer initializers already own MinicGlobalObject
#    storage. Route them through the same static pointer relocation semantics as
#    file-scope objects instead of the legacy null-only parser.
# ---------------------------------------------------------------------------
replace_once(
    "src/frontend/parser_statement.c",
    """        if (minic_type_is_pointer(declared_type)) {
            if (!minic_parser_parse_zero_pointer_constant(parser) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id) ||
                !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, \"cannot finalize static local null pointer storage\");
                }
                return false;
            }
            *out_object_id = scalar_object_id;
            return true;
        }
""",
    """        if (minic_type_is_pointer(declared_type)) {
            if (!minic_parser_parse_static_pointer_object_initializer(
                    parser, scalar_object_id, declared_type) ||
                !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, \"cannot finalize static local pointer storage\");
                }
                return false;
            }
            *out_object_id = scalar_object_id;
            return true;
        }
""",
)

# ---------------------------------------------------------------------------
# 2. Character integer arrays are still integer scalar arrays when initialized
#    with braces. Preserve the string-literal specialized path, but route a
#    braced inferred char/unsigned-char initializer through the common scalar
#    array transaction and its InitPlan owner.
# ---------------------------------------------------------------------------
replace_once(
    "src/frontend/parser_global.c",
    """    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
        !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(
                parser, \"inferred static character array requires a string literal initializer\");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, \"expected ';' after static character array\");
""",
    """    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
            !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, \"cannot initialize inferred static character array\");
            }
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!parse_static_scalar_array_transaction(parser, object_id, element_type, 0U, true)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser,
                                   \"cannot initialize inferred static character array from scalar list\");
            }
            return false;
        }
    } else {
        minic_parser_error(
            parser, \"inferred static character array requires a string or braced scalar initializer\");
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, \"expected ';' after static character array\");
""",
)

write(
    "tests/compiler/c0/static_local_pointer_array_decay.c",
    r'''static const unsigned char reserved_address_base[6] = { 1, 2, 3, 4, 5, 6 };

static int probe(void)
{
    static const unsigned short *value =
        (const unsigned short *)reserved_address_base;
    return value == (const unsigned short *)reserved_address_base ? 0 : 1;
}

int main(void)
{
    return probe();
}
''',
)

write(
    "tests/compiler/c0/inferred_static_unsigned_char_list.c",
    r'''static const unsigned char filetype_table[] = {
    0, 8, 4, 2, 6, 1, 12, 10
};

int main(void)
{
    return sizeof(filetype_table) == 8 &&
                   filetype_table[0] == 0 &&
                   filetype_table[1] == 8 &&
                   filetype_table[6] == 12 &&
                   filetype_table[7] == 10
               ? 0
               : 1;
}
''',
)

write(
    "tests/compiler/c0/run-first500-static-array-pointer-v1.sh",
    r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/first500-static-array-pointer-v1
mkdir -p "$work"

for name in static_local_pointer_array_decay inferred_static_unsigned_char_list; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
  "$minic" -S "$work/$name.i" -o "$work/$name.s"
  test -s "$work/$name.s"
done

printf '%s\n' 'PASS compiler/c0/first500-static-array-pointer-v1 local-pointer-relocation=1 inferred-uchar-list=1'
''',
)

replace_once(
    "tests/compiler/c0/run.sh",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"
""",
    """MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-pareto-v1.sh"

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-first500-static-array-pointer-v1.sh"
""",
)

replace_once(
    "tests/compiler/c0/run-runtime.sh",
    "run_case record_field_nonstring_attribute 0 record_field_nonstring_attribute\n",
    "run_case record_field_nonstring_attribute 0 record_field_nonstring_attribute\n"
    "run_case static_local_pointer_array_decay 0 static_local_pointer_array_decay\n"
    "run_case inferred_static_unsigned_char_list 0 inferred_static_unsigned_char_list\n",
)

print("FIRST500_STATIC_ARRAY_POINTER_V1_MATERIALIZED")
