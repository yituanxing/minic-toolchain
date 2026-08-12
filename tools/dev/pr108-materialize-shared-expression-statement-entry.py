#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_string_text(MinicParser *parser,\n                                    char **text,\n                                    size_t *length,\n                                    MinicSourceSpan *span);\nbool minic_parser_parse_expression(MinicParser *parser,\n""",
    """bool minic_parser_parse_string_text(MinicParser *parser,\n                                    char **text,\n                                    size_t *length,\n                                    MinicSourceSpan *span);\nbool minic_parser_token_starts_expression(MinicTokenKind kind);\nbool minic_parser_parse_expression(MinicParser *parser,\n""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """static bool\nparse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array);\n\nstatic bool finish_value_expression(MinicParser *parser,\n""",
    """static bool\nparse_comma_expression(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array);\n\nbool minic_parser_token_starts_expression(MinicTokenKind kind) {\n    switch (kind) {\n    case MINIC_TOKEN_IDENTIFIER:\n    case MINIC_TOKEN_INTEGER_CONSTANT:\n    case MINIC_TOKEN_CHARACTER_CONSTANT:\n    case MINIC_TOKEN_FLOATING_CONSTANT:\n    case MINIC_TOKEN_STRING_LITERAL:\n    case MINIC_TOKEN_LPAREN:\n    case MINIC_TOKEN_KW_SIZEOF:\n    case MINIC_TOKEN_KW_ALIGNOF:\n    case MINIC_TOKEN_PLUS:\n    case MINIC_TOKEN_MINUS:\n    case MINIC_TOKEN_BANG:\n    case MINIC_TOKEN_TILDE:\n    case MINIC_TOKEN_AMPERSAND:\n    case MINIC_TOKEN_AMPERSAND_AMPERSAND:\n    case MINIC_TOKEN_STAR:\n    case MINIC_TOKEN_PLUS_PLUS:\n    case MINIC_TOKEN_MINUS_MINUS:\n        return true;\n    default:\n        return false;\n    }\n}\n\nstatic bool finish_value_expression(MinicParser *parser,\n""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """static bool token_starts_expression(MinicTokenKind kind) {\n    return kind == MINIC_TOKEN_IDENTIFIER || kind == MINIC_TOKEN_INTEGER_CONSTANT ||\n           kind == MINIC_TOKEN_LPAREN || kind == MINIC_TOKEN_PLUS || kind == MINIC_TOKEN_MINUS ||\n           kind == MINIC_TOKEN_BANG || kind == MINIC_TOKEN_AMPERSAND ||\n           kind == MINIC_TOKEN_AMPERSAND_AMPERSAND || kind == MINIC_TOKEN_STAR;\n}\n\n""",
    "",
)
replace_once(
    "src/frontend/parser_statement.c",
    """    if (token_starts_expression(parser->current.kind)) {\n        return parse_expression_or_assignment_statement(parser, true);\n    }\n""",
    """    if (minic_parser_token_starts_expression(parser->current.kind)) {\n        return parse_expression_or_assignment_statement(parser, true);\n    }\n""",
)

(ROOT / "tests/compiler/c0/expression_statement_entry.c").write_text(
    """struct counters {\n    int contexts;\n};\n\nint drive_expression_entries(struct counters *ctx, int *value) {\n    ++ctx->contexts;\n    --*value;\n    ~ctx->contexts;\n    'x';\n    1.5;\n    \"entry\";\n    sizeof(ctx->contexts);\n    _Alignof(int);\n    return ctx->contexts + *value;\n}\n"""
)

(ROOT / "tests/compiler/c0/run-expression-statement-entry.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/expression-statement-entry"}
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/expression_statement_entry.c" \
    -o "$work/expression_statement_entry.i"
"$minic" -S "$work/expression_statement_entry.i" -o "$work/expression_statement_entry.s"

grep -F "  addi t0, t0, 1" "$work/expression_statement_entry.s" >/dev/null
grep -F "  addi t0, t0, -1" "$work/expression_statement_entry.s" >/dev/null
grep -F "  not a0, a0" "$work/expression_statement_entry.s" >/dev/null

printf '%s\n' "PASS compiler/c0/expression_statement_entry owner=expression-parser prefix=++,-- unary=~ literal=char,float,string query=sizeof,alignof linux-member-prefix=1"
'''
)

run = ROOT / "tests/compiler/c0/run.sh"
text = run.read_text()
line = 'MINIC="$minic" BUILD_DIR="$work/expression-statement-entry" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-expression-statement-entry.sh"\n'
if line not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + line
    run.write_text(text)
