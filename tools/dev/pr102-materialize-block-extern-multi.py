#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
statement = root / "src/frontend/parser_statement.c"
text = statement.read_text()

anchor = "static bool parse_block_scope_extern_declaration(MinicParser *parser) {\n"
helper = '''static bool declare_block_scope_extern_object_declarator(MinicParser *parser,\n                                                        MinicType base_type,\n                                                        MinicSourceSpan name_span) {\n    MinicGlobalObjectId current_scope_object;\n    MinicGlobalObjectId object_id;\n    MinicType object_type;\n    bool is_array;\n\n    object_type = base_type;\n    if (!minic_parser_parse_array_declarator_suffix(\n            parser, object_type, true, &object_type, &is_array)) {\n        return false;\n    }\n    (void)is_array;\n    if (!minic_parser_declare_block_scope_extern_object(\n            parser, name_span, object_type, &object_id)) {\n        return false;\n    }\n    current_scope_object =\n        minic_parser_find_scoped_global_object_in_current_scope(parser, name_span);\n    if (minic_parser_name_bound_in_current_scope(parser, name_span)) {\n        if (current_scope_object != object_id) {\n            minic_parser_error(parser,\n                               "block-scope extern object conflicts with local declaration");\n            return false;\n        }\n    } else if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        return false;\n    }\n    return true;\n}\n\nstatic bool parse_block_scope_extern_declaration(MinicParser *parser) {\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"extern declaration anchor count={text.count(anchor)}")
text = text.replace(anchor, helper, 1)

old = '''    if (parser->current.kind != MINIC_TOKEN_LPAREN) {\n        MinicGlobalObjectId current_scope_object;\n        MinicGlobalObjectId object_id;\n        MinicType object_type;\n        bool is_array;\n\n        object_type = return_type;\n        if (!minic_parser_parse_array_declarator_suffix(\n                parser, object_type, true, &object_type, &is_array)) {\n            return false;\n        }\n        (void)is_array;\n        if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {\n            minic_parser_error(parser, "block-scope extern object declaration must end with ';'");\n            return false;\n        }\n        if (!minic_parser_declare_block_scope_extern_object(\n                parser, name_span, object_type, &object_id)) {\n            return false;\n        }\n        current_scope_object =\n            minic_parser_find_scoped_global_object_in_current_scope(parser, name_span);\n        if (minic_parser_name_bound_in_current_scope(parser, name_span)) {\n            if (current_scope_object != object_id) {\n                minic_parser_error(parser,\n                                   "block-scope extern object conflicts with local declaration");\n                return false;\n            }\n        } else if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n            return false;\n        }\n        return minic_parser_advance(parser);\n    }\n'''
new = '''    if (parser->current.kind != MINIC_TOKEN_LPAREN) {\n        for (;;) {\n            if (!declare_block_scope_extern_object_declarator(parser, return_type, name_span)) {\n                return false;\n            }\n            if (parser->current.kind != MINIC_TOKEN_COMMA) {\n                break;\n            }\n            if (!minic_parser_advance(parser) ||\n                !minic_parser_parse_direct_declarator_name(parser, &name_span)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(parser,\n                                       "expected block-scope extern object declarator name");\n                }\n                return false;\n            }\n        }\n        return minic_parser_expect(\n            parser, MINIC_TOKEN_SEMICOLON,\n            "expected ';' after block-scope extern object declaration");\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"single block extern object path count={text.count(old)}")
statement.write_text(text.replace(old, new, 1))

(root / "tests/compiler/c0/block_scope_extern_multi_declarator.c").write_text(r'''extern char __init_begin[4];
extern char __init_end[4];

unsigned long span(void) {
    extern char __init_begin[], __init_end[];
    return (unsigned long)&__init_end - (unsigned long)&__init_begin;
}

int main(void) {
    return span() == 4 ? 0 : 1;
}
''')

(root / "tests/compiler/c0/run-block-scope-extern-multi-declarator.sh").write_text(r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-multi
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_multi_declarator.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F 'la a0, __init_begin' "$work/output.s" >/dev/null
grep -F 'la a0, __init_end' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/block_scope_extern_multi_declarator declarators=2 incomplete-arrays=2 scoped-bindings=2 entity-merge=existing-globals'
''')

run = root / "tests/compiler/c0/run.sh"
run_text = run.read_text()
anchor = '''MINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"\n'''
insert = anchor + '''\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-block-scope-extern-multi-declarator.sh"\n'''
if run_text.count(anchor) != 1:
    raise SystemExit(f"run.sh insertion anchor count={run_text.count(anchor)}")
run.write_text(run_text.replace(anchor, insert, 1))
