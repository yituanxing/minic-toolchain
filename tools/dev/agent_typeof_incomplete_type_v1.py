#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, content):
    (ROOT / path).write_text(content)


def one(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return content.replace(old, new, 1)


p = "src/frontend/parser_internal.h"
s = read(p)
s = one(
    s,
    '''bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type);\n''',
    '''bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type);\nbool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type);\n''',
    "type-name seam prototype",
)
write(p, s)

p = "src/frontend/parser_type.c"
s = read(p)
s = one(
    s,
    '''        if (minic_parser_token_starts_type_name(parser, parser->current)) {\n            if (!minic_parser_parse_type_name(parser, &parsed_type)) {\n                return false;\n            }\n''',
    '''        if (minic_parser_token_starts_type_name(parser, parser->current)) {\n            if (!minic_parser_parse_type_name_preserving_incomplete(parser, &parsed_type)) {\n                return false;\n            }\n''',
    "typeof preserves incomplete",
)
s = one(
    s,
    '''bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {\n    MinicType base_type;\n\n    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||\n        !minic_parser_parse_pointer_declarator(parser, base_type, type)) {\n        return false;\n    }\n    return minic_parser_require_complete_object_type(\n        parser, *type, "incomplete record type requires pointer declarator");\n}\n''',
    '''bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {\n    MinicType base_type;\n\n    return parser != NULL && type != NULL &&\n           minic_parser_parse_type_specifiers(parser, &base_type) &&\n           minic_parser_parse_pointer_declarator(parser, base_type, type);\n}\n\nbool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {\n    if (!minic_parser_parse_type_name_preserving_incomplete(parser, type)) {\n        return false;\n    }\n    return minic_parser_require_complete_object_type(\n        parser, *type, "incomplete record type requires pointer declarator");\n}\n''',
    "shared type-name seam",
)
write(p, s)

p = "src/frontend/parser_function.c"
s = read(p)
s = one(
    s,
    '''static bool parse_function_signature_type_name(MinicParser *parser, MinicType *type) {\n    MinicType base_type;\n\n    /* A function declaration may preserve an incomplete record/enum by value in\n     * its signature. Completeness becomes mandatory only when a definition\n     * materializes the return/parameter ABI or when ordinary object storage is\n     * created. Keep this parser about signature identity, not storage. */\n    return parser != NULL && type != NULL &&\n           minic_parser_parse_type_specifiers(parser, &base_type) &&\n           minic_parser_parse_pointer_declarator(parser, base_type, type);\n}\n\n''',
    '',
    "remove private signature type parser",
)
s = one(
    s,
    '''        if (!parse_function_signature_type_name(parser, &parameter_type)) {\n''',
    '''        if (!minic_parser_parse_type_name_preserving_incomplete(parser, &parameter_type)) {\n''',
    "signature uses shared seam",
)
write(p, s)

p = "tests/compiler/c0/typeof_generic.c"
s = read(p)
s = one(
    s,
    '''extern int generic_side_effect(void);\n\n''',
    '''extern int generic_side_effect(void);\n\nstruct TypeofPending;\nextern __attribute__((section(".probe.typeof.incomplete")))\n    __typeof__(struct TypeofPending) typeof_pending_object;\n\nvoid *typeof_incomplete_object_address(void) {\n    return &typeof_pending_object;\n}\n\n''',
    "typeof incomplete fixture",
)
write(p, s)

p = "tests/compiler/c0/run-typeof-generic.sh"
s = read(p)
s = one(
    s,
    '''              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size; do\n''',
    '''              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size \\\n              typeof_incomplete_object_address; do\n''',
    "typeof symbol list",
)
s = one(
    s,
    '''# RV64 unsigned long and pointers are both eight bytes.\nsize8=$(grep -c '  li a0, 8' "$assembly" || true)\ntest "$size8" -ge 3\n\nprintf '%s\\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name generic=typed,default controlling=unevaluated linux-shape=1'\n''',
    '''# RV64 unsigned long and pointers are both eight bytes.\nsize8=$(grep -c '  li a0, 8' "$assembly" || true)\ntest "$size8" -ge 3\ngrep -F 'typeof_pending_object' "$assembly" >/dev/null\n\ncat >"$work/incomplete-sizeof.c" <<'EOF'\nstruct StillPending;\nunsigned long bad_size(void) { return sizeof(__typeof__(struct StillPending)); }\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-sizeof.c" -o "$work/incomplete-sizeof.i"\nif "$minic" -S "$work/incomplete-sizeof.i" -o "$work/incomplete-sizeof.s" \\\n    2>"$work/incomplete-sizeof.stderr"; then\n    printf '%s\\n' 'FAIL compiler/c0/typeof_generic: sizeof incomplete typeof accepted' >&2\n    exit 1\nfi\ngrep -F 'incomplete record type requires pointer declarator' "$work/incomplete-sizeof.stderr" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name,incomplete-type-preserved generic=typed,default controlling=unevaluated linux-shape=1 completeness=consumer-owned'\n''',
    "typeof runner",
)
write(p, s)
