#include "frontend/lexer.h"

#include <stdio.h>
#include <string.h>

static char minic_lexer_peek(const MinicLexer *lexer) {
    if (lexer->cursor >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->cursor];
}

static char minic_lexer_peek_next(const MinicLexer *lexer) {
    if (lexer->cursor + 1U >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->cursor + 1U];
}

static void minic_lexer_advance(MinicLexer *lexer) {
    char current;

    if (lexer->cursor >= lexer->length) {
        return;
    }

    current = lexer->source[lexer->cursor];
    lexer->cursor += 1U;
    if (current == '\n') {
        lexer->line += 1U;
        lexer->column = 1U;
    } else {
        lexer->column += 1U;
    }
}

static MinicSourcePosition minic_lexer_position(const MinicLexer *lexer) {
    MinicSourcePosition position;

    position.offset = lexer->cursor;
    position.line = lexer->line;
    position.column = lexer->column;
    return position;
}

static bool minic_is_space(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

static bool minic_lexer_at_directive_start(const MinicLexer *lexer) {
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

static bool minic_lexer_skip_gcc_line_marker(MinicLexer *lexer) {
    size_t probe;

    if (lexer == NULL || !minic_lexer_at_directive_start(lexer)) {
        return false;
    }

    probe = lexer->cursor + 1U;
    while (probe < lexer->length && (lexer->source[probe] == ' ' || lexer->source[probe] == '\t')) {
        probe += 1U;
    }
    if (probe >= lexer->length || lexer->source[probe] < '0' || lexer->source[probe] > '9') {
        return false;
    }

    /* GCC -E emits numeric line-control markers in preprocessed .i files, e.g.
     * `# 1 "header.h" 1 3 4`. They are preprocessing metadata, not C tokens.
     * Only numeric markers at the beginning of a physical line are consumed;
     * arbitrary preprocessor directives remain errors instead of being hidden. */
    while (minic_lexer_peek(lexer) != '\0' && minic_lexer_peek(lexer) != '\n') {
        minic_lexer_advance(lexer);
    }
    if (minic_lexer_peek(lexer) == '\n') {
        minic_lexer_advance(lexer);
    }
    return true;
}

static bool minic_is_decimal_digit(char character) {
    return character >= '0' && character <= '9';
}

static bool minic_is_hexadecimal_digit(char character) {
    return minic_is_decimal_digit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

static bool minic_is_identifier_start(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

static bool minic_is_identifier_continue(char character) {
    return minic_is_identifier_start(character) || minic_is_decimal_digit(character);
}

static MinicTokenKind minic_classify_identifier(const char *text, size_t length) {
    if (length == 5U && memcmp(text, "_Bool", 5U) == 0) {
        return MINIC_TOKEN_KW_BOOL;
    }
    if (length == 14U && memcmp(text, "_Static_assert", 14U) == 0) {
        return MINIC_TOKEN_KW_STATIC_ASSERT;
    }
    if (length == 4U && memcmp(text, "char", 4U) == 0) {
        return MINIC_TOKEN_KW_CHAR;
    }
    if (length == 6U && memcmp(text, "double", 6U) == 0) {
        return MINIC_TOKEN_KW_DOUBLE;
    }
    if (length == 5U && memcmp(text, "float", 5U) == 0) {
        return MINIC_TOKEN_KW_FLOAT;
    }
    if (length == 6U && memcmp(text, "inline", 6U) == 0) {
        return MINIC_TOKEN_KW_INLINE;
    }
    if (length == 3U && memcmp(text, "int", 3U) == 0) {
        return MINIC_TOKEN_KW_INT;
    }
    if (length == 4U && memcmp(text, "long", 4U) == 0) {
        return MINIC_TOKEN_KW_LONG;
    }
    if (length == 5U && memcmp(text, "short", 5U) == 0) {
        return MINIC_TOKEN_KW_SHORT;
    }
    if ((length == 6U && memcmp(text, "signed", 6U) == 0) ||
        (length == 8U && memcmp(text, "__signed", 8U) == 0) ||
        (length == 10U && memcmp(text, "__signed__", 10U) == 0)) {
        return MINIC_TOKEN_KW_SIGNED;
    }
    if (length == 8U && memcmp(text, "unsigned", 8U) == 0) {
        return MINIC_TOKEN_KW_UNSIGNED;
    }
    if (length == 4U && memcmp(text, "void", 4U) == 0) {
        return MINIC_TOKEN_KW_VOID;
    }
    if (length == 6U && memcmp(text, "struct", 6U) == 0) {
        return MINIC_TOKEN_KW_STRUCT;
    }
    if (length == 4U && memcmp(text, "enum", 4U) == 0) {
        return MINIC_TOKEN_KW_ENUM;
    }
    if (length == 5U && memcmp(text, "union", 5U) == 0) {
        return MINIC_TOKEN_KW_UNION;
    }
    if (length == 5U && memcmp(text, "const", 5U) == 0) {
        return MINIC_TOKEN_KW_CONST;
    }
    if (length == 8U && memcmp(text, "volatile", 8U) == 0) {
        return MINIC_TOKEN_KW_VOLATILE;
    }
    if (length == 7U && memcmp(text, "typedef", 7U) == 0) {
        return MINIC_TOKEN_KW_TYPEDEF;
    }
    if (length == 6U && memcmp(text, "extern", 6U) == 0) {
        return MINIC_TOKEN_KW_EXTERN;
    }
    if (length == 6U && memcmp(text, "static", 6U) == 0) {
        return MINIC_TOKEN_KW_STATIC;
    }
    if (length == 6U && memcmp(text, "sizeof", 6U) == 0) {
        return MINIC_TOKEN_KW_SIZEOF;
    }
    if ((length == 8U && memcmp(text, "_Alignof", 8U) == 0) ||
        (length == 9U && memcmp(text, "__alignof", 9U) == 0) ||
        (length == 11U && memcmp(text, "__alignof__", 11U) == 0)) {
        return MINIC_TOKEN_KW_ALIGNOF;
    }
    if (length == 6U && memcmp(text, "return", 6U) == 0) {
        return MINIC_TOKEN_KW_RETURN;
    }
    if (length == 2U && memcmp(text, "if", 2U) == 0) {
        return MINIC_TOKEN_KW_IF;
    }
    if (length == 4U && memcmp(text, "else", 4U) == 0) {
        return MINIC_TOKEN_KW_ELSE;
    }
    if (length == 5U && memcmp(text, "while", 5U) == 0) {
        return MINIC_TOKEN_KW_WHILE;
    }
    if (length == 2U && memcmp(text, "do", 2U) == 0) {
        return MINIC_TOKEN_KW_DO;
    }
    if (length == 3U && memcmp(text, "for", 3U) == 0) {
        return MINIC_TOKEN_KW_FOR;
    }
    if (length == 6U && memcmp(text, "switch", 6U) == 0) {
        return MINIC_TOKEN_KW_SWITCH;
    }
    if (length == 4U && memcmp(text, "case", 4U) == 0) {
        return MINIC_TOKEN_KW_CASE;
    }
    if (length == 7U && memcmp(text, "default", 7U) == 0) {
        return MINIC_TOKEN_KW_DEFAULT;
    }
    if (length == 5U && memcmp(text, "break", 5U) == 0) {
        return MINIC_TOKEN_KW_BREAK;
    }
    if (length == 8U && memcmp(text, "continue", 8U) == 0) {
        return MINIC_TOKEN_KW_CONTINUE;
    }
    return MINIC_TOKEN_IDENTIFIER;
}

static void minic_lexer_set_diagnostic(const MinicLexer *lexer,
                                       MinicDiagnostic *diagnostic,
                                       MinicSourcePosition position,
                                       char character) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->path = lexer->path;
    diagnostic->line = position.line;
    diagnostic->column = position.column;
    if (character >= ' ' && character <= '~') {
        (void)snprintf(diagnostic->message,
                       sizeof(diagnostic->message),
                       "unexpected character '%c'",
                       character);
    } else {
        (void)snprintf(diagnostic->message,
                       sizeof(diagnostic->message),
                       "unexpected byte 0x%02x",
                       (unsigned int)(unsigned char)character);
    }
}

static void minic_lexer_set_message(const MinicLexer *lexer,
                                    MinicDiagnostic *diagnostic,
                                    MinicSourcePosition position,
                                    const char *message) {
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = lexer->path;
    diagnostic->line = position.line;
    diagnostic->column = position.column;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

static bool minic_lexer_scan_decimal_exponent(MinicLexer *lexer,
                                              MinicDiagnostic *diagnostic,
                                              MinicSourcePosition begin) {
    if (minic_lexer_peek(lexer) != 'e' && minic_lexer_peek(lexer) != 'E') {
        return true;
    }

    minic_lexer_advance(lexer);
    if (minic_lexer_peek(lexer) == '+' || minic_lexer_peek(lexer) == '-') {
        minic_lexer_advance(lexer);
    }
    if (!minic_is_decimal_digit(minic_lexer_peek(lexer))) {
        minic_lexer_set_message(lexer, diagnostic, begin, "expected decimal digit in exponent");
        return false;
    }
    do {
        minic_lexer_advance(lexer);
    } while (minic_is_decimal_digit(minic_lexer_peek(lexer)));
    return true;
}

static bool minic_lexer_scan_integer_suffix(MinicLexer *lexer,
                                            MinicDiagnostic *diagnostic,
                                            MinicSourcePosition begin) {
    unsigned int long_count;
    bool saw_unsigned;
    size_t suffix_count;

    long_count = 0U;
    saw_unsigned = false;
    suffix_count = 0U;
    while (minic_lexer_peek(lexer) == 'l' || minic_lexer_peek(lexer) == 'L' ||
           minic_lexer_peek(lexer) == 'u' || minic_lexer_peek(lexer) == 'U') {
        char suffix;

        suffix = minic_lexer_peek(lexer);
        if (suffix == 'l' || suffix == 'L') {
            long_count += 1U;
            if (long_count > 2U) {
                minic_lexer_set_message(lexer, diagnostic, begin, "too many long integer suffixes");
                return false;
            }
        } else {
            if (saw_unsigned) {
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "duplicate unsigned integer suffix");
                return false;
            }
            saw_unsigned = true;
        }
        suffix_count += 1U;
        if (suffix_count > 3U) {
            minic_lexer_set_message(
                lexer, diagnostic, begin, "unsupported integer constant suffix");
            return false;
        }
        minic_lexer_advance(lexer);
    }
    return true;
}

static bool minic_lexer_scan_string_literal(MinicLexer *lexer,
                                            MinicToken *token,
                                            MinicDiagnostic *diagnostic,
                                            MinicSourcePosition begin) {
    minic_lexer_advance(lexer);
    for (;;) {
        char character;

        character = minic_lexer_peek(lexer);
        if (character == '"') {
            minic_lexer_advance(lexer);
            token->kind = MINIC_TOKEN_STRING_LITERAL;
            token->span.end = minic_lexer_position(lexer);
            return true;
        }
        if (character == '\0') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(lexer, diagnostic, begin, "unterminated string literal");
            return false;
        }
        if (character == '\n' || character == '\r') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(
                lexer, diagnostic, minic_lexer_position(lexer), "newline in string literal");
            return false;
        }
        if (character == '\\') {
            minic_lexer_advance(lexer);
            character = minic_lexer_peek(lexer);
            if (character == '\0') {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(lexer, diagnostic, begin, "unterminated string literal");
                return false;
            }
            if (character == '\n' || character == '\r') {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(
                    lexer, diagnostic, minic_lexer_position(lexer), "newline in string literal");
                return false;
            }
        }
        minic_lexer_advance(lexer);
    }
}

static bool minic_lexer_scan_character_constant(MinicLexer *lexer,
                                                MinicToken *token,
                                                MinicDiagnostic *diagnostic,
                                                MinicSourcePosition begin) {
    char character;

    minic_lexer_advance(lexer);
    character = minic_lexer_peek(lexer);
    if (character == '\0') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
        return false;
    }
    if (character == '\n' || character == '\r') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
        return false;
    }
    if (character == '\'') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "empty character constant");
        return false;
    }
    if (character == '\\') {
        minic_lexer_advance(lexer);
        character = minic_lexer_peek(lexer);
        if (character == '\0') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
            return false;
        }
        if (character == '\n' || character == '\r') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(
                lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
            return false;
        }
        if (character == 'x') {
            minic_lexer_advance(lexer);
            if (!minic_is_hexadecimal_digit(minic_lexer_peek(lexer))) {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "hexadecimal character escape requires a digit");
                return false;
            }
            do {
                minic_lexer_advance(lexer);
            } while (minic_is_hexadecimal_digit(minic_lexer_peek(lexer)));
        } else if (character >= '0' && character <= '7') {
            unsigned int digit_count;

            digit_count = 0U;
            do {
                minic_lexer_advance(lexer);
                digit_count += 1U;
            } while (digit_count < 3U && minic_lexer_peek(lexer) >= '0' &&
                     minic_lexer_peek(lexer) <= '7');
        } else {
            minic_lexer_advance(lexer);
        }
    } else {
        minic_lexer_advance(lexer);
    }
    character = minic_lexer_peek(lexer);
    if (character == '\0') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
        return false;
    }
    if (character == '\n' || character == '\r') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
        return false;
    }
    if (character != '\'') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, begin, "multi-character constants are not supported yet");
        return false;
    }

    minic_lexer_advance(lexer);
    token->kind = MINIC_TOKEN_CHARACTER_CONSTANT;
    token->span.end = minic_lexer_position(lexer);
    return true;
}

