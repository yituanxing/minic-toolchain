from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"anchor mismatch {path}: {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


# 1. Preserve preprocessor directives as explicit semantic tokens after GCC line-marker skipping.
replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_KW_BOOL,",
    "    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_PREPROCESSOR_DIRECTIVE,\n    MINIC_TOKEN_KW_BOOL,",
)
replace_once(
    "src/frontend/token.c",
    '    case MINIC_TOKEN_STRING_LITERAL:\n        return "string literal";\n    case MINIC_TOKEN_KW_BOOL:',
    '    case MINIC_TOKEN_STRING_LITERAL:\n        return "string literal";\n    case MINIC_TOKEN_PREPROCESSOR_DIRECTIVE:\n        return "preprocessor directive";\n    case MINIC_TOKEN_KW_BOOL:',
)

replace_once(
    "src/frontend/lexer.c",
    "static bool minic_lexer_skip_gcc_line_marker(MinicLexer *lexer) {",
    r'''static bool minic_lexer_at_directive_start(const MinicLexer *lexer) {
    size_t cursor;

    if (lexer == NULL || minic_lexer_peek(lexer) != '#') {
        return false;
    }
    cursor = lexer->cursor;
    while (cursor > 0U && lexer->source[cursor - 1U] != '\n') {
        char previous = lexer->source[cursor - 1U];

        if (previous != ' ' && previous != '\t') {
            return false;
        }
        cursor -= 1U;
    }
    return true;
}

static bool minic_lexer_skip_gcc_line_marker(MinicLexer *lexer) {''',
)
replace_once(
    "src/frontend/lexer.c",
    "    if (lexer == NULL || minic_lexer_peek(lexer) != '#' || lexer->column != 1U) {\n        return false;\n    }",
    "    if (lexer == NULL || !minic_lexer_at_directive_start(lexer)) {\n        return false;\n    }",
)
replace_once(
    "src/frontend/lexer.c",
    "    if (character == '\\0') {\n        token->kind = MINIC_TOKEN_EOF;\n        return true;\n    }\n\n    if (minic_is_identifier_start(character)) {",
    r'''    if (character == '\0') {
        token->kind = MINIC_TOKEN_EOF;
        return true;
    }

    if (character == '#' && minic_lexer_at_directive_start(lexer)) {
        while (minic_lexer_peek(lexer) != '\0' && minic_lexer_peek(lexer) != '\n') {
            minic_lexer_advance(lexer);
        }
        if (minic_lexer_peek(lexer) == '\n') {
            minic_lexer_advance(lexer);
        }
        token->kind = MINIC_TOKEN_PREPROCESSOR_DIRECTIVE;
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    if (minic_is_identifier_start(character)) {''',
)

# 2. Translation-unit pragma state lives on the parser, not in DataLayout.
replace_once(
    "src/frontend/parser_internal.h",
    "    size_t switch_depth;\n    MinicParserSwitchContext switch_contexts[MINIC_PARSER_MAX_SWITCH_DEPTH];",
    "    size_t switch_depth;\n    size_t record_pack_alignment;\n    MinicParserSwitchContext switch_contexts[MINIC_PARSER_MAX_SWITCH_DEPTH];",
)

# 3. Interpret only the proven pragma-pack v0 at top level; all other directives fail closed.
anchor = "static bool top_level_is_gnu_extension_marker(const MinicParser *parser) {"
pragma_parser = r'''static void pragma_skip_horizontal_space(const char *text, size_t length, size_t *cursor) {
    while (*cursor < length && (text[*cursor] == ' ' || text[*cursor] == '\t')) {
        *cursor += 1U;
    }
}

static bool pragma_consume_word(const char *text,
                                size_t length,
                                size_t *cursor,
                                const char *word) {
    size_t word_length;

    word_length = strlen(word);
    if (*cursor > length || word_length > length - *cursor ||
        memcmp(text + *cursor, word, word_length) != 0) {
        return false;
    }
    *cursor += word_length;
    return true;
}

static bool pragma_only_trailing_space(const char *text, size_t length, size_t cursor) {
    while (cursor < length) {
        char character = text[cursor++];

        if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
            return false;
        }
    }
    return true;
}

static bool parse_top_level_preprocessor_directive(MinicParser *parser) {
    const char *text;
    size_t length;
    size_t cursor;
    size_t alignment;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
        return false;
    }
    text = parser->source + parser->current.span.begin.offset;
    length = minic_parser_span_length(parser->current.span);
    cursor = 0U;
    if (cursor >= length || text[cursor] != '#') {
        minic_parser_error(parser, "invalid preprocessor directive token");
        return false;
    }
    cursor += 1U;
    pragma_skip_horizontal_space(text, length, &cursor);
    if (!pragma_consume_word(text, length, &cursor, "pragma")) {
        minic_parser_error(parser, "unsupported preprocessor directive");
        return false;
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (!pragma_consume_word(text, length, &cursor, "pack")) {
        minic_parser_error(parser, "unsupported pragma directive");
        return false;
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (cursor >= length || text[cursor] != '(') {
        minic_parser_error(parser, "expected '(' after pragma pack");
        return false;
    }
    cursor += 1U;
    pragma_skip_horizontal_space(text, length, &cursor);
    alignment = 0U;
    if (cursor < length && text[cursor] == '1') {
        alignment = 1U;
        cursor += 1U;
    } else if (cursor < length && text[cursor] != ')') {
        minic_parser_error(parser, "unsupported pragma pack alignment");
        return false;
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (cursor >= length || text[cursor] != ')') {
        minic_parser_error(parser, "expected ')' after pragma pack alignment");
        return false;
    }
    cursor += 1U;
    if (!pragma_only_trailing_space(text, length, cursor)) {
        minic_parser_error(parser, "unsupported pragma pack syntax");
        return false;
    }
    parser->record_pack_alignment = alignment;
    return minic_parser_advance(parser);
}

'''
replace_once("src/frontend/parser_function.c", anchor, pragma_parser + anchor)
replace_once(
    "src/frontend/parser_function.c",
    "        if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {\n            success = minic_parser_advance(&parser);",
    "        if (parser.current.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {\n            success = parse_top_level_preprocessor_directive(&parser);\n        } else if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {\n            success = minic_parser_advance(&parser);",
)

