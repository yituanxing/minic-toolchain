#!/usr/bin/env python3
"""Materialize inferred outer bounds for multidimensional static-local arrays."""
from pathlib import Path

parser_path = Path("src/frontend/parser_statement.c")
text = parser_path.read_text()

old_parse = '''    if (parser == NULL || attributes == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
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
new_parse = '''    if (parser == NULL || attributes == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    /* The omitted outer bound belongs to the source-level array. Parse any
       following fixed dimensions as the complete element type of that outer
       inferred array, so T[][N] reuses the normal declarator/type registry. */
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicType nested_element_type;
        bool is_array;

        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &nested_element_type, &is_array) ||
            !is_array) {
            return false;
        }
        element_type = nested_element_type;
    }
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }
'''

if new_parse not in text:
    if old_parse not in text:
        raise SystemExit("static-local inferred multidimensional parse anchor not found")
    text = text.replace(old_parse, new_parse, 1)

old_kind = '''        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
            !minic_type_is_record(element_type)) {
            minic_parser_error(
                parser,
                "brace-initialized inferred static array requires scalar or record elements");
            return false;
        }
'''
new_kind = '''        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
            !minic_type_is_record(element_type) && !minic_type_is_array(element_type)) {
            minic_parser_error(
                parser,
                "brace-initialized inferred static array requires scalar or aggregate elements");
            return false;
        }
'''
if new_kind not in text:
    if old_kind not in text:
        raise SystemExit("static-local inferred aggregate element anchor not found")
    text = text.replace(old_kind, new_kind, 1)

parser_path.write_text(text)

test_path = Path("tests/compiler/c0/static_local_inferred_array.c")
test = test_path.read_text()
old_test = '''    static const MiniPair pair = {
        __builtin_offsetof(MiniRegs, a0),
        1 + 2,
    };

    return (int)sizeof(nextage) + nextage[0] + nextage[6] +
'''
new_test = '''    static const MiniPair pair = {
        __builtin_offsetof(MiniRegs, a0),
        1 + 2,
    };
    static const unsigned long matrix[][2] = {
        {101UL, 102UL},
        {201UL, 202UL},
    };

    return (int)sizeof(nextage) + nextage[0] + nextage[6] +
'''
if new_test not in test:
    if old_test not in test:
        raise SystemExit("static-local inferred multidimensional test anchor not found")
    test = test.replace(old_test, new_test, 1)
test_path.write_text(test)

run_path = Path("tests/compiler/c0/run-static-local-inferred-arrays.sh")
run = run_path.read_text()
old_run = '''grep -F '  .word 16' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .word 3' "$work/static_local_inferred_array.s" >/dev/null
if grep -F '.globl __minic_static_local_' "$work/static_local_inferred_array.s" >/dev/null; then
'''
new_run = '''grep -F '  .word 16' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .word 3' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .dword 101' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .dword 202' "$work/static_local_inferred_array.s" >/dev/null
grep -E '\\.size __minic_static_local_[^,]+, 32$' "$work/static_local_inferred_array.s" >/dev/null
if grep -F '.globl __minic_static_local_' "$work/static_local_inferred_array.s" >/dev/null; then
'''
if new_run not in run:
    if old_run not in run:
        raise SystemExit("static-local inferred multidimensional gate anchor not found")
    run = run.replace(old_run, new_run, 1)
old_pass = "PASS compiler/c0/static_local_inferred_array element=uchar count=7 shared-ice=offsetof,array,scalar,record arithmetic=1 internal-rodata=1"
new_pass = "PASS compiler/c0/static_local_inferred_array element=uchar count=7 multidim-inferred=2x2 shared-ice=offsetof,array,scalar,record arithmetic=1 internal-rodata=1"
if new_pass not in run:
    if old_pass not in run:
        raise SystemExit("static-local inferred multidimensional pass anchor not found")
    run = run.replace(old_pass, new_pass, 1)
run_path.write_text(run)
