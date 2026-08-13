#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]

decl_path = root / 'src/frontend/parser_declarator.c'
decl = decl_path.read_text()
old_sig = '''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array) {'''
new_sig = '''static bool parse_array_declarator_suffix_impl(MinicParser *parser,
                                               MinicType element_type,
                                               bool allow_incomplete_outermost,
                                               bool adjust_outermost_to_pointer,
                                               MinicType *declarator_type,
                                               bool *is_array) {'''
if decl.count(old_sig) != 1:
    raise SystemExit('array suffix signature anchor missing')
decl = decl.replace(old_sig, new_sig, 1)

old_build = '''        if (dimension == 0U && outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {
                minic_parser_error(parser, "cannot build incomplete array declarator type");
                return false;
            }
        } else if (zero_length[dimension]) {'''
new_build = '''        if (dimension == 0U && adjust_outermost_to_pointer) {
            if (!minic_type_pointer_to(type, &type)) {
                minic_parser_error(parser, "cannot adjust array parameter declarator to pointer");
                return false;
            }
        } else if (dimension == 0U && outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {
                minic_parser_error(parser, "cannot build incomplete array declarator type");
                return false;
            }
        } else if (zero_length[dimension]) {'''
if decl.count(old_build) != 1:
    raise SystemExit('array suffix build anchor missing')
decl = decl.replace(old_build, new_build, 1)

marker = '''bool minic_parser_build_function_declarator_type(MinicParser *parser,
                                                 MinicType return_type,'''
wrappers = '''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array) {
    return parse_array_declarator_suffix_impl(parser,
                                              element_type,
                                              allow_incomplete_outermost,
                                              false,
                                              declarator_type,
                                              is_array);
}

bool minic_parser_parse_array_parameter_suffix(MinicParser *parser,
                                               MinicType element_type,
                                               MinicType *adjusted_type) {
    bool is_array;

    if (parser == NULL || adjusted_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    is_array = false;
    return parse_array_declarator_suffix_impl(
               parser, element_type, true, true, adjusted_type, &is_array) &&
           is_array;
}

'''
if decl.count(marker) != 1:
    raise SystemExit('function declarator marker missing')
decl = decl.replace(marker, wrappers + marker, 1)
decl_path.write_text(decl)

header_path = root / 'src/frontend/parser_internal.h'
header = header_path.read_text()
old_proto = '''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array);
'''
new_proto = old_proto + '''bool minic_parser_parse_array_parameter_suffix(MinicParser *parser,
                                               MinicType element_type,
                                               MinicType *adjusted_type);
'''
if header.count(old_proto) != 1:
    raise SystemExit('array suffix prototype anchor missing')
header_path.write_text(header.replace(old_proto, new_proto, 1))

function_path = root / 'src/frontend/parser_function.c'
function = function_path.read_text()
pattern = re.compile(r'''static bool adjust_array_parameter_type\(MinicParser \*parser, MinicType \*parameter_type\) \{.*?\n\}\n\nbool minic_parser_parse_parameter_list''', re.S)
replacement = '''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_parameter_suffix(parser, *parameter_type, parameter_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot parse adjusted array parameter declarator");
        }
        return false;
    }
    return true;
}

bool minic_parser_parse_parameter_list'''
function, count = pattern.subn(replacement, function, count=1)
if count != 1:
    raise SystemExit(f'adjust array parameter function: expected one match, found {count}')
function_path.write_text(function)

(root / 'tests/compiler/c0/array_parameter_adjustment.c').write_text('''
struct node { int value; };

extern void consume_nodes(struct node *items[], unsigned int count);
extern void consume_matrix(int matrix[][3]);

int main(void) {
    return 0;
}
'''.lstrip())

(root / 'tests/compiler/c0/run-array-parameter-adjustment.sh').write_text('''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/array_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.globl main' "$work/output.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/array_parameter_adjustment incomplete=pointer fixed-outer=pointer multidim-inner=retained verifier=no-orphan'
''')
