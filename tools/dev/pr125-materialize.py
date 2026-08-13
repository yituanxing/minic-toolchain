from pathlib import Path

parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
old = '''    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (has_section || has_visibility || object_explicit_alignment != 0U) {
            minic_parser_error(
                parser, "static object symbol/layout attributes require explicit object semantics");
            return false;
        }
        return minic_parser_parse_static_global_after_head(parser, return_type, name_span);
    }
'''
new = '''    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
        MinicGlobalObjectId object_id;

        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (has_visibility || object_explicit_alignment != 0U) {
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
                                parser->program,
                                object_id,
                                section_name,
                                section_name_length))) {
            minic_parser_error(parser, "cannot persist static object section metadata");
            return false;
        }
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"static object route anchor mismatch: {text.count(old)}")
parser.write_text(text.replace(old, new, 1))

Path("tests/compiler/c0/static_global_object_section.c").write_text(r'''static int __attribute__((__section__(".data.static.init"))) section_initialized = 7;
static int __attribute__((section(".data.static.zero"))) section_zero;
static void *__attribute__((__used__)) __attribute__((__section__(".discard.addressable")))
    addressable_shape = (void *)0;

int read_static_global_sections(void) {
    return section_initialized + section_zero + (addressable_shape == (void *)0);
}
''')

Path("tests/compiler/c0/invalid_static_global_alignment.c").write_text(
    'static int __attribute__((aligned(16))) invalid_static_alignment = 1;\n'
)

Path("tests/compiler/c0/run-static-global-object-section.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-global-object-section

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_global_object_section.c" \
    -o "$work/static_global_object_section.s"
test -s "$work/static_global_object_section.s"
grep -F '.section .data.static.init' "$work/static_global_object_section.s" >/dev/null
grep -F 'section_initialized:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.zero' "$work/static_global_object_section.s" >/dev/null
grep -F 'section_zero:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .discard.addressable' "$work/static_global_object_section.s" >/dev/null
grep -F 'addressable_shape:' "$work/static_global_object_section.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_global_alignment.c" \
    -o "$work/invalid-alignment.s" >"$work/invalid-alignment.stdout" 2>"$work/invalid-alignment.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-global-object-section: static alignment widened accidentally' >&2
    exit 1
fi
grep -F 'static object symbol/layout attributes require explicit object semantics' \
    "$work/invalid-alignment.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-global-object-section section=global-metadata initialized+zero+used-composition alignment=fail-closed'
''')

gate = Path(".github/scripts/compiler-c0-full-gate.sh")
text = gate.read_text()
old = '''external_tentative_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-external-tentative" \\
        sh tests/compiler/c0/run-external-tentative-definitions.sh
}

external_cjson_frontier() {
'''
new = '''external_tentative_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-external-tentative" \\
        sh tests/compiler/c0/run-external-tentative-definitions.sh
}

static_global_section_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-global-section" \\
        sh tests/compiler/c0/run-static-global-object-section.sh
}

external_cjson_frontier() {
'''
if text.count(old) != 1:
    raise SystemExit(f"gate function anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''start_gate external-tentative-focused external_tentative_focused
start_gate wide-string-focused wide_string_focused
'''
new = '''start_gate external-tentative-focused external_tentative_focused
start_gate static-global-section-focused static_global_section_focused
start_gate wide-string-focused wide_string_focused
'''
if text.count(old) != 1:
    raise SystemExit(f"gate start anchor mismatch: {text.count(old)}")
gate.write_text(text.replace(old, new, 1))
