#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}: {pattern[:120]!r}")
    p.write_text(text)


# Promote the existing correct extern-array compatibility/composite owner out of
# parser_global.c's file-local scope so tentative definitions and full definitions
# use exactly the same declaration semantics instead of re-deriving array rules.
p = Path("src/frontend/parser_global.c")
text = p.read_text()
if "static bool extern_object_types_compatible(" not in text:
    raise SystemExit("parser_global.c: extern compatibility owner not found")
if "static bool merge_extern_array_composite_type(" not in text:
    raise SystemExit("parser_global.c: extern composite owner not found")
text = text.replace(
    "static bool extern_object_types_compatible(",
    "bool minic_parser_external_object_types_compatible(",
    1,
)
text = text.replace(
    "extern_object_types_compatible(",
    "minic_parser_external_object_types_compatible(",
)
text = text.replace(
    "static bool merge_extern_array_composite_type(",
    "bool minic_parser_merge_external_array_composite_type(",
    1,
)
text = text.replace(
    "merge_extern_array_composite_type(",
    "minic_parser_merge_external_array_composite_type(",
)
p.write_text(text)

anchor = """MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,\n                                                           MinicSourceSpan name_span);\n"""
addition = anchor + """bool minic_parser_external_object_types_compatible(const MinicC0Program *program,\n                                                   MinicType existing_type,\n                                                   MinicType declared_type);\nbool minic_parser_merge_external_array_composite_type(MinicC0Program *program,\n                                                       MinicType existing_type,\n                                                       MinicType declared_type);\n"""
replace_once("src/frontend/parser_internal.h", anchor, addition)

# Tentative definitions: compatible incomplete arrays must be completed in the
# existing canonical entity before changing storage state.
old_tentative = """        existing = minic_c0_program_global_object(parser->program, object_id);\n        if (existing == NULL ||\n            !minic_c0_types_compatible(parser->program, existing->type, object_type) ||\n            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {\n            minic_parser_error(parser, \"conflicting external tentative definition\");\n            return false;\n        }\n"""
new_tentative = """        existing = minic_c0_program_global_object(parser->program, object_id);\n        if (existing == NULL ||\n            !minic_parser_external_object_types_compatible(\n                parser->program, existing->type, object_type) ||\n            (minic_type_is_array(existing->type) &&\n             !minic_parser_merge_external_array_composite_type(\n                 parser->program, existing->type, object_type)) ||\n            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {\n            minic_parser_error(parser, \"conflicting external tentative definition\");\n            return false;\n        }\n"""
replace_once("src/frontend/parser_function.c", old_tentative, new_tentative)

old_definition = """        existing = minic_c0_program_global_object(parser->program, object_id);\n        if (existing == NULL ||\n            !minic_c0_types_compatible(parser->program, existing->type, object_type) ||\n            !minic_c0_global_object_begin_definition(parser->program, object_id)) {\n            minic_parser_error(parser, \"conflicting external object definition\");\n            return false;\n        }\n"""
new_definition = """        existing = minic_c0_program_global_object(parser->program, object_id);\n        if (existing == NULL ||\n            !minic_parser_external_object_types_compatible(\n                parser->program, existing->type, object_type) ||\n            (minic_type_is_array(existing->type) &&\n             !minic_parser_merge_external_array_composite_type(\n                 parser->program, existing->type, object_type)) ||\n            !minic_c0_global_object_begin_definition(parser->program, object_id)) {\n            minic_parser_error(parser, \"conflicting external object definition\");\n            return false;\n        }\n"""
replace_once("src/frontend/parser_function.c", old_definition, new_definition)

# Extend the existing tentative-definition regression with the exact Linux-style
# incomplete->complete array shapes that exposed the ownership split.
replace_once(
    "tests/compiler/c0/external_tentative_definitions.c",
    'char __attribute__((__section__(".init.data"))) boot_command_line[1024];\n',
    'extern char __attribute__((__section__(".init.data"))) boot_command_line[];\n'
    'char __attribute__((__section__(".init.data"))) boot_command_line[1024];\n',
)

p = Path("tests/compiler/c0/external_tentative_definitions.c")
text = p.read_text()
append = """

extern unsigned long composite_page_table[];
unsigned long composite_page_table[4]
    __attribute__((__section__(".bss..page_aligned"))) __attribute__((__aligned__(4096)));

extern void *const composite_call_table[];
void *const composite_call_table[4] = {0, 0, 0, 0};
"""
if "composite_page_table" in text or "composite_call_table" in text:
    raise SystemExit("external_tentative_definitions.c: composite regression already present")
p.write_text(text.rstrip() + append + "\n")

Path("tests/compiler/c0/invalid_external_array_composite_bound.c").write_text(
    "extern int conflicting_array_bound[2];\n"
    "int conflicting_array_bound[3];\n"
)

runner = Path("tests/compiler/c0/run-external-tentative-definitions.sh")
text = runner.read_text()
replace_anchor = """grep -F '.word 9' \"$assembly\" >/dev/null\n\n"""
replace_value = replace_anchor + """grep -F 'composite_page_table:' \"$assembly\" >/dev/null\ngrep -F '.size composite_page_table, 32' \"$assembly\" >/dev/null\ngrep -F 'composite_call_table:' \"$assembly\" >/dev/null\ngrep -F '.size composite_call_table, 32' \"$assembly\" >/dev/null\n\n"""
if text.count(replace_anchor) != 1:
    raise SystemExit("run-external-tentative-definitions.sh: assembly anchor not found uniquely")
text = text.replace(replace_anchor, replace_value, 1)
negative_anchor = """grep -F 'conflicting external tentative definition' \"$work/invalid-redecl.stderr\" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/external_tentative_definitions state=extern|tentative|defined zero=end-of-tu fixed-array=1 attrs=section suffix=1 incomplete-array=fail-closed'\n"""
negative_value = """grep -F 'conflicting external tentative definition' \"$work/invalid-redecl.stderr\" >/dev/null\n\nif \"$minic\" -S \"$root/tests/compiler/c0/invalid_external_array_composite_bound.c\" \\\n    -o \"$work/invalid-array-bound.s\" 2>\"$work/invalid-array-bound.stderr\"; then\n    printf '%s\\n' 'conflicting complete array bounds unexpectedly accepted' >&2\n    exit 1\nfi\ngrep -F 'conflicting external tentative definition' \\\n    \"$work/invalid-array-bound.stderr\" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/external_tentative_definitions state=extern|tentative|defined array-composite=incomplete-to-complete zero=end-of-tu fixed-array=1 attrs=section suffix=1 incomplete-array=fail-closed'\n"""
if text.count(negative_anchor) != 1:
    raise SystemExit("run-external-tentative-definitions.sh: negative anchor not found uniquely")
runner.write_text(text.replace(negative_anchor, negative_value, 1))

print("staged shared external array composite declaration ownership")
