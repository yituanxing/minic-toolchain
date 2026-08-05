#include "frontend/lexer.h"

#include <stdio.h>
#include <string.h>

static MinicSourcePosition minic_lexer_position(const MinicLexer *lexer)
{
    MinicSourcePosition position;

    position.offset = lexer->cursor;
    position.line = lexer->line;
    position.column = lexer->column;
    return position;
}

static char minic_lexer_peek(const MinicLexer *lexer)
{
    if (lexer->cursor >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->cursor];
}

static char minic_lexer_peek_next(const MinicLexer *lexer)
{
    if (lexer->cursor + 1U >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->cursor + 1U];
}

static void minic_lexer_advance(MinicLexer *lexer)
{
    char character;

    if (lexer->cursor >= lexer->length) {
        return;
    }

    character = lexer->source[lexer->cursor];
    lexer->cursor += 1U;
    if (character == '\n') {
        lexer->line += 1U;
        lexer->column = 1U;
    } else {
        lexer->column += 1U;
    }
}

static bool minic_is_space(char character)
{
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

static bool minic_is_ascii_letter(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

static bool minic_is_identifier_start(char character)
{
    return minic_is_ascii_letter(character) || character == '_';
}

static bool minic_is_identifier_continue(char character)
{
    return minic_is_identifier_start(character) ||
           (character >= '0' && character <= '9');
}

static bool minic_is_decimal_digit(char character)
{
    return character >= '0' && character <= '9';
}

static MinicTokenKind minic_classify_identifier(
    const char *text,
    size_t length)
{
    if (length == 3U && memcmp(text, "int", 3U) == 0) {
        return MINIC_TOKEN_KW_INT;
    }
    if (length == 4U && memcmp(text, "void", 4U) == 0) {
        return MINIC_TOKEN_KW_VOID;
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
    return MINIC_TOKEN_IDENTIFIER;
}

static void minic_lexer_set_diagnostic(
    const MinicLexer *lexer,
    MinicDiagnostic *diagnostic,
    MinicSourcePosition position,
    char character)
{
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->path = lexer->path;
    diagnostic->line = position.line;
    diagnostic->column = position.column;
    if (character >= ' ' && character <= '~') {
        (void)snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "unexpected character '%c'",
            character);
    } else {
        (void)snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "unexpected byte 0x%02x",
            (unsigned int)(unsigned char)character);
    }
}

void minic_lexer_initialize(
    MinicLexer *lexer,
    const char *path,
    const char *source,
    size_t length)
{
    lexer->path = path;
    lexer->source = source;
    lexer->length = length;
    lexer->cursor = 0U;
    lexer->line = 1U;
    lexer->column = 1U;
}

bool minic_lexer_next(
    MinicLexer *lexer,
    MinicToken *token,
    MinicDiagnostic *diagnostic)
{
    MinicSourcePosition begin;
    char character;

    while (minic_is_space(minic_lexer_peek(lexer))) {
        minic_lexer_advance(lexer);
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

    if (minic_is_identifier_start(character)) {
        size_t start;

        start = lexer->cursor;
        do {
            minic_lexer_advance(lexer);
        } while (minic_is_identifier_continue(minic_lexer_peek(lexer)));
        token->kind = minic_classify_identifier(
            lexer->source + start,
            lexer->cursor - start);
        token->span.end = minic_lexer_position(lexer);
        return true;
    }

    if (minic_is_decimal_digit(character)) {
        do {
            minic_lexer_advance(lexer);
        } while (minic_is_decimal_digit(minic_lexer_peek(lexer)));
        token->kind = MINIC_TOKEN_INTEGER_CONSTANT;
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
    case ';':
        token->kind = MINIC_TOKEN_SEMICOLON;
        break;
    case ',':
        token->kind = MINIC_TOKEN_COMMA;
        break;
    case '+':
        token->kind = MINIC_TOKEN_PLUS;
        break;
    case '-':
        token->kind = MINIC_TOKEN_MINUS;
        break;
    case '*':
        token->kind = MINIC_TOKEN_STAR;
        break;
    case '&':
        token->kind = MINIC_TOKEN_AMPERSAND;
        break;
    case '/':
        token->kind = MINIC_TOKEN_SLASH;
        break;
    case '%':
        token->kind = MINIC_TOKEN_PERCENT;
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
    case '<':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_LESS_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_LESS;
        }
        break;
    case '>':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_GREATER_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_GREATER;
        }
        break;
    default:
        minic_lexer_advance(lexer);
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_diagnostic(lexer, diagnostic, begin, character);
        return false;
    }

    minic_lexer_advance(lexer);
    token->span.end = minic_lexer_position(lexer);
    return true;
}