void minic_lexer_initialize(MinicLexer *lexer,
                            const char *path,
                            const char *source,
                            size_t length) {
    lexer->path = path;
    lexer->source = source;
    lexer->length = length;
    lexer->cursor = 0U;
    lexer->line = 1U;
    lexer->column = 1U;
}

bool minic_lexer_next(MinicLexer *lexer, MinicToken *token, MinicDiagnostic *diagnostic) {
    MinicSourcePosition begin;
    char character;

    for (;;) {
        while (minic_is_space(minic_lexer_peek(lexer))) {
            minic_lexer_advance(lexer);
        }
        if (!minic_lexer_skip_gcc_line_marker(lexer)) {
            break;
        }
    }

    begin = minic_lexer_position(lexer);
    token->kind = MINIC_TOKEN_INVALID;
    token->span.begin = begin;
    token->span.end = begin;
    character = minic_lexer_peek(lexer);

    if (character == '\0') {
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

    if (minic_is_identifier_start(character)) {
        size_t start;

        start = lexer->cursor;
        do {
            minic_lexer_advance(lexer);
        } while (minic_is_identifier_continue(minic_lexer_peek(lexer)));
        token->kind = minic_classify_identifier(lexer->source + start, lexer->cursor - start);
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    if (character == '"') {
        return minic_lexer_scan_string_literal(lexer, token, diagnostic, begin);
    }
    if (character == '\'') {
        return minic_lexer_scan_character_constant(lexer, token, diagnostic, begin);
    }

    if (character == '.' && minic_lexer_peek_next(lexer) == '.' &&
        lexer->cursor + 2U < lexer->length && lexer->source[lexer->cursor + 2U] == '.') {
        minic_lexer_advance(lexer);
        minic_lexer_advance(lexer);
        minic_lexer_advance(lexer);
        token->kind = MINIC_TOKEN_ELLIPSIS;
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    if (character == '.' && minic_is_decimal_digit(minic_lexer_peek_next(lexer))) {
        minic_lexer_advance(lexer);
        do {
            minic_lexer_advance(lexer);
        } while (minic_is_decimal_digit(minic_lexer_peek(lexer)));
        if (!minic_lexer_scan_decimal_exponent(lexer, diagnostic, begin)) {
            token->span.end = minic_lexer_position(lexer);
            return false;
        }
        token->kind = MINIC_TOKEN_FLOATING_CONSTANT;
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    if (minic_is_decimal_digit(character)) {
        bool is_floating;

        is_floating = false;
        if (character == '0' &&
            (minic_lexer_peek_next(lexer) == 'x' || minic_lexer_peek_next(lexer) == 'X')) {
            minic_lexer_advance(lexer);
            minic_lexer_advance(lexer);
            if (!minic_is_hexadecimal_digit(minic_lexer_peek(lexer))) {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "expected hexadecimal digit after 0x");
                return false;
            }
            do {
                minic_lexer_advance(lexer);
            } while (minic_is_hexadecimal_digit(minic_lexer_peek(lexer)));
        } else {
            do {
                minic_lexer_advance(lexer);
            } while (minic_is_decimal_digit(minic_lexer_peek(lexer)));
            if (minic_lexer_peek(lexer) == '.') {
                is_floating = true;
                minic_lexer_advance(lexer);
                while (minic_is_decimal_digit(minic_lexer_peek(lexer))) {
                    minic_lexer_advance(lexer);
                }
            }
            if (minic_lexer_peek(lexer) == 'e' || minic_lexer_peek(lexer) == 'E') {
                is_floating = true;
                if (!minic_lexer_scan_decimal_exponent(lexer, diagnostic, begin)) {
                    token->span.end = minic_lexer_position(lexer);
                    return false;
                }
            }
        }
        if (!is_floating && !minic_lexer_scan_integer_suffix(lexer, diagnostic, begin)) {
            token->span.end = minic_lexer_position(lexer);
            return false;
        }
        token->kind = is_floating ? MINIC_TOKEN_FLOATING_CONSTANT : MINIC_TOKEN_INTEGER_CONSTANT;
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    switch (character) {
    case '(':
        token->kind = MINIC_TOKEN_LPAREN;
        break;
    case ')':
        token->kind = MINIC_TOKEN_RPAREN;
        break;
    case '{':
        token->kind = MINIC_TOKEN_LBRACE;
        break;
    case '}':
        token->kind = MINIC_TOKEN_RBRACE;
        break;
    case '[':
        token->kind = MINIC_TOKEN_LBRACKET;
        break;
    case ']':
        token->kind = MINIC_TOKEN_RBRACKET;
        break;
    case ';':
        token->kind = MINIC_TOKEN_SEMICOLON;
        break;
    case ',':
        token->kind = MINIC_TOKEN_COMMA;
        break;
    case '?':
        token->kind = MINIC_TOKEN_QUESTION;
        break;
    case ':':
        token->kind = MINIC_TOKEN_COLON;
        break;
    case '.':
        token->kind = MINIC_TOKEN_DOT;
        break;
    case '+':
        if (minic_lexer_peek_next(lexer) == '+') {
            token->kind = MINIC_TOKEN_PLUS_PLUS;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_PLUS_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_PLUS;
        }
        break;
    case '-':
        if (minic_lexer_peek_next(lexer) == '-') {
            token->kind = MINIC_TOKEN_MINUS_MINUS;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '>') {
            token->kind = MINIC_TOKEN_ARROW;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_MINUS_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_MINUS;
        }
        break;
    case '*':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_STAR_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_STAR;
        }
        break;
    case '&':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_AMPERSAND_EQUAL;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '&') {
            token->kind = MINIC_TOKEN_AMPERSAND_AMPERSAND;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_AMPERSAND;
        }
        break;
    case '|':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_PIPE_EQUAL;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '|') {
            token->kind = MINIC_TOKEN_PIPE_PIPE;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_PIPE;
        }
        break;
    case '^':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_CARET_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_CARET;
        }
        break;
    case '/':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_SLASH_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_SLASH;
        }
        break;
    case '%':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_PERCENT_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_PERCENT;
        }
        break;
    case '=':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_EQUAL_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_EQUAL;
        }
        break;
    case '!':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_BANG_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_BANG;
        }
        break;
    case '~':
        token->kind = MINIC_TOKEN_TILDE;
        break;
    case '<':
        if (minic_lexer_peek_next(lexer) == '<' && lexer->cursor + 2U < lexer->length &&
            lexer->source[lexer->cursor + 2U] == '=') {
            token->kind = MINIC_TOKEN_LESS_LESS_EQUAL;
            minic_lexer_advance(lexer);
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '<') {
            token->kind = MINIC_TOKEN_LESS_LESS;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_LESS_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_LESS;
        }
        break;
    case '>':
        if (minic_lexer_peek_next(lexer) == '>' && lexer->cursor + 2U < lexer->length &&
            lexer->source[lexer->cursor + 2U] == '=') {
            token->kind = MINIC_TOKEN_GREATER_GREATER_EQUAL;
            minic_lexer_advance(lexer);
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '>') {
            token->kind = MINIC_TOKEN_GREATER_GREATER;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_GREATER_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_GREATER;
        }
        break;
    default:
        minic_lexer_set_diagnostic(lexer, diagnostic, begin, character);
        return false;
    }

    minic_lexer_advance(lexer);
    token->span.end = minic_lexer_position(lexer);
    return true;
}
