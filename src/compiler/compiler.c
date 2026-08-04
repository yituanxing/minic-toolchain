#include "minic/compiler.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicSourceBuffer {
    char *data;
    size_t size;
} MinicSourceBuffer;

typedef struct MinicParser {
    const char *path;
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    size_t column;
    MinicDiagnostic *diagnostic;
} MinicParser;

static void minic_set_diagnostic(
    MinicDiagnostic *diagnostic,
    const char *path,
    size_t line,
    size_t column,
    const char *format,
    ...)
{
    va_list arguments;

    if (diagnostic == NULL) {
        return;
    }

    diagnostic->path = path;
    diagnostic->line = line;
    diagnostic->column = column;

    va_start(arguments, format);
    (void)vsnprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        format,
        arguments);
    va_end(arguments);
}

static bool minic_read_file(
    const char *path,
    MinicSourceBuffer *buffer,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    long end_position;
    size_t size;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot open input: %s",
            strerror(errno));
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot seek input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    end_position = ftell(file);
    if (end_position < 0L) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot measure input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    if ((unsigned long)end_position > (unsigned long)(SIZE_MAX - 1U)) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "input is too large");
        (void)fclose(file);
        return false;
    }

    size = (size_t)end_position;
    if (fseek(file, 0L, SEEK_SET) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot rewind input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    data = (char *)malloc(size + 1U);
    if (data == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "out of memory while reading input");
        (void)fclose(file);
        return false;
    }

    if (size != 0U && fread(data, 1U, size, file) != size) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot read input: %s",
            ferror(file) != 0 ? strerror(errno) : "unexpected end of file");
        free(data);
        (void)fclose(file);
        return false;
    }

    data[size] = '\0';
    if (fclose(file) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot close input: %s",
            strerror(errno));
        free(data);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    return true;
}

static char minic_parser_peek(const MinicParser *parser)
{
    if (parser->cursor >= parser->length) {
        return '\0';
    }
    return parser->source[parser->cursor];
}

static void minic_parser_advance(MinicParser *parser)
{
    char character;

    if (parser->cursor >= parser->length) {
        return;
    }

    character = parser->source[parser->cursor];
    parser->cursor += 1U;
    if (character == '\n') {
        parser->line += 1U;
        parser->column = 1U;
    } else {
        parser->column += 1U;
    }
}

static void minic_parser_skip_space(MinicParser *parser)
{
    while (isspace((unsigned char)minic_parser_peek(parser)) != 0) {
        minic_parser_advance(parser);
    }
}

static bool minic_is_identifier_continue(char character)
{
    unsigned char value;

    value = (unsigned char)character;
    return isalnum(value) != 0 || character == '_';
}

static bool minic_parser_match_keyword(
    MinicParser *parser,
    const char *keyword)
{
    size_t keyword_length;

    minic_parser_skip_space(parser);
    keyword_length = strlen(keyword);
    if (keyword_length > parser->length - parser->cursor) {
        return false;
    }
    if (memcmp(parser->source + parser->cursor, keyword, keyword_length) != 0) {
        return false;
    }
    if (parser->cursor + keyword_length < parser->length &&
        minic_is_identifier_continue(parser->source[parser->cursor + keyword_length])) {
        return false;
    }

    while (keyword_length != 0U) {
        minic_parser_advance(parser);
        keyword_length -= 1U;
    }
    return true;
}

static bool minic_parser_expect_keyword(
    MinicParser *parser,
    const char *keyword)
{
    if (minic_parser_match_keyword(parser, keyword)) {
        return true;
    }

    minic_set_diagnostic(
        parser->diagnostic,
        parser->path,
        parser->line,
        parser->column,
        "expected keyword '%s'",
        keyword);
    return false;
}

static bool minic_parser_match_character(
    MinicParser *parser,
    char expected)
{
    minic_parser_skip_space(parser);
    if (minic_parser_peek(parser) != expected) {
        return false;
    }
    minic_parser_advance(parser);
    return true;
}

static bool minic_parser_expect_character(
    MinicParser *parser,
    char expected)
{
    if (minic_parser_match_character(parser, expected)) {
        return true;
    }

    minic_set_diagnostic(
        parser->diagnostic,
        parser->path,
        parser->line,
        parser->column,
        "expected '%c'",
        expected);
    return false;
}

