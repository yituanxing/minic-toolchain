#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    '''typedef struct MinicParsedFunctionDeclarator {\n    MinicSourceSpan name_span;\n    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n    size_t parameter_count;\n    size_t pointer_depth;\n    bool has_name;\n    bool is_variadic;\n} MinicParsedFunctionDeclarator;\n''',
    '''typedef struct MinicParsedFunctionDeclarator {\n    MinicSourceSpan name_span;\n    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n    size_t parameter_count;\n    size_t pointer_depth;\n    unsigned int pointer_const_qualifiers;\n    unsigned int pointer_volatile_qualifiers;\n    bool has_name;\n    bool is_variadic;\n} MinicParsedFunctionDeclarator;\n''',
    "parsed function pointer qualifier masks",
)

replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_pointer_declarator(MinicParser *parser,\n                                           MinicType base_type,\n                                           MinicType *type);\n''',
    '''bool minic_parser_parse_pointer_qualifier_sequence(MinicParser *parser,\n                                                  size_t pointer_depth,\n                                                  unsigned int *const_qualifiers,\n                                                  unsigned int *volatile_qualifiers);\nbool minic_parser_parse_pointer_declarator(MinicParser *parser,\n                                           MinicType base_type,\n                                           MinicType *type);\n''',
    "shared pointer qualifier helper declaration",
)

replace_once(
    "src/frontend/parser_declarator.c",
    '''#include <string.h>\n''',
    '''#include <limits.h>\n#include <string.h>\n''',
    "pointer qualifier bit capacity include",
)

replace_once(
    "src/frontend/parser_declarator.c",
    '''bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n''',
    '''static bool declarator_identifier_is(const MinicParser *parser, const char *text) {\n    size_t length;\n\n    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n        return false;\n    }\n    length = minic_parser_span_length(parser->current.span);\n    return strlen(text) == length &&\n           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;\n}\n\nbool minic_parser_parse_pointer_qualifier_sequence(MinicParser *parser,\n                                                  size_t pointer_depth,\n                                                  unsigned int *const_qualifiers,\n                                                  unsigned int *volatile_qualifiers) {\n    unsigned int bit;\n\n    if (parser == NULL || const_qualifiers == NULL || volatile_qualifiers == NULL ||\n        pointer_depth == 0U || pointer_depth > sizeof(unsigned int) * CHAR_BIT) {\n        return false;\n    }\n    bit = 1U << (pointer_depth - 1U);\n    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||\n           parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||\n           declarator_identifier_is(parser, "restrict") ||\n           declarator_identifier_is(parser, "__restrict")) {\n        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n            *const_qualifiers |= bit;\n        } else if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n            *volatile_qualifiers |= bit;\n        }\n        /* restrict remains a parse-only aliasing promise until alias-aware optimization exists. */\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n    return true;\n}\n\nbool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n''',
    "shared pointer qualifier parser",
)

replace_once(
    "src/frontend/parser_declarator.c",
    '''    while (parser->current.kind == MINIC_TOKEN_STAR) {\n        declarator->pointer_depth += 1U;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n''',
    '''    while (parser->current.kind == MINIC_TOKEN_STAR) {\n        declarator->pointer_depth += 1U;\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_parse_pointer_qualifier_sequence(\n                parser,\n                declarator->pointer_depth,\n                &declarator->pointer_const_qualifiers,\n                &declarator->pointer_volatile_qualifiers)) {\n            return false;\n        }\n    }\n''',
    "parenthesized function pointer qualifiers",
)

replace_once(
    "src/frontend/parser_declarator.c",
    '''    pointer_depth = declarator->pointer_depth;\n    while (pointer_depth > 0U) {\n        if (!minic_type_pointer_to(function_type, &function_type)) {\n            return false;\n        }\n        pointer_depth -= 1U;\n    }\n''',
    '''    pointer_depth = 0U;\n    while (pointer_depth < declarator->pointer_depth) {\n        unsigned int bit;\n\n        if (!minic_type_pointer_to(function_type, &function_type)) {\n            return false;\n        }\n        bit = 1U << pointer_depth;\n        if ((declarator->pointer_const_qualifiers & bit) != 0U &&\n            !minic_type_add_const(function_type, &function_type)) {\n            return false;\n        }\n        if ((declarator->pointer_volatile_qualifiers & bit) != 0U &&\n            !minic_type_add_volatile(function_type, &function_type)) {\n            return false;\n        }\n        pointer_depth += 1U;\n    }\n''',
    "build qualified function pointer layers",
)

