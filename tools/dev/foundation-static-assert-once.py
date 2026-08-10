#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_all(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count == 0:
        raise SystemExit(f"{label}: expected at least one anchor")
    return text.replace(old, new)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/token.h"
text = path.read_text()
text = replace_once(
    text,
    '''    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_KW_BOOL,\n    MINIC_TOKEN_KW_CHAR,\n''',
    '''    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_KW_BOOL,\n    MINIC_TOKEN_KW_STATIC_ASSERT,\n    MINIC_TOKEN_KW_CHAR,\n''',
    "token-static-assert",
)
path.write_text(text)

path = root / "src/frontend/token.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case MINIC_TOKEN_KW_BOOL:\n        return "_Bool";\n    case MINIC_TOKEN_KW_CHAR:\n''',
    '''    case MINIC_TOKEN_KW_BOOL:\n        return "_Bool";\n    case MINIC_TOKEN_KW_STATIC_ASSERT:\n        return "_Static_assert";\n    case MINIC_TOKEN_KW_CHAR:\n''',
    "token-name-static-assert",
)
path.write_text(text)

path = root / "src/frontend/lexer.c"
text = path.read_text()
text = replace_once(
    text,
    '''    if (length == 5U && memcmp(text, "_Bool", 5U) == 0) {\n        return MINIC_TOKEN_KW_BOOL;\n    }\n    if (length == 4U && memcmp(text, "char", 4U) == 0) {\n''',
    '''    if (length == 5U && memcmp(text, "_Bool", 5U) == 0) {\n        return MINIC_TOKEN_KW_BOOL;\n    }\n    if (length == 14U && memcmp(text, "_Static_assert", 14U) == 0) {\n        return MINIC_TOKEN_KW_STATIC_ASSERT;\n    }\n    if (length == 4U && memcmp(text, "char", 4U) == 0) {\n''',
    "lexer-static-assert-keyword",
)
path.write_text(text)

path = root / "src/frontend/parser_expression.c"
text = path.read_text()
text = replace_once(
    text,
    '''static bool builtin_constant_integer_value(const MinicC0Program *program,\n                                           MinicExpressionId expression_id,\n                                           int64_t *value) {\n''',
    '''bool minic_parser_evaluate_integer_constant_expression(const MinicC0Program *program,\n                                                       MinicExpressionId expression_id,\n                                                       int64_t *value) {\n''',
    "publish-ast-constant-evaluator",
)
text = replace_all(
    text,
    "builtin_constant_integer_value(",
    "minic_parser_evaluate_integer_constant_expression(",
    "ast-constant-evaluator-callers",
)
path.write_text(text)

path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    '''bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id);\nbool minic_parser_add_default_return(MinicParser *parser);\n''',
    '''bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id);\nbool minic_parser_evaluate_integer_constant_expression(const MinicC0Program *program,\n                                                       MinicExpressionId expression_id,\n                                                       int64_t *value);\nbool minic_parser_parse_static_assert_declaration(MinicParser *parser);\nbool minic_parser_add_default_return(MinicParser *parser);\n''',
    "parser-internal-static-assert-contract",
)
path.write_text(text)

path = root / "src/frontend/parser_static_assert.c"
path.write_text('''#include "frontend/parser_internal.h"\n\n#include <stdint.h>\n\nbool minic_parser_parse_static_assert_declaration(MinicParser *parser) {\n    const MinicExpression *condition;\n    MinicExpressionId condition_id;\n    int64_t condition_value;\n\n    if (parser == NULL || parser->current.kind != MINIC_TOKEN_KW_STATIC_ASSERT) {\n        if (parser != NULL) {\n            minic_parser_error(parser, "expected _Static_assert declaration");\n        }\n        return false;\n    }\n    if (!minic_parser_advance(parser) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after _Static_assert") ||\n        !minic_parser_parse_expression(parser, &condition_id, 0U)) {\n        return false;\n    }\n    condition = minic_c0_program_expression(parser->program, condition_id);\n    if (condition == NULL || !minic_type_is_integer(condition->type) ||\n        !minic_parser_evaluate_integer_constant_expression(\n            parser->program, condition_id, &condition_value)) {\n        minic_parser_error(parser,\n                           "_Static_assert condition must be an integer constant expression");\n        return false;\n    }\n    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in _Static_assert") ||\n        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "expected string literal in _Static_assert");\n        }\n        return false;\n    }\n    do {\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    } while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL);\n    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after _Static_assert") ||\n        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after _Static_assert")) {\n        return false;\n    }\n    if (condition_value == 0) {\n        minic_parser_error(parser, "static assertion failed");\n        return false;\n    }\n    return true;\n}\n''')

path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    '''\tsrc/frontend/parser_record.c \\\n\tsrc/frontend/parser_statement.c \\\n\tsrc/frontend/parser_string.c \\\n''',
    '''\tsrc/frontend/parser_record.c \\\n\tsrc/frontend/parser_static_assert.c \\\n\tsrc/frontend/parser_statement.c \\\n\tsrc/frontend/parser_string.c \\\n''',
    "makefile-static-assert-source",
)
path.write_text(text)