static bool minic_parser_parse_integer(
    MinicParser *parser,
    int *value)
{
    unsigned long result;
    char character;

    minic_parser_skip_space(parser);
    character = minic_parser_peek(parser);
    if (isdigit((unsigned char)character) == 0) {
        minic_set_diagnostic(
            parser->diagnostic,
            parser->path,
            parser->line,
            parser->column,
            "expected decimal integer constant");
        return false;
    }

    result = 0UL;
    while (isdigit((unsigned char)minic_parser_peek(parser)) != 0) {
        unsigned long digit;

        digit = (unsigned long)(unsigned int)(minic_parser_peek(parser) - '0');
        if (result > ((unsigned long)INT_MAX - digit) / 10UL) {
            minic_set_diagnostic(
                parser->diagnostic,
                parser->path,
                parser->line,
                parser->column,
                "integer constant exceeds C0 int range");
            return false;
        }
        result = result * 10UL + digit;
        minic_parser_advance(parser);
    }

    *value = (int)result;
    return true;
}

static bool minic_parse_c0_translation_unit(
    MinicParser *parser,
    int *return_value)
{
    if (!minic_parser_expect_keyword(parser, "int") ||
        !minic_parser_expect_keyword(parser, "main") ||
        !minic_parser_expect_character(parser, '(')) {
        return false;
    }

    if (!minic_parser_match_character(parser, ')')) {
        if (!minic_parser_expect_keyword(parser, "void") ||
            !minic_parser_expect_character(parser, ')')) {
            return false;
        }
    }

    if (!minic_parser_expect_character(parser, '{')) {
        return false;
    }

    *return_value = 0;
    minic_parser_skip_space(parser);
    if (minic_parser_peek(parser) != '}') {
        if (!minic_parser_expect_keyword(parser, "return") ||
            !minic_parser_parse_integer(parser, return_value) ||
            !minic_parser_expect_character(parser, ';')) {
            return false;
        }
    }

    if (!minic_parser_expect_character(parser, '}')) {
        return false;
    }

    minic_parser_skip_space(parser);
    if (minic_parser_peek(parser) != '\0') {
        minic_set_diagnostic(
            parser->diagnostic,
            parser->path,
            parser->line,
            parser->column,
            "unexpected input after main function");
        return false;
    }

    return true;
}

static bool minic_write_riscv_assembly(
    const char *path,
    int return_value,
    MinicDiagnostic *diagnostic)
{
    FILE *file;

    file = fopen(path, "wb");
    if (file == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot open output: %s",
            strerror(errno));
        return false;
    }

    if (fprintf(
            file,
            ".text\n"
            ".globl main\n"
            ".type main, @function\n"
            "main:\n"
            "  li a0, %d\n"
            "  ret\n"
            ".size main, .-main\n",
            return_value) < 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot write output: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    if (fclose(file) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot close output: %s",
            strerror(errno));
        return false;
    }

    return true;
}

int minic_compile_preprocessed_file(
    const char *input_path,
    const char *output_path,
    MinicDiagnostic *diagnostic)
{
    MinicSourceBuffer buffer;
    MinicParser parser;
    int return_value;
    bool success;

    if (input_path == NULL || output_path == NULL) {
        minic_set_diagnostic(
            diagnostic,
            input_path,
            1U,
            1U,
            "input and output paths are required");
        return 1;
    }

    if (diagnostic != NULL) {
        diagnostic->path = input_path;
        diagnostic->line = 1U;
        diagnostic->column = 1U;
        diagnostic->message[0] = '\0';
    }

    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    parser.path = input_path;
    parser.source = buffer.data;
    parser.length = buffer.size;
    parser.cursor = 0U;
    parser.line = 1U;
    parser.column = 1U;
    parser.diagnostic = diagnostic;

    success = minic_parse_c0_translation_unit(&parser, &return_value);
    if (success) {
        success = minic_write_riscv_assembly(
            output_path,
            return_value,
            diagnostic);
    }

    free(buffer.data);
    return success ? 0 : 1;
}
