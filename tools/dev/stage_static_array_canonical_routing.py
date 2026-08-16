from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    target.write_text(text.replace(old, new, 1))


path = Path("src/frontend/parser_global.c")
text = path.read_text()

# Inferred integer arrays are ordinary static objects; const controls
# mutability/read-only placement, not whether the declaration is legal.
old = """        !minic_type_is_integer(element_type) || !minic_type_is_const(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
"""
new = """        !minic_type_is_integer(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
"""
if text.count(old) != 1:
    raise SystemExit(f"inferred integer qualification anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = """                                            object_type,
                                            true,
                                            true,
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(
"""
new = """                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(
"""
# Restrict replacement to the inferred-integer helper region.
helper_start = text.index("static bool parse_static_inferred_integer_array(")
helper_end = text.index("static bool parse_static_inferred_char_array(", helper_start)
helper = text[helper_start:helper_end]
if helper.count(old) != 1:
    raise SystemExit(f"inferred integer read-only anchor count={helper.count(old)}")
helper = helper.replace(old, new, 1)
text = text[:helper_start] + helper + text[helper_end:]

# Replace the legacy fixed integer-array mini-parser with the canonical array
# declarator and shared static-storage initializer owners.  Keep inferred []
# dispatch separate for this bounded migration.
function_start = text.index("bool minic_parser_parse_static_global_after_head(")
legacy_start = text.index(
    "    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {",
    function_start,
)
legacy_end_marker = (
    "    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "
    "\"expected ';' after global object\");\n"
)
legacy_end = text.index(legacy_end_marker, legacy_start) + len(legacy_end_marker)
replacement = r'''    if (!minic_type_is_integer(element_type)) {
        minic_parser_error(parser, "static array requires an integer, pointer, or record element type");
        return false;
    }
    {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_char_integer(element_type)) {
                return parse_static_inferred_char_array(parser,
                                                        element_type,
                                                        name_span,
                                                        section_name,
                                                        section_capacity,
                                                        section_name_length,
                                                        has_section,
                                                        explicit_alignment);
            }
            return parse_static_inferred_integer_array(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment);
        }
    }
    {
        bool is_array;

        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &object_type, &is_array) ||
            !is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build fixed static array type");
            }
            return false;
        }
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add fixed static array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            minic_parser_error(parser, "cannot zero-initialize fixed static array object");
            return false;
        }
        return minic_parser_advance(parser);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !minic_parser_parse_static_storage_initializer_value(
            parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse fixed static array initializer");
        }
        return false;
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_SEMICOLON,
                               "expected ';' after static array initializer");
'''
text = text[:legacy_start] + replacement + text[legacy_end:]
path.write_text(text)

Path("tests/compiler/c0/static_mutable_arrays.c").write_text(
    r'''static char early_cmdline[2048];
static unsigned long riscv_isa[64];
static int aia_irq2bitpos[] = {0, -1, 0, -1, 2};
static const unsigned int fixed_values[4] = {1, 2, 3, 0};

int main(void) {
    early_cmdline[0] = 'x';
    riscv_isa[1] = 3;
    return aia_irq2bitpos[1] + (int)fixed_values[2] + (int)riscv_isa[1] +
           (int)early_cmdline[0];
}
'''
)

Path("tests/compiler/c0/run-static-mutable-arrays.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-mutable-arrays"
mkdir -p "$work"
asm="$work/static_mutable_arrays.s"

"$minic" -S "$root/tests/compiler/c0/static_mutable_arrays.c" -o "$asm"
grep -F '.type early_cmdline, @object' "$asm" >/dev/null
grep -F '.size early_cmdline, 2048' "$asm" >/dev/null
grep -F '.type riscv_isa, @object' "$asm" >/dev/null
grep -F '.size riscv_isa, 512' "$asm" >/dev/null
grep -F '.type aia_irq2bitpos, @object' "$asm" >/dev/null
grep -F '.size aia_irq2bitpos, 20' "$asm" >/dev/null
grep -F '.type fixed_values, @object' "$asm" >/dev/null
grep -F '.size fixed_values, 16' "$asm" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-mutable-arrays fixed-zero=2 inferred-int=1 shared-init=1'
'''
)

foundation = Path("tests/compiler/c0/run-foundation-focused.sh")
foundation_text = foundation.read_text()
old = """    run-static-inferred-integer-array.sh \\
    run-prefix-update-expressions.sh \\
"""
new = """    run-static-inferred-integer-array.sh \\
    run-static-mutable-arrays.sh \\
    run-prefix-update-expressions.sh \\
"""
if foundation_text.count(old) != 1:
    raise SystemExit("foundation static-array insertion anchor changed")
foundation.write_text(foundation_text.replace(old, new, 1))