path = root / "src/frontend/parser_function.c"
text = path.read_text()
text = replace_once(
    text,
    '''        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {\n            success = minic_parser_parse_typedef(&parser);\n''',
    '''        if (parser.current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {\n            success = minic_parser_parse_static_assert_declaration(&parser);\n        } else if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {\n            success = minic_parser_parse_typedef(&parser);\n''',
    "top-level-static-assert-dispatch",
)
path.write_text(text)

path = root / "src/frontend/parser_statement.c"
text = path.read_text()
text = replace_once(
    text,
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n        return parse_compound_statement(parser);\n    }\n    if (parser->current.kind == MINIC_TOKEN_KW_IF) {\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n        return parse_compound_statement(parser);\n    }\n    if (parser->current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {\n        if (!allow_declaration) {\n            minic_parser_error(parser, "_Static_assert requires a declaration scope");\n            return false;\n        }\n        return minic_parser_parse_static_assert_declaration(parser);\n    }\n    if (parser->current.kind == MINIC_TOKEN_KW_IF) {\n''',
    "block-static-assert-dispatch",
)
path.write_text(text)

path = root / "tests/frontend/token_model_test.c"
text = path.read_text()
text = replace_once(
    text,
    '''        expect_name(MINIC_TOKEN_STRING_LITERAL, "string literal") != 0 ||\n        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n''',
    '''        expect_name(MINIC_TOKEN_STRING_LITERAL, "string literal") != 0 ||\n        expect_name(MINIC_TOKEN_KW_STATIC_ASSERT, "_Static_assert") != 0 ||\n        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||\n''',
    "token-model-static-assert",
)
path.write_text(text)

path = root / "tests/frontend/lexer_test.c"
text = path.read_text()
anchor = '''static int test_control_keyword_boundaries(void)\n{\n'''
addition = '''static int test_static_assert_keyword_boundaries(void)\n{\n    static const char source[] = "_Static_assert _Static_assertion";\n    MinicLexer lexer;\n\n    minic_lexer_initialize(&lexer, "static-assert.c", source, sizeof(source) - 1U);\n    if (expect_token(&lexer, MINIC_TOKEN_KW_STATIC_ASSERT, 1U, 1U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 16U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 33U) != 0) {\n        return 1;\n    }\n    return 0;\n}\n\n'''
text = replace_once(text, anchor, addition + anchor, "lexer-static-assert-test")
text = replace_once(
    text,
    '''        test_comparison_operators() != 0 ||\n        test_control_keyword_boundaries() != 0 ||\n''',
    '''        test_comparison_operators() != 0 ||\n        test_static_assert_keyword_boundaries() != 0 ||\n        test_control_keyword_boundaries() != 0 ||\n''',
    "lexer-static-assert-main",
)
path.write_text(text)

path = root / "tests/compiler/c0/static_assert_declaration.c"
path.write_text('''static int generic_probe(int value) {\n    _Static_assert(__builtin_types_compatible_p(typeof(value), int), "block-scope type");\n    return value;\n}\n\n_Static_assert(\n    __builtin_types_compatible_p(typeof(generic_probe), typeof(generic_probe)) &&\n        __builtin_types_compatible_p(typeof(1), int),\n    "top-level " "type");\n\nint main(void) {\n    return generic_probe(7) == 7 ? 0 : 1;\n}\n''')

path = root / "tests/compiler/c0/invalid_static_assert_false.c"
path.write_text('''_Static_assert(0, "must fail");\n\nint main(void) {\n    return 0;\n}\n''')

path = root / "tests/compiler/c0/run-static-assert-declaration.sh"
path.write_text('''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nhost_cc=${HOST_CC:-${CC:-cc}}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-assert-declaration\n\nrm -rf "$work"\nmkdir -p "$work"\n\n"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_assert_declaration.c" \\\n    -o "$work/static_assert_declaration.i"\n"$minic" -S "$work/static_assert_declaration.i" -o "$work/static_assert_declaration.s"\ntest -s "$work/static_assert_declaration.s"\ngrep -F 'generic_probe:' "$work/static_assert_declaration.s" >/dev/null\ngrep -F 'main:' "$work/static_assert_declaration.s" >/dev/null\n\n"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_static_assert_false.c" \\\n    -o "$work/invalid_static_assert_false.i"\nif "$minic" -S "$work/invalid_static_assert_false.i" \\\n    -o "$work/invalid_static_assert_false.s" 2>"$work/invalid_static_assert_false.stderr"; then\n    printf '%s\\n' 'false _Static_assert unexpectedly compiled' >&2\n    exit 1\nfi\ngrep -F 'static assertion failed' "$work/invalid_static_assert_false.stderr" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/static_assert_declaration scope=file+block condition=shared-ast-consteval builtin=types-compatible+typeof message=concatenated false=reject runtime=none'\n''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    '''sh tests/compiler/c0/run-gnu-builtin-constant-p.sh\n''',
    '''sh tests/compiler/c0/run-gnu-builtin-constant-p.sh\nsh tests/compiler/c0/run-static-assert-declaration.sh\n''',
    "focused-static-assert-gate",
)
path.write_text(text)