# 4. Project active pack(1) into existing record packed metadata at definition time.
replace_once(
    "src/frontend/parser_record.c",
    "    explicit_alignment = 0U;\n    is_packed = false;",
    "    explicit_alignment = 0U;\n    is_packed = parser->record_pack_alignment == 1U;",
)

# 5. Focused raw-preprocessed tests: pack state, reset, forward identity, existing packed attr,
#    unsupported pack variants, and arbitrary directives remain fail-closed.
Path("tests/compiler/c0/pragma_pack_record_layout.i").write_text(r'''# 1 "pragma-pack.c"
#pragma pack(1)
struct packed_one {
    char lead;
    int value;
};
struct packed_two {
    char lead;
    unsigned long value;
};
#pragma pack()
struct natural_one {
    char lead;
    int value;
};
struct forward_record;
#pragma pack(1)
struct forward_record {
    char lead;
    int value;
};
#pragma pack()
struct attribute_packed {
    char lead;
    int value;
} __attribute__((packed));
_Static_assert(sizeof(struct packed_one) == 5, "pack(1) size");
_Static_assert(__builtin_offsetof(struct packed_one, value) == 1, "pack(1) offset");
_Static_assert(sizeof(struct packed_two) == 9, "pack state spans definitions");
_Static_assert(__builtin_offsetof(struct packed_two, value) == 1, "pack state offset");
_Static_assert(sizeof(struct natural_one) == 8, "pack reset size");
_Static_assert(__builtin_offsetof(struct natural_one, value) == 4, "pack reset offset");
_Static_assert(sizeof(struct forward_record) == 5, "definition-time pack state");
_Static_assert(__builtin_offsetof(struct forward_record, value) == 1, "forward identity");
_Static_assert(sizeof(struct attribute_packed) == 5, "GNU packed remains independent");
int main(void) { return 0; }
''')
Path("tests/compiler/c0/invalid_pragma_pack_alignment.i").write_text(
    '#pragma pack(2)\nstruct bad { char c; int value; };\n'
)
Path("tests/compiler/c0/invalid_unknown_pragma.i").write_text(
    '#pragma once\nint value;\n'
)
Path("tests/compiler/c0/invalid_preprocessor_directive.i").write_text(
    '#define HIDDEN 1\nint value;\n'
)
Path("tests/compiler/c0/run-pragma-pack-record-layout.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pragma-pack

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/pragma_pack_record_layout.i" -o "$work/pragma_pack.s"
test -s "$work/pragma_pack.s"
grep -F '.globl main' "$work/pragma_pack.s" >/dev/null

expect_failure() {
    name=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null || {
        cat "$work/$name.stderr" >&2
        exit 1
    }
}

expect_failure invalid_pragma_pack_alignment 'unsupported pragma pack alignment'
expect_failure invalid_unknown_pragma 'unsupported pragma directive'
expect_failure invalid_preprocessor_directive 'unsupported preprocessor directive'

printf '%s\n' 'PASS compiler/c0/pragma_pack_record_layout state=TU pack=1 reset=1 record-layout=DataLayout forward=definition-time unknown=reject'
''')

# Make the old line-marker gate's "arbitrary-directive=not-hidden" claim real.
replace_once(
    "tests/compiler/c0/run-preprocessed-line-markers.sh",
    "test -s \"$work/preprocessed_line_markers.s\"\ngrep -F 'marker_value:' \"$work/preprocessed_line_markers.s\" >/dev/null\ngrep -F '  li a0, 7' \"$work/preprocessed_line_markers.s\" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/preprocessed_line_markers gcc-numeric=skip arbitrary-directive=not-hidden'",
    "test -s \"$work/preprocessed_line_markers.s\"\ngrep -F 'marker_value:' \"$work/preprocessed_line_markers.s\" >/dev/null\ngrep -F '  li a0, 7' \"$work/preprocessed_line_markers.s\" >/dev/null\n\nif \"$minic\" -S \"$root/tests/compiler/c0/invalid_preprocessor_directive.i\" -o \"$work/invalid.s\" >\"$work/invalid.stdout\" 2>\"$work/invalid.stderr\"; then\n    printf '%s\\n' 'FAIL compiler/c0/preprocessed_line_markers: arbitrary directive was hidden' >&2\n    exit 1\nfi\ngrep -F 'unsupported preprocessor directive' \"$work/invalid.stderr\" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/preprocessed_line_markers gcc-numeric=skip arbitrary-directive=not-hidden'",
)

run_sh = Path("tests/compiler/c0/run.sh")
run_text = run_sh.read_text()
needle = 'sh "$root/tests/compiler/c0/run-preprocessed-line-markers.sh"\n'
if run_text.count(needle) != 1:
    raise SystemExit(f"run.sh line-marker anchor mismatch: {run_text.count(needle)}")
run_text = run_text.replace(
    needle,
    needle
    + '\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-pragma-pack-record-layout.sh"\n',
    1,
)
run_sh.write_text(run_text)