replace_once(
    "src/frontend/parser_type.c",
    '''    parsed_type = base_type;\n    while (parser->current.kind == MINIC_TOKEN_STAR) {\n        if (!minic_type_pointer_to(parsed_type, &parsed_type) || !minic_parser_advance(parser)) {\n            minic_parser_error(parser, "pointer declarator depth is unsupported");\n            return false;\n        }\n        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||\n               parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||\n               minic_parser_identifier_is(parser, "restrict") ||\n               minic_parser_identifier_is(parser, "__restrict")) {\n            if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n                if (!minic_type_add_volatile(parsed_type, &parsed_type)) {\n                    minic_parser_error(parser, "cannot apply pointer volatile qualifier");\n                    return false;\n                }\n            } else if (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n                if (!minic_type_add_const(parsed_type, &parsed_type)) {\n                    minic_parser_error(parser, "cannot apply pointer const qualifier");\n                    return false;\n                }\n            }\n            /* restrict is an aliasing promise, not an ABI/layout qualifier. MiniC does\n               not yet perform restrict-based alias optimization, so accepting it here\n               preserves observable semantics while keeping the target type unchanged. */\n            if (!minic_parser_advance(parser)) {\n                return false;\n            }\n        }\n    }\n''',
    '''    parsed_type = base_type;\n    while (parser->current.kind == MINIC_TOKEN_STAR) {\n        if (!minic_type_pointer_to(parsed_type, &parsed_type) || !minic_parser_advance(parser)) {\n            minic_parser_error(parser, "pointer declarator depth is unsupported");\n            return false;\n        }\n        if (!minic_parser_parse_pointer_qualifier_sequence(parser,\n                                                           parsed_type.pointer_depth,\n                                                           &parsed_type.pointer_qualifiers,\n                                                           &parsed_type.pointer_volatile_qualifiers)) {\n            minic_parser_error(parser, "cannot parse pointer qualifiers");\n            return false;\n        }\n    }\n''',
    "converge ordinary pointer qualifier parsing",
)

Path("tests/compiler/c0/function_pointer_qualifiers.c").write_text(
    '''typedef int callback_t(int value);\n\nstruct Ops {\n    int (* const filter)(int value);\n    int (* volatile hook)(int value);\n};\n\ntypedef int (* const const_callback_t)(int value);\n\nint call_filter(struct Ops *ops, int value)\n{\n    return ops->filter(value);\n}\n\nint call_hook(struct Ops *ops, int value)\n{\n    return ops->hook(value);\n}\n\nint call_typedef(const_callback_t callback, int value)\n{\n    return callback(value);\n}\n'''
)

Path("tests/compiler/c0/run-function-pointer-qualifiers.sh").write_text(
    '''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nhost_cc=${HOST_CC:-${CC:-cc}}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-pointer-qualifiers\n\nrm -rf "$work"\nmkdir -p "$work"\n"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/function_pointer_qualifiers.c" -o "$work/input.i"\n"$minic" -S "$work/input.i" -o "$work/output.s"\ntest -s "$work/output.s"\ngrep -F 'call_filter:' "$work/output.s" >/dev/null\ngrep -F 'call_hook:' "$work/output.s" >/dev/null\ngrep -F 'call_typedef:' "$work/output.s" >/dev/null\ngrep -F 'jalr' "$work/output.s" >/dev/null\n\ncat >"$work/const-field-assign.c" <<'EOF'\ntypedef int callback_t(int);\nstruct Ops { int (* const filter)(int); };\nvoid bad(struct Ops *ops, callback_t *callback)\n{\n    ops->filter = callback;\n}\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/const-field-assign.c" -o "$work/const-field-assign.i"\nif "$minic" -S "$work/const-field-assign.i" -o "$work/const-field-assign.s" 2>"$work/const-field-assign.stderr"; then\n    printf '%s\\n' 'const-qualified function pointer field unexpectedly modifiable' >&2\n    exit 1\nfi\ntest -s "$work/const-field-assign.stderr"\n\nprintf '%s\\n' 'PASS compiler/c0/function_pointer_qualifiers record-field=const+volatile typedef=const indirect-call=1 const-field=nonmodifiable shared-pointer-qualifier-parser=1'\n'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-function-parameter-adjustment.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-function-pointer-qualifiers.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
